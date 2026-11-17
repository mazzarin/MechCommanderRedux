// Built without the precompiled header: windows.h and MCCore's Win32 stand-ins can't meet.
#include "MCMemorySystem.h"

#ifdef _WIN32

#include "MCWinHttp.h"

#include <SDL3/SDL_log.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <wrl/client.h>

// Port-only: Vulkan's types alone (SDL's copy of Khronos' headers), its functions looked up from the loader SDL uses.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#pragma comment(lib, "dxgi.lib")

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;

    struct MCKernelCloser
    {
        void operator()(HANDLE handle) const { CloseHandle(handle); }
    };

    /// <summary>A kernel object's handle, closed with it (none: null).</summary>
    using MCKernelHandle = std::unique_ptr<void, MCKernelCloser>;

    struct MCLibraryCloser
    {
        void operator()(HMODULE library) const { FreeLibrary(library); }
    };

    /// <summary>A library loaded, freed with the handle.</summary>
    using MCLibraryHandle = std::unique_ptr<std::remove_pointer_t<HMODULE>, MCLibraryCloser>;

    /// <summary>A Vulkan instance of the game's own, destroyed with this (nothing without its destroy function).</summary>
    struct MCVulkanInstance
    {
        VkInstance Instance = VK_NULL_HANDLE;
        PFN_vkDestroyInstance Destroy = nullptr;

        MCVulkanInstance(VkInstance instance, PFN_vkDestroyInstance destroy) : Instance(instance), Destroy(destroy) {}
        ~MCVulkanInstance()
        {
            if (Destroy != nullptr)
            {
                Destroy(Instance, nullptr);
            }
        }

        MCVulkanInstance(const MCVulkanInstance&) = delete;
        MCVulkanInstance& operator=(const MCVulkanInstance&) = delete;
    };

    /// <summary>
    /// SDL's rank of a Vulkan device's type, as it picks the device for high performance (SDL_gpu_vulkan.c's
    /// DEVICE_PRIORITY_HIGHPERFORMANCE, by VkPhysicalDeviceType).
    /// </summary>
    constexpr std::array<uint32_t, 5> VulkanTypeRanks{0, 3, 4, 2, 1};

    /// <summary>
    /// The LUID of the Vulkan device SDL's GPU device named <paramref name="name"/> runs on, which DXGI names its
    /// adapter by: of the devices of that name, the first of the best type, as SDL picks among devices it can use
    /// (VULKAN_INTERNAL_GetDeviceRank: a later device of a type ranks below the first's type and memory); none when
    /// Vulkan or the device can't say. SDL doesn't hand out its device, so the device is found again through an
    /// instance of the game's own, once. Two devices of one name SDL can't both use would be told apart wrongly; DXGI's
    /// description is the name's fallback.
    /// </summary>
    std::optional<LUID> VulkanLuid(std::string_view name)
    {
        // SDL has loaded the loader already: this takes another reference to it.
        const MCLibraryHandle loader(LoadLibraryW(L"vulkan-1.dll"));

        if (loader == nullptr)
        {
            SDL_Log("HD caches: no Vulkan loader (error %lu): the adapter is found by its name", GetLastError());
            return std::nullopt;
        }

        const auto getProc =
            reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader.get(), "vkGetInstanceProcAddr"));
        const auto createInstance =
            getProc != nullptr ? reinterpret_cast<PFN_vkCreateInstance>(getProc(nullptr, "vkCreateInstance")) : nullptr;

        if (createInstance == nullptr)
        {
            SDL_Log("HD caches: the Vulkan loader has no vkCreateInstance: the adapter is found by its name");
            return std::nullopt;
        }

        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &application;
        VkInstance instance = VK_NULL_HANDLE;

        if (const VkResult made = createInstance(&info, nullptr, &instance); made != VK_SUCCESS)
        {
            SDL_Log("HD caches: no Vulkan instance (%d): the adapter is found by its name", static_cast<int>(made));
            return std::nullopt;
        }

        const MCVulkanInstance owned{instance,
                                     reinterpret_cast<PFN_vkDestroyInstance>(getProc(instance, "vkDestroyInstance"))};
        const auto enumerate =
            reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(getProc(instance, "vkEnumeratePhysicalDevices"));
        const auto properties =
            reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(getProc(instance, "vkGetPhysicalDeviceProperties2"));
        std::optional<LUID> found;
        uint32_t foundRank = 0;
        uint32_t count = 0;

        if (enumerate == nullptr || properties == nullptr || enumerate(instance, &count, nullptr) != VK_SUCCESS)
        {
            SDL_Log("HD caches: Vulkan lists no devices: the adapter is found by its name");
            return std::nullopt;
        }

        std::vector<VkPhysicalDevice> devices(count);

        if (enumerate(instance, &count, devices.data()) < VK_SUCCESS)
        {
            SDL_Log("HD caches: Vulkan's devices can't be listed: the adapter is found by its name");
            return std::nullopt;
        }

        devices.resize(count);

        for (VkPhysicalDevice device : devices)
        {
            VkPhysicalDeviceIDProperties id{};
            id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
            VkPhysicalDeviceProperties2 described{};
            described.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            described.pNext = &id;
            properties(device, &described);

            if (name != std::string_view(described.properties.deviceName) || id.deviceLUIDValid == VK_FALSE)
            {
                continue;
            }

            const auto type = static_cast<size_t>(described.properties.deviceType);
            const uint32_t rank = type < VulkanTypeRanks.size() ? VulkanTypeRanks[type] : 0;

            if (!found.has_value() || rank > foundRank)
            {
                LUID luid{};
                static_assert(sizeof(luid) == VK_LUID_SIZE);
                std::memcpy(&luid, id.deviceLUID, sizeof(luid));
                found = luid;
                foundRank = rank;
            }
        }

        if (!found.has_value())
        {
            SDL_Log("HD caches: Vulkan names no device \"%.*s\" with a LUID: the adapter is found by its name",
                    static_cast<int>(name.size()), name.data());
        }

        return found;
    }

    /// <summary>
    /// The game's committed private pages that are write-combined: the GPU driver's memory mapped for the CPU, which
    /// private bytes count and DXGI's figures count again (on the A10, 17 Nov: the card's local and non-local use less
    /// 88 MB, all through a run). A walk of the game's own address space, a VirtualQuery a region.
    /// </summary>
    uint64_t MappedGpuBytes()
    {
        uint64_t bytes = 0;
        uintptr_t address = 0;
        MEMORY_BASIC_INFORMATION region{};

        while (VirtualQuery(reinterpret_cast<LPCVOID>(address), &region, sizeof(region)) == sizeof(region))
        {
            if (region.State == MEM_COMMIT && region.Type == MEM_PRIVATE && (region.Protect & PAGE_WRITECOMBINE) != 0)
            {
                bytes += region.RegionSize;
            }

            const uintptr_t next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;

            if (next <= address)
            {
                break;
            }

            address = next;
        }

        return bytes;
    }

    /// <summary>
    /// The GPU's mapped pages (MappedGpuBytes), walked once a second on a thread of their own from the first time
    /// they're asked (a frame log's first line): a walk took 1.55 ms on the A10 (17 Nov), too long for the game's thread.
    /// </summary>
    class MCMappedGpuPages
    {
    public:
        /// <summary>The last walk's bytes (0 until the first is done), the walks started if they weren't.</summary>
        uint64_t Last()
        {
            std::call_once(_Started, [this] { _Walker = std::jthread([this](std::stop_token stop) { Walk(stop); }); });
            return _Bytes.load(std::memory_order_relaxed);
        }

    private:
        static constexpr std::chrono::seconds WalkEvery{1};

        void Walk(const std::stop_token& stop)
        {
            while (!stop.stop_requested())
            {
                _Bytes.store(MappedGpuBytes(), std::memory_order_relaxed);
                std::unique_lock held(_Lock);
                _Wake.wait_for(held, stop, WalkEvery, [] { return false; });
            }
        }

        std::atomic<uint64_t> _Bytes = 0;
        std::once_flag _Started;
        std::mutex _Lock;
        std::condition_variable_any _Wake;
        /// <summary>Last: stopped and joined before the rest it reads goes.</summary>
        std::jthread _Walker;
    };

    /// <summary>
    /// Windows' answers: short from the system's low-memory notification (by default set while less than about 32 MB a
    /// 4 GB of memory is available, 64 MB at most) until its high-memory one (three times as much), so a frame's trim
    /// doesn't end it only for the next frame read to start it again. The available memory itself isn't counted: it
    /// takes in the system's standby list (its file cache), and any mark on it would be ours, not the system's. On the
    /// card, DXGI's video memory budget for this process on the adapter SDL's Vulkan device runs on, against what the
    /// process uses there (Vulkan's allocations among it).
    /// </summary>
    class MCWinMemory final : public MCMemoryRoom::MCSystem
    {
    public:
        MCWinMemory()
            : _Low(CreateMemoryResourceNotification(LowMemoryResourceNotification))
            , _High(CreateMemoryResourceNotification(HighMemoryResourceNotification))
        {
            if (_Low == nullptr || _High == nullptr)
            {
                SDL_Log("HD caches: no memory resource notification (error %lu): the low one alone, or none, is heeded",
                        GetLastError());
            }
        }

        bool Short() override
        {
            const std::optional<bool> low = Signalled(_Low.get(), "low");
            const std::optional<bool> high = Signalled(_High.get(), "high");

            if (low.value_or(false))
            {
                _Short = true;
            }
            else if (high.value_or(true))
            {
                _Short = false;
            }

            return _Short;
        }

        std::optional<MCMemoryRoom::MCCard> Card(std::string_view deviceName) override
        {
            if (!_Looked || deviceName != _DeviceName)
            {
                _Looked = true;
                _DeviceName = std::string(deviceName);
                ComPtr<IDXGIAdapter3> found = AdapterNamed(deviceName);
                std::scoped_lock lock(_AdapterLock);
                _Adapter = std::move(found);
            }

            const ComPtr<IDXGIAdapter3> adapter = Adapter();

            if (adapter.Get() == nullptr)
            {
                return std::nullopt;
            }

            // A card that shares the machine's memory has it all in its local segment, and nothing non-local.
            DXGI_QUERY_VIDEO_MEMORY_INFO local{};
            DXGI_QUERY_VIDEO_MEMORY_INFO borrowed{};
            HRESULT result = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);

            if (SUCCEEDED(result))
            {
                result = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &borrowed);
            }

            if (FAILED(result))
            {
                // Asked again at the next reading.
                LogOnce(_QueryLogged, std::format("HD caches: DXGI can't say the video memory of \"{}\" (0x{:08x})",
                                                  _DeviceName, static_cast<uint32_t>(result)));
                return std::nullopt;
            }

            return MCMemoryRoom::MCCard{
                .Budget = local.Budget, .Used = local.CurrentUsage, .SharesMemory = borrowed.Budget == 0};
        }

        std::optional<MCMemoryLedger::MCProcessFigures> Process() override
        {
            PROCESS_MEMORY_COUNTERS_EX counters{};
            counters.cb = sizeof(counters);
            MCMemoryLedger::MCProcessFigures figures;

            if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                     sizeof(counters)))
            {
                figures.Footprint = counters.PrivateUsage;
                figures.GpuMapped = _MappedPages.Last();
            }
            else
            {
                LogOnce(_ProcessLogged,
                        std::format("HD caches: the game's private bytes can't be read (error {})", GetLastError()));
            }

            DXGI_QUERY_VIDEO_MEMORY_INFO local{};
            DXGI_QUERY_VIDEO_MEMORY_INFO borrowed{};

            // The adapter Card found (the renderer's first frame asks for it): until then, none.
            if (const ComPtr<IDXGIAdapter3> adapter = Adapter();
                adapter.Get() != nullptr &&
                SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) &&
                SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &borrowed)))
            {
                figures.GpuLocal = local.CurrentUsage;
                figures.GpuNonLocal = borrowed.CurrentUsage;
            }

            return figures;
        }

    private:
        /// <summary>The adapter found (null: none yet, or none), held while it's asked.</summary>
        ComPtr<IDXGIAdapter3> Adapter()
        {
            std::scoped_lock lock(_AdapterLock);
            return _Adapter;
        }

        /// <summary>Logs <paramref name="text"/> unless <paramref name="logged"/> says it was, which it then does.</summary>
        static void LogOnce(bool& logged, const std::string& text)
        {
            if (!logged)
            {
                logged = true;
                SDL_Log("%s", text.c_str());
            }
        }

        /// <summary>Whether the notification <paramref name="handle"/> is set; none when there's none or it can't say.</summary>
        std::optional<bool> Signalled(HANDLE handle, std::string_view which)
        {
            BOOL set = FALSE;

            if (handle == nullptr)
            {
                return std::nullopt;
            }

            if (!QueryMemoryResourceNotification(handle, &set))
            {
                LogOnce(_NotificationLogged,
                        std::format("HD caches: the {} memory notification can't be read (error {})", which,
                                    GetLastError()));
                return std::nullopt;
            }

            return set != FALSE;
        }

        /// <summary>
        /// The adapter SDL's Vulkan device named <paramref name="name"/> runs on: the one of its LUID (VulkanLuid); else
        /// the hardware adapter DXGI names so (Vulkan's device name and DXGI's description are usually the driver's one
        /// name; of two the same, the one with more memory of its own), else the one SDL's driver picks first, a card
        /// with memory of its own before one sharing the machine's, the one with the most; none without DXGI 1.4
        /// (Windows 10's).
        /// </summary>
        ComPtr<IDXGIAdapter3> AdapterNamed(std::string_view name)
        {
            ComPtr<IDXGIFactory1> factory;

            if (const HRESULT made = CreateDXGIFactory1(IID_PPV_ARGS(&factory)); FAILED(made))
            {
                SDL_Log("HD caches: no DXGI factory (0x%08x): the GPU's frames follow the memory's notifications alone",
                        static_cast<uint32_t>(made));
                return {};
            }

            // The adapter of the Vulkan device's LUID: Vulkan's name and DXGI's description needn't be the same.
            ComPtr<IDXGIFactory4> byLuid;

            if (const std::optional<LUID> luid = VulkanLuid(name); luid.has_value() && SUCCEEDED(factory.As(&byLuid)))
            {
                ComPtr<IDXGIAdapter3> adapter;

                const HRESULT found = byLuid->EnumAdapterByLuid(*luid, IID_PPV_ARGS(&adapter));

                if (SUCCEEDED(found))
                {
                    return adapter;
                }

                SDL_Log("HD caches: DXGI has no adapter of \"%.*s\"'s LUID (0x%08x): it is found by its name",
                        static_cast<int>(name.size()), name.data(), static_cast<uint32_t>(found));
            }

            ComPtr<IDXGIAdapter1> named;
            ComPtr<IDXGIAdapter1> largest;
            SIZE_T namedMemory = 0;
            SIZE_T largestMemory = 0;
            std::string largestName;

            for (UINT index = 0;; ++index)
            {
                ComPtr<IDXGIAdapter1> adapter;
                const HRESULT found = factory->EnumAdapters1(index, &adapter);

                // Past the last adapter, or DXGI failing: either way no later index answers.
                if (FAILED(found))
                {
                    if (found != DXGI_ERROR_NOT_FOUND)
                    {
                        SDL_Log("HD caches: DXGI's adapter %u can't be listed (0x%08x)", index,
                                static_cast<uint32_t>(found));
                    }

                    break;
                }

                DXGI_ADAPTER_DESC1 description{};

                if (const HRESULT described = adapter->GetDesc1(&description); FAILED(described))
                {
                    SDL_Log("HD caches: DXGI's adapter %u can't be described (0x%08x)", index,
                            static_cast<uint32_t>(described));
                    continue;
                }

                if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                {
                    continue;
                }

                const SIZE_T memory = description.DedicatedVideoMemory;

                if (MCWinHttp::Narrow(description.Description) == name &&
                    (named.Get() == nullptr || memory > namedMemory))
                {
                    named = adapter;
                    namedMemory = memory;
                }

                if (largest.Get() == nullptr || memory > largestMemory)
                {
                    largest = adapter;
                    largestMemory = memory;
                    largestName = MCWinHttp::Narrow(description.Description);
                }
            }

            if (named.Get() == nullptr && largest.Get() == nullptr)
            {
                SDL_Log("HD caches: DXGI lists no adapter: the GPU's frames follow the memory's notifications alone");
                return {};
            }

            if (named.Get() == nullptr)
            {
                SDL_Log("HD caches: DXGI names no adapter \"%.*s\": reading \"%s\", the one with the most memory",
                        static_cast<int>(name.size()), name.data(), largestName.c_str());
            }

            ComPtr<IDXGIAdapter3> adapter;

            if (const HRESULT cast = (named.Get() != nullptr ? named : largest).As(&adapter); FAILED(cast))
            {
                SDL_Log("HD caches: DXGI can't say an adapter's video memory before Windows 10 (0x%08x)",
                        static_cast<uint32_t>(cast));
                return {};
            }

            return adapter;
        }

        MCKernelHandle _Low;
        MCKernelHandle _High;
        bool _Short = false;
        /// <summary>The device looked for (once a name), and its adapter (null: none).</summary>
        bool _Looked = false;
        std::string _DeviceName;
        /// <summary>
        /// The adapter found, under its lock: Card (the renderer's reader) sets it, Process (the frame log's line) reads
        /// it, on threads that needn't be one.
        /// </summary>
        std::mutex _AdapterLock;
        ComPtr<IDXGIAdapter3> _Adapter;
        bool _QueryLogged = false;
        bool _NotificationLogged = false;
        bool _ProcessLogged = false;
        /// <summary>Last: its walks stopped before the rest goes.</summary>
        MCMappedGpuPages _MappedPages;
    };
}

std::shared_ptr<MCMemoryRoom::MCSystem> MCMakeMemorySystem()
{
    return std::make_shared<MCWinMemory>();
}

#endif

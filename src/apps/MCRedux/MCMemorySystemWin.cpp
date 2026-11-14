// Built without the precompiled header: windows.h and MCCore's Win32 stand-ins can't meet.
#include "MCMemorySystem.h"

#ifdef _WIN32

#include "MCWinHttp.h"

#include <SDL3/SDL_log.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#pragma comment(lib, "dxgi.lib")

#include <format>
#include <memory>
#include <string>

namespace
{
    using Microsoft::WRL::ComPtr;

    struct MCKernelCloser
    {
        void operator()(HANDLE handle) const { CloseHandle(handle); }
    };

    /// <summary>A kernel object's handle, closed with it (none: null).</summary>
    using MCKernelHandle = std::unique_ptr<void, MCKernelCloser>;

    /// <summary>
    /// Windows' answers: short from the system's low-memory notification (set where Windows starts taking memory back
    /// from the programs running) until its high-memory one (three times as much available, by default), so a frame's
    /// trim doesn't end it only for the next frame read to start it again; and on the card DXGI's video memory budget
    /// for this process on the adapter SDL's Vulkan device runs on, against what the process uses there (Vulkan's
    /// allocations among it).
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
                _Adapter = AdapterNamed(deviceName);
            }

            if (_Adapter.Get() == nullptr)
            {
                return std::nullopt;
            }

            // A card that shares the machine's memory has it all in its local segment, and nothing non-local.
            DXGI_QUERY_VIDEO_MEMORY_INFO local{};
            DXGI_QUERY_VIDEO_MEMORY_INFO borrowed{};
            HRESULT result = _Adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);

            if (SUCCEEDED(result))
            {
                result = _Adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &borrowed);
            }

            if (FAILED(result))
            {
                // The adapter may have gone (a driver's update): looked for again at the next reading.
                LogOnce(_QueryLogged, std::format("HD caches: DXGI can't say the video memory of \"{}\" (0x{:08x})",
                                                  _DeviceName, static_cast<uint32_t>(result)));
                _Looked = false;
                _Adapter.Reset();
                return std::nullopt;
            }

            return MCMemoryRoom::MCCard{
                .Budget = local.Budget, .Used = local.CurrentUsage, .SharesMemory = borrowed.Budget == 0};
        }

    private:
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
        /// The adapter SDL's Vulkan device named <paramref name="name"/> runs on: the hardware adapter DXGI names so
        /// (Vulkan's device name and DXGI's description are the driver's one name; of two the same, the one with more
        /// memory of its own), else the one SDL's driver picks first, a card with memory of its own before one sharing
        /// the machine's, the one with the most; none without DXGI 1.4 (Windows 10's).
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

            ComPtr<IDXGIAdapter1> named;
            ComPtr<IDXGIAdapter1> largest;
            SIZE_T namedMemory = 0;
            SIZE_T largestMemory = 0;
            std::string largestName;

            for (UINT index = 0;; ++index)
            {
                ComPtr<IDXGIAdapter1> adapter;
                const HRESULT found = factory->EnumAdapters1(index, &adapter);

                if (found == DXGI_ERROR_NOT_FOUND)
                {
                    break;
                }

                DXGI_ADAPTER_DESC1 description{};
                HRESULT described = found;

                if (SUCCEEDED(found))
                {
                    described = adapter->GetDesc1(&description);
                }

                if (FAILED(described))
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
        ComPtr<IDXGIAdapter3> _Adapter;
        bool _QueryLogged = false;
        bool _NotificationLogged = false;
    };
}

std::shared_ptr<MCMemoryRoom::MCSystem> MCMakeMemorySystem()
{
    return std::make_shared<MCWinMemory>();
}

#endif

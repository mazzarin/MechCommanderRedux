#include "stdafx.h"
#include "platform/MCGpuLimits.h"

// Vulkan's types alone (SDL's copy of Khronos' headers), its functions looked up from the loader SDL's GPU uses.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <SDL3/SDL_vulkan.h>

namespace
{
    constexpr std::string_view MetalDriver = "metal";
    constexpr std::string_view Direct3D12Driver = "direct3d12";
    constexpr std::string_view VulkanDriver = "vulkan";
    constexpr std::string_view NotListed = "Vulkan's devices can't be listed";

    /// <summary>SDL's Vulkan loader held (SDL_Vulkan_LoadLibrary, which SDL's GPU holds too) until this goes.</summary>
    struct MCHeldLoader
    {
        MCHeldLoader() = default;
        ~MCHeldLoader() { SDL_Vulkan_UnloadLibrary(); }
        MCHeldLoader(const MCHeldLoader&) = delete;
        MCHeldLoader& operator=(const MCHeldLoader&) = delete;
    };

    /// <summary>A Vulkan instance of the game's own, destroyed with this (nothing without its destroy function).</summary>
    struct MCHeldInstance
    {
        VkInstance Instance = VK_NULL_HANDLE;
        PFN_vkDestroyInstance Destroy = nullptr;

        ~MCHeldInstance()
        {
            if (Destroy != nullptr)
            {
                Destroy(Instance, nullptr);
            }
        }
    };

    template <typename Function>
    Function Lookup(PFN_vkGetInstanceProcAddr getProc, VkInstance instance, const char* name)
    {
        return reinterpret_cast<Function>(getProc(instance, name));
    }

    /// <summary>Whether Vulkan's loader offers the instance extension <paramref name="name"/>.</summary>
    bool HasInstanceExtension(PFN_vkGetInstanceProcAddr getProc, std::string_view name)
    {
        const auto enumerate = Lookup<PFN_vkEnumerateInstanceExtensionProperties>(
            getProc, VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
        uint32_t count = 0;

        if (enumerate == nullptr || enumerate(nullptr, &count, nullptr) != VK_SUCCESS)
        {
            return false;
        }

        std::vector<VkExtensionProperties> extensions(count);

        if (enumerate(nullptr, &count, extensions.data()) != VK_SUCCESS)
        {
            return false;
        }

        return std::ranges::any_of(extensions, [&](const VkExtensionProperties& extension)
                                   { return std::string_view(extension.extensionName) == name; });
    }
}

namespace MCGpuLimits
{
    std::expected<int32_t, std::string> VulkanLargestSide(std::string_view name)
    {
        if (!SDL_Vulkan_LoadLibrary(nullptr))
        {
            return std::unexpected(std::format("no Vulkan loader: {}", SDL_GetError()));
        }

        const MCHeldLoader held;
        const auto getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        const auto createInstance =
            getProc != nullptr ? Lookup<PFN_vkCreateInstance>(getProc, VK_NULL_HANDLE, "vkCreateInstance") : nullptr;

        if (createInstance == nullptr)
        {
            return std::unexpected("the Vulkan loader has no vkCreateInstance");
        }

        const bool portability = HasInstanceExtension(getProc, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        const char* const portabilityName = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &application;

        if (portability)
        {
            info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            info.enabledExtensionCount = 1;
            info.ppEnabledExtensionNames = &portabilityName;
        }

        VkInstance instance = VK_NULL_HANDLE;

        if (const VkResult made = createInstance(&info, nullptr, &instance); made != VK_SUCCESS)
        {
            return std::unexpected(std::format("no Vulkan instance ({})", static_cast<int>(made)));
        }

        const MCHeldInstance owned{instance, Lookup<PFN_vkDestroyInstance>(getProc, instance, "vkDestroyInstance")};
        const auto enumerate = Lookup<PFN_vkEnumeratePhysicalDevices>(getProc, instance, "vkEnumeratePhysicalDevices");
        const auto properties =
            Lookup<PFN_vkGetPhysicalDeviceProperties>(getProc, instance, "vkGetPhysicalDeviceProperties");
        uint32_t count = 0;

        if (enumerate == nullptr || properties == nullptr || enumerate(instance, &count, nullptr) != VK_SUCCESS)
        {
            return std::unexpected(std::string(NotListed));
        }

        std::vector<VkPhysicalDevice> devices(count);

        if (enumerate(instance, &count, devices.data()) != VK_SUCCESS)
        {
            return std::unexpected(std::string(NotListed));
        }

        devices.resize(count);

        std::optional<uint32_t> largest;

        for (VkPhysicalDevice device : devices)
        {
            VkPhysicalDeviceProperties described{};
            properties(device, &described);

            if (std::string_view(described.deviceName) == name)
            {
                largest = std::min(largest.value_or(described.limits.maxImageDimension2D),
                                   described.limits.maxImageDimension2D);
            }
        }

        if (!largest.has_value())
        {
            return std::unexpected(std::format("Vulkan lists no device \"{}\" among its {}", name, devices.size()));
        }

        return static_cast<int32_t>(std::min<uint32_t>(*largest, std::numeric_limits<int32_t>::max()));
    }

    int32_t LargestTextureSide(SDL_GPUDevice& device)
    {
        const char* const driverName = SDL_GetGPUDeviceDriver(&device);
        const std::string_view driver = driverName != nullptr ? driverName : "";

        if (driver == MetalDriver)
        {
            return MetalLeast;
        }

        if (driver == Direct3D12Driver)
        {
            return Direct3D12Least;
        }

        const char* const deviceName =
            SDL_GetStringProperty(SDL_GetGPUDeviceProperties(&device), SDL_PROP_GPU_DEVICE_NAME_STRING, "");

        if (driver != VulkanDriver)
        {
            SDL_Log("GPU: driver \"%.*s\" (%s) unknown: its textures taken as at most %d a side",
                    static_cast<int>(driver.size()), driver.data(), deviceName, VulkanLeast);
            return VulkanLeast;
        }

        const std::expected<int32_t, std::string> largest = VulkanLargestSide(deviceName);

        if (!largest)
        {
            SDL_Log("GPU: the largest texture of Vulkan's %s unknown (%s): taken as Vulkan's least, %d a side",
                    deviceName, largest.error().c_str(), VulkanLeast);
            return VulkanLeast;
        }

        SDL_Log("GPU: Vulkan's %s holds textures up to %d a side", deviceName, *largest);
        return *largest;
    }
}

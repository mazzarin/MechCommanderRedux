// Built without the precompiled header: Objective-C++, and Metal and MCCore's Win32 stand-ins can't meet.
#include "MCMemorySystem.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <SDL3/SDL_log.h>
#include <dispatch/dispatch.h>

#include <atomic>
#include <string>

namespace
{
    /// <summary>The memory pressure levels that say the system is short of memory (Activity Monitor's yellow and red).</summary>
    constexpr uintptr_t ShortLevels = DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL;
    constexpr uintptr_t AllLevels = DISPATCH_MEMORYPRESSURE_NORMAL | ShortLevels;

    /// <summary>
    /// The Mac's answers: short while the system's memory pressure is at warn or critical (a dispatch source tells each
    /// change; normal ends it), and on the card Metal's recommended working set for the device against what the
    /// process has allocated on it.
    /// </summary>
    class MCMacMemory final : public MCMemoryRoom::MCSystem
    {
    public:
        MCMacMemory()
        {
            _Pressure = dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0, AllLevels,
                                               dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));

            if (_Pressure == nil)
            {
                SDL_Log("HD caches: no memory pressure source: the frames kept in memory never shrink for it");
                return;
            }

            // The flag, not the source, in the block: the source holding its handler holding the source would never go.
            const std::shared_ptr<std::atomic<bool>> shortOf = _Short;
            __weak dispatch_source_t source = _Pressure;
            dispatch_source_set_event_handler(_Pressure, ^{
              if (dispatch_source_t held = source; held != nil)
              {
                  shortOf->store((dispatch_source_get_data(held) & ShortLevels) != 0);
              }
            });
            dispatch_activate(_Pressure);
        }

        ~MCMacMemory() override
        {
            if (_Pressure != nil)
            {
                dispatch_source_cancel(_Pressure);
            }
        }

        MCMacMemory(const MCMacMemory&) = delete;
        MCMacMemory& operator=(const MCMacMemory&) = delete;

        bool Short() override { return _Short->load(); }

        std::optional<MCMemoryRoom::MCCard> Card(std::string_view deviceName) override
        {
            @autoreleasepool
            {
                if (!_Looked || deviceName != _DeviceName)
                {
                    _Looked = true;
                    _DeviceName = std::string(deviceName);
                    _Device = DeviceNamed(deviceName);
                }

                if (_Device == nil)
                {
                    return std::nullopt;
                }

                return MCMemoryRoom::MCCard{.Budget = _Device.recommendedMaxWorkingSetSize,
                                            .Used = _Device.currentAllocatedSize,
                                            .SharesMemory = _Device.hasUnifiedMemory};
            }
        }

    private:
        /// <summary>
        /// The device SDL's Metal driver draws with: the system's default, which it takes (it asks for no low-power
        /// one), when its name is SDL's; else the first of the Mac's devices named so; none when no device is.
        /// Metal hands out one object a GPU, so its count of what's allocated holds SDL's textures.
        /// </summary>
        static id<MTLDevice> DeviceNamed(std::string_view name)
        {
            const auto named = [name](id<MTLDevice> device)
            { return device != nil && device.name != nil && name == std::string_view(device.name.UTF8String); };

            if (id<MTLDevice> chosen = MTLCreateSystemDefaultDevice(); named(chosen))
            {
                return chosen;
            }

            for (id<MTLDevice> device in MTLCopyAllDevices())
            {
                if (named(device))
                {
                    return device;
                }
            }

            SDL_Log("HD caches: no Metal device named \"%.*s\": the GPU's frames follow the memory's pressure alone",
                    static_cast<int>(name.size()), name.data());
            return nil;
        }

        dispatch_source_t _Pressure = nil;
        std::shared_ptr<std::atomic<bool>> _Short = std::make_shared<std::atomic<bool>>(false);
        /// <summary>The device looked for (once a name), and the one found (nil: none).</summary>
        bool _Looked = false;
        std::string _DeviceName;
        id<MTLDevice> _Device = nil;
    };
}

std::shared_ptr<MCMemoryRoom::MCSystem> MCMakeMemorySystem()
{
    return std::make_shared<MCMacMemory>();
}

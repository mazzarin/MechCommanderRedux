// Built without the precompiled header: Objective-C++, and Foundation and MCCore's Win32 stand-ins can't meet.
#include "MCTelemetryBackend.h"

#import <Foundation/Foundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#include <SDL3/SDL_log.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    /// <summary>The Info.plist key naming the server (empty: no reports).</summary>
    NSString* const ServerKey = @"MCReportsURL";
    NSString* const ShownVersionKey = @"CFBundleShortVersionString";
    NSString* const BuildKey = @"CFBundleVersion";
    /// <summary>The black box's file in the user folder.</summary>
    constexpr std::string_view BlackBoxName = "blackbox.bin";
    /// <summary>The system's crash reports, and the time zones' countries.</summary>
    NSString* const CrashFolder = @"Library/Logs/DiagnosticReports";
    NSString* const CrashExtension = @"ips";
    constexpr const char* ZoneTable = "/usr/share/zoneinfo/zone.tab";
    /// <summary>A request waits this long for the server.</summary>
    constexpr NSTimeInterval RequestSeconds = 20.0;
    /// <summary>Our own user agent: the system's default names the macOS version, which isn't for the server.</summary>
    NSString* const UserAgent = @"MechCommander Redux";
    /// <summary>A reply's bytes kept, at most (the server's are a line).</summary>
    constexpr size_t MaxReplyBytes = 4096;
    constexpr double BytesPerGb = 1024.0 * 1024.0 * 1024.0;

    std::string Utf8(NSString* text)
    {
        return text != nil ? std::string(text.UTF8String) : std::string();
    }

    /// <summary>A sysctl string ("hw.model"), or empty.</summary>
    std::string SysctlText(const char* name)
    {
        size_t size = 0;

        if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0)
        {
            return {};
        }

        std::string text(size, '\0');

        if (sysctlbyname(name, text.data(), &size, nullptr, 0) != 0)
        {
            return {};
        }

        text.resize(std::strlen(text.c_str()));
        return text;
    }

    /// <summary>A sysctl number, or 0.</summary>
    template <typename T> T SysctlNumber(const char* name)
    {
        T value = 0;
        size_t size = sizeof(value);
        return sysctlbyname(name, &value, &size, nullptr, 0) == 0 ? value : 0;
    }

    /// <summary>The graphics chip's cores, as Apple's GPU driver gives them (0 elsewhere).</summary>
    int32_t GpuCores()
    {
        io_iterator_t services = IO_OBJECT_NULL;

        if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("AGXAccelerator"), &services) !=
            KERN_SUCCESS)
        {
            return 0;
        }

        int32_t cores = 0;

        for (io_object_t service = IOIteratorNext(services); service != IO_OBJECT_NULL;
             service = IOIteratorNext(services))
        {
            if (cores == 0)
            {
                if (CFTypeRef value = IORegistryEntryCreateCFProperty(service, CFSTR("gpu-core-count"),
                                                                      kCFAllocatorDefault, 0);
                    value != nullptr)
                {
                    if (CFGetTypeID(value) == CFNumberGetTypeID())
                    {
                        CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt32Type, &cores);
                    }

                    CFRelease(value);
                }
            }

            IOObjectRelease(service);
        }

        IOObjectRelease(services);
        return cores;
    }

    /// <summary>Whether the Mac runs on its battery now.</summary>
    bool OnBattery()
    {
        CFTypeRef info = IOPSCopyPowerSourcesInfo();

        if (info == nullptr)
        {
            return false;
        }

        const CFStringRef source = IOPSGetProvidingPowerSourceType(info);
        const bool battery =
            source != nullptr && CFStringCompare(source, CFSTR(kIOPMBatteryPowerKey), 0) == kCFCompareEqualTo;
        CFRelease(info);
        return battery;
    }

    /// <summary>The country of time zone <paramref name="zone"/> ("Europe/Berlin": DE), from the system's zone.tab.</summary>
    std::string ZoneCountry(std::string_view zone)
    {
        std::ifstream table(ZoneTable);
        std::string line;

        // Each line: the country, the coordinates, the zone, and comments, apart by tabs.
        while (std::getline(table, line))
        {
            if (line.empty() || line.front() == '#')
            {
                continue;
            }

            const size_t first = line.find('\t');
            const size_t second = first == std::string::npos ? first : line.find('\t', first + 1);

            if (second == std::string::npos)
            {
                continue;
            }

            const size_t third = line.find('\t', second + 1);

            if (std::string_view(line).substr(second + 1, third == std::string::npos ? third : third - second - 1) ==
                zone)
            {
                return line.substr(0, first);
            }
        }

        return {};
    }

    /// <summary>A frame of a crash report: the image's name (no path), the offset in it, and its symbol if known.</summary>
    std::string CrashFrame(NSDictionary* frame, NSArray* images)
    {
        NSNumber* imageIndex = [frame[@"imageIndex"] isKindOfClass:NSNumber.class] ? frame[@"imageIndex"] : nil;
        NSNumber* offset = [frame[@"imageOffset"] isKindOfClass:NSNumber.class] ? frame[@"imageOffset"] : nil;
        std::string image = "?";

        if (imageIndex != nil && imageIndex.unsignedIntegerValue < images.count)
        {
            if (NSDictionary* used = images[imageIndex.unsignedIntegerValue];
                [used isKindOfClass:NSDictionary.class] && [used[@"name"] isKindOfClass:NSString.class])
            {
                image = Utf8(used[@"name"]);
            }
        }

        std::string text = std::format("{} +0x{:x}", image, offset != nil ? offset.unsignedLongLongValue : 0);

        if (NSString* symbol = frame[@"symbol"]; [symbol isKindOfClass:NSString.class])
        {
            NSNumber* at = [frame[@"symbolLocation"] isKindOfClass:NSNumber.class] ? frame[@"symbolLocation"]
                                                                                          : nil;
            text += std::format(" {} + {}", Utf8(symbol), at != nil ? at.unsignedLongLongValue : 0);
        }

        return text;
    }

    /// <summary>
    /// The crash in the system's report <paramref name="path"/> (an .ips: a line of JSON about it, then the report's
    /// JSON): the exception, and the failing thread's frames. Paths are left out: only images' names are kept.
    /// </summary>
    std::optional<MCTelemetry::MCCrash> ReadCrash(NSString* path)
    {
        NSData* data = [NSData dataWithContentsOfFile:path];
        const char* bytes = static_cast<const char*>(data.bytes);
        const char* newline = bytes != nullptr ? static_cast<const char*>(std::memchr(bytes, '\n', data.length)) : nullptr;

        if (newline == nullptr)
        {
            return std::nullopt;
        }

        const NSUInteger start = static_cast<NSUInteger>(newline - bytes) + 1;
        NSError* error = nil;
        NSDictionary* report =
            [NSJSONSerialization JSONObjectWithData:[data subdataWithRange:NSMakeRange(start, data.length - start)]
                                            options:0
                                              error:&error];

        if (![report isKindOfClass:NSDictionary.class])
        {
            SDL_Log("Reports: can't read the crash report %s (%s)", path.UTF8String,
                    error.localizedDescription.UTF8String);
            return std::nullopt;
        }

        MCTelemetry::MCCrash crash;

        if (NSDictionary* exception = report[@"exception"]; [exception isKindOfClass:NSDictionary.class])
        {
            for (NSString* key in @[ @"type", @"signal", @"subtype" ])
            {
                if (NSString* part = exception[key]; [part isKindOfClass:NSString.class])
                {
                    crash.What += (crash.What.empty() ? "" : " ") + Utf8(part);
                }
            }
        }

        NSArray* threads = report[@"threads"];
        NSArray* images = [report[@"usedImages"] isKindOfClass:NSArray.class] ? report[@"usedImages"] : @[];
        NSNumber* faulting =
            [report[@"faultingThread"] isKindOfClass:NSNumber.class] ? report[@"faultingThread"] : nil;

        if ([threads isKindOfClass:NSArray.class] && faulting != nil && faulting.unsignedIntegerValue < threads.count)
        {
            NSDictionary* thread = threads[faulting.unsignedIntegerValue];
            NSArray* frames = [thread isKindOfClass:NSDictionary.class] ? thread[@"frames"] : nil;

            for (NSDictionary* frame in [frames isKindOfClass:NSArray.class] ? frames : @[])
            {
                if ([frame isKindOfClass:NSDictionary.class])
                {
                    crash.Frames.push_back(CrashFrame(frame, images));
                }
            }
        }

        return crash;
    }

    /// <summary>The reports' backend on the Mac.</summary>
    class MCMacBackend final : public MCTelemetry::MCBackend
    {
    public:
        MCMacBackend(NSURL* server, std::filesystem::path userFolder)
            : _Server(server), _UserFolder(std::move(userFolder))
        {
            // No cookies or cache kept: each request stands alone.
            NSURLSessionConfiguration* configuration = NSURLSessionConfiguration.ephemeralSessionConfiguration;
            configuration.HTTPShouldSetCookies = NO;
            configuration.timeoutIntervalForRequest = RequestSeconds;
            _Session = [NSURLSession sessionWithConfiguration:configuration];
        }

        ~MCMacBackend() override
        {
            [_Session invalidateAndCancel];

            if (_Map != MAP_FAILED)
            {
                msync(_Map, _MapSize, MS_ASYNC);
                munmap(_Map, _MapSize);
            }

            if (_File >= 0)
            {
                close(_File);
            }
        }

        MCMacBackend(const MCMacBackend&) = delete;
        MCMacBackend& operator=(const MCMacBackend&) = delete;

        MCTelemetry::MCMachine Machine() override
        {
            NSDictionary* info = NSBundle.mainBundle.infoDictionary;
            const NSOperatingSystemVersion system = NSProcessInfo.processInfo.operatingSystemVersion;
            MCTelemetry::MCMachine machine;
            machine.Platform = "mac";
            machine.AppVersion = std::format("{} ({})", Utf8(info[ShownVersionKey]), Utf8(info[BuildKey]));
            machine.Model = SysctlText("hw.model");
            machine.Cpu = SysctlText("machdep.cpu.brand_string");
            machine.PerformanceCores = SysctlNumber<int32_t>("hw.perflevel0.physicalcpu");
            machine.EfficiencyCores = SysctlNumber<int32_t>("hw.perflevel1.physicalcpu");

            if (machine.PerformanceCores == 0)
            {
                machine.PerformanceCores = SysctlNumber<int32_t>("hw.physicalcpu");
            }

            machine.GpuCores = GpuCores();
            machine.MemoryGb =
                static_cast<int32_t>(std::lround(static_cast<double>(SysctlNumber<uint64_t>("hw.memsize")) / BytesPerGb));
            machine.System = std::format("{}.{}", system.majorVersion, system.minorVersion);
            return machine;
        }

        MCTelemetry::MCPower Power() override
        {
            NSProcessInfo* process = NSProcessInfo.processInfo;
            return {.Thermal = static_cast<int32_t>(process.thermalState),
                    .Battery = OnBattery(),
                    .LowPower = process.lowPowerModeEnabled == YES};
        }

        std::vector<std::string> LocalCountries() override
        {
            std::vector<std::string> countries;

            if (NSString* region = [NSLocale.currentLocale objectForKey:NSLocaleCountryCode];
                [region isKindOfClass:NSString.class])
            {
                countries.push_back(Utf8(region));
            }

            if (std::string zone = ZoneCountry(Utf8(NSTimeZone.localTimeZone.name)); !zone.empty())
            {
                countries.push_back(std::move(zone));
            }

            return countries;
        }

        int64_t Now() override
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        void Request(std::string path, std::string body,
                     std::function<void(int32_t status, std::string reply)> done) override
        {
            NSURL* url = [_Server URLByAppendingPathComponent:@(path.c_str())];
            NSMutableURLRequest* request =
                [NSMutableURLRequest requestWithURL:url
                                        cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                    timeoutInterval:RequestSeconds];
            [request setValue:UserAgent forHTTPHeaderField:@"User-Agent"];

            if (!body.empty())
            {
                request.HTTPMethod = @"POST";
                [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
                request.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];
            }

            auto reply = std::make_shared<std::function<void(int32_t, std::string)>>(std::move(done));
            NSURLSessionDataTask* task =
                [_Session dataTaskWithRequest:request
                            completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                              int32_t status = 0;

                              if (error == nil && [response isKindOfClass:NSHTTPURLResponse.class])
                              {
                                  status = static_cast<int32_t>(static_cast<NSHTTPURLResponse*>(response).statusCode);
                              }

                              std::string text;

                              if (data != nil)
                              {
                                  text.assign(static_cast<const char*>(data.bytes),
                                              std::min<size_t>(data.length, MaxReplyBytes));
                              }

                              (*reply)(status, std::move(text));
                            }];
            [task resume];
        }

        std::span<std::byte> MapBlackBox(size_t size) override
        {
            const std::filesystem::path path = _UserFolder / BlackBoxName;
            _File = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR);
            struct stat status{};

            // Another size is another layout: the box starts again (ftruncate zeroes what it adds).
            if (_File < 0 || fstat(_File, &status) != 0 ||
                (static_cast<size_t>(status.st_size) != size && ftruncate(_File, static_cast<off_t>(size)) != 0) ||
                (_Map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, _File, 0)) == MAP_FAILED)
            {
                SDL_Log("Reports: can't map the black box %s (%s): it lives in memory", path.c_str(),
                        std::strerror(errno));

                if (_File >= 0)
                {
                    close(_File);
                    _File = -1;
                }

                return {};
            }

            _MapSize = size;
            return {static_cast<std::byte*>(_Map), size};
        }

        std::optional<MCTelemetry::MCCrash> Crash(int64_t started) override
        {
            NSString* folder = [NSHomeDirectory() stringByAppendingPathComponent:CrashFolder];
            NSString* prefix = [NSProcessInfo.processInfo.processName stringByAppendingString:@"-"];
            NSString* newest = nil;
            NSDate* newestDate = [NSDate dateWithTimeIntervalSince1970:static_cast<NSTimeInterval>(started)];

            // The newest of this app's reports written since the run started.
            for (NSString* name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:folder error:nil])
            {
                if (![name hasPrefix:prefix] || ![name.pathExtension isEqualToString:CrashExtension])
                {
                    continue;
                }

                NSString* path = [folder stringByAppendingPathComponent:name];
                NSDate* modified = [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil].fileModificationDate;

                if (modified != nil && [modified compare:newestDate] != NSOrderedAscending)
                {
                    newest = path;
                    newestDate = modified;
                }
            }

            return newest != nil ? ReadCrash(newest) : std::nullopt;
        }

    private:
        NSURL* _Server;
        NSURLSession* _Session;
        std::filesystem::path _UserFolder;
        int _File = -1;
        void* _Map = MAP_FAILED;
        size_t _MapSize = 0;
    };
}

std::unique_ptr<MCTelemetry::MCBackend> MCMakeTelemetryBackend(const std::filesystem::path& userFolder)
{
    @autoreleasepool
    {
        NSString* server = NSBundle.mainBundle.infoDictionary[ServerKey];

        if (![server isKindOfClass:NSString.class] || server.length == 0)
        {
            return nullptr;
        }

        NSURL* url = [NSURL URLWithString:server];

        if (url == nil || ![url.scheme isEqualToString:@"https"])
        {
            SDL_Log("Reports: the Info.plist's %s isn't an https address: no reports", ServerKey.UTF8String);
            return nullptr;
        }

        return std::make_unique<MCMacBackend>(url, userFolder);
    }
}

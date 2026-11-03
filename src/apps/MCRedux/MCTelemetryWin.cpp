// Built without the precompiled header: Windows.h and MCCore's Win32 stand-ins can't meet.
#include "MCTelemetryBackend.h"

#ifdef _WIN32

#include "MCCrashTrace.h"
#include "MCVersion.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <SDL3/SDL_log.h>

#pragma comment(lib, "winhttp.lib")

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <format>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

// The server: the build defines MC_REPORTS_URL ("https://..."); without it the build sends nothing.
#ifndef MC_REPORTS_URL
#define MC_REPORTS_URL ""
#endif

namespace
{
    constexpr std::string_view ServerUrl = MC_REPORTS_URL;
    /// <summary>The black box's file, and MCCrashTrace's record of a crash, in the user folder.</summary>
    constexpr std::wstring_view BlackBoxName = L"blackbox.bin";
    constexpr std::wstring_view CrashRecordName = L"crash-record.txt";
    /// <summary>A request waits this long for each step (resolve, connect, send, receive).</summary>
    constexpr int RequestMilliseconds = 20000;
    /// <summary>A reply's bytes kept, at most (the server's are a line).</summary>
    constexpr size_t MaxReplyBytes = 4096;
    constexpr double BytesPerGb = 1024.0 * 1024.0 * 1024.0;
    constexpr std::wstring_view UserAgent = L"MechCommander Redux";
    constexpr BYTE BatterySaverOn = 1;
    constexpr BYTE OnLinePower = 1;

    std::wstring Wide(std::string_view text)
    {
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(static_cast<size_t>(std::max(length, 0)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
        return wide;
    }

    std::string Narrow(std::wstring_view text)
    {
        const int length =
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string narrow(static_cast<size_t>(std::max(length, 0)), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), length, nullptr,
                            nullptr);
        return narrow;
    }

    /// <summary>A string from HKEY_LOCAL_MACHINE, or empty.</summary>
    std::string MachineValue(const wchar_t* key, const wchar_t* name)
    {
        DWORD size = 0;

        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS)
        {
            return {};
        }

        std::wstring value(size / sizeof(wchar_t), L'\0');

        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS)
        {
            return {};
        }

        value.resize(std::wcslen(value.c_str()));
        return Narrow(value);
    }

    /// <summary>
    /// The processor's cores: on a hybrid one, the cores of its highest efficiency class are its performance cores
    /// (Windows ranks the faster cores higher), the rest efficiency cores; otherwise all are performance cores.
    /// </summary>
    std::pair<int32_t, int32_t> Cores()
    {
        DWORD length = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
        std::vector<std::byte> buffer(length);
        auto* first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());

        if (length == 0 || !GetLogicalProcessorInformationEx(RelationProcessorCore, first, &length))
        {
            return {0, 0};
        }

        std::vector<BYTE> classes;

        for (DWORD at = 0; at < length;)
        {
            const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + at);
            classes.push_back(entry->Processor.EfficiencyClass);
            at += entry->Size;
        }

        const BYTE fastest = classes.empty() ? 0 : *std::ranges::max_element(classes);
        const auto performance = static_cast<int32_t>(std::ranges::count(classes, fastest));
        return {performance, static_cast<int32_t>(classes.size()) - performance};
    }

    /// <summary>Windows' version, as RtlGetVersion gives it (GetVersionEx answers for the manifest's version).</summary>
    std::string SystemVersion()
    {
        using RtlGetVersionFunction = LONG(WINAPI*)(RTL_OSVERSIONINFOW*);
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto getVersion = ntdll != nullptr
                                    ? reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion"))
                                    : nullptr;
        RTL_OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);

        if (getVersion == nullptr || getVersion(&version) != 0)
        {
            return {};
        }

        return std::format("{}.{}.{}", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
    }

    struct MCInternetCloser
    {
        void operator()(HINTERNET handle) const { WinHttpCloseHandle(handle); }
    };

    using MCInternetHandle = std::unique_ptr<void, MCInternetCloser>;

    /// <summary>
    /// Sends <paramref name="body"/> to <paramref name="url"/> (a POST of JSON, or a GET when empty), waiting for the
    /// answer: the HTTP status (0: none) and the reply.
    /// </summary>
    std::pair<int32_t, std::string> Send(const std::wstring& url, const std::string& body)
    {
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);

        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        {
            return {0, {}};
        }

        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        const MCInternetHandle session(WinHttpOpen(UserAgent.data(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));

        if (session == nullptr)
        {
            return {0, {}};
        }

        WinHttpSetTimeouts(session.get(), RequestMilliseconds, RequestMilliseconds, RequestMilliseconds,
                           RequestMilliseconds);
        const MCInternetHandle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
        const MCInternetHandle request(connection != nullptr
                                           ? WinHttpOpenRequest(connection.get(), body.empty() ? L"GET" : L"POST",
                                                                path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                                WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                           : nullptr);
        const wchar_t* headers = body.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : L"Content-Type: application/json";
        const auto size = static_cast<DWORD>(body.size());

        if (request == nullptr ||
            !WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1L), const_cast<char*>(body.data()), size,
                                size, 0) ||
            !WinHttpReceiveResponse(request.get(), nullptr))
        {
            return {0, {}};
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        std::string reply;
        DWORD available = 0;

        while (reply.size() < MaxReplyBytes && WinHttpQueryDataAvailable(request.get(), &available) && available > 0)
        {
            const size_t start = reply.size();
            reply.resize(start + std::min<size_t>(available, MaxReplyBytes - start));
            DWORD read = 0;

            if (!WinHttpReadData(request.get(), reply.data() + start, static_cast<DWORD>(reply.size() - start), &read))
            {
                reply.resize(start);
                break;
            }

            reply.resize(start + read);
        }

        return {static_cast<int32_t>(status), std::move(reply)};
    }

    /// <summary>The reports' backend on Windows.</summary>
    class MCWindowsBackend final : public MCTelemetry::MCBackend
    {
    public:
        explicit MCWindowsBackend(std::filesystem::path userFolder) : _UserFolder(std::move(userFolder))
        {
            // A crash's stack goes to a record of module names and offsets too, which the next start sends.
            MCCrashTrace::SetRecordPath(_UserFolder / CrashRecordName);
        }

        ~MCWindowsBackend() override
        {
            MCCrashTrace::SetRecordPath({});

            if (_View != nullptr)
            {
                FlushViewOfFile(_View, 0);
                UnmapViewOfFile(_View);
            }

            if (_Mapping != nullptr)
            {
                CloseHandle(_Mapping);
            }

            if (_File != INVALID_HANDLE_VALUE)
            {
                CloseHandle(_File);
            }
        }

        MCWindowsBackend(const MCWindowsBackend&) = delete;
        MCWindowsBackend& operator=(const MCWindowsBackend&) = delete;

        MCTelemetry::MCMachine Machine() override
        {
            constexpr const wchar_t* bios = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
            constexpr const wchar_t* processor = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
            MEMORYSTATUSEX memory{};
            memory.dwLength = sizeof(memory);
            GlobalMemoryStatusEx(&memory);
            const auto [performance, efficiency] = Cores();
            MCTelemetry::MCMachine machine;
            machine.Platform = "windows";
            machine.AppVersion = MC_VERSION;
            machine.Model = std::format("{} {}", MachineValue(bios, L"SystemManufacturer"),
                                        MachineValue(bios, L"SystemProductName"));
            machine.Cpu = MachineValue(processor, L"ProcessorNameString");
            machine.PerformanceCores = performance;
            machine.EfficiencyCores = efficiency;
            machine.MemoryGb = static_cast<int32_t>(std::lround(static_cast<double>(memory.ullTotalPhys) / BytesPerGb));
            machine.System = SystemVersion();
            return machine;
        }

        MCTelemetry::MCPower Power() override
        {
            SYSTEM_POWER_STATUS power{};

            if (!GetSystemPowerStatus(&power))
            {
                return {};
            }

            // Windows has no thermal state to read: 0.
            return {.Battery = power.ACLineStatus != OnLinePower && power.BatteryFlag != BATTERY_FLAG_NO_BATTERY,
                    .LowPower = power.SystemStatusFlag == BatterySaverOn};
        }

        std::vector<std::string> LocalCountries() override
        {
            std::array<wchar_t, 8> code{};
            const GEOID nation = GetUserGeoID(GEOCLASS_NATION);

            if (nation == GEOID_NOT_AVAILABLE ||
                GetGeoInfoW(nation, GEO_ISO2, code.data(), static_cast<int>(code.size()), 0) <= 0)
            {
                return {};
            }

            return {Narrow(code.data())};
        }

        int64_t Now() override
        {
            return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        void Request(std::string path, std::string body,
                     std::function<void(int32_t status, std::string reply)> done) override
        {
            std::string base(ServerUrl);

            if (!base.ends_with('/'))
            {
                base += '/';
            }

            // A thread of its own: WinHTTP's calls wait; the reply goes to the reports' inbox from there.
            auto reply = std::make_shared<std::function<void(int32_t, std::string)>>(std::move(done));

            try
            {
                std::thread(
                    [url = Wide(base + path), body = std::move(body), reply]
                    {
                        auto [status, text] = Send(url, body);
                        (*reply)(status, std::move(text));
                    })
                    .detach();
            }
            catch (const std::system_error& error)
            {
                SDL_Log("Reports: no thread to send %s (%s)", path.c_str(), error.what());
                (*reply)(0, {});
            }
        }

        std::span<std::byte> MapBlackBox(size_t size) override
        {
            const std::filesystem::path path = _UserFolder / BlackBoxName;
            _File = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);

            // A larger mapping than the file grows it (with zeroes); the box's layout number spots another layout.
            _Mapping = _File != INVALID_HANDLE_VALUE
                           ? CreateFileMappingW(_File, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(size), nullptr)
                           : nullptr;
            _View = _Mapping != nullptr ? MapViewOfFile(_Mapping, FILE_MAP_ALL_ACCESS, 0, 0, size) : nullptr;

            if (_View == nullptr)
            {
                SDL_Log("Reports: can't map the black box %s (error %lu): it lives in memory",
                        Narrow(path.wstring()).c_str(), GetLastError());
                return {};
            }

            return {static_cast<std::byte*>(_View), size};
        }

        std::optional<MCTelemetry::MCCrash> Crash(int64_t started) override
        {
            const std::filesystem::path path = _UserFolder / CrashRecordName;
            std::error_code error;
            const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, error);

            if (error || std::chrono::clock_cast<std::chrono::system_clock>(written) <
                             std::chrono::system_clock::time_point(std::chrono::seconds(started)))
            {
                return std::nullopt;
            }

            // MCCrashTrace's record: the exception's line, then a frame a line (a module's name and offset).
            std::ifstream record(path);
            MCTelemetry::MCCrash crash;
            std::string line;

            while (std::getline(record, line))
            {
                if (line.empty())
                {
                    continue;
                }

                if (crash.What.empty())
                {
                    crash.What = line;
                }
                else
                {
                    crash.Frames.push_back(line);
                }
            }

            return crash.What.empty() ? std::nullopt : std::optional(std::move(crash));
        }

    private:
        std::filesystem::path _UserFolder;
        HANDLE _File = INVALID_HANDLE_VALUE;
        HANDLE _Mapping = nullptr;
        void* _View = nullptr;
    };
}

std::unique_ptr<MCTelemetry::MCBackend> MCMakeTelemetryBackend(const std::filesystem::path& userFolder)
{
    if (!ServerUrl.starts_with("https://"))
    {
        return nullptr;
    }

    return std::make_unique<MCWindowsBackend>(userFolder);
}

#endif

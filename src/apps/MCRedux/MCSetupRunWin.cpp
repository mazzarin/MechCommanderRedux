// Built without the precompiled header: windows.h and MCCore's Win32 stand-ins can't meet.
#include "MCSetupRun.h"

#ifdef _WIN32
#include "MCWinHttp.h"

#include <SDL3/SDL_log.h>
#include <format>

namespace
{
    /// <summary>What the setup reads (src/installer/windows/MechCommanderRedux.iss): the game to wait for, and RESTART.</summary>
    constexpr const wchar_t* UpdatePidVariable = L"MCREDUX_UPDATE_PID";
    constexpr const wchar_t* UpdateRestartVariable = L"MCREDUX_UPDATE_RESTART";
    constexpr const wchar_t* RestartValue = L"1";

    /// <summary>
    /// Windows' errors for a program it won't run: Application Control (Smart App Control, WDAC) and its reputation
    /// checks are ERROR_SYSTEM_INTEGRITY_* (4550 on), a file whose signature doesn't check out
    /// ERROR_INVALID_IMAGE_HASH.
    /// </summary>
    constexpr DWORD FirstIntegrityError = 4550;
    constexpr DWORD LastIntegrityError = 4599;

    /// <summary>Why Windows didn't start the setup, as the UPDATE page says it.</summary>
    std::string Why(DWORD error)
    {
        if ((error >= FirstIntegrityError && error <= LastIntegrityError) || error == ERROR_INVALID_IMAGE_HASH)
        {
            return std::format("Windows blocked the update (Smart App Control, error {})", error);
        }

        switch (error)
        {
            case ERROR_ACCESS_DISABLED_BY_POLICY:
                return "a policy on this PC blocked the update";
            case ERROR_VIRUS_INFECTED:
            case ERROR_VIRUS_DELETED:
                return "Windows Security blocked the update";
            case ERROR_ELEVATION_REQUIRED:
                return "the update needs an administrator";
            default:
                return std::format("Windows didn't start the update (error {})", error);
        }
    }

    /// <summary>
    /// The setup's uninstall entry, which Windows' Apps list shows, by its AppId (src/installer/windows/
    /// MechCommanderRedux.iss: never changed), and the values the setup writes there.
    /// </summary>
    constexpr const wchar_t* UninstallKey =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{603FB38F-BA9C-4011-9D9E-1B9A52D73050}_is1";
    constexpr const wchar_t* InstallLocationValue = L"InstallLocation";
    constexpr const wchar_t* DisplayVersionValue = L"DisplayVersion";

    /// <summary>Asks Windows whether what the game starts outlives it (<see cref="MCStartedOutlive"/>).</summary>
    MCOutlives AskOutlive()
    {
        BOOL inJob = FALSE;

        if (!IsProcessInJob(GetCurrentProcess(), nullptr, &inJob))
        {
            SDL_Log("Updater: whether the game runs in a job is unknown (error %lu): taken as not", GetLastError());
            return MCOutlives::Yes;
        }

        if (!inJob)
        {
            return MCOutlives::Yes;
        }

        // The game's own job (a nested one's nearest).
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};

        if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr))
        {
            SDL_Log("Updater: the game's job can't be asked about its limits (error %lu): taken as letting what the "
                    "game starts run on",
                    GetLastError());
            return MCOutlives::Yes;
        }

        const DWORD flags = limits.BasicLimitInformation.LimitFlags;

        if ((flags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) == 0 || (flags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK) != 0)
        {
            return MCOutlives::Yes;
        }

        return (flags & JOB_OBJECT_LIMIT_BREAKAWAY_OK) != 0 ? MCOutlives::BrokenAway : MCOutlives::No;
    }

    /// <summary>A registry value of <paramref name="key"/>'s, a string; empty without one.</summary>
    std::wstring TextValue(HKEY key, const wchar_t* name)
    {
        DWORD type = 0;
        DWORD bytes = 0;

        if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || type != REG_SZ ||
            bytes == 0)
        {
            return {};
        }

        std::wstring text(bytes / sizeof(wchar_t), L'\0');

        if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(text.data()), &bytes) != ERROR_SUCCESS)
        {
            return {};
        }

        text.resize(wcsnlen(text.c_str(), text.size()));
        return text;
    }

    /// <summary>Starts <paramref name="setup"/> with <paramref name="arguments"/>, with <paramref name="flags"/>.</summary>
    std::string Create(const std::filesystem::path& setup, std::string_view arguments, DWORD flags,
                       PROCESS_INFORMATION& process)
    {
        std::wstring commandLine = L"\"" + setup.native() + L"\"";

        if (!arguments.empty())
        {
            commandLine += L" " + MCWinHttp::Wide(arguments);
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);

        if (!CreateProcessW(setup.c_str(), commandLine.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr,
                            &startup, &process))
        {
            return Why(GetLastError());
        }

        return {};
    }
}

std::string MCSetupRunnable(const std::filesystem::path& setup)
{
    // Windows judges a program as it maps it, before it runs: made suspended, the setup never runs, and is ended.
    PROCESS_INFORMATION process{};

    if (std::string refused = Create(setup, {}, CREATE_SUSPENDED, process); !refused.empty())
    {
        return refused;
    }

    TerminateProcess(process.hProcess, 0);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {};
}

std::string MCStartSetup(const std::filesystem::path& setup, std::string_view arguments, bool restart)
{
    // The setup inherits them, and an installer that doesn't know them ignores them.
    SetEnvironmentVariableW(UpdatePidVariable, std::to_wstring(GetCurrentProcessId()).c_str());
    SetEnvironmentVariableW(UpdateRestartVariable, restart ? RestartValue : nullptr);
    PROCESS_INFORMATION process{};

    // Out of the game's job where only that outlives it (Windows refuses the flag outside one).
    if (std::string failed = Create(setup, arguments, MCOutlivingFlags(), process); !failed.empty())
    {
        return failed;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {};
}

MCOutlives MCStartedOutlive()
{
    static const MCOutlives outlives = AskOutlive();
    return outlives;
}

unsigned long MCOutlivingFlags()
{
    return MCStartedOutlive() == MCOutlives::BrokenAway ? CREATE_BREAKAWAY_FROM_JOB : 0;
}

std::string MCShowInstalledVersion(const std::filesystem::path& gameFolder, std::string_view version)
{
    HKEY key = nullptr;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, UninstallKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
    {
        return "the player has no uninstall entry of their own (an install for every user)";
    }

    const std::wstring location = TextValue(key, InstallLocationValue);
    std::error_code error;
    std::string failed;

    if (location.empty() || !std::filesystem::equivalent(std::filesystem::path(location), gameFolder, error))
    {
        failed = "the player's uninstall entry is another folder's";
    }
    else
    {
        const std::wstring shown = MCWinHttp::Wide(version);

        if (const LSTATUS set =
                RegSetValueExW(key, DisplayVersionValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(shown.c_str()),
                               static_cast<DWORD>((shown.size() + 1) * sizeof(wchar_t)));
            set != ERROR_SUCCESS)
        {
            failed = std::format("its version can't be set (error {})", set);
        }
    }

    RegCloseKey(key);
    return failed;
}
#endif

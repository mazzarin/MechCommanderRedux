// Built without the precompiled header: windows.h and MCCore's Win32 stand-ins can't meet.
#include "MCSetupRun.h"

#ifdef _WIN32
#include "MCWinHttp.h"

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

    if (std::string failed = Create(setup, arguments, 0, process); !failed.empty())
    {
        return failed;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {};
}
#endif

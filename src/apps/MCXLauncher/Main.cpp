// Port-only (the personal build): MCX.EXE in a MechCommander Gold folder Redux has taken over (the Windows setup puts
// it there and keeps the original in "Redux backup"). Whatever starts the original's MCX.EXE (its shortcuts, a
// DirectPlay lobby such as MechCommander Online's rooms) gets Redux: MCRedux.exe beside it, with the same command line
// and current folder. It waits for the game and returns its exit code, so a lobby waiting on MCX.EXE sees the game end.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <format>
#include <string>

namespace
{
    constexpr std::wstring_view GameExe = L"MCRedux.exe";
    /// <summary>
    /// The variable that hands a program's compatibility settings to the programs it starts. Windows keys the player's
    /// settings (Windows 98, 256 colours, 640x480, run as administrator) by the exe's path, so the ones made for the
    /// original's MCX.EXE apply to this one; the game must not get them.
    /// </summary>
    constexpr const wchar_t* CompatLayerVariable = L"__COMPAT_LAYER";
    constexpr const wchar_t* Title = L"MechCommander Redux";
    /// <summary>The exit code when the game couldn't be started or waited for.</summary>
    constexpr int FailedExitCode = 1;
    /// <summary>The first path buffer, in characters; doubled until the path fits.</summary>
    constexpr DWORD FirstPathLength = MAX_PATH;

    /// <summary>Windows' text for <paramref name="error"/>, else its number.</summary>
    std::wstring SystemMessage(DWORD error)
    {
        wchar_t* text = nullptr;
        const DWORD length =
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, error, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
        std::wstring message = length != 0 ? std::wstring(text, length) : std::format(L"error {}", error);
        LocalFree(text);

        // Windows ends its text with a line break.
        while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' || message.back() == L' '))
        {
            message.pop_back();
        }

        return message;
    }

    int Fail(const std::wstring& what)
    {
        MessageBoxW(nullptr, what.c_str(), Title, MB_OK | MB_ICONERROR);
        return FailedExitCode;
    }

    /// <summary>
    /// The command line after the program's name, which Windows splits off as CommandLineToArgvW does: up to the
    /// closing quote when it starts with one, else up to the first space or tab.
    /// </summary>
    const wchar_t* Arguments(const wchar_t* commandLine)
    {
        const wchar_t* at = commandLine;

        if (*at == L'"')
        {
            ++at;

            while (*at != L'\0' && *at != L'"')
            {
                ++at;
            }

            if (*at == L'"')
            {
                ++at;
            }
        }
        else
        {
            while (*at != L'\0' && *at != L' ' && *at != L'\t')
            {
                ++at;
            }
        }

        while (*at == L' ' || *at == L'\t')
        {
            ++at;
        }

        return at;
    }

    /// <summary>This exe's path, or empty with Windows' error.</summary>
    std::wstring OwnPath(DWORD& error)
    {
        std::wstring path(FirstPathLength, L'\0');

        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));

            if (length == 0)
            {
                error = GetLastError();
                return {};
            }

            if (length < path.size())
            {
                path.resize(length);
                return path;
            }

            path.resize(path.size() * 2);
        }
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    DWORD error = 0;
    const std::wstring ownPath = OwnPath(error);

    if (ownPath.empty())
    {
        return Fail(std::format(L"MechCommander Redux couldn't find its own folder: {}", SystemMessage(error)));
    }

    const std::wstring game = ownPath.substr(0, ownPath.find_last_of(L'\\') + 1) + std::wstring(GameExe);
    std::wstring commandLine = L"\"" + game + L"\"";

    if (const wchar_t* arguments = Arguments(GetCommandLineW()); *arguments != L'\0')
    {
        commandLine += L' ';
        commandLine += arguments;
    }

    SetEnvironmentVariableW(CompatLayerVariable, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    // Suspended until it may bring its window to the front, which this exe may as the program the player started.
    if (!CreateProcessW(game.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr,
                        &startup, &process))
    {
        return Fail(std::format(L"MechCommander Redux couldn't start {}: {}", game, SystemMessage(GetLastError())));
    }

    AllowSetForegroundWindow(process.dwProcessId);

    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1))
    {
        const DWORD resumeError = GetLastError();
        TerminateProcess(process.hProcess, FailedExitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return Fail(std::format(L"MechCommander Redux couldn't start {}: {}", game, SystemMessage(resumeError)));
    }

    CloseHandle(process.hThread);

    DWORD exitCode = FailedExitCode;

    if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process.hProcess, &exitCode))
    {
        exitCode = FailedExitCode;
    }

    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}

// Built without the precompiled header: windows.h and MCCore's Win32 stand-ins can't meet.
#include "MCGameCopy.h"

#include "MCRestart.h"

#include <chrono>
#include <format>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace
{
    /// <summary>How long a game asked to quit has before it's ended, and how often it's looked at meanwhile.</summary>
    constexpr auto QuitGrace = std::chrono::seconds(5);
    constexpr auto QuitLook = std::chrono::milliseconds(50);

#ifdef _WIN32
    /// <summary>
    /// <paramref name="argument"/> as one word of a Windows command line, as CommandLineToArgvW reads it back: quoted
    /// when it holds a space or a tab or is empty, its quotes and the backslashes before them escaped.
    /// </summary>
    void AppendWord(std::wstring& line, std::wstring_view argument)
    {
        if (!line.empty())
        {
            line += L' ';
        }

        if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring_view::npos)
        {
            line += argument;
            return;
        }

        line += L'"';
        size_t backslashes = 0;

        for (const wchar_t c : argument)
        {
            if (c == L'\\')
            {
                backslashes++;
                continue;
            }

            // Backslashes before a quote are doubled, and the quote escaped; elsewhere they stand as they are.
            line.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
            backslashes = 0;
            line += c;
        }

        line.append(backslashes * 2, L'\\');
        line += L'"';
    }
#endif
}

std::expected<std::unique_ptr<MCGameCopy>, std::string> MCGameCopy::Start(std::span<const std::string> arguments,
                                                                          const std::filesystem::path& log)
{
    const std::filesystem::path program = MCProgramPath();

    if (program.empty())
    {
        return std::unexpected(std::string("the game's program can't be found"));
    }

    std::unique_ptr<MCGameCopy> copy(new MCGameCopy());
#ifdef _WIN32
    std::wstring line;
    AppendWord(line, program.wstring());

    for (const std::string& argument : arguments)
    {
        AppendWord(line, std::filesystem::path(std::u8string(argument.begin(), argument.end())).wstring());
    }

    // The game is a windowed program: it writes nowhere, so the log is only made, as the Mac's is. It reads nothing
    // (the terminal is this program's), and it's handed these two handles alone, never this program's sockets.
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    const bool handed = output != INVALID_HANDLE_VALUE && input != INVALID_HANDLE_VALUE;
    std::array<HANDLE, 2> handles{output, input};
    SIZE_T listBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &listBytes);
    std::vector<uint8_t> listMemory(listBytes);
    const LPPROC_THREAD_ATTRIBUTE_LIST list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(listMemory.data());
    const bool listed = handed && InitializeProcThreadAttributeList(list, 1, 0, &listBytes) &&
                        UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(),
                                                  sizeof(handles), nullptr, nullptr);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);

    if (listed)
    {
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput = output;
        startup.StartupInfo.hStdError = output;
        startup.StartupInfo.hStdInput = input;
        startup.lpAttributeList = list;
    }

    PROCESS_INFORMATION process{};
    const BOOL started =
        CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, listed,
                       listed ? EXTENDED_STARTUPINFO_PRESENT : 0, nullptr, nullptr, &startup.StartupInfo, &process);
    const DWORD error = GetLastError();

    if (listed)
    {
        DeleteProcThreadAttributeList(list);
    }

    for (const HANDLE handle : handles)
    {
        if (handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle);
        }
    }

    if (!started)
    {
        return std::unexpected(std::format("the game didn't start (error {})", error));
    }

    CloseHandle(process.hThread);
    copy->_Process = reinterpret_cast<intptr_t>(process.hProcess);
#else
    std::vector<std::string> words = {program.string()};
    words.insert(words.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;

    for (std::string& word : words)
    {
        argv.push_back(word.data());
    }

    argv.push_back(nullptr);
    // Only what the actions open reach the game (Apple's close-on-exec by default): the log as its output and
    // errors, nothing to read (the terminal is this program's), and none of this program's sockets.
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
    const std::string logPath = log.string();
    constexpr int OutputMode = 0644;
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                                     OutputMode);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    pid_t started = 0;
    const int error = posix_spawn(&started, words.front().c_str(), &actions, &attributes, argv.data(), environ);
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);

    if (error != 0)
    {
        return std::unexpected(std::format("the game didn't start ({})", std::strerror(error)));
    }

    copy->_Process = started;
#endif
    return copy;
}

MCGameCopy::~MCGameCopy()
{
    Stop();
#ifdef _WIN32
    if (_Process != 0)
    {
        CloseHandle(reinterpret_cast<HANDLE>(_Process));
    }
#endif
}

bool MCGameCopy::Running()
{
    if (_Process == 0)
    {
        return false;
    }

#ifdef _WIN32
    return WaitForSingleObject(reinterpret_cast<HANDLE>(_Process), 0) == WAIT_TIMEOUT;
#else
    const pid_t done = waitpid(static_cast<pid_t>(_Process), nullptr, WNOHANG);

    if (done == 0)
    {
        return true;
    }

    _Process = 0;
    return false;
#endif
}

void MCGameCopy::Wait()
{
    if (_Process == 0)
    {
        return;
    }

#ifdef _WIN32
    WaitForSingleObject(reinterpret_cast<HANDLE>(_Process), INFINITE);
#else
    waitpid(static_cast<pid_t>(_Process), nullptr, 0);
    _Process = 0;
#endif
}

void MCGameCopy::Stop()
{
    if (!Running())
    {
        return;
    }

#ifdef _WIN32
    TerminateProcess(reinterpret_cast<HANDLE>(_Process), 1);
    WaitForSingleObject(reinterpret_cast<HANDLE>(_Process),
                        static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(QuitGrace).count()));
#else
    const pid_t game = static_cast<pid_t>(_Process);
    kill(game, SIGTERM);
    const auto until = std::chrono::steady_clock::now() + QuitGrace;

    while (Running() && std::chrono::steady_clock::now() < until)
    {
        std::this_thread::sleep_for(QuitLook);
    }

    if (Running())
    {
        kill(game, SIGKILL);
        Wait();
    }
#endif
}

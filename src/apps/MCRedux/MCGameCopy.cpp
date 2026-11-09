// Built without the precompiled header: windows.h and MCCore's Win32 stand-ins can't meet.
#include "MCGameCopy.h"

#include "MCRestart.h"

#include <format>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace
{
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

    // The game is a windowed program: it writes nowhere, so the log is only made, as the Mac's is.
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);

    if (output != INVALID_HANDLE_VALUE)
    {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = output;
        startup.hStdError = output;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }

    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, output != INVALID_HANDLE_VALUE,
                                        0, nullptr, nullptr, &startup, &process);
    const DWORD error = GetLastError();

    if (output != INVALID_HANDLE_VALUE)
    {
        CloseHandle(output);
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
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    const std::string logPath = log.string();
    constexpr int OutputMode = 0644;
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, OutputMode);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    pid_t started = 0;
    const int error = posix_spawn(&started, words.front().c_str(), &actions, nullptr, argv.data(), environ);
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
#ifdef _WIN32
    if (_Process != 0)
    {
        CloseHandle(reinterpret_cast<HANDLE>(_Process));
    }
#else
    // Its exit is collected even when it outlives this (it's never left a zombie while this program runs on).
    if (_Process != 0)
    {
        waitpid(static_cast<pid_t>(_Process), nullptr, WNOHANG);
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

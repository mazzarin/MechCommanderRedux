// Built without the precompiled header: windows.h and MCCore's Win32 stand-ins can't meet.
#include "MCRestart.h"

#include <SDL3/SDL_log.h>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#include "MCSetupRun.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cstring>
#include <mach-o/dyld.h>
#include <spawn.h>

extern char** environ;
#endif

namespace
{
#ifdef _WIN32
    /// <summary>The first try at the program's path, doubled until it fits.</summary>
    constexpr DWORD FirstPathLength = MAX_PATH;
#else
    /// <summary>
    /// LaunchServices' tool, which starts an app as the Finder does, focused; its switches for a new copy even while
    /// one runs, and for the words passed on to the app.
    /// </summary>
    constexpr std::string_view OpenTool = "/usr/bin/open";
    constexpr std::string_view NewCopySwitch = "-n";
    constexpr std::string_view ArgumentsSwitch = "--args";

    /// <summary>This program's path, or empty when the system won't say.</summary>
    std::filesystem::path ProgramPath()
    {
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string path(size, '\0');

        if (_NSGetExecutablePath(path.data(), &size) != 0)
        {
            return {};
        }

        path.resize(std::strlen(path.c_str()));
        return std::filesystem::path(path);
    }
#endif
}

bool MCStartAgain([[maybe_unused]] std::span<const std::string> arguments)
{
#ifdef _WIN32
    // Windows' own command line, as it was given: nothing lost turning it into words and back.
    std::wstring program(FirstPathLength, L'\0');
    DWORD length = 0;

    while ((length = GetModuleFileNameW(nullptr, program.data(), static_cast<DWORD>(program.size()))) == program.size())
    {
        program.resize(program.size() * 2);
    }

    if (length == 0)
    {
        SDL_Log("Updater: the game can't start again: its path is unknown (error %lu)", GetLastError());
        return false;
    }

    program.resize(length);
    std::wstring commandLine = GetCommandLineW();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    // Out of the game's job where only that outlives it (a lobby's that ends its processes as the game exits).
    if (!CreateProcessW(program.c_str(), commandLine.data(), nullptr, nullptr, FALSE, MCOutlivingFlags(), nullptr,
                        nullptr, &startup, &process))
    {
        SDL_Log("Updater: the game can't start again (error %lu)", GetLastError());
        return false;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    const std::filesystem::path program = ProgramPath();

    if (program.empty())
    {
        SDL_Log("Updater: the game can't start again: its path is unknown");
        return false;
    }

    // In the app, LaunchServices starts the new copy (-n: even while this one is still closing); a bare build runs
    // itself.
    const std::filesystem::path macOS = program.parent_path();
    const std::filesystem::path app = macOS.parent_path().parent_path();
    std::vector<std::string> words;

    if (macOS.filename() == "MacOS" && app.extension() == ".app")
    {
        words = {std::string(OpenTool), std::string(NewCopySwitch), app.string(), std::string(ArgumentsSwitch)};
    }
    else
    {
        words = {program.string()};
    }

    if (!arguments.empty())
    {
        words.insert(words.end(), arguments.begin() + 1, arguments.end());
    }

    std::vector<char*> argv;

    for (std::string& word : words)
    {
        argv.push_back(word.data());
    }

    argv.push_back(nullptr);
    pid_t started = 0;

    if (const int error = posix_spawn(&started, words.front().c_str(), nullptr, nullptr, argv.data(), environ);
        error != 0)
    {
        SDL_Log("Updater: the game can't start again (%s)", std::strerror(error));
        return false;
    }

    return true;
#endif
}

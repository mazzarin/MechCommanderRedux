// Built without the precompiled header: MCCore's Win32 stand-ins (MCWin32Defs.h) and Windows.h cannot meet.
#include "MCConsole.h"

#include <cstdio>
#include <iostream>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <io.h>

void MCConsole::AttachParent()
{
    if (HasOutput() || !AttachConsole(ATTACH_PARENT_PROCESS))
    {
        return;
    }

    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
}

bool MCConsole::HasOutput()
{
    return _fileno(stderr) >= 0 && _get_osfhandle(_fileno(stderr)) >= 0;
}

std::optional<std::string> MCConsole::ReadLine(std::string_view prompt, bool hidden)
{
    // The longest line read: a password, an email or a name, each far shorter.
    constexpr DWORD MostCharacters = 1024;
    const HANDLE input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, 0, nullptr);

    if (input == INVALID_HANDLE_VALUE)
    {
        return std::nullopt;
    }

    DWORD mode = 0;
    GetConsoleMode(input, &mode);

    if (hidden)
    {
        SetConsoleMode(input, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
    }

    std::fwrite(prompt.data(), 1, prompt.size(), stdout);
    std::fflush(stdout);
    std::wstring line(MostCharacters, L'\0');
    DWORD read = 0;
    const bool got = ReadConsoleW(input, line.data(), MostCharacters, &read, nullptr) != FALSE;
    SetConsoleMode(input, mode);
    CloseHandle(input);

    if (hidden)
    {
        std::fputs("\n", stdout);
    }

    if (!got || read == 0)
    {
        return std::nullopt;
    }

    line.resize(read);

    while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r'))
    {
        line.pop_back();
    }

    std::string text;
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);

    if (length > 0)
    {
        text.resize(static_cast<size_t>(length));
        WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), text.data(), length, nullptr,
                            nullptr);
    }

    SecureZeroMemory(line.data(), line.size() * sizeof(wchar_t));
    return text;
}

#else

#include <cstring>
#include <readpassphrase.h>

void MCConsole::AttachParent()
{
}

bool MCConsole::HasOutput()
{
    return true;
}

std::optional<std::string> MCConsole::ReadLine(std::string_view prompt, bool hidden)
{
    if (hidden)
    {
        // The longest password read, and its end.
        constexpr size_t MostBytes = 1024;
        std::string line(MostBytes, '\0');
        const std::string shown(prompt);

        if (readpassphrase(shown.c_str(), line.data(), line.size(), RPP_REQUIRE_TTY) == nullptr)
        {
            return std::nullopt;
        }

        line.resize(std::strlen(line.c_str()));
        return line;
    }

    std::cout << prompt << std::flush;
    std::string line;

    if (!std::getline(std::cin, line))
    {
        return std::nullopt;
    }

    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }

    return line;
}

#endif

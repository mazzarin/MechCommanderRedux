#pragma once

// Port-only (the personal build), Windows: what the game's WinHTTP users share (MCHttpGetWin.cpp, the updates'
// downloads; MCTelemetryWin.cpp, the reports) and Windows' UTF-16 text (MCSetupRunWin.cpp too). They're built without
// the precompiled header: windows.h and MCCore's Win32 stand-ins can't meet.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <memory>
#include <string>
#include <string_view>

namespace MCWinHttp
{
    /// <summary>Our own user agent, for the reports and the updates alike.</summary>
    inline constexpr std::wstring_view UserAgent = L"MechCommander Redux";

    /// <summary><paramref name="text"/> (UTF-8) as Windows' UTF-16.</summary>
    inline std::wstring Wide(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);

        if (length <= 0)
        {
            return {};
        }

        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
        return wide;
    }

    /// <summary><paramref name="text"/> (Windows' UTF-16) as UTF-8.</summary>
    inline std::string Narrow(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int length =
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);

        if (length <= 0)
        {
            return {};
        }

        std::string narrow(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), length, nullptr,
                            nullptr);
        return narrow;
    }

    struct MCHandleCloser
    {
        void operator()(HINTERNET handle) const { WinHttpCloseHandle(handle); }
    };

    /// <summary>A WinHTTP handle (a session, connection or request), closed as it goes.</summary>
    using MCHandle = std::unique_ptr<void, MCHandleCloser>;

    /// <summary>
    /// A session with our user agent and the system's proxy, each step (resolve, connect, send, receive) waiting at
    /// most <paramref name="milliseconds"/>; <paramref name="flags"/> are WinHttpOpen's (WINHTTP_FLAG_ASYNC).
    /// </summary>
    /// <returns>The session, or none when it can't be opened (GetLastError says why).</returns>
    inline MCHandle OpenSession(int milliseconds, DWORD flags = 0)
    {
        MCHandle session(WinHttpOpen(UserAgent.data(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, flags));

        if (session != nullptr)
        {
            WinHttpSetTimeouts(session.get(), milliseconds, milliseconds, milliseconds, milliseconds);
        }

        return session;
    }
}

#endif

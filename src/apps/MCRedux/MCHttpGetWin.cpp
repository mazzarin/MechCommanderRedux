// Built without the precompiled header: Windows.h and MCCore's Win32 stand-ins can't meet.
#include "MCHttpGet.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace
{
    /// <summary>A wait this long for each step (resolve, connect, send, the host's next bytes) ends the transfer.</summary>
    constexpr int IdleMilliseconds = 30000;
    /// <summary>Our own user agent, as the reports'.</summary>
    constexpr std::wstring_view UserAgent = L"MechCommander Redux";
    /// <summary>The body is read in pieces this big.</summary>
    constexpr size_t PieceBytes = 64 * 1024;

    struct MCInternetCloser
    {
        void operator()(HINTERNET handle) const { WinHttpCloseHandle(handle); }
    };

    using MCInternetHandle = std::unique_ptr<void, MCInternetCloser>;

    std::wstring Wide(std::string_view text)
    {
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()))
            .wstring();
    }

    /// <summary>What failed, with WinHTTP's error (12002 a timeout, 12007 no such host, 12029 no connection).</summary>
    std::string Failed(std::string_view what, DWORD error)
    {
        return std::format("{} failed (WinHTTP error {})", what, error);
    }

    /// <summary>The answer's Content-Range header, or empty (it's ASCII, so each wide character narrows as is).</summary>
    std::string ContentRange(HINTERNET request)
    {
        DWORD size = 0;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX,
                            WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
        {
            return {};
        }

        std::wstring header(size / sizeof(wchar_t), L'\0');

        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX, header.data(),
                                 &size, WINHTTP_NO_HEADER_INDEX))
        {
            return {};
        }

        header.resize(size / sizeof(wchar_t));
        std::string narrow;

        for (const wchar_t c : header)
        {
            narrow += c < 0x80 ? static_cast<char>(c) : '?';
        }

        return narrow;
    }

    class MCWinGet final : public MCHttpSource::MCGet
    {
    public:
        MCWinGet()
            : _Session(WinHttpOpen(UserAgent.data(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0))
        {
            if (_Session == nullptr)
            {
                _OpenError = GetLastError();
                return;
            }

            WinHttpSetTimeouts(_Session.get(), IdleMilliseconds, IdleMilliseconds, IdleMilliseconds, IdleMilliseconds);
        }

        std::expected<void, std::string> Get(const std::string& url, uint64_t from,
                                             MCHttpSource::MCReply& reply) override
        {
            if (_Session == nullptr)
            {
                return std::unexpected(Failed("Opening WinHTTP", _OpenError));
            }

            const std::wstring address = Wide(url);
            URL_COMPONENTS parts{};
            parts.dwStructSize = sizeof(parts);
            parts.dwHostNameLength = static_cast<DWORD>(-1);
            parts.dwUrlPathLength = static_cast<DWORD>(-1);

            if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts))
            {
                return std::unexpected(std::format("\"{}\" isn't an address", url));
            }

            const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
            const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
            // The session keeps connections open, so the next file reuses this one.
            const MCInternetHandle connection(WinHttpConnect(_Session.get(), host.c_str(), parts.nPort, 0));

            if (connection == nullptr)
            {
                return std::unexpected(Failed("Connecting", GetLastError()));
            }

            const DWORD flags =
                WINHTTP_FLAG_REFRESH | (parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
            const MCInternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
                                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
            const std::wstring range = from != 0 ? std::format(L"Range: bytes={}-", from) : std::wstring();

            if (request == nullptr ||
                !WinHttpSendRequest(request.get(), range.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : range.c_str(),
                                    range.empty() ? 0 : static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                !WinHttpReceiveResponse(request.get(), nullptr))
            {
                return std::unexpected(Failed("Asking the host", GetLastError()));
            }

            DWORD status = 0;
            DWORD statusSize = sizeof(status);

            if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
            {
                return std::unexpected(Failed("Reading the answer's status", GetLastError()));
            }

            if (!reply.Status(static_cast<int>(status), ContentRange(request.get())))
            {
                return {};
            }

            std::vector<char> piece(PieceBytes);

            for (;;)
            {
                DWORD read = 0;

                if (!WinHttpReadData(request.get(), piece.data(), static_cast<DWORD>(piece.size()), &read))
                {
                    return std::unexpected(Failed("Reading the answer", GetLastError()));
                }

                if (read == 0 || !reply.Body({piece.data(), read}))
                {
                    return {};
                }
            }
        }

    private:
        MCInternetHandle _Session;
        DWORD _OpenError = 0;
    };
}

std::unique_ptr<MCHttpSource::MCGet> MCMakeHttpGet()
{
    return std::make_unique<MCWinGet>();
}

#endif

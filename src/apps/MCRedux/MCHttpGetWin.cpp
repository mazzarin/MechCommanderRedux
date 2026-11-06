// Built without the precompiled header: Windows.h and MCCore's Win32 stand-ins can't meet.
#include "MCHttpGet.h"

#ifdef _WIN32

#include "MCWinHttp.h"

#pragma comment(lib, "winhttp.lib")

#include <algorithm>
#include <array>
#include <condition_variable>
#include <format>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    /// <summary>A wait this long for each step (resolve, connect, send, the host's next bytes) ends the transfer.</summary>
    constexpr int IdleMilliseconds = 30000;
    /// <summary>The body is read in pieces this big.</summary>
    constexpr size_t PieceBytes = 64 * 1024;
    constexpr std::string_view StoppedError = "stopped";
    /// <summary>The notices a request's callback gets: each step's end, and its handle's closing (its last).</summary>
    constexpr DWORD CallbackNotices = WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES;

    /// <summary>
    /// WinHTTP's errors for a host out of reach: a timeout (12002), its name not found (12007), no connection (12029), a
    /// connection reset or ended (12030).
    /// </summary>
    constexpr std::array<DWORD, 4> UnreachableErrors = {ERROR_WINHTTP_TIMEOUT, ERROR_WINHTTP_NAME_NOT_RESOLVED,
                                                        ERROR_WINHTTP_CANNOT_CONNECT, ERROR_WINHTTP_CONNECTION_ERROR};

    /// <summary>What failed, with WinHTTP's error, and whether that's the host out of reach.</summary>
    MCHttpSource::MCGetError Failed(std::string_view what, DWORD error)
    {
        return {.Unreachable = std::ranges::find(UnreachableErrors, error) != UnreachableErrors.end(),
                .Reason = std::format("{} failed (WinHTTP error {})", what, error)};
    }

    /// <summary>A failure that isn't WinHTTP's: never the host out of reach.</summary>
    MCHttpSource::MCGetError Failed(std::string reason)
    {
        return {.Unreachable = false, .Reason = std::move(reason)};
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

    /// <summary>
    /// A request's steps as WinHTTP's callback reports them (it's asynchronous inside, so a cancel can close it while
    /// a step is under way): each step's end, what it brought, and the handle's closing, after which no callback
    /// comes. The callback notifies under the lock, so the thread waiting never ends the state while it's in use.
    /// </summary>
    struct MCRequestState
    {
        std::mutex Lock;
        std::condition_variable Changed;
        bool Done = false;
        DWORD Error = 0;
        DWORD Bytes = 0;
        bool Closed = false;
    };

    void CALLBACK OnStatus(HINTERNET, DWORD_PTR context, DWORD notice, LPVOID information, DWORD length)
    {
        // The connection's and session's handles carry none: only a request's steps are followed.
        if (context == 0)
        {
            return;
        }

        auto& state = *reinterpret_cast<MCRequestState*>(context);
        const std::scoped_lock lock(state.Lock);

        switch (notice)
        {
            case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
                state.Done = true;
                break;
            case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
            {
                state.Done = true;
                state.Bytes = *static_cast<const DWORD*>(information);
                break;
            }
            case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            {
                state.Done = true;
                state.Bytes = length;
                break;
            }
            case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            {
                state.Done = true;
                state.Error = static_cast<const WINHTTP_ASYNC_RESULT*>(information)->dwError;
                break;
            }
            case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
                state.Closed = true;
                break;
            default:
                return;
        }

        state.Changed.notify_all();
    }

    class MCWinGet final : public MCHttpSource::MCGet
    {
    public:
        MCWinGet() : _Session(MCWinHttp::OpenSession(IdleMilliseconds, WINHTTP_FLAG_ASYNC))
        {
            if (_Session == nullptr)
            {
                _OpenError = GetLastError();
            }
        }

        std::expected<void, MCHttpSource::MCGetError> Get(const std::string& url, uint64_t from,
                                                          MCHttpSource::MCReply& reply) override
        {
            if (_Session == nullptr)
            {
                return std::unexpected(Failed("Opening WinHTTP", _OpenError));
            }

            const std::wstring address = MCWinHttp::Wide(url);
            URL_COMPONENTS parts{};
            parts.dwStructSize = sizeof(parts);
            parts.dwHostNameLength = static_cast<DWORD>(-1);
            parts.dwUrlPathLength = static_cast<DWORD>(-1);

            if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts))
            {
                return std::unexpected(Failed(std::format("\"{}\" isn't an address", url)));
            }

            const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
            const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
            // The session keeps connections open, so the next file reuses this one.
            const MCWinHttp::MCHandle connection(WinHttpConnect(_Session.get(), host.c_str(), parts.nPort, 0));

            if (connection == nullptr)
            {
                return std::unexpected(Failed("Connecting", GetLastError()));
            }

            const DWORD flags =
                WINHTTP_FLAG_REFRESH | (parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
            HINTERNET request = WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

            if (request == nullptr)
            {
                return std::unexpected(Failed("Asking the host", GetLastError()));
            }

            // The state outlives every callback: from here on, each way out closes the request and waits for its
            // closing.
            MCRequestState state;
            auto context = reinterpret_cast<DWORD_PTR>(&state);

            if (WinHttpSetStatusCallback(request, OnStatus, CallbackNotices, 0) == WINHTTP_INVALID_STATUS_CALLBACK ||
                !WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
            {
                const DWORD error = GetLastError();
                WinHttpCloseHandle(request);
                return std::unexpected(Failed("Asking the host", error));
            }

            // Cancelled before: every later transfer fails at once.
            bool cancelledBefore = false;

            {
                const std::scoped_lock lock(_Lock);
                cancelledBefore = _Cancelled;
                _Request = cancelledBefore ? nullptr : request;
            }

            std::expected<void, MCHttpSource::MCGetError> done = std::unexpected(Failed(std::string(StoppedError)));

            if (!cancelledBefore)
            {
                done = Follow(state, from, context, reply);
            }

            bool cancelled = false;

            {
                const std::scoped_lock lock(_Lock);
                cancelled = _Cancelled;

                if (cancelledBefore)
                {
                    WinHttpCloseHandle(request);
                }
                else if (_Request != nullptr)
                {
                    // Otherwise a cancel has closed it already.
                    WinHttpCloseHandle(_Request);
                    _Request = nullptr;
                }
            }

            std::unique_lock lock(state.Lock);
            state.Changed.wait(lock, [&] { return state.Closed; });

            if (cancelled)
            {
                return std::unexpected(Failed(std::string(StoppedError)));
            }

            return done;
        }

        /// <summary>
        /// From any thread: closes the request under way, which WinHTTP allows while its step is pending (never while
        /// another thread is in a call on it: those are made under the same lock), so that step ends at once.
        /// </summary>
        void Cancel() override
        {
            const std::scoped_lock lock(_Lock);
            _Cancelled = true;

            if (_Request != nullptr)
            {
                WinHttpCloseHandle(_Request);
                _Request = nullptr;
            }
        }

    private:
        /// <summary>
        /// Starts a step on the request with <paramref name="step"/> (under the lock, while it's still open), then
        /// waits for its end: what it brought, or what failed.
        /// </summary>
        template <typename MCStep>
        std::expected<DWORD, MCHttpSource::MCGetError> Step(MCRequestState& state, std::string_view what,
                                                            const MCStep& step)
        {
            {
                const std::scoped_lock stateLock(state.Lock);
                state.Done = false;
                state.Error = 0;
                state.Bytes = 0;
            }

            {
                const std::scoped_lock lock(_Lock);

                if (_Request == nullptr)
                {
                    return std::unexpected(Failed(std::string(StoppedError)));
                }

                if (!step(_Request))
                {
                    return std::unexpected(Failed(what, GetLastError()));
                }
            }

            std::unique_lock stateLock(state.Lock);
            state.Changed.wait(stateLock, [&] { return state.Done || state.Closed; });

            if (!state.Done)
            {
                return std::unexpected(Failed(std::string(StoppedError)));
            }

            if (state.Error != 0)
            {
                return std::unexpected(Failed(what, state.Error));
            }

            return state.Bytes;
        }

        /// <summary>Sends the request and passes the answer to the reply, until it ends or the reply stops it.</summary>
        std::expected<void, MCHttpSource::MCGetError> Follow(MCRequestState& state, uint64_t from, DWORD_PTR context,
                                                             MCHttpSource::MCReply& reply)
        {
            const std::wstring range = from != 0 ? std::format(L"Range: bytes={}-", from) : std::wstring();
            const auto sent = Step(
                state, "Asking the host",
                [&](HINTERNET request)
                {
                    return WinHttpSendRequest(request, range.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : range.c_str(),
                                              range.empty() ? 0 : static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0,
                                              0, context) != FALSE;
                });

            if (!sent)
            {
                return std::unexpected(sent.error());
            }

            const auto answered = Step(state, "Waiting for the answer", [](HINTERNET request)
                                       { return WinHttpReceiveResponse(request, nullptr) != FALSE; });

            if (!answered)
            {
                return std::unexpected(answered.error());
            }

            DWORD status = 0;
            std::string contentRange;

            {
                const std::scoped_lock lock(_Lock);

                if (_Request == nullptr)
                {
                    return std::unexpected(Failed(std::string(StoppedError)));
                }

                DWORD statusSize = sizeof(status);

                if (!WinHttpQueryHeaders(_Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                         WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
                {
                    return std::unexpected(Failed("Reading the answer's status", GetLastError()));
                }

                contentRange = ContentRange(_Request);
            }

            if (!reply.Status(static_cast<int>(status), contentRange))
            {
                return {};
            }

            std::vector<char> piece(PieceBytes);

            for (;;)
            {
                const auto available = Step(state, "Reading the answer", [](HINTERNET request)
                                            { return WinHttpQueryDataAvailable(request, nullptr) != FALSE; });

                if (!available)
                {
                    return std::unexpected(available.error());
                }

                if (*available == 0)
                {
                    return {};
                }

                const auto read =
                    Step(state, "Reading the answer",
                         [&](HINTERNET request)
                         {
                             return WinHttpReadData(request, piece.data(),
                                                    static_cast<DWORD>(std::min<size_t>(*available, piece.size())),
                                                    nullptr) != FALSE;
                         });

                if (!read)
                {
                    return std::unexpected(read.error());
                }

                if (*read == 0 || !reply.Body({piece.data(), *read}))
                {
                    return {};
                }
            }
        }

        MCWinHttp::MCHandle _Session;
        DWORD _OpenError = 0;
        /// <summary>The request under way (none: none, or a cancel closed it), and whether a cancel came.</summary>
        std::mutex _Lock;
        HINTERNET _Request = nullptr;
        bool _Cancelled = false;
    };
}

std::unique_ptr<MCHttpSource::MCGet> MCMakeHttpGet()
{
    return std::make_unique<MCWinGet>();
}

#endif

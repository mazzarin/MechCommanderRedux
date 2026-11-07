// Built without the precompiled header: Windows.h and MCCore's Win32 stand-ins can't meet.
#include "MCWebSocketOpen.h"

#ifdef _WIN32

#include "MCWinHttp.h"

#pragma comment(lib, "winhttp.lib")

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
    /// <summary>
    /// Each step of the opening (resolve, connect, send, the answer) waits this long, and a read as long: the service's
    /// keep-alive answers every 30 s (MCServiceSignaling's ping), so a socket this quiet is gone.
    /// </summary>
    constexpr int StepMilliseconds = 90000;
    /// <summary>A message is read in pieces this big.</summary>
    constexpr DWORD PieceBytes = 64 * 1024;
    /// <summary>The largest message the service sends (a link's 64 KB of bytes, a description's 32 KB), with room.</summary>
    constexpr size_t MaxMessageBytes = 1u << 20;
    /// <summary>The bytes arrived and not taken past which the socket closes: the game takes them every frame.</summary>
    constexpr size_t MaxUnreadBytes = size_t{4} << 20;
    /// <summary>
    /// The bytes waiting to go past which the socket closes: the transport holds a link the service carries to 1 MB
    /// unsent (it reads <see cref="MCWebSocket::Unsent"/>), so more is a service that stopped reading.
    /// </summary>
    constexpr size_t MaxUnsentBytes = size_t{8} << 20;
    constexpr DWORD SwitchingProtocols = 101;
    /// <summary>
    /// The game's own close of an open socket sends the service its close (1000, its player leaving) and waits this long
    /// at most for it to go before the handles close.
    /// </summary>
    constexpr auto LeaveWait = std::chrono::seconds(1);
    /// <summary>
    /// The callback's notices: each step's end, the close sent, its handles' closing, and why a secure connection failed.
    /// (A notice's flag is its status's bit.)
    /// </summary>
    constexpr DWORD CallbackNotices = WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
                                      WINHTTP_CALLBACK_STATUS_SHUTDOWN_COMPLETE | WINHTTP_CALLBACK_FLAG_HANDLES |
                                      WINHTTP_CALLBACK_FLAG_SECURE_FAILURE;

    std::string Failed(std::string_view what, DWORD error)
    {
        return std::format("{} failed (WinHTTP error {})", what, error);
    }

    /// <summary>What WinHTTP's callback reported, for the socket's own thread to act on.</summary>
    struct MCEvent
    {
        DWORD Notice = 0;
        HINTERNET Handle = nullptr;
        DWORD Error = 0;
        DWORD Bytes = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE Type = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
    };

    /// <summary>
    /// The callback's side: events queued under their own lock, which no WinHTTP call is made under (WinHTTP may call
    /// back on the thread making a call), and a wake for the socket's thread.
    /// </summary>
    struct MCEvents
    {
        std::mutex Lock;
        std::condition_variable Changed;
        std::deque<MCEvent> Queue;
        bool Woken = false;
    };

    void CALLBACK OnStatus(HINTERNET handle, DWORD_PTR context, DWORD notice, LPVOID information, DWORD)
    {
        // The session's and connection's handles carry none: only the request's and the socket's are followed.
        if (context == 0)
        {
            return;
        }

        auto& events = *reinterpret_cast<MCEvents*>(context);
        MCEvent event{.Notice = notice, .Handle = handle};

        switch (notice)
        {
            case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
            case WINHTTP_CALLBACK_STATUS_SHUTDOWN_COMPLETE:
            case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
                break;
            case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            {
                const auto& status = *static_cast<const WINHTTP_WEB_SOCKET_STATUS*>(information);
                event.Bytes = status.dwBytesTransferred;
                event.Type = status.eBufferType;
                break;
            }
            case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
                // A socket's error (WINHTTP_WEB_SOCKET_ASYNC_RESULT) starts with the request's.
                event.Error = static_cast<const WINHTTP_ASYNC_RESULT*>(information)->dwError;
                break;
            case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
                event.Error = *static_cast<const DWORD*>(information);
                break;
            default:
                return;
        }

        const std::scoped_lock lock(events.Lock);
        events.Queue.push_back(event);
        events.Changed.notify_all();
    }

    /// <summary>
    /// A WebSocket over WinHTTP, asynchronous: one thread of its own makes every WinHTTP call on it and closes its
    /// handles, acting on what the callback queues, so no handle is used by one thread as another closes it, and no
    /// send waits on the network. The game's calls only queue and read, under the socket's lock.
    /// </summary>
    class MCWebSocketWin final : public MCWebSocket
    {
    public:
        explicit MCWebSocketWin(const std::string& url) : _Worker([this, url] { Run(url); }) {}

        ~MCWebSocketWin() override
        {
            Close();
            _Worker.join();
        }

        void SendText(std::string_view text) override { Queue({.Binary = false, .Data = std::string(text)}); }

        void SendBinary(std::span<const uint8_t> bytes) override
        {
            Queue({.Binary = true, .Data = std::string(bytes.begin(), bytes.end())});
        }

        std::vector<Message> Take() override
        {
            const std::scoped_lock lock(_Lock);
            _InboxBytes = 0;
            return std::exchange(_Inbox, {});
        }

        State GetState() const override
        {
            const std::scoped_lock lock(_Lock);
            return _State;
        }

        std::string WhyClosed() const override
        {
            const std::scoped_lock lock(_Lock);
            return _Why;
        }

        size_t Unsent() const override
        {
            const std::scoped_lock lock(_Lock);
            return _UnsentBytes;
        }

        void Close() override
        {
            {
                const std::scoped_lock lock(_Lock);
                // An open socket's close is sent first (Leaving): the service then knows its player left. A second
                // Close (the destructor's) keeps the first's.
                _Leaving = _Leaving || _State == State::Open;
                End("this game closed it");
            }

            Wake();
        }

    private:
        /// <summary>The socket's thread: opens it, then acts on the callback's events until its handles are closed.</summary>
        void Run(const std::string& url)
        {
            if (!Start(url))
            {
                CloseHandles();
            }

            for (;;)
            {
                std::deque<MCEvent> events;

                {
                    std::unique_lock lock(_Events.Lock);
                    const auto heard = [&] { return !_Events.Queue.empty() || _Events.Woken || Done(); };

                    if (_LeaveBy.has_value())
                    {
                        _Events.Changed.wait_until(lock, *_LeaveBy, heard);
                    }
                    else
                    {
                        _Events.Changed.wait(lock, heard);
                    }

                    events = std::exchange(_Events.Queue, {});
                    _Events.Woken = false;
                }

                for (const MCEvent& event : events)
                {
                    Handle(event);
                }

                if (!_HandlesClosed && GetState() == State::Closed && !Leaving())
                {
                    CloseHandles();
                }

                if (Done())
                {
                    return;
                }

                if (!_HandlesClosed)
                {
                    SendNext();
                }
            }
        }

        /// <summary>
        /// The game's own close of an open socket, from the socket's thread: its close (1000) sent once no send is under
        /// way (WinHTTP takes one at a time); true while it's going, up to <see cref="LeaveWait"/>.
        /// </summary>
        bool Leaving()
        {
            bool sending = false;

            {
                const std::scoped_lock lock(_Lock);

                if (!_Leaving || _Socket == nullptr || _LeaveDone)
                {
                    return false;
                }

                sending = _Sending;
            }

            const auto now = std::chrono::steady_clock::now();
            _LeaveBy = _LeaveBy.value_or(now + LeaveWait);

            if (!_LeaveSent && !sending)
            {
                const DWORD error =
                    WinHttpWebSocketShutdown(_Socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);

                if (error != NO_ERROR && error != ERROR_IO_PENDING)
                {
                    // The close can't be sent: the service takes the socket's end as a drop, and frees its place later.
                    return false;
                }

                // Asynchronous either way: it has gone once WinHTTP says so (SHUTDOWN_COMPLETE), as a send has.
                _LeaveSent = true;
            }

            return !_LeaveDone && now < *_LeaveBy;
        }

        /// <summary>Every handle the callback follows has said its last: nothing calls back into this object again.</summary>
        bool Done() const { return _HandlesClosed && _Followed == 0; }

        /// <summary>Opens the session, connection and request and sends the handshake; false when one fails.</summary>
        bool Start(const std::string& url)
        {
            // WinHTTP speaks http and https: a WebSocket's address is the same with its scheme swapped.
            std::string address = url;

            if (address.starts_with("wss://"))
            {
                address.replace(0, 3, "https");
            }
            else if (address.starts_with("ws://"))
            {
                address.replace(0, 2, "http");
            }

            const std::wstring wide = MCWinHttp::Wide(address);
            URL_COMPONENTS parts{};
            parts.dwStructSize = sizeof(parts);
            parts.dwHostNameLength = static_cast<DWORD>(-1);
            parts.dwUrlPathLength = static_cast<DWORD>(-1);

            if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts))
            {
                return Fail(std::format("\"{}\" isn't an address", url));
            }

            const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
            const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
            _Session = MCWinHttp::OpenSession(StepMilliseconds, WINHTTP_FLAG_ASYNC).release();
            _Connection = _Session != nullptr ? WinHttpConnect(_Session, host.c_str(), parts.nPort, 0) : nullptr;
            const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
            _Request = _Connection != nullptr
                           ? WinHttpOpenRequest(_Connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, flags)
                           : nullptr;

            if (_Request == nullptr)
            {
                return Fail(Failed("Opening the socket", GetLastError()));
            }

            auto context = reinterpret_cast<DWORD_PTR>(&_Events);

            if (WinHttpSetStatusCallback(_Request, OnStatus, CallbackNotices, 0) == WINHTTP_INVALID_STATUS_CALLBACK ||
                !WinHttpSetOption(_Request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
            {
                return Fail(Failed("Opening the socket", GetLastError()));
            }

            _Followed++;

            if (!WinHttpSetOption(_Request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
                !WinHttpSendRequest(_Request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, context))
            {
                return Fail(Failed("Reaching the service", GetLastError()));
            }

            return true;
        }

        void Handle(const MCEvent& event)
        {
            switch (event.Notice)
            {
                case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
                {
                    _Followed--;
                    return;
                }
                case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
                {
                    _SecureFailure = event.Error;
                    return;
                }
                case WINHTTP_CALLBACK_STATUS_SHUTDOWN_COMPLETE:
                {
                    _LeaveDone = true;
                    return;
                }
                case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
                {
                    if (_SecureFailure != 0)
                    {
                        Fail(std::format("{} (WinHTTP flags {:#x})", MCWebSocketUntrusted, _SecureFailure));
                    }
                    else if (event.Error == ERROR_WINHTTP_SECURE_FAILURE)
                    {
                        Fail(std::format("{} (WinHTTP error {})", MCWebSocketUntrusted, event.Error));
                    }
                    else
                    {
                        Fail(Failed(event.Handle == _Socket ? "The socket" : "Reaching the service", event.Error));
                    }

                    return;
                }
                default:
                    break;
            }

            // What a closed handle still reports is moot.
            if (_HandlesClosed)
            {
                return;
            }

            if (event.Notice == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE)
            {
                if (!WinHttpReceiveResponse(_Request, nullptr))
                {
                    Fail(Failed("Reading the service's answer", GetLastError()));
                }
            }
            else if (event.Notice == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)
            {
                Upgrade();
            }
            else if (event.Notice == WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
            {
                Read(event);
            }
            else if (event.Notice == WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE)
            {
                const std::scoped_lock lock(_Lock);
                _UnsentBytes -= std::min(_UnsentBytes, _InFlight.Data.size());
                _InFlight = {};
                _Sending = false;
            }
        }

        /// <summary>The service's answer to the handshake: the socket, open, and its first read.</summary>
        void Upgrade()
        {
            DWORD status = 0;
            DWORD statusSize = sizeof(status);

            if (!WinHttpQueryHeaders(_Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
            {
                Fail(Failed("Reading the service's answer", GetLastError()));
                return;
            }

            if (status != SwitchingProtocols)
            {
                Fail(std::format("the service answered {}, not a WebSocket", status));
                return;
            }

            _Socket = WinHttpWebSocketCompleteUpgrade(_Request, reinterpret_cast<DWORD_PTR>(&_Events));

            if (_Socket == nullptr)
            {
                Fail(Failed("Opening the socket", GetLastError()));
                return;
            }

            _Followed++;

            // Set again on the socket itself, whether or not it inherits the request's.
            if (WinHttpSetStatusCallback(_Socket, OnStatus, CallbackNotices, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
            {
                Fail(Failed("Opening the socket", GetLastError()));
                return;
            }

            WinHttpCloseHandle(_Request);
            _Request = nullptr;

            {
                const std::scoped_lock lock(_Lock);

                if (_State == State::Opening)
                {
                    _State = State::Open;
                }
            }

            ReadNext();
        }

        void ReadNext()
        {
            if (const DWORD error = WinHttpWebSocketReceive(_Socket, _Piece.data(), PieceBytes, nullptr, nullptr);
                error != NO_ERROR)
            {
                Fail(Failed("Reading", error));
            }
        }

        /// <summary>A read's piece: a message once whole, or the service's close.</summary>
        void Read(const MCEvent& event)
        {
            if (event.Type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
            {
                USHORT status = 0;
                DWORD reasonLength = 0;
                std::array<char, WINHTTP_WEB_SOCKET_MAX_CLOSE_REASON_LENGTH> reason{};
                WinHttpWebSocketQueryCloseStatus(_Socket, &status, reason.data(), static_cast<DWORD>(reason.size()),
                                                 &reasonLength);
                Fail(std::format("closed ({}){}{}", status, reasonLength > 0 ? ": " : "",
                                 std::string_view(reason.data(), reasonLength)));
                return;
            }

            _Partial.Binary = event.Type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
                              event.Type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE;
            _Partial.Data.append(reinterpret_cast<const char*>(_Piece.data()), event.Bytes);
            const bool whole = event.Type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
                               event.Type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;

            {
                const std::scoped_lock lock(_Lock);

                if (_Partial.Data.size() > MaxMessageBytes || _InboxBytes + _Partial.Data.size() > MaxUnreadBytes)
                {
                    End(std::format("the service sent over {} bytes the game hadn't read", MaxUnreadBytes));
                    return;
                }

                if (whole)
                {
                    _InboxBytes += _Partial.Data.size();
                    _Inbox.push_back(std::exchange(_Partial, {}));
                }
            }

            ReadNext();
        }

        /// <summary>Sends the next message waiting, once the last one has gone (WinHTTP takes one at a time).</summary>
        void SendNext()
        {
            {
                const std::scoped_lock lock(_Lock);

                if (_Socket == nullptr || _State != State::Open || _Sending || _Outbox.empty())
                {
                    return;
                }

                // Its bytes stay here until WinHTTP says they've gone.
                _InFlight = std::move(_Outbox.front());
                _Outbox.pop_front();
                _Sending = true;
            }

            const DWORD error = WinHttpWebSocketSend(_Socket,
                                                     _InFlight.Binary ? WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE
                                                                      : WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                                     _InFlight.Data.data(), static_cast<DWORD>(_InFlight.Data.size()));

            if (error != NO_ERROR)
            {
                Fail(Failed("Sending", error));
            }
        }

        void Queue(Message message)
        {
            {
                const std::scoped_lock lock(_Lock);

                if (_State == State::Closed)
                {
                    return;
                }

                if (_UnsentBytes + message.Data.size() > MaxUnsentBytes)
                {
                    End(std::format("the service took too little: over {} bytes waited to go", MaxUnsentBytes));
                }
                else
                {
                    _UnsentBytes += message.Data.size();
                    _Outbox.push_back(std::move(message));
                }
            }

            Wake();
        }

        void Wake()
        {
            const std::scoped_lock lock(_Events.Lock);
            _Events.Woken = true;
            _Events.Changed.notify_all();
        }

        /// <summary>Ends it with why, from the socket's thread: false, for a step that failed.</summary>
        bool Fail(std::string why)
        {
            const std::scoped_lock lock(_Lock);
            End(std::move(why));
            return false;
        }

        /// <summary>Ends it (under the lock), with the first reason given.</summary>
        void End(std::string why)
        {
            if (_State != State::Closed)
            {
                _State = State::Closed;
                _Why = std::move(why);
                // Nothing waits to go now (Unsent): a count left would hold the service's other sockets' sends.
                _Outbox.clear();
                _UnsentBytes = 0;
            }
        }

        /// <summary>
        /// Closes WinHTTP's handles, from the socket's thread only: what's under way on them is cancelled, and each the
        /// callback follows says its last (HANDLE_CLOSING) before the thread leaves.
        /// </summary>
        void CloseHandles()
        {
            const auto close = [](HINTERNET& handle)
            {
                if (handle != nullptr)
                {
                    WinHttpCloseHandle(handle);
                    handle = nullptr;
                }
            };

            close(_Socket);
            close(_Request);
            close(_Connection);
            close(_Session);
            _HandlesClosed = true;
            // Nothing more to wait for but the handles' last words.
            _LeaveBy.reset();
        }

        /// <summary>The game's side, under the lock.</summary>
        mutable std::mutex _Lock;
        State _State = State::Opening;
        std::string _Why;
        std::vector<Message> _Inbox;
        size_t _InboxBytes = 0;
        std::deque<Message> _Outbox;
        size_t _UnsentBytes = 0;
        bool _Sending = false;
        /// <summary>The socket's thread's own: what it sends (kept until WinHTTP says it has gone) and reads.</summary>
        Message _InFlight;
        Message _Partial;
        std::vector<uint8_t> _Piece = std::vector<uint8_t>(PieceBytes);
        HINTERNET _Session = nullptr;
        HINTERNET _Connection = nullptr;
        HINTERNET _Request = nullptr;
        HINTERNET _Socket = nullptr;
        /// <summary>The handles the callback follows that haven't said their last.</summary>
        int _Followed = 0;
        bool _HandlesClosed = false;
        DWORD _SecureFailure = 0;
        /// <summary>The game closed it while open (under the lock); its close sent, and gone; the most it waits.</summary>
        bool _Leaving = false;
        bool _LeaveSent = false;
        bool _LeaveDone = false;
        std::optional<std::chrono::steady_clock::time_point> _LeaveBy;
        MCEvents _Events;
        /// <summary>Last, so it starts once everything it uses is made.</summary>
        std::thread _Worker;
    };
}

std::unique_ptr<MCWebSocket> MCOpenWebSocket(const std::string& url)
{
    return std::make_unique<MCWebSocketWin>(url);
}

#endif

// Built without the precompiled header: Windows.h and MCCore's Win32 stand-ins can't meet.
#include "MCWebSocketOpen.h"

#ifdef _WIN32

#include "MCWinHttp.h"

#pragma comment(lib, "winhttp.lib")

#include <array>
#include <format>
#include <mutex>
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
    constexpr DWORD SwitchingProtocols = 101;

    std::string Failed(std::string_view what, DWORD error)
    {
        return std::format("{} failed (WinHTTP error {})", what, error);
    }

    /// <summary>
    /// A WebSocket over WinHTTP (synchronous): a thread of its own opens it and reads it; sends go from the caller's
    /// thread, one at a time, as WinHTTP allows beside a read.
    /// </summary>
    class MCWebSocketWin final : public MCWebSocket
    {
    public:
        explicit MCWebSocketWin(const std::string& url) : _Reader([this, url] { Run(url); }) {}

        ~MCWebSocketWin() override
        {
            Close();
            _Reader.join();
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

        void Close() override
        {
            const std::scoped_lock lock(_Lock);
            End("this game closed it");

            // Closing the handles ends what the reader waits on (WinHTTP fails it), and the reader leaves.
            if (_Socket != nullptr)
            {
                WinHttpWebSocketShutdown(_Socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            }

            CloseHandles();
        }

    private:
        /// <summary>The reader: opens the socket, then reads its messages until it ends.</summary>
        void Run(const std::string& url)
        {
            if (!Open(url))
            {
                return;
            }

            std::vector<uint8_t> piece(PieceBytes);
            Message message;

            for (;;)
            {
                HINTERNET socket = nullptr;

                {
                    const std::scoped_lock lock(_Lock);

                    if (_State == State::Closed)
                    {
                        return;
                    }

                    socket = _Socket;
                }

                DWORD read = 0;
                WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
                const DWORD error =
                    WinHttpWebSocketReceive(socket, piece.data(), static_cast<DWORD>(piece.size()), &read, &type);
                const std::scoped_lock lock(_Lock);

                if (error != NO_ERROR)
                {
                    End(Failed("Reading", error));
                    CloseHandles();
                    return;
                }

                if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
                {
                    USHORT status = 0;
                    DWORD reasonLength = 0;
                    std::array<char, WINHTTP_WEB_SOCKET_MAX_CLOSE_REASON_LENGTH> reason{};
                    WinHttpWebSocketQueryCloseStatus(socket, &status, reason.data(), static_cast<DWORD>(reason.size()),
                                                     &reasonLength);
                    End(std::format("closed ({}){}{}", status, reasonLength > 0 ? ": " : "",
                                    std::string_view(reason.data(), reasonLength)));
                    CloseHandles();
                    return;
                }

                message.Binary = type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
                                 type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE;
                message.Data.append(reinterpret_cast<const char*>(piece.data()), read);

                if (message.Data.size() > MaxMessageBytes || _InboxBytes + message.Data.size() > MaxUnreadBytes)
                {
                    End(std::format("the service sent over {} bytes the game hadn't read", MaxUnreadBytes));
                    CloseHandles();
                    return;
                }

                // A fragment: the rest of its message follows.
                if (type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE ||
                    type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE)
                {
                    continue;
                }

                _InboxBytes += message.Data.size();
                _Inbox.push_back(std::exchange(message, {}));
            }
        }

        /// <summary>The handshake: false (and closed, with why) when it fails or the socket was closed meanwhile.</summary>
        bool Open(const std::string& url)
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
                const std::scoped_lock lock(_Lock);
                End(std::format("\"{}\" isn't an address", url));
                return false;
            }

            const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
            const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
            HINTERNET request = nullptr;

            {
                const std::scoped_lock lock(_Lock);

                if (_State == State::Closed)
                {
                    return false;
                }

                _Session = MCWinHttp::OpenSession(StepMilliseconds).release();
                _Connection = _Session != nullptr ? WinHttpConnect(_Session, host.c_str(), parts.nPort, 0) : nullptr;
                const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
                request = _Connection != nullptr
                              ? WinHttpOpenRequest(_Connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, flags)
                              : nullptr;
                _Request = request;

                if (request == nullptr || !WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
                {
                    End(Failed("Opening the socket", GetLastError()));
                    CloseHandles();
                    return false;
                }
            }

            // The handshake waits on the network, so it runs without the lock: a Close meanwhile closes the handles,
            // which fails it.
            const bool sent =
                WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(request, nullptr);
            const DWORD sendError = sent ? NO_ERROR : GetLastError();
            DWORD status = 0;
            DWORD statusSize = sizeof(status);
            const bool answered =
                sent &&
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
            HINTERNET socket =
                answered && status == SwitchingProtocols ? WinHttpWebSocketCompleteUpgrade(request, 0) : nullptr;
            const std::scoped_lock lock(_Lock);

            if (_State == State::Closed)
            {
                if (socket != nullptr)
                {
                    WinHttpCloseHandle(socket);
                }

                return false;
            }

            if (socket == nullptr)
            {
                End(!sent       ? Failed("Reaching the service", sendError)
                    : !answered ? Failed("Reading the service's answer", GetLastError())
                                : std::format("the service answered {}, not a WebSocket", status));
                CloseHandles();
                return false;
            }

            WinHttpCloseHandle(_Request);
            _Request = nullptr;
            _Socket = socket;
            _State = State::Open;

            for (const Message& waiting : std::exchange(_Waiting, {}))
            {
                SendNow(waiting);
            }

            return true;
        }

        void Queue(Message message)
        {
            const std::scoped_lock lock(_Lock);

            if (_State == State::Open)
            {
                SendNow(message);
            }
            else if (_State == State::Opening)
            {
                _Waiting.push_back(std::move(message));
            }
        }

        /// <summary>Sends one message (under the lock, so sends keep their order).</summary>
        void SendNow(const Message& message)
        {
            const DWORD error =
                WinHttpWebSocketSend(_Socket,
                                     message.Binary ? WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE
                                                    : WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                     const_cast<char*>(message.Data.data()), static_cast<DWORD>(message.Data.size()));

            if (error != NO_ERROR)
            {
                End(Failed("Sending", error));
                CloseHandles();
            }
        }

        /// <summary>Ends it (under the lock), with the first reason given.</summary>
        void End(std::string why)
        {
            if (_State != State::Closed)
            {
                _State = State::Closed;
                _Why = std::move(why);
                _Waiting.clear();
            }
        }

        /// <summary>Closes WinHTTP's handles (under the lock), the socket's first.</summary>
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
        }

        mutable std::mutex _Lock;
        State _State = State::Opening;
        std::string _Why;
        std::vector<Message> _Inbox;
        size_t _InboxBytes = 0;
        std::vector<Message> _Waiting;
        HINTERNET _Session = nullptr;
        HINTERNET _Connection = nullptr;
        HINTERNET _Request = nullptr;
        HINTERNET _Socket = nullptr;
        /// <summary>Last, so it starts once everything it uses is made.</summary>
        std::thread _Reader;
    };
}

std::unique_ptr<MCWebSocket> MCOpenWebSocket(const std::string& url)
{
    return std::make_unique<MCWebSocketWin>(url);
}

#endif

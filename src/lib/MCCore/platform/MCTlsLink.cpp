#include "stdafx.h"
#include "platform/MCTlsLink.h"

#include "platform/MCTlsThreading.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <array>
#include <chrono>
#include <format>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    /// <summary>Mixed into the random generator's seed, as Mbed TLS asks: what it's for.</summary>
    constexpr std::string_view RandomPurpose = "MechCommanderReduxHD.TlsLink";
    /// <summary>Room for Mbed TLS's description of an error.</summary>
    constexpr size_t ErrorTextBytes = 160;

    /// <summary>Mbed TLS's description of <paramref name="code"/>.</summary>
    std::string ErrorText(int code)
    {
        std::array<char, ErrorTextBytes> text{};
        mbedtls_strerror(code, text.data(), text.size());
        return std::format("{} (-0x{:04x})", text.data(), static_cast<unsigned>(-code));
    }

    /// <summary>The milliseconds left until <paramref name="deadline"/> (0 once it has passed).</summary>
    int MsLeft(Clock::time_point deadline)
    {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        return left > 0 ? static_cast<int>(left) : 0;
    }
}

struct MCTlsLink::MCState
{
    MCSocketTransport Sockets;
    MCSocket::Handle Socket = MCSocket::InvalidHandle;
    mbedtls_entropy_context Entropy{};
    mbedtls_ctr_drbg_context Random{};
    mbedtls_x509_crt Anchor{};
    mbedtls_ssl_config Config{};
    mbedtls_ssl_context Ssl{};
    /// <summary>The protocol asked for, and Mbed TLS's list of it (which must outlive the configuration).</summary>
    std::string Alpn;
    std::array<const char*, 2> AlpnList{};
    bool Open = false;

    MCState()
    {
        mbedtls_entropy_init(&Entropy);
        mbedtls_ctr_drbg_init(&Random);
        mbedtls_x509_crt_init(&Anchor);
        mbedtls_ssl_config_init(&Config);
        mbedtls_ssl_init(&Ssl);
    }

    ~MCState()
    {
        mbedtls_ssl_free(&Ssl);
        mbedtls_ssl_config_free(&Config);
        mbedtls_x509_crt_free(&Anchor);
        mbedtls_ctr_drbg_free(&Random);
        mbedtls_entropy_free(&Entropy);
        Sockets.Close(Socket);
    }

    MCState(const MCState&) = delete;
    MCState& operator=(const MCState&) = delete;

    /// <summary>Waits until <paramref name="deadline"/> for the socket to read (or write, for <paramref name="write"/>).</summary>
    bool WaitFor(bool write, Clock::time_point deadline)
    {
        std::array<MCSocketTransport::MCWaiting, 1> waiting{MCSocketTransport::MCWaiting{Socket, write}};
        return MCSocketTransport::Wait(waiting, MsLeft(deadline)) > 0;
    }

    /// <summary>Mbed TLS's sending: what the socket takes now, or "try again" when it's full.</summary>
    static int SendBytes(void* context, const unsigned char* data, size_t size)
    {
        MCState& state = *static_cast<MCState*>(context);
        const int sent = state.Sockets.Send(state.Socket, std::span<const uint8_t>(data, size));

        if (sent > 0)
        {
            return sent;
        }

        return sent == 0 ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
    }

    /// <summary>Mbed TLS's receiving: what has arrived, or "try again" when nothing has.</summary>
    static int ReceiveBytes(void* context, unsigned char* data, size_t size)
    {
        MCState& state = *static_cast<MCState*>(context);
        const int received = state.Sockets.Receive(state.Socket, std::span<uint8_t>(data, size));

        if (received > 0)
        {
            return received;
        }

        return received == 0 ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_CONN_RESET;
    }
};

MCTlsLink::MCTlsLink(std::unique_ptr<MCState> state) : _State(std::move(state))
{
}

MCTlsLink::~MCTlsLink()
{
    Close();
}

std::expected<std::unique_ptr<MCTlsLink>, std::string> MCTlsLink::Connect(const MCSettings& settings)
{
    if (settings.AnchorPem.empty() && !settings.Trust.has_value())
    {
        return std::unexpected(std::string("no trust to check the server's certificates by"));
    }

    MCStartTlsThreading();

    if (const psa_status_t status = psa_crypto_init(); status != PSA_SUCCESS)
    {
        return std::unexpected(std::format("TLS's cryptography couldn't start ({})", static_cast<int>(status)));
    }

    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(settings.TimeoutMs);
    auto state = std::make_unique<MCState>();
    const std::optional<uint32_t> ip = state->Sockets.Resolve(settings.Host);

    if (!ip.has_value())
    {
        return std::unexpected(std::format("{} wasn't found", settings.Host));
    }

    state->Socket = state->Sockets.ConnectTcp(MCSocket::Address{*ip, settings.Port}, settings.TimeoutMs);

    if (state->Socket == MCSocket::InvalidHandle)
    {
        return std::unexpected(std::format("{}:{} couldn't be reached", settings.Host, settings.Port));
    }

    const auto failed = [](std::string_view what, int code) -> std::unexpected<std::string>
    { return std::unexpected(std::format("{}: {}", what, ErrorText(code))); };

    if (const int code =
            mbedtls_ctr_drbg_seed(&state->Random, mbedtls_entropy_func, &state->Entropy,
                                  reinterpret_cast<const unsigned char*>(RandomPurpose.data()), RandomPurpose.size());
        code != 0)
    {
        return failed("TLS's random numbers", code);
    }

    if (const int code = mbedtls_ssl_config_defaults(&state->Config, MBEDTLS_SSL_IS_CLIENT,
                                                     MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        code != 0)
    {
        return failed("TLS's settings", code);
    }

    mbedtls_ssl_conf_min_tls_version(&state->Config, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_rng(&state->Config, mbedtls_ctr_drbg_random, &state->Random);
    state->Alpn = settings.Alpn;
    state->AlpnList = {state->Alpn.c_str(), nullptr};

    if (const int code = mbedtls_ssl_conf_alpn_protocols(&state->Config, state->AlpnList.data()); code != 0)
    {
        return failed("TLS's application protocol", code);
    }

    if (!settings.AnchorPem.empty())
    {
        // Mbed TLS reads PEM text with its terminating NUL.
        if (const int code = mbedtls_x509_crt_parse(&state->Anchor,
                                                    reinterpret_cast<const unsigned char*>(settings.AnchorPem.c_str()),
                                                    settings.AnchorPem.size() + 1);
            code != 0)
        {
            return failed("the trusted authority", code);
        }

        mbedtls_ssl_conf_ca_chain(&state->Config, &state->Anchor, nullptr);
        mbedtls_ssl_conf_authmode(&state->Config, MBEDTLS_SSL_VERIFY_REQUIRED);
    }
    else
    {
        // The computer's trust judges the chain once the handshake (which checks the server holds its key) is done,
        // before anything is sent: Mbed TLS has no authorities of its own to judge it by.
        mbedtls_ssl_conf_authmode(&state->Config, MBEDTLS_SSL_VERIFY_OPTIONAL);
    }

    if (const int code = mbedtls_ssl_setup(&state->Ssl, &state->Config); code != 0)
    {
        return failed("TLS", code);
    }

    if (const int code = mbedtls_ssl_set_hostname(&state->Ssl, settings.Host.c_str()); code != 0)
    {
        return failed("the server's name", code);
    }

    mbedtls_ssl_set_bio(&state->Ssl, state.get(), &MCState::SendBytes, &MCState::ReceiveBytes, nullptr);

    for (;;)
    {
        const int code = mbedtls_ssl_handshake(&state->Ssl);

        if (code == 0)
        {
            break;
        }

        if ((code != MBEDTLS_ERR_SSL_WANT_READ && code != MBEDTLS_ERR_SSL_WANT_WRITE) ||
            !state->WaitFor(code == MBEDTLS_ERR_SSL_WANT_WRITE, deadline))
        {
            if (code == MBEDTLS_ERR_SSL_WANT_READ || code == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                return std::unexpected(std::format("{}:{} took too long to answer", settings.Host, settings.Port));
            }

            return failed("the TLS handshake", code);
        }
    }

    if (settings.AnchorPem.empty())
    {
        std::vector<std::span<const uint8_t>> chain;

        for (const mbedtls_x509_crt* certificate = mbedtls_ssl_get_peer_cert(&state->Ssl);
             certificate != nullptr && certificate->raw.p != nullptr; certificate = certificate->next)
        {
            chain.emplace_back(certificate->raw.p, certificate->raw.len);
        }

        if (chain.empty())
        {
            return std::unexpected(std::string("the server sent no certificate"));
        }

        if (const std::string distrust = settings.Trust->get().Check(chain, settings.Host); !distrust.empty())
        {
            return std::unexpected(std::format("the server's certificate isn't trusted: {}", distrust));
        }
    }

    const char* protocol = mbedtls_ssl_get_alpn_protocol(&state->Ssl);

    if (protocol == nullptr || std::string_view(protocol) != settings.Alpn)
    {
        return std::unexpected(std::format("the server doesn't speak {}", settings.Alpn));
    }

    state->Open = true;
    return std::unique_ptr<MCTlsLink>(new MCTlsLink(std::move(state)));
}

MCSocket::Handle MCTlsLink::Socket() const
{
    return _State->Open ? _State->Socket : MCSocket::InvalidHandle;
}

bool MCTlsLink::Buffered() const
{
    // Only bytes decrypted and not yet read: over TCP Mbed TLS takes a record's bytes as the record needs them, so a
    // record not yet whole waits on the socket, which the caller waits on.
    return _State->Open && mbedtls_ssl_get_bytes_avail(&_State->Ssl) > 0;
}

int MCTlsLink::Read(std::span<uint8_t> data)
{
    if (!_State->Open)
    {
        return -1;
    }

    for (;;)
    {
        const int code = mbedtls_ssl_read(&_State->Ssl, data.data(), data.size());

        if (code > 0)
        {
            return code;
        }

        if (code == MBEDTLS_ERR_SSL_WANT_READ || code == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            return 0;
        }

        // TLS 1.3's session tickets come between the records; the data may follow them.
        if (code == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
        {
            continue;
        }

        // Closed (0, or the server's goodbye) or failed.
        _State->Open = false;
        return -1;
    }
}

std::expected<void, std::string> MCTlsLink::Write(std::span<const uint8_t> data, int timeoutMs)
{
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    size_t sent = 0;

    while (sent < data.size())
    {
        if (!_State->Open)
        {
            return std::unexpected(std::string("the connection is closed"));
        }

        const int code = mbedtls_ssl_write(&_State->Ssl, data.data() + sent, data.size() - sent);

        if (code > 0)
        {
            sent += static_cast<size_t>(code);
            continue;
        }

        if (code != MBEDTLS_ERR_SSL_WANT_READ && code != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            _State->Open = false;
            return std::unexpected(std::format("sending failed: {}", ErrorText(code)));
        }

        // Mbed TLS asks for the same bytes again once there's room.
        if (!_State->WaitFor(code == MBEDTLS_ERR_SSL_WANT_WRITE, deadline))
        {
            _State->Open = false;
            return std::unexpected(std::string("sending took too long"));
        }
    }

    return {};
}

void MCTlsLink::Close()
{
    if (_State == nullptr || _State->Socket == MCSocket::InvalidHandle)
    {
        return;
    }

    if (_State->Open)
    {
        // One try: a goodbye that can't go out now isn't waited for.
        mbedtls_ssl_close_notify(&_State->Ssl);
        _State->Open = false;
    }

    _State->Sockets.Close(_State->Socket);
    _State->Socket = MCSocket::InvalidHandle;
}

// Built without the precompiled header (MCCore.vcxproj): libdatachannel's headers include the system's socket headers
// (winsock2.h drags in windows.h), which must not meet the game's Win32 stand-ins.

#include "platform/MCIceLink.h"
#include "platform/MCTlsThreading.h"

#include <SDL3/SDL_log.h>
#include <rtc/rtc.hpp>

#include <algorithm>
#include <deque>
#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <string_view>
#include <variant>

#ifdef _WIN32
// libdatachannel's, usrsctp's and libjuice's sockets and interfaces; Mbed TLS's and libjuice's random numbers.
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "bcrypt.lib")
#endif

namespace
{
    /// <summary>The one data channel's name, as both ends make it.</summary>
    constexpr std::string_view ChannelLabel = "mc";
    /// <summary>A candidate's signal (MCIceLink::Signal::Kind); a description's is its type, "offer" or "answer".</summary>
    constexpr std::string_view CandidateKind = "candidate";
    /// <summary>
    /// The most bytes a message carries: the channel keeps each message whole, and a stream's bytes are cut to fit any
    /// end's limit (libdatachannel's own is 256 KB; WebRTC's safe size is 16 KB).
    /// </summary>
    constexpr size_t MessageBytes = 16 * 1024;
    /// <summary>What libdatachannel logs into the game's log: its warnings and errors.</summary>
    constexpr rtc::LogLevel LoggedLevel = rtc::LogLevel::Warning;

    void StartLibraries()
    {
        MCStartTlsThreading();
        static std::once_flag logging;
        std::call_once(logging,
                       []
                       {
                           rtc::InitLogger(LoggedLevel, [](rtc::LogLevel, const std::string& message)
                                           { SDL_Log("libdatachannel: %s", message.c_str()); });
                       });
    }

    rtc::Configuration ConfigurationOf(const MCIceLink::Settings& settings)
    {
        rtc::Configuration configuration;

        for (const std::string& stun : settings.Stun)
        {
            configuration.iceServers.emplace_back(std::format("stun:{}", stun));
        }

        for (const MCIceLink::Settings::Relay& relay : settings.Relays)
        {
            configuration.iceServers.emplace_back(relay.Host, relay.Port, relay.User, relay.Password,
                                                  rtc::IceServer::RelayType::TurnUdp);
        }

        configuration.iceTransportPolicy = settings.RelayOnly ? rtc::TransportPolicy::Relay : rtc::TransportPolicy::All;

        if (!settings.BindAddress.empty())
        {
            configuration.bindAddress = settings.BindAddress;
        }

        return configuration;
    }
}

/// <summary>What libdatachannel's threads and the game's share: the queues and the state, under one lock.</summary>
struct MCIceShared
{
    mutable std::mutex Lock;
    MCIceLink::State State = MCIceLink::State::Connecting;
    std::vector<MCIceLink::Signal> Outgoing;
    std::deque<uint8_t> Received;
    std::shared_ptr<rtc::DataChannel> Channel;

    /// <summary>Ends the link as <paramref name="state"/> unless it has ended already (the first word stands).</summary>
    void End(MCIceLink::State state, std::string_view why)
    {
        const std::scoped_lock lock(Lock);

        if (State == MCIceLink::State::Connecting || State == MCIceLink::State::Open)
        {
            SDL_Log("MCIceLink: the link %s: %.*s", state == MCIceLink::State::Failed ? "failed" : "closed",
                    static_cast<int>(why.size()), why.data());
            State = state;
        }
    }
};

struct MCIceLink::Impl
{
    bool Offers = false;
    std::shared_ptr<MCIceShared> Shared = std::make_shared<MCIceShared>();
    std::shared_ptr<rtc::PeerConnection> Peer;
    /// <summary>Whether the other end's description is in: its candidates wait for it.</summary>
    bool HeardDescription = false;
    std::vector<Signal> EarlyCandidates;
    mutable std::optional<Path> KnownPath;

    /// <summary>The channel's own callbacks, onto the shared queues (never onto this, which may be gone).</summary>
    static void Watch(const std::shared_ptr<rtc::DataChannel>& channel, const std::weak_ptr<MCIceShared>& weak)
    {
        channel->onOpen(
            [weak]
            {
                if (const std::shared_ptr<MCIceShared> shared = weak.lock())
                {
                    const std::scoped_lock lock(shared->Lock);

                    if (shared->State == State::Connecting)
                    {
                        shared->State = State::Open;
                    }
                }
            });
        channel->onClosed(
            [weak]
            {
                if (const std::shared_ptr<MCIceShared> shared = weak.lock())
                {
                    shared->End(State::Closed, "the channel closed");
                }
            });
        channel->onError(
            [weak](const std::string& error)
            {
                if (const std::shared_ptr<MCIceShared> shared = weak.lock())
                {
                    shared->End(State::Failed, std::format("the channel's error: {}", error));
                }
            });
        channel->onMessage(
            [weak](rtc::message_variant message)
            {
                const std::shared_ptr<MCIceShared> shared = weak.lock();

                if (shared == nullptr)
                {
                    return;
                }

                // The other end sends only bytes: text means it isn't this game, and the stream can't be trusted.
                if (const rtc::binary* bytes = std::get_if<rtc::binary>(&message))
                {
                    const std::scoped_lock lock(shared->Lock);

                    for (const std::byte byte : *bytes)
                    {
                        shared->Received.push_back(static_cast<uint8_t>(byte));
                    }
                }
                else
                {
                    shared->End(State::Failed, "the other end sent text, not bytes");
                }
            });
    }

    void Start(const Settings& settings)
    {
        Peer = std::make_shared<rtc::PeerConnection>(ConfigurationOf(settings));
        const std::weak_ptr<MCIceShared> weak = Shared;
        Peer->onLocalDescription(
            [weak](const rtc::Description& description)
            {
                if (const std::shared_ptr<MCIceShared> shared = weak.lock())
                {
                    const std::scoped_lock lock(shared->Lock);
                    shared->Outgoing.push_back({description.typeString(), std::string(description), {}});
                }
            });
        Peer->onLocalCandidate(
            [weak](const rtc::Candidate& candidate)
            {
                if (const std::shared_ptr<MCIceShared> shared = weak.lock())
                {
                    const std::scoped_lock lock(shared->Lock);
                    shared->Outgoing.push_back({std::string(CandidateKind), std::string(candidate), candidate.mid()});
                }
            });
        Peer->onStateChange(
            [weak](rtc::PeerConnection::State state)
            {
                const std::shared_ptr<MCIceShared> shared = weak.lock();

                if (shared == nullptr)
                {
                    return;
                }

                if (state == rtc::PeerConnection::State::Failed)
                {
                    shared->End(State::Failed, "no path worked, or the path was lost");
                }
                else if (state == rtc::PeerConnection::State::Disconnected ||
                         state == rtc::PeerConnection::State::Closed)
                {
                    shared->End(State::Closed, "the other end stopped answering, or closed");
                }
            });

        if (Offers)
        {
            // The offer comes of making the channel (its description is the first signal).
            std::shared_ptr<rtc::DataChannel> channel = Peer->createDataChannel(std::string(ChannelLabel));
            Watch(channel, weak);
            const std::scoped_lock lock(Shared->Lock);
            Shared->Channel = std::move(channel);
        }
        else
        {
            Peer->onDataChannel(
                [weak](std::shared_ptr<rtc::DataChannel> channel)
                {
                    const std::shared_ptr<MCIceShared> shared = weak.lock();

                    if (shared == nullptr)
                    {
                        return;
                    }

                    // Only the offer's one channel: another is from a game that isn't this one.
                    bool taken = false;

                    {
                        const std::scoped_lock lock(shared->Lock);
                        taken = shared->Channel == nullptr && channel->label() == ChannelLabel;

                        if (taken)
                        {
                            shared->Channel = channel;
                        }
                    }

                    if (!taken)
                    {
                        SDL_Log("MCIceLink: a second channel (\"%s\") refused", channel->label().c_str());
                        channel->close();
                        return;
                    }

                    // Out of the lock: a callback set on a channel already open may run at once.
                    Watch(channel, weak);
                    const std::scoped_lock lock(shared->Lock);

                    // It may have opened before its callbacks were set.
                    if (channel->isOpen() && shared->State == State::Connecting)
                    {
                        shared->State = State::Open;
                    }
                });
        }
    }

    void AddCandidate(const Signal& signal) { Peer->addRemoteCandidate(rtc::Candidate(signal.Text, signal.Mid)); }
};

MCIceLink::MCIceLink(std::unique_ptr<Impl> impl) : _Impl(std::move(impl))
{
}

MCIceLink::~MCIceLink()
{
    Close();
}

std::unique_ptr<MCIceLink> MCIceLink::Offer(const Settings& settings)
{
    StartLibraries();
    auto impl = std::make_unique<Impl>();
    impl->Offers = true;

    try
    {
        impl->Start(settings);
    }
    catch (const std::exception& error)
    {
        SDL_Log("MCIceLink: libdatachannel refused to start an offering link: %s", error.what());
        return nullptr;
    }

    return std::unique_ptr<MCIceLink>(new MCIceLink(std::move(impl)));
}

std::unique_ptr<MCIceLink> MCIceLink::Answer(const Settings& settings)
{
    StartLibraries();
    auto impl = std::make_unique<Impl>();

    try
    {
        impl->Start(settings);
    }
    catch (const std::exception& error)
    {
        SDL_Log("MCIceLink: libdatachannel refused to start an answering link: %s", error.what());
        return nullptr;
    }

    return std::unique_ptr<MCIceLink>(new MCIceLink(std::move(impl)));
}

std::vector<MCIceLink::Signal> MCIceLink::TakeSignals()
{
    const std::scoped_lock lock(_Impl->Shared->Lock);
    return std::exchange(_Impl->Shared->Outgoing, {});
}

void MCIceLink::Heard(const Signal& signal)
{
    if (GetState() != State::Connecting)
    {
        return;
    }

    Impl& impl = *_Impl;
    // The other end's description is the answer to this end's offer, or the offer this end answers.
    const std::string_view expected = impl.Offers ? "answer" : "offer";

    try
    {
        if (signal.Kind == CandidateKind)
        {
            if (impl.HeardDescription)
            {
                impl.AddCandidate(signal);
            }
            else
            {
                impl.EarlyCandidates.push_back(signal);
            }
        }
        else if (signal.Kind == expected && !impl.HeardDescription)
        {
            impl.Peer->setRemoteDescription(rtc::Description(signal.Text, signal.Kind));
            impl.HeardDescription = true;

            for (const Signal& early : std::exchange(impl.EarlyCandidates, {}))
            {
                impl.AddCandidate(early);
            }
        }
        else
        {
            impl.Shared->End(State::Failed, std::format("a signal out of turn (\"{}\")", signal.Kind));
        }
    }
    catch (const std::exception& error)
    {
        impl.Shared->End(State::Failed, std::format("the other end's {} didn't parse: {}", signal.Kind, error.what()));
    }
}

MCIceLink::State MCIceLink::GetState() const
{
    const std::scoped_lock lock(_Impl->Shared->Lock);
    return _Impl->Shared->State;
}

MCIceLink::Path MCIceLink::GetPath() const
{
    if (_Impl->KnownPath.has_value())
    {
        return *_Impl->KnownPath;
    }

    if (GetState() != State::Open)
    {
        return Path::None;
    }

    rtc::Candidate local;
    rtc::Candidate remote;

    if (!_Impl->Peer->getSelectedCandidatePair(&local, &remote))
    {
        return Path::None;
    }

    if (local.type() == rtc::Candidate::Type::Relayed || remote.type() == rtc::Candidate::Type::Relayed)
    {
        _Impl->KnownPath = Path::Relay;
    }
    else
    {
        _Impl->KnownPath = local.family() == rtc::Candidate::Family::Ipv6 ? Path::DirectIpv6 : Path::DirectIpv4;
    }

    return *_Impl->KnownPath;
}

bool MCIceLink::Send(std::span<const uint8_t> bytes)
{
    std::shared_ptr<rtc::DataChannel> channel;

    {
        const std::scoped_lock lock(_Impl->Shared->Lock);

        if (_Impl->Shared->State != State::Open)
        {
            return false;
        }

        channel = _Impl->Shared->Channel;
    }

    try
    {
        // libdatachannel keeps what the network can't take yet, in order (its false is "kept", not "lost").
        for (size_t at = 0; at < bytes.size(); at += MessageBytes)
        {
            const std::span<const uint8_t> part = bytes.subspan(at, std::min(MessageBytes, bytes.size() - at));
            channel->send(reinterpret_cast<const std::byte*>(part.data()), part.size());
        }
    }
    catch (const std::exception& error)
    {
        _Impl->Shared->End(State::Failed, std::format("sending {} bytes failed: {}", bytes.size(), error.what()));
        return false;
    }

    return true;
}

size_t MCIceLink::Receive(std::span<uint8_t> into)
{
    const std::scoped_lock lock(_Impl->Shared->Lock);
    std::deque<uint8_t>& received = _Impl->Shared->Received;
    const size_t count = std::min(into.size(), received.size());
    std::copy_n(received.begin(), count, into.begin());
    received.erase(received.begin(), received.begin() + static_cast<std::ptrdiff_t>(count));
    return count;
}

void MCIceLink::Close()
{
    _Impl->Shared->End(State::Closed, "this end closed it");

    try
    {
        _Impl->Peer->close();
    }
    catch (const std::exception& error)
    {
        SDL_Log("MCIceLink: closing the link threw: %s", error.what());
    }
}

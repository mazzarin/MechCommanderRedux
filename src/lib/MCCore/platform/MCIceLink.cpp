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
    /// <summary>A candidate's signal (MCIceLink::Signal::Kind); a description's is its type, an offer or an answer.</summary>
    constexpr std::string_view CandidateKind = "candidate";
    constexpr std::string_view OfferKind = "offer";
    constexpr std::string_view AnswerKind = "answer";
    /// <summary>
    /// The addresses the other end may offer (a game offers a few: its own, its router's, its relay's, each family's);
    /// more, and it isn't this game.
    /// </summary>
    constexpr size_t MaxHeardCandidates = 64;
    /// <summary>
    /// The most bytes a message carries: the channel keeps each message whole, and a stream's bytes are cut to fit any
    /// end's limit (libdatachannel's own is 256 KB; WebRTC's safe size is 16 KB).
    /// </summary>
    constexpr size_t MessageBytes = 16 * 1024;
    /// <summary>
    /// The bytes received and not yet read a link holds. libdatachannel hands on every message as it comes, so nothing
    /// slows the other end: more than this, and it isn't this game, or is flooding it, and the link fails. A game's
    /// largest message is 5 KB (MsgBufferSize) and the game reads its links every frame, so this is a flood's size,
    /// never a game's: libdatachannel's own SCTP window is as big, and a Mac's TCP takes at most 4 MB unread.
    /// </summary>
    constexpr size_t MaxReceivedBytes = 1u << 20;
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

    /// <summary>
    /// A signal's kind as the log may name it: the other end's text is anything it likes (an address, even), so only the
    /// kinds this game sends.
    /// </summary>
    std::string_view KindShown(std::string_view kind)
    {
        for (const std::string_view known : {CandidateKind, OfferKind, AnswerKind})
        {
            if (kind == known)
            {
                return known;
            }
        }

        return "unknown";
    }

    /// <summary>Notes the kind of address <paramref name="candidate"/> is in <paramref name="offered"/>.</summary>
    void NoteOffered(MCIceLink::Offered& offered, const rtc::Candidate& candidate)
    {
        switch (candidate.type())
        {
            case rtc::Candidate::Type::Host:
                offered.Host = true;
                break;
            case rtc::Candidate::Type::ServerReflexive:
                offered.Reflexive = true;
                break;
            case rtc::Candidate::Type::Relayed:
                offered.Relay = true;
                break;
            case rtc::Candidate::Type::PeerReflexive:
            case rtc::Candidate::Type::Unknown:
                // Learned from the other end's packets, or no kind at all: never offered.
                break;
        }
    }
}

/// <summary>What libdatachannel's threads and the game's share: the queues and the state, under one lock.</summary>
struct MCIceShared
{
    mutable std::mutex Lock;
    MCIceLink::State State = MCIceLink::State::Connecting;
    std::vector<MCIceLink::Signal> Outgoing;
    /// <summary>The kinds of address this end has offered (libdatachannel's threads gather them).</summary>
    MCIceLink::Offered Here;
    /// <summary>ICE found a path (libdatachannel's ICE state reached connected).</summary>
    bool FoundPath = false;
    /// <summary>This end failed the link for what the other end sent.</summary>
    bool CutOff = false;
    /// <summary>When the channel opened, as libdatachannel's threads saw it (it may close before the game looks).</summary>
    std::optional<std::chrono::steady_clock::time_point> OpenedAt;
    std::deque<uint8_t> Received;
    std::shared_ptr<rtc::DataChannel> Channel;

    /// <summary>Ends the link as <paramref name="state"/> unless it has ended already (the first word stands).</summary>
    void End(MCIceLink::State state, std::string_view why)
    {
        const std::scoped_lock lock(Lock);
        EndLocked(state, why);
    }

    /// <summary>Fails the link for what the other end sent (under the lock): it can't be trusted.</summary>
    void CutLocked(std::string_view why)
    {
        if (State == MCIceLink::State::Connecting || State == MCIceLink::State::Open)
        {
            CutOff = true;
        }

        EndLocked(MCIceLink::State::Failed, why);
    }

    /// <summary>CutLocked, taking the lock.</summary>
    void Cut(std::string_view why)
    {
        const std::scoped_lock lock(Lock);
        CutLocked(why);
    }

    /// <summary>End, under the lock.</summary>
    void EndLocked(MCIceLink::State state, std::string_view why)
    {
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
    /// <summary>Settings::RelayOnly: the other end's own addresses are never tried.</summary>
    bool RelayOnly = false;
    std::shared_ptr<MCIceShared> Shared = std::make_shared<MCIceShared>();
    std::shared_ptr<rtc::PeerConnection> Peer;
    /// <summary>Whether the other end's description is in: its candidates wait for it.</summary>
    bool HeardDescription = false;
    std::vector<Signal> EarlyCandidates;
    /// <summary>The addresses the other end has offered, in its candidates and its description.</summary>
    size_t HeardCandidates = 0;
    /// <summary>The kinds of address the other end has offered (its signals, heard on the game's thread).</summary>
    Offered There;
    mutable std::optional<Path> KnownPath;
    /// <summary>libdatachannel's counts as last read (the game's thread's).</summary>
    mutable Traffic Kept;

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
                        shared->OpenedAt = std::chrono::steady_clock::now();
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

                    if (shared->State != State::Open)
                    {
                        return;
                    }

                    if (shared->Received.size() + bytes->size() > MaxReceivedBytes)
                    {
                        shared->CutLocked(
                            std::format("the other end sent over {} bytes this end hadn't read", MaxReceivedBytes));
                        return;
                    }

                    for (const std::byte byte : *bytes)
                    {
                        shared->Received.push_back(static_cast<uint8_t>(byte));
                    }
                }
                else
                {
                    shared->Cut("the other end sent text, not bytes");
                }
            });
    }

    void Start(const Settings& settings)
    {
        RelayOnly = settings.RelayOnly;
        Peer = std::make_shared<rtc::PeerConnection>(ConfigurationOf(settings));
        const std::weak_ptr<MCIceShared> weak = Shared;
        Peer->onLocalDescription(
            [weak](const rtc::Description& description)
            {
                if (const std::shared_ptr<MCIceShared> shared = weak.lock())
                {
                    const std::scoped_lock lock(shared->Lock);

                    for (const rtc::Candidate& candidate : description.candidates())
                    {
                        NoteOffered(shared->Here, candidate);
                    }

                    shared->Outgoing.push_back({description.typeString(), std::string(description), {}});
                }
            });
        Peer->onLocalCandidate(
            [weak](const rtc::Candidate& candidate)
            {
                if (const std::shared_ptr<MCIceShared> shared = weak.lock())
                {
                    const std::scoped_lock lock(shared->Lock);
                    NoteOffered(shared->Here, candidate);
                    shared->Outgoing.push_back({std::string(CandidateKind), std::string(candidate), candidate.mid()});
                }
            });
        Peer->onIceStateChange(
            [weak](rtc::PeerConnection::IceState state)
            {
                const std::shared_ptr<MCIceShared> shared = weak.lock();

                if (shared != nullptr && (state == rtc::PeerConnection::IceState::Connected ||
                                          state == rtc::PeerConnection::IceState::Completed))
                {
                    const std::scoped_lock lock(shared->Lock);
                    shared->FoundPath = true;
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
                        shared->OpenedAt = std::chrono::steady_clock::now();
                    }
                });
        }
    }

    /// <summary>
    /// Whether this end tries <paramref name="candidate"/>, an address the other end offered. Relay only, it tries the
    /// other end's relay alone: its checks to any other address would tell that end this one's (libdatachannel's
    /// policy only keeps this end's own addresses out of its signals). Not yet enough against a game that isn't this
    /// one: libjuice checks every pair from this end's own socket too, so an address offered as a relay's that isn't
    /// one still learns this end's (PLAN.md, step 3: before public games).
    /// </summary>
    bool Tries(const rtc::Candidate& candidate) const
    {
        return !RelayOnly || candidate.type() == rtc::Candidate::Type::Relayed;
    }

    void AddCandidate(const Signal& signal)
    {
        rtc::Candidate candidate(signal.Text, signal.Mid);
        NoteOffered(There, candidate);

        if (Tries(candidate))
        {
            Peer->addRemoteCandidate(std::move(candidate));
        }
    }
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
    const std::string_view expected = impl.Offers ? AnswerKind : OfferKind;

    try
    {
        if (signal.Kind == CandidateKind)
        {
            if (++impl.HeardCandidates > MaxHeardCandidates)
            {
                impl.Shared->Cut(std::format("the other end offered over {} addresses", MaxHeardCandidates));
                return;
            }

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
            rtc::Description description(signal.Text, signal.Kind);
            std::vector<rtc::Candidate> candidates = description.extractCandidates();
            impl.HeardCandidates += candidates.size();

            if (impl.HeardCandidates > MaxHeardCandidates)
            {
                impl.Shared->Cut(std::format("the other end offered over {} addresses", MaxHeardCandidates));
                return;
            }

            for (rtc::Candidate& candidate : candidates)
            {
                NoteOffered(impl.There, candidate);

                if (impl.Tries(candidate))
                {
                    description.addCandidate(std::move(candidate));
                }
            }

            impl.Peer->setRemoteDescription(std::move(description));
            impl.HeardDescription = true;

            for (const Signal& early : std::exchange(impl.EarlyCandidates, {}))
            {
                impl.AddCandidate(early);
            }
        }
        else
        {
            impl.Shared->Cut(std::format("a signal out of turn (\"{}\")", KindShown(signal.Kind)));
        }
    }
    catch (const std::exception&)
    {
        // What libdatachannel says of it can quote the address ("Invalid candidate address ..."), which the log
        // never holds.
        impl.Shared->Cut(std::format("the other end's {} didn't parse", KindShown(signal.Kind)));
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

MCIceLink::Offered MCIceLink::OfferedHere() const
{
    const std::scoped_lock lock(_Impl->Shared->Lock);
    return _Impl->Shared->Here;
}

MCIceLink::Offered MCIceLink::OfferedThere() const
{
    return _Impl->There;
}

bool MCIceLink::FoundPath() const
{
    const std::scoped_lock lock(_Impl->Shared->Lock);
    return _Impl->Shared->FoundPath;
}

std::optional<std::chrono::steady_clock::time_point> MCIceLink::OpenedAt() const
{
    const std::scoped_lock lock(_Impl->Shared->Lock);
    return _Impl->Shared->OpenedAt;
}

bool MCIceLink::CutOff() const
{
    const std::scoped_lock lock(_Impl->Shared->Lock);
    return _Impl->Shared->CutOff;
}

MCIceLink::Traffic MCIceLink::Carried() const
{
    Traffic& kept = _Impl->Kept;
    rtc::PeerConnection& peer = *_Impl->Peer;
    // The counts only grow while the transports last, and read 0 once they're gone: the most seen is the last.
    kept.BytesSent = std::max<uint64_t>(kept.BytesSent, peer.bytesSent());
    kept.BytesReceived = std::max<uint64_t>(kept.BytesReceived, peer.bytesReceived());

    if (const std::optional<std::chrono::milliseconds> roundTrip = peer.rtt())
    {
        kept.RoundTrip = roundTrip;
    }

    return kept;
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

size_t MCIceLink::Unsent() const
{
    std::shared_ptr<rtc::DataChannel> channel;

    {
        const std::scoped_lock lock(_Impl->Shared->Lock);
        channel = _Impl->Shared->Channel;
    }

    // Out of the lock: the channel takes its own.
    return channel != nullptr ? channel->bufferedAmount() : 0;
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

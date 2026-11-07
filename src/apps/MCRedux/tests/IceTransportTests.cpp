#include "stdafx.h"
#include "MCTest.h"
#include "fakes/MCFakeSignaling.h"
#include "platform/MCIceTransport.h"

// The internet games' network (platform/MCIceTransport): MCNetTransport over ICE links, two games' transports in one
// process, the service's signals passed by the fake board, every link on the loopback (IceLinkTests.cpp says why), and
// each link's report as it ends.

namespace
{
    /// <summary>The game the tests host, by its code on the service.</summary>
    constexpr std::string_view GameCode = "K7QF";
    constexpr uint16_t GamePort = 28800;
    /// <summary>How long anything here gets: a link on one machine opens in well under a second.</summary>
    constexpr auto LinkDeadline = std::chrono::seconds(15);
    constexpr auto PumpStep = std::chrono::milliseconds(2);
    /// <summary>How long a connection waits (MCDirectPlay's own wait).</summary>
    constexpr int ConnectMs = 3000;
    /// <summary>The link deadline of a transport whose host never answers.</summary>
    constexpr auto ShortOpenDeadline = std::chrono::milliseconds(1000);
    /// <summary>The idle deadline of a host whose guests' links carry nothing.</summary>
    constexpr auto ShortIdleDeadline = std::chrono::milliseconds(1000);
    /// <summary>How long a link that finds no path waits before the service carries it, in the tests that let it.</summary>
    constexpr auto ShortServiceDeadline = std::chrono::milliseconds(300);
    /// <summary>What a link the service carries takes each way: a lobby's worth and more.</summary>
    constexpr size_t ServiceBytes = 200 * 1024;
    /// <summary>The unsent bytes past which a connection's sends wait (MCIceTransport.cpp's SendWindowBytes).</summary>
    constexpr size_t ServiceWindowBytes = 1u << 20;
    /// <summary>Frames as a game sends them (MCIceTransport.cpp's): a connection, and its bytes.</summary>
    constexpr uint8_t ConnectFrame = 2;
    constexpr uint8_t DataFrame = 5;
    /// <summary>A flood: twice what a host holds unread for a connection (1 MB), in frames of 16 KB.</summary>
    constexpr size_t FloodBytes = 2u << 20;
    constexpr size_t FloodFrameBytes = 16 * 1024;
    constexpr size_t GuestBytes = 3u << 20;
    constexpr size_t HostBytes = 70 * 1024;
    constexpr size_t LargestSend = 40 * 1024;
    /// <summary>The raw link's number, as its guest numbers it.</summary>
    constexpr uint32_t RawLink = 7;

    /// <summary>The kind of a host's answer, the signal that carries its certificate's fingerprint.</summary>
    constexpr std::string_view AnswerKind = "answer";
    constexpr std::string_view FingerprintLine = "a=fingerprint:";

    using MCTelemetry::MCLinkEnd;
    constexpr bool GuestEnd = true;
    constexpr bool HostEnd = false;

    /// <summary>The links' reports a test's transports make, from whichever thread pumps them.</summary>
    class MCLinkLog
    {
    public:
        /// <summary>Where a transport's reports go: here.</summary>
        MCIceTransport::LinkReports Reports()
        {
            return [this](const MCTelemetry::MCLink& link)
            {
                const std::scoped_lock lock(_Lock);
                _Ended.push_back(link);
            };
        }

        /// <summary>The reports of the guest ends' links, or of the host ends', in the order they ended.</summary>
        std::vector<MCTelemetry::MCLink> Of(bool guest) const
        {
            const std::scoped_lock lock(_Lock);
            std::vector<MCTelemetry::MCLink> ended;
            std::ranges::copy_if(_Ended, std::back_inserter(ended),
                                 [&](const MCTelemetry::MCLink& link) { return link.Guest == guest; });
            return ended;
        }

    private:
        mutable std::mutex _Lock;
        std::vector<MCTelemetry::MCLink> _Ended;
    };

    /// <summary>Changes the certificate's fingerprint in <paramref name="description"/>: its last hex digit.</summary>
    void ChangeFingerprint(std::string& description)
    {
        const size_t line = description.find(FingerprintLine);
        REQUIRE(line != std::string::npos);
        const size_t end = description.find_first_of("\r\n", line);
        REQUIRE(end != std::string::npos);
        char& digit = description[end - 1];
        digit = digit == '0' ? '1' : '0';
    }

    std::span<const uint8_t> Bytes(std::string_view text)
    {
        return std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }

    /// <summary>Whether <paramref name="done"/> comes true before the deadline.</summary>
    template <typename Done> bool Until(Done done)
    {
        const auto deadline = std::chrono::steady_clock::now() + LinkDeadline;

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (done())
            {
                return true;
            }

            std::this_thread::sleep_for(PumpStep);
        }

        return done();
    }

    /// <summary>Moves a transport's signals and links along: every call does, an accept on no listener included.</summary>
    void Pump(MCIceTransport& transport)
    {
        MCSocket::Address from;
        transport.AcceptTcp(MCSocket::InvalidHandle, from);
    }

    /// <summary>Runs <paramref name="call"/> (which waits on the other game) while another thread pumps it.</summary>
    template <typename Call> auto WithPumped(MCIceTransport& other, Call&& call)
    {
        std::atomic<bool> stop = false;
        std::thread pumper(
            [&]
            {
                while (!stop)
                {
                    Pump(other);
                    std::this_thread::sleep_for(PumpStep);
                }
            });
        const auto result = call();
        stop = true;
        pumper.join();
        return result;
    }

    /// <summary>A datagram waiting at <paramref name="udp"/> as text (empty when none).</summary>
    std::string TakeDatagram(MCIceTransport& transport, MCSocket::Handle udp, MCSocket::Address& from)
    {
        std::array<uint8_t, 1500> datagram{};
        const int size = transport.ReceiveFrom(udp, datagram, from);
        return size > 0 ? std::string(datagram.begin(), datagram.begin() + size) : std::string();
    }

    /// <summary><paramref name="count"/> bytes no two runs of which line up by chance.</summary>
    std::vector<uint8_t> Pattern(size_t count, uint32_t seed)
    {
        std::vector<uint8_t> bytes(count);

        for (size_t i = 0; i < count; i++)
        {
            bytes[i] = static_cast<uint8_t>((static_cast<uint32_t>(i) * 2654435761u + seed) >> 13);
        }

        return bytes;
    }

    /// <summary>
    /// Sends the next part of <paramref name="bytes"/> from <paramref name="at"/> (parts of every size up to
    /// LargestSend), as much as the connection takes.
    /// </summary>
    /// <returns>The bytes taken, or -1 when the connection failed.</returns>
    int SendPart(MCIceTransport& transport, MCSocket::Handle stream, std::span<const uint8_t> bytes, size_t at)
    {
        const size_t size = std::min(at * 7 % LargestSend + 1, bytes.size() - at);
        return at < bytes.size() ? transport.Send(stream, bytes.subspan(at, size)) : 0;
    }

    void PutLe(std::vector<uint8_t>& out, uint32_t value, int bytes)
    {
        for (int i = 0; i < bytes; i++)
        {
            out.push_back(static_cast<uint8_t>(value >> (8 * i)));
        }
    }

    /// <summary>A frame as a game sends it: its kind, its body's size, its body.</summary>
    std::vector<uint8_t> Frame(uint8_t kind, std::span<const uint8_t> body)
    {
        std::vector<uint8_t> frame{kind};
        PutLe(frame, static_cast<uint32_t>(body.size()), 4);
        frame.insert(frame.end(), body.begin(), body.end());
        return frame;
    }

    /// <summary>
    /// Raw links from one guest to the game, as a game that isn't this one would open them: <see cref="Pass"/> trades
    /// their signals (link i numbered i + 1) and pumps the host.
    /// </summary>
    struct RawGuest
    {
        std::shared_ptr<MCIceSignaling> Signals;
        std::vector<std::unique_ptr<MCIceLink>> Links;

        RawGuest(MCFakeSignalBoard& board, size_t links) : Signals(board.Endpoint("raw"))
        {
            for (size_t i = 0; i < links; i++)
            {
                Links.push_back(MCIceLink::Offer(Signals->LinkSettings()));
            }
        }

        void Pass(MCIceTransport& host)
        {
            for (size_t i = 0; i < Links.size(); i++)
            {
                for (const MCIceLink::Signal& signal : Links[i]->TakeSignals())
                {
                    Signals->ToHost(GameCode, static_cast<uint32_t>(i + 1), signal);
                }
            }

            for (const MCIceSignaling::Heard& heard : Signals->Take())
            {
                if (heard.Signal.has_value() && heard.Link >= 1 && heard.Link <= Links.size())
                {
                    Links[heard.Link - 1]->Heard(*heard.Signal);
                }
            }

            Pump(host);
        }
    };

    /// <summary>Reads everything waiting on <paramref name="stream"/>; false when it reads as closed.</summary>
    bool Drain(MCIceTransport& transport, MCSocket::Handle stream, std::vector<uint8_t>& into)
    {
        std::array<uint8_t, 4096> buffer{};

        for (;;)
        {
            const int got = transport.Receive(stream, buffer);

            if (got <= 0)
            {
                return got == 0;
            }

            into.insert(into.end(), buffer.begin(), buffer.begin() + got);
        }
    }
}

TEST_CASE("ice transport: a guest's datagrams and connection reach the game it names, over one link")
{
    MCLinkLog links;
    MCFakeSignalBoard board;
    board.Host(GameCode, "host");
    MCIceTransport host(board.Endpoint("host"), {}, links.Reports());
    MCIceTransport guest(board.Endpoint("guest"), {}, links.Reports());
    const MCSocket::Handle listener = host.ListenTcp(GamePort);
    const MCSocket::Handle hostUdp = host.OpenUdp(GamePort);
    REQUIRE(listener != MCSocket::InvalidHandle);
    REQUIRE(hostUdp != MCSocket::InvalidHandle);

    // The game's address: the same each time, in the block no machine has.
    const std::optional<uint32_t> game = guest.Resolve(GameCode);
    REQUIRE(game.has_value());
    CHECK_EQ(*game >> 24, 0u);
    CHECK(guest.Resolve(GameCode) == game);
    CHECK(guest.Resolve("ZZ9P") != game);
    CHECK(!guest.Resolve("").has_value());

    // A datagram opens the link, goes once it's open, and the answer comes back from the game's address.
    const MCSocket::Handle guestUdp = guest.OpenUdp(0);
    REQUIRE(guestUdp != MCSocket::InvalidHandle);
    REQUIRE(guest.SendTo(guestUdp, {*game, GamePort}, Bytes("query")));
    CHECK_EQ(guest.Links(), size_t{1});
    MCSocket::Address asker;
    std::string query;
    REQUIRE(Until(
        [&]
        {
            Pump(guest);
            query = TakeDatagram(host, hostUdp, asker);
            return !query.empty();
        }));
    CHECK_EQ(query, std::string("query"));
    CHECK_EQ(asker.Port, guest.LocalPort(guestUdp));
    CHECK_EQ(asker.Ip >> 24, 0u);
    REQUIRE(host.SendTo(hostUdp, asker, Bytes("answer")));
    MCSocket::Address answerer;
    std::string answer;
    REQUIRE(Until(
        [&]
        {
            Pump(host);
            answer = TakeDatagram(guest, guestUdp, answerer);
            return !answer.empty();
        }));
    CHECK_EQ(answer, std::string("answer"));
    CHECK((answerer == MCSocket::Address{*game, GamePort}));
    // Neither a broadcast nor a machine's address goes anywhere.
    CHECK(!guest.SendTo(guestUdp, {MCSocket::BroadcastIp, GamePort}, Bytes("query")));
    CHECK(!guest.SendTo(guestUdp, {MCSocket::LoopbackIp, GamePort}, Bytes("query")));

    // The connection rides the same link, and the host's side names the guest as its datagrams did.
    const MCSocket::Handle stream = WithPumped(host, [&] { return guest.ConnectTcp({*game, GamePort}, ConnectMs); });
    REQUIRE(stream != MCSocket::InvalidHandle);
    MCSocket::Address connected;
    const MCSocket::Handle accepted = host.AcceptTcp(listener, connected);
    REQUIRE(accepted != MCSocket::InvalidHandle);
    CHECK((connected == MCSocket::Address{asker.Ip, guest.LocalPort(stream)}));
    CHECK_EQ(guest.Links(), size_t{1});
    CHECK_EQ(host.Links(), size_t{1});

    // Bytes both ways, in order, whatever the sends' sizes; more than the link holds unsent, so sends wait for room.
    const std::vector<uint8_t> fromGuest = Pattern(GuestBytes, 1);
    const std::vector<uint8_t> fromHost = Pattern(HostBytes, 2);
    std::vector<uint8_t> atHost;
    std::vector<uint8_t> atGuest;
    size_t sentByGuest = 0;
    size_t sentByHost = 0;
    bool failed = false;
    REQUIRE(Until(
        [&]
        {
            const int guestSent = SendPart(guest, stream, fromGuest, sentByGuest);
            const int hostSent = SendPart(host, accepted, fromHost, sentByHost);
            failed = guestSent < 0 || hostSent < 0 || !Drain(host, accepted, atHost) || !Drain(guest, stream, atGuest);
            sentByGuest += static_cast<size_t>(std::max(guestSent, 0));
            sentByHost += static_cast<size_t>(std::max(hostSent, 0));
            return failed || (atHost.size() >= fromGuest.size() && atGuest.size() >= fromHost.size());
        }));
    CHECK(!failed);
    CHECK(atHost == fromGuest);
    CHECK(atGuest == fromHost);

    // The host closing its end: the guest reads it as closed.
    host.Close(accepted);
    CHECK(Until(
        [&]
        {
            Pump(host);
            std::vector<uint8_t> after;
            return !Drain(guest, stream, after);
        }));

    // A port nothing listens on refuses the connection, well within its wait.
    const auto asked = std::chrono::steady_clock::now();
    CHECK_EQ(WithPumped(host, [&] { return guest.ConnectTcp({*game, GamePort + 1}, ConnectMs); }),
             MCSocket::InvalidHandle);
    CHECK(std::chrono::steady_clock::now() - asked < std::chrono::milliseconds(ConnectMs));

    // Once nothing uses it, the guest closes the link (once what it sent has gone), and the host's end goes with it.
    guest.Close(stream);
    CHECK_EQ(guest.Links(), size_t{1});
    guest.Close(guestUdp);
    CHECK(Until(
        [&]
        {
            Pump(guest);
            Pump(host);
            return guest.Links() == 0 && host.Links() == 0;
        }));

    // Each end reported its link as it ended: its path, what it carried (libdatachannel's counts) and how it ended.
    const std::vector<MCTelemetry::MCLink> guestLinks = links.Of(GuestEnd);
    const std::vector<MCTelemetry::MCLink> hostLinks = links.Of(HostEnd);
    REQUIRE_EQ(guestLinks.size(), size_t{1});
    REQUIRE_EQ(hostLinks.size(), size_t{1});
    const MCTelemetry::MCLink& guestLink = guestLinks[0];
    const MCTelemetry::MCLink& hostLink = hostLinks[0];
    CHECK(guestLink.Opened);
    CHECK(guestLink.Path == MCIceLink::Path::DirectIpv4);
    CHECK(guestLink.Open.count() > 0);
    CHECK(guestLink.Carried.BytesSent >= GuestBytes);
    CHECK(guestLink.Carried.BytesReceived >= HostBytes);
    CHECK(guestLink.Carried.RoundTrip.has_value());
    CHECK((guestLink.Here == MCIceLink::Offered{.Host = true}));
    CHECK((guestLink.There == MCIceLink::Offered{.Host = true}));
    CHECK(guestLink.End == MCLinkEnd::Unused);
    CHECK(hostLink.Opened);
    CHECK(hostLink.Path == MCIceLink::Path::DirectIpv4);
    CHECK(hostLink.Carried.BytesReceived >= GuestBytes);
    CHECK(hostLink.End == MCLinkEnd::Closed);
}

TEST_CASE("ice transport: a link to a game the service doesn't have, or whose host never answers, fails")
{
    MCLinkLog links;
    MCFakeSignalBoard board;
    // A host whose game never takes its signals.
    board.Host("MUTE", "nobody");
    MCIceTransport guest(board.Endpoint("guest"), {.Open = ShortOpenDeadline}, links.Reports());

    // No such game: the service says so, and the connection fails at once.
    const std::optional<uint32_t> missing = guest.Resolve("NONE");
    REQUIRE(missing.has_value());
    const auto asked = std::chrono::steady_clock::now();
    CHECK_EQ(guest.ConnectTcp({*missing, GamePort}, ConnectMs), MCSocket::InvalidHandle);
    CHECK(std::chrono::steady_clock::now() - asked < std::chrono::milliseconds(ConnectMs));
    CHECK_EQ(guest.Links(), size_t{0});
    REQUIRE_EQ(links.Of(GuestEnd).size(), size_t{1});
    CHECK(!links.Of(GuestEnd)[0].Opened);
    CHECK((links.Of(GuestEnd)[0].There == MCIceLink::Offered{}));
    CHECK(links.Of(GuestEnd)[0].End == MCLinkEnd::NoGame);

    // A host that never answers: the link waits out its deadline, then goes, and the next datagram tries again.
    const std::optional<uint32_t> mute = guest.Resolve("MUTE");
    REQUIRE(mute.has_value());
    const MCSocket::Handle udp = guest.OpenUdp(0);
    REQUIRE(guest.SendTo(udp, {*mute, GamePort}, Bytes("query")));
    CHECK_EQ(guest.Links(), size_t{1});
    CHECK(Until(
        [&]
        {
            Pump(guest);
            return guest.Links() == 0;
        }));
    REQUIRE_EQ(links.Of(GuestEnd).size(), size_t{2});
    const MCTelemetry::MCLink unanswered = links.Of(GuestEnd)[1];
    CHECK(!unanswered.Opened);
    CHECK(unanswered.Setup >= ShortOpenDeadline);
    CHECK((unanswered.Here == MCIceLink::Offered{.Host = true}));
    CHECK((unanswered.There == MCIceLink::Offered{}));
    CHECK(unanswered.End == MCLinkEnd::Unanswered);
    REQUIRE(guest.SendTo(udp, {*mute, GamePort}, Bytes("query")));
    CHECK_EQ(guest.Links(), size_t{1});
}

TEST_CASE("ice transport: a host closes the link of a guest that sends what isn't a frame")
{
    MCLinkLog links;
    MCFakeSignalBoard board;
    board.Host(GameCode, "host");
    MCIceTransport host(board.Endpoint("host"), {}, links.Reports());

    // A guest's offer is answered only while the game listens.
    const std::shared_ptr<MCIceSignaling> raw = board.Endpoint("raw");
    raw->ToHost(GameCode, RawLink, {"offer", "v=0", {}});
    Pump(host);
    CHECK_EQ(host.Links(), size_t{0});

    REQUIRE(host.ListenTcp(GamePort) != MCSocket::InvalidHandle);
    RawGuest guest(board, 1);
    REQUIRE(guest.Links[0] != nullptr);
    MCIceLink& link = *guest.Links[0];
    REQUIRE(Until(
        [&]
        {
            guest.Pass(host);
            return link.GetState() == MCIceLink::State::Open;
        }));
    CHECK_EQ(host.Links(), size_t{1});

    // A kind of frame the game never sends.
    const std::array<uint8_t, 5> notAFrame{0xee, 0, 0, 0, 0};
    REQUIRE(link.Send(notAFrame));
    CHECK(Until(
        [&]
        {
            guest.Pass(host);
            return link.GetState() != MCIceLink::State::Open;
        }));
    CHECK_EQ(host.Links(), size_t{0});
    REQUIRE_EQ(links.Of(HostEnd).size(), size_t{1});
    CHECK(links.Of(HostEnd)[0].Opened);
    CHECK(links.Of(HostEnd)[0].End == MCLinkEnd::CutOff);
}

TEST_CASE("ice transport: a host cuts off a guest that floods a connection the game doesn't read")
{
    MCLinkLog links;
    MCFakeSignalBoard board;
    board.Host(GameCode, "host");
    MCIceTransport host(board.Endpoint("host"), {}, links.Reports());
    const MCSocket::Handle listener = host.ListenTcp(GamePort);
    REQUIRE(listener != MCSocket::InvalidHandle);
    RawGuest guest(board, 1);
    REQUIRE(guest.Links[0] != nullptr);
    MCIceLink& link = *guest.Links[0];
    REQUIRE(Until(
        [&]
        {
            guest.Pass(host);
            return link.GetState() == MCIceLink::State::Open;
        }));

    // A connection, numbered 1, from port 50000: taken, then never read.
    std::vector<uint8_t> connect;
    PutLe(connect, 1, 4);
    PutLe(connect, GamePort, 2);
    PutLe(connect, 50000, 2);
    REQUIRE(link.Send(Frame(ConnectFrame, connect)));
    MCSocket::Address from;
    MCSocket::Handle accepted = MCSocket::InvalidHandle;
    REQUIRE(Until(
        [&]
        {
            guest.Pass(host);
            accepted = host.AcceptTcp(listener, from);
            return accepted != MCSocket::InvalidHandle;
        }));

    std::vector<uint8_t> data;
    PutLe(data, 1, 4);
    data.resize(data.size() + FloodFrameBytes);
    const std::vector<uint8_t> frame = Frame(DataFrame, data);

    for (size_t sent = 0; sent < FloodBytes; sent += FloodFrameBytes)
    {
        REQUIRE(link.Send(frame));
    }

    CHECK(Until(
        [&]
        {
            guest.Pass(host);
            return link.GetState() != MCIceLink::State::Open;
        }));
    CHECK_EQ(host.Links(), size_t{0});
    REQUIRE_EQ(links.Of(HostEnd).size(), size_t{1});
    CHECK(links.Of(HostEnd)[0].End == MCLinkEnd::CutOff);
}

TEST_CASE(
    "ice transport: a link that finds no path, or whose channel won't open on the path found, says how far it got")
{
    MCLinkLog links;
    MCFakeSignalBoard board;
    board.Host(GameCode, "host");
    // The service never carries the host's links here, so a link with no path fails as it would with no service.
    MCIceTransport host(board.Endpoint("host"), {.Open = ShortOpenDeadline, .Service = std::nullopt}, links.Reports());
    REQUIRE(host.ListenTcp(GamePort) != MCSocket::InvalidHandle);
    const auto refused = [&](MCIceTransport& guest)
    {
        const std::optional<uint32_t> game = guest.Resolve(GameCode);
        REQUIRE(game.has_value());
        CHECK_EQ(WithPumped(host, [&] { return guest.ConnectTcp({*game, GamePort}, ConnectMs); }),
                 MCSocket::InvalidHandle);
        CHECK(Until(
            [&]
            {
                Pump(guest);
                Pump(host);
                return guest.Links() == 0 && host.Links() == 0;
            }));
    };

    // A guest that may take only the relays' path, and has none: it offers no address and never tries the host's own
    // (its checks would tell the host its address), so no path is found.
    {
        MCIceTransport guest(board.Endpoint("relayed", {.RelayOnly = true, .BindAddress = "127.0.0.1"}),
                             {.Open = ShortOpenDeadline, .Service = std::nullopt}, links.Reports());
        refused(guest);
    }

    REQUIRE_EQ(links.Of(GuestEnd).size(), size_t{1});
    const MCTelemetry::MCLink relayed = links.Of(GuestEnd)[0];
    CHECK(!relayed.Opened);
    CHECK((relayed.Here == MCIceLink::Offered{}));
    CHECK((relayed.There == MCIceLink::Offered{.Host = true}));
    CHECK(relayed.End == MCLinkEnd::NoPath);
    REQUIRE_EQ(links.Of(HostEnd).size(), size_t{1});
    CHECK((links.Of(HostEnd)[0].There == MCIceLink::Offered{}));
    CHECK(links.Of(HostEnd)[0].End == MCLinkEnd::NoPath);

    // A service that changes the host's certificate's fingerprint on its way: ICE finds the path, the encryption
    // refuses the certificate the host shows, and the channel never opens. Nor does the service carry it instead: a
    // path was found, so what failed may be a lie.
    board.TamperToGuests(
        [](MCIceLink::Signal& signal)
        {
            if (signal.Kind == AnswerKind)
            {
                ChangeFingerprint(signal.Text);
            }
        });
    {
        MCIceTransport guest(board.Endpoint("guest"), {.Open = ShortOpenDeadline, .Service = ShortServiceDeadline},
                             links.Reports());
        refused(guest);
    }

    REQUIRE_EQ(links.Of(GuestEnd).size(), size_t{2});
    CHECK(!links.Of(GuestEnd)[1].Opened);
    CHECK(links.Of(GuestEnd)[1].End == MCLinkEnd::NoChannel);
}

TEST_CASE("ice transport: a link that finds no path goes through the service, and carries the game as ICE's would")
{
    MCLinkLog links;
    MCFakeSignalBoard board;
    board.Host(GameCode, "host");
    const MCIceTransport::Deadlines deadlines{.Service = ShortServiceDeadline};
    MCIceTransport host(board.Endpoint("host"), deadlines, links.Reports());
    // A guest whose ICE finds no path, as where UDP is blocked: the relays' path only, and no relay.
    MCIceTransport guest(board.Endpoint("blocked", {.RelayOnly = true, .BindAddress = "127.0.0.1"}), deadlines,
                         links.Reports());
    const MCSocket::Handle listener = host.ListenTcp(GamePort);
    const MCSocket::Handle hostUdp = host.OpenUdp(GamePort);
    REQUIRE(listener != MCSocket::InvalidHandle);
    REQUIRE(hostUdp != MCSocket::InvalidHandle);
    const std::optional<uint32_t> game = guest.Resolve(GameCode);
    REQUIRE(game.has_value());

    // The datagram waits while ICE looks, then goes through the service, and the answer comes back the same way.
    const MCSocket::Handle guestUdp = guest.OpenUdp(0);
    REQUIRE(guest.SendTo(guestUdp, {*game, GamePort}, Bytes("query")));
    MCSocket::Address asker;
    std::string query;
    REQUIRE(Until(
        [&]
        {
            Pump(guest);
            query = TakeDatagram(host, hostUdp, asker);
            return !query.empty();
        }));
    CHECK_EQ(query, std::string("query"));
    REQUIRE(host.SendTo(hostUdp, asker, Bytes("answer")));
    MCSocket::Address answerer;
    std::string answer;
    REQUIRE(Until(
        [&]
        {
            Pump(host);
            answer = TakeDatagram(guest, guestUdp, answerer);
            return !answer.empty();
        }));
    CHECK_EQ(answer, std::string("answer"));
    CHECK((answerer == MCSocket::Address{*game, GamePort}));

    // The connection, and bytes both ways in order.
    const MCSocket::Handle stream = WithPumped(host, [&] { return guest.ConnectTcp({*game, GamePort}, ConnectMs); });
    REQUIRE(stream != MCSocket::InvalidHandle);
    MCSocket::Address connected;
    const MCSocket::Handle accepted = host.AcceptTcp(listener, connected);
    REQUIRE(accepted != MCSocket::InvalidHandle);
    const std::vector<uint8_t> fromGuest = Pattern(ServiceBytes, 3);
    const std::vector<uint8_t> fromHost = Pattern(ServiceBytes, 4);
    std::vector<uint8_t> atHost;
    std::vector<uint8_t> atGuest;
    size_t sentByGuest = 0;
    size_t sentByHost = 0;
    bool failed = false;
    REQUIRE(Until(
        [&]
        {
            const int guestSent = SendPart(guest, stream, fromGuest, sentByGuest);
            const int hostSent = SendPart(host, accepted, fromHost, sentByHost);
            failed = guestSent < 0 || hostSent < 0 || !Drain(host, accepted, atHost) || !Drain(guest, stream, atGuest);
            sentByGuest += static_cast<size_t>(std::max(guestSent, 0));
            sentByHost += static_cast<size_t>(std::max(hostSent, 0));
            return failed || (atHost.size() >= fromGuest.size() && atGuest.size() >= fromHost.size());
        }));
    CHECK(!failed);
    CHECK(atHost == fromGuest);
    CHECK(atGuest == fromHost);
    CHECK_EQ(guest.Links(), size_t{1});
    CHECK_EQ(host.Links(), size_t{1});

    // A service holding a window's worth unsent holds the connection's sends, as a full ICE link does, and drops
    // datagrams, as a full UDP buffer does.
    board.HoldUnsent(ServiceWindowBytes);
    CHECK_EQ(guest.Send(stream, Bytes("held")), 0);
    REQUIRE(host.SendTo(hostUdp, asker, Bytes("dropped")));
    board.HoldUnsent(0);
    CHECK_EQ(guest.Send(stream, Bytes("goes")), 4);
    REQUIRE(host.SendTo(hostUdp, asker, Bytes("kept")));
    REQUIRE(Until(
        [&]
        {
            Pump(host);
            answer = TakeDatagram(guest, guestUdp, answerer);
            return !answer.empty();
        }));
    CHECK_EQ(answer, std::string("kept"));

    // Once nothing uses it, the guest closes it, and the service tells the host's end.
    guest.Close(stream);
    guest.Close(guestUdp);
    CHECK(Until(
        [&]
        {
            Pump(guest);
            Pump(host);
            return guest.Links() == 0 && host.Links() == 0;
        }));

    // Both ends report the service's path, what it carried, and no round trip (the service's isn't measured).
    REQUIRE_EQ(links.Of(GuestEnd).size(), size_t{1});
    REQUIRE_EQ(links.Of(HostEnd).size(), size_t{1});
    const MCTelemetry::MCLink guestLink = links.Of(GuestEnd)[0];
    const MCTelemetry::MCLink hostLink = links.Of(HostEnd)[0];
    CHECK(guestLink.Opened);
    CHECK(guestLink.Path == MCIceLink::Path::Service);
    CHECK(guestLink.Setup >= ShortServiceDeadline);
    CHECK(guestLink.Carried.BytesSent >= ServiceBytes);
    CHECK(guestLink.Carried.BytesReceived >= ServiceBytes);
    CHECK(!guestLink.Carried.RoundTrip.has_value());
    CHECK((guestLink.Here == MCIceLink::Offered{}));
    CHECK(guestLink.End == MCLinkEnd::Unused);
    CHECK(hostLink.Opened);
    CHECK(hostLink.Path == MCIceLink::Path::Service);
    CHECK(hostLink.Carried.BytesReceived >= ServiceBytes);
    CHECK(hostLink.End == MCLinkEnd::Closed);
}

TEST_CASE("ice transport: a link the other end takes nothing from is cut off")
{
    MCLinkLog links;
    MCFakeSignalBoard board;
    board.Host(GameCode, "host");
    const MCIceTransport::Deadlines deadlines{.Service = ShortServiceDeadline};
    MCIceTransport host(board.Endpoint("host"), deadlines, links.Reports());
    MCIceTransport guest(board.Endpoint("blocked", {.RelayOnly = true, .BindAddress = "127.0.0.1"}), deadlines,
                         links.Reports());
    REQUIRE(host.ListenTcp(GamePort) != MCSocket::InvalidHandle);
    const std::optional<uint32_t> game = guest.Resolve(GameCode);
    REQUIRE(game.has_value());
    const MCSocket::Handle stream = WithPumped(host, [&] { return guest.ConnectTcp({*game, GamePort}, ConnectMs); });
    REQUIRE(stream != MCSocket::InvalidHandle);

    // Past four windows unsent, the end that looks first (the guest's, pumped first) cuts its link off, and the other
    // hears it closed.
    board.HoldUnsent(4 * ServiceWindowBytes + 1);
    CHECK(Until(
        [&]
        {
            Pump(guest);
            Pump(host);
            return guest.Links() == 0 && host.Links() == 0;
        }));
    REQUIRE_EQ(links.Of(GuestEnd).size(), size_t{1});
    REQUIRE_EQ(links.Of(HostEnd).size(), size_t{1});
    CHECK(links.Of(GuestEnd)[0].End == MCLinkEnd::CutOff);
    CHECK(links.Of(HostEnd)[0].End == MCLinkEnd::Closed);
}

TEST_CASE("ice transport: a host keeps two links to a guest at most, and closes those that carry no connection")
{
    MCFakeSignalBoard board;
    board.Host(GameCode, "host");
    MCIceTransport host(board.Endpoint("host"), {.Idle = ShortIdleDeadline});
    REQUIRE(host.ListenTcp(GamePort) != MCSocket::InvalidHandle);
    RawGuest guest(board, 3);
    REQUIRE(std::ranges::none_of(guest.Links, [](const auto& link) { return link == nullptr; }));
    REQUIRE(Until(
        [&]
        {
            guest.Pass(host);
            return guest.Links[0]->GetState() == MCIceLink::State::Open &&
                   guest.Links[1]->GetState() == MCIceLink::State::Open;
        }));
    CHECK_EQ(host.Links(), size_t{2});
    CHECK(guest.Links[2]->GetState() == MCIceLink::State::Connecting);

    // Neither carries a connection: the host closes both after its idle deadline.
    CHECK(Until(
        [&]
        {
            guest.Pass(host);
            return host.Links() == 0 && guest.Links[0]->GetState() != MCIceLink::State::Open &&
                   guest.Links[1]->GetState() != MCIceLink::State::Open;
        }));
}

#include "stdafx.h"
#include "MCTest.h"
#include "platform/MCIceLink.h"

// The internet games' links (platform/MCIceLink: libdatachannel's data channels over libjuice's ICE), two ends in one
// process trading their signals directly, as the service will pass them. Both ends on the loopback (BindAddress), with
// no STUN or relay: the Mac's firewall holds the packets an unsigned program gets from the machine's other addresses
// once it has listened (the DirectPlay tests' hosts), until someone answers it.

namespace
{
    /// <summary>The ends' settings: the loopback only (see the file's comment).</summary>
    const MCIceLink::Settings OnThisMachine{.BindAddress = "127.0.0.1"};
    /// <summary>How long two ends on one machine get to open their link, or to see it closed.</summary>
    constexpr auto LinkDeadline = std::chrono::seconds(15);
    /// <summary>How long the pump waits between rounds.</summary>
    constexpr auto PumpStep = std::chrono::milliseconds(2);
    /// <summary>The bytes the guest's end sends, cut into sends of sizes up to this and beyond one message's.</summary>
    constexpr size_t GuestBytes = 300 * 1024;
    constexpr size_t HostBytes = 70 * 1024;
    constexpr size_t LargestSend = 40 * 1024;
    /// <summary>Twice what a link holds unread (1 MB).</summary>
    constexpr size_t FloodBytes = 2u << 20;
    /// <summary>
    /// IPv6's loopback, and its name: what a lookup of "localhost" finds first on a Mac (RFC 6724's order).
    /// </summary>
    constexpr std::string_view Ipv6Loopback = "::1";
    constexpr std::string_view LoopbackName = "localhost";
    /// <summary>How long a link offered only names is watched: a link on one machine opens in well under a second.</summary>
    constexpr auto NamedLinkWatch = std::chrono::seconds(3);

    /// <summary>
    /// Passes each end's signals to the other and reads what each received, until <paramref name="done"/> or the
    /// deadline.
    /// </summary>
    template <typename Done>
    bool Pump(MCIceLink& guest, MCIceLink& host, std::vector<uint8_t>& atGuest, std::vector<uint8_t>& atHost, Done done)
    {
        const auto deadline = std::chrono::steady_clock::now() + LinkDeadline;
        std::array<uint8_t, 4096> buffer{};

        while (std::chrono::steady_clock::now() < deadline)
        {
            for (const MCIceLink::Signal& signal : guest.TakeSignals())
            {
                host.Heard(signal);
            }

            for (const MCIceLink::Signal& signal : host.TakeSignals())
            {
                guest.Heard(signal);
            }

            for (size_t got = 0; (got = guest.Receive(buffer)) != 0;)
            {
                atGuest.insert(atGuest.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(got));
            }

            for (size_t got = 0; (got = host.Receive(buffer)) != 0;)
            {
                atHost.insert(atHost.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(got));
            }

            if (done())
            {
                return true;
            }

            std::this_thread::sleep_for(PumpStep);
        }

        return false;
    }

    /// <summary><paramref name="count"/> bytes no two runs of which line up by chance (each its index's hash).</summary>
    std::vector<uint8_t> Pattern(size_t count, uint32_t seed)
    {
        std::vector<uint8_t> bytes(count);

        for (size_t i = 0; i < count; i++)
        {
            bytes[i] = static_cast<uint8_t>((static_cast<uint32_t>(i) * 2654435761u + seed) >> 13);
        }

        return bytes;
    }

    /// <summary>Sends <paramref name="bytes"/> in sends of every size from 1 up to LargestSend, in turn.</summary>
    bool SendInParts(MCIceLink& link, std::span<const uint8_t> bytes)
    {
        size_t size = 1;

        for (size_t at = 0; at < bytes.size(); at += size, size = size * 3 % LargestSend + 1)
        {
            if (!link.Send(bytes.subspan(at, std::min(size, bytes.size() - at))))
            {
                return false;
            }
        }

        return true;
    }
}

TEST_CASE("ice: two games' ends open a link and carry bytes both ways in order")
{
    const std::unique_ptr<MCIceLink> guest = MCIceLink::Offer(OnThisMachine);
    const std::unique_ptr<MCIceLink> host = MCIceLink::Answer(OnThisMachine);
    REQUIRE(guest != nullptr && host != nullptr);
    CHECK(guest->GetState() == MCIceLink::State::Connecting);
    CHECK(guest->GetPath() == MCIceLink::Path::None);
    // Nothing goes before the link is open.
    CHECK(!guest->Send(Pattern(1, 0)));

    std::vector<uint8_t> atGuest;
    std::vector<uint8_t> atHost;
    REQUIRE(
        Pump(*guest, *host, atGuest, atHost, [&]
             { return guest->GetState() == MCIceLink::State::Open && host->GetState() == MCIceLink::State::Open; }));

    // The loopback's IPv4 address: a direct path.
    CHECK(guest->GetPath() == MCIceLink::Path::DirectIpv4);
    CHECK(host->GetPath() == MCIceLink::Path::DirectIpv4);

    const std::vector<uint8_t> fromGuest = Pattern(GuestBytes, 1);
    const std::vector<uint8_t> fromHost = Pattern(HostBytes, 2);
    REQUIRE(SendInParts(*guest, fromGuest));
    REQUIRE(SendInParts(*host, fromHost));
    REQUIRE(Pump(*guest, *host, atGuest, atHost,
                 [&] { return atHost.size() >= fromGuest.size() && atGuest.size() >= fromHost.size(); }));
    CHECK(atHost == fromGuest);
    CHECK(atGuest == fromHost);

    // Closing one end closes the other's.
    guest->Close();
    CHECK(guest->GetState() == MCIceLink::State::Closed);
    CHECK(Pump(*guest, *host, atGuest, atHost, [&] { return host->GetState() == MCIceLink::State::Closed; }));
    CHECK(!host->Send(Pattern(1, 0)));
}

TEST_CASE("ice: a link fails on a signal out of turn, one that doesn't parse, or too many addresses")
{
    // An offer heard by the offering end.
    const std::unique_ptr<MCIceLink> guest = MCIceLink::Offer(OnThisMachine);
    REQUIRE(guest != nullptr);
    guest->Heard({"offer", "v=0", {}});
    CHECK(guest->GetState() == MCIceLink::State::Failed);

    // A description that isn't one.
    const std::unique_ptr<MCIceLink> host = MCIceLink::Answer(OnThisMachine);
    REQUIRE(host != nullptr);
    host->Heard({"offer", "not a description", {}});
    CHECK(host->GetState() == MCIceLink::State::Failed);

    // A kind no end says.
    const std::unique_ptr<MCIceLink> other = MCIceLink::Answer(OnThisMachine);
    REQUIRE(other != nullptr);
    other->Heard({"hello", "", {}});
    CHECK(other->GetState() == MCIceLink::State::Failed);
    CHECK(other->CutOff());

    // More addresses than a game offers, before its description even: a link that isn't this game's.
    constexpr int tooManyCandidates = 65;
    const std::unique_ptr<MCIceLink> flooded = MCIceLink::Offer(OnThisMachine);
    REQUIRE(flooded != nullptr);

    for (int i = 0; i < tooManyCandidates; i++)
    {
        flooded->Heard({"candidate", "candidate:1 1 UDP 2122317823 127.0.0.1 50000 typ host", "0"});
    }

    CHECK(flooded->GetState() == MCIceLink::State::Failed);
    CHECK(flooded->CutOff());
}

TEST_CASE("ice: a relay-only link fails on a description hiding addresses behind a carriage return")
{
    // libdatachannel keeps a line it doesn't know ("a=candi\rdate:...") and passes it on to libjuice, which reads past
    // every carriage return: its checks would go to the addresses hidden there, which the relay-only filter never saw.
    // The guest's offer carries its own candidates so hidden; the host, relay only, must never find a path to them.
    const std::unique_ptr<MCIceLink> guest = MCIceLink::Offer(OnThisMachine);
    MCIceLink::Settings relayOnly = OnThisMachine;
    relayOnly.RelayOnly = true;
    const std::unique_ptr<MCIceLink> host = MCIceLink::Answer(relayOnly);
    REQUIRE(guest != nullptr && host != nullptr);

    std::optional<MCIceLink::Signal> offer;
    std::vector<MCIceLink::Signal> candidates;
    const auto gathered = std::chrono::steady_clock::now() + NamedLinkWatch;

    while ((!offer.has_value() || candidates.empty()) && std::chrono::steady_clock::now() < gathered)
    {
        for (MCIceLink::Signal& signal : guest->TakeSignals())
        {
            if (signal.Kind == "offer")
            {
                offer = std::move(signal);
            }
            else if (signal.Kind == "candidate")
            {
                candidates.push_back(std::move(signal));
            }
        }

        std::this_thread::sleep_for(PumpStep);
    }

    REQUIRE(offer.has_value());
    REQUIRE(!candidates.empty());
    constexpr std::string_view media = "\r\nm=";
    const size_t at = offer->Text.find(media);
    REQUIRE(at != std::string::npos);
    std::string hidden;

    for (const MCIceLink::Signal& candidate : candidates)
    {
        constexpr std::string_view name = "candidate";
        const std::string_view text = std::string_view(candidate.Text).substr(candidate.Text.starts_with("a=") ? 2 : 0);
        REQUIRE(text.starts_with(name));
        hidden += std::format("\r\na=candi\rdate{}", text.substr(name.size()));
    }

    offer->Text.insert(at, hidden);
    host->Heard(*offer);

    // The host's answer reaches the guest, which answers the checks of a host that tried the hidden addresses.
    const auto watched = std::chrono::steady_clock::now() + NamedLinkWatch;

    while (!host->FoundPath() && !host->CutOff() && std::chrono::steady_clock::now() < watched)
    {
        for (const MCIceLink::Signal& signal : host->TakeSignals())
        {
            guest->Heard(signal);
        }

        guest->TakeSignals();
        std::this_thread::sleep_for(PumpStep);
    }

    CHECK(!host->FoundPath());
    CHECK(host->GetState() == MCIceLink::State::Failed);
    CHECK(host->CutOff());

    // A candidate holding one, and a control character in a line, fail the same way.
    const std::unique_ptr<MCIceLink> other = MCIceLink::Offer(OnThisMachine);
    REQUIRE(other != nullptr);
    other->Heard({"candidate", "candidate:1 1 UDP 2122317823 127.0.0.1 50000 typ\rhost", "0"});
    CHECK(other->CutOff());
    const std::unique_ptr<MCIceLink> tabbed = MCIceLink::Offer(OnThisMachine);
    REQUIRE(tabbed != nullptr);
    tabbed->Heard({"candidate", "candidate:1 1 UDP 2122317823 127.0.0.1 50000 typ\x01host", "0"});
    CHECK(tabbed->CutOff());
}

TEST_CASE("ice: a link fails when the other end sends more than this end holds unread")
{
    const std::unique_ptr<MCIceLink> guest = MCIceLink::Offer(OnThisMachine);
    const std::unique_ptr<MCIceLink> host = MCIceLink::Answer(OnThisMachine);
    REQUIRE(guest != nullptr && host != nullptr);
    std::vector<uint8_t> atGuest;
    std::vector<uint8_t> atHost;
    REQUIRE(
        Pump(*guest, *host, atGuest, atHost, [&]
             { return guest->GetState() == MCIceLink::State::Open && host->GetState() == MCIceLink::State::Open; }));

    // The host never reads.
    REQUIRE(guest->Send(Pattern(FloodBytes, 3)));
    const auto deadline = std::chrono::steady_clock::now() + LinkDeadline;

    while (host->GetState() == MCIceLink::State::Open && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(PumpStep);
    }

    CHECK(host->GetState() == MCIceLink::State::Failed);
}

TEST_CASE("ice: a link never looks up a name the other end offers as its address")
{
    // Each end offers its address by a name, as a game that isn't this one could offer any name (libdatachannel would
    // look each up on a thread of its own, asking the name servers): here the loopback's, which a lookup finds, so a
    // link that looked them up would open. Both ends on IPv6's loopback, the name's first address.
    const MCIceLink::Settings onIpv6{.BindAddress = std::string(Ipv6Loopback)};
    const std::unique_ptr<MCIceLink> guest = MCIceLink::Offer(onIpv6);
    const std::unique_ptr<MCIceLink> host = MCIceLink::Answer(onIpv6);
    REQUIRE(guest != nullptr && host != nullptr);
    const std::string address = std::format(" {} ", Ipv6Loopback);
    const std::string name = std::format(" {} ", LoopbackName);
    size_t named = 0;
    const auto byName = [&](MCIceLink::Signal signal)
    {
        for (size_t at = 0; (at = signal.Text.find(address, at)) != std::string::npos; at += name.size())
        {
            signal.Text.replace(at, address.size(), name);
            named++;
        }

        return signal;
    };

    bool opened = false;
    const auto until = std::chrono::steady_clock::now() + NamedLinkWatch;

    while (!opened && std::chrono::steady_clock::now() < until)
    {
        for (const MCIceLink::Signal& signal : guest->TakeSignals())
        {
            host->Heard(byName(signal));
        }

        for (const MCIceLink::Signal& signal : host->TakeSignals())
        {
            guest->Heard(byName(signal));
        }

        opened = guest->GetState() == MCIceLink::State::Open || host->GetState() == MCIceLink::State::Open;
        std::this_thread::sleep_for(PumpStep);
    }

    // Both ends offered their addresses (by name), and neither tried them.
    CHECK(named >= 2);
    CHECK(guest->OfferedThere().Host);
    CHECK(host->OfferedThere().Host);
    CHECK(!opened);
    CHECK(!guest->FoundPath());
    CHECK(!host->FoundPath());
}

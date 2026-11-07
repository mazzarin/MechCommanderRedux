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

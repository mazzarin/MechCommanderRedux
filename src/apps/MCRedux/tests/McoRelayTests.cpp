#include "stdafx.h"
#include "../MCSystemTrust.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "fakes/MCFakeMcoRelay.h"
#include "network/MCMcoBridge.h"
#include "network/MCMcoContent.h"
#include "network/MCMcoRelay.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCSocket.h"
#include "platform/MCTlsLink.h"
#include "platform/MCTlsThreading.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <thread>

// Port-only (Redux HD): MechCommander Online's match relay as the bridge speaks it (network/MCMcoRelay.h,
// network/MCMcoBridge.h): its frames, hello and answers, the game's datagrams it lets through, MCO's fingerprint of the
// game data (network/MCMcoContent.h, its values worked out apart from it by a Python copy of MCO's launcher's code), the
// TLS link (platform/MCTlsLink.h) against a server in the test, and the bridges carrying a game over a stand-in for the
// relay (tests/fakes/MCFakeMcoRelay.h). DirectPlayTests.cpp plays a real session through them.

namespace
{
    using namespace std::chrono_literals;
    using MCMcoRelay::MCFrameType;
    using Clock = std::chrono::steady_clock;

    /// <summary>How long a test waits for what crosses the loopback.</summary>
    constexpr int ArrivesMs = 3000;
    /// <summary>How often a wait looks again.</summary>
    constexpr auto LookEvery = 5ms;
    /// <summary>A TLS link's time to connect in the tests, and a server's that never answers.</summary>
    constexpr int TlsTimeoutMs = 5000;
    constexpr int SilentTimeoutMs = 400;
    /// <summary>A bridge's wait for a host game that answers wrongly, and the most it may overrun it.</summary>
    constexpr int HostWaitMs = 600;
    constexpr int OverrunMs = 1500;
    /// <summary>Bytes sent at once across the bridge: several stream frames' worth.</summary>
    constexpr size_t ManyBytes = 100000;

    /// <summary>A game id of the tests' (the bridge passes any game's query; MCO's helper checks only its shape).</summary>
    constexpr _GUID TestGame{0x12345678, 0x1234, 0x5678, {1, 2, 3, 4, 5, 6, 7, 8}};

    std::span<const uint8_t> BytesOf(std::string_view text)
    {
        return std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }

    std::string TextOf(std::span<const uint8_t> bytes)
    {
        return std::string(bytes.begin(), bytes.end());
    }

    std::vector<uint8_t> Joined(std::initializer_list<std::vector<uint8_t>> parts)
    {
        std::vector<uint8_t> joined;

        for (const std::vector<uint8_t>& part : parts)
        {
            joined.insert(joined.end(), part.begin(), part.end());
        }

        return joined;
    }

    /// <summary>Whether <paramref name="done"/> comes true within <paramref name="ms"/>.</summary>
    template <class Done> bool Within(int ms, Done&& done)
    {
        const Clock::time_point until = Clock::now() + std::chrono::milliseconds(ms);

        while (!done())
        {
            if (Clock::now() > until)
            {
                return false;
            }

            std::this_thread::sleep_for(LookEvery);
        }

        return true;
    }

    /// <summary>The next datagram on <paramref name="udp"/> within the wait, and its sender.</summary>
    std::optional<std::vector<uint8_t>> ReceiveWithin(MCSocketTransport& sockets, MCSocket::Handle udp,
                                                      MCSocket::Address& from)
    {
        std::array<uint8_t, MCMcoRelay::DatagramMost + 1> datagram{};
        int size = 0;

        if (!Within(ArrivesMs, [&] { return (size = sockets.ReceiveFrom(udp, datagram, from)) > 0; }))
        {
            return std::nullopt;
        }

        return std::vector<uint8_t>(datagram.begin(), datagram.begin() + size);
    }

    MCSocket::Handle AcceptWithin(MCSocketTransport& sockets, MCSocket::Handle listener)
    {
        MCSocket::Handle accepted = MCSocket::InvalidHandle;
        MCSocket::Address from;
        Within(ArrivesMs, [&] { return (accepted = sockets.AcceptTcp(listener, from)) != MCSocket::InvalidHandle; });
        return accepted;
    }

    /// <summary>Up to <paramref name="count"/> bytes from <paramref name="tcp"/>, all that arrive within the wait.</summary>
    std::vector<uint8_t> ReadWithin(MCSocketTransport& sockets, MCSocket::Handle tcp, size_t count)
    {
        std::vector<uint8_t> read;
        std::array<uint8_t, MCMcoRelay::StreamMost> bytes{};
        Within(ArrivesMs,
               [&]
               {
                   const int took =
                       sockets.Receive(tcp, std::span(bytes).first(std::min(bytes.size(), count - read.size())));

                   if (took > 0)
                   {
                       read.insert(read.end(), bytes.begin(), bytes.begin() + took);
                   }

                   return took < 0 || read.size() >= count;
               });
        return read;
    }

    /// <summary>Whether <paramref name="tcp"/> is closed at its other end within the wait.</summary>
    bool ClosedWithin(MCSocketTransport& sockets, MCSocket::Handle tcp)
    {
        std::array<uint8_t, 64> bytes{};
        return Within(ArrivesMs, [&] { return sockets.Receive(tcp, bytes) < 0; });
    }

    /// <summary>A session's answer as the game writes it: the magic, kind 2, its port (little-endian), the session.</summary>
    std::vector<uint8_t> Answer(uint16_t port, std::string_view session)
    {
        std::vector<uint8_t> answer(MCMcoRelay::GameMagic.begin(), MCMcoRelay::GameMagic.end());
        answer.push_back(2);
        answer.push_back(static_cast<uint8_t>(port & 0xff));
        answer.push_back(static_cast<uint8_t>(port >> 8));
        answer.insert(answer.end(), session.begin(), session.end());
        return answer;
    }

    std::vector<uint8_t> ControlFrame(std::string_view json)
    {
        return MCMcoRelay::Frame(MCFrameType::Control, BytesOf(json));
    }
}

TEST_CASE("mco relay: frames are a type, a big-endian length and the payload, read across any split")
{
    CHECK(MCMcoRelay::Frame(MCFrameType::Control, BytesOf("{}")) == (std::vector<uint8_t>{1, 0, 2, '{', '}'}));
    CHECK(MCMcoRelay::Frame(MCFrameType::KeepAlive) == (std::vector<uint8_t>{6, 0, 0}));
    const std::vector<uint8_t> stream(MCMcoRelay::StreamMost, 0x5a);
    const std::vector<uint8_t> streamFrame = MCMcoRelay::Frame(MCFrameType::StreamData, stream);
    CHECK(std::vector<uint8_t>(streamFrame.begin(), streamFrame.begin() + 3) == (std::vector<uint8_t>{4, 0x40, 0x00}));
    CHECK_EQ(streamFrame.size(), MCMcoRelay::HeaderBytes + MCMcoRelay::StreamMost);

    // Each kind's sizes, at their edges.
    CHECK(MCMcoRelay::SizeAllowed(1, 1));
    CHECK(MCMcoRelay::SizeAllowed(1, MCMcoRelay::ControlMost));
    CHECK(!MCMcoRelay::SizeAllowed(1, 0));
    CHECK(!MCMcoRelay::SizeAllowed(1, MCMcoRelay::ControlMost + 1));
    CHECK(MCMcoRelay::SizeAllowed(2, MCMcoRelay::DatagramMost));
    CHECK(!MCMcoRelay::SizeAllowed(2, MCMcoRelay::DatagramMost + 1));
    CHECK(!MCMcoRelay::SizeAllowed(4, MCMcoRelay::StreamMost + 1));
    CHECK(MCMcoRelay::SizeAllowed(3, 0));
    CHECK(!MCMcoRelay::SizeAllowed(5, 1));
    CHECK(!MCMcoRelay::SizeAllowed(0, 0));
    CHECK(!MCMcoRelay::SizeAllowed(7, 0));

    const std::vector<uint8_t> datagram(300, 0x11);
    const std::vector<uint8_t> bytes =
        Joined({ControlFrame(R"({"status":"ready"})"), MCMcoRelay::Frame(MCFrameType::KeepAlive),
                MCMcoRelay::Frame(MCFrameType::Datagram, datagram), streamFrame});
    const auto expectFrames = [&](const std::vector<MCMcoRelay::MCFrame>& frames)
    {
        REQUIRE_EQ(frames.size(), size_t{4});
        CHECK(frames[0].Type == MCFrameType::Control);
        CHECK_EQ(TextOf(frames[0].Payload), std::string(R"({"status":"ready"})"));
        CHECK(frames[1].Type == MCFrameType::KeepAlive);
        CHECK(frames[1].Payload.empty());
        CHECK(frames[2].Type == MCFrameType::Datagram);
        CHECK(frames[2].Payload == datagram);
        CHECK(frames[3].Type == MCFrameType::StreamData);
        CHECK(frames[3].Payload == stream);
    };

    // A byte at a time, and all at once, give the same frames.
    MCMcoRelay::MCFrameReader trickled;
    std::vector<MCMcoRelay::MCFrame> fromTrickle;

    for (const uint8_t byte : bytes)
    {
        trickled.Feed(std::span(&byte, 1));

        while (std::optional<MCMcoRelay::MCFrame> frame = trickled.Next())
        {
            fromTrickle.push_back(std::move(*frame));
        }
    }

    expectFrames(fromTrickle);
    CHECK(!trickled.Bad());
    MCMcoRelay::MCFrameReader whole;
    whole.Feed(bytes);
    std::vector<MCMcoRelay::MCFrame> fromWhole;

    while (std::optional<MCMcoRelay::MCFrame> frame = whole.Next())
    {
        fromWhole.push_back(std::move(*frame));
    }

    expectFrames(fromWhole);
}

TEST_CASE("mco relay: a frame of a kind or a length the relay never sends marks the connection wrong, for good")
{
    for (const std::vector<uint8_t>& wrong : std::vector<std::vector<uint8_t>>{
             {7, 0, 0}, {0, 0, 0}, {6, 0, 1, 0}, {3, 0, 1, 0}, {1, 0, 0}, {4, 0x40, 0x01}, {2, 0x08, 0x01}})
    {
        MCTest::Scope scope(std::format("frame {:02x} {:02x} {:02x}", wrong[0], wrong[1], wrong[2]));
        MCMcoRelay::MCFrameReader reader;
        reader.Feed(wrong);
        CHECK(!reader.Next().has_value());
        CHECK(reader.Bad());
        reader.Feed(MCMcoRelay::Frame(MCFrameType::KeepAlive));
        CHECK(!reader.Next().has_value());
    }
}

TEST_CASE("mco relay: the hello and the relay's answers read as its helper writes and reads them")
{
    // The helper's order: the mode, then its ticket or grant, left out when there's none.
    CHECK_EQ(MCMcoRelay::Hello(MCMcoRelay::MCMode::Status), std::string(R"({"mode":"status"})"));
    CHECK_EQ(MCMcoRelay::Hello(MCMcoRelay::MCMode::Host, "T.k-1_"),
             std::string(R"({"mode":"host","ticket":"T.k-1_"})"));
    CHECK_EQ(MCMcoRelay::Hello(MCMcoRelay::MCMode::Join, "T"), std::string(R"({"mode":"join","ticket":"T"})"));
    CHECK_EQ(MCMcoRelay::Hello(MCMcoRelay::MCMode::Attach, "abc"), std::string(R"({"mode":"attach","grant":"abc"})"));

    const auto ready = MCMcoRelay::ReadReply(BytesOf(R"({"epoch":"e1","status":"ready"})"));
    REQUIRE(ready.has_value());
    CHECK_EQ(ready->Status, std::string("ready"));
    CHECK_EQ(ready->Epoch, std::string("e1"));
    CHECK(ready->Grant.empty());
    const auto attach = MCMcoRelay::ReadReply(BytesOf(R"({"status":"attach","grant":"g","epoch":null,"more":1})"));
    REQUIRE(attach.has_value());
    CHECK_EQ(attach->Grant, std::string("g"));
    CHECK(attach->Epoch.empty());

    const std::map<std::string, std::string> refused{
        {"{}", "the relay's answer has no status"},
        {R"({"status":3})", "the relay's status isn't text"},
        {R"({"status":"ready","grant":[1]})", "the relay's grant isn't text"},
        {"[1]", "the relay's answer isn't a JSON object"},
        {"not json", "the relay's answer isn't a JSON object"},
    };

    for (const auto& [payload, why] : refused)
    {
        MCTest::Scope scope(payload);
        const auto reply = MCMcoRelay::ReadReply(BytesOf(payload));
        REQUIRE(!reply.has_value());
        CHECK_EQ(reply.error(), why);
    }
}

TEST_CASE("mco relay: only the game's session query and answer pass, the answer's port made the guest's own")
{
    // MechCommander's query, as MCDirectPlay writes it: the magic, kind 1, the game's id little-endian.
    const std::array<uint8_t, MCMcoRelay::QueryBytes> query = MCMcoRelay::Query(MultiPlayerAppGuid);
    CHECK(query ==
          (std::array<uint8_t, MCMcoRelay::QueryBytes>{'M',  'C',  'G',  '1',  1,    0x00, 0x88, 0x60, 0x09, 0x15, 0x48,
                                                       0xd2, 0x11, 0x92, 0xd2, 0x00, 0x60, 0x97, 0x3c, 0xfb, 0x2c}));
    CHECK(MCMcoRelay::IsQuery(query));
    CHECK(!MCMcoRelay::IsAnswer(query));
    CHECK(!MCMcoRelay::IsQuery(std::span(query).first(MCMcoRelay::QueryBytes - 1)));
    std::vector<uint8_t> longer(query.begin(), query.end());
    longer.push_back(0);
    CHECK(!MCMcoRelay::IsQuery(longer));
    std::array<uint8_t, MCMcoRelay::QueryBytes> otherMagic = query;
    otherMagic[3] = '2';
    CHECK(!MCMcoRelay::IsQuery(otherMagic));

    std::vector<uint8_t> answer = Answer(28800, "Solaris");
    CHECK(MCMcoRelay::IsAnswer(answer));
    CHECK(!MCMcoRelay::IsQuery(answer));
    CHECK_EQ(MCMcoRelay::AnswerPort(answer), uint16_t{28800});
    MCMcoRelay::SetAnswerPort(answer, 0x1234);
    CHECK_EQ(int{answer[5]}, 0x34);
    CHECK_EQ(int{answer[6]}, 0x12);
    CHECK_EQ(MCMcoRelay::AnswerPort(answer), uint16_t{0x1234});
    CHECK_EQ(TextOf(std::span(answer).subspan(MCMcoRelay::AnswerLeast)), std::string("Solaris"));

    const std::vector<uint8_t> least = Answer(1, "");
    CHECK_EQ(least.size(), MCMcoRelay::AnswerLeast);
    CHECK(MCMcoRelay::IsAnswer(least));
    CHECK(!MCMcoRelay::IsAnswer(std::span(least).first(MCMcoRelay::AnswerLeast - 1)));
    const std::vector<uint8_t> most = Answer(1, std::string(MCMcoRelay::DatagramMost - MCMcoRelay::AnswerLeast, 's'));
    CHECK(MCMcoRelay::IsAnswer(most));
    CHECK(!MCMcoRelay::IsAnswer(Answer(1, std::string(MCMcoRelay::DatagramMost - MCMcoRelay::AnswerLeast + 1, 's'))));
}

namespace
{
    /// <summary>An .fst's table naming <paramref name="names"/> (each entry empty).</summary>
    std::string ArchiveNaming(std::initializer_list<std::string_view> names)
    {
        constexpr size_t nameBytes = 250;
        constexpr size_t numbers = 3;
        std::string archive;
        const auto put = [&](int32_t value)
        {
            for (size_t i = 0; i < sizeof(value); i++)
            {
                archive += static_cast<char>((static_cast<uint32_t>(value) >> (8 * i)) & 0xff);
            }
        };

        put(static_cast<int32_t>(names.size()));

        for (const std::string_view name : names)
        {
            for (size_t i = 0; i < numbers; i++)
            {
                put(0);
            }

            std::string padded(name);
            padded.resize(nameBytes, '\0');
            archive += padded;
        }

        return archive;
    }

    void Lay(const std::filesystem::path& root, const std::vector<std::pair<std::string_view, std::string>>& files)
    {
        for (const auto& [name, bytes] : files)
        {
            const std::filesystem::path path = root / std::filesystem::path(name);
            std::filesystem::create_directories(path.parent_path());
            std::ofstream(path, std::ios::binary) << bytes;
        }
    }
}

TEST_CASE("mco relay: the data's fingerprint is MCO's, the user folder overlaying the game's")
{
    MCScratchFolder scratch("mc_tests_mco", "fingerprint");
    const std::filesystem::path game = scratch.Path() / "game";
    const std::filesystem::path user = scratch.Path() / "user";
    // The same folders as the Python copy of MCO's code that worked the values out, which counts: ART.FST (a core
    // archive, though it holds only a mission's file), BROKEN.FST (not a table), MIXED.FST (not maps alone), the
    // objects, REDUXER/D.CSV, SUB/HD/H.PAK and SUB/WINDOWS.FIT; and leaves out MAPS.FST, the missions, saves, WINDOWS.FIT,
    // the .mpk and .pkk, the text, the Temp, Tmp and Redux folders' files, Redux HD's own folders and the link.
    Lay(game, {
                  {"ART.FST", ArchiveNaming({"data\\missions\\m1.abl"})},
                  {"MAPS.FST", ArchiveNaming({"DATA\\MISSIONS\\M1.FIT", "data/terrain/t1.pak"})},
                  {"MIXED.FST", ArchiveNaming({"DATA\\MISSIONS\\M2.FIT", "DATA\\ART\\X.TGA"})},
                  {"BROKEN.FST", "abc"},
                  {"data/objects/mech.csv", "mech,1\n"},
                  {"data/objects/weapons.FIT", "game's weapons"},
                  {"DATA/MISSIONS/m1.fit", "mission"},
                  {"data/savegame/s.fit", "save"},
                  {"WINDOWS.FIT", "windows"},
                  {"sub/WINDOWS.FIT", "not the top one"},
                  {"music.mpk", "m"},
                  {"x.PKK", "p"},
                  {"readme.txt", "r"},
                  {"Temp/a.csv", "skipped"},
                  {"sub/tmp/b.csv", "skipped"},
                  {"REDUX/c.csv", "skipped"},
                  {"Reduxer/d.csv", "counted"},
                  {"Redux update/e.csv", "ours"},
                  {"Redux backup/f.csv", "ours"},
                  {"HD/g.pak", "ours"},
                  {"sub/HD/h.pak", "not ours"},
              });
    // A link where links can be made (Windows asks for a privilege): never followed, so it changes nothing.
    std::error_code linked;
    std::filesystem::create_symlink(game / "data/objects/mech.csv", game / "link.csv", linked);
    // The user folder: its objects and archives stand in for the game's (DATA/OBJECTS/WEAPONS.FIT) or add to them; its
    // Temp isn't skipped as the game folder's is (MCO's rule); its other files, saves and HD aren't counted.
    Lay(user, {
                  {"data/objects/weapons.fit", "the user's weapons"},
                  {"data/objects/new.abl", "new"},
                  {"data/other/z.csv", "not an object"},
                  {"extra.fst", "xyz"},
                  {"temp/q.fst", "q"},
                  {"data/savegame/s.fst", "save"},
                  {"HD/k.fst", "ours"},
                  {"prefs.cfg", "prefs"},
              });

    const auto alone = MCMcoContent::Fingerprint(game);
    REQUIRE(alone.has_value());
    CHECK_EQ(*alone, std::string("f87606529b7d2a3c55096d34d56b74db62dfcea617b43eb91525ef5c9176f37a"));
    const auto overlaid = MCMcoContent::Fingerprint(game, user);
    REQUIRE(overlaid.has_value());
    CHECK_EQ(*overlaid, std::string("25f207fc64a0899e59ff13bbbd8419de8f658f8810e6e06811d1eaee648a6e34"));
    const auto noUser = MCMcoContent::Fingerprint(game, scratch.Path() / "no such folder");
    REQUIRE(noUser.has_value());
    CHECK_EQ(*noUser, *alone);

    const std::filesystem::path empty = scratch.Path() / "empty";
    std::filesystem::create_directories(empty);
    CHECK(!MCMcoContent::Fingerprint(empty).has_value());
    CHECK(!MCMcoContent::Fingerprint(scratch.Path() / "no such folder").has_value());
}

namespace
{
    // A test authority, a second one, and the localhost server's certificate (localhost, 127.0.0.1) the first signed
    // with its key: EC P-256, good from 8 Nov 2026 to 2126, made with OpenSSL for these tests alone.
    constexpr std::string_view TestAuthorityPem = R"(-----BEGIN CERTIFICATE-----
MIIBljCCATygAwIBAgIUKdqRjbJ6G6r1FowrNbxIsGbN3tswCgYIKoZIzj0EAwIw
KDEmMCQGA1UEAwwdUmVkdXggSEQgdGVzdCByZWxheSBhdXRob3JpdHkwIBcNMjYx
MTA4MDAwMDAwWhgPMjEyNjExMDgwMDAwMDBaMCgxJjAkBgNVBAMMHVJlZHV4IEhE
IHRlc3QgcmVsYXkgYXV0aG9yaXR5MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE
ah3j0/Dyg2GpuhwXcYvJB3d6C0up84Gi/XyPzl5j94Kj8lFiRe4KD+jxWJp+iPg8
OVfUy0J51CiDwj6+UgZpAKNCMEAwHQYDVR0OBBYEFMfNXqWSVIvU8if75eJDdXLw
m11qMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQDAgEGMAoGCCqGSM49BAMC
A0gAMEUCID1u/BHG+VshH0e4dmi+ukMoMUGT0Ewt29Rh0O1mQwMuAiEAvFz7LNxh
JlPaw0o6i83t4bsTn5FLI/LLwjDGZwhz6Do=
-----END CERTIFICATE-----
)";
    constexpr std::string_view OtherAuthorityPem = R"(-----BEGIN CERTIFICATE-----
MIIBlTCCATygAwIBAgIUCB6VAjKpnJzDtlocZfWcmECwkZQwCgYIKoZIzj0EAwIw
KDEmMCQGA1UEAwwdUmVkdXggSEQgdGVzdCBvdGhlciBhdXRob3JpdHkwIBcNMjYx
MTA4MDAwMDAwWhgPMjEyNjExMDgwMDAwMDBaMCgxJjAkBgNVBAMMHVJlZHV4IEhE
IHRlc3Qgb3RoZXIgYXV0aG9yaXR5MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE
FosAgiR9/Ac+bxK87iXhH5Y8841POLMooZhq/aC8SxrD/7gR9iy6tGUbtIDQgrOr
MPvsLkf3EzUnBNg5aMOjrqNCMEAwHQYDVR0OBBYEFEcUKyd8atd7cavto3x0xrjU
htYEMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQDAgEGMAoGCCqGSM49BAMC
A0cAMEQCIB1O/OI1Z1gYDJn7ocrG09fqqr+FASakC7GIucTzPfJ1AiAzs+ur5/82
HTC/6kbfqmwcozYJ092eFdUsUnFrH4onfQ==
-----END CERTIFICATE-----
)";
    constexpr std::string_view ServerPem = R"(-----BEGIN CERTIFICATE-----
MIIBjDCCATOgAwIBAgITdLv15BqmRp/cC9pnxo8Xa7yZsDAKBggqhkjOPQQDAjAo
MSYwJAYDVQQDDB1SZWR1eCBIRCB0ZXN0IHJlbGF5IGF1dGhvcml0eTAgFw0yNjEx
MDgwMDAwMDBaGA8yMTI2MTEwODAwMDAwMFowFDESMBAGA1UEAwwJbG9jYWxob3N0
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAErVpfugAtHIEBtdRk9PSsFs9UfA8T
s4L93a03JcpfEgY3JOJz4uiIkIn3Xa2QtfYhNbq63CUvKKWN9hPo9NH0+qNOMEww
CQYDVR0TBAIwADAOBgNVHQ8BAf8EBAMCB4AwEwYDVR0lBAwwCgYIKwYBBQUHAwEw
GgYDVR0RBBMwEYIJbG9jYWxob3N0hwR/AAABMAoGCCqGSM49BAMCA0cAMEQCIFmK
zRcpooIbR8mPUlUeEu4yW/vM4uUGGfgjtv42IbGiAiAZ5mabS7ox3ySBly7LCEMU
G5Nf/TK5awnIxEnOh8bshg==
-----END CERTIFICATE-----
)";
    constexpr std::string_view ServerKeyPem = R"(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgYFrqJdyUxv155x7o
V9/Nitw/Yuir9rJgOalNO72rSpKhRANCAAStWl+6AC0cgQG11GT09KwWz1R8DxOz
gv3drTclyl8SBjck4nPi6IiQifddrZC19iE1urrcJS8opY32E+j00fT6
-----END PRIVATE KEY-----
)";
    constexpr std::string_view ServerName = "localhost";
    /// <summary>How long the test's server waits for its one connection, and its loop's longest wait.</summary>
    constexpr int ServerAcceptMs = 10000;
    constexpr int ServerLoopMs = 20;
    constexpr size_t EchoBytes = 4096;

    /// <summary>The bytes Mbed TLS parses PEM from: the text with its terminating NUL (a literal's).</summary>
    std::span<const unsigned char> PemBytes(std::string_view pem)
    {
        return std::span(reinterpret_cast<const unsigned char*>(pem.data()), pem.size() + 1);
    }

    /// <summary>The test server's connection, as Mbed TLS's callbacks reach it.</summary>
    struct MCServerSocket
    {
        MCSocketTransport& Sockets;
        MCSocket::Handle Socket;
    };

    int ServerSend(void* context, const unsigned char* data, size_t size)
    {
        MCServerSocket& server = *static_cast<MCServerSocket*>(context);
        const int sent = server.Sockets.Send(server.Socket, std::span<const uint8_t>(data, size));
        return sent > 0 ? sent : sent == 0 ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
    }

    int ServerReceive(void* context, unsigned char* data, size_t size)
    {
        MCServerSocket& server = *static_cast<MCServerSocket*>(context);
        const int received = server.Sockets.Receive(server.Socket, std::span<uint8_t>(data, size));
        return received > 0 ? received : received == 0 ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_CONN_RESET;
    }

    /// <summary>
    /// A TLS server on 127.0.0.1 for one connection, with the test authority's certificate for localhost: it speaks
    /// <see cref="MCSetup::Alpn"/> (none: no protocol) and echoes what it's sent; or, not <see cref="MCSetup::Speaks"/>,
    /// takes the connection and never answers.
    /// </summary>
    class MCTlsServer
    {
    public:
        struct MCSetup
        {
            std::string Alpn{MCMcoRelay::Alpn};
            bool Speaks = true;
        };

        explicit MCTlsServer(MCSetup setup) : _Setup(std::move(setup))
        {
            MCStartTlsThreading();
            psa_crypto_init();
            _Listener = _Sockets.ListenTcpOn(MCSocket::LoopbackIp, 0);
            _Port = _Listener == MCSocket::InvalidHandle ? 0 : _Sockets.LocalPort(_Listener);
            _Thread = std::thread([this] { Run(); });
        }

        ~MCTlsServer()
        {
            _Stopping = true;
            _Thread.join();
            _Sockets.Close(_Listener);
        }

        MCTlsServer(const MCTlsServer&) = delete;
        MCTlsServer& operator=(const MCTlsServer&) = delete;

        uint16_t Port() const { return _Port; }

    private:
        bool Wait(MCSocket::Handle socket, bool write) const
        {
            std::array<MCSocketTransport::MCWaiting, 1> waiting{MCSocketTransport::MCWaiting{socket, write}};
            MCSocketTransport::Wait(waiting, ServerLoopMs);
            return !_Stopping;
        }

        void Run()
        {
            MCSocket::Handle socket = MCSocket::InvalidHandle;
            const Clock::time_point until = Clock::now() + std::chrono::milliseconds(ServerAcceptMs);

            while (socket == MCSocket::InvalidHandle && Wait(_Listener, false) && Clock::now() < until)
            {
                MCSocket::Address from;
                socket = _Sockets.AcceptTcp(_Listener, from);
            }

            if (socket != MCSocket::InvalidHandle && _Setup.Speaks)
            {
                Serve(socket);
            }

            while (socket != MCSocket::InvalidHandle && !_Setup.Speaks && Wait(socket, false))
            {
            }

            _Sockets.Close(socket);
        }

        void Serve(MCSocket::Handle socket)
        {
            mbedtls_entropy_context entropy;
            mbedtls_ctr_drbg_context random;
            mbedtls_x509_crt chain;
            mbedtls_pk_context key;
            mbedtls_ssl_config config;
            mbedtls_ssl_context ssl;
            mbedtls_entropy_init(&entropy);
            mbedtls_ctr_drbg_init(&random);
            mbedtls_x509_crt_init(&chain);
            mbedtls_pk_init(&key);
            mbedtls_ssl_config_init(&config);
            mbedtls_ssl_init(&ssl);
            std::array<const char*, 2> alpn{_Setup.Alpn.c_str(), nullptr};
            MCServerSocket connection{_Sockets, socket};
            const auto serverPem = PemBytes(ServerPem);
            const auto authorityPem = PemBytes(TestAuthorityPem);
            const auto keyPem = PemBytes(ServerKeyPem);
            bool ready = mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy, nullptr, 0) == 0 &&
                         mbedtls_x509_crt_parse(&chain, serverPem.data(), serverPem.size()) == 0 &&
                         mbedtls_x509_crt_parse(&chain, authorityPem.data(), authorityPem.size()) == 0 &&
                         mbedtls_pk_parse_key(&key, keyPem.data(), keyPem.size(), nullptr, 0, mbedtls_ctr_drbg_random,
                                              &random) == 0 &&
                         mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                                     MBEDTLS_SSL_PRESET_DEFAULT) == 0;

            if (ready)
            {
                mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &random);
                ready = mbedtls_ssl_conf_own_cert(&config, &chain, &key) == 0 &&
                        (_Setup.Alpn.empty() || mbedtls_ssl_conf_alpn_protocols(&config, alpn.data()) == 0) &&
                        mbedtls_ssl_setup(&ssl, &config) == 0;
            }

            if (ready)
            {
                mbedtls_ssl_set_bio(&ssl, &connection, &ServerSend, &ServerReceive, nullptr);
                int code = 0;

                while ((code = mbedtls_ssl_handshake(&ssl)) == MBEDTLS_ERR_SSL_WANT_READ ||
                       code == MBEDTLS_ERR_SSL_WANT_WRITE)
                {
                    if (!Wait(socket, code == MBEDTLS_ERR_SSL_WANT_WRITE))
                    {
                        break;
                    }
                }

                if (code == 0)
                {
                    Echo(ssl, socket);
                }
            }

            mbedtls_ssl_free(&ssl);
            mbedtls_ssl_config_free(&config);
            mbedtls_pk_free(&key);
            mbedtls_x509_crt_free(&chain);
            mbedtls_ctr_drbg_free(&random);
            mbedtls_entropy_free(&entropy);
        }

        void Echo(mbedtls_ssl_context& ssl, MCSocket::Handle socket)
        {
            std::array<unsigned char, EchoBytes> bytes{};

            for (;;)
            {
                const int read = mbedtls_ssl_read(&ssl, bytes.data(), bytes.size());

                if (read == MBEDTLS_ERR_SSL_WANT_READ || read == MBEDTLS_ERR_SSL_WANT_WRITE)
                {
                    if (!Wait(socket, read == MBEDTLS_ERR_SSL_WANT_WRITE))
                    {
                        return;
                    }

                    continue;
                }

                if (read <= 0)
                {
                    return;
                }

                for (int sent = 0; sent < read;)
                {
                    const int took = mbedtls_ssl_write(&ssl, bytes.data() + sent, static_cast<size_t>(read - sent));

                    if (took > 0)
                    {
                        sent += took;
                    }
                    else if ((took != MBEDTLS_ERR_SSL_WANT_READ && took != MBEDTLS_ERR_SSL_WANT_WRITE) ||
                             !Wait(socket, took == MBEDTLS_ERR_SSL_WANT_WRITE))
                    {
                        return;
                    }
                }
            }
        }

        MCSetup _Setup;
        MCSocketTransport _Sockets;
        MCSocket::Handle _Listener = MCSocket::InvalidHandle;
        uint16_t _Port = 0;
        std::atomic<bool> _Stopping = false;
        std::thread _Thread;
    };

    /// <summary>A computer's trust that answers as it's told, keeping what it was asked.</summary>
    class MCToldTrust final : public MCTlsLink::MCTrust
    {
    public:
        explicit MCToldTrust(std::string answer) : _Answer(std::move(answer)) {}

        std::string Check(std::span<const std::span<const uint8_t>> chain, std::string_view host) const override
        {
            Certificates = chain.size();
            Host = host;
            return _Answer;
        }

        mutable size_t Certificates = 0;
        mutable std::string Host;

    private:
        std::string _Answer;
    };

    MCTlsLink::MCSettings TestSettings(uint16_t port)
    {
        return MCTlsLink::MCSettings{.Host = std::string(ServerName),
                                     .Port = port,
                                     .Alpn = std::string(MCMcoRelay::Alpn),
                                     .TimeoutMs = TlsTimeoutMs,
                                     .AnchorPem = std::string(TestAuthorityPem)};
    }
}

TEST_CASE("mco relay: a TLS link to a server its one authority vouches for, speaking its protocol, carries bytes")
{
    MCTlsServer server({});
    REQUIRE(server.Port() != 0);
    auto link = MCTlsLink::Connect(TestSettings(server.Port()));

    if (!link.has_value())
    {
        FAIL(link.error());
    }

    MCTlsLink& open = **link;
    CHECK(open.Socket() != MCSocket::InvalidHandle);
    REQUIRE(open.Write(BytesOf("ping"), TlsTimeoutMs).has_value());
    std::string echoed;
    std::array<uint8_t, 16> bytes{};
    CHECK(Within(ArrivesMs,
                 [&]
                 {
                     const int read = open.Read(bytes);

                     if (read > 0)
                     {
                         echoed += TextOf(std::span(bytes).first(static_cast<size_t>(read)));
                     }

                     return read < 0 || echoed.size() >= 4;
                 }));
    CHECK_EQ(echoed, std::string("ping"));

    open.Close();
    CHECK_EQ(open.Socket(), MCSocket::InvalidHandle);
    CHECK_EQ(open.Read(bytes), -1);
    CHECK(!open.Write(BytesOf("x"), TlsTimeoutMs).has_value());
}

TEST_CASE("mco relay: a TLS link refuses a server another authority vouches for, or one not speaking its protocol")
{
    {
        MCTlsServer server({});
        MCTlsLink::MCSettings settings = TestSettings(server.Port());
        settings.AnchorPem = std::string(OtherAuthorityPem);
        CHECK(!MCTlsLink::Connect(settings).has_value());
    }

    {
        MCTlsServer server({.Alpn = ""});
        const auto link = MCTlsLink::Connect(TestSettings(server.Port()));
        REQUIRE(!link.has_value());
        CHECK_EQ(link.error(), std::format("the server doesn't speak {}", MCMcoRelay::Alpn));
    }

    {
        MCTlsServer server({.Alpn = "h2"});
        CHECK(!MCTlsLink::Connect(TestSettings(server.Port())).has_value());
    }

    {
        MCTlsLink::MCSettings settings = TestSettings(1);
        settings.AnchorPem.clear();
        const auto link = MCTlsLink::Connect(settings);
        REQUIRE(!link.has_value());
        CHECK_EQ(link.error(), std::string("no trust to check the server's certificates by"));
    }
}

TEST_CASE("mco relay: a TLS link hands the computer's trust the server's chain and its name, and holds to its word")
{
    {
        MCTlsServer server({});
        const MCToldTrust trusting("");
        MCTlsLink::MCSettings settings = TestSettings(server.Port());
        settings.AnchorPem.clear();
        settings.Trust = trusting;
        const auto link = MCTlsLink::Connect(settings);

        if (!link.has_value())
        {
            FAIL_CHECK(link.error());
        }

        CHECK_EQ(trusting.Certificates, size_t{2});
        CHECK_EQ(trusting.Host, std::string(ServerName));
    }

    {
        MCTlsServer server({});
        const MCToldTrust doubting("not today");
        MCTlsLink::MCSettings settings = TestSettings(server.Port());
        settings.AnchorPem.clear();
        settings.Trust = doubting;
        const auto link = MCTlsLink::Connect(settings);
        REQUIRE(!link.has_value());
        CHECK_EQ(link.error(), std::string("the server's certificate isn't trusted: not today"));
    }

    {
        // The computer's own trust knows nothing of the test authority.
        MCTlsServer server({});
        const std::unique_ptr<MCTlsLink::MCTrust> system = MCMakeSystemTrust();
        MCTlsLink::MCSettings settings = TestSettings(server.Port());
        settings.AnchorPem.clear();
        settings.Trust = *system;
        const auto link = MCTlsLink::Connect(settings);
        REQUIRE(!link.has_value());
        CHECK(link.error().starts_with("the server's certificate isn't trusted: "));
    }
}

TEST_CASE("mco relay: a TLS link gives up on a server that takes the connection and never answers")
{
    MCTlsServer server({.Speaks = false});
    MCTlsLink::MCSettings settings = TestSettings(server.Port());
    settings.TimeoutMs = SilentTimeoutMs;
    const Clock::time_point start = Clock::now();
    const auto link = MCTlsLink::Connect(settings);
    REQUIRE(!link.has_value());
    CHECK_EQ(link.error(), std::format("{}:{} took too long to answer", ServerName, server.Port()));
    CHECK(Clock::now() - start < std::chrono::milliseconds(SilentTimeoutMs + OverrunMs));
}

TEST_CASE("mco relay: the bridges carry a guest's game to the host's through the relay, as MCO's helper does")
{
    MCFakeMcoRelay::MCRelay relay;
    REQUIRE(relay.Port() != 0);
    MCSocketTransport sockets;
    // The host's game: its session's answers on UDP and its joins on TCP, one port.
    const MCSocket::Handle gameListener = sockets.ListenTcpOn(MCSocket::LoopbackIp, 0);
    const uint16_t gamePort = sockets.LocalPort(gameListener);
    const MCSocket::Handle gameUdp = sockets.OpenUdpOn(MCSocket::LoopbackIp, gamePort);
    REQUIRE(gameUdp != MCSocket::InvalidHandle);

    const auto epoch = MCMcoBridge::Probe(relay.Dial());
    REQUIRE(epoch.has_value());
    CHECK_EQ(*epoch, std::string(MCFakeMcoRelay::Epoch));
    auto host = MCMcoBridge::MCHost::Start(relay.Dial(), "host-ticket", gamePort, {});
    REQUIRE(host.has_value());
    // The relay answers a guest once the host has opened a connection for it.
    auto guest = MCMcoBridge::MCGuest::Start(relay.Dial(), "join-ticket", {});

    if (!guest.has_value())
    {
        FAIL(guest.error());
    }

    CHECK(relay.Hellos() == (std::vector<std::string>{"status ", "host host-ticket", "join join-ticket",
                                                      std::format("attach {:032x}", 1)}));
    CHECK_EQ(relay.Pairs(), size_t{1});
    CHECK_EQ((*host)->Guests(), size_t{1});
    CHECK((*guest)->Running());
    CHECK(!(*guest)->Connected());

    // The guest's game asks for the session at the guest's port: the host's game hears the question alone, not
    // datagrams of other shapes.
    const MCSocket::Handle guestGame = sockets.OpenUdpOn(MCSocket::LoopbackIp, 0);
    const MCSocket::Address guestBridge{MCSocket::LoopbackIp, (*guest)->Port()};
    const std::array<uint8_t, MCMcoRelay::QueryBytes> query = MCMcoRelay::Query(TestGame);
    sockets.SendTo(guestGame, guestBridge, std::vector<uint8_t>(MCMcoRelay::QueryBytes + 1, 'M'));
    sockets.SendTo(guestGame, guestBridge, query);
    MCSocket::Address asker;
    const auto asked = ReceiveWithin(sockets, gameUdp, asker);
    REQUIRE(asked.has_value());
    CHECK(*asked == std::vector<uint8_t>(query.begin(), query.end()));
    CHECK(MCSocket::IsLoopback(asker.Ip));

    // Its answer, naming the game's port, reaches the guest's game naming the guest's.
    sockets.SendTo(gameUdp, asker, Answer(gamePort, "Solaris"));
    MCSocket::Address answerer;
    const auto answered = ReceiveWithin(sockets, guestGame, answerer);
    REQUIRE(answered.has_value());
    CHECK(answerer == guestBridge);
    CHECK_EQ(MCMcoRelay::AnswerPort(*answered), (*guest)->Port());
    CHECK_EQ(TextOf(std::span(*answered).subspan(MCMcoRelay::AnswerLeast)), std::string("Solaris"));
    CHECK_EQ(relay.Passed(MCFrameType::Datagram), size_t{2});

    // The guest's game joins where the answer says: the host's game is joined by the host's bridge, and bytes cross
    // both ways, many at once whole and in order.
    const MCSocket::Handle guestTcp = sockets.ConnectTcp(guestBridge, ArrivesMs);
    REQUIRE(guestTcp != MCSocket::InvalidHandle);
    const MCSocket::Handle hostTcp = AcceptWithin(sockets, gameListener);
    REQUIRE(hostTcp != MCSocket::InvalidHandle);
    CHECK(Within(ArrivesMs, [&] { return (*guest)->Connected(); }));
    CHECK(MCFakeMcoRelay::SendAll(sockets, guestTcp, BytesOf("to host"), ArrivesMs).has_value());
    CHECK_EQ(TextOf(ReadWithin(sockets, hostTcp, 7)), std::string("to host"));
    std::vector<uint8_t> many(ManyBytes);

    for (size_t i = 0; i < many.size(); i++)
    {
        many[i] = static_cast<uint8_t>(i * 7);
    }

    CHECK(MCFakeMcoRelay::SendAll(sockets, hostTcp, many, ArrivesMs).has_value());
    CHECK(ReadWithin(sockets, guestTcp, many.size()) == many);
    CHECK_EQ(relay.Passed(MCFrameType::StreamOpen), size_t{2});
    CHECK(relay.Passed(MCFrameType::StreamData) >= ManyBytes / MCMcoRelay::StreamMost + 1);

    // What each bridge carried (counted just after it's passed on, so the counts may trail what arrived a moment).
    const MCMcoBridge::MCCarried& guestCarried = (*guest)->Carried();
    const MCMcoBridge::MCCarried& hostCarried = (*host)->Carried();
    CHECK(Within(ArrivesMs, [&] { return guestCarried.BytesIn == ManyBytes && hostCarried.BytesOut == ManyBytes; }));
    CHECK(Within(ArrivesMs, [&] { return guestCarried.BytesOut == 7 && hostCarried.BytesIn == 7; }));
    CHECK_EQ(guestCarried.DatagramsOut.load(), uint64_t{1});
    CHECK_EQ(guestCarried.DatagramsIn.load(), uint64_t{1});
    CHECK_EQ(hostCarried.DatagramsIn.load(), uint64_t{1});
    CHECK_EQ(hostCarried.DatagramsOut.load(), uint64_t{1});

    // The guest's game ending its sending side: the host's game reads its end, and what the host's still sends reaches
    // the guest's (as MCO's helper carries each side on its own).
    CHECK(sockets.EndSending(guestTcp));
    CHECK(ClosedWithin(sockets, hostTcp));
    CHECK(Within(ArrivesMs, [&] { return relay.Passed(MCFrameType::StreamClose) == 1; }));
    CHECK(MCFakeMcoRelay::SendAll(sockets, hostTcp, BytesOf("last words"), ArrivesMs).has_value());
    CHECK_EQ(TextOf(ReadWithin(sockets, guestTcp, 10)), std::string("last words"));

    // The host's game closing too ends the guest's game's connection.
    sockets.Close(hostTcp);
    CHECK(ClosedWithin(sockets, guestTcp));
    CHECK(Within(ArrivesMs, [&] { return relay.Passed(MCFrameType::StreamClose) == 2; }));

    // The host's bridge stopping ends the guest's: the relay closes its side of the pair.
    (*host)->Stop();
    CHECK(!(*host)->Running());
    CHECK(Within(ArrivesMs, [&] { return !(*guest)->Running(); }));
    CHECK_EQ((*guest)->Ended(), std::string("the relay closed the connection"));

    sockets.Close(guestTcp);
    sockets.Close(guestGame);
    sockets.Close(gameUdp);
    sockets.Close(gameListener);
}

TEST_CASE("mco relay: a refusal, a bad epoch or a frame out of place ends the bridge, saying what happened")
{
    MCFakeMcoRelay::MCRelay relay;
    MCMcoBridge::MCDial unreached;

    {
        const MCFakeMcoRelay::MCRelay gone;
        unreached = gone.Dial();
    }

    const auto noRelay = MCMcoBridge::Probe(unreached);
    REQUIRE(!noRelay.has_value());
    CHECK_EQ(noRelay.error(), std::string("the stand-in relay couldn't be reached"));

    const auto noHost = MCMcoBridge::MCGuest::Start(relay.Dial(), "join-ticket", {});
    REQUIRE(!noHost.has_value());
    CHECK_EQ(noHost.error(), std::string("the relay refused the join connection: no host"));

    relay.Refuse("full");
    const auto refused = MCMcoBridge::MCHost::Start(relay.Dial(), "host-ticket", 1, {});
    REQUIRE(!refused.has_value());
    CHECK_EQ(refused.error(), std::string("the relay refused the host connection: full"));
    relay.Refuse("");
    relay.SetEpoch("not an epoch");
    const auto badEpoch = MCMcoBridge::Probe(relay.Dial());
    REQUIRE(!badEpoch.has_value());
    CHECK_EQ(badEpoch.error(), std::string("the relay's epoch isn't one"));

    // The host's control connection told something other than an attach, or sent a frame no relay sends.
    {
        auto host = MCMcoBridge::MCHost::Start(relay.Dial(), "host-ticket", 1, {});
        REQUIRE(host.has_value());
        relay.SendRaw("host", ControlFrame(R"({"status":"closed"})"));
        CHECK(Within(ArrivesMs, [&] { return !(*host)->Running(); }));
        CHECK_EQ((*host)->Ended(), std::string("the relay said closed"));
    }

    {
        auto host = MCMcoBridge::MCHost::Start(relay.Dial(), "host-ticket", 1, {});
        REQUIRE(host.has_value());
        relay.SendRaw("host", {9, 0, 0});
        CHECK(Within(ArrivesMs, [&] { return !(*host)->Running(); }));
        CHECK_EQ((*host)->Ended(), std::string("the relay sent a frame it never sends"));
    }

    // A guest sent its game's bytes before its connection opened. (The host's bridge's pair for it, its game's port
    // shut, hears nothing.)
    MCSocketTransport sockets;
    const MCSocket::Handle shut = sockets.ListenTcpOn(MCSocket::LoopbackIp, 0);
    const uint16_t shutPort = sockets.LocalPort(shut);
    sockets.Close(shut);
    auto host = MCMcoBridge::MCHost::Start(relay.Dial(), "host-ticket", shutPort, {});
    REQUIRE(host.has_value());
    auto guest = MCMcoBridge::MCGuest::Start(relay.Dial(), "join-ticket", {});
    REQUIRE(guest.has_value());
    relay.SendRaw("join", MCMcoRelay::Frame(MCFrameType::StreamData, BytesOf("early")));
    CHECK(Within(ArrivesMs, [&] { return !(*guest)->Running(); }));
    CHECK_EQ((*guest)->Ended(), std::string("the relay sent the game's bytes before its connection"));

    // The relay closing the host's control connection ends the host's bridge, its guests with it.
    relay.Close("host");
    CHECK(Within(ArrivesMs, [&] { return !(*host)->Running(); }));
    CHECK_EQ((*host)->Ended(), std::string("the relay closed the host's connection"));
    CHECK(Within(ArrivesMs, [&] { return (*host)->Guests() == 0; }));
}

namespace
{
    /// <summary>A host game's session answers on 127.0.0.1, each naming <paramref name="named"/> as its port.</summary>
    class MCAnsweringGame
    {
    public:
        explicit MCAnsweringGame(std::optional<uint16_t> named = std::nullopt)
        {
            _Udp = _Sockets.OpenUdpOn(MCSocket::LoopbackIp, 0);
            _Port = _Sockets.LocalPort(_Udp);
            _Named = named.value_or(_Port);
            _Thread = std::thread(
                [this]
                {
                    while (!_Stopping)
                    {
                        std::array<MCSocketTransport::MCWaiting, 1> waiting{MCSocketTransport::MCWaiting{_Udp}};
                        MCSocketTransport::Wait(waiting, MCFakeMcoRelay::LoopMs);
                        std::array<uint8_t, MCMcoRelay::DatagramMost> datagram{};
                        MCSocket::Address from;

                        for (int size = _Sockets.ReceiveFrom(_Udp, datagram, from); size > 0;
                             size = _Sockets.ReceiveFrom(_Udp, datagram, from))
                        {
                            if (MCMcoRelay::IsQuery(std::span(datagram).first(static_cast<size_t>(size))))
                            {
                                _Sockets.SendTo(_Udp, from, Answer(_Named, "Solaris"));
                            }
                        }
                    }
                });
        }

        ~MCAnsweringGame()
        {
            _Stopping = true;
            _Thread.join();
            _Sockets.Close(_Udp);
        }

        MCAnsweringGame(const MCAnsweringGame&) = delete;
        MCAnsweringGame& operator=(const MCAnsweringGame&) = delete;

        uint16_t Port() const { return _Port; }

    private:
        MCSocketTransport _Sockets;
        MCSocket::Handle _Udp = MCSocket::InvalidHandle;
        uint16_t _Port = 0;
        uint16_t _Named = 0;
        std::atomic<bool> _Stopping = false;
        std::thread _Thread;
    };
}

TEST_CASE("mco relay: the host's bridge waits for its game's answer, one naming the game's own port")
{
    const std::atomic<bool> going = false;
    {
        const MCAnsweringGame game;
        CHECK(MCMcoBridge::WaitForHostGame(game.Port(), ArrivesMs, going));
    }

    {
        const MCAnsweringGame elsewhere(uint16_t{1});
        const Clock::time_point start = Clock::now();
        CHECK(!MCMcoBridge::WaitForHostGame(elsewhere.Port(), HostWaitMs, going));
        CHECK(Clock::now() - start >= std::chrono::milliseconds(HostWaitMs));
        CHECK(Clock::now() - start < std::chrono::milliseconds(HostWaitMs + OverrunMs));
    }

    {
        const std::atomic<bool> cancelled = true;
        const MCAnsweringGame game;
        CHECK(!MCMcoBridge::WaitForHostGame(game.Port(), ArrivesMs, cancelled));
    }
}

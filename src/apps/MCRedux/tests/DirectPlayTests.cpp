#include "stdafx.h"
#include "MCTest.h"
#include "fakes/MCLoopbackTransport.h"
#include "lib/MCOptionalRef.h"
#include "main/MCGameContext.h"
#include "platform/MCDirectPlay.h"
#include "platform/MCSocket.h"

// The port's DirectPlay stand-in, driven the way linkup drives DirectPlay 3, with a host and a joiner in one process
// talking over the loopback. The expected behaviour is DirectPlay's: a session is found by enumerating, joined with
// Open, and every machine hears about the others' players and groups through system messages (never about its own),
// never receives its own sends, and learns of a lost host with DPSYS_SESSIONLOST.

namespace
{
    using namespace MCDirectPlayGuids;

    /// <summary>The game id the tests host under (not MechCommander's, so a running game never answers).</summary>
    constexpr _GUID TestApplication{0x12345678, 0x1234, 0x5678, {1, 2, 3, 4, 5, 6, 7, 8}};

    /// <summary>One received message.</summary>
    struct Received
    {
        uint32_t From = 0;
        uint32_t To = 0;
        std::vector<uint8_t> Bytes;

        uint32_t SystemType() const { return reinterpret_cast<const DPMSG_GENERIC*>(Bytes.data())->dwType; }

        template <class T> const T& As() const { return *reinterpret_cast<const T*>(Bytes.data()); }

        std::string Text() const { return std::string(Bytes.begin(), Bytes.end()); }
    };

    /// <summary>Text as the bytes sent.</summary>
    std::span<const uint8_t> Bytes(std::string_view text)
    {
        return std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }

    /// <summary>Selects the TCP/IP connection to <paramref name="address"/> ("host:port").</summary>
    uint32_t Connect(MCDirectPlay& directPlay, std::string_view address)
    {
        const auto provider = std::bit_cast<std::array<uint8_t, sizeof(_GUID)>>(DPSPGUID_TCPIP);
        std::vector<uint8_t> host(address.begin(), address.end());
        host.push_back(0);
        const std::array<DPCOMPOUNDADDRESSELEMENT, 2> elements{
            DPCOMPOUNDADDRESSELEMENT{DPAID_ServiceProvider, std::vector<uint8_t>(provider.begin(), provider.end())},
            DPCOMPOUNDADDRESSELEMENT{DPAID_INet, host}};
        const std::vector<uint8_t> connection = MCDirectPlay::CreateCompoundAddress(elements);
        CHECK_EQ(connection.size(), size_t{MCDirectPlay::ConnectionDataSize});
        return directPlay.InitializeConnection(connection, 0);
    }

    /// <summary>The flags the game hosts its sessions with (MCFidpSession).</summary>
    constexpr uint32_t GameSessionFlags = DPSESSION_KEEPALIVE | DPSESSION_MIGRATEHOST;

    /// <summary>Hosts a session named <paramref name="name"/> for up to <paramref name="maxPlayers"/>.</summary>
    uint32_t Host(MCDirectPlay& directPlay, std::string_view name, uint32_t maxPlayers,
                  uint32_t flags = GameSessionFlags)
    {
        DPSESSIONDESC2 desc{};
        desc.dwSize = sizeof(desc);
        desc.dwFlags = flags;
        desc.guidApplication = TestApplication;
        desc.dwMaxPlayers = maxPlayers;
        desc.SessionName = name;
        return directPlay.Open(desc, DPOPEN_CREATE | DPOPEN_RETURNSTATUS);
    }

    uint32_t NewPlayer(MCDirectPlay& directPlay, std::string_view name, uint32_t& id)
    {
        return directPlay.CreatePlayer(id, DPNAME{std::string(name), std::nullopt}, {}, 0);
    }

    /// <summary>The next message for <paramref name="directPlay"/>, pumping <paramref name="other"/> meanwhile.</summary>
    std::optional<Received> Next(MCDirectPlay& directPlay, MCOptionalRef<MCDirectPlay> other = std::nullopt,
                                 uint64_t waitMs = 2000)
    {
        const uint64_t until = SDL_GetTicks() + waitMs;

        for (;;)
        {
            std::array<uint8_t, 512> buffer{};
            uint32_t size = 0;
            Received received;

            if (directPlay.Receive(received.From, received.To, DPRECEIVE_ALL, buffer, size) == DP_OK)
            {
                received.Bytes.assign(buffer.begin(), buffer.begin() + size);
                return received;
            }

            if (SDL_GetTicks() > until)
            {
                return std::nullopt;
            }

            if (other)
            {
                other->get().Pump();
            }

            SDL_Delay(1);
        }
    }

    /// <summary>Whether nothing arrives for <paramref name="directPlay"/> within a short while.</summary>
    bool Quiet(MCDirectPlay& directPlay, MCOptionalRef<MCDirectPlay> other = std::nullopt)
    {
        return !Next(directPlay, other, 150).has_value();
    }

    /// <summary>The sessions an enumeration reports.</summary>
    std::vector<DPSESSIONDESC2> Enumerate(MCDirectPlay& directPlay, MCDirectPlay& host, uint32_t flags)
    {
        struct Found
        {
            std::vector<DPSESSIONDESC2> Sessions;
            bool TimedOut = false;
        } found;

        DPSESSIONDESC2 desc{};
        desc.dwSize = sizeof(desc);
        desc.guidApplication = TestApplication;

        // DirectPlay's enumeration is asynchronous: the host answers when it next pumps, and a later call lists it.
        for (int attempt = 0; attempt < 20 && found.Sessions.empty(); attempt++)
        {
            found = {};
            directPlay.EnumSessions(
                desc, 0,
                [&](const DPSESSIONDESC2& session, uint32_t sessionFlags)
                {
                    if ((sessionFlags & DPESC_TIMEDOUT) != 0)
                    {
                        found.TimedOut = true;
                        return false;
                    }

                    found.Sessions.push_back(session);
                    return true;
                },
                flags);
            host.Pump();
        }

        return found.Sessions;
    }

    /// <summary>Runs <paramref name="call"/> (which blocks on the host) while another thread pumps the host.</summary>
    template <class Call> uint32_t WithHostRunning(MCDirectPlay& host, Call&& call)
    {
        std::atomic<bool> stop = false;
        std::thread pumper(
            [&]
            {
                while (!stop)
                {
                    host.Pump();
                    SDL_Delay(1);
                }
            });
        const uint32_t result = call();
        stop = true;
        pumper.join();
        return result;
    }

    /// <summary>The ids EnumPlayers or EnumGroups lists, with their short names.</summary>
    std::map<uint32_t, std::string> Listed(MCDirectPlay& directPlay, bool groups)
    {
        std::map<uint32_t, std::string> listed;
        auto collect = [&](uint32_t id, uint32_t, const DPNAME& name, uint32_t)
        {
            listed[id] = name.ShortName.value_or("");
            return true;
        };

        if (groups)
        {
            directPlay.EnumGroups(collect, 0);
        }
        else
        {
            directPlay.EnumPlayers(collect, 0);
        }

        return listed;
    }
}

TEST_CASE("directplay: a joiner finds, joins and plays with a host")
{
    MCDirectPlay host;
    MCDirectPlay joiner;
    REQUIRE_EQ(Connect(host, "127.0.0.1:28871"), DP_OK);
    REQUIRE_EQ(Host(host, "Solaris VII", 6), DP_OK);
    uint32_t hostPlayer = 0;
    REQUIRE_EQ(NewPlayer(host, "Commander", hostPlayer), DP_OK);

    // The session is found by name and shows its player count.
    REQUIRE_EQ(Connect(joiner, "127.0.0.1:28871"), DP_OK);
    const std::vector<DPSESSIONDESC2> sessions = Enumerate(joiner, host, DPENUMSESSIONS_AVAILABLE);
    REQUIRE_EQ(sessions.size(), size_t{1});
    CHECK(sessions[0].SessionName == std::optional<std::string>("Solaris VII"));
    CHECK_EQ(sessions[0].dwCurrentPlayers, 1u);
    CHECK_EQ(sessions[0].dwMaxPlayers, 6u);
    CHECK(MCSameGuid(sessions[0].guidApplication, TestApplication));

    DPSESSIONDESC2 join = sessions[0];
    REQUIRE_EQ(WithHostRunning(host, [&] { return joiner.Open(join, DPOPEN_JOIN | DPOPEN_RETURNSTATUS); }), DP_OK);
    uint32_t guestPlayer = 0;
    REQUIRE_EQ(WithHostRunning(host, [&] { return NewPlayer(joiner, "Guest", guestPlayer); }), DP_OK);
    CHECK(guestPlayer != hostPlayer);
    CHECK(guestPlayer > 1u);

    // The host hears of the new player; the joiner hears nothing of its own.
    std::optional<Received> created = Next(host);
    REQUIRE(created.has_value());
    CHECK_EQ(created->From, uint32_t{DPID_SYSMSG});
    CHECK_EQ(created->SystemType(), uint32_t{DPSYS_CREATEPLAYERORGROUP});
    CHECK_EQ(created->As<DPMSG_CREATEPLAYERORGROUP>().dwPlayerType, uint32_t{DPPLAYERTYPE_PLAYER});
    CHECK_EQ(created->As<DPMSG_CREATEPLAYERORGROUP>().dpId, guestPlayer);
    CHECK(MCDirectPlayMessageName(created->Bytes, created->As<DPMSG_CREATEPLAYERORGROUP>().dpnName).ShortName ==
          std::optional<std::string>("Guest"));
    CHECK(Quiet(joiner, host));

    // Both list both players.
    const std::map<uint32_t, std::string> expected{{hostPlayer, "Commander"}, {guestPlayer, "Guest"}};
    CHECK(Listed(host, false) == expected);
    CHECK(Listed(joiner, false) == expected);

    // A message to a player reaches it, from its sender.
    REQUIRE_EQ(joiner.Send(guestPlayer, hostPlayer, 0, Bytes("hello")), DP_OK);
    std::optional<Received> hello = Next(host);
    REQUIRE(hello.has_value());
    CHECK_EQ(hello->From, guestPlayer);
    CHECK_EQ(hello->Text(), std::string("hello"));

    // A message to everyone reaches everyone but the sender.
    REQUIRE_EQ(host.Send(hostPlayer, DPID_ALLPLAYERS, 0, Bytes("all")), DP_OK);
    std::optional<Received> all = Next(joiner, host);
    REQUIRE(all.has_value());
    CHECK_EQ(all->From, hostPlayer);
    CHECK_EQ(all->Text(), std::string("all"));
    CHECK(Quiet(host));

    // Messages arrive in the order they were sent.
    for (int i = 0; i < 50; i++)
    {
        const uint8_t value = static_cast<uint8_t>(i);
        joiner.Send(guestPlayer, hostPlayer, 0, std::span(&value, 1));
    }

    for (int i = 0; i < 50; i++)
    {
        MCTest::Scope scope(std::format("message {}", i));
        std::optional<Received> numbered = Next(host);
        REQUIRE(numbered.has_value());
        CHECK_EQ(numbered->Bytes.size(), size_t{1});
        CHECK_EQ(int{numbered->Bytes[0]}, i);
    }

    // A group the host makes, and a player it adds, are announced to the joiner.
    uint32_t group = 0;
    const DPNAME groupName{std::string("InnerSphereGroup"), std::nullopt};
    REQUIRE_EQ(host.CreateGroup(group, groupName, {}, 0), DP_OK);
    REQUIRE_EQ(host.AddPlayerToGroup(group, guestPlayer), DP_OK);
    std::optional<Received> groupCreated = Next(joiner, host);
    REQUIRE(groupCreated.has_value());
    CHECK_EQ(groupCreated->SystemType(), uint32_t{DPSYS_CREATEPLAYERORGROUP});
    CHECK_EQ(groupCreated->As<DPMSG_CREATEPLAYERORGROUP>().dwPlayerType, uint32_t{DPPLAYERTYPE_GROUP});
    CHECK_EQ(groupCreated->As<DPMSG_CREATEPLAYERORGROUP>().dpId, group);
    std::optional<Received> added = Next(joiner, host);
    REQUIRE(added.has_value());
    CHECK_EQ(added->SystemType(), uint32_t{DPSYS_ADDPLAYERTOGROUP});
    CHECK_EQ(added->As<DPMSG_ADDPLAYERTOGROUP>().dpIdGroup, group);
    CHECK_EQ(added->As<DPMSG_ADDPLAYERTOGROUP>().dpIdPlayer, guestPlayer);
    CHECK(Listed(joiner, true) == (std::map<uint32_t, std::string>{{group, "InnerSphereGroup"}}));

    // A message to the group reaches its members only.
    REQUIRE_EQ(host.Send(hostPlayer, group, 0, Bytes("team")), DP_OK);
    std::optional<Received> team = Next(joiner, host);
    REQUIRE(team.has_value());
    CHECK_EQ(team->Text(), std::string("team"));
    REQUIRE_EQ(joiner.Send(guestPlayer, group, 0, Bytes("self")), DP_OK);
    CHECK(Quiet(host));
    CHECK(Quiet(joiner, host));

    // A joiner that leaves is taken out of its groups, then destroyed, on the host.
    joiner.Close();
    std::optional<Received> removed = Next(host);
    REQUIRE(removed.has_value());
    CHECK_EQ(removed->SystemType(), uint32_t{DPSYS_DELETEPLAYERFROMGROUP});
    CHECK_EQ(removed->As<DPMSG_ADDPLAYERTOGROUP>().dpIdPlayer, guestPlayer);
    std::optional<Received> destroyed = Next(host);
    REQUIRE(destroyed.has_value());
    CHECK_EQ(destroyed->SystemType(), uint32_t{DPSYS_DESTROYPLAYERORGROUP});
    CHECK_EQ(destroyed->As<DPMSG_DESTROYPLAYERORGROUP>().dpId, guestPlayer);
    CHECK(Listed(host, false) == (std::map<uint32_t, std::string>{{hostPlayer, "Commander"}}));
}

TEST_CASE("directplay: joiners lose the session when the host leaves")
{
    MCDirectPlay host;
    MCDirectPlay joiner;
    REQUIRE_EQ(Connect(host, "127.0.0.1:28872"), DP_OK);
    REQUIRE_EQ(Host(host, "Short game", 6), DP_OK);
    uint32_t hostPlayer = 0;
    REQUIRE_EQ(NewPlayer(host, "Commander", hostPlayer), DP_OK);
    REQUIRE_EQ(Connect(joiner, "127.0.0.1:28872"), DP_OK);
    std::vector<DPSESSIONDESC2> sessions = Enumerate(joiner, host, DPENUMSESSIONS_AVAILABLE);
    REQUIRE_EQ(sessions.size(), size_t{1});
    REQUIRE_EQ(WithHostRunning(host, [&] { return joiner.Open(sessions[0], DPOPEN_JOIN); }), DP_OK);
    uint32_t guestPlayer = 0;
    REQUIRE_EQ(WithHostRunning(host, [&] { return NewPlayer(joiner, "Guest", guestPlayer); }), DP_OK);

    host.Close();
    std::optional<Received> lost = Next(joiner);
    REQUIRE(lost.has_value());
    CHECK_EQ(lost->From, uint32_t{DPID_SYSMSG});
    CHECK_EQ(lost->SystemType(), uint32_t{DPSYS_SESSIONLOST});
    CHECK_EQ(joiner.Send(guestPlayer, DPID_ALLPLAYERS, 0, Bytes("x")), uint32_t{DPERR_SESSIONLOST});
}

TEST_CASE("directplay: a locked or full session takes no new players")
{
    MCDirectPlay host;
    MCDirectPlay joiner;
    REQUIRE_EQ(Connect(host, "127.0.0.1:28873"), DP_OK);
    REQUIRE_EQ(Host(host, "Two only", 2), DP_OK);
    uint32_t hostPlayer = 0;
    REQUIRE_EQ(NewPlayer(host, "Commander", hostPlayer), DP_OK);
    REQUIRE_EQ(Connect(joiner, "127.0.0.1:28873"), DP_OK);
    std::vector<DPSESSIONDESC2> sessions = Enumerate(joiner, host, DPENUMSESSIONS_AVAILABLE);
    REQUIRE_EQ(sessions.size(), size_t{1});

    // The host locks the session (SessionManager::LockSession): it is no longer listed as available, and joining
    // it is refused.
    DPSESSIONDESC2 locked{};
    locked.dwSize = sizeof(locked);
    locked.dwFlags = GameSessionFlags | DPSESSION_NEWPLAYERSDISABLED;
    locked.dwMaxPlayers = 2;
    locked.SessionName = "Two only";
    REQUIRE_EQ(host.SetSessionDesc(locked, 0), DP_OK);
    host.Pump();
    SDL_Delay(4100);

    // Enumeration is asynchronous: the first listing may still use an answer the host sent before locking.
    CHECK_EQ(Enumerate(joiner, host, 0).size(), size_t{1});
    CHECK(Enumerate(joiner, host, DPENUMSESSIONS_AVAILABLE).empty());
    CHECK_EQ(WithHostRunning(host, [&] { return joiner.Open(sessions[0], DPOPEN_JOIN); }),
             uint32_t{DPERR_NONEWPLAYERS});
}

/// <summary>
/// A session that asks DirectPlay to drop players who vanish (DPSESSION_KEEPALIVE, as the game's do) has both ends of
/// each link to it kept alive (MCSocket::KeepAlive), so either notices the other machine going; one that doesn't
/// ask has neither.
/// </summary>
TEST_CASE("directplay: both ends of a link to a session that asks for keep-alive are kept alive")
{
    for (const uint32_t flags : {GameSessionFlags, uint32_t{DPSESSION_MIGRATEHOST}})
    {
        MCTestContextScope scope;
        MCLoopbackTransport& net = scope.Context().SetNet(std::make_unique<MCLoopbackTransport>());
        MCDirectPlay host;
        MCDirectPlay joiner;
        REQUIRE_EQ(Connect(host, "127.0.0.1:28874"), DP_OK);
        REQUIRE_EQ(Host(host, "Watched", 6, flags), DP_OK);
        uint32_t hostPlayer = 0;
        REQUIRE_EQ(NewPlayer(host, "Commander", hostPlayer), DP_OK);
        REQUIRE_EQ(Connect(joiner, "127.0.0.1:28874"), DP_OK);
        const std::vector<DPSESSIONDESC2> sessions = Enumerate(joiner, host, DPENUMSESSIONS_AVAILABLE);
        REQUIRE_EQ(sessions.size(), size_t{1});
        CHECK_EQ(net.KeptAliveSockets(), size_t{0});
        REQUIRE_EQ(WithHostRunning(host, [&] { return joiner.Open(sessions[0], DPOPEN_JOIN); }), DP_OK);
        CHECK_EQ(net.KeptAliveSockets(), (flags & DPSESSION_KEEPALIVE) != 0 ? size_t{2} : size_t{0});
    }
}

/// <summary>
/// With a host typed in, only that host's answer lists a session (issue #25): another session's answer, passed on to
/// the searcher from the typed address, names its own port and is ignored.
/// </summary>
TEST_CASE("directplay: a search for a typed host ignores answers about other sessions")
{
    MCTestContextScope scope;
    scope.Context().SetNet(std::make_unique<MCLoopbackTransport>());
    constexpr uint16_t typedPort = 28874;

    // The test stands where the typed host would be, and catches the searcher's question.
    const MCSocket::Handle typed = MCSocket::OpenUdp(typedPort);
    REQUIRE(typed != MCSocket::InvalidHandle);
    MCDirectPlay joiner;
    REQUIRE_EQ(Connect(joiner, std::format("127.0.0.1:{}", typedPort)), DP_OK);
    DPSESSIONDESC2 desc{};
    desc.dwSize = sizeof(desc);
    desc.guidApplication = TestApplication;
    std::vector<std::string> listed;
    const auto search = [&]
    {
        listed.clear();
        joiner.EnumSessions(
            desc, 0,
            [&](const DPSESSIONDESC2& session, uint32_t flags)
            {
                if ((flags & DPESC_TIMEDOUT) == 0)
                {
                    listed.push_back(session.SessionName.value_or(""));
                }

                return (flags & DPESC_TIMEDOUT) == 0;
            },
            0);
    };

    search();
    std::array<uint8_t, 1500> datagram{};
    MCSocket::Address searcher;
    const int questionSize = MCSocket::ReceiveFrom(typed, datagram, searcher);
    REQUIRE(questionSize > 0);
    const std::vector<uint8_t> question(datagram.begin(), datagram.begin() + questionSize);

    // A decoy session elsewhere answers the same question; its answer goes to the searcher from the typed address.
    MCDirectPlay decoy;
    REQUIRE_EQ(Connect(decoy, "127.0.0.1:28875"), DP_OK);
    REQUIRE_EQ(Host(decoy, "Decoy", 6), DP_OK);
    REQUIRE(MCSocket::SendTo(typed, {MCSocket::LoopbackIp, 28875}, question));
    decoy.Pump();
    MCSocket::Address decoyAddress;
    const int answerSize = MCSocket::ReceiveFrom(typed, datagram, decoyAddress);
    REQUIRE(answerSize > 0);
    REQUIRE(MCSocket::SendTo(typed, searcher, std::span(datagram).first(static_cast<size_t>(answerSize))));

    search();
    CHECK(listed.empty());
    MCSocket::Close(typed);
}

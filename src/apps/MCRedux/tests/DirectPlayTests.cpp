#include "stdafx.h"
#include "MCTest.h"
#include "fakes/MCFakeSignaling.h"
#include "fakes/MCLoopbackTransport.h"
#include "lib/MCOptionalRef.h"
#include "linkup/MCSessionManager.h"
#include "main/MCGameContext.h"
#include "platform/MCDirectPlay.h"
#include "platform/MCIceTransport.h"
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

    /// <summary>The searches a LAN game gets to find a session.</summary>
    constexpr int SearchAttempts = 20;

    /// <summary>The sessions an enumeration reports.</summary>
    std::vector<DPSESSIONDESC2> Enumerate(MCDirectPlay& directPlay, MCDirectPlay& host, uint32_t flags,
                                          int attempts = SearchAttempts)
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
        for (int attempt = 0; attempt < attempts && found.Sessions.empty(); attempt++)
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

/// <summary>
/// An internet game: the stand-in over ICE links (platform/MCIceTransport), the service's signals passed in the process
/// (MCFakeSignalBoard), the host and the joiner on one transport as the other tests share the loopback. The search's
/// first query opens the link, so a search finds the session once it has opened.
/// </summary>
TEST_CASE("directplay: an internet game, over ICE links, is found, joined and played as a LAN one")
{
    // A search a few hundred milliseconds long each time, for up to 15 s, as the link opens.
    constexpr int linkSearchAttempts = 250;
    // A link closed at one end is seen closed at the other within a second or two.
    constexpr uint64_t closeSeenMs = 10000;
    MCTestContextScope scope;
    MCFakeSignalBoard board;
    board.Host("K7QF", "this game");
    scope.Context().SetNet(std::make_unique<MCIceTransport>(board.Endpoint("this game")));
    MCDirectPlay host;
    MCDirectPlay joiner;
    REQUIRE_EQ(Connect(host, "K7QF:28881"), DP_OK);
    REQUIRE_EQ(Host(host, "Over the internet", 6), DP_OK);
    uint32_t hostPlayer = 0;
    REQUIRE_EQ(NewPlayer(host, "Commander", hostPlayer), DP_OK);

    REQUIRE_EQ(Connect(joiner, "K7QF:28881"), DP_OK);
    const std::vector<DPSESSIONDESC2> sessions = Enumerate(joiner, host, DPENUMSESSIONS_AVAILABLE, linkSearchAttempts);
    REQUIRE_EQ(sessions.size(), size_t{1});
    CHECK(sessions[0].SessionName == std::optional<std::string>("Over the internet"));
    CHECK_EQ(sessions[0].dwCurrentPlayers, 1u);
    REQUIRE_EQ(WithHostRunning(host, [&] { return joiner.Open(sessions[0], DPOPEN_JOIN | DPOPEN_RETURNSTATUS); }),
               DP_OK);
    uint32_t guestPlayer = 0;
    REQUIRE_EQ(WithHostRunning(host, [&] { return NewPlayer(joiner, "Guest", guestPlayer); }), DP_OK);

    std::optional<Received> created = Next(host);
    REQUIRE(created.has_value());
    CHECK_EQ(created->SystemType(), uint32_t{DPSYS_CREATEPLAYERORGROUP});
    CHECK_EQ(created->As<DPMSG_CREATEPLAYERORGROUP>().dpId, guestPlayer);
    const std::map<uint32_t, std::string> expected{{hostPlayer, "Commander"}, {guestPlayer, "Guest"}};
    CHECK(Listed(host, false) == expected);
    CHECK(Listed(joiner, false) == expected);

    REQUIRE_EQ(joiner.Send(guestPlayer, hostPlayer, 0, Bytes("hello")), DP_OK);
    std::optional<Received> hello = Next(host);
    REQUIRE(hello.has_value());
    CHECK_EQ(hello->From, guestPlayer);
    CHECK_EQ(hello->Text(), std::string("hello"));
    REQUIRE_EQ(host.Send(hostPlayer, DPID_ALLPLAYERS, 0, Bytes("all")), DP_OK);
    std::optional<Received> all = Next(joiner, host);
    REQUIRE(all.has_value());
    CHECK_EQ(all->From, hostPlayer);
    CHECK_EQ(all->Text(), std::string("all"));

    host.Close();
    std::optional<Received> lost = Next(joiner, std::nullopt, closeSeenMs);
    REQUIRE(lost.has_value());
    CHECK_EQ(lost->SystemType(), uint32_t{DPSYS_SESSIONLOST});
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

// Port-only (Redux HD): the session rolling over to another machine when its host goes (SPEC-host-rollover.md), with
// three machines on the loopback.

namespace
{
    using Machines = std::initializer_list<std::reference_wrapper<MCDirectPlay>>;

    /// <summary>Pumps each of <paramref name="machines"/> once.</summary>
    void PumpEach(Machines machines)
    {
        for (MCDirectPlay& machine : machines)
        {
            machine.Pump();
        }
    }

    /// <summary>The next message for <paramref name="directPlay"/>, pumping <paramref name="others"/> meanwhile.</summary>
    std::optional<Received> NextAmong(MCDirectPlay& directPlay, Machines others, uint64_t waitMs = 2000)
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

            PumpEach(others);
            SDL_Delay(1);
        }
    }

    /// <summary>Every message <paramref name="directPlay"/> gets until it is quiet, <paramref name="others"/> pumped.</summary>
    std::vector<Received> Drain(MCDirectPlay& directPlay, Machines others)
    {
        constexpr uint64_t quietMs = 150;
        std::vector<Received> all;

        while (std::optional<Received> next = NextAmong(directPlay, others, quietMs))
        {
            all.push_back(std::move(*next));
        }

        return all;
    }

    /// <summary>The system messages' types in <paramref name="messages"/>, in order.</summary>
    std::vector<uint32_t> SystemTypes(const std::vector<Received>& messages)
    {
        std::vector<uint32_t> types;

        for (const Received& message : messages)
        {
            if (message.From == DPID_SYSMSG)
            {
                types.push_back(message.SystemType());
            }
        }

        return types;
    }

    /// <summary>Has <paramref name="joiner"/> find and join the session at <paramref name="address"/> as a new player.</summary>
    uint32_t JoinAs(MCDirectPlay& joiner, MCDirectPlay& host, std::string_view address, std::string_view name)
    {
        uint32_t id = 0;
        REQUIRE_EQ(Connect(joiner, address), DP_OK);
        const std::vector<DPSESSIONDESC2> sessions = Enumerate(joiner, host, DPENUMSESSIONS_AVAILABLE);
        REQUIRE_EQ(sessions.size(), size_t{1});
        REQUIRE_EQ(WithHostRunning(host, [&] { return joiner.Open(sessions[0], DPOPEN_JOIN | DPOPEN_RETURNSTATUS); }),
                   DP_OK);
        REQUIRE_EQ(WithHostRunning(host, [&] { return NewPlayer(joiner, name, id); }), DP_OK);
        return id;
    }

    /// <summary>
    /// A session of three machines at <paramref name="address"/> (a typed host, or a LAN search's ":port"): the host's
    /// Bob, then Ann's machine, then Cat's, the three players in the host's group "Team", each machine's messages so
    /// far taken.
    /// </summary>
    struct ThreeMachines
    {
        explicit ThreeMachines(uint16_t port) : ThreeMachines(std::format("127.0.0.1:{}", port)) {}

        explicit ThreeMachines(std::string address) : Address(std::move(address))
        {
            REQUIRE_EQ(Connect(*Host, Address), DP_OK);
            REQUIRE_EQ(::Host(*Host, "Rolling", 6), DP_OK);
            REQUIRE_EQ(NewPlayer(*Host, "Bob", Bob), DP_OK);
            REQUIRE_EQ(Host->CreateGroup(Team, DPNAME{std::string("Team"), std::nullopt}, {}, 0), DP_OK);
            Ann = JoinAs(AnnMachine, *Host, Address, "Ann");
            Cat = JoinAs(CatMachine, *Host, Address, "Cat");

            for (const uint32_t player : {Bob, Ann, Cat})
            {
                REQUIRE_EQ(Host->AddPlayerToGroup(Team, player), DP_OK);
            }

            Drain(*Host, {AnnMachine, CatMachine});
            Drain(AnnMachine, {*Host, CatMachine});
            Drain(CatMachine, {*Host, AnnMachine});
        }

        std::string Address;
        std::optional<MCDirectPlay> Host{std::in_place};
        MCDirectPlay AnnMachine;
        MCDirectPlay CatMachine;
        uint32_t Bob = 0;
        uint32_t Ann = 0;
        uint32_t Cat = 0;
        uint32_t Team = 0;
    };

    /// <summary>Whether <paramref name="messages"/> hold Bob's leave, flagged as the hub's player, after his group's.</summary>
    bool HubLeft(const std::vector<Received>& messages, uint32_t bob, uint32_t team)
    {
        const auto left = std::ranges::find_if(messages,
                                               [&](const Received& message)
                                               {
                                                   return message.From == DPID_SYSMSG &&
                                                          message.SystemType() == DPSYS_DESTROYPLAYERORGROUP &&
                                                          message.As<DPMSG_DESTROYPLAYERORGROUP>().dpId == bob;
                                               });
        const auto outOfTeam = std::ranges::find_if(messages,
                                                    [&](const Received& message)
                                                    {
                                                        return message.From == DPID_SYSMSG &&
                                                               message.SystemType() == DPSYS_DELETEPLAYERFROMGROUP &&
                                                               message.As<DPMSG_ADDPLAYERTOGROUP>().dpIdPlayer == bob &&
                                                               message.As<DPMSG_ADDPLAYERTOGROUP>().dpIdGroup == team;
                                                    });
        return left != messages.end() && outOfTeam < left &&
               (left->As<DPMSG_DESTROYPLAYERORGROUP>().dwFlags & DPPLAYER_SERVERPLAYER) != 0;
    }

    /// <summary>A frame of the stand-in's own protocol, as a test speaking it raw writes one.</summary>
    std::vector<uint8_t> RawFrame(uint8_t type, std::span<const uint8_t> payload)
    {
        std::vector<uint8_t> frame;
        const auto size = static_cast<uint32_t>(payload.size() + 1);

        for (int shift = 0; shift < 32; shift += 8)
        {
            frame.push_back(static_cast<uint8_t>(size >> shift));
        }

        frame.push_back(type);
        frame.insert(frame.end(), payload.begin(), payload.end());
        return frame;
    }

    /// <summary>Appends a little-endian word, a GUID as the stand-in writes one, or a length-prefixed blob.</summary>
    void PutU32(std::vector<uint8_t>& bytes, uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            bytes.push_back(static_cast<uint8_t>(value >> shift));
        }
    }

    void PutGuid(std::vector<uint8_t>& bytes, const _GUID& guid)
    {
        const auto raw = std::bit_cast<std::array<uint8_t, sizeof(_GUID)>>(guid);
        bytes.insert(bytes.end(), raw.begin(), raw.end());
    }

    void PutBlob(std::vector<uint8_t>& bytes, std::span<const uint8_t> blob)
    {
        PutU32(bytes, static_cast<uint32_t>(blob.size()));
        bytes.insert(bytes.end(), blob.begin(), blob.end());
    }

    /// <summary>A rollover's wait in the tests that set one: long enough for the test's steps.</summary>
    constexpr auto RolloverTestWait = std::chrono::milliseconds(10000);

    /// <summary>The frame types the raw tests send and read (MCDirectPlay.cpp's FrameType).</summary>
    constexpr uint8_t JoinRequestFrame = 1;
    constexpr uint8_t JoinReplyFrame = 2;
    constexpr uint8_t CreatePlayerFrame = 3;
    constexpr uint8_t CreateReplyFrame = 5;
    constexpr uint8_t ResumeFrame = 15;
    constexpr uint8_t ResumeReplyFrame = 16;
    /// <summary>The first word of the stand-in's join: "MCG1".</summary>
    constexpr uint32_t RawMagic = 0x3147434d;

    /// <summary>Reads the next whole frame from <paramref name="socket"/> (its type and payload), pumping meanwhile.</summary>
    std::optional<std::pair<uint8_t, std::vector<uint8_t>>> RawRead(MCSocket::Handle socket, Machines pumped)
    {
        constexpr uint64_t waitMs = 2000;
        const uint64_t until = SDL_GetTicks() + waitMs;
        std::vector<uint8_t> input;

        while (SDL_GetTicks() < until)
        {
            std::array<uint8_t, 4096> chunk{};
            const int received = MCSocket::Receive(socket, chunk);

            if (received > 0)
            {
                input.insert(input.end(), chunk.begin(), chunk.begin() + received);
            }

            if (input.size() >= 5)
            {
                const uint32_t size = input[0] | (input[1] << 8) | (input[2] << 16) | (uint32_t{input[3]} << 24);

                if (input.size() >= 4 + size)
                {
                    return std::pair(input[4], std::vector<uint8_t>(input.begin() + 5, input.begin() + 4 + size));
                }
            }

            if (received < 0)
            {
                return std::nullopt;
            }

            PumpEach(pumped);
            SDL_Delay(1);
        }

        return std::nullopt;
    }

    /// <summary>Whether <paramref name="socket"/>'s other end closes it within a while (pumping meanwhile).</summary>
    bool RawClosed(MCSocket::Handle socket, Machines pumped)
    {
        constexpr uint64_t waitMs = 2000;
        const uint64_t until = SDL_GetTicks() + waitMs;

        while (SDL_GetTicks() < until)
        {
            std::array<uint8_t, 4096> chunk{};

            if (MCSocket::Receive(socket, chunk) < 0)
            {
                return true;
            }

            PumpEach(pumped);
            SDL_Delay(1);
        }

        return false;
    }

    /// <summary>The result word a reply frame starts with.</summary>
    uint32_t ReplyResult(std::span<const uint8_t> payload, size_t at = 0)
    {
        return payload.size() < at + 4
                   ? DPERR_GENERIC
                   : payload[at] | (payload[at + 1] << 8) | (payload[at + 2] << 16) | (uint32_t{payload[at + 3]} << 24);
    }
}

/// <summary>
/// The host's link cut (its machine gone without a word): the session goes on under Ann's machine, the earliest
/// joined, with the same instance, players, ids and groups; Cat's machine comes back to it. Ann's game hears Bob leave
/// (out of his group first, flagged as the hub's player) and that it hosts; Cat's hears Bob leave. Messages go between
/// the two, the group's still reach its members, and a new machine finds the session on its port and joins it, its
/// player numbered on from the highest. (On the process's own network: a LAN search reaches every machine on the port.)
/// </summary>
TEST_CASE("directplay: the host's link cut, the session rolls over to the earliest joined machine, the same ids kept")
{
    MCTestContextScope scope;
    scope.Context().SetNet(std::make_unique<MCLoopbackTransport>());
    ThreeMachines session(":28961");
    const std::vector<DPSESSIONDESC2> before = Enumerate(session.CatMachine, *session.Host, 0);
    REQUIRE_EQ(before.size(), size_t{1});
    session.Host.reset();

    const std::vector<Received> ann = Drain(session.AnnMachine, {session.CatMachine});
    const std::vector<Received> cat = Drain(session.CatMachine, {session.AnnMachine});
    CHECK(HubLeft(ann, session.Bob, session.Team));
    CHECK(HubLeft(cat, session.Bob, session.Team));
    CHECK(std::ranges::contains(SystemTypes(ann), uint32_t{DPSYS_HOST}));
    CHECK(!std::ranges::contains(SystemTypes(cat), uint32_t{DPSYS_HOST}));
    CHECK(!std::ranges::contains(SystemTypes(cat), uint32_t{DPSYS_SESSIONLOST}));
    CHECK(!session.CatMachine.RollingOver());

    // Ann's player speaks for the session now, on both machines; the players and the group are as they were.
    CHECK(session.AnnMachine.IsSessionHostPlayer(session.Ann));
    CHECK(session.CatMachine.IsSessionHostPlayer(session.Ann));
    CHECK(!session.CatMachine.IsSessionHostPlayer(session.Cat));
    const std::map<uint32_t, std::string> players{{session.Ann, "Ann"}, {session.Cat, "Cat"}};
    CHECK(Listed(session.AnnMachine, false) == players);
    CHECK(Listed(session.CatMachine, false) == players);
    const std::map<uint32_t, std::string> groups{{session.Team, "Team"}};
    CHECK(Listed(session.AnnMachine, true) == groups);
    CHECK(Listed(session.CatMachine, true) == groups);

    REQUIRE_EQ(session.CatMachine.Send(session.Cat, session.Ann, 0, Bytes("to the new host")), DP_OK);
    std::optional<Received> toAnn = NextAmong(session.AnnMachine, {session.CatMachine});
    REQUIRE(toAnn.has_value());
    CHECK_EQ(toAnn->From, session.Cat);
    CHECK_EQ(toAnn->Text(), std::string("to the new host"));
    REQUIRE_EQ(session.AnnMachine.Send(session.Ann, session.Team, 0, Bytes("team")), DP_OK);
    std::optional<Received> toCat = NextAmong(session.CatMachine, {session.AnnMachine});
    REQUIRE(toCat.has_value());
    CHECK_EQ(toCat->From, session.Ann);
    CHECK_EQ(toCat->Text(), std::string("team"));

    // A new machine finds the same session on its port, now Ann's, and joins it.
    MCDirectPlay danMachine;
    REQUIRE_EQ(Connect(danMachine, session.Address), DP_OK);
    const std::vector<DPSESSIONDESC2> after = Enumerate(danMachine, session.AnnMachine, 0);
    REQUIRE_EQ(after.size(), size_t{1});
    CHECK(MCSameGuid(after[0].guidInstance, before[0].guidInstance));
    CHECK_EQ(after[0].dwCurrentPlayers, 2u);
    uint32_t dan = 0;
    REQUIRE_EQ(WithHostRunning(session.AnnMachine,
                               [&] { return danMachine.Open(after[0], DPOPEN_JOIN | DPOPEN_RETURNSTATUS); }),
               DP_OK);
    REQUIRE_EQ(WithHostRunning(session.AnnMachine, [&] { return NewPlayer(danMachine, "Dan", dan); }), DP_OK);
    CHECK(dan > std::max({session.Bob, session.Ann, session.Cat, session.Team}));
    std::optional<Received> danJoined = NextAmong(session.CatMachine, {session.AnnMachine, danMachine});
    REQUIRE(danJoined.has_value());
    CHECK_EQ(danJoined->SystemType(), uint32_t{DPSYS_CREATEPLAYERORGROUP});
    CHECK_EQ(danJoined->As<DPMSG_CREATEPLAYERORGROUP>().dpId, dan);
}

/// <summary>
/// A host that leaves the session (Leave, the game's EXIT and quit) hands it on as one whose link is cut; one that
/// closes it (Close: a host restarting into an update, whose guests wait for it) ends it for everyone.
/// </summary>
TEST_CASE("directplay: a host that leaves hands the session on; one that closes it ends it for everyone")
{
    {
        ThreeMachines session(28962);
        REQUIRE_EQ(session.Host->Leave(), DP_OK);
        const std::vector<Received> ann = Drain(session.AnnMachine, {session.CatMachine});
        const std::vector<Received> cat = Drain(session.CatMachine, {session.AnnMachine});
        CHECK(HubLeft(ann, session.Bob, session.Team));
        CHECK(HubLeft(cat, session.Bob, session.Team));
        CHECK(Listed(session.CatMachine, false) ==
              (std::map<uint32_t, std::string>{{session.Ann, "Ann"}, {session.Cat, "Cat"}}));
    }

    ThreeMachines session(28963);
    REQUIRE_EQ(session.Host->Close(), DP_OK);
    CHECK(SystemTypes(Drain(session.AnnMachine, {session.CatMachine})) ==
          std::vector<uint32_t>{uint32_t{DPSYS_SESSIONLOST}});
    CHECK(SystemTypes(Drain(session.CatMachine, {session.AnnMachine})) ==
          std::vector<uint32_t>{uint32_t{DPSYS_SESSIONLOST}});
}

/// <summary>
/// Only a machine itself comes back as itself: a connection to the new hub asking to be Cat's machine with another key
/// is refused and closed, and Cat's own comes back after it. (Machines are numbered as they join: the host's 0, Ann's
/// 1, Cat's 2.)
/// </summary>
TEST_CASE("directplay: a machine coming back to the new hub with the wrong key is refused")
{
    constexpr uint16_t port = 28964;
    constexpr uint32_t catMachine = 2;
    ThreeMachines session(port);
    const std::vector<DPSESSIONDESC2> sessions = Enumerate(session.CatMachine, *session.Host, 0);
    REQUIRE_EQ(sessions.size(), size_t{1});
    session.Host.reset();

    // Ann's machine takes over; Cat's isn't pumped yet, so its slot waits.
    std::vector<Received> ann = Drain(session.AnnMachine, {});
    REQUIRE(std::ranges::contains(SystemTypes(ann), uint32_t{DPSYS_HOST}));

    // A search on the session's port is answered by the new hub, naming its own port: an impostor connects there and
    // asks to be Cat's machine.
    uint16_t hubPort = 0;
    {
        const MCSocket::Handle udp = MCSocket::OpenUdp(0);
        REQUIRE(udp != MCSocket::InvalidHandle);
        std::vector<uint8_t> query;
        PutU32(query, RawMagic);
        query.push_back(1);
        PutGuid(query, TestApplication);
        REQUIRE(MCSocket::SendTo(udp, {MCSocket::LoopbackIp, port}, query));

        for (int attempt = 0; attempt < SearchAttempts * 10 && hubPort == 0; attempt++)
        {
            session.AnnMachine.Pump();
            std::array<uint8_t, 1500> answer{};
            MCSocket::Address from;

            // Magic, kind, then the port joins go to.
            if (MCSocket::ReceiveFrom(udp, answer, from) > 7)
            {
                hubPort = static_cast<uint16_t>(answer[5] | (answer[6] << 8));
            }

            SDL_Delay(1);
        }

        MCSocket::Close(udp);
    }

    REQUIRE(hubPort != 0);
    CHECK(hubPort != port);
    const MCSocket::Handle impostor = MCSocket::ConnectTcp({MCSocket::LoopbackIp, hubPort}, 1000);
    REQUIRE(impostor != MCSocket::InvalidHandle);
    std::vector<uint8_t> resume;
    PutU32(resume, RawMagic);
    PutGuid(resume, TestApplication);
    PutGuid(resume, sessions[0].guidInstance);
    PutU32(resume, catMachine);
    const std::vector<uint8_t> wrongKey(32, 0x5a);
    PutBlob(resume, wrongKey);
    PutBlob(resume, wrongKey);
    PutU32(resume, 1);
    resume.push_back(0);
    resume.push_back(0);
    REQUIRE_EQ(MCSocket::Send(impostor, RawFrame(ResumeFrame, resume)), static_cast<int>(resume.size() + 5));
    const auto refused = RawRead(impostor, {session.AnnMachine});
    REQUIRE(refused.has_value());
    CHECK_EQ(int{refused->first}, int{ResumeReplyFrame});
    CHECK(ReplyResult(refused->second) != uint32_t{DP_OK});
    CHECK(RawClosed(impostor, {session.AnnMachine}));
    MCSocket::Close(impostor);

    // Cat's own machine comes back, its player Cat's as before.
    const std::vector<Received> cat = Drain(session.CatMachine, {session.AnnMachine});
    CHECK(HubLeft(cat, session.Bob, session.Team));
    CHECK(Listed(session.AnnMachine, false) ==
          (std::map<uint32_t, std::string>{{session.Ann, "Ann"}, {session.Cat, "Cat"}}));
    REQUIRE_EQ(session.CatMachine.Send(session.Cat, session.Ann, 0, Bytes("me")), DP_OK);
    std::optional<Received> me = NextAmong(session.AnnMachine, {session.CatMachine});
    REQUIRE(me.has_value());
    CHECK_EQ(me->From, session.Cat);
}

/// <summary>
/// An older build's joiner (its join says nothing of the rollover) never takes the session over: its players go with
/// the host's, on the new hub and the machine coming back to it, and its link ends with the host's machine.
/// </summary>
TEST_CASE("directplay: an older build's joiner loses the session as its host goes, its players let go")
{
    constexpr uint16_t port = 28965;
    ThreeMachines session(port);
    const std::vector<DPSESSIONDESC2> sessions = Enumerate(session.CatMachine, *session.Host, 0);
    REQUIRE_EQ(sessions.size(), size_t{1});

    // The older build joins as release 1's did: its join's words end with the password; then its player.
    const MCSocket::Handle older = MCSocket::ConnectTcp({MCSocket::LoopbackIp, port}, 1000);
    REQUIRE(older != MCSocket::InvalidHandle);
    std::vector<uint8_t> join;
    PutU32(join, RawMagic);
    PutGuid(join, TestApplication);
    PutGuid(join, sessions[0].guidInstance);
    PutBlob(join, {});
    MCSocket::Send(older, RawFrame(JoinRequestFrame, join));
    const auto joined = RawRead(older, {*session.Host});
    REQUIRE(joined.has_value());
    CHECK_EQ(int{joined->first}, int{JoinReplyFrame});
    CHECK_EQ(ReplyResult(joined->second), uint32_t{DP_OK});
    std::vector<uint8_t> create;
    PutU32(create, 1);
    PutU32(create, 0);
    PutU32(create, DPPLAYERTYPE_PLAYER);
    PutU32(create, 0);
    PutU32(create, 0);
    const std::string_view name = "Old";
    PutBlob(create, Bytes(name));
    PutBlob(create, {});
    PutBlob(create, {});
    PutU32(create, 0);
    MCSocket::Send(older, RawFrame(CreatePlayerFrame, create));
    const auto created = RawRead(older, {*session.Host});
    REQUIRE(created.has_value());
    CHECK_EQ(int{created->first}, int{CreateReplyFrame});
    REQUIRE_EQ(ReplyResult(created->second, 4), uint32_t{DP_OK});
    const uint32_t old = ReplyResult(created->second, 8);
    Drain(session.AnnMachine, {*session.Host, session.CatMachine});
    Drain(session.CatMachine, {*session.Host, session.AnnMachine});
    REQUIRE(Listed(session.CatMachine, false).contains(old));

    session.Host.reset();
    const auto leftOf = [](const std::vector<Received>& messages, uint32_t id)
    {
        return std::ranges::any_of(messages,
                                   [&](const Received& message)
                                   {
                                       return message.From == DPID_SYSMSG &&
                                              message.SystemType() == DPSYS_DESTROYPLAYERORGROUP &&
                                              message.As<DPMSG_DESTROYPLAYERORGROUP>().dpId == id;
                                   });
    };

    const std::vector<Received> ann = Drain(session.AnnMachine, {session.CatMachine});
    const std::vector<Received> cat = Drain(session.CatMachine, {session.AnnMachine});
    CHECK(leftOf(ann, old));
    CHECK(leftOf(cat, old));
    CHECK(HubLeft(cat, session.Bob, session.Team));
    const std::map<uint32_t, std::string> players{{session.Ann, "Ann"}, {session.Cat, "Cat"}};
    CHECK(Listed(session.AnnMachine, false) == players);
    CHECK(Listed(session.CatMachine, false) == players);
    CHECK(RawClosed(older, {}));
    MCSocket::Close(older);
}

/// <summary>
/// Where a joiner reaches the machine taking over: where its hub saw that machine, but where the hub was for one the
/// hub saw over an internet game's link, or on its own loopback (a second copy of the game on the hub's computer) when
/// the joiner reached the hub from another computer. A LAN search finds a host on the same computer at 127.0.0.1 too,
/// so such a copy joins over the loopback, and its hub shares it as 127.0.0.1: a third computer reaching for it there
/// reached for itself (8 Nov's playtest: the laptop's game lost the session).
/// </summary>
TEST_CASE("directplay: a rollover reaches the machine taking over where its host was, for one the host saw on its own "
          "computer or over an internet link")
{
    constexpr uint32_t hub = 0xc0a8320au;       // 192.168.50.10
    constexpr uint32_t other = 0xc0a83214u;     // 192.168.50.20
    constexpr uint32_t hubLink = 0x00000005u;   // an internet game's link, 0.0.0.5
    constexpr uint32_t otherLink = 0x00000007u; // 0.0.0.7
    constexpr uint32_t loopbackOther = 0x7f000002u;

    CHECK(MCDirectPlay::RolloverIp(other, hub) == other);
    CHECK(MCDirectPlay::RolloverIp(MCSocket::LoopbackIp, hub) == hub);
    CHECK(MCDirectPlay::RolloverIp(loopbackOther, hub) == hub);
    CHECK(MCDirectPlay::RolloverIp(MCSocket::LoopbackIp, MCSocket::LoopbackIp) == MCSocket::LoopbackIp);
    CHECK(MCDirectPlay::RolloverIp(other, MCSocket::LoopbackIp) == other);
    CHECK(MCDirectPlay::RolloverIp(otherLink, hubLink) == hubLink);
    CHECK(MCDirectPlay::RolloverIp(otherLink, hub) == hub);
}

/// <summary>
/// A launcher's game reaching its host through the launcher's relay on its own computer (MechCommander Online's: a join
/// at 127.0.0.1) never takes the session over: a host whose every guest joined so has no successor (its leave ends the
/// session, as before the rollover), and its guests lose the session as it goes, without waiting for a machine to take
/// it over.
/// </summary>
TEST_CASE("directplay: guests that never take over leave their host no successor, and lose the session as it goes")
{
    constexpr uint16_t port = 28976;
    const std::string address = std::format("127.0.0.1:{}", port);
    std::optional<MCDirectPlay> host{std::in_place};
    REQUIRE_EQ(Connect(*host, address), DP_OK);
    REQUIRE_EQ(Host(*host, "Relayed", 6), DP_OK);
    uint32_t bob = 0;
    REQUIRE_EQ(NewPlayer(*host, "Bob", bob), DP_OK);
    MCDirectPlay ann;
    MCDirectPlay cat;
    ann.NeverTakeOver();
    cat.NeverTakeOver();
    JoinAs(ann, *host, address, "Ann");
    JoinAs(cat, *host, address, "Cat");
    Drain(*host, {ann, cat});
    Drain(ann, {*host, cat});
    Drain(cat, {*host, ann});
    CHECK(!host->HasSuccessor());

    host.reset();
    const std::vector<uint32_t> annHeard = SystemTypes(Drain(ann, {cat}));
    REQUIRE(!annHeard.empty());
    CHECK_EQ(annHeard.back(), uint32_t{DPSYS_SESSIONLOST});
    CHECK(!ann.RollingOver());
}

/// <summary>
/// Whether a launcher's join goes through this computer's own loopback (where MechCommander Online's relay is), so the
/// game never takes the session over.
/// </summary>
TEST_CASE("directplay: a launcher's join at this computer's own address goes through it")
{
    REQUIRE(MCSocket::Startup());
    CHECK(MCSessionManager::JoinsThroughThisComputer("127.0.0.1"));
    CHECK(MCSessionManager::JoinsThroughThisComputer("127.0.0.1:28800"));
    CHECK(MCSessionManager::JoinsThroughThisComputer("127.0.0.2:28800"));
    CHECK(MCSessionManager::JoinsThroughThisComputer(""));
    CHECK(!MCSessionManager::JoinsThroughThisComputer("192.168.50.10"));
    CHECK(!MCSessionManager::JoinsThroughThisComputer("192.168.50.10:28800"));
}

/// <summary>
/// A rollover whose new hub can't be connected to yet (SetRolloverReach: an internet game's link to the game's new host
/// not yet open) waits for it, within its wait: Cat's machine comes back only once it can, and its game hears nothing
/// meanwhile; one that can't within the wait loses the session.
/// </summary>
TEST_CASE("directplay: a rollover waits until the machine taking over can be reached, within its wait")
{
    constexpr auto shortWait = std::chrono::milliseconds(400);
    ThreeMachines session(28969);
    std::atomic<bool> reachable = false;
    session.CatMachine.SetRolloverReach([&] { return reachable.load(); }, RolloverTestWait);
    session.Host.reset();
    CHECK(SystemTypes(Drain(session.AnnMachine, {})) ==
          (std::vector<uint32_t>{uint32_t{DPSYS_DELETEPLAYERFROMGROUP}, uint32_t{DPSYS_DESTROYPLAYERORGROUP},
                                 uint32_t{DPSYS_HOST}}));
    CHECK(Drain(session.CatMachine, {session.AnnMachine}).empty());
    CHECK(session.CatMachine.RollingOver());
    reachable = true;
    CHECK(HubLeft(Drain(session.CatMachine, {session.AnnMachine}), session.Bob, session.Team));
    CHECK(!session.CatMachine.RollingOver());

    ThreeMachines unreached(28970);
    unreached.CatMachine.SetRolloverReach([] { return false; }, shortWait);
    unreached.Host.reset();
    Drain(unreached.AnnMachine, {});
    CHECK(Drain(unreached.CatMachine, {unreached.AnnMachine}).empty());
    CHECK(unreached.CatMachine.RollingOver());
    SDL_Delay(static_cast<uint32_t>(shortWait.count()));
    CHECK(SystemTypes(Drain(unreached.CatMachine, {unreached.AnnMachine})) ==
          std::vector<uint32_t>{uint32_t{DPSYS_SESSIONLOST}});
}

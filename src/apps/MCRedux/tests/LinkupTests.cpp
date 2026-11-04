#include "stdafx.h"
#include "MCTest.h"
#include "fakes/MCLoopbackTransport.h"
#include "fakes/MCManualClock.h"
#include "linkup/MCFidpGroup.h"
#include "linkup/MCFidpMessage.h"
#include "linkup/MCFidpNetworkProtocol.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "main/MCGameContext.h"
#include "platform/MCDirectPlay.h"
#include "platform/MCFileSystem.h"

// The linkup layer: guaranteed delivery over DirectPlay (each sender numbers its guaranteed messages per receiver,
// the receiver acknowledges them by number and hands them on in order), the players, groups and sessions it keeps,
// and a whole session between two session managers on the in-process network.

namespace
{
    /// <summary>The game id the tests host under.</summary>
    constexpr _GUID TestApplication{0x87654321, 0x4321, 0x8765, {8, 7, 6, 5, 4, 3, 2, 1}};

    /// <summary>A DirectPlay name.</summary>
    DPNAME Name(std::string_view shortName)
    {
        return DPNAME{std::string(shortName), std::nullopt};
    }

    /// <summary>A guaranteed message whose send counter for player number <paramref name="number"/> is <paramref name="count"/>.</summary>
    std::shared_ptr<MCFidpMessage> Numbered(int32_t number, uint8_t count)
    {
        auto msg = std::make_shared<MCFidpMessage>(1, 0x200);
        MCFIGuaranteedMessageHeader header;
        header.Header = FIMSG_GUARANTEED | 20;
        header.Tagger.SendCount[number] = count;
        msg->SetMessageBuffer(LinkupBytes(header));
        return msg;
    }

    /// <summary>Runs <paramref name="call"/> while another thread pumps <paramref name="other"/>'s DirectPlay.</summary>
    template <class Call> auto WhilePumping(MCSessionManager& other, Call&& call)
    {
        std::atomic<bool> stop = false;
        std::thread pumper(
            [&]
            {
                while (!stop)
                {
                    other.DirectPlay->Pump();
                    SDL_Delay(1);
                }
            });
        auto result = call();
        stop = true;
        pumper.join();
        return result;
    }

    /// <summary>Lets both machines handle what they were sent until <paramref name="done"/> (at most two seconds).</summary>
    template <class Done> bool Exchange(MCSessionManager& one, MCSessionManager& two, Done&& done)
    {
        for (int round = 0; round < 400; round++)
        {
            one.ProcessMessages();
            two.ProcessMessages();

            if (done())
            {
                return true;
            }

            SDL_Delay(5);
        }

        return false;
    }

    /// <summary>
    /// The first session <paramref name="joiner"/> lists while <paramref name="host"/> pumps (none after 40 tries): the
    /// enumeration is answered when the host next pumps, and a later call lists the session.
    /// </summary>
    std::shared_ptr<MCFidpSession> FindSession(MCSessionManager& host, MCSessionManager& joiner)
    {
        for (int attempt = 0; attempt < 40; attempt++)
        {
            const std::optional<std::vector<std::shared_ptr<MCFidpSession>>> sessions = joiner.GetSessions();

            if (sessions.has_value() && !sessions->empty())
            {
                return sessions->front();
            }

            host.DirectPlay->Pump();
            SDL_Delay(5);
        }

        return nullptr;
    }
}

/// <summary>
/// A guaranteed message waits in the receiver's verify list until a verify names its number; the round trip of one
/// sent once goes into the five-sample latency history, whose mean (500 ms with no samples) is the player's latency.
/// </summary>
TEST_CASE("linkup: a guaranteed message waits until it is verified by its number, and its round trip is averaged")
{
    MCTestContextScope scope;
    MCManualClock& clock = scope.Context().SetClock(std::make_unique<MCManualClock>());
    MCFidpPlayer player(7, Name("Guest"), 0);
    player.PlayerNumber = 2;
    CHECK_EQ(player.AverageLatency(), 500);

    std::vector<std::shared_ptr<MCFidpMessage>> sent;

    for (uint8_t count = 10; count < 13; count++)
    {
        sent.push_back(Numbered(2, count));
        player.AddToVerifyList(sent.back());
        CHECK_EQ(sent.back()->TimesSent, 1);
    }

    player.OutgoingSendCount = 12;
    // The send counter is two past the oldest one waiting (10): 13 - 10.
    CHECK_EQ(player.VerifyCountDifference(), 3);
    CHECK(!player.IsVerifyListFull());

    // 40 ms later the middle one is verified: it leaves the list, and its round trip is the only sample.
    clock.Advance(40'000'000);
    CHECK(player.RemoveFromVerifyList(11) == sent[1]);
    CHECK(player.RemoveFromVerifyList(11) == nullptr);
    CHECK_EQ(player.VerifyList.size(), size_t{2});
    CHECK_EQ(player.LastLatency, 40u);
    CHECK_EQ(player.AverageLatency(), 40);

    // A resent message's round trip is not measured.
    sent[0]->WasResent = true;
    clock.Advance(100'000'000);
    CHECK(player.RemoveFromVerifyList(10) == sent[0]);
    CHECK_EQ(player.AverageLatency(), 40);
    CHECK(player.RemoveFromVerifyList(12) == sent[2]);
    CHECK_EQ(player.AverageLatency(), (40 + 140) / 2);
    CHECK_EQ(player.VerifyCountDifference(), 0);

    // Five samples are kept: the sixth replaces the first.
    for (int i = 0; i < 5; i++)
    {
        auto msg = Numbered(2, 20);
        player.AddToVerifyList(msg);
        clock.Advance(10'000'000);
        CHECK(player.RemoveFromVerifyList(20) == msg);
    }

    CHECK_EQ(player.AverageLatency(), 10);

    // 128 or more unverified messages fill the list (the receiver's window is 128).
    player.OutgoingSendCount = 0xff;
    auto oldest = Numbered(2, 0x80);
    player.AddToVerifyList(oldest);
    CHECK_EQ(player.VerifyCountDifference(), 0x80);
    CHECK(player.IsVerifyListFull());
}

/// <summary>
/// Guaranteed messages from a player are held by their number and handed on in order; one outside the 128-number
/// window ahead of the next expected, or a duplicate, is refused.
/// </summary>
TEST_CASE("linkup: incoming guaranteed messages are handed on in number order, inside a 128-number window")
{
    MCFidpPlayer sender(5, Name("Host"), 0);
    auto second = Numbered(0, 1);
    auto first = Numbered(0, 0);
    auto third = Numbered(0, 2);

    CHECK(sender.HandleIncomingMessage(second, 1));
    CHECK(sender.NextMessageToProcess() == nullptr);
    CHECK_EQ(int{sender.NextIncomingSendCount}, 0);
    CHECK(!sender.HandleIncomingMessage(second, 1));
    CHECK(sender.HandleIncomingMessage(first, 0));
    // Both are here: the next number expected is past them.
    CHECK_EQ(int{sender.NextIncomingSendCount}, 2);
    CHECK_EQ(sender.NumIncomingMessages, 2);
    CHECK(sender.NextMessageToProcess() == first);
    CHECK(sender.NextMessageToProcess() == second);
    CHECK(sender.NextMessageToProcess() == nullptr);

    // 2 + 127 is the last number of the window; 2 + 128 is out.
    auto far = Numbered(0, 130);
    CHECK(!sender.HandleIncomingMessage(far, 130));
    auto edge = Numbered(0, 129);
    CHECK(sender.HandleIncomingMessage(edge, 129));
    CHECK(sender.HandleIncomingMessage(third, 2));
    CHECK(sender.NextMessageToProcess() == third);
    CHECK_EQ(sender.NumIncomingMessages, 1);
}

/// <summary>A group takes a player once; the player keeps the groups it joined.</summary>
TEST_CASE("linkup: a group takes each player once and a player records the groups it is in")
{
    MCFidpGroup group(40, Name("InnerSphereGroup"), 0);
    CHECK_EQ(group.Name, std::string("InnerSphereGroup"));
    CHECK(group.AddPlayer(3));
    CHECK(group.AddPlayer(9));
    CHECK(!group.AddPlayer(3));
    CHECK(group.Players == (std::vector<uint32_t>{3, 9}));
    CHECK(group.RemovePlayer(3));
    CHECK(!group.RemovePlayer(3));
    CHECK(group.Players == (std::vector<uint32_t>{9}));

    MCFidpPlayer player(9, Name("Guest"), 0);
    player.JoinGroup(40);
    player.JoinGroup(41);
    CHECK(player.IsInGroup(41));
    player.LeaveGroup(41);
    CHECK(!player.IsInGroup(41));
    CHECK(player.IsInGroup(40));

    // DirectPlay's names are cut: a group's short name at 64 characters, a player's at 127.
    const std::string longName(200, 'x');
    CHECK_EQ(MCFidpGroup(1, Name(longName.c_str()), 0).Name.size(), size_t{64});
    CHECK_EQ(MCFidpPlayer(1, Name(longName.c_str()), 0).Name.size(), size_t{127});
}

/// <summary>A session's description holds its own name and password, which are cut to 63 characters.</summary>
TEST_CASE("linkup: a session keeps its own name and password, cut to 63 characters, for its description")
{
    MCFidpSession session(TestApplication);
    CHECK_EQ(session.SessionDesc.dwMaxPlayers, 6u);
    CHECK(!session.SessionDesc.SessionName.has_value());
    const std::string longName(80, 'n');
    session.SetName(longName);
    CHECK_EQ(session.Name.size(), size_t{63});
    CHECK(session.SessionDesc.SessionName == session.Name);
    session.SetPassword("secret");
    CHECK((session.SessionDesc.dwFlags & DPSESSION_PASSWORDREQUIRED) != 0);

    // A copy holds copies, which the copy's description holds too.
    const MCFidpSession copy(session);
    CHECK(copy.SessionDesc.SessionName == session.Name);
    CHECK(copy.SessionDesc.SessionName == copy.Name);
    CHECK(copy.SessionDesc.Password == std::optional<std::string>("secret"));

    // Without a password the flag goes, and the description still holds the password kept.
    session.SetPassword(std::nullopt);
    CHECK((session.SessionDesc.dwFlags & DPSESSION_PASSWORDREQUIRED) == 0);
    CHECK(session.SessionDesc.Password == session.Password);
    session.SetName(std::nullopt);
    CHECK_EQ(session.Name, std::string("No name"));
}

/// <summary>
/// DirectPlay's names were C strings, which the original copied with strncpy: a name ends at its first null, and a
/// missing one is empty (a session's missing name is "No name").
/// </summary>
TEST_CASE("linkup: names end at their first null, as the original's string copies did")
{
    const DPNAME name{std::string("Hunter\0Killer", 13), std::string("Long\0Name", 9)};
    const MCFidpPlayer player(1, name, 0);
    CHECK_EQ(player.Name, std::string("Hunter"));
    CHECK_EQ(player.LongName, std::string("Long"));
    CHECK_EQ(MCFidpGroup(2, name, 0).Name, std::string("Hunter"));

    const MCFidpNetworkProtocol protocol(name, std::vector<uint8_t>{1, 2, 3});
    CHECK_EQ(protocol.ShortName, std::string("Hunter"));
    CHECK_EQ(protocol.LongName, std::string("Long"));
    CHECK(protocol.ConnectionBuffer == (std::vector<uint8_t>{1, 2, 3}));
    const MCFidpNetworkProtocol unnamed(DPNAME{}, {});
    CHECK(unnamed.ShortName.empty());
    CHECK(unnamed.LongName.empty());

    MCFidpSession session(TestApplication);
    session.SetName(std::string_view("Solaris\0VII", 11));
    CHECK_EQ(session.Name, std::string("Solaris"));
    session.SetPassword(std::string_view("pass\0word", 9));
    CHECK_EQ(session.Password, std::string("pass"));
}

/// <summary>
/// Before the server numbers this machine, a guaranteed group message and a server message wait in their queues. The
/// sends set the flags in the caller's own message (a group message is marked so; a server message isn't), and the
/// queued copy holds the message's bytes and its receiver.
/// </summary>
TEST_CASE("linkup: messages sent before this machine has a number wait in their queues, flagged in place")
{
    MCTestContextScope scope;
    scope.Context().SetNet(std::make_unique<MCLoopbackTransport>());
    MCSessionManager manager(TestApplication);
    REQUIRE(!manager.HasPlayerNumber);

    MCFIValueMessage latency;
    latency.Header = LinkupHeader(MCLinkupMessageType::Latency, FIMSG_GUARANTEED);
    latency.Value = 77;
    manager.SendMessageToGroup(9, LinkupBytes(latency));
    CHECK((latency.Header & FIMSG_GROUP_MESSAGE) != 0);
    REQUIRE_EQ(manager.PreIDGroupMessages.size(), size_t{1});
    const MCFidpMessage& queued = *manager.PreIDGroupMessages.front();
    CHECK_EQ(queued.ToID, 9u);
    CHECK(std::ranges::equal(queued.Bytes(), LinkupBytes(latency)));

    MCFIValueMessage plain;
    plain.Header = static_cast<uint16_t>(FIMSG_GROUP_MESSAGE | 20);
    manager.ServerID = 4;
    manager.SendMessageToServer(LinkupBytes(plain));
    CHECK_EQ(plain.Header, uint16_t{20});
    REQUIRE_EQ(manager.PreIDServerMessages.size(), size_t{1});
    CHECK_EQ(manager.PreIDServerMessages.front()->ToID, 4u);
    CHECK_EQ(manager.PreIDServerMessages.front()->MessageSize, uint32_t{sizeof(plain)});
}

/// <summary>
/// Two machines on the in-process network: the host hosts a session and makes a group; the joiner finds the session,
/// joins it and gets its player number (1, the host being 0) and the group's members from the host; a guaranteed
/// message reaches the host once and is verified; a file transfer is dropped; the host hands the server role to the machine with more memory;
/// and when the joiner leaves the host drops it.
/// </summary>
TEST_CASE("linkup: a joiner joins the host's session, is numbered, verified, made server, and dropped when it leaves")
{
    MCTestContextScope scope;
    scope.Context().SetNet(std::make_unique<MCLoopbackTransport>());
    constexpr std::string_view address = "127.0.0.1:28890";

    MCSessionManager host(TestApplication);
    host.ConnectTcp(address);
    MCFidpSession hosted(TestApplication);
    hosted.SetName("Solaris VII");
    REQUIRE_EQ(host.HostSession(hosted, "Commander"), 0);
    REQUIRE(host.MyPlayer() != nullptr);
    CHECK(host.IsHost);
    CHECK(host.HasPlayerNumber);
    CHECK_EQ(host.MyPlayer()->PlayerNumber, 0);
    CHECK_EQ(host.ServerID, host.MyPlayerID);
    uint32_t group = 0;
    host.CreateGroup(group, "InnerSphereGroup", {}, 0);
    REQUIRE(host.GetGroup(group) != nullptr);
    CHECK(host.AddPlayerToGroup(group, 0));
    CHECK(host.MyPlayer()->IsInGroup(group));

    std::vector<std::vector<uint8_t>> hostReceived;
    host.ApplicationCallback = [&](MCFidpMessage& msg)
    { hostReceived.emplace_back(msg.Bytes().begin(), msg.Bytes().end()); };

    MCSessionManager joiner(TestApplication);
    joiner.ConnectTcp(address);
    const std::shared_ptr<MCFidpSession> found = FindSession(host, joiner);
    REQUIRE(found != nullptr);
    CHECK_EQ(found->Name, std::string("Solaris VII"));
    const _GUID instance = found->SessionDesc.guidInstance;
    REQUIRE_EQ(WhilePumping(host, [&] { return joiner.JoinSession(instance, "Guest"); }), 0);
    REQUIRE(joiner.MyPlayer() != nullptr);
    CHECK(!joiner.IsHost);
    CHECK(!joiner.HasPlayerNumber);

    // The host numbers the new player and tells it; the joiner then knows everyone's number and the server.
    REQUIRE(Exchange(host, joiner, [&] { return joiner.HasPlayerNumber; }));
    CHECK_EQ(joiner.MyPlayer()->PlayerNumber, 1);
    CHECK_EQ(joiner.ServerID, host.MyPlayerID);
    CHECK_EQ(host.Players.size(), size_t{2});
    CHECK_EQ(host.GetPlayer(joiner.MyPlayerID)->PlayerNumber, 1);
    CHECK_EQ(joiner.GetPlayer(host.MyPlayerID)->PlayerNumber, 0);

    // The players of each group follow, so the joiner knows who is on the host's team.
    REQUIRE(Exchange(host, joiner,
                     [&]
                     {
                         const auto listed = joiner.GetPlayer(host.MyPlayerID);
                         return listed != nullptr && listed->IsInGroup(group);
                     }));

    // A guaranteed message to the server arrives once, and the joiner's copy leaves its verify list when the host's
    // verify comes back. The joiner's own system information (sent at the join) went the same way.
    MCFIValueMessage hello;
    hello.Header = FIMSG_GUARANTEED | 20;
    hello.Value = 0x1234;
    joiner.SendMessageToServerGuaranteed(LinkupBytes(hello));
    REQUIRE(Exchange(host, joiner, [&] { return !hostReceived.empty(); }));
    auto hostOnJoiner = joiner.GetPlayer(host.MyPlayerID);
    REQUIRE(Exchange(host, joiner, [&] { return hostOnJoiner->VerifyList.empty(); }));
    REQUIRE_EQ(hostReceived.size(), size_t{1});
    CHECK_EQ(ReadLinkupMessage<MCFIValueMessage>(hostReceived[0]).Value, 0x1234u);
    CHECK_EQ(host.GetPlayer(joiner.MyPlayerID)->TotalPhysicalMemory, MCPort::TotalPhysicalMemory());

    // A file transfer (the original's announcement with its "name\directory", then a piece of the file) is dropped:
    // nothing is written and the game never sees it. The message after it arrives as usual.
    const std::filesystem::path dropped = MCFileSystem::ResolveWrite("mc_tests_dropped.bin");
    std::filesystem::remove(dropped);
    hostReceived.clear();
    std::array<uint8_t, 0x20> begin{};
    MCFIGuaranteedMessageHeader beginHeader;
    beginHeader.Header = LinkupHeader(MCLinkupMessageType::BeginFileTransfer, FIMSG_GUARANTEED);
    std::memcpy(begin.data(), &beginHeader, sizeof(beginHeader));
    constexpr std::string_view droppedName = "mc_tests_dropped.bin";
    std::memcpy(begin.data() + sizeof(beginHeader) + 1, droppedName.data(), droppedName.size());
    joiner.SendMessageToServerGuaranteed(begin);
    std::array<uint8_t, sizeof(MCFIGuaranteedMessageHeader) + 4> piece{};
    MCFIGuaranteedMessageHeader pieceHeader;
    pieceHeader.Header = LinkupHeader(MCLinkupMessageType::FileData, FIMSG_GUARANTEED);
    std::memcpy(piece.data(), &pieceHeader, sizeof(pieceHeader));
    piece[sizeof(pieceHeader) + 1] = 1;
    joiner.SendMessageToServerGuaranteed(piece);
    hello.Value = 0x5678;
    joiner.SendMessageToServerGuaranteed(LinkupBytes(hello));
    REQUIRE(Exchange(host, joiner, [&] { return !hostReceived.empty(); }));
    REQUIRE_EQ(hostReceived.size(), size_t{1});
    CHECK_EQ(ReadLinkupMessage<MCFIValueMessage>(hostReceived[0]).Value, 0x5678u);
    CHECK(!std::filesystem::exists(dropped));

    // Everyone knows the host's player, as DirectPlay marked the server player; a joiner's never is.
    CHECK(joiner.DirectPlay->IsSessionHostPlayer(host.MyPlayerID));
    CHECK(!joiner.DirectPlay->IsSessionHostPlayer(joiner.MyPlayerID));
    CHECK(host.DirectPlay->IsSessionHostPlayer(host.MyPlayerID));
    CHECK(!host.DirectPlay->IsSessionHostPlayer(joiner.MyPlayerID));
    CHECK(host.SpeaksForSession(host.MyPlayerID));
    CHECK(!host.SpeaksForSession(joiner.MyPlayerID));

    // A joiner that isn't the server can't run the session (issue #25): naming itself the server, removing the host
    // from the game, taking the host out of its group or sending more than a message holds all come to nothing, and
    // what it sends after them still arrives.
    hostReceived.clear();
    MCFIValueMessage takeOver;
    takeOver.Header = LinkupHeader(MCLinkupMessageType::NewServer, FIMSG_GUARANTEED);
    takeOver.Value = joiner.MyPlayerID;
    joiner.SendMessageToServerGuaranteed(LinkupBytes(takeOver));
    // (The removal goes straight through DirectPlay: one sent guaranteed takes a number the receiver never orders.)
    MCFIValueMessage remove;
    remove.Header = LinkupHeader(MCLinkupMessageType::PlayerRemoved, FIMSG_GUARANTEED);
    remove.Value = host.MyPlayerID;
    CHECK_EQ(joiner.DirectPlay->Send(joiner.MyPlayerID, host.MyPlayerID, 0, LinkupBytes(remove)), uint32_t{DP_OK});
    CHECK_EQ(joiner.DirectPlay->DeletePlayerFromGroup(group, host.MyPlayerID), uint32_t{DP_OK});
    std::vector<uint8_t> oversized(0x300, 0x55);
    SetLinkupHeader(oversized, 20);
    CHECK_EQ(joiner.DirectPlay->Send(joiner.MyPlayerID, host.MyPlayerID, 0, oversized), uint32_t{DP_OK});
    hello.Value = 0x9abc;
    joiner.SendMessageToServerGuaranteed(LinkupBytes(hello));
    REQUIRE(Exchange(host, joiner, [&] { return !hostReceived.empty(); }));
    REQUIRE_EQ(hostReceived.size(), size_t{1});
    CHECK_EQ(ReadLinkupMessage<MCFIValueMessage>(hostReceived[0]).Value, 0x9abcu);
    CHECK(host.IsHost);
    CHECK_EQ(host.ServerID, host.MyPlayerID);
    CHECK(host.MyPlayer() != nullptr);
    CHECK(host.MyPlayer()->IsInGroup(group));
    CHECK(host.GetGroup(group) != nullptr && std::ranges::contains(host.GetGroup(group)->Players, host.MyPlayerID));

    // The server role goes to the player with the most memory (over 100000): the joiner here.
    host.MyPlayer()->TotalPhysicalMemory = 1;
    host.GetPlayer(joiner.MyPlayerID)->TotalPhysicalMemory = 200000;
    host.SwitchServers();
    CHECK(!host.IsHost);
    CHECK_EQ(host.ServerID, joiner.MyPlayerID);
    REQUIRE(Exchange(host, joiner, [&] { return joiner.IsHost; }));
    CHECK_EQ(joiner.ServerID, joiner.MyPlayerID);

    // The joiner leaves: the host takes it off its list.
    CHECK(joiner.LeaveSession());
    CHECK(joiner.MyPlayer() == nullptr);
    REQUIRE(Exchange(host, joiner, [&] { return host.Players.size() == 1; }));
    CHECK(host.GetPlayer(host.MyPlayerID) != nullptr);
}

/// <summary>
/// A session holds six players, so only a host that breaks the rules can announce a seventh: a joiner leaves it out and
/// plays on. The original ended the game ("Could not connect to game."); fuzzing the joiner found it (4 Nov).
/// </summary>
TEST_CASE("linkup: a joiner leaves out a seventh player a host announces, where the original's game quit")
{
    MCTestContextScope scope;
    scope.Context().SetNet(std::make_unique<MCLoopbackTransport>());
    constexpr std::string_view address = "127.0.0.1:28891";
    // The host's player and the joiner's take numbers 0 and 1; these take 2 to 5, and the last is the seventh.
    constexpr int32_t extraPlayers = MaxLinkupPlayers - 1;

    MCSessionManager host(TestApplication);
    host.ConnectTcp(address);
    MCFidpSession hosted(TestApplication);
    hosted.SetName("Solaris VII");
    // The rule broken: the host's DirectPlay takes more players than a session holds.
    hosted.SessionDesc.dwMaxPlayers = MaxLinkupPlayers + 2;
    REQUIRE_EQ(host.HostSession(hosted, "Commander"), 0);

    MCSessionManager joiner(TestApplication);
    joiner.ConnectTcp(address);
    const std::shared_ptr<MCFidpSession> found = FindSession(host, joiner);
    REQUIRE(found != nullptr);
    const _GUID instance = found->SessionDesc.guidInstance;
    REQUIRE_EQ(WhilePumping(host, [&] { return joiner.JoinSession(instance, "Guest"); }), 0);
    REQUIRE(Exchange(host, joiner, [&] { return joiner.HasPlayerNumber; }));

    std::vector<std::vector<uint8_t>> joinerReceived;
    joiner.ApplicationCallback = [&](MCFidpMessage& msg)
    { joinerReceived.emplace_back(msg.Bytes().begin(), msg.Bytes().end()); };

    // More players on the host's machine, then a message: when it arrives, the joiner has handled every player.
    std::vector<uint32_t> extras;

    for (int32_t i = 0; i < extraPlayers; i++)
    {
        uint32_t id = 0;
        REQUIRE_EQ(host.DirectPlay->CreatePlayer(id, Name("Extra " + std::to_string(i)), {}, 0), DP_OK);
        extras.push_back(id);
    }

    MCFIValueMessage after;
    after.Header = 20;
    after.Value = 0x7777;
    host.SendMessageToGroup(0, LinkupBytes(after));
    REQUIRE(Exchange(host, joiner, [&] { return !joinerReceived.empty(); }));
    CHECK_EQ(ReadLinkupMessage<MCFIValueMessage>(joinerReceived.front()).Value, 0x7777u);

    CHECK_EQ(joiner.Players.size(), static_cast<size_t>(MaxLinkupPlayers));
    CHECK(joiner.GetPlayer(extras.back()) == nullptr);
    CHECK(joiner.MyPlayer() != nullptr);

    for (int32_t i = 0; i + 1 < extraPlayers; i++)
    {
        const std::shared_ptr<MCFidpPlayer> listed = joiner.GetPlayer(extras[static_cast<size_t>(i)]);
        REQUIRE(listed != nullptr);
        CHECK_EQ(listed->PlayerNumber, i + 2);
    }
}

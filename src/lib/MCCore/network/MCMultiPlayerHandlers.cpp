#include "stdafx.h"
#include "network/MCMultiPlayerHandlers.h"
#include "network/MCMultiPlayer.h"
#include "abl/MCAblDebugger.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCRefit.h"
#include "ai/MCScenarioMap.h"
#include "ai/MCTacticalOrder.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCCommandParser.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "linkup/MCFidpMessage.h"
#include "linkup/MCFidpPlayer.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCLogToolButton.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "network/MCMPGameConfiguration.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCGameSession.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "mission/MCMissionResultsScreen.h"
#include "mission/MCScenario.h"
#include "object/MCArtillery.h"
#include "object/MCArtilleryChunk.h"
#include "object/MCBuilding.h"
#include "object/MCExplosion.h"
#include "object/MCExplosionType.h"
#include "object/MCForces.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCMoverGroup.h"
#include "object/MCObjectSystem.h"
#include "object/MCTerrainObject.h"
#include "object/MCTree.h"
#include "object/MCTreeBuilding.h"
#include "object/MCTurret.h"
#include "object/MCWeaponShotInfo.h"
#include "platform/MCFileSystem.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTerrain.h"

// The handlers of the game's messages: what the server tells the clients (scenario start and end, the movers',
// turrets' and weapons' updates, the world's events), what the clients tell the server (check-ins, orders, artillery)
// and the session screen's own messages, and the SessionManager callbacks that dispatch to them.

namespace
{
    /// <summary>The entry angle of a weapon hit (WeaponHitChunk::EntryAngle) in degrees.</summary>
    constexpr std::array<float, 4> HitEntryAngles = {0.0f, 180.0f, -90.0f, 90.0f};

    /// <summary>The radio message a pilot sends for a kill, by kill kind.</summary>
    /// <remarks>
    /// The original's table has six entries; kinds 6 and 7 (allowed by BuildPilotKillStat) read on into the next
    /// data, 0 and the bytes of a string.
    /// </remarks>
    constexpr std::array<int32_t, 8> KillRadioMessages = {19, 19, 19, 19, 18, 17, 0, 0};

    /// <summary>The multiplayer game (the handlers only run with one).</summary>
    MCMultiPlayer& Game()
    {
        return *MultiPlayer();
    }

    /// <summary>Whether a mission is running a multiplayer game with company (every in-mission handler's check).</summary>
    bool InMultiplayerMission()
    {
        return Scenario() != nullptr && EventsToMissionResultsScreen == 0 && Game().NumPlayers() > 1;
    }

    /// <summary>The value at <paramref name="offset"/> of <paramref name="bytes"/> (zero past their end).</summary>
    template <class T> T Get(std::span<const uint8_t> bytes, size_t offset)
    {
        return bytes.size() > offset ? ReadLinkupMessage<T>(bytes.subspan(offset)) : T{};
    }

    /// <summary>The byte at <paramref name="offset"/> of <paramref name="bytes"/> (0 past their end).</summary>
    uint8_t ByteAt(std::span<const uint8_t> bytes, size_t offset)
    {
        return offset < bytes.size() ? bytes[offset] : 0;
    }

    /// <summary><paramref name="count"/> packed words from <paramref name="offset"/> of <paramref name="bytes"/>.</summary>
    std::vector<uint32_t> Words(std::span<const uint8_t> bytes, size_t offset, size_t count)
    {
        std::vector<uint32_t> words(count);

        for (size_t i = 0; i < count; i++)
        {
            words[i] = Get<uint32_t>(bytes, offset + i * 4);
        }

        return words;
    }

    /// <summary>
    /// <paramref name="count"/> bytes from <paramref name="offset"/> of <paramref name="bytes"/>, as many as there are.
    /// </summary>
    std::span<const uint8_t> BytesAt(std::span<const uint8_t> bytes, size_t offset, size_t count)
    {
        return offset < bytes.size() ? bytes.subspan(offset, std::min(count, bytes.size() - offset))
                                     : std::span<const uint8_t>();
    }

    // Issue #25: every message comes from another machine, which can send anything. The checks below keep its indexes,
    // ids and names to what the game can take; a message that fails one is dropped and logged.

    /// <summary>Logs a message dropped because of what it held.</summary>
    void Dropped(std::string_view what, uint32_t fromID)
    {
        SDL_Log("MCMultiPlayer: dropped %.*s from player %u", static_cast<int>(what.size()), what.data(), fromID);
    }

    /// <summary>The player number of <paramref name="fromID"/> (-1 when it has none).</summary>
    int32_t PlayerNumberOf(uint32_t fromID)
    {
        const auto player = Game().SessionManager != nullptr ? Game().SessionManager->GetPlayer(fromID) : nullptr;
        return player != nullptr && player->PlayerNumber >= 0 && player->PlayerNumber < MaxLinkupPlayers
                   ? player->PlayerNumber
                   : -1;
    }

    /// <summary>
    /// Whether <paramref name="checkInId"/>, a check-in id or commander id in a message, is its sender's own: a player
    /// checks in, orders and calls artillery only for itself (the game numbers check-ins as the session numbers its
    /// players).
    /// </summary>
    bool IsSendersOwn(uint32_t fromID, int32_t checkInId)
    {
        return checkInId >= 0 && checkInId == PlayerNumberOf(fromID);
    }

    /// <summary>Whether world point <paramref name="point"/> (x and y) is on the game map.</summary>
    bool OnMap(const MCVector3D& point)
    {
        if (!std::isfinite(point.X) || !std::isfinite(point.Y))
        {
            return false;
        }

        int32_t tileR = 0;
        int32_t tileC = 0;
        MCScenarioMap::WorldToMapTilePos(point, tileR, tileC);
        return GameMap().OnMap(tileR, tileC);
    }

    /// <summary>Whether map cell (<paramref name="cellR"/>, <paramref name="cellC"/>) is on the game map.</summary>
    bool CellOnMap(int32_t cellR, int32_t cellC)
    {
        return cellR >= 0 && cellC >= 0 && GameMap().OnMap(cellR / MapCellDim, cellC / MapCellDim);
    }

    /// <summary>
    /// Whether <paramref name="name"/>, a file name from another machine, stays in the game's folders: relative, with
    /// no "..", drive or stream, and (with <paramref name="bare"/>) no folder at all. With
    /// <paramref name="extension"/>, it must end in it (case ignored).
    /// </summary>
    bool IsGameFileName(std::string_view name, bool bare, std::string_view extension = {})
    {
        if (!MCFileSystem::IsContainedPath(name) || (bare && name.find_first_of("\\/") != std::string_view::npos))
        {
            return false;
        }

        return extension.empty() || (name.size() >= extension.size() &&
                                     MCPort::StrICmp(name.substr(name.size() - extension.size()), extension) == 0);
    }

    /// <summary>Sets up the dialog's single OK button with the given exit.</summary>
    void ShowOkDialog(MCReusableDialog& dialog, std::string_view text, std::function<void()> exit)
    {
        dialog.SetText(text);
        dialog.SetTwoButton(false);
        dialog.Callback = nullptr;
        dialog.OkButton->Callback().SetExec(std::move(exit));
        char upArt[] = "bh_okay.tga";
        char downArt[] = "bg_okay.tga";
        dialog.OkButton->SetUpPicture(upArt);
        dialog.OkButton->SetDownPicture(downArt);
        dialog.OkButton->Disabled = false;
        dialog.OkButton->Draw();
        dialog.Activate();
    }
}

MCWeaponHitChunk EmptyWeaponHitChunk()
{
    MCWeaponHitChunk chunk;
    chunk.HitLocation = -1;
    return chunk;
}

void HandleSysCreatePlayer(const DPMSG_CREATEPLAYERORGROUP& msg)
{
    MCMultiPlayer& game = Game();

    if (game.SessionManager->CurrentConnection != MCNetProtocol::Lobby)
    {
        NumLanPlayers++;
    }

    if (game.IsServer)
    {
        game.SendPlayerSetup(msg.dpId, game.InnerSphereGroupID, game.ClanGroupID);
    }
}

void HandleSysAddPlayerToGroup(const DPMSG_ADDPLAYERTOGROUP& msg)
{
    MCMultiPlayer& game = Game();
    const auto player = game.SessionManager->GetPlayer(msg.dpIdPlayer);

    if (player != nullptr && player->PlayerNumber >= 0 && player->PlayerNumber < MaxLinkupPlayers)
    {
        game.PlayerTeams[player->PlayerNumber] = {msg.dpIdPlayer, static_cast<int32_t>(msg.dpIdGroup)};
    }
}

void HandleAppChat(MCFidpMessage& msg)
{
    const auto player = Game().SessionManager->GetPlayer(msg.FromID);
    // Port fix: a sender already gone from the session has no name; the original read through the null player.
    const std::string line = std::format("{}: {}", player != nullptr ? player->Name : std::string(),
                                         MessageText(msg.Bytes(), sizeof(MCMPChatMessage)));

    if (AblGetDebugger() != nullptr)
    {
        // The original formatted it into 256 bytes.
        AblGetDebugger()->Print(std::string_view(line).substr(0, 0xff));
    }
}

void HandleAppNewServer(uint32_t, std::span<const uint8_t> msg)
{
    Game().SetServer(static_cast<uint32_t>(ReadLinkupMessage<MCMPLongMessage>(msg).Value));
}

void HandleAppPlayerCheckIn(uint32_t fromID, std::span<const uint8_t> msg)
{
    const auto checkIn = ReadLinkupMessage<MCMPPlayerCheckInMessage>(msg);
    MCMultiPlayer& game = Game();

    if (!IsSendersOwn(fromID, checkIn.CheckInId))
    {
        Dropped("a check-in for another player", fromID);
        return;
    }

    if (GlobalLog() != nullptr && GlobalLog()->PlayerLights != nullptr)
    {
        GlobalLog()->PlayerLights->SetPlayerStatus(fromID, 2);
    }

    if (game.IsServer)
    {
        game.PlayerCheckedIn[static_cast<size_t>(checkIn.CheckInId)] = static_cast<int32_t>(fromID);

        if (game.AllPlayersCheckedIn())
        {
            game.HandleOwnMessages = true;
            game.SendStartScenario(game.PlayerCheckedIn, "");
            game.HandleOwnMessages = false;
        }
    }
}

void HandleAppPlayerSetup(uint32_t, std::span<const uint8_t> msg)
{
    const auto setup = ReadLinkupMessage<MCMPPlayerSetupMessage>(msg);
    MCMultiPlayer& game = Game();
    MCSessionManager& sessionManager = *game.SessionManager;

    if (game.CheckInId == -1)
    {
        game.CheckInId = sessionManager.MyPlayer()->PlayerNumber;
    }

    game.InitUpdateFrequencies();
    game.HostID = sessionManager.ServerID;
    game.ServerID = game.HostID;
    game.AllPlayerGroupID = setup.AllPlayerGroupID;
    game.ClanGroupID = setup.ClanGroupID;
    game.InnerSphereGroupID = setup.InnerSphereGroupID;

    if (game.HomeTeam == 0)
    {
        sessionManager.AddPlayerToGroup(game.InnerSphereGroupID, 0);
        game.HomeTeamGroupID = game.InnerSphereGroupID;
        game.EnemyTeamGroupID = game.ClanGroupID;
    }
    else if (game.HomeTeam == 1)
    {
        sessionManager.AddPlayerToGroup(game.ClanGroupID, 0);
        game.HomeTeamGroupID = game.ClanGroupID;
        game.EnemyTeamGroupID = game.InnerSphereGroupID;
    }

    sessionManager.AddPlayerToGroup(game.AllPlayerGroupID, 0);
}

void HandleAppPlayerCheckInReceipt(uint32_t fromID, std::span<const uint8_t> msg)
{
    const int32_t checkInId = ReadLinkupMessage<MCMPLongMessage>(msg).Value;
    MCMultiPlayer& game = Game();

    if (!IsSendersOwn(fromID, checkInId))
    {
        Dropped("a check-in receipt for another player", fromID);
        return;
    }

    game.PlayerCheckedIn[static_cast<size_t>(checkInId)] = static_cast<int32_t>(fromID);

    if (game.AllPlayersCheckedIn())
    {
        game.HandleOwnMessages = true;
        game.SendStartPlanning();
        game.HandleOwnMessages = false;
    }
}

void HandleAppStartPlanning(uint32_t, std::span<const uint8_t>)
{
    Game().InLogistics = true;
}

void HandleAppReadyForBattle(uint32_t fromID, std::span<const uint8_t> msg)
{
    const auto ready = ReadLinkupMessage<MCMPPlayerCheckInMessage>(msg);
    MCMultiPlayer& game = Game();

    if (GlobalLog() == nullptr)
    {
        return;
    }

    if (!IsSendersOwn(fromID, ready.CheckInId))
    {
        Dropped("a ready-for-battle for another player", fromID);
        return;
    }

    if (GlobalLog()->PlayerLights != nullptr)
    {
        GlobalLog()->PlayerLights->SetPlayerStatus(fromID, 2);
    }

    if (game.IsHost)
    {
        game.PlayerCheckedIn[static_cast<size_t>(ready.CheckInId)] = static_cast<int32_t>(fromID);

        if (game.AllPlayersCheckedIn())
        {
            game.SendPrepareScenario();
        }
    }
}

void HandleAppJoinTeam(uint32_t, std::span<const uint8_t> msg)
{
    const auto join = ReadLinkupMessage<MCMPJoinTeamMessage>(msg);

    if (GlobalLog() != nullptr && GlobalLog()->SessionScreen != nullptr)
    {
        GlobalLog()->SessionScreen->AssignPlayer(join.PlayerID, join.Team, join.Slot, false);
    }
}

void HandleAppRPUpdate(uint32_t, std::span<const uint8_t> msg)
{
    const auto update = ReadLinkupMessage<MCMPTwoLongMessage>(msg);

    if (GlobalLog() == nullptr || GlobalLog()->SessionScreen == nullptr)
    {
        return;
    }

    const std::shared_ptr<MCSessionScreen> sessionScreen = GlobalLog()->SessionScreen;

    if (update.Value2 == 1)
    {
        sessionScreen->SetTeam1RP(update.Value1);
    }

    if (update.Value2 == 2)
    {
        sessionScreen->SetTeam2RP(update.Value1);
    }

    sessionScreen->Draw();
}

void HandleAppTechbaseChange(uint32_t, std::span<const uint8_t> msg)
{
    const auto change = ReadLinkupMessage<MCMPTwoLongMessage>(msg);

    if (GlobalLog() != nullptr && GlobalLog()->SessionScreen != nullptr)
    {
        GlobalLog()->SessionScreen->SetTeamTechBase(static_cast<int8_t>(change.Value1),
                                                    static_cast<int8_t>(change.Value2));
    }
}

void HandleAppSwitchScreen(uint32_t, std::span<const uint8_t> msg)
{
    if (ReadLinkupMessage<MCMPTwoLongMessage>(msg).Value1 == 1 && GlobalLog() != nullptr)
    {
        GlobalLog()->SetUpSessionScreen();
    }
}

void HandleAppStartScenario(uint32_t fromID, std::span<const uint8_t> msg)
{
    const auto start = ReadLinkupMessage<MCMPStartScenarioMessage>(msg);
    MCMultiPlayer& game = Game();

    // A start with no scenario loaded is ignored (the original went on through a null scenario).
    if (Scenario() == nullptr)
    {
        Dropped("a scenario start outside a scenario", fromID);
        return;
    }

    for (uint32_t i = 1; i <= Scenario()->NumParts(); i++)
    {
        const auto player = game.SessionManager->GetPlayerNumber(Scenario()->Parts[i].CommanderId);

        if (player != nullptr)
        {
            if (const auto mover = std::dynamic_pointer_cast<MCMover>(LockObject(Scenario()->Parts[i].Object)))
            {
                mover->NetOwnerID = player->Id;
                // The original copied at most 255 characters into the name's buffer.
                mover->NetName = player->Name.substr(0, 0xff);
            }
        }
    }

    for (int32_t i = 0; i < std::min(game.NumMovers, static_cast<int32_t>(start.MoverFlags.size())); i++)
    {
        if (const std::shared_ptr<MCMover> mover = game.RosterMover(i);
            mover != nullptr && mover->GetPilot() != nullptr)
        {
            mover->GetPilot()->EscapesThruEjection = (start.MoverFlags[static_cast<size_t>(i)] & 1) != 0 ? 1 : 0;
        }
    }

    game.InMission = true;
    game.PrepareScenarioReceived = false;

    if (AblGetDebugger() != nullptr)
    {
        AblGetDebugger()->Print(MessageText(msg, sizeof(MCMPStartScenarioMessage)));
    }
}

void HandleAppEndScenario(uint32_t, std::span<const uint8_t> msg)
{
    Game().ScenarioResult = ReadLinkupMessage<MCMPLongMessage>(msg).Value;
}

void HandleAppPlayerOrder(uint32_t fromID, std::span<const uint8_t> msg)
{
    const auto message = ReadLinkupMessage<MCMPPlayerOrderMessage>(msg);

    if (!InMultiplayerMission())
    {
        return;
    }

    std::shared_ptr<MCCommander> commander = CommanderById(message.CheckInId);

    if (!Game().IsServer)
    {
        return;
    }

    if (!IsSendersOwn(fromID, message.CheckInId) || commander == nullptr)
    {
        Dropped("an order for another player's units", fromID);
        return;
    }

    MCTacticalOrder order;
    order.Reset();
    order.Data[0] = message.PackedOrder[0];
    order.Data[1] = message.PackedOrder[1];
    order.Unpack();
    MCVector3D wayPoint;
    wayPoint.X = std::bit_cast<float>(message.OrderParam1);
    wayPoint.Y = std::bit_cast<float>(message.OrderParam2);

    // The way point (sent for move and jump orders, zero otherwise), the order and its aim must be ones the game has:
    // an aim goes straight to the target's armor tables.
    const auto aimTarget = std::dynamic_pointer_cast<MCMover>(LockObject(order.Target));
    const bool aimValid =
        order.AttackParams.AimLocation == -1 || (order.AttackParams.AimLocation >= 0 && aimTarget != nullptr &&
                                                 aimTarget->IsHitLocation(order.AttackParams.AimLocation));
    const bool wayPointValid = (message.OrderParam1 == 0 && message.OrderParam2 == 0) || OnMap(wayPoint);
    const bool targetPointValid =
        order.Code != MCTacticalOrderCode::AttackPoint || OnMap(order.AttackParams.TargetPoint);

    if (order.Code >= MCTacticalOrderCode::Count || !aimValid || !wayPointValid || !targetPointValid)
    {
        Dropped("an order the game can't carry out", fromID);
        return;
    }

    wayPoint.Z = MCTerrain::GetTerrainElevation(wayPoint);
    order.SetWayPoint(0, wayPoint);

    // A jump-attack (method 1) becomes a jump to the target's position, as Parser::SendTacOrder does locally.
    if (order.Code == MCTacticalOrderCode::AttackObject && order.AttackParams.Method == 1)
    {
        order.Code = MCTacticalOrderCode::JumpToObject;
        order.MoveParams.Wait = 0;
        order.MoveParams.WayPath.Mode[0] = 0;

        if (LockObject(order.Target) != nullptr)
        {
            order.SetWayPoint(0, LockObject(order.Target)->GetPosition());
        }
    }

    if (order.Code == MCTacticalOrderCode::JumpToObject)
    {
        order.Code = MCTacticalOrderCode::JumpToPoint;
        Assert(LockObject(order.Target) != nullptr, 0, " JumpToObject is NULL ");

        // A jump to a target that isn't there is dropped (the original went on through the null target).
        if (LockObject(order.Target) == nullptr)
        {
            Dropped("a jump to no target", fromID);
            return;
        }

        order.SetWayPoint(0, LockObject(order.Target)->GetPosition());
    }

    std::array<std::shared_ptr<MCMover>, MCMultiPlayer::MaxLocalMovers> movers{};
    std::shared_ptr<MCMover> point;
    const int32_t numMovers = order.GetGroup(message.CheckInId, movers, point);
    std::array<MCVector3D, 72> jumpGoals{};
    // Bits 1-4: the order goes to group (bit - 1) of the commander too.
    auto groupFlagged = [&](int32_t groupId) { return (message.Flags & (2 << groupId)) != 0; };

    if (order.Code == MCTacticalOrderCode::JumpToPoint)
    {
        int32_t numGoals = numMovers;

        for (int32_t groupId = 0; groupId < 4; groupId++)
        {
            if (groupFlagged(groupId))
            {
                numGoals += commander->GetGroup(groupId)->NumMovers();
            }
        }

        CalcJumpGoals(order.GetWayPoint(0), std::min(numGoals, static_cast<int32_t>(jumpGoals.size())), jumpGoals,
                      order.GetJumpTarget());
    }

    const int fromGroup = (message.Flags & 0x20) != 0 ? 1 : 0;

    if (numMovers > 0)
    {
        // Original behaviour (OB-105): the client sends its sort flag in bit 0, but the server reads bit 4, group 3's
        // bit.
        const bool sortMovers = (message.Flags & 0x10) != 0;

        if (sortMovers)
        {
            SortMoverList(std::span<const std::shared_ptr<MCMover>>(movers).first(static_cast<size_t>(numMovers)),
                          order.GetWayPoint(0));
        }

        for (int32_t i = 0; i < numMovers; i++)
        {
            const std::shared_ptr<MCMover>& mover = movers[i];

            if (mover == nullptr || mover == LockObject(order.Target))
            {
                continue;
            }

            if (sortMovers)
            {
                order.SelectionIndex = mover->SelectionIndex;
            }

            if (order.Code == MCTacticalOrderCode::JumpToPoint)
            {
                order.SetWayPoint(0, jumpGoals[i]);
            }

            mover->HandleTacticalOrder(order, 1, fromGroup);
        }
    }

    int32_t goalIndex = numMovers;

    for (int32_t groupId = 0; groupId < 4; groupId++)
    {
        if (!groupFlagged(groupId))
        {
            continue;
        }

        std::span<const MCVector3D> destinations;

        if (order.Code == MCTacticalOrderCode::JumpToPoint)
        {
            destinations = std::span<const MCVector3D>(jumpGoals).subspan(
                std::min(static_cast<size_t>(goalIndex), jumpGoals.size()));
            goalIndex += commander->GetGroup(groupId)->NumMovers();
        }

        commander->GetGroup(groupId)->HandleTacticalOrder(order, 1, destinations, fromGroup);
    }
}

void HandleAppPlayerMoverGroup(uint32_t fromID, std::span<const uint8_t> msg)
{
    const auto message = ReadLinkupMessage<MCMPPlayerMoverGroupMessage>(msg);

    if (!InMultiplayerMission())
    {
        return;
    }

    const std::shared_ptr<MCCommander> commander = CommanderById(message.CheckInId);

    if (!IsSendersOwn(fromID, message.CheckInId) || commander == nullptr || message.GroupId < 0 ||
        message.GroupId >= MCCommander::MaxGroups)
    {
        Dropped("a group for another player's units", fromID);
        return;
    }

    const std::array<std::weak_ptr<MCMover>, MCMultiPlayer::MaxLocalMovers>& playerMovers =
        Game().PlayerMoverRoster[static_cast<size_t>(message.CheckInId)];
    uint32_t memberBits = message.Members >> 4;
    std::vector<std::shared_ptr<MCMover>> movers;

    for (const std::weak_ptr<MCMover>& mover : playerMovers)
    {
        // A slot whose mover is gone isn't added (the original added the null).
        if ((memberBits & 1) != 0 && LockObject(mover) != nullptr)
        {
            movers.push_back(LockObject(mover));
        }

        memberBits >>= 1;
    }

    const auto numMovers = static_cast<int32_t>(movers.size());

    int32_t pointIndex = message.Members & 0xf;
    int32_t i = 0;

    // A point past the 12 slots (from the message) is no mover (the original read past them).
    const std::shared_ptr<MCMover> pointMover = pointIndex < MCMultiPlayer::MaxLocalMovers
                                                    ? LockObject(playerMovers[static_cast<size_t>(pointIndex)])
                                                    : nullptr;

    for (; i < numMovers; i++)
    {
        if (pointMover == movers[i])
        {
            pointIndex = i;
            break;
        }
    }

    Assert(i < numMovers, 0, " handleAppPlayerMoverGroup: bad pointMover ");
    commander->SetGroup(message.GroupId, movers, pointIndex);
}

void HandleAppPlayerArtillery(uint32_t fromID, std::span<const uint8_t> msg)
{
    const auto message = ReadLinkupMessage<MCMPPlayerArtilleryMessage>(msg);

    if (!InMultiplayerMission() || !Game().IsServer)
    {
        return;
    }

    MCArtilleryChunk chunk;
    chunk.CommanderId = -1;
    chunk.StrikeType = -1;
    chunk.CellRow = -1;
    chunk.CellCol = -1;
    chunk.Seconds = -1;
    chunk.Data = message.ArtilleryData;
    chunk.Unpack();
    MCVector3D location;
    location.X = message.TargetX;
    location.Y = message.TargetY;

    // The strike is the sender's own, on the map (its type is three bits, all of them kinds of strike).
    if (!IsSendersOwn(fromID, chunk.CommanderId) || CommanderById(chunk.CommanderId) == nullptr || !OnMap(location))
    {
        Dropped("an artillery call for another player or off the map", fromID);
        return;
    }

    location.Z = MCTerrain::GetTerrainElevation(location);
    CallArtillery(chunk.CommanderId, chunk.StrikeType, location, chunk.Seconds, 0);
}

void HandleAppMoverUpdate(uint32_t, std::span<const uint8_t> msg)
{
    MCMultiPlayer& game = Game();
    const auto sequence = Get<uint16_t>(msg, 2);

    if (!InMultiplayerMission() || sequence < game.MoverUpdateSequence)
    {
        return;
    }

    if (!game.IsServer)
    {
        const size_t statusOffset = 4 + static_cast<size_t>(game.NumMovers) * 4;
        const size_t orderOffset = statusOffset + static_cast<size_t>(game.NumMovers) * 4;

        // A roster slot whose mover is gone is skipped (the original went on through the null).
        for (int32_t i = 0; i < game.NumMovers; i++)
        {
            const std::shared_ptr<MCMover> mover = game.RosterMover(i);
            Assert(mover != nullptr, 0, " handleAppMoveUpdate: No Mover ");

            if (mover != nullptr)
            {
                mover->HandleMoveChunk(Get<uint32_t>(msg, 4 + static_cast<size_t>(i) * 4));
            }
        }

        for (int32_t i = 0; i < game.NumMovers; i++)
        {
            if (const std::shared_ptr<MCMover> mover = game.RosterMover(i))
            {
                mover->HandleStatusChunk(sequence - game.MoverUpdateSequence,
                                         Get<uint32_t>(msg, statusOffset + static_cast<size_t>(i) * 4));
            }
        }

        for (int32_t i = 0; i < game.NumMovers; i++)
        {
            if (const std::shared_ptr<MCMover> mover = game.RosterMover(i);
                mover != nullptr && mover->GetPilot() != nullptr)
            {
                mover->GetPilot()->UpdateClientOrderQueue(ByteAt(msg, orderOffset + static_cast<size_t>(i)));
            }
        }
    }

    game.MoverUpdateSequence = static_cast<uint16_t>(sequence + 1);
}

void HandleAppTurretUpdate(uint32_t, std::span<const uint8_t> msg)
{
    MCMultiPlayer& game = Game();
    const auto sequence = Get<uint16_t>(msg, 2);

    if (!InMultiplayerMission() || sequence < game.TurretUpdateSequence)
    {
        return;
    }

    if (!game.IsServer)
    {
        for (int32_t i = 0; i < game.NumTurrets; i++)
        {
            const std::shared_ptr<MCTurret> turret = game.RosterTurret(i);
            Assert(turret != nullptr, 0, " handleAppTurretUpdate: No Turret ");
            const auto targetIndex = static_cast<int8_t>(ByteAt(msg, 4 + static_cast<size_t>(i)));

            if (turret == nullptr)
            {
                continue;
            }

            // Original behaviour (OB-106): -1 is no target, 0-127 a mover of the roster. The original's third case (a
            // part id from 0x80248 down) can't be reached with a signed byte, so building targets are misread. An index
            // past the roster (the original read past it) is no target.
            turret->Target = game.RosterMover(targetIndex);
        }
    }

    game.TurretUpdateSequence = static_cast<uint16_t>(sequence + 1);
}

void HandleAppMoverWeaponFireUpdate(uint32_t, std::span<const uint8_t> msg)
{
    MCMultiPlayer& game = Game();

    if (!InMultiplayerMission() || game.IsServer)
    {
        return;
    }

    const auto firstMover = static_cast<int8_t>(ByteAt(msg, 8));
    const auto numMovers = static_cast<int8_t>(ByteAt(msg, 9));
    size_t chunkIndex = 0;

    for (int32_t i = 0; i < numMovers; i++)
    {
        const uint8_t numChunks = ByteAt(msg, 10 + static_cast<size_t>(i));

        // A mover past the roster, or gone, gets nothing (the original wrote through the slot).
        if (const std::shared_ptr<MCMover> mover = game.RosterMover(firstMover + i))
        {
            mover->AddWeaponFireChunks(1, Words(msg, 0x22 + chunkIndex * 4, numChunks));
        }

        chunkIndex += numChunks;
    }
}

void HandleAppTurretWeaponFireUpdate(uint32_t, std::span<const uint8_t> msg)
{
    MCMultiPlayer& game = Game();

    if (!InMultiplayerMission() || game.IsServer)
    {
        return;
    }

    const auto numTurrets = static_cast<int8_t>(ByteAt(msg, 8));
    size_t chunkIndex = 0;

    for (int32_t i = 0; i < numTurrets; i++)
    {
        // Each turret's byte: its roster index (bits 2-7) and its number of chunks (bits 0-1).
        const uint8_t entry = ByteAt(msg, 9 + static_cast<size_t>(i));
        const size_t numChunks = entry & 3;

        // A turret past the roster, or gone, gets nothing (the original wrote through the slot).
        if (const std::shared_ptr<MCTurret> turret = game.RosterTurret(entry >> 2))
        {
            turret->AddWeaponFireChunks(1, Words(msg, 9 + static_cast<size_t>(numTurrets) + chunkIndex * 4, numChunks));
        }

        chunkIndex += numChunks;
    }
}

void HandleAppMoverCriticalHitUpdate(uint32_t, std::span<const uint8_t> msg)
{
    MCMultiPlayer& game = Game();

    if (!InMultiplayerMission() || game.IsServer)
    {
        return;
    }

    const auto firstMover = static_cast<int8_t>(ByteAt(msg, 8));
    const auto numMovers = static_cast<int8_t>(ByteAt(msg, 9));
    size_t chunkIndex = 0;

    for (int32_t i = 0; i < numMovers; i++)
    {
        // A mover past the roster, or gone, gets nothing (the original wrote through the slot); its bytes are still
        // stepped over.
        const std::shared_ptr<MCMover> mover = game.RosterMover(firstMover + i);
        const uint32_t numCriticalHits = ByteAt(msg, 10 + static_cast<size_t>(i));
        Assert(numCriticalHits < 0x81, numCriticalHits, " handleAppMoverCritHits: bad numCH ");

        if (numCriticalHits != 0)
        {
            if (mover != nullptr)
            {
                mover->AddCriticalHitChunks(1, BytesAt(msg, 0x3a + chunkIndex, numCriticalHits));
            }

            chunkIndex += numCriticalHits;
        }

        const uint32_t numRadio = ByteAt(msg, 0x22 + static_cast<size_t>(i));
        Assert(numRadio < 8, numRadio, " handleAppMoverCritHits: bad numRDO ");

        if (numRadio != 0)
        {
            if (mover != nullptr)
            {
                mover->AddRadioChunks(1, BytesAt(msg, 0x3a + chunkIndex, numRadio));
            }

            chunkIndex += numRadio;
        }
    }
}

void HandleAppWeaponHitUpdate(uint32_t fromID, std::span<const uint8_t> msg)
{
    MCMultiPlayer& game = Game();

    if (!InMultiplayerMission() || game.IsServer)
    {
        return;
    }

    const uint8_t numChunks = ByteAt(msg, 8);

    for (size_t i = 0; i < numChunks; i++)
    {
        MCWeaponHitChunk chunk = EmptyWeaponHitChunk();
        chunk.Data = Get<uint32_t>(msg, 9 + i * 4);
        chunk.Unpack();

        // The target is a mover of the roster (type 0) or the object a part id names (1 and 2); one that isn't there,
        // or isn't the kind the chunk needs, or an armor location the target doesn't have, is skipped. The original
        // read past the roster, cast whatever it found, and was fatal on type 3.
        std::shared_ptr<MCGameObject> target;

        if (chunk.TargetType == 0)
        {
            target = game.RosterMover(chunk.TargetId);
        }
        else if (chunk.TargetType == 1 || chunk.TargetType == 2)
        {
            target = std::dynamic_pointer_cast<MCGameObject>(ObjectList()->FindObjectFromPart(chunk.TargetId));
        }

        const auto mover = std::dynamic_pointer_cast<MCMover>(target);

        if (target == nullptr || (chunk.Refit != 0 && mover == nullptr) ||
            (chunk.Refit == 0 && mover != nullptr && !mover->IsHitLocation(chunk.HitLocation)))
        {
            Dropped("a weapon hit on no target", fromID);
            continue;
        }

        if (chunk.Refit == 0)
        {
            MCWeaponShotInfo shotInfo;
            shotInfo.Attacker.reset();
            shotInfo.MasterId = chunk.Cause;
            shotInfo.Damage = chunk.Damage;
            shotInfo.HitLocation = chunk.HitLocation;
            shotInfo.EntryAngle = HitEntryAngles[chunk.EntryAngle];
            target->HandleWeaponHit(shotInfo, 0);
            continue;
        }

        float pointsUsed = 0.0f;
        DoRefit(*mover, chunk.Damage, pointsUsed, chunk.Damage == -6.0f ? 1 : 0);
    }
}

namespace
{
    /// <summary>A mine chunk: the tile's mine layout (the Clan's or the Inner Sphere's), and an explosion when set off.</summary>
    void ApplyMine(const MCWorldStateChunk& chunk)
    {
        const int32_t tileR = chunk.TileRow / 3;
        const int32_t tileC = chunk.TileCol / 3;
        const int32_t layout = std::min(chunk.Param2, 3);

        // A mine off the map (from the server's message) is ignored; the original wrote past it.
        if (!GameMap().OnMap(tileR, tileC))
        {
            return;
        }

        MCMapTile& tile = GameMap().TileAt(tileR, tileC);

        if (chunk.Param1 == 1)
        {
            tile.Overlay = (tile.Overlay & 0xffff9fff) | (layout << 13);
        }
        else
        {
            tile.Overlay = (tile.Overlay & 0xffffe7ff) | (layout << 11);
        }

        if (chunk.Param2 <= 3)
        {
            return;
        }

        MCVector3D position = MapCellToWorldPos(chunk.TileRow, chunk.TileCol);
        position.Z = MCTerrain::GetTerrainElevation(position);

        if (chunk.Param2 == 4)
        {
            CreateExplosion(MineExplosion, position, 0.0f, 0.0f);
        }
        else if (chunk.Param2 == 5)
        {
            CreateExplosion(MineExplosion, position, MineSplashDamage, MineSplashRange * WorldUnitsPerMeter);
        }
    }

    /// <summary>A terrain fire chunk: the object it names is set on fire.</summary>
    void ApplyTerrainFire(const MCWorldStateChunk& chunk)
    {
        auto object = std::static_pointer_cast<MCGameObject>(ObjectList()->FindObjectFromPart(chunk.ObjectWid));

        if (object == nullptr || object->GetObjectType() == nullptr)
        {
            return;
        }

        const auto seconds = static_cast<float>(chunk.Param1);

        switch (object->ObjectClass)
        {
            case MCObjectClass::Building:
            {
                static_cast<MCBuilding&>(*object).LightOnFire(seconds);
                break;
            }

            case MCObjectClass::Tree:
            {
                static_cast<MCTree&>(*object).LightOnFire(seconds);
                break;
            }

            case MCObjectClass::MiscTerrainObject:
            {
                static_cast<MCTerrainObject&>(*object).LightOnFire(seconds);
                break;
            }

            case MCObjectClass::TreeBuilding:
            {
                static_cast<MCTreeBuilding&>(*object).LightOnFire(seconds);
                break;
            }

            default:
            {
                // The part id came from the server's message; an object that can't burn is left alone (the original was
                // fatal).
                break;
            }
        }
    }
}

void HandleAppWorldStateUpdate(uint32_t fromID, std::span<const uint8_t> msg)
{
    MCMultiPlayer& game = Game();

    if (!InMultiplayerMission() || game.IsServer)
    {
        return;
    }

    const uint8_t numChunks = ByteAt(msg, 8);

    for (size_t i = 0; i < numChunks; i++)
    {
        MCWorldStateChunk chunk;
        chunk.Data = Get<uint32_t>(msg, 10 + i * 4);

        // A type the game doesn't have is dropped (the original was fatal).
        if (!chunk.Unpack())
        {
            Dropped("a world event of no known type", fromID);
            continue;
        }

        switch (chunk.Type)
        {
            case MCWorldStateChunk::Mine:
            {
                ApplyMine(chunk);
                break;
            }

            case MCWorldStateChunk::TerrainFire:
            {
                ApplyTerrainFire(chunk);
                break;
            }

            case MCWorldStateChunk::MissionScriptMessage:
            {
                Scenario()->HandleMultiplayMessage(chunk.Param1, chunk.Param2);
                break;
            }

            case MCWorldStateChunk::PilotKillStat:
            {
                // A mover past the roster, or a kill kind past the pilot's seven counts, is dropped (the original read
                // past the roster and counted past the table).
                const std::shared_ptr<MCMover> mover = game.RosterMover(chunk.Param1);
                const std::shared_ptr<MCMechWarrior> pilot = mover != nullptr ? mover->GetPilot() : nullptr;

                if (pilot != nullptr && chunk.Param2 >= 0 &&
                    chunk.Param2 < static_cast<int32_t>(std::size(pilot->NumKilled)))
                {
                    pilot->NumKilled[chunk.Param2][1]++;

                    if (pilot->OnHomeTeam() != 0)
                    {
                        pilot->RadioMessage(KillRadioMessages[static_cast<size_t>(chunk.Param2)], 0);
                    }
                }

                break;
            }

            default:
            {
                // A strike off the map, or by a commander not in the game, is dropped.
                if (!CellOnMap(chunk.TileRow, chunk.TileCol) ||
                    CommanderById(chunk.Type - MCWorldStateChunk::Artillery) == nullptr)
                {
                    Dropped("an artillery strike off the map", fromID);
                    break;
                }

                MCVector3D location = MapCellToWorldPos(chunk.TileRow, chunk.TileCol);
                location.Z = MCTerrain::GetTerrainElevation(location);
                CallArtillery(chunk.Type - MCWorldStateChunk::Artillery, chunk.Param1, location, chunk.Param2, 0);
                break;
            }
        }
    }
}

void HandleAppPlayerUpdate(uint32_t, std::span<const uint8_t>)
{
    Assert(Game().IsServer, 0, " Sending player update to non-server ");
}

uint32_t GetCheckSum(std::string_view fileName)
{
    MCFile file;

    if (file.Open(fileName) != 0)
    {
        return 0;
    }

    std::vector<uint8_t> contents(file.GetLength());
    file.Read(contents);
    file.Close();
    // Only the file's first four bytes: the "checksum" tells little more than the file's existence.
    uint32_t checkSum = 0;
    std::memcpy(&checkSum, contents.data(), std::min<size_t>(contents.size(), 4));
    return checkSum;
}

void HandleAppFileInquiry(uint32_t fromID, std::span<const uint8_t> msg)
{
    // The inquiry's name starts at +0x8 (SendFileInquiry writes it there), not at a file-name message's +0xc.
    const std::string fileName(MessageText(msg, sizeof(MCFIGuaranteedMessageHeader)));
    MCMultiPlayer& game = Game();
    constexpr size_t nameOffset = sizeof(MCMPFileNameMessage);
    auto report = game.StartGuaranteedMessage<MCMPFileNameMessage>(MCMPMessageType::FileReport);

    // Issue #25: only a mission file in the game's folders is looked at; anything else is reported missing (the
    // original opened any path and sent back its first four bytes).
    const bool missionFile = IsGameFileName(fileName, false, ".mpk");

    if (!missionFile)
    {
        Dropped("an inquiry about a file outside the game", fromID);
    }

    report.Value = missionFile ? static_cast<int32_t>(GetCheckSum(fileName)) : 0;
    game.PutMessage(report);
    const size_t length = std::min(fileName.size(), MCMultiPlayer::MsgBufferSize - nameOffset - 1);
    std::memcpy(game.MsgBuffer.data() + nameOffset, fileName.data(), length);
    game.MsgBuffer[nameOffset + length] = 0;
    game.SessionManager->SendMessageToPlayerGuaranteed(
        fromID, game.Outgoing(static_cast<uint32_t>(length + nameOffset + 1)), true);
}

void HandleAppFileReport(uint32_t fromID, std::span<const uint8_t> msg)
{
    const bool haveFile = ReadLinkupMessage<MCMPLongMessage>(msg).Value != 0;

    // Port: a configured match asked (it has no session screen to count the answers).
    if (MPConfiguredMatchFileReport(fromID, haveFile))
    {
        return;
    }

    if (GlobalLog() != nullptr && GlobalLog()->SessionScreen != nullptr)
    {
        GlobalLog()->SessionScreen->FileReport(fromID, haveFile);
    }
}

void HandleAppLoadMission(uint32_t fromID, std::span<const uint8_t> msg)
{
    const std::string_view fileName = MessageText(msg, sizeof(MCMPFileNameMessage));

    // Issue #25: only a mission file in the game's folders is opened (the original opened any path the host sent).
    if (!IsGameFileName(fileName, false, ".mpk"))
    {
        Dropped("a mission outside the game's folders", fromID);
        return;
    }

    if (GlobalLog() != nullptr && GlobalLog()->SessionScreen != nullptr)
    {
        GlobalLog()->SessionScreen->LoadMission(fileName);
    }
}

void HandleAppStart(uint32_t fromID, std::span<const uint8_t> msg)
{
    const std::string_view missionName = MessageText(msg, sizeof(MCMPFileNameMessage));

    // Issue #25: the host sends the mission's bare name (LoadCampaign adds the folder and extension); anything else is
    // dropped, as is a start with no session screen to leave.
    if (!IsGameFileName(missionName, true) || GlobalLog() == nullptr || GlobalLog()->SessionScreen == nullptr)
    {
        Dropped("a start of a mission outside the game's folders", fromID);
        return;
    }

    GuiSystem().RemoveTimer(*GlobalLog()->SessionScreen, 0);
    Game().SessionManager->SendLatencyInfo();
    SoundSystem()->PlayBettySample(0x19);
    GlobalLog()->SessionScreen->PublishMatchState();
    GlobalLog()->InitializeMultiplayer();
    GlobalLog()->LoadCampaign(missionName, ".MPK", false, false);
    GlobalLog()->SetUpBriefingScreen(false);
}

void LostConnectionDialogExit()
{
    if (!LaunchedFromLobby)
    {
        MCGameContext::Current().SetMultiPlayer(nullptr);
    }
    else
    {
        KillTheGame(1);
    }
}

void HandleLocalPlayerRemoved()
{
    MCMultiPlayer& game = Game();
    LastConnectionType = game.SessionManager->CurrentConnection;

    if (!game.PrepareScenarioReceived && !game.InMission)
    {
        game.InLogistics = false;
        GlobalLog()->DestroyMultiplayer();

        if (GlobalLog()->CurrentScreen() != GlobalLog()->MainScreen)
        {
            WhackTimer = true;
        }

        GlobalLog()->CurrentScreen()->ShowGuiWindow(false);
        GlobalLog()->SetCurrentScreen(GlobalLog()->MainScreen);
        GlobalLog()->LogisticsState = 1;
        GlobalLog()->ShowLogScreen(true, true);
        game.LeaveSession();
    }
    else
    {
        game.InMission = false;
        game.LeaveSession();
        Mission()->EndScenario();
    }

    ShowOkDialog(*GlobalLog()->MessageDialog, LoadGameString(0x369, 0xfe), LostConnectionDialogExit);
}

void MultiPlayerSystemCallback(MCFidpMessage& msg)
{
    const std::span<const uint8_t> buffer = std::as_const(msg).Buffer();

    switch (ReadLinkupMessage<DPMSG_GENERIC>(buffer).dwType)
    {
        case DPSYS_CREATEPLAYERORGROUP:
        {
            const auto create = ReadLinkupMessage<DPMSG_CREATEPLAYERORGROUP>(buffer);

            if (create.dwPlayerType == DPPLAYERTYPE_PLAYER)
            {
                HandleSysCreatePlayer(create);
            }

            break;
        }

        case DPSYS_DESTROYPLAYERORGROUP:
        {
            const auto destroy = ReadLinkupMessage<DPMSG_DESTROYPLAYERORGROUP>(buffer);

            if (destroy.dwPlayerType == DPPLAYERTYPE_PLAYER)
            {
                Game().PlayerLeftGame(destroy.dpId);
            }

            break;
        }

        case DPSYS_ADDPLAYERTOGROUP:
        {
            HandleSysAddPlayerToGroup(ReadLinkupMessage<DPMSG_ADDPLAYERTOGROUP>(buffer));
            break;
        }

        case DPSYS_SESSIONLOST:
        {
            if (Scenario() == nullptr || Scenario()->StartingUp == 0)
            {
                HandleLocalPlayerRemoved();
            }
            else
            {
                BadSessionCounter++;

                if (BadSessionCounter > 10)
                {
                    KillTheGame(1);
                }
            }

            break;
        }

        default:
        {
            break;
        }
    }
}

namespace
{
    /// <summary>
    /// Whether <paramref name="fromID"/> may send a message of <paramref name="type"/>: what runs the game (the
    /// session's set-up, the scenario's start and end, every in-mission update, the session screen's choices) comes
    /// only from the server or the session's host; the players' own messages from anyone, checked by their handlers.
    /// </summary>
    /// <remarks>
    /// Issue #25: the original took every message from anyone, so any player could start, end or rewrite the game on
    /// the others' machines. The host relays every message and vouches for its sender.
    /// </remarks>
    bool MaySend(MCMPMessageType type, uint32_t fromID)
    {
        switch (type)
        {
            case MCMPMessageType::PlayerSetup:
            case MCMPMessageType::StartPlanning:
            case MCMPMessageType::StartScenario:
            case MCMPMessageType::EndScenario:
            case MCMPMessageType::MoverUpdate:
            case MCMPMessageType::TurretUpdate:
            case MCMPMessageType::MoverWeaponFireUpdate:
            case MCMPMessageType::TurretWeaponFireUpdate:
            case MCMPMessageType::MoverCriticalHitUpdate:
            case MCMPMessageType::WeaponHitUpdate:
            case MCMPMessageType::WorldStateUpdate:
            case MCMPMessageType::PrepareScenario:
            case MCMPMessageType::FileInquiry:
            case MCMPMessageType::LoadMission:
            case MCMPMessageType::Start:
            case MCMPMessageType::JoinTeam:
            case MCMPMessageType::SwitchScreen:
            case MCMPMessageType::RPUpdate:
            case MCMPMessageType::TechbaseChange:
            case MCMPMessageType::StartMatch:
            {
                const MCMultiPlayer& game = Game();
                return fromID == game.ServerID || fromID == game.HostID ||
                       (game.SessionManager != nullptr && game.SessionManager->SpeaksForSession(fromID));
            }

            default:
            {
                return true;
            }
        }
    }
}

void MultiPlayerApplicationCallback(MCFidpMessage& msg)
{
    const uint32_t fromID = msg.FromID;
    const std::span<const uint8_t> bytes = msg.Bytes();
    MCMultiPlayer& game = Game();
    const auto type = static_cast<MCMPMessageType>(msg.Type());

    if (!MaySend(type, fromID))
    {
        Dropped(std::format("message {} (the server's or host's)", static_cast<uint32_t>(type)), fromID);
        return;
    }

    switch (type)
    {
        case MCMPMessageType::NewServer:
        {
            HandleAppNewServer(fromID, bytes);
            break;
        }

        case MCMPMessageType::PlayerRemoved:
        {
            HandleLocalPlayerRemoved();
            break;
        }

        case MCMPMessageType::Latency:
        {
            if (game.SessionManager->ReadyToChooseServer())
            {
                game.SwitchServers();
            }

            break;
        }

        case MCMPMessageType::Chat:
        {
            game.ChatCallback(msg);
            break;
        }

        case MCMPMessageType::PlayerCheckIn:
        {
            HandleAppPlayerCheckIn(fromID, bytes);
            break;
        }

        case MCMPMessageType::PlayerSetup:
        {
            HandleAppPlayerSetup(fromID, bytes);
            break;
        }

        case MCMPMessageType::PlayerCheckInReceipt:
        {
            HandleAppPlayerCheckInReceipt(fromID, bytes);
            break;
        }

        case MCMPMessageType::StartPlanning:
        {
            HandleAppStartPlanning(fromID, bytes);
            break;
        }

        case MCMPMessageType::StartScenario:
        {
            HandleAppStartScenario(fromID, bytes);
            break;
        }

        case MCMPMessageType::EndScenario:
        {
            HandleAppEndScenario(fromID, bytes);
            break;
        }

        case MCMPMessageType::PlayerOrder:
        {
            HandleAppPlayerOrder(fromID, bytes);
            break;
        }

        case MCMPMessageType::PlayerMoverGroup:
        {
            HandleAppPlayerMoverGroup(fromID, bytes);
            break;
        }

        case MCMPMessageType::PlayerArtillery:
        {
            HandleAppPlayerArtillery(fromID, bytes);
            break;
        }

        case MCMPMessageType::MoverUpdate:
        {
            HandleAppMoverUpdate(fromID, bytes);
            break;
        }

        case MCMPMessageType::TurretUpdate:
        {
            HandleAppTurretUpdate(fromID, bytes);
            break;
        }

        case MCMPMessageType::MoverWeaponFireUpdate:
        {
            HandleAppMoverWeaponFireUpdate(fromID, bytes);
            break;
        }

        case MCMPMessageType::TurretWeaponFireUpdate:
        {
            HandleAppTurretWeaponFireUpdate(fromID, bytes);
            break;
        }

        case MCMPMessageType::MoverCriticalHitUpdate:
        {
            HandleAppMoverCriticalHitUpdate(fromID, bytes);
            break;
        }

        case MCMPMessageType::WeaponHitUpdate:
        {
            HandleAppWeaponHitUpdate(fromID, bytes);
            break;
        }

        case MCMPMessageType::WorldStateUpdate:
        {
            HandleAppWorldStateUpdate(fromID, bytes);
            break;
        }

        case MCMPMessageType::DeployForce:
        {
            // The bytes received, not the whole buffer: the handler reads no further than the message (issue #25).
            if (GlobalLog() != nullptr)
            {
                GlobalLog()->HandleDeployForceMessage(fromID, bytes);
            }

            break;
        }

        case MCMPMessageType::RemoveForce:
        {
            if (GlobalLog() != nullptr)
            {
                GlobalLog()->HandleRemoveForceMessage(fromID, bytes);
            }

            break;
        }

        case MCMPMessageType::PlayerUpdate:
        {
            HandleAppPlayerUpdate(fromID, bytes);
            break;
        }

        case MCMPMessageType::PrepareScenario:
        {
            game.PlayerCheckedIn.fill(0);

            if (GlobalLog() != nullptr)
            {
                GlobalLog()->HandlePrepareScenarioMessage();
            }

            game.PrepareScenarioReceived = true;
            break;
        }

        case MCMPMessageType::ReadyForBattle:
        {
            HandleAppReadyForBattle(fromID, bytes);
            break;
        }

        case MCMPMessageType::FileInquiry:
        {
            HandleAppFileInquiry(fromID, bytes);
            break;
        }

        case MCMPMessageType::FileReport:
        {
            HandleAppFileReport(fromID, bytes);
            break;
        }

        case MCMPMessageType::LoadMission:
        {
            HandleAppLoadMission(fromID, bytes);
            break;
        }

        case MCMPMessageType::Start:
        {
            HandleAppStart(fromID, bytes);
            break;
        }

        case MCMPMessageType::JoinTeam:
        {
            HandleAppJoinTeam(fromID, bytes);
            break;
        }

        case MCMPMessageType::SwitchScreen:
        {
            HandleAppSwitchScreen(fromID, bytes);
            break;
        }

        case MCMPMessageType::RPUpdate:
        {
            HandleAppRPUpdate(fromID, bytes);
            break;
        }

        case MCMPMessageType::TechbaseChange:
        {
            HandleAppTechbaseChange(fromID, bytes);
            break;
        }

        case MCMPMessageType::StartMatch:
        {
            MPHandleStartMatch(fromID, bytes);
            break;
        }

        case MCMPMessageType::SessionCheckIn:
        {
            // A sender without a number (gone, or not yet numbered) is ignored; the original read through the null
            // player or wrote before the table (issue #25).
            const int32_t playerNumber = PlayerNumberOf(fromID);
            Assert(playerNumber >= 0 && playerNumber <= 5, 0, "PNUM BAD");

            if (playerNumber < 0)
            {
                Dropped("a session check-in from no player", fromID);
                break;
            }

            game.PlayerSessionCheckIn[static_cast<size_t>(playerNumber)] = 1;

            if (GlobalLog() != nullptr && GlobalLog()->SessionScreen != nullptr)
            {
                GlobalLog()->SessionScreen->SomeoneCheckedIn();
            }

            break;
        }

        default:
        {
            break;
        }
    }
}

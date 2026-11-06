#include "stdafx.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerHandlers.h"
#include "abl/MCAblRoutines.h"
#include "abl/MCAblRuntime.h"
#include "ai/MCTacticalOrder.h"
#include "lib/MCFatal.h"
#include "linkup/MCFidpMessage.h"
#include "linkup/MCFidpPlayer.h"
#include "main/MCLogistics.h"
#include "main/MCMissionGlobals.h"
#include "main/MCTelemetry.h"
#include "network/MCGameDataHash.h"
#include "object/MCArtilleryChunk.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCMoverGroup.h"
#include "object/MCObjectSystem.h"
#include "object/MCTurret.h"
#include "sprite/MCMechStops.h"

// The multiplayer game's sends: the session messages (chat, check-in, setup, the scenario's start and end), the
// players' orders, and the server's updates of movers, turrets, weapons and the world, each packed into the message
// buffer.

namespace
{
    /// <summary>scenarioTime of a client's next player update to the server.</summary>
    /// <remarks>Original behaviour: a file static, so it carries over from one game to the next.</remarks>
    float NextPlayerUpdateTime = 0.0f;

    /// <summary>The most mover weapon-fire chunks one update message holds.</summary>
    constexpr int32_t MoverFireChunksPerMessage = 0x77;
    /// <summary>The most weapon-hit chunks one update message holds.</summary>
    constexpr size_t HitChunksPerMessage = 0x7d;

    /// <summary>Writes <paramref name="value"/> at <paramref name="offset"/> of <paramref name="buffer"/>.</summary>
    template <class T> void Put(std::span<uint8_t> buffer, size_t offset, const T& value)
    {
        std::memcpy(buffer.data() + offset, &value, sizeof(T));
    }

    /// <summary>An artillery chunk as the original's locals start (every field -1).</summary>
    MCArtilleryChunk EmptyArtilleryChunk()
    {
        MCArtilleryChunk chunk;
        chunk.CommanderId = -1;
        chunk.StrikeType = -1;
        chunk.CellRow = -1;
        chunk.CellCol = -1;
        chunk.Seconds = -1;
        chunk.Data = 0;
        return chunk;
    }
}

std::span<uint8_t> MCMultiPlayer::Outgoing(size_t size)
{
    uint16_t header = 0;
    std::memcpy(&header, MsgBuffer.data(), sizeof(header));
    MCTelemetry::MessageOut(header & FIMSG_TYPE_MASK, size);
    return std::span(MsgBuffer).first(size);
}

int32_t MCMultiPlayer::SendChat(uint32_t toID, std::string_view text)
{
    if (SessionManager == nullptr)
    {
        return -1;
    }

    constexpr size_t textOffset = sizeof(MCMPChatMessage);
    text = text.substr(0, MsgBufferSize - textOffset - 1);
    StartGuaranteedMessage(MCMPMessageType::Chat);
    MsgBuffer[textOffset - 1] = toID == 0 ? 1 : 0;
    std::memcpy(MsgBuffer.data() + textOffset, text.data(), text.size());
    MsgBuffer[textOffset + text.size()] = 0;
    const auto size = static_cast<uint32_t>(textOffset + text.size() + 1);
    SessionManager->SendMessageToGroup(toID, Outgoing(size));

    if (HandleOwnMessages)
    {
        const uint32_t myID = SessionManager->MyPlayer()->Id;
        MCFidpMessage message(myID, 0x200);
        message.FromID = myID;
        message.SetMessageBuffer(Built(size));
        ChatCallback(message);
    }

    return 0;
}

int32_t MCMultiPlayer::SendPlayerRules()
{
    auto rules = StartGuaranteedMessage<MCMPLongMessage>(MCMPMessageType::PlayerRules);
    rules.Value = static_cast<int32_t>(MCMatchRule::Supported);
    PutMessage(rules);

    if (!IsServer)
    {
        SessionManager->SendMessageToServerGuaranteed(Outgoing(sizeof(MCMPLongMessage)));
    }
    else
    {
        PlayerRules[SessionManager->MyPlayer()->Id] = MCMatchRule::Supported;
    }

    return 0;
}

uint32_t MCMultiPlayer::WantedRules() const
{
    return MCWantedRules(MatchOptions.Rules, GMechBraking != 0);
}

int32_t MCMultiPlayer::SendMatchRules()
{
    // Those the match options last played (else the server's own PREFS), as far as every player's build plays by them:
    // a build that said its rules only at check-in plays none that the host's screen didn't show.
    auto rules = StartGuaranteedMessage<MCMPLongMessage>(MCMPMessageType::MatchRules);
    rules.Value = static_cast<int32_t>(AgreedRules(Played.Rules.value_or(WantedRules())));
    PutMessage(rules);
    SessionManager->SendMessageToGroup(0, Outgoing(sizeof(MCMPLongMessage)));
    MatchRules = static_cast<uint32_t>(rules.Value);
    return 0;
}

int32_t MCMultiPlayer::SendMatchOptions()
{
    // What every player's build plays of the host's choices, so a build without the host's weather (one that never
    // said its rules, as the original's) keeps every player to the mission's own.
    Played = PlayedOptions();
    auto options = StartGuaranteedMessage<MCMPMatchOptionsMessage>(MCMPMessageType::MatchOptions);
    options.Weather =
        Played.Weather.has_value() ? static_cast<int8_t>(*Played.Weather) : MCMPMatchOptionsMessage::MissionOwn;
    options.TimeOfDay =
        Played.TimeOfDay.has_value() ? static_cast<int8_t>(*Played.TimeOfDay) : MCMPMatchOptionsMessage::MissionOwn;
    options.Rules = Played.Rules.value_or(0);
    PutMessage(options);
    SessionManager->SendMessageToGroup(0, Outgoing(sizeof(MCMPMatchOptionsMessage)));
    return 0;
}

void MCMultiPlayer::SendPlayersRules()
{
    // Every field set: the buffer holds the last message's bytes past the header.
    auto said = StartGuaranteedMessage<MCMPPlayersRulesMessage>(MCMPMessageType::PlayersRules);
    said.Count = 0;
    said.Players = {};
    std::map<uint32_t, uint32_t> players = PlayerRules;
    players[SessionManager->MyPlayer()->Id] = MCMatchRule::Supported;

    for (const auto& [id, rules] : players)
    {
        if (said.Count == said.Players.size())
        {
            break;
        }

        said.Players[said.Count++] = {.PlayerId = id, .Rules = rules};
    }

    PutMessage(said);
    SessionManager->SendMessageToGroup(0, Outgoing(sizeof(MCMPPlayersRulesMessage)));
}

void MCMultiPlayer::UpdateMatchOptions()
{
    if (IsHost && PlayedOptions() != Played)
    {
        SendMatchOptions();
    }
}

int32_t MCMultiPlayer::SendPlayerCheckIn()
{
    SendPlayerRules();
    auto checkIn = StartGuaranteedMessage<MCMPPlayerCheckInMessage>(MCMPMessageType::PlayerCheckIn);
    checkIn.CheckInId = static_cast<int8_t>(CheckInId);
    checkIn.HomeTeam = static_cast<int8_t>(HomeTeam);
    PutMessage(checkIn);

    if (!IsServer)
    {
        SessionManager->SendMessageToServerGuaranteed(Outgoing(sizeof(MCMPPlayerCheckInMessage)));
    }
    else
    {
        HandleAppPlayerCheckIn(SessionManager->MyPlayer()->Id, Built(sizeof(MCMPPlayerCheckInMessage)));
    }

    return 0;
}

int32_t MCMultiPlayer::SendPlayerSetup(uint32_t toID, uint32_t setupInnerSphereGroupID, uint32_t setupClanGroupID)
{
    auto setup = StartGuaranteedMessage<MCMPPlayerSetupMessage>(MCMPMessageType::PlayerSetup);
    setup.AllPlayerGroupID = AllPlayerGroupID;
    setup.ClanGroupID = setupClanGroupID;
    setup.InnerSphereGroupID = setupInnerSphereGroupID;
    PutMessage(setup);

    if (NumPlayers() > 1)
    {
        if (toID == 0)
        {
            SessionManager->SendMessageToGroup(0, Outgoing(sizeof(MCMPPlayerSetupMessage)));
        }
        else
        {
            SessionManager->SendMessageToPlayerGuaranteed(toID, Outgoing(sizeof(MCMPPlayerSetupMessage)), true);
        }
    }

    if (HandleOwnMessages)
    {
        HandleAppPlayerSetup(SessionManager->MyPlayer()->Id, MsgBuffer);
    }

    return 0;
}

int32_t MCMultiPlayer::SendStartPlanning()
{
    StartGuaranteedMessage(MCMPMessageType::StartPlanning);
    SessionManager->SendMessageToGroup(0, Outgoing(sizeof(MCFIGuaranteedMessageHeader)));

    if (HandleOwnMessages)
    {
        HandleAppStartPlanning(SessionManager->MyPlayer()->Id, Built(sizeof(MCFIGuaranteedMessageHeader)));
        PlayerCheckedIn.fill(0);
    }

    return 0;
}

int32_t MCMultiPlayer::SendReadyForBattle()
{
    auto ready = StartGuaranteedMessage<MCMPPlayerCheckInMessage>(MCMPMessageType::ReadyForBattle);
    ready.CheckInId = static_cast<int8_t>(CheckInId);
    ready.HomeTeam = static_cast<int8_t>(HomeTeam);
    PutMessage(ready);
    SessionManager->SendMessageToGroup(0, Outgoing(sizeof(MCMPPlayerCheckInMessage)));
    HandleAppReadyForBattle(SessionManager->MyPlayer()->Id, Built(sizeof(MCMPPlayerCheckInMessage)));
    return 0;
}

int32_t MCMultiPlayer::SendPrepareScenario()
{
    StartGuaranteedMessage(MCMPMessageType::PrepareScenario);
    SessionManager->SendMessageToGroup(0, Outgoing(sizeof(MCFIGuaranteedMessageHeader)));

    if (IsHost)
    {
        PlayerCheckedIn.fill(0);

        if (GlobalLog() != nullptr)
        {
            GlobalLog()->HandlePrepareScenarioMessage();
        }

        PrepareScenarioReceived = true;
    }

    return 0;
}

int32_t MCMultiPlayer::SendStartScenario(std::span<const int32_t> playerValues, std::string_view missionName)
{
    // Port: the rules first, so that every player plays the scenario by them from its start.
    SendMatchRules();
    constexpr size_t nameOffset = sizeof(MCMPStartScenarioMessage);
    auto start = StartGuaranteedMessage<MCMPStartScenarioMessage>(MCMPMessageType::StartScenario);
    const size_t numValues =
        std::min({static_cast<size_t>(std::max(NumPlayers(), 0)), playerValues.size(), start.PlayerValues.size()});

    for (size_t i = 0; i < numValues; i++)
    {
        start.PlayerValues[i] = playerValues[i];
    }

    for (int32_t i = 0; i < NumMovers; i++)
    {
        std::shared_ptr<const MCMechWarrior> pilot = LockObject(MoverRoster[i])->GetPilot();
        Assert(pilot != nullptr, 0, " sendStartScenario: no pilot ");
        start.MoverFlags[i] = pilot != nullptr && pilot->EscapesThruEjection != 0 ? 1 : 0;
    }

    PutMessage(start);

    missionName = missionName.substr(0, MsgBufferSize - nameOffset - 1);
    std::memcpy(MsgBuffer.data() + nameOffset, missionName.data(), missionName.size());
    MsgBuffer[nameOffset + missionName.size()] = 0;
    SessionManager->SendMessageToGroup(0, Outgoing(static_cast<uint32_t>(nameOffset + missionName.size() + 1)));

    if (HandleOwnMessages)
    {
        HandleAppStartScenario(SessionManager->MyPlayer()->Id, MsgBuffer);
    }

    PlayerCheckedIn.fill(0);
    return 0;
}

int32_t MCMultiPlayer::SendEndScenario(int32_t result)
{
    auto end = StartGuaranteedMessage<MCMPLongMessage>(MCMPMessageType::EndScenario);
    end.Value = result;
    PutMessage(end);
    SessionManager->SendMessageToGroup(0, Outgoing(sizeof(MCMPLongMessage)));

    if (HandleOwnMessages)
    {
        HandleAppEndScenario(SessionManager->MyPlayer()->Id, MsgBuffer);
    }

    return 0;
}

int32_t MCMultiPlayer::SendLights(int32_t checkInId, std::span<const std::shared_ptr<MCMover>> units, bool on)
{
    static_assert(MaxLightsParts == static_cast<size_t>(MaxLocalMovers));

    // Every field set: the buffer holds the last message's bytes past the header.
    auto message = StartGuaranteedMessage<MCMPLightsMessage>(MCMPMessageType::Lights);
    message.CheckInId = static_cast<int8_t>(checkInId);
    message.On = on ? 1 : 0;
    message.Count = 0;
    message.Parts = {};

    for (const std::shared_ptr<MCMover>& unit : units)
    {
        if (unit != nullptr && message.Count < message.Parts.size())
        {
            message.Parts[message.Count++] = static_cast<uint16_t>(unit->PartId);
        }
    }

    PutMessage(message);

    if (IsServer)
    {
        SessionManager->SendMessageToGroup(0, Outgoing(sizeof(MCMPLightsMessage)));
    }
    else
    {
        SessionManager->SendMessageToServerGuaranteed(Outgoing(sizeof(MCMPLightsMessage)));
    }

    return 0;
}

int32_t MCMultiPlayer::SendPlayerOrder(MCTacticalOrder& order, bool queued, std::span<const int32_t> moverParts,
                                       std::span<const std::weak_ptr<MCMoverGroup>> groups, bool fromGroup)
{
    auto message = StartGuaranteedMessage<MCMPPlayerOrderMessage>(MCMPMessageType::PlayerOrder);
    message.CheckInId = static_cast<int8_t>(CheckInId);

    // The client clears its own movers' order queues for a stop order; the server sends the orders themselves. The
    // cleared orders made on the way are the original's (each takes an order id of the pilot).
    for (const int32_t part : moverParts)
    {
        auto mover = std::static_pointer_cast<MCMover>(ObjectList()->FindObjectFromPart(part));

        if (mover == nullptr || mover == LockObject(order.Target))
        {
            continue;
        }

        order.SetGroupFlag(mover->NetPlayerId, true);

        if (!fromGroup)
        {
            MCTacticalOrder clearOrder;
            clearOrder.Reset();
            clearOrder.SetId(*mover->GetPilot());

            if (order.Code == MCTacticalOrderCode::Stop)
            {
                mover->GetPilot()->ClearTacOrderQueue();
            }
        }
    }

    if (order.Code == MCTacticalOrderCode::MoveToPoint || order.Code == MCTacticalOrderCode::JumpToPoint)
    {
        message.OrderParam1 = std::bit_cast<uint32_t>(order.MoveParams.WayPath.Points[0]);
        message.OrderParam2 = std::bit_cast<uint32_t>(order.MoveParams.WayPath.Points[1]);
    }

    order.Pack();
    message.PackedOrder = {order.Data[0], order.Data[1]};
    uint8_t flags = queued ? 1 : 0;

    if (fromGroup)
    {
        flags |= 0x20;
    }

    for (const std::weak_ptr<MCMoverGroup>& subject : groups)
    {
        const std::shared_ptr<MCMoverGroup> group = subject.lock();

        if (group == nullptr)
        {
            continue;
        }

        flags |= static_cast<uint8_t>(1 << (group->GetId() + 1));
        const std::vector<std::shared_ptr<MCMover>> groupMovers = group->GetMovers();
        const auto numGroupMovers = static_cast<int32_t>(groupMovers.size());

        for (int32_t j = 0; j < numGroupMovers && !fromGroup; j++)
        {
            MCTacticalOrder clearOrder;
            clearOrder.Reset();
            clearOrder.SetId(*groupMovers[j]->GetPilot());

            if (order.Code == MCTacticalOrderCode::Stop)
            {
                groupMovers[j]->GetPilot()->ClearTacOrderQueue();
            }
        }
    }

    message.Flags = flags;
    PutMessage(message);
    SessionManager->SendMessageToServerGuaranteed(Outgoing(sizeof(MCMPPlayerOrderMessage)));
    return 0;
}

int32_t MCMultiPlayer::SendPlayerMoverGroup(int32_t groupId, std::span<const std::shared_ptr<MCMover>> movers,
                                            int32_t pointIndex)
{
    if (pointIndex >= static_cast<int32_t>(movers.size()) || pointIndex < 0)
    {
        return 0;
    }

    auto message = StartGuaranteedMessage<MCMPPlayerMoverGroupMessage>(MCMPMessageType::PlayerMoverGroup);
    message.CheckInId = static_cast<int8_t>(CheckInId);
    message.GroupId = static_cast<int8_t>(groupId);
    uint16_t members = 0;

    for (const std::shared_ptr<MCMover>& mover : movers)
    {
        members |= static_cast<uint16_t>(1 << (mover->NetPlayerId & 0x1f));
    }

    message.Members = static_cast<uint16_t>(members << 4 | movers[pointIndex]->NetPlayerId);
    PutMessage(message);
    SessionManager->SendMessageToGroup(0, Outgoing(sizeof(MCMPPlayerMoverGroupMessage)));
    return 0;
}

int32_t MCMultiPlayer::SendPlayerArtillery(int32_t strikeType, const MCVector3D& location, int32_t seconds)
{
    auto message = StartGuaranteedMessage<MCMPPlayerArtilleryMessage>(MCMPMessageType::PlayerArtillery);
    MCArtilleryChunk chunk = EmptyArtilleryChunk();
    chunk.Build(CheckInId, strikeType, location, seconds);
    chunk.Pack();
    MCArtilleryChunk check = EmptyArtilleryChunk();
    check.Data = chunk.Data;
    check.Unpack();

    if (chunk.EqualTo(check) == 0)
    {
        Fatal(0, " MultiPlayer.sendPlayerArtillery: Artillery chunks don't match ");
    }

    message.TargetX = location.X;
    message.TargetY = location.Y;
    message.ArtilleryData = chunk.Data;
    PutMessage(message);
    SessionManager->SendMessageToGroup(0, Outgoing(sizeof(MCMPPlayerArtilleryMessage)));
    return 0;
}

int32_t MCMultiPlayer::SendMoverUpdate()
{
    // The header word, the update's number, each mover's move chunk, each one's status chunk, each pilot's order id.
    StartPlainMessage(MCMPMessageType::MoverUpdate);
    Put(MsgBuffer, 2, MoverUpdateSequence);
    MoverUpdateSequence++;
    const size_t statusOffset = 4 + static_cast<size_t>(NumMovers) * 4;
    const size_t orderOffset = statusOffset + static_cast<size_t>(NumMovers) * 4;

    for (int32_t i = 0; i < NumMovers; i++)
    {
        const std::shared_ptr<MCMover> mover = LockObject(MoverRoster[i]);
        Assert(mover != nullptr, 0, " SendMoverUpdate: No Mover ");
        mover->BuildMoveChunk();
        Put(MsgBuffer, 4 + static_cast<size_t>(i) * 4, mover->GetMoveChunk().Data);
    }

    for (int32_t i = 0; i < NumMovers; i++)
    {
        const std::shared_ptr<MCMover> mover = LockObject(MoverRoster[i]);
        mover->BuildStatusChunk();
        Put(MsgBuffer, statusOffset + static_cast<size_t>(i) * 4, mover->GetStatusChunk().Data);
    }

    for (int32_t i = 0; i < NumMovers; i++)
    {
        MsgBuffer[orderOffset + i] = static_cast<uint8_t>(LockObject(MoverRoster[i])->GetPilot()->CurTacOrder.Id);
    }

    const auto size = static_cast<uint32_t>(orderOffset + NumMovers);
    Assert(size < MsgBufferSize, size, " sendMoverUpdate: msgSz too large! ");
    SessionManager->BroadcastMessage(Outgoing(size));
    return 0;
}

int32_t MCMultiPlayer::SendTurretUpdate()
{
    if (NumTurrets == 0)
    {
        return 0;
    }

    StartPlainMessage(MCMPMessageType::TurretUpdate);
    Put(MsgBuffer, 2, TurretUpdateSequence);
    TurretUpdateSequence++;

    for (int32_t i = 0; i < NumTurrets; i++)
    {
        const std::shared_ptr<MCTurret> turret = LockObject(TurretRoster[i]);
        Assert(turret != nullptr, 0, " SendTurretUpdate: No Turret ");
        uint8_t& target = MsgBuffer[4 + static_cast<size_t>(i)];

        if (turret->GetAwake() == 0 || LockObject(turret->Target) == nullptr)
        {
            target = 0xff;
        }
        else if (IsMoverClass(LockObject(turret->Target)->ObjectClass))
        {
            target =
                static_cast<uint8_t>(std::static_pointer_cast<MCMover>(LockObject(turret->Target))->NetRosterIndex);
        }
        else
        {
            // Original behaviour (OB-106): a building's part id less 0x48, which the receivers read as a mover's
            // roster index or as no target.
            target = static_cast<uint8_t>(LockObject(turret->Target)->PartId - 0x48);
        }
    }

    const auto size = static_cast<uint32_t>(NumTurrets + 4);
    Assert(size < MsgBufferSize, size, " sendTurretUpdate: msgSz too large! ");
    SessionManager->BroadcastMessage(Outgoing(size));
    return 0;
}

int32_t MCMultiPlayer::SendMoverWeaponFireUpdate()
{
    // The header, the first mover (0), the number of movers, each mover's chunk count, then the chunks: as many
    // messages as it takes, each holding at most 0x77 chunks.
    constexpr size_t chunkOffset = 0x22;

    while (true)
    {
        StartGuaranteedMessage(MCMPMessageType::MoverWeaponFireUpdate);
        MsgBuffer[8] = 0;
        MsgBuffer[9] = static_cast<uint8_t>(NumMovers);
        std::fill_n(MsgBuffer.begin() + 10, NumMovers, uint8_t{0});
        int32_t numChunks = 0;

        for (int32_t i = 0; i < NumMovers; i++)
        {
            std::vector<uint32_t> chunks(static_cast<size_t>(MoverFireChunksPerMessage - numChunks));
            const int32_t grabbed = LockObject(MoverRoster[i])->GrabWeaponFireChunks(0, chunks);
            std::memcpy(MsgBuffer.data() + chunkOffset + static_cast<size_t>(numChunks) * 4, chunks.data(),
                        static_cast<size_t>(grabbed) * 4);
            MsgBuffer[10 + static_cast<size_t>(i)] = static_cast<uint8_t>(grabbed);
            numChunks += grabbed;

            if (numChunks == MoverFireChunksPerMessage)
            {
                break;
            }
        }

        if (numChunks < 1)
        {
            return 0;
        }

        const auto size = static_cast<uint32_t>(numChunks * 4 + chunkOffset);
        Assert(size < 0x200, size, " sendMoverWeaponFireUpdate: msgSz too large! ");
        SessionManager->SendMessageToGroup(0, Outgoing(size));
    }
}

int32_t MCMultiPlayer::SendTurretWeaponFireUpdate()
{
    // The header, the number of firing turrets, a byte per turret (its roster index * 4 + its chunk count), the chunks.
    StartGuaranteedMessage(MCMPMessageType::TurretWeaponFireUpdate);
    const auto firing = std::span(TurretRoster).first(static_cast<size_t>(NumTurrets));
    const auto numFiring =
        static_cast<int32_t>(std::ranges::count_if(firing, [](const std::weak_ptr<MCTurret>& turret)
                                                   { return !LockObject(turret)->WeaponFireChunks[0].empty(); }));

    if (numFiring == 0)
    {
        return 0;
    }

    MsgBuffer[8] = static_cast<uint8_t>(numFiring);
    int32_t entry = 0;
    int32_t numChunks = 0;

    for (const std::weak_ptr<MCTurret>& slot : firing)
    {
        const std::shared_ptr<MCTurret> turret = LockObject(slot);

        if (turret->WeaponFireChunks[0].empty())
        {
            continue;
        }

        const size_t offset = static_cast<size_t>(numChunks * 4 + numFiring + 9);
        std::vector<uint32_t> chunks((MsgBufferSize - offset) / 4);
        const int32_t turretChunks = turret->GrabWeaponFireChunks(0, chunks);
        std::memcpy(MsgBuffer.data() + offset, chunks.data(), static_cast<size_t>(turretChunks) * 4);
        numChunks += turretChunks;
        MsgBuffer[9 + static_cast<size_t>(entry)] = static_cast<uint8_t>(turretChunks + turret->NetRosterIndex * 4);
        entry++;
        turret->ClearWeaponFireChunks(0);
    }

    if (numChunks > 0)
    {
        const auto size = static_cast<uint32_t>(entry + 9 + numChunks * 4);
        Assert(size < MsgBufferSize, size, " sendTurretWeaponFireUpdate: msgSz too large! ");
        SessionManager->SendMessageToGroup(0, Outgoing(size));
    }

    return 0;
}

int32_t MCMultiPlayer::SendMoverCriticalHitUpdate()
{
    // The header, the first mover (0), the number of movers, a critical-hit count and a radio count per mover (24
    // each), then the bytes.
    constexpr size_t radioCounts = 0x22;
    constexpr size_t chunkOffset = 0x3a;
    StartGuaranteedMessage(MCMPMessageType::MoverCriticalHitUpdate);
    MsgBuffer[8] = 0;
    MsgBuffer[9] = static_cast<uint8_t>(NumMovers);
    int32_t numBytes = 0;

    for (int32_t i = 0; i < static_cast<int8_t>(MsgBuffer[9]); i++)
    {
        const std::shared_ptr<MCMover> mover = LockObject(MoverRoster[static_cast<int8_t>(MsgBuffer[8]) + i]);
        const auto numCriticalHits = static_cast<uint32_t>(
            mover->GrabCriticalHitChunks(0, std::span(MsgBuffer).subspan(numBytes + chunkOffset)));
        MsgBuffer[10 + static_cast<size_t>(i)] = static_cast<uint8_t>(numCriticalHits);
        Assert(numCriticalHits < 0x81, numCriticalHits, " sendMoverCritHits: bad numCH ");
        const auto numRadio = static_cast<uint32_t>(
            mover->GrabRadioChunks(0, std::span(MsgBuffer).subspan(numBytes + numCriticalHits + chunkOffset)));
        MsgBuffer[radioCounts + static_cast<size_t>(i)] = static_cast<uint8_t>(numRadio);
        Assert(numRadio < 8, numRadio, " sendMoverCritHits: bad numRDO ");
        numBytes += static_cast<int32_t>(numCriticalHits + numRadio);
        mover->ClearCriticalHitChunks(0);
        mover->ClearRadioChunks(0);
    }

    if (numBytes > 0)
    {
        const auto size = static_cast<uint32_t>(numBytes + chunkOffset);
        Assert(size < MsgBufferSize, size, " sendMoverCriticalHitUpdate: msgSz too large! ");
        SessionManager->SendMessageToGroup(0, Outgoing(size));
    }

    return 0;
}

int32_t MCMultiPlayer::SendWeaponHitUpdate()
{
    // The header, the number of chunks, the chunks: at most 0x7d to a message.
    while (!WeaponHitChunks.empty())
    {
        StartGuaranteedMessage(MCMPMessageType::WeaponHitUpdate);
        std::vector<uint32_t> chunks(std::min(WeaponHitChunks.size(), HitChunksPerMessage));
        const size_t numChunks = GrabWeaponHitChunks(chunks);
        MsgBuffer[8] = static_cast<uint8_t>(numChunks);
        std::memcpy(MsgBuffer.data() + 9, chunks.data(), numChunks * 4);
        const auto size = static_cast<uint32_t>(MsgBuffer[8] * 4 + 9);
        Assert(size < 0x200, size, " sendWeaponHitUpdate: msgSz too large! ");
        SessionManager->SendMessageToGroup(0, Outgoing(size));
    }

    return 0;
}

int32_t MCMultiPlayer::SendWorldStateUpdate()
{
    // The header, the number of chunks (a byte), a byte the receivers skip, the chunks: all the queued ones.
    constexpr size_t chunkOffset = 10;

    if (WorldStateChunks.empty())
    {
        return 0;
    }

    StartGuaranteedMessage(MCMPMessageType::WorldStateUpdate);
    MsgBuffer[8] = static_cast<uint8_t>(WorldStateChunks.size());
    const size_t numChunks = std::min(WorldStateChunks.size(), (MsgBufferSize - chunkOffset) / 4);
    std::memcpy(MsgBuffer.data() + chunkOffset, WorldStateChunks.data(), numChunks * 4);
    WorldStateChunks.clear();
    const auto size = static_cast<uint32_t>(MsgBuffer[8] * 4 + chunkOffset);

    if (size > 0x1ff)
    {
        const std::string text = std::format(" sendWorldStateUpdate: msgSz too large! [{},{},{},{},{},{},{},{},{},{}] ",
                                             WorldStateChunkTally[0], WorldStateChunkTally[1], WorldStateChunkTally[2],
                                             WorldStateChunkTally[3], WorldStateChunkTally[4], WorldStateChunkTally[5],
                                             WorldStateChunkTally[6], WorldStateChunkTally[7], WorldStateChunkTally[8],
                                             WorldStateChunkTally[9]);
        DebugMissionScriptMessages();
        Assert(false, size, text);
    }

    SessionManager->SendMessageToGroup(0, Outgoing(size));
    WorldStateChunkTally.fill(0);

    if (const std::shared_ptr<MCAblRuntime> abl = AblRuntime())
    {
        abl->MissionScriptMessages.clear();
    }

    return 0;
}

int32_t MCMultiPlayer::SendFileInquiry(std::string_view fileName)
{
    AskedFileHash = MCMissionDataHash(fileName);
    StartGuaranteedMessage(MCMPMessageType::FileInquiry);
    fileName = fileName.substr(0, MsgBufferSize - sizeof(MCFIGuaranteedMessageHeader) - 1);
    std::memcpy(MsgBuffer.data() + sizeof(MCFIGuaranteedMessageHeader), fileName.data(), fileName.size());
    MsgBuffer[sizeof(MCFIGuaranteedMessageHeader) + fileName.size()] = 0;
    SessionManager->SendMessageToGroup(
        AllPlayerGroupID, Outgoing(static_cast<uint32_t>(fileName.size() + sizeof(MCFIGuaranteedMessageHeader) + 1)));
    return 0;
}

int32_t MCMultiPlayer::UpdateClients()
{
    if (!InMission)
    {
        return 0;
    }

    if (NextWorldStateUpdateTime < ScenarioTime)
    {
        SendWorldStateUpdate();
        SendMoverWeaponFireUpdate();
        SendTurretWeaponFireUpdate();
        SendWeaponHitUpdate();
        SendMoverCriticalHitUpdate();
        NextWorldStateUpdateTime += WorldStateUpdateFrequency;
    }

    if (NextMoverUpdateTime < ScenarioTime)
    {
        SendMoverUpdate();
        NextMoverUpdateTime += MoverUpdateFrequency;
    }

    if (NextTurretUpdateTime < ScenarioTime)
    {
        SendTurretUpdate();
        NextTurretUpdateTime += TurretUpdateFrequency;
    }

    return 0;
}

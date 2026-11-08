#include "stdafx.h"
#include "network/MCMechHandover.h"
#include "iface/MCInterfaceTypes.h"
#include "iface/MCMechBar.h"
#include "iface/MCTacticalInterface.h"
#include "linkup/MCFidpPlayer.h"
#include "network/MCMultiPlayer.h"
#include "object/MCForces.h"
#include "object/MCMover.h"

namespace
{
    /// <summary>Whether <paramref name="player"/> is a player number a roster has.</summary>
    bool IsPlayerNumber(int32_t player)
    {
        return player >= 0 && player < MaxLinkupPlayers;
    }

    /// <summary>Whether <paramref name="slot"/> is a slot of a player's roster.</summary>
    bool IsSlot(int32_t slot)
    {
        return slot >= 0 && slot < MCMultiPlayer::MaxLocalMovers;
    }

    /// <summary>Whether player <paramref name="playerId"/>'s build takes a handover, as its rules said.</summary>
    bool TakesUnits(const MCMultiPlayer& game, uint32_t playerId)
    {
        const auto said = game.PlayerRules.find(playerId);
        return said != game.PlayerRules.end() && (said->second & MCMatchRule::MechHandover) != 0;
    }

    /// <summary>
    /// The receiver's game: <paramref name="unit"/>, now in its player's roster at <paramref name="slot"/>, becomes its
    /// own, as the scenario's load makes a player's own: its local mover there and a button on the mech bar.
    /// </summary>
    void MakeOwn(MCMultiPlayer& game, const std::shared_ptr<MCMover>& unit, int32_t slot)
    {
        unit->NetPlayerId = slot;
        game.LocalMovers[static_cast<size_t>(slot)] = unit;
        game.NumLocalMovers = std::max(game.NumLocalMovers, slot + 1);

        if (const std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
            iface != nullptr && iface->MechBar != nullptr && iface->MechBar->GetButtonFromID(unit->PartId) == nullptr)
        {
            iface->AddMech(unit->PartId, NoLance, unit->GetAwake() != 0, true);
        }
    }
}

namespace MCMechHandover
{
    std::vector<MCHanded> Plan(std::span<const int32_t> parts, std::span<const MCTaker> takers)
    {
        std::vector<size_t> order(takers.size());
        std::iota(order.begin(), order.end(), size_t{0});
        std::ranges::sort(order, {},
                          [&takers](size_t taker) { return std::pair(takers[taker].Units, takers[taker].Player); });

        std::vector<MCHanded> plan;
        size_t next = 0;

        for (const size_t taker : order)
        {
            for (const int32_t slot : takers[taker].FreeSlots)
            {
                if (next == parts.size())
                {
                    return plan;
                }

                plan.push_back({.Part = parts[next++], .Player = takers[taker].Player, .Slot = slot});
            }
        }

        return plan;
    }

    std::vector<MCTaker> Takers(const MCMultiPlayer& game, int32_t leaver)
    {
        std::vector<MCTaker> takers;
        const std::shared_ptr<MCCommander> leaving = CommanderById(leaver);
        const std::shared_ptr<MCTeam> team = leaving != nullptr ? leaving->GetTeam() : nullptr;

        if (team == nullptr || game.SessionManager == nullptr)
        {
            return takers;
        }

        const std::shared_ptr<MCFidpPlayer> me = game.SessionManager->MyPlayer();

        for (const std::shared_ptr<MCFidpPlayer>& player : game.SessionManager->GetPlayers(nullptr))
        {
            const int32_t number = player->PlayerNumber;

            if (!player->HasPlayerNumber || !IsPlayerNumber(number) || number == leaver)
            {
                continue;
            }

            if (const std::shared_ptr<MCCommander> commander = CommanderById(number);
                commander == nullptr || commander->GetTeam() != team)
            {
                continue;
            }

            if (player != me && !TakesUnits(game, player->Id))
            {
                SDL_Log("Mech handover: player %d (%s) can't take player %d's units: its game lacks the rule", number,
                        player->Name.c_str(), leaver);
                continue;
            }

            MCTaker& taker = takers.emplace_back(MCTaker{.Player = number});

            for (int32_t slot = 0; slot < MCMultiPlayer::MaxLocalMovers; slot++)
            {
                const std::shared_ptr<MCMover> unit =
                    LockObject(game.PlayerMoverRoster[static_cast<size_t>(number)][static_cast<size_t>(slot)]);

                if (unit == nullptr)
                {
                    taker.FreeSlots.push_back(slot);
                }
                else if (unit->IsDisabled() == 0)
                {
                    taker.Units++;
                }
            }
        }

        return takers;
    }

    std::vector<int32_t> UnitsToHand(const MCMultiPlayer& game, int32_t player)
    {
        std::vector<int32_t> parts;

        if (!IsPlayerNumber(player))
        {
            return parts;
        }

        for (const std::weak_ptr<MCMover>& slot : game.PlayerMoverRoster[static_cast<size_t>(player)])
        {
            if (const std::shared_ptr<MCMover> unit = LockObject(slot); unit != nullptr && unit->IsDisabled() == 0)
            {
                parts.push_back(unit->PartId);
            }
        }

        return parts;
    }

    std::vector<MCHanded> HandOn(MCMultiPlayer& game, int32_t leaver)
    {
        const std::vector<int32_t> parts = UnitsToHand(game, leaver);

        if (parts.empty())
        {
            return {};
        }

        const std::vector<MCTaker> takers = Takers(game, leaver);
        std::vector<MCHanded> plan = Plan(parts, takers);
        SDL_Log("Mech handover: player %d left with %zu units; %zu handed on to %zu teammates able", leaver,
                parts.size(), plan.size(), takers.size());

        if (plan.empty())
        {
            return plan;
        }

        Send(game, leaver, plan);
        Apply(game, leaver, plan);
        return plan;
    }

    void Send(MCMultiPlayer& game, int32_t leaver, std::span<const MCHanded> units)
    {
        // Every field set: the buffer holds the last message's bytes past the header.
        auto message = game.StartGuaranteedMessage<MCMPHandoverMessage>(MCMPMessageType::Handover);
        message.Leaver = static_cast<int8_t>(leaver);
        message.Count = 0;
        message.Units = {};

        for (const MCHanded& unit : units.first(std::min(units.size(), MaxHandoverUnits)))
        {
            message.Units[message.Count++] = {.Part = static_cast<uint16_t>(unit.Part),
                                              .Player = static_cast<int8_t>(unit.Player),
                                              .Slot = static_cast<int8_t>(unit.Slot)};
        }

        game.PutMessage(message);
        game.SessionManager->SendMessageToGroup(0, game.Outgoing(sizeof(MCMPHandoverMessage)));
    }

    int32_t Apply(MCMultiPlayer& game, int32_t leaver, std::span<const MCHanded> units)
    {
        if (!IsPlayerNumber(leaver))
        {
            return 0;
        }

        std::array<std::weak_ptr<MCMover>, MCMultiPlayer::MaxLocalMovers>& from =
            game.PlayerMoverRoster[static_cast<size_t>(leaver)];
        int32_t moved = 0;
        bool madeOwn = false;

        for (const MCHanded& unit : units)
        {
            if (!IsPlayerNumber(unit.Player) || unit.Player == leaver || !IsSlot(unit.Slot))
            {
                SDL_Log("Mech handover: skipped unit %d of player %d's: no slot %d of player %d", unit.Part, leaver,
                        unit.Slot, unit.Player);
                continue;
            }

            const auto held =
                std::ranges::find_if(from, [&unit](const std::weak_ptr<MCMover>& slot)
                                     { return LockObject(slot) != nullptr && LockObject(slot)->PartId == unit.Part; });
            std::weak_ptr<MCMover>& to =
                game.PlayerMoverRoster[static_cast<size_t>(unit.Player)][static_cast<size_t>(unit.Slot)];

            if (held == from.end() || LockObject(to) != nullptr)
            {
                SDL_Log("Mech handover: skipped unit %d of player %d's for player %d's slot %d: %s", unit.Part, leaver,
                        unit.Player, unit.Slot,
                        held == from.end() ? "not in the player's roster" : "the slot is taken");
                continue;
            }

            const std::shared_ptr<MCMover> mover = LockObject(*held);
            held->reset();
            to = mover;
            moved++;

            if (unit.Player == game.CheckInId)
            {
                MakeOwn(game, mover, unit.Slot);
                madeOwn = true;
            }
        }

        // The new buttons dance into place after the player's own.
        if (const std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
            madeOwn && iface != nullptr && iface->MechBar != nullptr)
        {
            iface->MechBar->PlaceButtons(true);
        }

        return moved;
    }

    bool Commands(const MCMultiPlayer& game, int32_t player, const MCMover& unit)
    {
        return IsPlayerNumber(player) && std::ranges::any_of(game.PlayerMoverRoster[static_cast<size_t>(player)],
                                                             [&unit](const std::weak_ptr<MCMover>& slot)
                                                             { return LockObject(slot).get() == &unit; });
    }
}

#include "stdafx.h"
#include "logistics/MCDeploy.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCGameStrings.h"
#include "main/MCLogMech.h"
#include "main/MCLogMechList.h"
#include "main/MCLogVehicle.h"
#include "main/MCLogVehicleList.h"
#include "main/MCLogWarrior.h"
#include "main/MCLogWarriorList.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"
#include "object/MCMasterComponent.h"
#include "platform/MCInput.h"
#include "sprite/MCHdArt.h"

int32_t CurDeployTonnage = 0;
int32_t MaxDeployTonnage = 0;

namespace
{
    /// <summary>The keys held with the right button to send a unit to group 1, 2 or 3, and the held bit of their state.</summary>
    constexpr std::array<int32_t, MCDeploy::Groups> GroupKeys = {'1', '2', '3'};
    constexpr int32_t KeyHeld = 0x8000;
    /// <summary>The original's strings: a unit too heavy, a damaged engine, a destroyed location, the units' limit.</summary>
    constexpr uint32_t TooHeavyString = 0x4c;
    constexpr uint32_t EngineDamagedString = 0x35d;
    constexpr uint32_t LocationDestroyedString = 0x364;
    constexpr uint32_t TooManyUnitsString = 0x373;
    /// <summary>A match's "ready" chat line (string 0x379, the player's name in it) and the original's buffer for it.</summary>
    constexpr uint32_t ReadyString = 0x379;
    constexpr uint32_t ReadyStringLength = 199;
    constexpr size_t ReadyChatMost = 303;
    /// <summary>The strings' buffer the original read them into.</summary>
    constexpr uint32_t StringLength = 0xfe;

    uint64_t TheRevision = 0;
    bool TheLock = false;

    /// <summary>
    /// A unit of the drop kept for a loss's RETRY (MCDeploy::Keep), by what the mission's start file gives back, its
    /// place in the force's lists aside (a load puts each unit it reads at its list's front): a 'Mech by its name and its
    /// pilot's (a pilot is in one 'Mech), a vehicle by its name, armour and structure (two alike are the same to the drop).
    /// </summary>
    struct MCKeptUnit
    {
        bool Mech = false;
        std::string Key;
    };

    /// <summary>The drop kept for RETRY: its mission, and each slot's unit.</summary>
    struct MCKeptDrop
    {
        int32_t Mission = -1;
        std::array<std::optional<MCKeptUnit>, MCDeploy::Slots> Slots{};
    };

    std::optional<MCKeptDrop> TheKeptDrop;

    std::string MechKey(const MCLogistics& logistics, const MCLogMech& mech)
    {
        std::shared_ptr<MCLogWarrior> pilot;
        logistics.AssignedWarriorList->GetWarriorInfo(mech.PilotIndex, pilot);
        return pilot != nullptr ? std::format("{}|{}|{}", mech.FileName, pilot->Callsign, pilot->Name) : mech.FileName;
    }

    std::string VehicleKey(const MCLogVehicle& vehicle)
    {
        std::string key = vehicle.FileName;

        for (size_t at = 0; at < MCLogVehicle::NumLocations; at++)
        {
            key += std::format("|{},{}", vehicle.CurArmorPoints[at], vehicle.CurInternalStructure[at]);
        }

        return key;
    }

    /// <summary>The first of <paramref name="parts"/> not in the drop whose key (<paramref name="keyOf"/>) is <paramref name="key"/>.</summary>
    template <typename Part, typename KeyOf>
    std::shared_ptr<Part> Waiting(const std::vector<std::shared_ptr<Part>>& parts, const std::string& key, KeyOf keyOf)
    {
        const auto found = std::ranges::find_if(parts, [&](const std::shared_ptr<Part>& part)
                                                { return !part->Deployed && keyOf(*part) == key; });
        return found != parts.end() ? *found : nullptr;
    }

    MCLogistics& Logistics()
    {
        return *GlobalLog();
    }

    MCLogistics::DeploySlot& DeploySlotOf(size_t slot)
    {
        return Logistics().DeploySlots[slot / MCDeploy::GroupSlots][slot % MCDeploy::GroupSlots];
    }

    /// <summary>The drop's tons with <paramref name="part"/>'s added (or, with <paramref name="sign"/> -1, taken off).</summary>
    int32_t TonsWith(const MCLogPart& part, float sign)
    {
        return static_cast<int32_t>(static_cast<float>(CurDeployTonnage) + sign * part.CurTonnage);
    }

    /// <summary>
    /// Adds up the tons of the units in <paramref name="group"/>'s slots (the original's running <c>__ftol</c> of an int
    /// plus the float tons).
    /// </summary>
    int32_t SlotsTons(size_t group)
    {
        int32_t tons = 0;

        for (const MCLogistics::DeploySlot& slot : Logistics().DeploySlots[group])
        {
            if (slot.Unit >= 0)
            {
                std::shared_ptr<MCLogMech> mech;
                Logistics().ForceMechList->GetMechInfo(slot.Unit, mech);
                tons = static_cast<int32_t>(static_cast<float>(tons) + mech->CurTonnage);
            }
            else if (slot.Vehicle >= 0)
            {
                std::shared_ptr<MCLogVehicle> vehicle;
                Logistics().ForceVehicleList->GetVehicleInfo(slot.Vehicle, vehicle);
                tons = static_cast<int32_t>(static_cast<float>(tons) + vehicle->CurTonnage);
            }
        }

        return tons;
    }

    /// <summary>The mech <paramref name="part"/> is, or null for a vehicle (the original's PartType 1).</summary>
    std::shared_ptr<MCLogMech> AsMech(MCLogPart& part)
    {
        return part.PartType == 1 ? std::static_pointer_cast<MCLogMech>(part.shared_from_this()) : nullptr;
    }

    std::shared_ptr<MCLogVehicle> AsVehicle(MCLogPart& part)
    {
        return part.PartType == 1 ? nullptr : std::static_pointer_cast<MCLogVehicle>(part.shared_from_this());
    }

    /// <summary>
    /// The original's PlaceInEmptySlot: <paramref name="part"/> into the empty slot <paramref name="slot"/> when it fits
    /// (or the limit is lifted); false when it doesn't.
    /// </summary>
    bool PlaceInEmpty(MCLogPart& part, size_t slot)
    {
        MCLogistics& logistics = Logistics();
        MCLogistics::DeploySlot& deploy = DeploySlotOf(slot);
        const auto group = static_cast<int32_t>(slot / MCDeploy::GroupSlots);
        const auto place = static_cast<int32_t>(slot % MCDeploy::GroupSlots);

        if (!MCDeploy::Fits(part) && MCDeploy::LimitHolds())
        {
            return false;
        }

        if (const std::shared_ptr<MCLogMech> mech = AsMech(part); mech != nullptr)
        {
            logistics.SendAddMechMessage(*mech, group, place);
            deploy.Unit = logistics.ForceMechList->GetMechIndex(mech);
            CurDeployTonnage = TonsWith(*mech, 1.0f);
            mech->Deployed = true;
            logistics.AssignedWarriorList->SetDeployed(mech->PilotIndex, true);
            // Port-only (SPEC-memory.md's "From the briefing"): its HD art read from now, behind the briefing.
            MCHdArt::HoldMechAhead(mech->HdFolders);
            return true;
        }

        const std::shared_ptr<MCLogVehicle> vehicle = AsVehicle(part);
        logistics.SendAddVehicleMessage(*vehicle, group, place);
        deploy.Vehicle = logistics.ForceVehicleList->GetVehicleIndex(vehicle);
        CurDeployTonnage = TonsWith(*vehicle, 1.0f);
        vehicle->Deployed = true;
        return true;
    }

    /// <summary>
    /// The original's drop onto a filled slot: the unit there leaves the drop, then <paramref name="part"/> goes in when
    /// it fits what is free (the limit's lifting not asked, as the original's); false when it doesn't.
    /// </summary>
    bool PlaceOverOccupant(MCLogPart& part, size_t slot)
    {
        MCLogistics& logistics = Logistics();
        MCLogistics::DeploySlot& deploy = DeploySlotOf(slot);
        const auto group = static_cast<int32_t>(slot / MCDeploy::GroupSlots);
        const auto place = static_cast<int32_t>(slot % MCDeploy::GroupSlots);
        const std::shared_ptr<MCLogMech> mech = AsMech(part);
        const std::shared_ptr<MCLogVehicle> vehicle = AsVehicle(part);
        // The occupant's kind sets one difference: a mech put in a mech's place has its pilot left unmarked (OB-166).
        const bool mechWasThere = deploy.Unit > -1;

        if (mechWasThere)
        {
            // A mech was there: it leaves the force's drop. (Original behaviour (OB-166): its pilot stays marked
            // deployed, and a mech put in its place isn't marked.)
            std::shared_ptr<MCLogMech> occupant;
            logistics.ForceMechList->GetMechInfo(deploy.Unit, occupant);
            CurDeployTonnage = TonsWith(*occupant, -1.0f);
            occupant->Deployed = false;
            deploy.Unit = -1;
            // Port-only (SPEC-memory.md's "The GPU from the briefing"): the mission's no more.
            MCHdArt::MechTakenOff(occupant->HdFolders);
        }
        else
        {
            std::shared_ptr<MCLogVehicle> occupant;
            logistics.ForceVehicleList->GetVehicleInfo(deploy.Vehicle, occupant);
            CurDeployTonnage = TonsWith(*occupant, -1.0f);
            occupant->Deployed = false;
            deploy.Vehicle = -1;
        }

        if (!MCDeploy::Fits(part))
        {
            return false;
        }

        if (mech != nullptr)
        {
            logistics.SendAddMechMessage(*mech, group, place);
            deploy.Vehicle = -1;
            deploy.Unit = logistics.ForceMechList->GetMechIndex(mech);
            CurDeployTonnage = TonsWith(*mech, 1.0f);
            mech->Deployed = true;

            if (!mechWasThere)
            {
                logistics.AssignedWarriorList->SetDeployed(mech->PilotIndex, true);
            }

            // Port-only (SPEC-memory.md's "From the briefing"): as PlaceInEmpty's.
            MCHdArt::HoldMechAhead(mech->HdFolders);
            return true;
        }

        logistics.SendAddVehicleMessage(*vehicle, group, place);
        deploy.Unit = -1;
        deploy.Vehicle = logistics.ForceVehicleList->GetVehicleIndex(vehicle);
        CurDeployTonnage = TonsWith(*vehicle, 1.0f);
        vehicle->Deployed = true;
        return true;
    }
}

auto MCDeploy::ReadyUnits() -> std::vector<std::shared_ptr<MCLogPart>>
{
    std::vector<std::shared_ptr<MCLogPart>> units;
    const MCLogistics& logistics = Logistics();

    for (const std::shared_ptr<MCLogMech>& mech : logistics.ForceMechList->Mechs)
    {
        if (mech->PilotIndex >= 0 && !mech->Deployed)
        {
            units.push_back(mech);
        }
    }

    for (const std::shared_ptr<MCLogVehicle>& vehicle : logistics.ForceVehicleList->Vehicles)
    {
        if (vehicle->Deployed == 0)
        {
            units.push_back(vehicle);
        }
    }

    return units;
}

auto MCDeploy::SlotUnit(size_t slot) -> std::shared_ptr<MCLogPart>
{
    const MCLogistics& logistics = Logistics();
    const MCLogistics::DeploySlot& deploy = logistics.DeploySlots[slot / GroupSlots][slot % GroupSlots];

    if (deploy.Unit >= 0)
    {
        std::shared_ptr<MCLogMech> mech;
        logistics.ForceMechList->GetMechInfo(deploy.Unit, mech);
        return mech;
    }

    if (deploy.Vehicle >= 0)
    {
        std::shared_ptr<MCLogVehicle> vehicle;
        logistics.ForceVehicleList->GetVehicleInfo(deploy.Vehicle, vehicle);
        return vehicle;
    }

    if (MultiPlayer() != nullptr)
    {
        return logistics.DropSlots[slot / GroupSlots][slot % GroupSlots].Part.lock();
    }

    return nullptr;
}

auto MCDeploy::Open(size_t slot) -> bool
{
    return slot < Slots && Logistics().LocalDropSlot[slot];
}

auto MCDeploy::GroupShown(size_t group) -> bool
{
    const std::array<bool, MCLogistics::NumDropSlots>& open = Logistics().LocalDropSlot;
    const bool match = MultiPlayer() != nullptr;
    return group == 0 || (group == 1 && (match || open[GroupSlots])) ||
           (group == 2 && (match || (open[GroupSlots] && open[2 * GroupSlots])));
}

auto MCDeploy::SlotOf(const MCLogPart& part) -> std::optional<size_t>
{
    for (size_t slot = 0; slot < Slots; slot++)
    {
        const MCLogistics::DeploySlot& deploy = DeploySlotOf(slot);

        if (deploy.Unit < 0 && deploy.Vehicle < 0)
        {
            continue;
        }

        if (const std::shared_ptr<MCLogPart> unit = SlotUnit(slot); unit.get() == &part)
        {
            return slot;
        }
    }

    return std::nullopt;
}

auto MCDeploy::GroupTons(size_t group) -> int32_t
{
    if (MultiPlayer() == nullptr)
    {
        return SlotsTons(group);
    }

    // Multiplayer: a group counts once one of its slots holds a unit (the second always with two players).
    bool occupied = group == 1 && static_cast<int32_t>(MultiPlayer()->PlayersOnHomeTeam()->size()) == 2;

    for (size_t slot = 0; slot < GroupSlots && !occupied; slot++)
    {
        const MCLogistics::DeploySlot& deploy = Logistics().DeploySlots[group][slot];

        if (deploy.Unit >= 0)
        {
            std::shared_ptr<MCLogMech> mech;
            Logistics().ForceMechList->GetMechInfo(deploy.Unit, mech);
            occupied = mech != nullptr;
        }
        else if (deploy.Vehicle >= 0)
        {
            // Original behaviour (OB-082): the vehicle looked up is the first group's, whatever the group.
            std::shared_ptr<MCLogVehicle> vehicle;
            Logistics().ForceVehicleList->GetVehicleInfo(Logistics().DeploySlots[0][slot].Vehicle, vehicle);
            occupied = vehicle != nullptr;
        }
    }

    return occupied ? SlotsTons(group) : 0;
}

auto MCDeploy::CountTons() -> void
{
    TheRevision++;

    if (MultiPlayer() != nullptr)
    {
        // A group counts once one of its slots holds a unit (GroupTons).
        CurDeployTonnage = 0;

        for (size_t group = 0; group < Groups; group++)
        {
            CurDeployTonnage += GroupTons(group);
        }

        return;
    }

    CurDeployTonnage = GroupTons(0);

    if (Logistics().LocalDropSlot[GroupSlots])
    {
        CurDeployTonnage += GroupTons(1);

        if (Logistics().LocalDropSlot[2 * GroupSlots])
        {
            CurDeployTonnage += GroupTons(2);
        }
    }
}

auto MCDeploy::Fits(const MCLogPart& part) -> bool
{
    return !(static_cast<float>(MaxDeployTonnage) < static_cast<float>(CurDeployTonnage) + part.CurTonnage);
}

auto MCDeploy::LimitHolds() -> bool
{
    return !Logistics().HammerDown;
}

auto MCDeploy::CantDrop(const MCLogPart& part) -> MCRefusal
{
    if (part.LocalPart == 0)
    {
        return MCRefusal::NotOwn;
    }

    if (part.PartType != 1)
    {
        return MCRefusal::None;
    }

    const auto& mech = static_cast<const MCLogMech&>(part);
    const auto& items = mech.Inventory->Items;
    const auto engine =
        std::ranges::find_if(items, [](const std::shared_ptr<MCLogInventoryItem>& item)
                             { return MasterComponentList[item->MasterID].Form == MCComponentForm::Engine; });

    // Port fix: a mech without an engine item (the original dereferenced null) counts as undamaged.
    if (engine != items.end() && !(*engine)->Stats.empty() && (*engine)->Stats.front()->Hits != 0)
    {
        return MCRefusal::EngineDamaged;
    }

    for (const auto& location : mech.Internals)
    {
        if (location.CurArmor == 0)
        {
            return MCRefusal::LocationDestroyed;
        }
    }

    return MCRefusal::None;
}

auto MCDeploy::Lift(MCLogPart& part) -> MCRefusal
{
    if (part.LocalPart == 0)
    {
        return MCRefusal::NotOwn;
    }

    if (Locked())
    {
        return MCRefusal::Locked;
    }

    if (const MCRefusal refusal = CantDrop(part); refusal != MCRefusal::None)
    {
        return refusal;
    }

    TheRevision++;
    part.Deployed = true;

    if (const std::shared_ptr<MCLogMech> mech = AsMech(part); mech != nullptr)
    {
        Logistics().AssignedWarriorList->SetDeployed(mech->PilotIndex, true);
    }

    return MCRefusal::None;
}

auto MCDeploy::Unlift(MCLogPart& part) -> void
{
    TheRevision++;
    part.Deployed = false;

    if (const std::shared_ptr<MCLogMech> mech = AsMech(part); mech != nullptr)
    {
        Logistics().AssignedWarriorList->SetDeployed(mech->PilotIndex, false);
    }
}

auto MCDeploy::HeldGroup() -> std::optional<size_t>
{
    for (size_t group = 0; group < GroupKeys.size(); group++)
    {
        if ((MCInput::GetAsyncKeyState(GroupKeys[group]) & KeyHeld) != 0)
        {
            return group;
        }
    }

    return std::nullopt;
}

auto MCDeploy::RefusalString(MCRefusal refusal) -> std::optional<uint32_t>
{
    switch (refusal)
    {
        case MCRefusal::TooHeavy:
            return TooHeavyString;
        case MCRefusal::EngineDamaged:
            return EngineDamagedString;
        case MCRefusal::LocationDestroyed:
            return LocationDestroyedString;
        case MCRefusal::None:
        case MCRefusal::NoSlot:
        case MCRefusal::NotOwn:
        case MCRefusal::Locked:
            break;
    }

    return std::nullopt;
}

auto MCDeploy::Place(MCLogPart& part, size_t slot) -> MCRefusal
{
    TheRevision++;
    const MCLogistics::DeploySlot& deploy = DeploySlotOf(slot);
    const bool filled = deploy.Unit > -1 || deploy.Vehicle > -1;
    const bool placed = filled ? PlaceOverOccupant(part, slot) : PlaceInEmpty(part, slot);
    return placed ? MCRefusal::None : MCRefusal::TooHeavy;
}

auto MCDeploy::Send(MCLogPart& part, std::optional<size_t> group) -> MCRefusal
{
    const size_t from = group.value_or(0);
    const size_t to = group.has_value() ? *group + 1 : Groups;

    for (size_t lance = from; lance < to; lance++)
    {
        for (size_t place = 0; place < GroupSlots; place++)
        {
            const size_t slot = lance * GroupSlots + place;

            if (!Open(slot))
            {
                // Any group: the slots the player can fill may have gaps; the group asked for ends at its first.
                if (!group.has_value())
                {
                    continue;
                }

                break;
            }

            const MCLogistics::DeploySlot& deploy = DeploySlotOf(slot);

            if (deploy.Unit != -1 || deploy.Vehicle != -1)
            {
                continue;
            }

            TheRevision++;
            return PlaceInEmpty(part, slot) ? MCRefusal::None : MCRefusal::TooHeavy;
        }
    }

    // Port fix (OB-083): with no group asked for and no free slot the original searched a fourth group past the end of
    // the slot tables.
    return MCRefusal::NoSlot;
}

auto MCDeploy::TakeOut(size_t slot) -> bool
{
    if (!Open(slot))
    {
        return false;
    }

    MCLogistics& logistics = Logistics();
    MCLogistics::DeploySlot& deploy = DeploySlotOf(slot);

    if (deploy.Unit < 0 && deploy.Vehicle < 0)
    {
        return false;
    }

    TheRevision++;

    if (deploy.Unit < 0)
    {
        std::shared_ptr<MCLogVehicle> vehicle;
        logistics.ForceVehicleList->GetVehicleInfo(deploy.Vehicle, vehicle);
        vehicle->Deployed = false;
        deploy.Vehicle = -1;
        CurDeployTonnage = TonsWith(*vehicle, -1.0f);
    }
    else
    {
        std::shared_ptr<MCLogMech> mech;
        logistics.ForceMechList->GetMechInfo(deploy.Unit, mech);
        mech->Deployed = false;
        deploy.Unit = -1;
        CurDeployTonnage = TonsWith(*mech, -1.0f);
        logistics.AssignedWarriorList->SetDeployed(mech->PilotIndex, false);
        // Port-only (SPEC-memory.md's "The GPU from the briefing"): the mission's no more until it's put in a slot again.
        MCHdArt::MechTakenOff(mech->HdFolders);
    }

    CountTons();
    logistics.SendRemoveForceMessage(static_cast<int>(slot / GroupSlots), static_cast<int>(slot % GroupSlots));
    return true;
}

auto MCDeploy::CanBegin() -> bool
{
    if (CurDeployTonnage < 1)
    {
        return false;
    }

    return !((MaxDeployTonnage < CurDeployTonnage || !Logistics().RequiredAssigned()) && LimitHolds());
}

auto MCDeploy::Begin() -> MCBegin
{
    if (!CanBegin())
    {
        return MCBegin::Refused;
    }

    if (MultiPlayer() == nullptr)
    {
        // Everything the player owns must be within the unit limit (a mission's salvage can take it past).
        if (NumUnits() > MaxUnits())
        {
            return MCBegin::TooManyUnits;
        }

        Mission()->RequestScenario(Mission()->Scenarios[Mission()->CurrentScenario]);
        return MCBegin::Ready;
    }

    SetLocked(true);
    MultiPlayer()->SendReadyForBattle();
    std::string text = MCFormatPrintf(LoadGameString(ReadyString, ReadyStringLength).c_str(),
                                      MultiPlayer()->SessionManager->MyPlayer()->Name.c_str());
    // The original's 304-byte buffer.
    text.resize(std::min(text.size(), ReadyChatMost));
    MultiPlayer()->SendChat(0, text.data());
    return MCBegin::Ready;
}

auto MCDeploy::TooManyUnitsText() -> std::string
{
    const int32_t units = NumUnits();
    const int32_t most = MaxUnits();
    return MCFormatPrintf(LoadGameString(TooManyUnitsString, StringLength).c_str(), units, most, units - most);
}

auto MCDeploy::Locked() -> bool
{
    return TheLock;
}

auto MCDeploy::SetLocked(bool locked) -> void
{
    TheLock = locked;
    TheRevision++;
}

auto MCDeploy::Revision() -> uint64_t
{
    return TheRevision;
}

auto MCDeploy::Changed() -> void
{
    TheRevision++;
}

auto MCDeploy::Keep(int32_t mission) -> void
{
    const MCLogistics& logistics = Logistics();
    MCKeptDrop kept{.Mission = mission};

    for (size_t slot = 0; slot < Slots; slot++)
    {
        const MCLogistics::DeploySlot& deploy = DeploySlotOf(slot);

        if (std::shared_ptr<MCLogMech> mech;
            deploy.Unit >= 0 && logistics.ForceMechList->GetMechInfo(deploy.Unit, mech) == 0)
        {
            kept.Slots[slot] = MCKeptUnit{.Mech = true, .Key = MechKey(logistics, *mech)};
        }
        else if (std::shared_ptr<MCLogVehicle> vehicle;
                 deploy.Unit < 0 && deploy.Vehicle >= 0 &&
                 logistics.ForceVehicleList->GetVehicleInfo(deploy.Vehicle, vehicle) == 0)
        {
            kept.Slots[slot] = MCKeptUnit{.Mech = false, .Key = VehicleKey(*vehicle)};
        }
    }

    TheKeptDrop = kept;
}

auto MCDeploy::Kept(int32_t mission) -> bool
{
    return TheKeptDrop.has_value() && TheKeptDrop->Mission == mission;
}

auto MCDeploy::PutBackKept() -> bool
{
    if (!TheKeptDrop.has_value())
    {
        return false;
    }

    MCLogistics& logistics = Logistics();
    // The drop's tons from its slots (none, as the load left them).
    CountTons();

    for (size_t slot = 0; slot < Slots; slot++)
    {
        const std::optional<MCKeptUnit>& kept = TheKeptDrop->Slots[slot];

        if (!kept.has_value())
        {
            continue;
        }

        const std::shared_ptr<MCLogPart> part =
            kept->Mech ? std::shared_ptr<MCLogPart>(Waiting(logistics.ForceMechList->Mechs, kept->Key,
                                                            [&logistics](const MCLogMech& mech)
                                                            { return MechKey(logistics, mech); }))
                       : Waiting(logistics.ForceVehicleList->Vehicles, kept->Key, VehicleKey);
        // One that doesn't go back takes the rest out again: the drop empty, as REPLAY MISSION leaves it.
        const auto notBack = [&kept, slot](const char* why)
        {
            SDL_Log("MCDeploy: mission %d's drop not put back: slot %zu's unit (%s) %s", TheKeptDrop->Mission, slot,
                    kept->Key.c_str(), why);

            for (size_t placed = 0; placed < slot; placed++)
            {
                TakeOut(placed);
            }

            return false;
        };

        if (part == nullptr)
        {
            return notBack("isn't in the force");
        }

        if (!Open(slot))
        {
            return notBack("has its slot shut");
        }

        if (Lift(*part) != MCRefusal::None)
        {
            return notBack("can't drop");
        }

        if (Place(*part, slot) != MCRefusal::None)
        {
            Unlift(*part);
            return notBack("is over the limit");
        }
    }

    return true;
}

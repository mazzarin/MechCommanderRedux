#include "stdafx.h"
#include "main/MCLogistics.h"
#include "logistics/MCMechInventoryBlock.h"
#include "logistics/MCMechRepairBlock.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCVehicleInventoryBlock.h"
#include "network/MCMultiPlayer.h"

auto MCLogistics::SetPilot(int32_t mechIndex, int32_t pilotIndex) const -> void
{
    std::shared_ptr<MCLogMech> mech;

    if (ForceMechList->GetMechInfo(mechIndex, mech) != 0)
    {
        return;
    }

    // The mech's PilotIndex is the one record of who is in it (the original also kept the mech, unread, on the
    // pilot's inventory block).
    if (pilotIndex >= 0)
    {
        // The repair block's pilot paint worked out the mech's condition (the row draws the pilot each frame).
        mech->PilotIndex = pilotIndex;
        mech->CalcStatus();
        mech->CalcPilotModifier();
        return;
    }

    // A negative index takes the pilot off: the row shows the portrait's place blank until it is painted again.
    if (RepairScreen != nullptr)
    {
        if (const std::shared_ptr<MCMechRepairBlock> row = RepairScreen->RowOf(*mech); row != nullptr)
        {
            row->ClearPilot();
        }
    }

    mech->PilotIndex = pilotIndex;
    mech->CalcPilotModifier();
}

auto MCLogistics::UndeployMech(MCLogMech& mech) -> void
{
    if (mech.Deployed == 0)
    {
        return;
    }

    for (int32_t lance = 0; lance < 3; ++lance)
    {
        for (int32_t slot = 0; slot < 4; ++slot)
        {
            int32_t& unit = DeploySlots[lance][slot].Unit;

            if (unit < 0 || unit != mech.SlotIndex)
            {
                continue;
            }

            if (MultiPlayer() != nullptr)
            {
                SendRemoveForceMessage(lance, slot);
            }

            // (The original also took the mech's briefing block off the screen; the briefing screen shows the slots
            // again from the deploy data.)
            unit = -1;
            mech.Deployed = false;
            return;
        }
    }

    mech.Deployed = false;
}

auto MCLogistics::RemoveVehicleFromSlots(int32_t forceIndex) -> void
{
    for (auto& lance : DeploySlots)
    {
        for (auto& slot : lance)
        {
            if (slot.Vehicle < 0)
            {
                continue;
            }

            if (slot.Vehicle == forceIndex)
            {
                // (The original also took the vehicle's briefing block off the screen; the briefing screen shows the
                // slots again from the deploy data.)
                slot.Vehicle = -1;
            }
            else if (slot.Vehicle > forceIndex)
            {
                // OB-183 fix: only the vehicles after the one leaving move up a place in the force list (the original
                // lowered every other deployed vehicle's index, so earlier ones dropped the wrong vehicle or none).
                --slot.Vehicle;
            }
        }
    }
}

namespace
{
    /// <summary>
    /// Moves the elements of <paramref name="from"/> that <paramref name="moves"/> picks into <paramref name="to"/>,
    /// each at the place <paramref name="place"/> finds in <paramref name="to"/>, in <paramref name="from"/>'s order.
    /// </summary>
    /// <remarks>
    /// OB-097 (fixed): the original's walks kept the element just moved as the "previous" one, so a second element to
    /// move right after it was unlinked through the other list; and its search for the place went on from where the
    /// last one stopped. The port searches each from the start. With one element moving per call (as the callers
    /// do), the result is the original's.
    /// </remarks>
    template <typename T, typename Moves, typename Place>
    void MoveElements(std::vector<std::shared_ptr<T>>& from, std::vector<std::shared_ptr<T>>& to, Moves moves,
                      Place place)
    {
        for (auto element = from.begin(); element != from.end();)
        {
            if (!moves(**element))
            {
                ++element;
                continue;
            }

            to.insert(place(to, **element), std::move(*element));
            element = from.erase(element);
        }
    }

    /// <summary>A mech is shown in the inventory's mech list unless it was read without widgets.</summary>
    bool Listed(const MCLogMech& mech)
    {
        return mech.ReadWithWidgets;
    }

    /// <summary>A vehicle is shown in the inventory's vehicle list unless it was read without widgets.</summary>
    bool Listed(const MCLogVehicle& vehicle)
    {
        return vehicle.ReadWithWidgets;
    }

    /// <summary>A pilot is always shown in the inventory's pilot list.</summary>
    bool Listed(const MCLogWarrior&)
    {
        return true;
    }

    /// <summary>Gives each listed element's inventory row its place in <paramref name="list"/> (see <see cref="Listed"/>).</summary>
    template <typename T> void RenumberRows(const std::vector<std::shared_ptr<T>>& list)
    {
        int32_t index = 0;

        for (const std::shared_ptr<T>& element : list)
        {
            if (Listed(*element))
            {
                element->ListIndex = index;
            }

            index++;
        }
    }

    /// <summary>The front of <paramref name="list"/> (where each assigned unit goes, in front of the one before it).</summary>
    template <typename T> auto Front(std::vector<std::shared_ptr<T>>& list, const T&)
    {
        return list.begin();
    }
}

auto MCLogistics::ReorderMechs() const -> void
{
    // Assigned mechs move from the mech list to the head of the force list; unassigned force mechs go back into the
    // mech list before the first with an equal or higher sort key.
    MoveElements(
        MechList->Mechs, ForceMechList->Mechs, [](const MCLogMech& mech) { return mech.Assigned; }, Front<MCLogMech>);
    MoveElements(
        ForceMechList->Mechs, MechList->Mechs, [](const MCLogMech& mech) { return !mech.Assigned; },
        [](std::vector<std::shared_ptr<MCLogMech>>& list, const MCLogMech& mech)
        {
            return std::ranges::find_if(list, [&](const std::shared_ptr<MCLogMech>& other)
                                        { return other->SortKey >= mech.SortKey; });
        });
    RenumberRows(MechList->Mechs);
    RenumberRows(ForceMechList->Mechs);
}

auto MCLogistics::ReorderVehicles() const -> void
{
    // As ReorderMechs, with the vehicle list sorted by tonnage.
    MoveElements(
        VehicleList->Vehicles, ForceVehicleList->Vehicles, [](const MCLogVehicle& vehicle) { return vehicle.Assigned; },
        Front<MCLogVehicle>);
    MoveElements(
        ForceVehicleList->Vehicles, VehicleList->Vehicles,
        [](const MCLogVehicle& vehicle) { return !vehicle.Assigned; },
        [](std::vector<std::shared_ptr<MCLogVehicle>>& list, const MCLogVehicle& vehicle)
        {
            return std::ranges::find_if(list, [&](const std::shared_ptr<MCLogVehicle>& other)
                                        { return other->CurTonnage >= vehicle.CurTonnage; });
        });
    RenumberRows(VehicleList->Vehicles);
    RenumberRows(ForceVehicleList->Vehicles);
}

auto MCLogistics::ReorderWarriors() const -> void
{
    // Assigned pilots move into the assigned list before the first of an equal or higher rank; unassigned ones go
    // back into the pilot list by rank, then callsign.
    MoveElements(
        WarriorList->Warriors, AssignedWarriorList->Warriors,
        [](const MCLogWarrior& warrior) { return warrior.Assigned; },
        [](std::vector<std::shared_ptr<MCLogWarrior>>& list, const MCLogWarrior& warrior)
        {
            return std::ranges::find_if(list, [&](const std::shared_ptr<MCLogWarrior>& other)
                                        { return other->Rank >= warrior.Rank; });
        });
    MoveElements(
        AssignedWarriorList->Warriors, WarriorList->Warriors,
        [](const MCLogWarrior& warrior) { return !warrior.Assigned; },
        [](std::vector<std::shared_ptr<MCLogWarrior>>& list, const MCLogWarrior& warrior)
        {
            auto place = std::ranges::find_if(list, [&](const std::shared_ptr<MCLogWarrior>& other)
                                              { return other->Rank >= warrior.Rank; });

            while (place != list.end() && (*place)->Callsign < warrior.Callsign && (*place)->Rank == warrior.Rank)
            {
                ++place;
            }

            return place;
        });
    RenumberRows(WarriorList->Warriors);
    RenumberRows(AssignedWarriorList->Warriors);
}

auto MCLogistics::ShiftPilots(int32_t from, int32_t amount) const -> void
{
    for (const std::shared_ptr<MCLogMech>& mech : ForceMechList->Mechs)
    {
        if (from <= mech->PilotIndex)
        {
            mech->PilotIndex += amount;
        }
    }
}

auto MCLogistics::RequiredAssigned() const -> bool
{
    if (MultiplayerInitialized)
    {
        return true;
    }

    const auto& mechs = MechList->Mechs;
    const auto& forceMechs = ForceMechList->Mechs;
    const auto& vehicles = VehicleList->Vehicles;
    const auto& forceVehicles = ForceVehicleList->Vehicles;

    // Every required mech needs a deployed mech of its chassis in the force, and every required force mech has to
    // be deployed.
    const bool mechsThere =
        std::ranges::all_of(mechs,
                            [&](const std::shared_ptr<MCLogMech>& mech)
                            {
                                return !mech->Required ||
                                       std::ranges::any_of(
                                           forceMechs, [&](const std::shared_ptr<MCLogMech>& found)
                                           { return mech->Chassis == found->Chassis && found->Deployed; });
                            }) &&
        std::ranges::none_of(forceMechs,
                             [](const std::shared_ptr<MCLogMech>& mech) { return mech->Required && !mech->Deployed; });

    if (!mechsThere)
    {
        return false;
    }

    // The same for vehicles, except that a force vehicle of the chassis counts whether it is deployed or not.
    return std::ranges::all_of(vehicles,
                               [&](const std::shared_ptr<MCLogVehicle>& vehicle)
                               {
                                   return !vehicle->Required ||
                                          std::ranges::any_of(forceVehicles,
                                                              [&](const std::shared_ptr<MCLogVehicle>& found)
                                                              { return vehicle->Chassis == found->Chassis; });
                               }) &&
           std::ranges::none_of(forceVehicles, [](const std::shared_ptr<MCLogVehicle>& vehicle)
                                { return vehicle->Required && !vehicle->Deployed; });
}

#include "stdafx.h"
#include "iface/MCTacticalInterface.h"
#include "ai/MCMoveSystem.h"
#include "iface/MCCommandParser.h"
#include "iface/MCMechBar.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCMoverGroup.h"
#include "object/MCObjectEvent.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"
#include "object/MCTreeBuilding.h"
#include "terrain/MCTacticalMap.h"

namespace
{
    /// <summary>The slopes that split the cursor directions (<see cref="MCTacticalInterface::SetCursorOffset"/>).</summary>
    constexpr std::array<float, 8> SlopeTest = {0.0984914f, 0.3033467f, 0.5345111f, 0.8206788f,
                                                1.2185035f, 1.8708684f, 3.2965581f, 10.1531706f};

    /// <summary>Sends <paramref name="object"/> the select (0x1c) or deselect (0x1d) object event.</summary>
    void SendSelectionEvent(MCBaseObject& object, int32_t type, int32_t selectionIndex = 0)
    {
        MCObjectEvent event;
        event.Init(type);

        if (type == 0x1c)
        {
            event.SelectionIndex = selectionIndex;
        }

        object.HandleEvent(event);
    }

    /// <summary>
    /// Whether <paramref name="mover"/> can jump from where it will be (its last queued order's point when
    /// <paramref name="fromWayPoint"/> and it has queued orders, else where it is) to <paramref name="position"/>.
    /// </summary>
    bool InJumpRange(MCMover& mover, const MCVector3D& position, bool fromWayPoint)
    {
        MCVector3D from;
        const int32_t numQueued = mover.GetPilot()->GetTacOrderQueueSize();

        if (numQueued > 0 && fromWayPoint)
        {
            from = mover.GetPilot()->GetTacOrderQueue().back().Point;
        }
        else
        {
            from = mover.GetPosition();
        }

        // The x87 code keeps dx and dy at full precision and rounds dz to a float.
        const double dx = static_cast<double>(from.X) - position.X;
        const double dy = static_cast<double>(from.Y) - position.Y;
        const float dz = from.Z - position.Z;
        const auto distance = static_cast<float>(std::sqrt(dy * dy + static_cast<double>(dz) * dz + dx * dx));
        return !(mover.GetJumpRange() < distance);
    }
}

auto MCTacticalInterface::IsSelected(int32_t partId) const -> bool
{
    return std::ranges::contains(SelectedMovers, partId);
}

auto MCTacticalInterface::IsSelected(const MCMoverGroup& group) const -> bool
{
    return std::ranges::any_of(SelectedLances, [&group](const std::weak_ptr<MCMoverGroup>& lance)
                               { return lance.lock().get() == &group; });
}

auto MCTacticalInterface::SelectMech(int32_t partId) -> void
{
    if (SelectedMovers.size() >= MaxSelectedMovers)
    {
        return;
    }

    if (ObjectList() != nullptr)
    {
        auto object = std::static_pointer_cast<MCGameObject>(ObjectList()->FindObjectFromPart(partId));

        // Port fix: the original asks a missing object whether it is disabled.
        if (object != nullptr && object->IsDisabled() != 0)
        {
            return;
        }

        if (object != nullptr)
        {
            SendSelectionEvent(*object, 0x1c, static_cast<int32_t>(SelectedMovers.size()));
        }
    }

    SelectedMovers.push_back(partId);
}

auto MCTacticalInterface::SelectVisibleMechs() -> void
{
    ClearMechSelection();
    std::shared_ptr<MCObjectList> list = HomeTeam()->Alignment == -1 ? ClanMechList() : InnerSphereMechList();

    if (list == nullptr)
    {
        return;
    }

    for (std::shared_ptr<MCBaseObject> object : *list)
    {
        if (!IsMoverClass(object->ObjectClass))
        {
            continue;
        }

        auto mover = std::static_pointer_cast<MCMover>(object);

        if (mover->NetPlayerId != -1 && mover->GetWindowsVisible() == Turn && mover->IsDisabled() == 0)
        {
            SelectMech(mover->PartId);
            CommandParser->AddSubject(mover->PartId);
        }
    }
}

auto MCTacticalInterface::DeselectMech(int32_t partId) -> void
{
    // OB-156 fixed: the original cleared the slot past the last one, which with twelve selected was the first selected
    // lance's.
    std::erase(SelectedMovers, partId);

    // A mover taken out of a selected lance breaks the lance up: its other members stay selected on their own.
    if (std::shared_ptr<MCFriendlyMechIcon> icon = MechBar->GetButtonFromID(partId); icon != nullptr)
    {
        if (std::shared_ptr<MCLanceIcon> lanceIcon = MechBar->GetLanceIconFromID(icon->Lance); lanceIcon != nullptr)
        {
            const std::shared_ptr<MCMoverGroup> group = lanceIcon->Group.lock();

            if (group != nullptr && IsSelected(*group))
            {
                DeselectLance(group);
                CommandParser->RemoveSubject(*group);

                for (const std::shared_ptr<MCMover>& member : group->GetMovers())
                {
                    if (member != LockObject(icon->Mover))
                    {
                        SelectMech(member->PartId);
                        CommandParser->AddSubject(member->PartId);
                    }
                }
            }
        }
    }

    if (ObjectList() != nullptr)
    {
        if (std::shared_ptr<MCBaseObject> object = ObjectList()->FindObjectFromPart(partId); object != nullptr)
        {
            SendSelectionEvent(*object, 0x1d);
        }
    }
}

auto MCTacticalInterface::DeselectEnemy() const -> void
{
    if (const std::shared_ptr<MCGameObject> enemy = LockObject(SelectedEnemy); enemy != nullptr)
    {
        SendSelectionEvent(*enemy, 0x1d);
    }
}

auto MCTacticalInterface::SelectLance(std::weak_ptr<MCMoverGroup> lance) -> void
{
    const std::shared_ptr<MCMoverGroup> group = lance.lock();
    std::shared_ptr<MCLanceIcon> lanceIcon = MechBar->GetLanceIconFromID(group->GetId());

    if (!lanceIcon->Linked)
    {
        // Not a linked lance: select its movers one by one.
        for (const std::shared_ptr<MCMover>& member : group->GetMovers())
        {
            if (member != nullptr && !IsSelected(member->PartId))
            {
                SelectMech(member->PartId);
                CommandParser->AddSubject(member->PartId);
            }
        }

        return;
    }

    if (SelectedLances.size() >= MaxSelectedLances || IsSelected(*group))
    {
        return;
    }

    SelectedLances.push_back(std::move(lance));

    for (const std::shared_ptr<MCMover>& member : group->GetMovers())
    {
        if (member != nullptr && !IsSelected(member->PartId))
        {
            SelectMech(member->PartId);
        }
    }
}

auto MCTacticalInterface::DeselectLance(std::shared_ptr<MCMoverGroup> group) -> void
{
    if (group == nullptr)
    {
        return;
    }

    std::erase_if(SelectedLances, [&group](const std::weak_ptr<MCMoverGroup>& lance) { return lance.lock() == group; });

    for (const std::shared_ptr<MCMover>& member : group->GetMovers())
    {
        DeselectMech(member != nullptr ? member->PartId : -1);
    }
}

auto MCTacticalInterface::ClearMechSelection() -> void
{
    if (Mission() != nullptr && Mission()->ScenarioCallback != nullptr)
    {
        for (const std::shared_ptr<MCFriendlyMechIcon>& icon : MechBar->Buttons())
        {
            if (const std::shared_ptr<MCBaseObject> mover = LockObject(icon->Mover); mover != nullptr)
            {
                SendSelectionEvent(*mover, 0x1d);
            }
        }
    }

    SelectedMovers.clear();
    SelectedLances.clear();
    CommandParser->ClearSubjects();
}

auto MCTacticalInterface::IsOurs(int32_t partId) const -> bool
{
    return MechBar->GetButtonFromID(partId) != nullptr;
}

auto MCTacticalInterface::AnySelected(bool armed) const -> bool
{
    if (SelectedMovers.empty() && SelectedLances.empty())
    {
        return false;
    }

    if (!armed)
    {
        return true;
    }

    // A live mover with weapons can take an attack.
    for (int32_t partId : SelectedMovers)
    {
        auto object = std::static_pointer_cast<MCGameObject>(ObjectList()->FindObjectFromPart(partId));

        if (object != nullptr && object->IsDisabled() == 0 && IsMoverClass(object->ObjectClass) &&
            static_cast<MCMover&>(*object).NumWeapons != 0)
        {
            return true;
        }
    }

    for (const std::weak_ptr<MCMoverGroup>& lance : SelectedLances)
    {
        if (const std::shared_ptr<MCMoverGroup> group = lance.lock();
            group != nullptr && std::ranges::any_of(group->GetMovers(), [](const std::shared_ptr<MCMover>& member)
                                                    { return member != nullptr && member->NumWeapons != 0; }))
        {
            return true;
        }
    }

    return false;
}

auto MCTacticalInterface::CanSelectionJump() const -> bool
{
    for (int32_t partId : SelectedMovers)
    {
        std::shared_ptr<MCBaseObject> object = ObjectList()->FindObjectFromPart(partId);

        if (!IsMoverClass(object->ObjectClass) || static_cast<MCMover&>(*object).CanJump() == 0)
        {
            return false;
        }
    }

    return !SelectedMovers.empty();
}

auto MCTacticalInterface::CanSelectionJumpTo(MCVector3D position, std::shared_ptr<MCGameObject> target,
                                             bool fromWayPoint) const -> bool
{
    // Not onto one of the player's own movers.
    if (target != nullptr && IsMoverClass(target->ObjectClass) && target->GetTeam() == HomeTeam())
    {
        return false;
    }

    if (GameMap().CellPassable(position) == 0)
    {
        return false;
    }

    for (int32_t partId : SelectedMovers)
    {
        std::shared_ptr<MCBaseObject> object = ObjectList()->FindObjectFromPart(partId);

        if (IsMoverClass(object->ObjectClass) && static_cast<MCMover&>(*object).GetPilot() != nullptr &&
            !InJumpRange(static_cast<MCMover&>(*object), position, fromWayPoint))
        {
            return false;
        }
    }

    // The lances' movers jump from where they are or their last way point.
    for (const std::weak_ptr<MCMoverGroup>& lance : SelectedLances)
    {
        const std::shared_ptr<MCMoverGroup> group = lance.lock();

        if (group == nullptr)
        {
            continue;
        }

        for (const std::shared_ptr<MCMover>& mover : group->GetMovers())
        {
            if (IsMoverClass(mover->ObjectClass) && mover->GetPilot() != nullptr &&
                !InJumpRange(*mover, position, true))
            {
                return false;
            }
        }
    }

    return true;
}

auto MCTacticalInterface::SetCursorOffset(MCVector2D screenPos) -> void
{
    if (::TacticalMap() != nullptr && ::TacticalMap()->MouseInside != 0)
    {
        CursorOffset = 6;
    }

    if (SelectedMovers.empty())
    {
        CursorOffset = 6;
    }

    // The centre of the selected movers on screen.
    const auto count = static_cast<int32_t>(SelectedMovers.size());
    float sumX = 0.0f;
    float sumY = 0.0f;

    for (int32_t partId : SelectedMovers)
    {
        if (auto object = std::static_pointer_cast<MCGameObject>(ObjectList()->FindObjectFromPart(partId));
            object != nullptr)
        {
            sumX += object->GetScreenPos(0).X;
            sumY += object->GetScreenPos(0).Y;
        }
    }

    // The x87 code stores centreX as a float and keeps centreY at full precision.
    const float centerX = sumX / static_cast<float>(count);
    const double centerY = static_cast<double>(sumY) / static_cast<double>(count);
    const auto slope = static_cast<float>(std::fabs(static_cast<double>(screenPos.Y) - centerY) /
                                          std::fabs(static_cast<double>(screenPos.X) - centerX));
    int32_t index = 0;

    while (index < static_cast<int32_t>(SlopeTest.size()) && slope > SlopeTest[index])
    {
        index++;
    }

    // Written so that a NaN centre (nothing selected) takes the original's branches.
    if (centerY <= screenPos.Y)
    {
        if (screenPos.X <= centerX)
        {
            CursorOffset = 0x20 - index;

            if (CursorOffset == 0x20)
            {
                CursorOffset = 0;
            }

            return;
        }

        CursorOffset = index + 0x10;
        return;
    }

    if (centerX <= screenPos.X)
    {
        CursorOffset = 0x10 - index;
        return;
    }

    CursorOffset = index;
}

auto MCTacticalInterface::RefitCheck(std::shared_ptr<MCGameObject> target) const -> bool
{
    if (SelectedMovers.size() != 1)
    {
        return false;
    }

    std::shared_ptr<MCBaseObject> object = ObjectList()->FindObjectFromPart(SelectedMovers[0]);

    if (object == nullptr || object->ObjectClass != MCObjectClass::GroundVehicle)
    {
        return false;
    }

    auto vehicle = std::static_pointer_cast<MCMover>(object);
    return 0.0f < vehicle->GetRefitPoints() && target != nullptr && IsMoverClass(target->ObjectClass) &&
           static_cast<MCMover&>(*target).NeedsRefit(vehicle->AmmoTruck) != 0 &&
           static_cast<MCMover&>(*target).NetPlayerId > -1;
}

auto MCTacticalInterface::GetFixedCheck(std::shared_ptr<MCGameObject> target) const -> bool
{
    if (target == nullptr || target->ObjectClass != MCObjectClass::TreeBuilding)
    {
        return false;
    }

    if (!(0.0f < target->GetRefitPoints()))
    {
        return false;
    }

    if (HomeTeam()->Alignment != target->GetAlignment() || SelectedMovers.size() != 1)
    {
        return false;
    }

    std::shared_ptr<MCBaseObject> object = ObjectList()->FindObjectFromPart(SelectedMovers[0]);

    if (object == nullptr || !IsMoverClass(object->ObjectClass))
    {
        return false;
    }

    auto mover = std::static_pointer_cast<MCMover>(object);
    const int32_t mechBay = static_cast<MCTreeBuilding&>(*target).MechBay;

    if (mover->NeedsRefit(0) == 0 || !((mover->ObjectClass == MCObjectClass::BattleMech && mechBay != 0) ||
                                       (mover->ObjectClass == MCObjectClass::GroundVehicle && mechBay == 0)))
    {
        return false;
    }

    MCVector3D bayPosition = target->GetPosition();
    return mover->DistanceFrom(bayPosition) < 100.0f;
}

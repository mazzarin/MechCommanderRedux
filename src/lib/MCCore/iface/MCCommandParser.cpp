#include "stdafx.h"
#include "iface/MCCommandParser.h"
#include "iface/MCOrderSink.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "lib/MCMsvcSort.h"
#include "network/MCMultiPlayer.h"
#include "object/MCMoverGroup.h"
#include "object/MCMover.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectQueue.h"
#include "sound/MCSoundSystem.h"

namespace
{
    /// <summary>An entry of <see cref="RankByDistance"/>'s sort: an index and its distance.</summary>
    struct MCRankedDistance
    {
        size_t Index = 0;
        float Distance = 0;
    };

    /// <summary>The distance a missing mover ranks at (the original's sentinel, near FLT_MAX).</summary>
    const float MissingMoverDistance = std::bit_cast<float>(0x7f7fc99eu);
}

auto MCCommandParser::AddSubject(int32_t partId) -> bool
{
    if (Subjects.size() >= MaxSubjects)
    {
        return false;
    }

    if (!IsSubject(partId))
    {
        Subjects.push_back(partId);
    }

    return true;
}

auto MCCommandParser::AddSubject(std::weak_ptr<MCMoverGroup> group) -> bool
{
    const std::shared_ptr<MCMoverGroup> lance = group.lock();
    SDL_assert(lance != nullptr);

    if (GroupSubjects.size() >= MaxGroupSubjects)
    {
        return false;
    }

    if (IsSubject(*lance))
    {
        return true;
    }

    // The lance's movers stop being single subjects.
    for (const std::shared_ptr<MCMover>& member : lance->GetMovers())
    {
        if (IsSubject(member->PartId))
        {
            RemoveSubject(member->PartId);
        }
    }

    GroupSubjects.push_back(std::move(group));
    return true;
}

auto MCCommandParser::IsSubject(int32_t partId) const -> bool
{
    return std::ranges::contains(Subjects, partId);
}

auto MCCommandParser::IsSubject(const MCMoverGroup& group) const -> bool
{
    return std::ranges::any_of(GroupSubjects, [&group](const std::weak_ptr<MCMoverGroup>& subject)
                               { return subject.lock().get() == &group; });
}

auto MCCommandParser::RemoveSubject(int32_t partId) -> void
{
    if (const auto found = std::ranges::find(Subjects, partId); found != Subjects.end())
    {
        Subjects.erase(found);
        return;
    }

    // Not a single subject: if it is one through its lance, the lance gives way to its other movers.
    std::shared_ptr<MCBaseObject> object = ObjectList()->FindObjectFromPart(partId);

    if (object == nullptr)
    {
        return;
    }

    if (object->ObjectClass != MCObjectClass::BattleMech && object->ObjectClass != MCObjectClass::GroundVehicle &&
        object->ObjectClass != MCObjectClass::Elemental && object->ObjectClass != MCObjectClass::Mover)
    {
        return;
    }

    std::shared_ptr<MCMoverGroup> group = static_cast<MCMover&>(*object).Group.lock();

    if (group == nullptr || !IsSubject(*group))
    {
        return;
    }

    RemoveSubject(*group);

    for (const std::shared_ptr<MCMover>& member : group->GetMovers())
    {
        if (member == nullptr || member->PartId == partId || Subjects.size() >= MaxSubjects ||
            IsSubject(member->PartId))
        {
            continue;
        }

        Subjects.push_back(member->PartId);
    }
}

auto MCCommandParser::RemoveSubject(const MCMoverGroup& group) -> void
{
    if (const auto found = std::ranges::find_if(GroupSubjects, [&group](const std::weak_ptr<MCMoverGroup>& subject)
                                                { return subject.lock().get() == &group; });
        found != GroupSubjects.end())
    {
        GroupSubjects.erase(found);
    }
}

auto MCCommandParser::ClearSubjects() -> void
{
    Subjects.clear();
    GroupSubjects.clear();
}

auto RankByDistance(std::span<const float> distances) -> std::vector<size_t>
{
    std::vector<MCRankedDistance> ranked(distances.size());

    for (size_t i = 0; i < distances.size(); i++)
    {
        ranked[i] = {i, distances[i]};
    }

    MCMsvcSort(std::span(ranked),
               [](const MCRankedDistance& a, const MCRankedDistance& b)
               {
                   if (a.Distance == b.Distance)
                   {
                       return 0;
                   }

                   return a.Distance > b.Distance ? 1 : -1;
               });

    std::vector<size_t> order(ranked.size());

    for (size_t i = 0; i < ranked.size(); i++)
    {
        order[i] = ranked[i].Index;
    }

    return order;
}

auto SortMoverList(std::span<const std::shared_ptr<MCMover>> movers, MCVector3D goal) -> void
{
    std::vector<float> distances(movers.size(), MissingMoverDistance);

    for (size_t i = 0; i < movers.size(); i++)
    {
        if (movers[i] == nullptr)
        {
            continue;
        }

        const MCVector3D position = movers[i]->GetPosition();
        const float dx = position.X - goal.X;
        const float dy = position.Y - goal.Y;
        const float dz = position.Z - goal.Z;
        distances[i] = static_cast<float>(
            std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dz) * dz + static_cast<double>(dy) * dy));
    }

    const std::vector<size_t> order = RankByDistance(distances);

    for (size_t rank = 0; rank < order.size(); rank++)
    {
        if (const std::shared_ptr<MCMover>& mover = movers[order[rank]]; mover != nullptr)
        {
            mover->SelectionIndex = static_cast<int32_t>(rank);
        }
    }
}

auto MCCommandParser::SubjectMovers() const -> std::vector<std::shared_ptr<MCMover>>
{
    std::vector<std::shared_ptr<MCMover>> movers;
    const auto add = [&movers](const std::shared_ptr<MCMover>& mover)
    {
        if (mover != nullptr && std::ranges::find(movers, mover) == movers.end())
        {
            movers.push_back(mover);
        }
    };

    for (const int32_t part : Subjects)
    {
        add(std::dynamic_pointer_cast<MCMover>(ObjectList()->FindObjectFromPart(part)));
    }

    for (const std::weak_ptr<MCMoverGroup>& subject : GroupSubjects)
    {
        if (const std::shared_ptr<MCMoverGroup> group = subject.lock(); group != nullptr)
        {
            for (const std::weak_ptr<MCMover>& mover : group->Movers)
            {
                add(mover.lock());
            }
        }
    }

    return movers;
}

auto MCCommandParser::SendTacOrder(MCTacticalOrder order, bool sortMovers) -> bool
{
    if (Subjects.empty() && GroupSubjects.empty())
    {
        return false;
    }

    SoundSystem()->PlayInterfaceSample(0xf);
    MCOrderSink& sink = _Owner.Orders();

    if (MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0)
    {
        sink.SendToServer(order, sortMovers, Subjects, GroupSubjects, false);
    }
    else
    {
        if (sortMovers)
        {
            std::vector<std::shared_ptr<MCMover>> movers(Subjects.size());

            for (size_t i = 0; i < Subjects.size(); i++)
            {
                movers[i] = std::static_pointer_cast<MCMover>(ObjectList()->FindObjectFromPart(Subjects[i]));
            }

            SortMoverList(movers, MCVector3D(order.MoveParams.WayPath.Points[0], order.MoveParams.WayPath.Points[1],
                                             order.MoveParams.WayPath.Points[2]));
        }

        const MCVector3D goal = order.GetWayPoint(0);

        // A jump-attack (method 1) becomes a jump to the target's position.
        bool jumpToObject = false;

        if (order.Code == MCTacticalOrderCode::AttackObject)
        {
            if (order.AttackParams.Method == 1)
            {
                order.Code = MCTacticalOrderCode::JumpToObject;
                order.MoveParams.Wait = 0;
                order.MoveParams.WayPath.Mode[0] = 0;

                if (LockObject(order.Target) != nullptr)
                {
                    order.SetWayPoint(0, LockObject(order.Target)->GetPosition());
                }

                jumpToObject = true;
            }
        }
        else if (order.Code == MCTacticalOrderCode::JumpToObject)
        {
            jumpToObject = true;
        }

        if (jumpToObject)
        {
            order.Code = MCTacticalOrderCode::JumpToPoint;
            Assert(LockObject(order.Target) != nullptr, 0, " JumpToObject is NULL ");
            order.SetWayPoint(0, LockObject(order.Target)->GetPosition());
        }

        // A jump gives every mover its own landing spot around the goal.
        std::vector<MCVector3D> jumpGoals;

        if (order.Code == MCTacticalOrderCode::JumpToPoint)
        {
            size_t numGoals = Subjects.size();

            for (const std::weak_ptr<MCMoverGroup>& subject : GroupSubjects)
            {
                if (const std::shared_ptr<MCMoverGroup> group = subject.lock(); group != nullptr)
                {
                    numGoals += group->Movers.size();
                }
            }

            jumpGoals.resize(numGoals);
            CalcJumpGoals(order.GetWayPoint(0), static_cast<int32_t>(numGoals), jumpGoals, order.GetJumpTarget());
        }

        for (size_t i = 0; i < Subjects.size(); i++)
        {
            auto mover = std::static_pointer_cast<MCMover>(ObjectList()->FindObjectFromPart(Subjects[i]));

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

            sink.Give(*mover, order);
        }

        order.SetWayPoint(0, goal);
        size_t goalIndex = Subjects.size();

        for (const std::weak_ptr<MCMoverGroup>& subject : GroupSubjects)
        {
            const std::shared_ptr<MCMoverGroup> group = subject.lock();

            if (group == nullptr)
            {
                continue;
            }

            const bool jump = order.Code == MCTacticalOrderCode::JumpToPoint;

            if (group->NumMovers() == 1)
            {
                if (jump)
                {
                    order.SetWayPoint(0, jumpGoals[goalIndex]);
                }

                sink.Give(*group->GetMover(0), order);
            }
            else
            {
                sink.Give(*group, order,
                          jump ? std::span<const MCVector3D>(jumpGoals).subspan(goalIndex)
                               : std::span<const MCVector3D>{});
            }

            goalIndex += group->Movers.size();
        }
    }

    SoundSystem()->PlayInterfaceSample(0x11);

    if (_Owner.OneShotMode)
    {
        _Owner.CurrentMode = MCInterfaceMode::None;
    }

    return true;
}

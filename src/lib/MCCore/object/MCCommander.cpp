#include "stdafx.h"
#include "object/MCCommander.h"
#include "lib/MCFatal.h"
#include "network/MCMultiPlayer.h"
#include "object/MCForces.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"

namespace
{
    /// <summary>The home commander's support changed: redraw the tactical map.</summary>
    void RedrawTacticalMap(const MCCommander& commander)
    {
        if (TacticalMap() != nullptr && &commander == HomeCommander().get())
        {
            TacticalMap()->RefreshPage();
        }
    }
}

MCCommander::MCCommander(int32_t id) : Id(id)
{
    for (int32_t i = 0; i < MaxGroups; i++)
    {
        Groups[i] = std::make_shared<MCMoverGroup>(i);
    }
}

auto MCCommander::SetGroup(int32_t groupId, std::span<const std::shared_ptr<MCMover>> movers, int32_t point) -> int32_t
{
    Assert(groupId >= 0 && groupId < MaxGroups, 0, " Commander::bad id in setGroup ");
    MCMoverGroup& group = *Groups[groupId];
    group.Disband();

    for (const std::shared_ptr<MCMover>& mover : movers)
    {
        if (std::shared_ptr<MCMoverGroup> oldGroup = mover->Group.lock())
        {
            oldGroup->Remove(*mover);
        }

        group.Add(mover);
    }

    if (point < std::ssize(movers) && point > -1)
    {
        group.SetPoint(movers[point]);
    }

    return 0;
}

auto MCCommander::SetNetPlayerId(int32_t playerId) -> void
{
    for (const std::shared_ptr<MCMoverGroup>& group : Groups)
    {
        for (const std::shared_ptr<MCMover>& mover : group->GetMovers())
        {
            mover->NetPlayerId = playerId;
        }
    }
}

auto MCCommander::AddToGui(int visible) -> void
{
    for (int32_t i = 0; i < 4; i++)
    {
        Groups[i]->AddToGui(visible);
    }
}

namespace
{
    /// <summary>Port: <paramref name="count"/> strikes, or none in a match that bans them (<paramref name="ban"/>).</summary>
    int32_t Allowed(int32_t count, uint32_t ban)
    {
        return MCMatchPlays(ban) ? 0 : count;
    }
}

auto MCCommander::SetNumSmallStrikes(int32_t strikes) -> void
{
    NumSmallStrikes = Allowed(strikes, MCMatchRule::NoArtillery);
    RedrawTacticalMap(*this);
}

auto MCCommander::SetNumLargeStrikes(int32_t strikes) -> void
{
    NumLargeStrikes = Allowed(strikes, MCMatchRule::NoArtillery);
    RedrawTacticalMap(*this);
}

auto MCCommander::SetNumSensorStrikes(int32_t strikes) -> void
{
    NumSensorStrikes = Allowed(strikes, MCMatchRule::NoProbes);
    RedrawTacticalMap(*this);
}

auto MCCommander::SetNumCameraDrones(int32_t drones) -> void
{
    NumCameraDrones = Allowed(drones, MCMatchRule::NoProbes);
    RedrawTacticalMap(*this);
}

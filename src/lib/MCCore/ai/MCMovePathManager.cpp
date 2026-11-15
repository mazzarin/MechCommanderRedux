#include "stdafx.h"
#include "ai/MCMovePathManager.h"
#include "ai/MCMovePath.h"
#include "object/MCMechWarrior.h"
#include "object/MCMoverGameSystem.h"
#include "sprite/MCMechActor.h"

namespace
{
    /// <summary>Port-only: how many steps past a path's current one its heading is taken toward (the first steps may
    /// step aside round a corner).</summary>
    constexpr int32_t HeadingSteps = 2;
}

auto MCPathHeading(const MCMovePath& path) -> std::optional<MCVector3D>
{
    if (path.NumSteps <= 0)
    {
        return std::nullopt;
    }

    const auto step = static_cast<size_t>(std::clamp(path.CurStep + HeadingSteps, 0, path.NumSteps - 1));
    return step < path.StepList.size() ? std::optional(path.StepList[step].Destination) : std::nullopt;
}

auto MCMovePathManager::Remove(const MCPathQueueRec& rec) -> void
{
    _Queue.remove_if([&rec](const std::shared_ptr<MCPathQueueRec>& queued) { return queued.get() == &rec; });
}

auto MCMovePathManager::Remove(MCMechWarrior& pilot) -> void
{
    const std::shared_ptr<MCPathQueueRec> request = pilot.MovePathRequest.lock();

    if (request == nullptr)
    {
        return;
    }

    Remove(*request);
    pilot.MovePathRequest.reset();
}

auto MCMovePathManager::Request(MCMechWarrior& pilot, int32_t selectionIndex, uint32_t moveParams, float priority,
                                int32_t initPath) -> void
{
    Remove(pilot);

    // Behind the last request of a higher priority (so ahead of those of the same priority).
    const auto before = std::ranges::find_if(_Queue, [priority](const std::shared_ptr<MCPathQueueRec>& queued)
                                             { return !(priority < queued->Priority); });
    const auto rec = _Queue.insert(before, std::make_shared<MCPathQueueRec>(priority, pilot.shared_from_this(),
                                                                            selectionIndex, moveParams, initPath));
    pilot.MovePathRequest = *rec;
}

auto MCMovePathManager::CalcPath() -> void
{
    if (_Queue.empty())
    {
        return;
    }

    const std::shared_ptr<MCPathQueueRec> rec = _Queue.front();
    _Queue.pop_front();
    std::shared_ptr<MCMechWarrior> pilot = rec->Pilot.lock();
    pilot->MovePathRequest.reset();

    if (const std::shared_ptr<MCGameObject> vehicle = pilot->GetVehicle(); vehicle != nullptr)
    {
        pilot->CalcMovePath(rec->SelectionIndex, rec->MoveParams, rec->InitPath);

        // Port-only (HD art, SPEC-first-use.md: a unit's first frames, by intent): a mech's walk and run toward its new
        // path's heading read before it turns.
        if (const std::optional<MCVector3D> heading = MCPathHeading(*pilot->MoveOrders.Path[0]); heading.has_value())
        {
            MCReadMechMoveAhead(*vehicle, *heading);
        }
    }
}

auto MCMovePathManager::Update() -> void
{
    for (int32_t i = 0; i < RequestsPerUpdate && !_Queue.empty(); i++)
    {
        CalcPath();
    }
}

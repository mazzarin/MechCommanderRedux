#pragma once

#include "lib/MCVector3D.h"

class MCMechWarrior;
class MCMovePath;

/// <summary>A queued path request of <see cref="MCMovePathManager"/>.</summary>
/// <remarks>Original: <c>struct _PathQueueRec</c>; the queue links went with the pool.</remarks>
struct MCPathQueueRec
{
    /// <summary>Sort key; higher is served first, and a new request goes ahead of the ones of equal priority.</summary>
    float Priority = 0;
    std::weak_ptr<MCMechWarrior> Pilot;
    int32_t SelectionIndex = 0;
    uint32_t MoveParams = 0;
    /// <summary>Passed as the last argument of MechWarrior::calcMovePath.</summary>
    int32_t InitPath = 0;
};

/// <summary>Spreads path calculation over frames: pilots queue requests, a few are served per update.</summary>
/// <remarks>Original source: <c>ai\move.cpp</c>. The original's pool held 300 requests ("Too many pilots calcing
/// paths"); the port's queue grows (a pilot has one request at a time).</remarks>
class MCMovePathManager
{
public:
    /// <summary>Requests served per <see cref="Update"/>.</summary>
    static constexpr int32_t RequestsPerUpdate = 5;

    /// <summary>Removes the pilot's pending request, if any.</summary>
    void Remove(MCMechWarrior& pilot);
    /// <summary>Queues (or requeues) a path request for <paramref name="pilot"/>, sorted by priority.</summary>
    void Request(MCMechWarrior& pilot, int32_t selectionIndex, uint32_t moveParams, float priority, int32_t initPath);
    /// <summary>Serves the first request.</summary>
    void CalcPath();
    /// <summary>Serves up to <see cref="RequestsPerUpdate"/> requests.</summary>
    void Update();

    /// <summary>The number of queued requests.</summary>
    int32_t NumPathsInQueue() const { return static_cast<int32_t>(_Queue.size()); }

    /// <summary>The queued requests, first served first.</summary>
    const std::list<std::shared_ptr<MCPathQueueRec>>& Queue() const { return _Queue; }

private:
    /// <summary>Removes <paramref name="rec"/> (an element of the queue).</summary>
    void Remove(const MCPathQueueRec& rec);

    /// <summary>The requests, highest priority first. A list, so a pilot's pointer to its request stays valid.</summary>
    std::list<std::shared_ptr<MCPathQueueRec>> _Queue;
};

/// <summary>
/// Port-only (HD art, SPEC-first-use.md: a unit's first frames, by intent): the point <paramref name="path"/> heads for
/// first, a couple of steps past its current one (its first steps may step aside round a corner); none while it has no
/// steps.
/// </summary>
std::optional<MCVector3D> MCPathHeading(const MCMovePath& path);

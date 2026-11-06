#pragma once

#include "ai/MCTacticalOrder.h"

class MCMover;
class MCMoverGroup;
class MCTacticalInterface;

/// <summary>
/// The command parser: holds the "subjects" of the player's next order (movers by part id, and lances) and the way path
/// being laid, and sends a <see cref="MCTacticalOrder"/> to each subject through the interface's order sink.
/// </summary>
/// <remarks>Original source: <c>iface\parser.cpp</c> (<c>Parser</c>).</remarks>
class MCCommandParser
{
public:
    /// <summary>
    /// The movers it orders (kept: a player's force; the multiplayer order and group messages carry a player's movers
    /// as 12 bits).
    /// </summary>
    static constexpr size_t MaxSubjects = 12;
    /// <summary>
    /// The lances it orders. Kept quirk (OB-068): it takes only three, though the commander has four lances.
    /// </summary>
    static constexpr size_t MaxGroupSubjects = 3;

    /// <summary>A parser whose orders go out through <paramref name="owner"/>'s order sink.</summary>
    explicit MCCommandParser(MCTacticalInterface& owner) : _Owner(owner) {}

    /// <summary>Adds the mover with part id <paramref name="partId"/> (once).</summary>
    /// <returns>False when the subjects are full.</returns>
    bool AddSubject(int32_t partId);
    /// <summary>Adds a lance (once), dropping its movers from the single subjects.</summary>
    /// <returns>False when the lances are full.</returns>
    bool AddSubject(std::weak_ptr<MCMoverGroup> group);
    bool IsSubject(int32_t partId) const;
    bool IsSubject(const MCMoverGroup& group) const;
    /// <summary>
    /// Removes the mover with part id <paramref name="partId"/>; if it is only a subject through its lance, the lance
    /// is replaced by its other movers.
    /// </summary>
    void RemoveSubject(int32_t partId);
    void RemoveSubject(const MCMoverGroup& group);
    void ClearSubjects();

    /// <summary>
    /// Sends <paramref name="order"/> to every subject; with <paramref name="sortMovers"/> the movers are ranked by
    /// distance to the goal first (<see cref="SortMoverList"/>). Jump orders get per-mover goals
    /// (<c>CalcJumpGoals</c>). A multiplayer client sends the order to the server instead.
    /// </summary>
    /// <returns>False when there was no subject.</returns>
    bool SendTacOrder(MCTacticalOrder order, bool sortMovers);

    /// <summary>
    /// Port-only: every mover ordered, the single subjects' then their lances', each once (the selection, for an
    /// order sent as its units, as the LIGHTS are: mission/MCNightLights.h).
    /// </summary>
    std::vector<std::shared_ptr<MCMover>> SubjectMovers() const;

    /// <summary>Part ids of the single movers ordered.</summary>
    std::vector<int32_t> Subjects;
    /// <summary>The lances ordered.</summary>
    std::vector<std::weak_ptr<MCMoverGroup>> GroupSubjects;

private:
    MCTacticalInterface& _Owner;
};

/// <summary>
/// The order in which <paramref name="distances"/> rank (nearest first): position i of the result holds the index of
/// the i-th nearest. Equal distances keep the order MCX.EXE's <c>qsort</c> gave them (rule R5).
/// </summary>
std::vector<size_t> RankByDistance(std::span<const float> distances);

/// <summary>
/// Ranks <paramref name="movers"/> by distance to <paramref name="goal"/>, storing each one's rank in the mover (its
/// formation position).
/// </summary>
void SortMoverList(std::span<const std::shared_ptr<MCMover>> movers, MCVector3D goal);

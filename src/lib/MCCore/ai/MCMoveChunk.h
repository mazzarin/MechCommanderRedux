#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "lib/MCVector3D.h"

class MCMechWarrior;
class MCMover;
class MCMovePath;

/// <summary>
/// Port-only: what the server's chunk of fewer than <see cref="MCMoveChunk::MaxSteps"/> steps says of the move past its
/// last step, in its last step direction, which such a chunk leaves unused (older builds send 0 there and never read
/// it). Without it a guest can't tell a mech held up, or at a leg's end before the next is planned, from one at its
/// move's end: the chunk is as short (OB-189: the original's guest stood its mech there).
/// </summary>
enum class MCChunkWord : int32_t
{
    /// <summary>No word: an older server's chunk, or a full one.</summary>
    None = 0,
    /// <summary>The move ends at the chunk's last step.</summary>
    EndsHere = 1,
    /// <summary>The move goes on past it: the mover is held up, or its next path or leg is yet to be planned.</summary>
    GoesOn = 2,
};

/// <summary>
/// Port-only: the state of a pilot's paths a chunk was built from (not sent): once it changes, a chunk built again
/// may show more of the move.
/// </summary>
struct MCChunkPaths
{
    int32_t Steps = 0;
    int32_t StepsWhenNotPaused = 0;
    int32_t Leg = 0;
    int32_t NextSteps = 0;
    int32_t NextLeg = 0;
    bool Asked = false;

    bool operator==(const MCChunkPaths&) const = default;

    /// <summary>The state of <paramref name="pilot"/>'s paths as they stand.</summary>
    static MCChunkPaths Of(const MCMechWarrior& pilot);
};

/// <summary>
/// A mover's move path in the compact form sent over the network: up to four steps, each a tile and cell, with the
/// direction from each step to the next, and all of it packed into one 32-bit word.
/// </summary>
/// <remarks>Original source: <c>ai\move.cpp</c> (init inline in <c>ai\move.h</c>).</remarks>
class MCMoveChunk
{
public:
    /// <summary>Steps a chunk carries: the packed word stores the count - 1 in 2 bits (a wire format limit).</summary>
    static constexpr int32_t MaxSteps = 4;
    /// <summary>The last step direction: a chunk of fewer than <see cref="MaxSteps"/> steps never uses it.</summary>
    static constexpr size_t LastDirection = MaxSteps - 2;

    /// <summary>Makes the chunk empty (no start cell, no steps, walking).</summary>
    void Reset()
    {
        StepPos[0][0] = -1;
        StepPos[0][1] = -1;
        NumSteps = 0;
        Run = 0;
        Data = 0;
        Word = MCChunkWord::None;
        Paths.reset();
    }

    /// <summary>Builds the chunk from the mover's current path (and the one after it).</summary>
    void Build(MCMover& mover, const MCMovePath& path1, const MCMovePath& path2);
    /// <summary>Builds a one-step chunk at the mover's cell (a mover without a path).</summary>
    void Build(MCMover& mover);
    /// <summary>Builds a one-step chunk to a jump destination.</summary>
    void Build(MCMover& mover, MCVector3D jumpGoal);
    /// <summary>Packs the steps into <see cref="Data"/>.</summary>
    void Pack(MCMover& mover);
    /// <summary>Unpacks <see cref="Data"/>.</summary>
    /// <returns>Whether the chunk was good (a step count of 1..4, and directions 0..7).</returns>
    bool Unpack(MCMover& mover);
    /// <summary>Whether every unpacked step is on the game map.</summary>
    /// <remarks>Issue #25: a chunk from the server can name any cell its ten bits hold.</remarks>
    bool OnMap() const;
    /// <summary>Whether two chunks hold the same steps and word (else dumps both to mvchunk.dbg).</summary>
    bool EqualTo(MCMover& mover, const MCMoveChunk& chunk) const;
    /// <summary>
    /// Port-only: whether the server's chunk said the move goes on past it (<see cref="MCChunkWord::GoesOn"/>) and
    /// <paramref name="pilot"/>'s paths have changed since it was built: one built now may show more of the move.
    /// </summary>
    bool AwaitsMore(const MCMechWarrior& pilot) const;

private:
    /// <summary>The end of both path builds: a bad step count falls back to the mover's cell; then the run flag.</summary>
    void FinishBuild(MCMover& mover);

public:
    /// <summary>Per step: tileR, tileC, cellR, cellC.</summary>
    std::array<std::array<int32_t, 4>, MaxSteps> StepPos{};
    /// <summary>Direction (0-7) from each step to the next.</summary>
    std::array<int32_t, MaxSteps - 1> StepRelPos{};
    int32_t NumSteps = 0;
    int32_t Run = 0;
    /// <summary>
    /// The packed chunk: start cell row/col, numSteps - 1, run, then the three step directions (port-only: the last,
    /// unused in a chunk of fewer steps, its <see cref="Word"/>).
    /// </summary>
    uint32_t Data = 0;
    /// <summary>Port-only: what a chunk of fewer than <see cref="MaxSteps"/> steps says of the move past it.</summary>
    MCChunkWord Word = MCChunkWord::None;
    /// <summary>Port-only: the state of the pilot's paths the server built the chunk from (not sent).</summary>
    std::optional<MCChunkPaths> Paths;
};

/// <summary>Writes one or two chunks and the mover's state to mvchunk.dbg (and the crash report's game text).</summary>
void DebugMoveChunk(MCMover& mover, const MCMoveChunk& chunk1, std::optional<MCMoveChunk> chunk2);

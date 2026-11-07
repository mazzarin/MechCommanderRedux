#include "stdafx.h"
#include "ai/MCMoveChunk.h"
#include "ai/MCMoveGeometry.h"
#include "ai/MCMovePath.h"
#include "ai/MCObjectMap.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCMechWarrior.h"
#include "object/MCWeaponChunkDebug.h"

namespace
{
    /// <summary>The size of ChunkDebugMsg (object\gameobj.cpp).</summary>
    constexpr size_t ChunkDebugMsgSize = 0x1400;

    /// <summary>
    /// Copies path step <paramref name="step"/> into chunk step <paramref name="index"/>, with its direction: from
    /// the start cell for the first step, else the path step's own.
    /// </summary>
    /// <remarks>Inlined three times in MoveChunk::build.</remarks>
    void CopyChunkStep(MCMoveChunk& chunk, int32_t index, const MCPathStep& step)
    {
        std::array<int32_t, 4>& pos = chunk.StepPos[static_cast<size_t>(index)];
        pos = {step.TileR, step.TileC, step.CellR, step.CellC};

        if (index == 1)
        {
            const std::array<int32_t, 4>& start = chunk.StepPos[0];
            chunk.StepRelPos[0] = CellDirToCell(start[0], start[1], start[2], start[3], pos[0], pos[1], pos[2], pos[3]);
        }
        else
        {
            chunk.StepRelPos[static_cast<size_t>(index - 1)] = static_cast<int8_t>(step.Direction);
        }
    }

    /// <summary>Makes the chunk a single step at the mover's cell.</summary>
    void SetChunkAtMover(MCMoveChunk& chunk, MCMover& mover)
    {
        std::shared_ptr<const MCObjectPosition> objPosition = mover.GetObjPosition();
        chunk.StepPos[0] = {objPosition->TileR, objPosition->TileC, objPosition->CellR, objPosition->CellC};
        chunk.StepRelPos = {};
        chunk.NumSteps = 1;
    }

    /// <summary>The text of one chunk for the debug dump.</summary>
    std::string ChunkText(std::string_view title, const MCMoveChunk& chunk)
    {
        std::string text(title);

        for (int32_t i = 0; i < MCMoveChunk::MaxSteps; i++)
        {
            const std::array<int32_t, 4>& pos = chunk.StepPos[static_cast<size_t>(i)];
            text += std::format("stepPos[{}] = ({}, {}, {}, {})\n", i, pos[0], pos[1], pos[2], pos[3]);
        }

        text += std::format("stepRelPos = {}, {}, {}\n", chunk.StepRelPos[0], chunk.StepRelPos[1], chunk.StepRelPos[2]);
        text += std::format("numSteps = {}\n", chunk.NumSteps);
        text += std::format("run = {}\n", chunk.Run != 0 ? 'T' : 'F');
        text += std::format("word = {}\n", static_cast<int32_t>(chunk.Word));
        return text;
    }
}

auto DebugMoveChunk(MCMover& mover, const MCMoveChunk& chunk1, std::optional<MCMoveChunk> chunk2) -> void
{
    std::string text;
    const MCVector3D position = mover.GetPosition();
    std::shared_ptr<const MCObjectPosition> objPosition = mover.GetObjPosition();
    text += std::format("Mover = {} ({})\n", mover.DebugStatus, mover.PartId);
    text += std::format("Mover World Pos = ({:.4f}, {:.4f}, {:.4f})\n", position.X, position.Y, position.Z);
    text += std::format("Mover Obj Pos = [{}, {}, {}, {}]\n", objPosition->TileR, objPosition->TileC,
                        objPosition->CellR, objPosition->CellC);

    if (mover.GetPilot() == nullptr)
    {
        text += "NULL pilot!\n";
    }

    if (mover.ObjectClass == MCObjectClass::BattleMech && static_cast<MCBattleMech&>(mover).InJump != 0)
    {
        int32_t tileR = 0;
        int32_t tileC = 0;
        int32_t cellR = 0;
        int32_t cellC = 0;
        WorldCoordToMapCoord(static_cast<MCBattleMech&>(mover).JumpGoal, tileR, tileC, cellR, cellC);
        text += std::format("Jumping to [{}, {}, {}, {}]\n", tileR, tileC, cellR, cellC);
    }

    text += "\n";
    text += ChunkText("CHUNK1\n", chunk1);

    if (chunk2)
    {
        text += ChunkText("\nCHUNK2\n", *chunk2);
    }

    ChunkDebugMsg = std::move(text);
    SaveChunkDebugMsg("mvchunk.dbg");
}

auto MCChunkPaths::Of(const MCMechWarrior& pilot) -> MCChunkPaths
{
    MCChunkPaths paths;
    paths.Asked = pilot.MovePathRequest.lock() != nullptr;

    if (const std::unique_ptr<MCMovePath>& path = pilot.MoveOrders.Path[0]; path != nullptr)
    {
        paths.Steps = path->NumSteps;
        paths.StepsWhenNotPaused = path->NumStepsWhenNotPaused;
        paths.Leg = path->GlobalStep;
    }

    if (const std::unique_ptr<MCMovePath>& next = pilot.MoveOrders.Path[1]; next != nullptr)
    {
        paths.NextSteps = next->NumStepsWhenNotPaused;
        paths.NextLeg = next->GlobalStep;
    }

    return paths;
}

auto MCMoveChunk::Build(MCMover& mover, const MCMovePath& path1, const MCMovePath& path2) -> void
{
    SetChunkAtMover(*this, mover);
    Word = MCChunkWord::None;
    bool nextShown = false;

    if (path1.NumSteps > 0)
    {
        const int32_t pathSteps = path1.NumSteps;
        const int32_t curStep = path1.CurStep;
        int32_t index = 1;
        bool roomLeft = true;

        if (curStep < pathSteps)
        {
            const MCPathStep& current = path1.StepList[static_cast<size_t>(curStep)];
            bool onCurrentStep = StepPos[0][0] == current.TileR && StepPos[0][1] == current.TileC &&
                                 StepPos[0][2] == current.CellR && StepPos[0][3] == current.CellC;

            if (!onCurrentStep && CellDirToCell(StepPos[0][0], StepPos[0][1], StepPos[0][2], StepPos[0][3],
                                                current.TileR, current.TileC, current.CellR, current.CellC) == -2)
            {
                onCurrentStep = true; // not next to the current step: start the chunk from it
            }

            int32_t firstStep;
            int32_t stepsLeft;

            if (onCurrentStep)
            {
                StepPos[0] = {current.TileR, current.TileC, current.CellR, current.CellC};
                firstStep = curStep + 1;
                stepsLeft = pathSteps - firstStep;
            }
            else
            {
                firstStep = curStep;
                stepsLeft = pathSteps - curStep;
            }

            roomLeft = stepsLeft < MaxSteps - 1;
            NumSteps = roomLeft ? stepsLeft : MaxSteps - 1;

            for (int32_t i = 0; i < NumSteps; i++)
            {
                CopyChunkStep(*this, index++, path1.StepList[static_cast<size_t>(firstStep + i)]);
            }

            NumSteps++;
        }

        const bool nextLeg = path1.GlobalStep >= 0 && path2.GlobalStep == path1.GlobalStep + 1;

        if (roomLeft && nextLeg && path2.NumStepsWhenNotPaused > 0 && NumSteps < MaxSteps)
        {
            const int32_t extraSteps = std::min(MaxSteps - NumSteps, path2.NumSteps);

            for (int32_t i = 0; i < extraSteps; i++)
            {
                Assert(index < MaxSteps, static_cast<uint32_t>(index),
                       " MoveChunk.build: path2 and bad curStep > MOVECHUNK_NUM_STEPS ");

                // Port fix: the original writes past stepPos[] when the assert above fails.
                if (index >= MaxSteps)
                {
                    break;
                }

                CopyChunkStep(*this, index++, path2.StepList[static_cast<size_t>(i)]);
            }

            NumSteps = extraSteps + NumSteps;
            nextShown = extraSteps > 0;
            Assert(NumSteps < MaxSteps + 1, static_cast<uint32_t>(index),
                   " MoveChunk.build: path2 and bad curStep > MOVECHUNK_NUM_STEPS ");
        }
    }

    FinishBuild(mover);

    // Port-only: a chunk with room left says whether the move goes on past it: held up (its path paused: its own cell
    // alone), a path asked for, or more of the move than the chunk's paths.
    const MCMechWarrior& pilot = *mover.GetPilot();
    Paths = MCChunkPaths::Of(pilot);

    if (NumSteps < MaxSteps)
    {
        const bool heldUp = path1.NumSteps < path1.NumStepsWhenNotPaused && path1.CurStep < path1.NumStepsWhenNotPaused;
        const bool goesOn = heldUp || Paths->Asked || pilot.MoveGoesOnPast(nextShown ? path2 : path1);
        Word = goesOn ? MCChunkWord::GoesOn : MCChunkWord::EndsHere;
    }
}

auto MCMoveChunk::Build(MCMover& mover) -> void
{
    SetChunkAtMover(*this, mover);
    Word = MCChunkWord::None;
    Paths.reset();
    FinishBuild(mover);
}

auto MCMoveChunk::FinishBuild(MCMover& mover) -> void
{
    if (NumSteps < 1 || NumSteps > MaxSteps)
    {
        SetChunkAtMover(*this, mover);
    }

    Run = mover.GetPilot()->MoveOrders.Run;

    if (Run != 0 && mover.ObjectClass == MCObjectClass::BattleMech)
    {
        Run = static_cast<MCBattleMech&>(mover).LegStatus == 0 ? 1 : 0;
    }

    Data = 0;
}

auto MCMoveChunk::Build(MCMover& /*mover*/, MCVector3D jumpGoal) -> void
{
    WorldCoordToMapCoord(jumpGoal, StepPos[0][0], StepPos[0][1], StepPos[0][2], StepPos[0][3]);
    StepRelPos = {};
    NumSteps = 1;
    Run = 0;
    Word = MCChunkWord::None;
    Paths.reset();
}

auto MCMoveChunk::Pack(MCMover& mover) -> void
{
    const int32_t stepCount = NumSteps;
    uint32_t packed = static_cast<uint32_t>(StepPos[0][1] * MapCellDim + StepPos[0][3]) << 3 |
                      static_cast<uint32_t>(StepPos[0][0] * MapCellDim + StepPos[0][2]) << 13 |
                      static_cast<uint32_t>(stepCount * 2 - 2);

    if (Run != 0)
    {
        packed |= 1;
    }

    // Original behaviour (OB-029): a direction of -2 (MoveChunk::build found no neighbour) spills into the upper
    // bits. Port-only: a chunk with room left carries its word in the last direction.
    const int32_t lastDirection = stepCount < MaxSteps ? static_cast<int32_t>(Word) : StepRelPos[LastDirection];
    packed = ((packed << 3 | static_cast<uint32_t>(StepRelPos[0])) << 3 | static_cast<uint32_t>(StepRelPos[1])) << 3 |
             static_cast<uint32_t>(lastDirection);
    Data = packed;

    if (stepCount < 1 || stepCount > MaxSteps)
    {
        DebugMoveChunk(mover, *this, std::nullopt);
        Assert(false, static_cast<uint32_t>(NumSteps),
               std::format(" MoveChunk.pack: bad numSteps {} (save mvchunk.dbg file) ", NumSteps));
    }
}

auto MCMoveChunk::Unpack(MCMover& /*mover*/) -> bool
{
    const uint32_t packed = Data;
    StepRelPos[2] = static_cast<int32_t>(packed & 7);
    StepRelPos[1] = static_cast<int32_t>((packed >> 3) & 7);
    StepRelPos[0] = static_cast<int32_t>((packed >> 6) & 7);
    NumSteps = static_cast<int32_t>((packed >> 10) & 3) + 1;
    Run = static_cast<int32_t>((packed >> 9) & 1);
    const uint32_t cellCol = (packed >> 12) & 0x3ff;
    StepPos[0][1] = static_cast<int32_t>(cellCol / MapCellDim);
    StepPos[0][3] = static_cast<int32_t>(cellCol % MapCellDim);
    const uint32_t cellRow = packed >> 22;
    Data = cellRow; // as the original: the packed word is left holding the start cell row
    StepPos[0][0] = static_cast<int32_t>(cellRow / MapCellDim);
    StepPos[0][2] = static_cast<int32_t>(cellRow % MapCellDim);

    // Port-only: a chunk with room left carries its word in the last direction (an older server's 0, no word).
    const int32_t word = StepRelPos[LastDirection];
    Word = NumSteps < MaxSteps && (word == static_cast<int32_t>(MCChunkWord::EndsHere) ||
                                   word == static_cast<int32_t>(MCChunkWord::GoesOn))
               ? static_cast<MCChunkWord>(word)
               : MCChunkWord::None;

    for (int32_t i = 0; i < NumSteps - 1; i++)
    {
        const int32_t dir = StepRelPos[static_cast<size_t>(i)];

        if (dir < 0 || dir > 7)
        {
            return false;
        }

        const std::array<int32_t, 4>& from = StepPos[static_cast<size_t>(i)];
        const std::array<int32_t, 4>& adj =
            AdjCellTable[static_cast<size_t>(from[2] * MapCellDim + from[3])][static_cast<size_t>(dir)];
        StepPos[static_cast<size_t>(i + 1)] = {adj[0] + from[0], adj[1] + from[1], adj[2], adj[3]};
    }

    return true;
}

auto MCMoveChunk::OnMap() const -> bool
{
    const auto steps = std::span(StepPos).first(static_cast<size_t>(std::clamp(NumSteps, 0, MaxSteps)));
    return std::ranges::all_of(steps,
                               [](const std::array<int32_t, 4>& pos) { return GameMap().OnMap(pos[0], pos[1]); });
}

auto MCMoveChunk::EqualTo(MCMover& mover, const MCMoveChunk& chunk) const -> bool
{
    bool equal = NumSteps == chunk.NumSteps && Run == chunk.Run && Word == chunk.Word;

    for (int32_t i = 0; equal && i < NumSteps; i++)
    {
        equal = StepPos[static_cast<size_t>(i)] == chunk.StepPos[static_cast<size_t>(i)];
    }

    for (int32_t i = 0; equal && i < NumSteps - 1; i++)
    {
        equal = StepRelPos[static_cast<size_t>(i)] == chunk.StepRelPos[static_cast<size_t>(i)];
    }

    if (!equal)
    {
        DebugMoveChunk(mover, *this, chunk);
    }

    return equal;
}

auto MCMoveChunk::AwaitsMore(const MCMechWarrior& pilot) const -> bool
{
    return Word == MCChunkWord::GoesOn && Paths.has_value() && *Paths != MCChunkPaths::Of(pilot);
}

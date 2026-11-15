#include "stdafx.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "TestGame.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "engine/MCByteFlag.h"
#include "lib/MCHeldFile.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCExplosionType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMechControlData.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectType.h"
#include "object/MCObjectTypeManager.h"
#include "object/MCMechWarrior.h"
#include "platform/MCFileSystem.h"
#include "platform/MCFrameLog.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCSpriteManager.h"
#include "sprite/MCSpriteTree.h"
#include "sprite/MCVfxAppearanceType.h"
#include "sprite/MCShape.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxClip.h"

namespace
{
    /// <summary>Whether map cell (<paramref name="row"/>, <paramref name="col"/>) is passable (off the map: no).</summary>
    bool CellPassable(int32_t row, int32_t col)
    {
        if (row < 0 || col < 0 || row >= GameMap().Height * 3 || col >= GameMap().Width * 3)
        {
            return false;
        }

        return GameMap().Map[(row / 3) * GameMap().Width + col / 3].GetCellPassable(row % 3, col % 3) != 0;
    }

    /// <summary>The map cell under <paramref name="position"/>, as row and column.</summary>
    std::pair<int32_t, int32_t> CellAt(MCVector3D position)
    {
        int32_t tileR = 0;
        int32_t tileC = 0;
        int32_t cellR = 0;
        int32_t cellC = 0;
        GameMap().WorldToMapPos(position, tileR, tileC, cellR, cellC);
        return {tileR * 3 + cellR, tileC * 3 + cellC};
    }
}

namespace
{
    /// <summary>What one mover did in the cells it entered.</summary>
    struct MoverTrack
    {
        /// <summary>The cell it stood on last frame.</summary>
        std::pair<int32_t, int32_t> cell{-1, -1};
        /// <summary>Blocked cells entered in a row (a passable cell resets it).</summary>
        int32_t run = 0;
        /// <summary>The longest such run.</summary>
        int32_t longestRun = 0;
        /// <summary>Every blocked cell entered.</summary>
        int32_t blockedCells = 0;
        /// <summary>Its withdraw flag last frame.</summary>
        bool withdrawing = false;
        /// <summary>The last frames.</summary>
        std::deque<std::string> trace;
        /// <summary>The frames up to the end of the longest run.</summary>
        std::vector<std::string> longestTrace;
    };

    /// <summary>One frame of a mover, for the trace.</summary>
    std::string DescribeMover(MCMover& mover, std::pair<int32_t, int32_t> cell)
    {
        const MCVector3D position = mover.GetPosition();
        std::string line =
            std::format("    t={:.2f} pos ({:.1f},{:.1f}) cell ({},{}) p{}", ScenarioTime, position.X, position.Y,
                        cell.first, cell.second, CellPassable(cell.first, cell.second) ? 1 : 0);

        if (mover.ObjectClass == MCObjectClass::BattleMech)
        {
            auto& actor = static_cast<MCMechActor&>(*mover.Appearance);
            line += std::format(" gesture {}/{} goal {} legs {}", actor.CurrentGesture, actor.CurrentStateGesture,
                                actor.GestureGoal, static_cast<int32_t>(static_cast<MCBattleMech&>(mover).LegStatus));
        }

        std::shared_ptr<MCMechWarrior> pilot = mover.GetPilot();

        if (pilot == nullptr)
        {
            return line;
        }

        const MCMovePath& path = pilot->GetMovePath();
        const int32_t numSteps = path.NumStepsWhenNotPaused;
        const int32_t curStep = path.CurStep;

        if (curStep >= 0 && curStep < numSteps)
        {
            const MCPathStep& step = path.StepList[curStep];
            const int32_t stepRow = step.TileR * 3 + step.CellR;
            const int32_t stepCol = step.TileC * 3 + step.CellC;
            line += std::format(" step {}/{} -> ({},{}) p{} dest ({:.1f},{:.1f}) dir {} facing {:.1f}", curStep,
                                numSteps, stepRow, stepCol, CellPassable(stepRow, stepCol) ? 1 : 0, step.Destination.X,
                                step.Destination.Y, static_cast<int8_t>(step.Direction),
                                mover.RelFacingTo(step.Destination, -1));
        }
        else
        {
            line += std::format(" step {}/{}", curStep, numSteps);
        }

        // MechAIControl::update only moves a mech whose pilot can (not disabled, wounds under 6, status 0..2 or 4).
        line += std::format(" pilot wounds {:.1f} status {} disabled {} awake {}", pilot->Wounds, pilot->Status,
                            mover.IsDisabled(), mover.GetAwake());
        line += std::format(
            " order {} pathType {} withdraw {} moveState {}/{}", static_cast<int32_t>(pilot->CurTacOrder.Code),
            static_cast<int32_t>(pilot->MoveOrders.PathType), mover.Withdrawing,
            std::to_underlying(pilot->MoveOrders.MoveState), std::to_underlying(pilot->MoveOrders.MoveStateGoal));

        if (mover.ObjectClass == MCObjectClass::BattleMech)
        {
            auto& controlData = static_cast<MCMechControlData&>(*mover.Control->ControlData);
            line += std::format(" rotate {} throttle {} pivot {}", static_cast<int32_t>(controlData.Rotate),
                                static_cast<int32_t>(controlData.Throttle), controlData.Pivot);
            // updateMovement's early exits.
            line += std::format(" numSteps {} check {} engineBlow {:.1f} flags {}{}{} captured {} jump {}",
                                path.NumSteps, mover.PilotingCheckPending, mover.EngineBlowTime, mover.DisableThisFrame,
                                mover.ShutDownThisFrame, mover.StartUpThisFrame, mover.IsCaptured(),
                                static_cast<MCBattleMech&>(mover).InJump);
        }

        return line;
    }
}

/// <summary>
/// The bug report: the lance fights mission 1's Uller (part 896); badly hurt, it walked off through forest and water
/// and off the map. With the test's dice its pilot is wounded to 4 mid-stride; the port stopped driving a mech at 3.5
/// wounds (the original's 6.0 misread), so the mech walked on in a straight line. Movers move only along passable
/// cells, so none, the Uller or anyone else, may cross blocked cells. Cutting a corner between two diagonal cells can
/// graze one blocked cell, so a run of one is allowed; two blocked cells in a row is walking through. The camera
/// follows the Uller, as the player did. Before the battle, every mech starts on a passable cell (the game itself puts
/// nothing on forest or water; vehicles may be parked in a building's cells).
/// </summary>
TEST_CASE_ISOLATED("game: mission 1's battle keeps every mover on passable cells")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));

    for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; partId++)
    {
        std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

        if (mover != nullptr && mover->ObjectClass == MCObjectClass::BattleMech)
        {
            MCTest::Scope scope(std::format("start of part {}", partId));
            const auto [row, col] = CellAt(mover->GetPosition());
            CHECK(CellPassable(row, col));
        }
    }

    for (int32_t partId = 0x200; partId < 0x203; partId++)
    {
        REQUIRE(GetMoverFromPartId(partId) != nullptr);
    }

    // The player's attack command, as the interface sends it (icallbk.cpp: attack, any range, pursue).
    std::shared_ptr<MCMover> uller = MCTestGame::OrderMissionOneAttack();
    REQUIRE(uller != nullptr);
    REQUIRE(uller->ObjectClass == MCObjectClass::BattleMech);
    REQUIRE_EQ(uller->GetAlignment(), -1);

    constexpr float FrameSeconds = 1.0f / 15.0f;
    constexpr int32_t MaxFrames = 15 * 60 * 10;
    // After the Uller is gone, the rest of the battle runs on for this long.
    constexpr int32_t AfterFrames = 15 * 120;
    std::map<int32_t, MoverTrack> tracks;
    int32_t framesLeft = MaxFrames;
    bool ullerGone = false;
    // A mover walking through blocked cells soon walks off the map, which the game doesn't survive; the battle stops
    // at the first one.
    bool walkingThrough = false;

    while (framesLeft-- > 0 && !walkingThrough)
    {
        if (!ullerGone && (uller->IsDestroyed() != 0 || uller->GetPilot()->Status == 2))
        {
            ullerGone = true;
            const MCVector3D position = uller->GetPosition();
            std::cout << "  t=" << ScenarioTime << " the Uller is gone at (" << position.X << "," << position.Y
                      << "), damage " << uller->TotalDamageTaken << ", destroyed " << uller->IsDestroyed()
                      << ", withdraw " << uller->Withdrawing << "\n";
            framesLeft = std::min(framesLeft, AfterFrames);
        }

        if (!ullerGone)
        {
            Eye()->SetPosition(uller->GetPosition());
        }

        MCTestGame::RunFrame(FrameSeconds);

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; partId++)
        {
            std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover == nullptr || mover->IsDestroyed() != 0)
            {
                continue;
            }

            MoverTrack& track = tracks[partId];
            const auto cell = CellAt(mover->GetPosition());

            if (mover->Withdrawing != track.withdrawing)
            {
                track.withdrawing = mover->Withdrawing;
                std::cout << "  t=" << ScenarioTime << " part " << partId << " withdraw " << track.withdrawing
                          << ", damage " << mover->TotalDamageTaken << "\n";
            }

            track.trace.push_back(DescribeMover(*mover, cell));

            if (track.trace.size() > 150)
            {
                track.trace.pop_front();
            }

            if (cell == track.cell)
            {
                continue;
            }

            const bool first = track.cell.first < 0;
            track.cell = cell;

            if (first || CellPassable(cell.first, cell.second))
            {
                track.run = 0;
                continue;
            }

            track.blockedCells++;
            track.run++;

            // The frames into the first walk through: up to its second blocked cell.
            if (track.run == 2 && track.longestRun < 2)
            {
                track.longestTrace.assign(track.trace.begin(), track.trace.end());
            }

            if (track.run == 4 && track.longestRun < 4)
            {
                walkingThrough = true;
                std::cout << "  part " << partId << " is walking through blocked cells:\n";

                for (const std::string& line : track.longestTrace)
                {
                    std::cout << line << "\n";
                }

                for (size_t i = track.trace.size() >= 20 ? track.trace.size() - 20 : 0; i < track.trace.size(); i++)
                {
                    std::cout << track.trace[i] << "\n";
                }

                std::cout << std::flush;
            }

            track.longestRun = std::max(track.longestRun, track.run);
        }
    }

    for (const auto& [partId, track] : tracks)
    {
        if (track.blockedCells == 0)
        {
            continue;
        }

        std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
        std::cout << "  part " << partId << " (class " << static_cast<int32_t>(mover->ObjectClass) << ", type "
                  << mover->GetObjectType()->ObjTypeNum << ") entered " << track.blockedCells
                  << " blocked cells, longest run " << track.longestRun << "\n";
        MCTest::Scope scope("part " + std::to_string(partId));
        CHECK(track.longestRun < 2);
    }
}

/// <summary>
/// A pilot is alive and at the controls until 6 wounds (MechWarrior::injure disables the mech at 6; the controls drive
/// it while the pilot has fewer). So a mech whose pilot takes 4 wounds mid-walk still follows its path: it gets to the
/// goal, stops there, and crosses no blocked cells. One of the player's mechs (part 512) walks 600 units, as ordered.
/// </summary>
TEST_CASE_ISOLATED("game: a mech whose pilot is wounded but alive keeps following its path")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::shared_ptr<MCMover> mover = GetMoverFromPartId(0x200);
    REQUIRE(mover != nullptr);
    REQUIRE(mover->ObjectClass == MCObjectClass::BattleMech);
    REQUIRE(mover->IsDestroyed() == 0);
    std::shared_ptr<MCMechWarrior> pilot = mover->GetPilot();
    REQUIRE(pilot->Wounds < 4.0f);

    // A goal 600 units away on a passable cell.
    const MCVector3D start = mover->GetPosition();
    MCVector3D goal = start;
    bool found = false;

    for (int32_t degrees = 0; degrees < 360 && !found; degrees += 15)
    {
        const double radians = degrees * 3.14159265358979 / 180.0;
        goal = MCVector3D(start.X + static_cast<float>(600.0 * std::cos(radians)),
                          start.Y + static_cast<float>(600.0 * std::sin(radians)), start.Z);
        const auto [row, col] = CellAt(goal);
        found = CellPassable(row, col);
    }

    REQUIRE(found);
    pilot->OrderMoveToPoint(0, 1, MCOrderOrigin::Player, goal, -1, 0);

    constexpr float FrameSeconds = 1.0f / 15.0f;

    // Three seconds in, it is walking; then the hit.
    for (int32_t frame = 0; frame < 45; frame++)
    {
        MCTestGame::RunFrame(FrameSeconds);
    }

    REQUIRE_EQ(pilot->Injure(4.0f - pilot->Wounds, 0), 0);
    CHECK_EQ(pilot->Wounds, 4.0f);
    CHECK_EQ(pilot->Status, 0);

    std::pair<int32_t, int32_t> lastCell = CellAt(mover->GetPosition());
    int32_t run = 0;
    int32_t longestRun = 0;
    bool arrived = false;

    for (int32_t frame = 0; frame < 15 * 90 && !arrived; frame++)
    {
        MCTestGame::RunFrame(FrameSeconds);
        const auto cell = CellAt(mover->GetPosition());

        if (cell != lastCell)
        {
            lastCell = cell;
            run = CellPassable(cell.first, cell.second) ? 0 : run + 1;
            longestRun = std::max(longestRun, run);
        }

        // Within a cell of the goal (world units, on the ground).
        const MCVector3D at = mover->GetPosition();
        arrived = std::hypot(at.X - goal.X, at.Y - goal.Y) < 64.0f;
    }

    // And stops there: five seconds on, it hasn't walked past.
    for (int32_t frame = 0; frame < 15 * 5 && arrived; frame++)
    {
        MCTestGame::RunFrame(FrameSeconds);
    }

    const MCVector3D position = mover->GetPosition();
    std::cout << "  from (" << start.X << "," << start.Y << ") to (" << goal.X << "," << goal.Y << "), ended at ("
              << position.X << "," << position.Y << ") at t=" << ScenarioTime << "\n";
    CHECK(arrived);
    CHECK(std::hypot(position.X - goal.X, position.Y - goal.Y) < 64.0f);
    CHECK(longestRun < 2);
}

/// <summary>
/// The stand, walk and run shapes a mission preloads for the player's mechs are the large (90-pixel) art the view
/// draws. (The preload once took the small art, which then stood in for the large shapes in the mech's cache, so
/// those gestures drew at half size, as the Raven did on mission 3.)
/// </summary>
TEST_CASE_ISOLATED("game: mission 3's mechs preload full-size part shapes")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(3));
    // The shape list's part ranges: legs, torso, right arm, left arm (in the part PAKs' order).
    constexpr std::array<uint32_t, 5> partStarts = {PartShapeStart[0], PartShapeStart[1], PartShapeStart[2],
                                                    PartShapeStart[3], TreeShapeCount};
    std::set<std::shared_ptr<MCSpriteTree>> trees;

    for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; partId++)
    {
        std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

        if (mover != nullptr && mover->ObjectClass == MCObjectClass::BattleMech)
        {
            trees.insert(static_cast<MCMechActor*>(mover->Appearance.get())->MechTree);
        }
    }

    REQUIRE(!trees.empty());
    int32_t shapes = 0;

    for (const std::shared_ptr<MCSpriteTree>& tree : trees)
    {
        // Preload again into an empty cache (the shapes stay in the sprite manager, ownerless, as on a tree's end).
        for (std::weak_ptr<MCShape>& slot : tree->ShapeList)
        {
            if (const std::shared_ptr<MCShape> shape = slot.lock())
            {
                shape->Owner.reset();
            }

            slot.reset();
        }

        tree->GesturesPreloaded = false;
        tree->PreloadGestures();

        // Frame 0's bounds (XMin, YMin, XMax, YMax) tell the two sizes apart. Taken before loading anything else,
        // which may push preloaded shapes out of the cache.
        struct Preloaded
        {
            int32_t part;
            uint32_t packet;
            std::array<uint8_t, 16> bounds;
        };

        std::vector<Preloaded> preloaded;

        for (int32_t part = 0; part < 4; part++)
        {
            for (uint32_t i = partStarts[part]; i < partStarts[part + 1]; i++)
            {
                if (const std::shared_ptr<MCShape> shape = tree->ShapeList[i].lock())
                {
                    Preloaded entry{part, i - partStarts[part], {}};
                    std::memcpy(entry.bounds.data(), MCVfxShape(shape->FrameList(), 0).subspan(8, 16).data(), 16);
                    preloaded.push_back(entry);
                }
            }
        }

        for (const Preloaded& entry : preloaded)
        {
            MCTest::Scope scope(std::format("leg file {}, part {}, packet {}", tree->FileNumbers[MCMechPart::Legs],
                                            entry.part, entry.packet));
            const auto part = static_cast<MCMechPart>(entry.part);
            const std::shared_ptr<MCShape> large =
                SpriteManager()->GetMechShapeData(tree->FileNumbers[part], entry.packet, part, Turn, {}, true);
            REQUIRE(large != nullptr);
            CHECK(std::memcmp(entry.bounds.data(), MCVfxShape(large->FrameList(), 0).subspan(8, 16).data(), 16) == 0);
            shapes++;
        }
    }

    std::cout << std::format("  {} mech types, {} preloaded part shapes\n", trees.size(), shapes);
    CHECK(shapes > 0);
}

namespace
{
    /// <summary>Every mech of the mission's parts (those parked too), with its actor.</summary>
    std::vector<std::pair<std::shared_ptr<MCMover>, std::shared_ptr<MCMechActor>>> MissionMechs()
    {
        std::vector<std::pair<std::shared_ptr<MCMover>, std::shared_ptr<MCMechActor>>> mechs;

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; partId++)
        {
            std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover != nullptr && mover->ObjectClass == MCObjectClass::BattleMech)
            {
                mechs.emplace_back(mover, std::static_pointer_cast<MCMechActor>(mover->Appearance));
            }
        }

        return mechs;
    }

    /// <summary>Whether <paramref name="together"/> is the shape <paramref name="alone"/> is: its table, tied to the same HD frames.</summary>
    bool SameShape(const std::shared_ptr<MCShape>& together, const std::shared_ptr<MCShape>& alone)
    {
        return together != nullptr && alone != nullptr &&
               std::ranges::equal(together->FrameList(), alone->FrameList()) && together->HdFolder == alone->HdFolder;
    }
}

/// <summary>
/// Port-only (SPEC-first-use.md: the original's packets at the load): shapes read together (a mech's packets from its
/// parts' PAKs, an explosion's from the sprite PAK) are the shapes read one at a time, tied to the same HD frames.
/// </summary>
TEST_CASE_ISOLATED("game: the original's shapes read together are those read one at a time")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    MCSpriteManager& sprites = *SpriteManager();
    const auto mechs = MissionMechs();
    REQUIRE(!mechs.empty());
    const MCSpriteTree& tree = *mechs.front().second->MechTree;
    std::vector<MCShapeWanted> mechWanted;

    for (const MCGesturePacket& where : tree.PreloadPackets())
    {
        mechWanted.push_back(MCShapeWanted{tree.FileNumbers[where.FilePart], where.Packet, where.FilePart, {}});
    }

    const MCShapesLoaded mechShapes = sprites.GetMechShapesData(mechWanted, Turn, true);
    REQUIRE_EQ(mechShapes.Shapes.size(), mechWanted.size());
    CHECK(mechShapes.Reads < mechWanted.size() / 4);

    for (size_t i = 0; i < mechWanted.size(); ++i)
    {
        const MCShapeWanted& wanted = mechWanted[i];
        MCTest::Scope scope(std::format("part {}, packet {}", std::to_underlying(wanted.Part), wanted.Packet));
        CHECK(SameShape(mechShapes.Shapes[i],
                        sprites.GetMechShapeData(wanted.FileNum, wanted.Packet, wanted.Part, Turn, {}, true)));
    }

    // An explosion's every packet.
    const std::span<const std::shared_ptr<MCObjectType>> types = ObjectTypeManager().Types();
    const auto explosion =
        std::ranges::find_if(types, [](const std::shared_ptr<MCObjectType>& type)
                             { return dynamic_cast<const MCExplosionType*>(type.get()) != nullptr; });
    REQUIRE(explosion != types.end());
    const std::span<const std::shared_ptr<MCAppearanceType>> listed = AppearanceTypeList().Types();
    const auto appearance = std::ranges::find(listed, (*explosion)->AppearName, &MCAppearanceType::AppearanceNum);
    REQUIRE(appearance != listed.end());
    const auto& vfx = static_cast<const MCVfxAppearanceType&>(**appearance);
    std::vector<MCShapeWanted> shapeWanted;

    // The appearance's PAK is the sprite PAK's packet its number's low 24 bits name (MCAppearanceType::ShapeFileNum).
    const uint32_t shapeFile = vfx.AppearanceNum & 0xffffff;

    for (uint32_t packet = 0; packet < vfx.ShapeList.size(); ++packet)
    {
        shapeWanted.push_back(MCShapeWanted{.FileNum = shapeFile, .Packet = packet});
    }

    const MCShapesLoaded shapes = sprites.GetShapesData(shapeWanted, Turn);
    REQUIRE(!shapeWanted.empty());

    for (size_t i = 0; i < shapeWanted.size(); ++i)
    {
        MCTest::Scope scope(std::format("packet {}", shapeWanted[i].Packet));
        const std::shared_ptr<MCShape> alone = sprites.GetShapeData(shapeFile, shapeWanted[i].Packet, Turn, {});
        CHECK((shapes.Shapes[i] == nullptr && alone == nullptr) || SameShape(shapes.Shapes[i], alone));
    }

    std::cout << std::format("  {} mech shapes in {} reads, {} of an explosion's in {}\n", mechWanted.size(),
                             mechShapes.Reads, shapeWanted.size(), shapes.Reads);
}

/// <summary>
/// Port-only (SPEC-first-use.md: the original's packets at the load): a mission's load reads every side's mechs' stand,
/// walk and run at every stored facing and each mech's packets drawn now (the parked parts' too), so that no mech's
/// shape is read at its first draw: none is read one at a time by the load or the mission's first frames.
/// </summary>
TEST_CASE_ISOLATED("game: a mission's load reads every side's mechs' shapes, none read at their first draws")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::set<int32_t> sides;
    std::set<std::shared_ptr<MCSpriteTree>> trees;

    for (const auto& [mover, actor] : MissionMechs())
    {
        sides.insert(mover->Alignment);
        trees.insert(actor->MechTree);

        for (const MCGesturePacket& where : actor->DrawnPackets())
        {
            MCTest::Scope scope(std::format("part {}, packet {}", mover->PartId, where.Packet));
            CHECK(!actor->MechTree->ShapeList[where.ListIndex].expired());
        }
    }

    for (const std::shared_ptr<MCSpriteTree>& tree : trees)
    {
        CHECK(tree->GesturesPreloaded);

        for (const MCGesturePacket& where : tree->PreloadPackets())
        {
            MCTest::Scope scope(std::format("{}, packet {}", tree->Name, where.ListIndex));
            CHECK(!tree->ShapeList[where.ListIndex].expired());
        }
    }

    CHECK(sides.size() >= 2);
    CHECK_EQ(SpriteManager()->ShapesReadAlone().Mechs, 0u);
    std::cout << std::format("  {} mech types, {} sides; {} other shapes read alone\n", trees.size(), sides.size(),
                             SpriteManager()->ShapesReadAlone().Others);
}

/// <summary>
/// Port-only (SPEC-first-use.md: the original's packets at the load): a mission's load holds what its types can make in
/// play, each type's wreck and explosion and theirs, kept for the mission with every shape of their appearances, so
/// that the explosions' shapes outlast MeasureExplosions and none is read as it's first made.
/// </summary>
TEST_CASE_ISOLATED("game: a mission's load keeps the wrecks and explosions its types can make, with their shapes")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    MCObjectTypeManager& manager = ObjectTypeManager();
    size_t wrecks = 0;
    size_t explosions = 0;
    size_t shapes = 0;

    for (const std::shared_ptr<MCObjectType>& type : manager.Types())
    {
        for (const int32_t made : {type->DestroyedObject, type->ExplosionObject})
        {
            if (made > 0)
            {
                MCTest::Scope scope(std::format("type {} makes {}", type->ObjTypeNum, made));
                const std::shared_ptr<MCObjectType> found = manager.Find(made);
                REQUIRE(found != nullptr);
                CHECK(found->KeepMe != 0);
                (made == type->DestroyedObject ? wrecks : explosions)++;
            }
        }

        if (type->KeepMe == 0)
        {
            continue;
        }

        const std::span<const std::shared_ptr<MCAppearanceType>> listed = AppearanceTypeList().Types();
        const auto appearance = std::ranges::find(listed, type->AppearName, &MCAppearanceType::AppearanceNum);

        if (appearance != listed.end())
        {
            MCTest::Scope scope(std::format("type {}'s appearance {:#x}", type->ObjTypeNum, type->AppearName));
            const std::vector<MCShapeWanted> unloaded = (*appearance)->ShapesUnloaded();
            // Only the packets that hold no shape (empty ones) are left.
            CHECK(std::ranges::all_of(
                unloaded, [](const MCShapeWanted& wanted)
                { return SpriteManager()->GetShapeData(wanted.FileNum, wanted.Packet, Turn, {}) == nullptr; }));
            shapes++;
        }
    }

    CHECK(wrecks > 0);
    CHECK(explosions > 0);
    std::cout << std::format("  {} wrecks, {} explosions made, {} kept types' shapes in hand\n", wrecks, explosions,
                             shapes);
}

/// <summary>
/// A scripted reveal's radius is in meters at the mission's scale (its WorldUnitsPerMeter, 5.01 here): mission 3's
/// lookout on the central hill (LOP2 in 1_3LOP.ABI, 430 m) reaches the two Commandos 16 tiles north. (The port once
/// took the default 3.34 units a meter, so every such reveal came out two thirds as wide, here inside what the
/// hilltop already sees.)
/// </summary>
TEST_CASE_ISOLATED("game: mission 3's lookout on the central hill reveals the Commandos")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(3));
    MCTerrain& terrain = *Terrain();
    terrain.ISVisibleBits->ResetAll(false);
    // LOP2's setRevealed(1, 430.0, aPoint).
    terrain.MarkRadiusSeen(MCVector3D(351.0f, 1142.0f, 0.0f), MCVector3D(), 360.0f, 430.0f, 1);

    // The two COM-Ws of MIS0103.fit's Parts 3 and 4, standing at (428, 3110) and (609, 3110).
    int32_t commandos = 0;

    for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; partId++)
    {
        std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

        if (mover != nullptr && mover->Alignment == -1 && std::abs(mover->Position.Y - 3110.0f) < 1.0f)
        {
            CHECK(mover->IsRevealed() != 0);
            commandos++;
        }
    }

    CHECK_EQ(commandos, 2);
}

/// <summary>
/// Port-only (SPEC-first-use.md): a mission's load holds the original's small files it reads all through, each read
/// whole on threads of its own as it opens or the load begins (MCHeldFile): its FastFiles (ART, MISC, MISSION and
/// SHAPES.FST) read none of their own files while it runs (its frame log's note says so), and the type and sprite
/// managers' packet files (sprites.pak, object2.pak and shapes.pak, not shapes90.pak) stay held for the mission, every
/// piece read, their reads from memory.
/// </summary>
TEST_CASE_ISOLATED("game: mission 16's load reads its small original files whole, none of its reads from their disks")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCScratchFolder folder("mc_mission_tests", "load-held");
    const std::filesystem::path log = folder.Path() / "load.framelog";
    REQUIRE(MCFrameLog::Open(log));
    REQUIRE(MCTestGame::StartMission(16));
    // The test game's frames don't move the log's on: its first frame, which the notes so far go to, then written.
    MCFrameLog::NextFrame();
    MCFrameLog::NextFrame();
    MCFrameLog::Close();

    // The managers' files, held for the mission.
    const std::vector<MCHeldFile::MCHeldStats> live = MCHeldFile::Live();

    for (const std::string_view name : {"sprites.pak", "object2.pak", "shapes.pak"})
    {
        MCTest::Scope scope(name);
        const auto held = std::ranges::find_if(
            live, [&](const MCHeldFile::MCHeldStats& stats)
            { return !MCFileSystem::NameLess(stats.Name, name) && !MCFileSystem::NameLess(name, stats.Name); });
        REQUIRE(held != live.end());
        CHECK(held->Done);
        CHECK(!held->Failed);
        CHECK(held->Served > 0);
        std::cout << std::format("  {}: {:.1f} MB, {} reads from it, {} of {} pieces read by an asker\n", held->Name,
                                 held->Bytes / (1024.0 * 1024.0), held->Served, held->PiecesAsked, held->Pieces);
    }

    CHECK_EQ(live.size(), 3u);

    // The load's note: the FastFiles and the managers' files held, and no read of the FastFiles' own.
    std::ifstream in(log);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    const size_t at = text.find("original files held: ");
    REQUIRE(at != std::string::npos);
    size_t files = 0;
    size_t served = 0;
    size_t own = 0;
    const int scanned = std::sscanf(text.c_str() + at,
                                    "original files held: %zu files, %*f MB in %*u pieces, %*[^(](%*f ms), %zu reads "
                                    "from them, %*u pieces read by the load itself, %zu reads of the FastFiles' own",
                                    &files, &served, &own);
    REQUIRE_EQ(scanned, 3);
    CHECK_EQ(files, 7u);
    CHECK(served > 300);
    CHECK_EQ(own, 0u);
    std::cout << "  " << text.substr(at, text.find('\n', at) - at) << "\n";
}

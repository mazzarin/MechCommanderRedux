#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "ai/MCMoveGeometry.h"
#include "ai/MCRefit.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "engine/MCByteFlag.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCBuildingMarines.h"
#include "object/MCExplosionType.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMechControlData.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectType.h"
#include "object/MCObjectTypeManager.h"
#include "object/MCMechWarrior.h"
#include "object/MCForces.h"
#include "object/MCTreeBuilding.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCSpriteManager.h"
#include "sprite/MCSpriteTree.h"
#include "sprite/MCVfxAppearanceType.h"
#include "sprite/MCShape.h"
#include "sound/MCRadio.h"
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
/// Port-only (SPEC-memory.md: the bar ends with the mission's whole art in memory): what a mission makes in play by
/// other than a type's wreck and explosion, a mech's arms thrown off and its jump jets' flames, a vehicle's crew
/// bailing out and a mine's blast (the game's own number), is loaded at the load and kept with its appearance, so the
/// load's fill holds its art. The canyon's cold battle on the A10 (18 Nov) first read five such archives in play.
/// </summary>
TEST_CASE_ISOLATED("game: a mission's load keeps the arms thrown off, the jump jets, the crews and a mine's blast")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const MCObjectTypeManager& manager = ObjectTypeManager();
    const std::span<const std::shared_ptr<MCAppearanceType>> listed = AppearanceTypeList().Types();
    const auto keptWithAppearance = [&](int32_t number)
    {
        MCTest::Scope scope(std::format("type {}", number));
        const std::shared_ptr<MCObjectType> type = manager.Find(number);
        REQUIRE(type != nullptr);
        CHECK(type->KeepMe != 0);
        CHECK(std::ranges::find(listed, type->AppearName, &MCAppearanceType::AppearanceNum) != listed.end());
    };

    size_t arms = 0;
    size_t vehicles = 0;

    for (const std::shared_ptr<MCObjectType>& type : manager.Types())
    {
        if (const auto* mech = dynamic_cast<const MCBattleMechType*>(type.get()); mech != nullptr)
        {
            for (const uint32_t arm : {mech->RightArmDebrisId, mech->LeftArmDebrisId})
            {
                if (static_cast<int32_t>(arm) > 0)
                {
                    keptWithAppearance(static_cast<int32_t>(arm));
                    arms++;
                }
            }
        }
        else if (dynamic_cast<const MCGroundVehicleType*>(type.get()) != nullptr)
        {
            vehicles++;
        }
    }

    CHECK(arms > 0);
    keptWithAppearance(static_cast<int32_t>(MechJumpJetType));
    keptWithAppearance(MineExplosion);

    if (vehicles > 0)
    {
        keptWithAppearance(DefaultPilotId);
    }

    std::cout << std::format("  {} arms, {} vehicle types\n", arms, vehicles);
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

namespace
{
    /// <summary>The seconds a target stands still in the stand-still test (32 of a pilot's combat ticks).</summary>
    constexpr double StandingSeconds = 8.0;

    /// <summary>
    /// The chance <paramref name="shooter"/> has of hitting <paramref name="target"/>, standing still, with
    /// <paramref name="weaponIndex"/> once it has stood <see cref="StandingSeconds"/> at <paramref name="fps"/>
    /// frames a second, checked at each of a pilot's combat ticks (CombatUpdateFrequency) as the pilot's weapons
    /// status checks it; first timing <paramref name="other"/>, so the target's time starts from nothing.
    /// </summary>
    float ChanceAfterStanding(MCMover& shooter, const std::shared_ptr<MCMover>& target,
                              const std::shared_ptr<MCMover>& other, int32_t weaponIndex, int fps)
    {
        const auto chance = [&](const std::shared_ptr<MCMover>& at)
        {
            int32_t range = 0;
            return shooter.CalcAttackChance(at, -1, ScenarioTime, weaponIndex, 0.0f, range, std::nullopt);
        };

        chance(other);
        const float first = chance(target);
        const double frameSeconds = 1.0 / fps;
        double stood = 0.0;
        double nextCheck = CombatUpdateFrequency;
        float last = first;
        int32_t checks = 0;

        // The clock counted here, not ScenarioTime (a float summed every frame), so both rates check as often.
        while (stood + frameSeconds / 2 < StandingSeconds)
        {
            MCTestGame::RunFrame(static_cast<float>(frameSeconds));
            stood += frameSeconds;

            if (stood + frameSeconds / 2 >= nextCheck)
            {
                REQUIRE_EQ(target->GetVelocity().Magnitude(), 0.0);
                last = chance(target);
                checks++;
                nextCheck += CombatUpdateFrequency;
            }
        }

        CHECK_EQ(checks, static_cast<int32_t>(StandingSeconds / CombatUpdateFrequency));
        // It stood: the chance grew.
        CHECK(last > first);
        std::cout << std::format("  {} a second: {:.2f} at first, {:.2f} after {} s\n", fps, first, last,
                                 StandingSeconds);
        return last;
    }
}

/// <summary>
/// OB-193 (fixed): a target holding still gets easier to hit, up to MaxStationaryTime, by the time added at each
/// to-hit check. The original adds the frame's length, and the checks come at the pilot's combat ticks and shots, not
/// every frame, so at 120 frames a second it built 8 times slower than at 15. It builds as the original's does at 15,
/// at any frame rate: mission 1's first mech checking its lancemate, standing beside it, every combat tick for 8
/// seconds.
/// </summary>
TEST_CASE_ISOLATED("game: a target standing still gets easier to hit as fast at 120 frames a second as at 15 (OB-193)")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    constexpr int32_t FirstPlayerPart = 0x200;
    const std::shared_ptr<MCMover> shooter = GetMoverFromPartId(FirstPlayerPart);
    const std::shared_ptr<MCMover> target = GetMoverFromPartId(FirstPlayerPart + 1);
    const std::shared_ptr<MCMover> other = GetMoverFromPartId(FirstPlayerPart + 2);
    REQUIRE(shooter != nullptr);
    REQUIRE(target != nullptr);
    REQUIRE(other != nullptr);

    // A weapon whose chance it rates at that range (none: out of every weapon's range).
    int32_t weaponIndex = -1;

    for (int32_t i = shooter->NumOther; weaponIndex < 0 && i < shooter->NumOther + shooter->NumWeapons; i++)
    {
        int32_t range = 0;
        weaponIndex = shooter->CalcAttackChance(target, -1, ScenarioTime, i, 0.0f, range, std::nullopt) > 0.0f ? i : -1;
    }

    REQUIRE(weaponIndex >= 0);
    constexpr int Locked = 15;
    constexpr int Fast = 120;
    const float locked = ChanceAfterStanding(*shooter, target, other, weaponIndex, Locked);
    const float fast = ChanceAfterStanding(*shooter, target, other, weaponIndex, Fast);

    CHECK_EQ(fast, locked);
}

namespace
{
    /// <summary>Runs up to <paramref name="seconds"/> of 15 Hz frames, until <paramref name="done"/> says so.</summary>
    bool RunUntil(float seconds, const std::function<bool()>& done)
    {
        constexpr int32_t Fps = 15;

        for (int32_t frame = 0; frame < static_cast<int32_t>(seconds * Fps); frame++)
        {
            MCTestGame::RunFrame(1.0f / static_cast<float>(Fps));

            if (done())
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>The player's first mech bay in the mission, or none.</summary>
    std::shared_ptr<MCTreeBuilding> FindHomeMechBay()
    {
        for (const std::shared_ptr<MCObjectList>& list : ObjectList()->Lists())
        {
            for (const std::shared_ptr<MCBaseObject>& object : *list)
            {
                if (object == nullptr || object->ObjectClass != MCObjectClass::TreeBuilding)
                {
                    continue;
                }

                auto building = std::static_pointer_cast<MCTreeBuilding>(object);

                if (building->MechBay && building->GetRefitPoints() > 0.0f &&
                    building->GetAlignment() == HomeTeam()->Alignment)
                {
                    return building;
                }
            }
        }

        return nullptr;
    }

    /// <summary>One of the player's mechs repaired at the home mech bay, starting up again.</summary>
    struct MCRepairedMech
    {
        std::shared_ptr<MCTreeBuilding> Bay;
        std::shared_ptr<MCMover> Mover;
        std::shared_ptr<MCMechWarrior> Pilot;
    };

    /// <summary>
    /// Mission 17: the player's mech (part 513) put in front of the home mech bay with half its armor gone, ordered to
    /// the bay as the interface orders a repair, and run until it starts up again, repaired; with RepairWalkOut as
    /// <paramref name="walkOut"/> says (set once the mission has started, whose start reads the settings).
    /// </summary>
    MCRepairedMech RepairAtHomeBay(bool walkOut)
    {
        REQUIRE(MCTestGame::StartMission(17));
        GRepairWalkOut = walkOut ? 1 : 0;
        REQUIRE(MCRepairWalkOutInForce() == walkOut);
        const std::shared_ptr<MCTreeBuilding> bay = FindHomeMechBay();
        REQUIRE(bay != nullptr);
        const std::shared_ptr<MCMover> mover = GetMoverFromPartId(0x201);
        REQUIRE(mover != nullptr);
        REQUIRE(mover->ObjectClass == MCObjectClass::BattleMech);
        const std::shared_ptr<MCMechWarrior> pilot = mover->GetPilot();
        REQUIRE(pilot != nullptr);

        // In front of the bay (the interface offers a repair within 100 m), on the first open cell below it.
        int32_t bayR = 0;
        int32_t bayC = 0;
        WorldCoordToMapTile(bay->GetPosition(), bayR, bayC);
        MCVector3D front;
        bool open = false;

        for (int32_t down = 2; down < 6 && !open; down++)
        {
            front = MapTileCellToWorldPos(bayR + down, bayC, 1, 1);
            const auto [row, col] = CellAt(front);
            open = CellPassable(row, col);
        }

        REQUIRE(open);
        front.Z = MCTerrain::GetTerrainElevation(front);
        mover->SetPosition(front);
        RunUntil(3.0f, [] { return false; });
        REQUIRE(pilot->CurTacOrder.Code == MCTacticalOrderCode::None);

        for (MCArmorLocation& armor : mover->Armor)
        {
            armor.CurArmor = static_cast<float>(armor.MaxArmor) * 0.5f;
        }

        // The repair, as the interface orders it.
        MCTacticalOrder repair;
        repair.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::GetFixed, 0);
        repair.Target = bay;
        repair.MoveParams.WayPath.Mode[0] = 1;
        REQUIRE(mover->HandleTacticalOrder(repair, 1, 0) == 0);
        constexpr int8_t ShutDown = 5;
        bool shutDown = false;
        const bool poweringUp = RunUntil(60.0f,
                                         [&]
                                         {
                                             shutDown = shutDown || static_cast<int8_t>(mover->Status) == ShutDown;
                                             return shutDown && static_cast<int8_t>(mover->Status) != ShutDown;
                                         });
        REQUIRE(poweringUp);
        CHECK(mover->NeedsRefit(0) == 0);
        CHECK_EQ(pilot->LastMessageType, static_cast<int32_t>(MCRadioMessageType::RefitDone));
        return {bay, mover, pilot};
    }
}

/// <summary>
/// A move given while a repaired mech powers up in its bay runs, but the pilot stays in the alarm state of the power-up
/// (OB-008). When the move ended, the original gave it again from where the mech stood (OB-196): to a spot the mech
/// can't stand on, whose path ends in the open cell nearest it, more than 10 m from the point given, the second try
/// found no step to take and the pilot radioed "I can't get through this way". Mission 17's mech bay repairs one of the
/// player's mechs (part 513), which is then sent to a blocked cell about ten tiles east as it powers up.
/// </summary>
TEST_CASE_ISOLATED("game: a mech sent on as it powers up after a repair doesn't radio a blocked move (OB-196)")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCRepairedMech repaired = RepairAtHomeBay(false);
    const std::shared_ptr<MCMover>& mover = repaired.Mover;
    const std::shared_ptr<MCMechWarrior>& pilot = repaired.Pilot;

    // A blocked cell near an open one ten tiles east.
    int32_t tileR = 0;
    int32_t tileC = 0;
    WorldCoordToMapTile(mover->GetPosition(), tileR, tileC);
    constexpr int32_t Tiles = 10;
    const MCVector3D open10 = MapTileCellToWorldPos(tileR, tileC + Tiles, 1, 1);
    const auto [openRow, openCol] = CellAt(open10);
    REQUIRE(CellPassable(openRow, openCol));
    MCVector3D goal;
    bool blocked = false;

    for (int32_t radius = 1; radius <= 3 && !blocked; radius++)
    {
        for (int32_t dr = -radius; dr <= radius && !blocked; dr++)
        {
            for (int32_t dc = -radius; dc <= radius && !blocked; dc++)
            {
                if (!CellPassable(openRow + dr, openCol + dc))
                {
                    blocked = true;
                    goal = MapCellToWorldPos(openRow + dr, openCol + dc);
                }
            }
        }
    }

    REQUIRE(blocked);
    mover->SelectionIndex = 0;
    const MCWayPathPoint point{goal, false};
    MCTacticalOrder move;
    move.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::MoveToPoint, 0);
    move.InitWayPath(std::span(&point, 1));
    move.MoveParams.Wait = 0;
    move.SelectionIndex = mover->SelectionIndex;
    REQUIRE(mover->HandleTacticalOrder(move, 1, 0) == 0);
    const float ordered = ScenarioTime;
    RunUntil(1.0f, [&] { return pilot->CurTacOrder.Code == MCTacticalOrderCode::MoveToPoint; });
    CHECK(pilot->OrderState == MCOrderState::Alarm);

    // To the end of the move, then 5 seconds more.
    bool radioedBlocked = false;
    auto heardBlocked = [&]
    {
        radioedBlocked =
            radioedBlocked || (pilot->LastMessageType == static_cast<int32_t>(MCRadioMessageType::MoveBlocked) &&
                               pilot->LastMessageTime >= ordered);
        return false;
    };

    const bool ended = RunUntil(90.0f,
                                [&]
                                {
                                    heardBlocked();
                                    return pilot->CurTacOrder.Code == MCTacticalOrderCode::None;
                                });
    REQUIRE(ended);
    RunUntil(5.0f, heardBlocked);

    // The path ended short of the spot given, as the original's second try needed.
    CHECK(mover->DistanceFrom(goal) > MoveMarginOfError[1]);
    CHECK(!radioedBlocked);
    CHECK(pilot->TacOrderOf(MCOrderState::Player).Code == MCTacticalOrderCode::None);
}

namespace
{
    /// <summary>How far past the spot it was repaired on a mech that walked out of its bay is (two cells: ~14 m).</summary>
    constexpr double WalkedOutMeters = 10.0;
    /// <summary>How near the spot it was repaired on a mech that stayed in its bay is.</summary>
    constexpr double StayedMeters = 1.0;
    /// <summary>The time a repaired mech is given to walk out, or to show it stays.</summary>
    constexpr float WalkOutSeconds = 30.0f;
}

/// <summary>
/// Port-only (RepairWalkOut): the original's repair order walks a repaired mech out of its bay, but the original
/// powered it up with an alarm order of the pilot's own, which took over and dropped the walk, so the mech stayed in the
/// bay until ordered out (OB-197). With RepairWalkOut it starts up under the repair order, walks out by itself with no
/// radio of a blocked move, and lets the bay go.
/// </summary>
TEST_CASE_ISOLATED("game: with RepairWalkOut a repaired mech walks out of its bay by itself (OB-197)")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCRepairedMech repaired = RepairAtHomeBay(true);
    const std::shared_ptr<MCMechWarrior>& pilot = repaired.Pilot;
    MCVector3D inBay = repaired.Mover->GetPosition();
    const float repairedAt = ScenarioTime;
    bool radioedBlocked = false;
    const bool done = RunUntil(WalkOutSeconds,
                               [&]
                               {
                                   radioedBlocked =
                                       radioedBlocked || (pilot->LastMessageType ==
                                                              static_cast<int32_t>(MCRadioMessageType::MoveBlocked) &&
                                                          pilot->LastMessageTime >= repairedAt);
                                   return pilot->CurTacOrder.Code == MCTacticalOrderCode::None;
                               });

    CHECK(done);
    CHECK(pilot->OrderState != MCOrderState::Alarm);
    CHECK(repaired.Mover->DistanceFrom(inBay) > WalkedOutMeters);
    CHECK(repaired.Bay->RefitBuddy.lock() == nullptr);
    CHECK(repaired.Mover->RefitBuddy.lock() == nullptr);
    CHECK(!radioedBlocked);
    GRepairWalkOut = 0;
}

/// <summary>Without RepairWalkOut (upstream's default) a repaired mech stays in its bay, as the original's (OB-197).</summary>
TEST_CASE_ISOLATED("game: without RepairWalkOut a repaired mech stays in its bay, as the original's")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCRepairedMech repaired = RepairAtHomeBay(false);
    MCVector3D inBay = repaired.Mover->GetPosition();
    RunUntil(WalkOutSeconds, [] { return false; });
    CHECK(repaired.Mover->DistanceFrom(inBay) < StayedMeters);
}

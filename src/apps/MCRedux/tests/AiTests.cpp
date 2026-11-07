#include "stdafx.h"
#include "MCTest.h"
#include "fixtures/MCTinyMap.h"
#include "ai/MCMoveGeometry.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCTacticalOrder.h"
#include "main/MCGameContext.h"
#include "main/MCMissionGlobals.h"
#include "object/MCBigGameObject.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectQueue.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "terrain/MCTerrain.h"

namespace
{
    /// <summary>A game object that keeps its object map record, as the movers and terrain objects do.</summary>
    class TestObject final : public MCGameObject
    {
    public:
        void SetObjPosition(std::shared_ptr<MCObjectPosition> position) override { _Position = position; }
        std::shared_ptr<MCObjectPosition> GetObjPosition() override { return _Position.lock(); }

    private:
        std::weak_ptr<MCObjectPosition> _Position;
    };

    /// <summary>A game object of no type (so its destructor releases nothing).</summary>
    std::shared_ptr<TestObject> NewObject(MCObjectClass objectClass, int32_t alignment, MCVector3D position = {})
    {
        const auto object = std::make_shared<TestObject>();
        object->ObjectClass = objectClass;
        object->Alignment = alignment;
        object->Position = position;
        return object;
    }

    /// <summary>The move level of a plain cell in these tests (a step's cost; a diagonal costs half again).</summary>
    constexpr int32_t MoveLevel = 10;

    /// <summary>The path finder's window in these tests, in tiles (its top-left tile is the map's).</summary>
    constexpr int32_t WindowTiles = 8;

    /// <summary>
    /// A tiny map with a cell path finder of an 8-tile window and an empty world (no mechs stand anywhere),
    /// searching for a Clan mech, in an object system of its own (empty mech lists) until it goes.
    /// </summary>
    /// <remarks>
    /// The map must have at least as many tiles a side as the window has cells (24): OB-027 reads map tiles at the
    /// window's cell coordinates, which a real map (120 tiles and more) always has.
    /// </remarks>
    class PathWorld
    {
    public:
        explicit PathWorld(int32_t tiles) : Map(tiles)
        {
            Map.System().PathFinder = std::make_unique<MCMoveMap>(WindowTiles, WindowTiles, Map.System().OpenList);
            Search.MovingObject = NewObject(MCObjectClass::BattleMech, -1);
            _PreviousObjects = MCGameContext::Current().SetObjectSystem(std::make_unique<MCObjectSystem>(nullptr));
        }

        ~PathWorld() { MCGameContext::Current().SetObjectSystem(std::move(_PreviousObjects)); }

        PathWorld(const PathWorld&) = delete;
        PathWorld& operator=(const PathWorld&) = delete;

        MCMoveMap& PathFinder() { return *Map.System().PathFinder; }

        /// <summary>A path in the window from cell (startR, startC) to cell (goalR, goalC).</summary>
        int32_t Plan(MCMovePath& path, int32_t startR, int32_t startC, int32_t goalR, int32_t goalC,
                     uint32_t params = 0)
        {
            const MCVector3D start = Map.CellCentre(startR, startC);
            path.Clear();
            PathFinder().SetUp(Search, GameMap(), 0, 0, WindowTiles, WindowTiles, start, startR, startC,
                               Map.CellCentre(goalR, goalC), goalR, goalC, 0, MoveLevel, 0, 8, params);
            std::array<int32_t, 2> goalCell{};
            return PathFinder().CalcPath(Search, GameMap(), path, goalCell);
        }

        MCTinyMap Map;
        /// <summary>The search: for a Clan mech.</summary>
        MCPathSearch Search;

    private:
        std::shared_ptr<MCObjectSystem> _PreviousObjects;
    };

    /// <summary>The map cell of a path step.</summary>
    std::pair<int32_t, int32_t> StepCell(const MCPathStep& step)
    {
        return {step.TileR * MapCellDim + step.CellR, step.TileC * MapCellDim + step.CellC};
    }

    /// <summary>
    /// Why a path isn't a walk from <paramref name="start"/>: a step that is blocked, not next to the one before, or
    /// heading for another cell's centre. Empty when it is one.
    /// </summary>
    std::string CheckWalk(const MCTinyMap& map, const MCMovePath& path, std::pair<int32_t, int32_t> start)
    {
        std::pair<int32_t, int32_t> previous = start;

        for (int32_t i = 0; i < path.NumSteps; i++)
        {
            const MCPathStep& step = path.StepList[static_cast<size_t>(i)];
            const auto [row, col] = StepCell(step);

            if (!map.Passable(row, col))
            {
                return std::format("step {} ({},{}) is blocked", i, row, col);
            }

            if (std::abs(row - previous.first) > 1 || std::abs(col - previous.second) > 1)
            {
                return std::format("step {} ({},{}) is not next to ({},{})", i, row, col, previous.first,
                                   previous.second);
            }

            // The step heads for a point of its own cell (the cell's centre, as the path finder computes it).
            int32_t tileR = 0;
            int32_t tileC = 0;
            int32_t cellR = 0;
            int32_t cellC = 0;
            GameMap().WorldToMapPos(step.Destination, tileR, tileC, cellR, cellC);
            const MCVector3D centre = map.CellCentre(row, col);

            if (tileR * MapCellDim + cellR != row || tileC * MapCellDim + cellC != col ||
                std::abs(step.Destination.X - centre.X) > 0.01f || std::abs(step.Destination.Y - centre.Y) > 0.01f)
            {
                return std::format("step {} ({},{}) heads for ({}, {})", i, row, col, step.Destination.X,
                                   step.Destination.Y);
            }

            previous = {row, col};
        }

        return {};
    }

    /// <summary>MCX.EXE's cellShift table (row, column per offset; 0x007948b0).</summary>
    constexpr int32_t OriginalCellShift[NumCellOffsets * 2] = {
        -1,  0, -1,  1,   0, 1,   1,   1,   1,   0,  1,   -1,  0,  -1,  -1,  -1,  -2,  0, -2,  2,   0, 2,   2,   2,
        2,   0, 2,   -2,  0, -2,  -2,  -2,  -3,  0,  -3,  3,   0,  3,   3,   3,   3,   0, 3,   -3,  0, -3,  -3,  -3,
        -3,  2, -2,  3,   2, 3,   3,   2,   3,   -2, 2,   -3,  -2, -3,  -3,  -2,  -4,  0, -4,  4,   0, 4,   4,   4,
        4,   0, 4,   -4,  0, -4,  -4,  -4,  -5,  0,  -5,  5,   0,  5,   5,   5,   5,   0, 5,   -5,  0, -5,  -5,  -5,
        -6,  0, -6,  6,   0, 6,   6,   6,   6,   0,  6,   -6,  0,  -6,  -6,  -6,  -7,  0, -7,  7,   0, 7,   7,   7,
        7,   0, 7,   -7,  0, -7,  -7,  -7,  -8,  0,  -8,  8,   0,  8,   8,   8,   8,   0, 8,   -8,  0, -8,  -8,  -8,
        -9,  0, -9,  9,   0, 9,   9,   9,   9,   0,  9,   -9,  0,  -9,  -9,  -9,  -10, 0, -10, 10,  0, 10,  10,  10,
        10,  0, 10,  -10, 0, -10, -10, -10, -11, 0,  -11, 11,  0,  11,  11,  11,  11,  0, 11,  -11, 0, -11, -11, -11,
        -12, 0, -12, 12,  0, 12,  12,  12,  12,  0,  12,  -12, 0,  -12, -12, -12,
    };

    /// <summary>MCX.EXE's overlay flag tables (overlayIsBridge, overlayIsClosedGate, overlayIsDirtRoad).</summary>
    constexpr int8_t OriginalIsBridge[NumOverlayTypes] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
    };

    constexpr int8_t OriginalIsClosedGate[NumOverlayTypes] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1,
    };

    constexpr int8_t OriginalIsDirtRoad[NumOverlayTypes] = {
        0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    };
}

TEST_CASE("move tables: the offsets, reverse offsets and overlay flags are MCX.EXE's tables")
{
    for (int32_t i = 0; i < NumCellOffsets; i++)
    {
        MCTest::Scope scope(std::format("offset {}", i));
        CHECK_EQ(CellShiftRow[static_cast<size_t>(i)], OriginalCellShift[i * 2]);
        CHECK_EQ(CellShiftCol[static_cast<size_t>(i)], OriginalCellShift[i * 2 + 1]);
        // The reverse offset steps back to where the offset came from.
        const size_t reverse = static_cast<size_t>(ReverseShift[static_cast<size_t>(i)]);
        CHECK_EQ(CellShiftRow[reverse], -OriginalCellShift[i * 2]);
        CHECK_EQ(CellShiftCol[reverse], -OriginalCellShift[i * 2 + 1]);
        CHECK_EQ(IsDiagonalStep(i), i < 8 && i % 2 == 1);
    }

    for (int32_t overlay = 0; overlay < NumOverlayTypes; overlay++)
    {
        MCTest::Scope scope(std::format("overlay {}", overlay));
        CHECK_EQ(OverlayIsBridge[static_cast<size_t>(overlay)], OriginalIsBridge[overlay] != 0);
        CHECK_EQ(OverlayIsClosedGate[static_cast<size_t>(overlay)], OriginalIsClosedGate[overlay] != 0);
        CHECK_EQ(OverlayIsDirtRoad[static_cast<size_t>(overlay)], OriginalIsDirtRoad[overlay] != 0);
    }
}

TEST_CASE("move map: a straight path steps cell by cell to the goal at a move level a cell")
{
    PathWorld world(24);
    MCMovePath path;
    REQUIRE_EQ(world.Plan(path, 12, 2, 12, 10), 8);
    CHECK_EQ(path.Cost, 8 * MoveLevel);
    CHECK_EQ(CheckWalk(world.Map, path, {12, 2}), std::string());
    const float cellMeters = MetersPerWorldUnit * MCTerrain::MetersPerVertexDivMapcellDim;

    for (int32_t i = 0; i < 8; i++)
    {
        MCTest::Scope scope(std::format("step {}", i));
        const MCPathStep& step = path.StepList[static_cast<size_t>(i)];
        CHECK(StepCell(step) == std::pair(12, 3 + i));
        // East is offset 2; the distance left counts the cells still to go.
        CHECK_EQ(static_cast<int32_t>(step.Direction), 2);
        CHECK(std::abs(step.DistanceToGoal - static_cast<float>(7 - i) * cellMeters) < 0.01f);
    }

    CHECK(path.Goal.X == world.Map.CellCentre(12, 10).X && path.Goal.Y == world.Map.CellCentre(12, 10).Y);
}

TEST_CASE("move map: a diagonal step costs half again, and equal routes come out the same each time")
{
    PathWorld world(24);
    MCMovePath path;
    REQUIRE_EQ(world.Plan(path, 2, 2, 6, 6), 4);
    CHECK_EQ(path.Cost, 4 * (MoveLevel + MoveLevel / 2));

    // Two rows down and eight columns across: two diagonals and six straight steps, in an order the search's ties
    // decide. A fresh path finder makes the same choice.
    REQUIRE_EQ(world.Plan(path, 12, 2, 14, 10), 8);
    CHECK_EQ(path.Cost, 2 * (MoveLevel + MoveLevel / 2) + 6 * MoveLevel);
    std::vector<std::pair<int32_t, int32_t>> first;

    for (int32_t i = 0; i < path.NumSteps; i++)
    {
        first.push_back(StepCell(path.StepList[static_cast<size_t>(i)]));
    }

    world.Map.System().PathFinder = std::make_unique<MCMoveMap>(WindowTiles, WindowTiles, world.Map.System().OpenList);
    world.Search.MovingObject = NewObject(MCObjectClass::BattleMech, -1);
    MCMovePath again;
    REQUIRE_EQ(world.Plan(again, 12, 2, 14, 10), 8);

    for (int32_t i = 0; i < again.NumSteps; i++)
    {
        MCTest::Scope scope(std::format("step {}", i));
        CHECK(StepCell(again.StepList[static_cast<size_t>(i)]) == first[static_cast<size_t>(i)]);
    }
}

TEST_CASE("move map: a path goes around a wall, never cutting a blocked corner")
{
    PathWorld world(24);

    // A wall down column 10, open only in the bottom three rows.
    for (int32_t row = 0; row < 21; row++)
    {
        world.Map.Block(row, 10);
    }

    MCMovePath path;
    const int32_t steps = world.Plan(path, 5, 5, 5, 15);
    REQUIRE(steps > 0);
    CHECK_EQ(CheckWalk(world.Map, path, {5, 5}), std::string());
    CHECK(StepCell(path.StepList[static_cast<size_t>(steps - 1)]) == std::pair(5, 15));
    bool passedBelow = false;

    for (int32_t i = 0; i < steps; i++)
    {
        const auto [row, col] = StepCell(path.StepList[static_cast<size_t>(i)]);
        passedBelow = passedBelow || (col == 10 && row >= 21);
    }

    CHECK(passedBelow);

    // A diagonal between two blocked cells is no way through: (20,9) to (21,10) needs (20,10) or (21,9) open.
    world.Map.Block(21, 9);
    REQUIRE(world.Plan(path, 20, 9, 21, 10) > 0);
    CHECK(StepCell(path.StepList[0]) != std::pair(21, 10));
}

TEST_CASE("move map: an enclosed goal has no path")
{
    PathWorld world(24);

    for (int32_t dr = -1; dr <= 1; dr++)
    {
        for (int32_t dc = -1; dc <= 1; dc++)
        {
            if (dr != 0 || dc != 0)
            {
                world.Map.Block(12 + dr, 12 + dc);
            }
        }
    }

    MCMovePath path;
    CHECK_EQ(world.Plan(path, 3, 3, 12, 12), 0);
    CHECK_EQ(path.NumSteps, 0);
}

TEST_CASE("move path: a path never set has no current destination, and a cleared one keeps its old steps")
{
    // A network client's mech asks for its current step before the server has sent it a path.
    MCMovePath path;
    CHECK(!path.CurrentDestination().has_value());

    path.SetNumSteps(2);
    path.SetDestination(0, MCVector3D(10.0f, 20.0f, 0.0f));
    path.Clear();
    const std::optional<MCVector3D> destination = path.CurrentDestination();
    REQUIRE(destination.has_value());
    CHECK_EQ(destination->X, 10.0f);
    CHECK_EQ(destination->Y, 20.0f);
}

TEST_CASE("path locks: a locked step is set on the map, and a path planned with locks steps around it")
{
    PathWorld world(24);
    MCMovePath path;
    REQUIRE_EQ(world.Plan(path, 12, 2, 12, 10), 8);

    // Lock the first three steps: their cells are locked, the rest aren't.
    path.Lock(-1, 3, 1);
    CHECK(path.IsLocked(0, 3));
    CHECK(!path.IsLocked(3, 5));
    int reachedEnd = 0;
    CHECK(path.IsLocked(0, 20, reachedEnd));
    CHECK_EQ(reachedEnd, 1);
    CHECK(GameMap().GetCellPathLocked(4, 1, 0, 0));
    CHECK(!GameMap().GetCellPathLocked(4, 2, 0, 0));
    path.Lock(-1, 3, 0);
    CHECK(!path.IsLocked(0, 8));

    // A locked cell costs eight move levels more when the path finder counts locks (params 0x80): stepping around it
    // (two diagonals) is cheaper than stepping through.
    GameMap().SetCellPathLocked(4, 2, 0, 0, 1);
    MCMovePath around;
    REQUIRE(world.Plan(around, 12, 2, 12, 10, 0x80) > 0);
    bool throughLock = false;

    for (int32_t i = 0; i < around.NumSteps; i++)
    {
        throughLock = throughLock || StepCell(around.StepList[static_cast<size_t>(i)]) == std::pair(12, 6);
    }

    CHECK(!throughLock);
    CHECK_EQ(around.Cost, 6 * MoveLevel + 2 * (MoveLevel + MoveLevel / 2));

    // Without the flag the lock costs nothing.
    MCMovePath through;
    REQUIRE_EQ(world.Plan(through, 12, 2, 12, 10), 8);
    CHECK_EQ(through.Cost, 8 * MoveLevel);
}

namespace
{
    /// <summary>
    /// A global map computed from a 30-tile map: nine sectors of 10 tiles, all passable (the sector finder's 30-cell
    /// window must fit in the map's tiles, see PathWorld).
    /// </summary>
    struct SectorWorld
    {
        SectorWorld()
            : World(30)
            , SectorFinder(MCGlobalMap::DefaultSectorDim, MCGlobalMap::DefaultSectorDim, World.Map.System().OpenList)
        {
            Map = std::make_unique<MCGlobalMap>(GameMap(), SectorFinder, World.Search, World.Map.System().OpenList);
        }

        PathWorld World;
        MCMoveMap SectorFinder;
        std::unique_ptr<MCGlobalMap> Map;
    };

    /// <summary>The middle cell (row, column) of a door, on the side of <paramref name="area"/>.</summary>
    std::pair<int32_t, int32_t> DoorMiddle(const MCGlobalMapDoor& door, int32_t area)
    {
        const int32_t side = door.Area[1] == area ? 1 : 0;
        const bool eastWest = door.Direction[0] == 1;
        return {door.Row * MapCellDim + door.CellR + (eastWest ? door.Length / 2 : side),
                door.Col * MapCellDim + door.CellC + (eastWest ? side : door.Length / 2)};
    }
}

TEST_CASE("global map: each sector of an open map is an area, joined by edge-long doors, linked at the cells' cost")
{
    SectorWorld world;
    const MCGlobalMap& map = *world.Map;

    // Areas are numbered sector by sector, row by row.
    REQUIRE_EQ(map.NumAreas, 9);
    CHECK_EQ(map.CalcArea(5, 5), 0);
    CHECK_EQ(map.CalcArea(5, 15), 1);
    CHECK_EQ(map.CalcArea(15, 5), 3);
    CHECK_EQ(map.CalcArea(25, 25), 8);
    CHECK_EQ(map.CalcArea(30, 0), -1);

    // A door along each shared sector edge, the whole edge long (30 cells), and the start and goal doors.
    REQUIRE_EQ(map.NumDoors, 12);
    REQUIRE_EQ(static_cast<int32_t>(map.Doors.size()), 14);
    std::set<std::pair<int32_t, int32_t>> joined;

    for (int32_t i = 0; i < map.NumDoors; i++)
    {
        const MCGlobalMapDoor& door = map.Doors[static_cast<size_t>(i)];
        MCTest::Scope scope(std::format("door {}", i));
        CHECK_EQ(static_cast<int32_t>(door.Length), 30);
        joined.insert({door.Area[0], door.Area[1]});
    }

    const std::set<std::pair<int32_t, int32_t>> sectorEdges{{0, 1}, {1, 2}, {3, 4}, {4, 5}, {6, 7}, {7, 8},
                                                            {0, 3}, {1, 4}, {2, 5}, {3, 6}, {4, 7}, {5, 8}};
    CHECK(joined == sectorEdges);

    // A door side links to each other door of its area, at the cost of the cells between their middles: a move level
    // a straight step, half again a diagonal one.
    for (int32_t i = 0; i < map.NumDoors; i++)
    {
        const MCGlobalMapDoor& door = map.Doors[static_cast<size_t>(i)];

        for (size_t side = 0; side < 2; side++)
        {
            const int32_t area = door.Area[side];
            const int32_t areaDoors = map.Areas[static_cast<size_t>(area)].NumDoors;
            MCTest::Scope scope(std::format("door {} side {}", i, side));
            REQUIRE_EQ(static_cast<int32_t>(door.NumLinks[side]), areaDoors - 1);

            for (int32_t l = 0; l < door.NumLinks[side]; l++)
            {
                const MCDoorLink& link = map.DoorLinks[static_cast<size_t>(door.FirstLink[side] + l)];
                const auto [fromRow, fromCol] = DoorMiddle(door, area);
                const auto [toRow, toCol] = DoorMiddle(map.Doors[static_cast<size_t>(link.DoorIndex)], area);
                const int32_t rows = std::abs(toRow - fromRow);
                const int32_t cols = std::abs(toCol - fromCol);
                CHECK_EQ(link.Cost,
                         std::min(rows, cols) * (MoveLevel + MoveLevel / 2) + std::abs(rows - cols) * MoveLevel);
            }
        }
    }
}

TEST_CASE("global map: the door path crosses the areas between, and closed doors end it")
{
    SectorWorld world;
    MCGlobalMap& map = *world.Map;
    std::array<MCGlobalPathStep, MCGlobalMap::MaxPathSteps> path{};

    // Next door: leave area 0 by the door to area 1, then cross area 1 to the goal.
    REQUIRE_EQ(map.CalcPath(0, 1, path), 2);
    CHECK_EQ(path[0].ThruArea, 0);
    CHECK_EQ(path[1].ThruArea, 1);
    const MCGlobalMapDoor& door = map.Doors[static_cast<size_t>(path[0].GoalDoor)];
    CHECK((door.Area[0] == 0 && door.Area[1] == 1));

    // Corner to corner: four doors, five areas crossed, each next to the one before.
    REQUIRE_EQ(map.CalcPath(0, 8, path), 5);
    CHECK_EQ(path[0].ThruArea, 0);
    CHECK_EQ(path[4].ThruArea, 8);

    for (size_t i = 1; i < 5; i++)
    {
        const int32_t from = path[i - 1].ThruArea;
        const int32_t to = path[i].ThruArea;
        MCTest::Scope scope(std::format("step {}", i));
        CHECK_EQ(std::abs(from / 3 - to / 3) + std::abs(from % 3 - to % 3), 1);
    }

    CHECK_EQ(map.CalcPath(-1, 8, path), -1);

    // Close the doors out of area 0: no way out, while area 1 still gets there.
    for (int32_t i = 0; i < map.NumDoors; i++)
    {
        if (map.Doors[static_cast<size_t>(i)].Area[0] == 0)
        {
            map.CloseDoor(i);
        }
    }

    CHECK_EQ(map.CalcPath(0, 8, path), 0);
    CHECK_EQ(map.CalcPath(1, 8, path), 4);
}

TEST_CASE("object map: objects are counted on their tile, follow their moves and leave the map")
{
    MCTinyMap tiny(8);
    tiny.System().ObjectMap = std::make_unique<MCObjectMap>(GameMap());
    MCObjectMap& objects = *tiny.System().ObjectMap;
    const std::shared_ptr<TestObject> mech = NewObject(MCObjectClass::BattleMech, 0, tiny.CellCentre(4, 4));
    const std::shared_ptr<TestObject> tree = NewObject(MCObjectClass::Tree, 0, tiny.CellCentre(5, 3));
    objects.AddObject(*mech);
    objects.AddObject(*tree);

    // Both on tile (1, 1); the tree doesn't block sensors.
    CHECK_EQ(objects.GetNumObjects(1, 1), 2);
    CHECK_EQ(objects.GetNumSensorBlockingObjects(1, 1), 1);
    CHECK_EQ(objects.GetNumObjects(1, 2), 0);
    REQUIRE(mech->GetObjPosition() != nullptr);
    CHECK_EQ(mech->GetObjPosition()->CellR, 1);
    CHECK_EQ(mech->GetObjPosition()->CellC, 1);

    // The mech walks to tile (1, 2).
    mech->Position = tiny.CellCentre(4, 7);
    CHECK(objects.UpdateObject(*mech));
    CHECK_EQ(objects.GetNumObjects(1, 1), 1);
    CHECK_EQ(objects.GetNumObjects(1, 2), 1);
    CHECK_EQ(mech->GetObjPosition()->MapCellR, 4);
    CHECK_EQ(mech->GetObjPosition()->MapCellC, 7);

    // The tree is taken off; then the map goes, clearing the mech's record.
    objects.RemoveObject(*tree);
    CHECK(tree->GetObjPosition() == nullptr);
    CHECK_EQ(objects.GetNumObjects(1, 1), 0);
    tiny.System().ObjectMap.reset();
    CHECK(mech->GetObjPosition() == nullptr);
}

TEST_CASE("move chunk: steps pack into one word and unpack to the same cells")
{
    MCMoveChunk chunk;
    chunk.Reset();
    // Start at tile (10, 20) cell (1, 2), then east, south-east, south.
    chunk.StepPos[0] = {10, 20, 1, 2};
    chunk.StepRelPos = {2, 3, 4};
    chunk.NumSteps = 4;
    chunk.Run = 1;
    const auto mover = std::make_shared<MCMover>();
    chunk.Pack(*mover);
    CHECK_EQ(chunk.Data >> 22, 10u * 3 + 1);
    CHECK_EQ((chunk.Data >> 12) & 0x3ff, 20u * 3 + 2);

    MCMoveChunk received;
    received.Reset();
    received.Data = chunk.Data;
    REQUIRE(received.Unpack(*mover));
    CHECK_EQ(received.NumSteps, 4);
    CHECK_EQ(received.Run, 1);
    CHECK(received.StepPos[0] == (std::array<int32_t, 4>{10, 20, 1, 2}));
    // East of cell (1, 2) is the next tile's cell (1, 0); then south-east, then south.
    CHECK(received.StepPos[1] == (std::array<int32_t, 4>{10, 21, 1, 0}));
    CHECK(received.StepPos[2] == (std::array<int32_t, 4>{10, 21, 2, 1}));
    CHECK(received.StepPos[3] == (std::array<int32_t, 4>{11, 21, 0, 1}));
}

/// <summary>
/// A chunk of fewer than four steps leaves its last step direction unused: the server's word on the move past it rides
/// there (it ends there, or goes on), changing no other bit, so the steps unpack as an older build reads them; an older
/// server's chunk (0 there) carries no word, and a full chunk's last direction is a step's.
/// </summary>
TEST_CASE("move chunk: a chunk with room left carries the server's word on the move past it in its unused direction")
{
    const auto mover = std::make_shared<MCMover>();
    constexpr int32_t East = 2;
    constexpr uint32_t DirectionBits = 7;
    const auto packed = [&](int32_t steps, MCChunkWord word)
    {
        MCMoveChunk chunk;
        chunk.Reset();
        chunk.StepPos[0] = {10, 20, 1, 2};
        chunk.StepRelPos = {};
        std::fill_n(chunk.StepRelPos.begin(), steps - 1, East);
        chunk.NumSteps = steps;
        chunk.Word = word;
        chunk.Pack(*mover);
        return chunk.Data;
    };

    const auto unpacked = [&](uint32_t data)
    {
        MCMoveChunk received;
        received.Reset();
        received.Data = data;
        REQUIRE(received.Unpack(*mover));
        return received;
    };

    for (int32_t steps = 1; steps < MCMoveChunk::MaxSteps; ++steps)
    {
        const uint32_t plain = packed(steps, MCChunkWord::None);
        const MCMoveChunk older = unpacked(plain);
        CHECK(older.Word == MCChunkWord::None);

        for (const MCChunkWord word : {MCChunkWord::EndsHere, MCChunkWord::GoesOn})
        {
            MCTest::Scope scope(std::format("{} steps, word {}", steps, static_cast<int32_t>(word)));
            const uint32_t data = packed(steps, word);
            CHECK_EQ(data & ~DirectionBits, plain & ~DirectionBits);
            const MCMoveChunk received = unpacked(data);
            CHECK(received.Word == word);
            CHECK_EQ(received.NumSteps, steps);
            CHECK(std::equal(received.StepPos.begin(), received.StepPos.begin() + steps, older.StepPos.begin()));
        }
    }

    // A full chunk's last direction is its last step's.
    const MCMoveChunk full = unpacked(packed(MCMoveChunk::MaxSteps, MCChunkWord::GoesOn));
    CHECK(full.Word == MCChunkWord::None);
    CHECK_EQ(full.StepRelPos[MCMoveChunk::LastDirection], East);
}

/// <summary>
/// The direction from a cell to a neighbour, clockwise from north (0) to north-west (7), across tile edges; the cell
/// itself and a cell further away have none (-2).
/// </summary>
TEST_CASE("move geometry: the direction to a neighbouring cell runs clockwise from north, across tile edges")
{
    // From tile (5, 5) cell (1, 1), the middle of its tile.
    const std::array<std::pair<std::pair<int32_t, int32_t>, int32_t>, 8> neighbours = {
        {{{0, 1}, 0}, {{0, 2}, 1}, {{1, 2}, 2}, {{2, 2}, 3}, {{2, 1}, 4}, {{2, 0}, 5}, {{1, 0}, 6}, {{0, 0}, 7}}};

    for (const auto& [cell, direction] : neighbours)
    {
        MCTest::Scope scope(std::format("cell ({}, {})", cell.first, cell.second));
        CHECK_EQ(CellDirToCell(5, 5, 1, 1, 5, 5, cell.first, cell.second), direction);
    }

    CHECK_EQ(CellDirToCell(5, 5, 1, 1, 5, 5, 1, 1), -2);
    // Cell (0, 2) of tile (5, 5): east is tile (5, 6)'s cell (0, 0), north-east tile (4, 6)'s cell (2, 0).
    CHECK_EQ(CellDirToCell(5, 5, 0, 2, 5, 6, 0, 0), 2);
    CHECK_EQ(CellDirToCell(5, 5, 0, 2, 4, 6, 2, 0), 1);
    CHECK_EQ(CellDirToCell(5, 5, 0, 2, 5, 6, 0, 1), -2);
    CHECK_EQ(CellDirToCell(5, 5, 1, 1, 7, 5, 1, 1), -2);
}

/// <summary>
/// A gate (overlays 67-74) behaves by the mover's alignment: alignment -1 sees each gate as its opposite (67 as 71,
/// 71 as 67), alignment 0 sees every gate closed (-1), alignment 1 sees it as it is. Other overlays are themselves for
/// everyone.
/// </summary>
TEST_CASE("move geometry: a gate overlay is open to one side, its opposite to the other")
{
    for (uint32_t gate = 67; gate <= 74; gate++)
    {
        MCTest::Scope scope(std::format("overlay {}", gate));
        const uint32_t opposite = gate < 71 ? gate + 4 : gate - 4;
        CHECK_EQ(GateOverlay(gate, -1), static_cast<int32_t>(opposite));
        CHECK_EQ(GateOverlay(gate, 0), -1);
        CHECK_EQ(GateOverlay(gate, 1), static_cast<int32_t>(gate));
    }

    CHECK_EQ(GateOverlay(66, -1), 66);
    CHECK_EQ(GateOverlay(75, 0), 75);
}

/// <summary>
/// World positions and map cells convert both ways: the map's north-west corner is cell (0, 0), x runs east along the
/// columns and y north against the rows, a tile is three cells a side; a cell's world position is its centre.
/// </summary>
TEST_CASE("move geometry: world positions and map cells convert both ways")
{
    MCTinyMap tiny(8);
    const float cellSide = MCTinyMap::MetersPerTile / MapCellDim;
    const float halfSide = MCTinyMap::MetersPerTile * 8 / 2;

    for (const auto [row, col] : {std::pair(0, 0), std::pair(12, 12), std::pair(5, 17), std::pair(23, 23)})
    {
        MCTest::Scope scope(std::format("cell ({}, {})", row, col));
        const MCVector3D centre(-halfSide + (static_cast<float>(col) + 0.5f) * cellSide,
                                halfSide - (static_cast<float>(row) + 0.5f) * cellSide, 0.0f);
        int32_t cellR = -1;
        int32_t cellC = -1;
        WorldCoordToMapCell(centre, cellR, cellC);
        CHECK_EQ(cellR, row);
        CHECK_EQ(cellC, col);

        int32_t tileR = -1;
        int32_t tileC = -1;
        WorldCoordToMapCoord(centre, tileR, tileC, cellR, cellC);
        CHECK_EQ(tileR, row / MapCellDim);
        CHECK_EQ(tileC, col / MapCellDim);
        CHECK_EQ(cellR, row % MapCellDim);
        CHECK_EQ(cellC, col % MapCellDim);

        const MCVector3D fromCell = MapCellToWorldPos(row, col);
        CHECK(std::abs(fromCell.X - centre.X) < 0.01f);
        CHECK(std::abs(fromCell.Y - centre.Y) < 0.01f);
        const MCVector3D fromTile =
            MapTileCellToWorldPos(row / MapCellDim, col / MapCellDim, row % MapCellDim, col % MapCellDim);
        CHECK(std::abs(fromTile.X - centre.X) < 0.01f);
        CHECK(std::abs(fromTile.Y - centre.Y) < 0.01f);
    }
}

/// <summary>
/// The point a distance away at an angle (0 degrees is south: the reach is negative): an open point is itself; a
/// blocked one is walked back toward the start in steps of a sixth of a tile, and (OB-032) the result is the last
/// blocked point before the walk got out, one step short.
/// </summary>
TEST_CASE("move geometry: a point at an angle and distance walks back out of blocked cells, a step short (OB-032)")
{
    MCTinyMap tiny(8);
    const float savedUnits = WorldUnitsPerMeter;
    WorldUnitsPerMeter = 1.0f;
    const MCVector3D start = tiny.CellCentre(6, 12);

    const MCVector3D open = RelativePositionToPoint(start, 0.0f, 200.0f, 0);
    CHECK(std::abs(open.X - start.X) < 0.01f);
    CHECK(std::abs(open.Y - (start.Y - 200.0f)) < 0.01f);

    // The point lands in cell (10, 12) or near it; block the cells around it, back toward the start.
    for (int32_t row = 9; row <= 12; row++)
    {
        tiny.Block(row, 12);
    }

    const MCVector3D walked = RelativePositionToPoint(start, 0.0f, 200.0f, 0);
    CHECK(std::abs(walked.X - start.X) < 0.01f);
    CHECK(!GameMap().CellPassable(walked));
    const float step = MCTinyMap::MetersPerTile / 6.0f;
    CHECK(GameMap().CellPassable(MCVector3D(walked.X, walked.Y + step, 0.0f)));
    CHECK(walked.Y > start.Y - 200.0f);
    WorldUnitsPerMeter = savedUnits;
}

TEST_CASE("path manager: requests are served by priority, the newest of a priority first, one per pilot")
{
    MCMovePathManager manager;
    std::vector<std::shared_ptr<MCMechWarrior>> pilots;

    for (int32_t i = 0; i < 4; i++)
    {
        pilots.push_back(std::make_shared<MCMechWarrior>());
    }

    manager.Request(*pilots[0], 0, 0, 1.0f, 0);
    manager.Request(*pilots[1], 0, 0, 5.0f, 0);
    manager.Request(*pilots[2], 0, 0, 1.0f, 0);
    manager.Request(*pilots[3], 0, 0, 3.0f, 0);
    auto order = [&manager]
    {
        std::vector<MCAddressKey> pilotsInOrder;

        for (const std::shared_ptr<MCPathQueueRec>& rec : manager.Queue())
        {
            const std::shared_ptr<MCMechWarrior> pilot = rec->Pilot.lock();
            pilotsInOrder.push_back(pilot != nullptr ? MCAddressKey::Of(*pilot) : MCAddressKey());
        }

        return pilotsInOrder;
    };

    const auto keys = [&](std::initializer_list<size_t> indices)
    {
        std::vector<MCAddressKey> result;

        for (const size_t index : indices)
        {
            result.push_back(MCAddressKey::Of(*pilots[index]));
        }

        return result;
    };

    CHECK(order() == keys({1, 3, 2, 0}));

    // A pilot asking again replaces its request.
    manager.Request(*pilots[0], 0, 0, 4.0f, 0);
    CHECK(order() == keys({1, 0, 3, 2}));
    CHECK(pilots[0]->MovePathRequest.lock() == *std::next(manager.Queue().begin()));
    manager.Remove(*pilots[3]);
    CHECK(pilots[3]->MovePathRequest.lock() == nullptr);
    CHECK_EQ(manager.NumPathsInQueue(), 3);

    // An update serves the queue (pilots without a vehicle plan nothing); each served pilot has no request left.
    manager.Update();
    CHECK_EQ(manager.NumPathsInQueue(), 0);

    for (const auto& pilot : pilots)
    {
        CHECK(pilot->MovePathRequest.lock() == nullptr);
    }
}

TEST_CASE("tactical order: an order packs into two words and unpacks to the same order")
{
    MCTinyMap tiny(8);
    MCTacticalOrder order;
    order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::MoveToPoint, 1);
    order.SetWayPoint(0, tiny.CellCentre(7, 11));
    order.MoveParams.WayPath.Mode[0] = 1;
    order.MoveParams.Wait = 1;
    order.AttackParams.Range = 2;
    order.AttackParams.AimLocation = 5;
    order.AttackParams.Method = 1;
    order.SetGroupFlag(3, true);
    order.SetGroupFlag(7, true);
    order.SetGroupFlag(3, false);
    order.PointLocalMoverId = 7;
    order.Pack();

    MCTacticalOrder received;
    received.Data[0] = order.Data[0];
    received.Data[1] = order.Data[1];
    received.Unpack();
    CHECK(received.Code == MCTacticalOrderCode::MoveToPoint);
    CHECK(received.Origin == MCOrderOrigin::Player);
    CHECK(received.IsGroupOrder());
    CHECK_EQ(received.GroupFlags, 1u << 7);
    CHECK_EQ(static_cast<int32_t>(received.PointLocalMoverId), 7);
    CHECK_EQ(static_cast<int32_t>(received.MoveParams.WayPath.Mode[0]), 1);
    CHECK_EQ(received.MoveParams.Wait, 1);
    CHECK_EQ(received.AttackParams.Range, 2);
    CHECK_EQ(received.AttackParams.AimLocation, 5);
    CHECK_EQ(received.AttackParams.Method, 1);
    // The way point travels as its map cell, and comes back as the cell's centre.
    const MCVector3D point = received.GetWayPoint(0);
    const MCVector3D centre = tiny.CellCentre(7, 11);
    CHECK(std::abs(point.X - centre.X) < 0.01f && std::abs(point.Y - centre.Y) < 0.01f);

    // A guard order on a point is code 0x1f on the wire.
    order.Reset(MCOrderOrigin::Commander, MCTacticalOrderCode::Guard, 0);
    order.SetWayPoint(0, tiny.CellCentre(2, 3));
    order.Pack();
    CHECK_EQ(order.Data[0] & 0x1f, 0x1fu);
    received.Data[0] = order.Data[0];
    received.Data[1] = order.Data[1];
    received.Unpack();
    CHECK(received.Code == MCTacticalOrderCode::Guard);
    CHECK(received.Origin == MCOrderOrigin::Commander);
    CHECK(LockObject(received.Target) == nullptr);
}

TEST_CASE("tactical order: the status of the orders that need no world")
{
    const float savedTime = ScenarioTime;
    auto pilot = std::make_unique<MCMechWarrior>();
    MCTacticalOrder order;
    ScenarioTime = 100.0f;

    // A wait lasts until its delay time.
    order.Reset(MCOrderOrigin::Self, MCTacticalOrderCode::Wait, 0);
    order.DelayedTime = 105.0f;
    CHECK(!order.Status(*pilot));
    ScenarioTime = 105.5f;
    CHECK(order.Status(*pilot));

    // Orders that go on until replaced.
    for (const MCTacticalOrderCode code :
         {MCTacticalOrderCode::PatrolPath, MCTacticalOrderCode::Guard, MCTacticalOrderCode::WayPointsDone,
          MCTacticalOrderCode::HoldFire, MCTacticalOrderCode::Withdraw, MCTacticalOrderCode::AttackPoint})
    {
        MCTest::Scope scope(std::format("code {}", static_cast<int32_t>(code)));
        order.Reset(MCOrderOrigin::Self, code, 0);
        CHECK(!order.Status(*pilot));
    }

    // Done at once: a stop, and an order without a status of its own.
    order.Reset(MCOrderOrigin::Self, MCTacticalOrderCode::Stop, 0);
    CHECK(order.Status(*pilot));
    order.Reset(MCOrderOrigin::Self, MCTacticalOrderCode::Eject, 0);
    CHECK(order.Status(*pilot));

    // Staged orders: a jump is done at stage 3, a traverse at stage 2, a refused capture or deployment at once.
    order.Reset(MCOrderOrigin::Self, MCTacticalOrderCode::JumpToPoint, 0);
    CHECK(!order.Status(*pilot));
    order.Stage = 3;
    CHECK(order.Status(*pilot));
    order.Reset(MCOrderOrigin::Self, MCTacticalOrderCode::TraversePath, 0);
    CHECK(!order.Status(*pilot));
    order.Stage = 2;
    CHECK(order.Status(*pilot));
    order.Reset(MCOrderOrigin::Self, MCTacticalOrderCode::Capture, 0);
    order.Stage = MCTacticalOrder::StageDone;
    CHECK(order.Status(*pilot));

    // An attack on nothing is over.
    order.Reset(MCOrderOrigin::Self, MCTacticalOrderCode::AttackObject, 0);
    CHECK(order.Status(*pilot));
    ScenarioTime = savedTime;
}

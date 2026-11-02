#include "stdafx.h"
#include "ai/MCMoveMap.h"
#include "ai/MCMoveSystem.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCPriorityQueue.h"
#include "mission/MCScenario.h"
#include "object/MCBigGameObject.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectQueue.h"
#include "object/MCMechWarrior.h"

namespace
{
    /// <summary>The "no position" value the path finder stores (0xc97423f0).</summary>
    constexpr float NoPosition = -999999.0f;

    /// <summary>
    /// The weights ClearBridgeTiles lowers by 10000 in the first move level of the overlay weights (the centre
    /// column of the two bridge overlays' cells, and of the two railroad bridges').
    /// </summary>
    constexpr int32_t BridgeWeightCells[12] = {334, 337, 340, 352, 355, 358, 496, 499, 502, 514, 517, 520};

    /// <summary>Adds <paramref name="delta"/> to the bridge cells of overlay weight level <paramref name="level"/>.</summary>
    void AdjustBridgeWeights(int32_t level, int32_t delta)
    {
        for (const int32_t cell : BridgeWeightCells)
        {
            OverlayWeightTable[static_cast<size_t>(level * OverlayWeightLevelSize + cell)] += delta;
        }
    }

    /// <summary>A cost plus <paramref name="delta"/>, at least 1.</summary>
    int32_t AddCost(int32_t cost, int32_t delta)
    {
        return std::max(cost + delta, 1);
    }

    /// <summary>The open-list id of a window cell.</summary>
    int32_t CellId(int32_t r, int32_t c)
    {
        return c + r * 1000;
    }

    /// <summary>The known-mine bits of a tile's overlay for a side (Inner Sphere -1: bits 25-26, Clan: 27-28).</summary>
    uint32_t KnownMines(const MCMapTile& tile, int32_t alignment)
    {
        return (tile.Overlay >> (alignment == -1 ? 25 : 27)) & 3;
    }
}

auto DebugOpenList(const MCPriorityQueue& openList, std::string_view message,
                   std::shared_ptr<const MCGameObject> movingObject) -> void
{
    MCFile debugFile;
    debugFile.Create("openlist.dbg");
    debugFile.WriteString(message);

    if (movingObject != nullptr)
    {
        debugFile.WriteString(std::format(
            "MovingObject = {} [{}]\n", static_cast<const MCMover&>(*movingObject).DebugStatus, movingObject->PartId));

        if (movingObject->ObjectClass == MCObjectClass::Elemental)
        {
            debugFile.WriteString("Is an elemental!\n");
        }
    }

    debugFile.WriteString("\nOPENLIST INFO\n");
    debugFile.WriteString(std::format("NumItems = {}\n", openList.Size()));

    // As the original: items are read from slot 0 (the sentinel) up, so the last item is left out.
    for (int32_t i = 0; i < openList.Size(); i++)
    {
        const MCPQNode& item = openList.GetItem(i);
        debugFile.WriteString(std::format("Item: {:04}\n     key: {}\n      id: {}\n     row: {}\n     col: {}\n", i,
                                          item.Key, item.Id, item.Row, item.Col));
    }

    debugFile.Close();
}

MCMoveMap::MCMoveMap(int32_t maxWidth, int32_t maxHeight, std::shared_ptr<MCPriorityQueue> openList)
    : MaxWidth(maxWidth)
    , MaxHeight(maxHeight)
    , MaxCellWidth(maxWidth * MapCellDim)
    , MaxCellHeight(maxHeight * MapCellDim)
    , Width(maxWidth)
    , Height(maxHeight)
    , CellWidth(maxWidth * MapCellDim)
    , CellHeight(maxHeight * MapCellDim)
    , _Map(static_cast<size_t>(MaxCellHeight * MaxCellWidth))
    , _OpenList(std::move(openList))
{
    Clear();
}

auto MCMoveMap::Clear() -> void
{
    const int32_t numCells = CellHeight * MaxCellWidth;

    for (int32_t i = 0; i < numCells; i++)
    {
        MCMoveMapNode& node = _Map[static_cast<size_t>(i)];
        node.Parent = -1;
        node.Flags = 0;
        node.HPrime = -1;
    }

    GoalPos = MCVector3D(0.0f, 0.0f, 0.0f);
    Target = MCVector3D(NoPosition, NoPosition, NoPosition);
}

auto MCMoveMap::IsBlockingMover(const MCPathSearch& search, MCGameObject& object) -> bool
{
    return object.ObjectClass != MCObjectClass::Elemental && &object != search.MovingObject.get() &&
           &object != search.RamObject.get() && object.IsDisabled() == 0;
}

auto MCMoveMap::PlaceMovers(const MCPathSearch& search, const MCScenarioMap& map) -> void
{
    auto placeList = [this, &search, &map](const MCObjectList& list, int32_t bridgeCost)
    {
        for (std::shared_ptr<MCBaseObject> current : list)
        {
            auto& object = static_cast<MCGameObject&>(*current);

            if (!IsBlockingMover(search, object))
            {
                continue;
            }

            const MCObjectPosition& position = *object.GetObjPosition();
            const int32_t r = (position.TileR - ULr) * 3 + position.CellR;
            const int32_t c = (position.TileC - ULc) * 3 + position.CellC;

            if (r < 0 || r >= CellHeight || c < 0 || c >= CellWidth || (r == StartR && c == StartC))
            {
                continue;
            }

            MCMoveMapNode& node = NodeAt(r, c);

            if ((node.Flags & GoalFlag) != 0)
            {
                continue;
            }

            if (object.GetPilot()->GetMovePath().NumSteps != 0)
            {
                continue;
            }

            node.Flags |= MoverFlag;
            const uint32_t overlay = map.TileAt(position.TileR, position.TileC).OverlayType();
            node.Cost = AddCost(node.Cost, OverlayIsBridge[overlay] ? bridgeCost : ClosedCost);
        }
    };

    // Original behaviour (OB-030): Inner Sphere mechs standing on a bridge cost 3333 to pass, Clan ones nothing extra.
    placeList(*InnerSphereMechList(), 0xd05);
    placeList(*ClanMechList(), 0);
}

auto MCMoveMap::SetStart(std::optional<MCVector3D> startPos, int32_t startR, int32_t startC) -> void
{
    StartPos = startPos.value_or(MCVector3D(NoPosition, NoPosition, NoPosition));

    if (startR == -1)
    {
        int32_t tileR = 0;
        int32_t tileC = 0;
        int32_t cellR = 0;
        int32_t cellC = 0;
        MCScenarioMap::WorldToMapPos(*startPos, tileR, tileC, cellR, cellC);
        StartR = (tileR - ULr) * 3 + cellR;
        StartC = (tileC - ULc) * 3 + cellC;
        return;
    }

    StartR = startR;
    StartC = startC;
}

auto MCMoveMap::SetGoal(MCVector3D goalPos, int32_t goalR, int32_t goalC) -> void
{
    GoalPos = goalPos;

    if (goalR == -1)
    {
        int32_t tileR = 0;
        int32_t tileC = 0;
        int32_t cellR = 0;
        int32_t cellC = 0;
        MCScenarioMap::WorldToMapPos(GoalPos, tileR, tileC, cellR, cellC);
        GoalR = (tileR - ULr) * 3 + cellR;
        GoalC = (tileC - ULc) * 3 + cellC;
    }
    else
    {
        GoalR = goalR;
        GoalC = goalC;
    }

    DoorDirection = -1;
}

auto MCMoveMap::SetGoal(int32_t thruArea, int32_t goalDoor) -> void
{
    GoalPos = MCVector3D(NoPosition, NoPosition, NoPosition);
    Door = goalDoor;
    // Per door direction (1 east-west, 2 north-south) and side: the direction the door is entered from.
    static constexpr int32_t entryDirection[8] = {-1, -1, 1, 3, 2, 0, -1, -1};
    const MCGlobalMapDoor& goal = GlobalMoveMap().Doors[static_cast<size_t>(goalDoor)];
    DoorSide = goal.Area[1] == thruArea ? 1 : 0;
    const int32_t direction = goal.Direction[0];
    Assert(direction == 1 || direction == 2, 0, " MoveMap: Bad Area Door Direction in setGoal() ");
    DoorDirection = entryDirection[DoorSide + direction * 2];

    if (DoorDirection == 0 || DoorDirection == 2)
    {
        GoalR = goal.Row * 3 + DoorSide + (goal.CellR - MinRow);
        GoalC = ((goal.Col * 3 + goal.CellC) - MinCol) + goal.Length / 2;
    }
    else if (DoorDirection == 1 || DoorDirection == 3)
    {
        GoalR = (goal.CellR - MinRow) + goal.Row * 3 + goal.Length / 2;
        GoalC = ((goal.Col * 3 + goal.CellC) - MinCol) + DoorSide;
    }
}

auto MCMoveMap::BeginSetUp(int32_t uLr, int32_t uLc, int32_t height, int32_t width, std::optional<MCVector3D> startPos,
                           int32_t startR, int32_t startC, int32_t overlayLevel, int32_t moveLevel, int32_t jumpCost,
                           int32_t numOffsets) -> void
{
    Assert(width <= MaxWidth && height <= MaxHeight, 0, " MoveMap: window larger than the map ");
    Width = width;
    Height = height;
    CellWidth = width * 3;
    CellHeight = height * 3;
    Clear();
    ULr = uLr;
    ULc = uLc;
    MinCol = uLc * 3;
    MinRow = uLr * 3;
    OverlayLevel = overlayLevel;
    MoveLevel = moveLevel;
    JumpCost = jumpCost;
    NumOffsets = numOffsets;
    SetStart(startPos, startR, startC);
}

auto MCMoveMap::SetTileCosts(const MCPathSearch& search, const MCMapTile& tile, int32_t row, int32_t col) -> void
{
    auto node = [&](int32_t cell) -> MCMoveMapNode& { return NodeAt(row * 3 + cell / 3, col * 3 + cell % 3); };

    for (int32_t cell = 0; cell < MapCellDim * MapCellDim; cell++)
    {
        node(cell).Cost = tile.GetCellPassable(cell / 3, cell % 3) != 0 ? MoveLevel : BlockedCost;
    }

    const uint32_t overlay = tile.OverlayType();

    if (overlay == 0)
    {
        return;
    }

    const int32_t weightOverlay = IsGateOverlay(overlay) ? GateOverlay(overlay, search.MovingObject->GetAlignment())
                                                         : static_cast<int32_t>(overlay);

    for (int32_t cell = 0; cell < MapCellDim * MapCellDim; cell++)
    {
        const int32_t weight = weightOverlay == -1
                                   ? ClosedCost
                                   : OverlayWeightTable[static_cast<size_t>(OverlayLevel * OverlayWeightLevelSize +
                                                                            OverlayWeightIndex(weightOverlay) + cell)];
        node(cell).Cost = AddCost(node(cell).Cost, weight);
    }
}

auto MCMoveMap::SetUp(const MCPathSearch& search, const MCScenarioMap& map, int32_t uLr, int32_t uLc, int32_t height,
                      int32_t width, std::optional<MCVector3D> startPos, int32_t startR, int32_t startC,
                      MCVector3D goalPos, int32_t goalR, int32_t goalC, int32_t overlayLevel, int32_t moveLevel,
                      int32_t jumpCost, int32_t numOffsets, uint32_t params) -> void
{
    BeginSetUp(uLr, uLc, height, width, startPos, startR, startC, overlayLevel, moveLevel, jumpCost, numOffsets);
    SetGoal(goalPos, goalR, goalC);

    if (search.ClearBridgeTiles)
    {
        AdjustBridgeWeights(OverlayLevel, -10000);
    }

    const std::shared_ptr<MCGameObject> movingObject = search.MovingObject;
    const int32_t lockCost = MoveLevel << 3;
    const bool checkMines = !(movingObject != nullptr && movingObject->ObjectClass == MCObjectClass::GroundVehicle &&
                              static_cast<const MCGroundVehicle&>(*movingObject).MineSweeper != 0);

    for (int32_t row = 0; row < Height; row++)
    {
        for (int32_t col = 0; col < Width; col++)
        {
            const int32_t tileR = ULr + row;
            const int32_t tileC = ULc + col;

            if (!map.OnMap(tileR, tileC))
            {
                continue;
            }

            const MCMapTile& tile = map.TileAt(tileR, tileC);
            SetTileCosts(search, tile, row, col);
            auto node = [&](int32_t cell) -> MCMoveMapNode& { return NodeAt(row * 3 + cell / 3, col * 3 + cell % 3); };
            uint32_t locks = (tile.Overlay >> 15) & 0x1ff;

            if (locks != 0 && (params & 0x80) != 0)
            {
                for (int32_t cell = 0; cell < MapCellDim * MapCellDim; cell++, locks >>= 1)
                {
                    if ((locks & 1) != 0)
                    {
                        node(cell).Cost = AddCost(node(cell).Cost, lockCost);
                    }
                }
            }

            if (checkMines)
            {
                const int32_t alignment = movingObject->GetAlignment();
                const uint32_t knownMines = KnownMines(tile, alignment);

                if (knownMines != 0)
                {
                    for (int32_t cell = 0; cell < MapCellDim * MapCellDim; cell++)
                    {
                        node(cell).Cost = AddCost(node(cell).Cost, MoveLevel << knownMines);
                    }
                }

                // The side's own mines draw it (so that a mine layer re-walks its fields).
                const uint32_t ownMines = (tile.Overlay >> (alignment == -1 ? 11 : 13)) & 3;

                if (ownMines == 3 || ownMines == 1)
                {
                    for (int32_t cell = 0; cell < MapCellDim * MapCellDim; cell++)
                    {
                        node(cell).Cost = AddCost(node(cell).Cost, (MoveLevel << ownMines) * -2);
                    }
                }
            }

            // A mine layer laying mines is drawn to the middle of each tile.
            if (movingObject->ObjectClass == MCObjectClass::GroundVehicle)
            {
                const auto& vehicle = static_cast<const MCGroundVehicle&>(*movingObject);

                if (vehicle.MineLayer != 0 && vehicle.Pilot.lock()->CurTacOrder.MoveParams.Mode == 1)
                {
                    node(4).Cost = AddCost(node(4).Cost, MoveLevel * -16);
                }
            }
        }
    }

    if (!search.FindingEscapePath)
    {
        // Port fix: the original marks a goal outside the window too, writing outside map, before SearchPath stops
        // on it (" Bad Move Goal "). The port leaves the mark out so that Fatal is what reports it.
        if (GoalR >= 0 && GoalR < CellHeight && GoalC >= 0 && GoalC < CellWidth)
        {
            NodeAt(GoalR, GoalC).Flags |= GoalFlag;
        }
    }
    else
    {
        MarkEscapeGoalCells(map, goalPos);
    }

    if ((params & 0x40) != 0)
    {
        PlaceMovers(search, map);
    }
}

auto MCMoveMap::MarkEscapeGoalCells(const MCScenarioMap& map, MCVector3D escapeGoal) -> void
{
    const MCGlobalMap& globalMap = GlobalMoveMap();
    int32_t goalTileR = 0;
    int32_t goalTileC = 0;
    int32_t goalCellR = 0;
    int32_t goalCellC = 0;
    MCScenarioMap::WorldToMapPos(escapeGoal, goalTileR, goalTileC, goalCellR, goalCellC);
    const int32_t goalArea = globalMap.CalcArea(goalTileR, goalTileC);

    for (int32_t row = 0; row < Height; row++)
    {
        for (int32_t col = 0; col < Width; col++)
        {
            const int32_t tileR = ULr + row;
            const int32_t tileC = ULc + col;

            if (!map.OnMap(tileR, tileC))
            {
                continue;
            }

            const int32_t area = globalMap.CalcArea(tileR, tileC);

            if (area != goalArea && globalMap.GetPathCost(area, goalArea) <= 0)
            {
                continue;
            }

            for (int32_t cell = 0; cell < MapCellDim * MapCellDim; cell++)
            {
                NodeAt(row * 3 + cell / 3, col * 3 + cell % 3).Flags |= GoalFlag;
            }
        }
    }
}

auto MCMoveMap::SetUp(const MCPathSearch& search, const MCScenarioMap& map, int32_t uLr, int32_t uLc, int32_t height,
                      int32_t width, std::optional<MCVector3D> startPos, int32_t startR, int32_t startC,
                      int32_t thruArea, int32_t goalDoor, MCVector3D targetPos, int32_t overlayLevel, int32_t moveLevel,
                      int32_t jumpCost, int32_t numOffsets, uint32_t params) -> bool
{
    BeginSetUp(uLr, uLc, height, width, startPos, startR, startC, overlayLevel, moveLevel, jumpCost, numOffsets);
    SetGoal(thruArea, goalDoor);

    if (search.ClearBridgeTiles)
    {
        AdjustBridgeWeights(OverlayLevel, -10000);
    }

    for (int32_t row = 0; row < Height; row++)
    {
        for (int32_t col = 0; col < Width; col++)
        {
            const int32_t tileR = ULr + row;
            const int32_t tileC = ULc + col;

            if (!map.OnMap(tileR, tileC))
            {
                continue;
            }

            const MCMapTile& tile = map.TileAt(tileR, tileC);
            SetTileCosts(search, tile, row, col);
            auto node = [&](int32_t cell) -> MCMoveMapNode& { return NodeAt(row * 3 + cell / 3, col * 3 + cell % 3); };

            // Unlike the other SetUp: a lock costs MoveLevel (not 8 x), and only the known mines count.
            uint32_t locks = (tile.Overlay >> 15) & 0x1ff;

            if (locks != 0 && (params & 0x80) != 0)
            {
                for (int32_t cell = 0; cell < MapCellDim * MapCellDim; cell++, locks >>= 1)
                {
                    if ((locks & 1) != 0)
                    {
                        node(cell).Cost = AddCost(node(cell).Cost, MoveLevel);
                    }
                }
            }

            const uint32_t knownMines = KnownMines(tile, search.MovingObject->GetAlignment());

            if (knownMines != 0)
            {
                for (int32_t cell = 0; cell < MapCellDim * MapCellDim; cell++)
                {
                    node(cell).Cost = AddCost(node(cell).Cost, MoveLevel << knownMines);
                }
            }
        }
    }

    if (MarkGoalCells(search, targetPos) == 0)
    {
        return false;
    }

    if ((params & 0x40) != 0)
    {
        PlaceMovers(search, map);
    }

    return true;
}

auto MCMoveMap::MarkGoalCells(const MCPathSearch& search, MCVector3D targetPos) -> int32_t
{
    const MCGlobalMap& globalMap = GlobalMoveMap();
    const MCGlobalMapDoor& goal = globalMap.Doors[static_cast<size_t>(Door)];
    const int32_t length = goal.Length;
    _DoorCellFree.assign(static_cast<size_t>(std::max(length, 0)), true);
    Assert(Door >= 0 && Door < globalMap.NumDoors, 0, " FUDGE 1");
    Assert(goal.Direction[0] == 1 || goal.Direction[0] == 2, 0, " FUDGE 2");
    // The original also asserted Length <= 0x3ff, which its one-byte length always is.
    Assert(goal.Length >= 1, 0, " FUDGE 3");

    // Clears the door cells a standing mech occupies (on either side of the door).
    const bool alongRows = goal.Direction[0] == 1;
    const int32_t doorRow = goal.Row * MapCellDim + goal.CellR;
    const int32_t doorCol = goal.Col * MapCellDim + goal.CellC;
    auto clearOccupied = [&](const MCObjectList& list)
    {
        for (std::shared_ptr<MCBaseObject> current : list)
        {
            auto& object = static_cast<MCGameObject&>(*current);

            if (!IsBlockingMover(search, object))
            {
                continue;
            }

            const MCObjectPosition& position = *object.GetObjPosition();
            const int32_t along = alongRows ? position.MapCellR - doorRow : position.MapCellC - doorCol;
            const int32_t across = alongRows ? position.MapCellC - doorCol : position.MapCellR - doorRow;

            if (along < 0 || along >= length || across < 0 || across >= 2)
            {
                continue;
            }

            Assert(along >= 0 && along < length, 0, " Bad Cell Index ");
            _DoorCellFree[static_cast<size_t>(along)] = false;
        }
    };

    clearOccupied(*InnerSphereMechList());
    clearOccupied(*ClanMechList());

    int32_t tileR = 0;
    int32_t tileC = 0;
    int32_t cellR = 0;
    int32_t cellC = 0;
    MCScenarioMap::WorldToMapPos(targetPos, tileR, tileC, cellR, cellC);
    const int32_t targetR = cellR + (tileR * 3 - MinRow);
    const int32_t targetC = cellC + (tileC * 3 - MinCol);
    int32_t numMarked = 0;
    const int32_t half = length / 2;
    const int32_t step = MoveLevel / 2;

    if (DoorDirection == 0 || DoorDirection == 2)
    {
        // The door runs along goal row GoalR; the target just past it is the only goal.
        const int32_t firstCol = GoalC - half;
        const int32_t beyondRow = DoorSide == 0 ? GoalR + 1 : GoalR - 1;

        if (targetR == beyondRow && targetC >= firstCol && targetC < firstCol + length)
        {
            NodeAt(GoalR, targetC).Flags |= GoalFlag;
            return 1;
        }

        // Else every free door cell is a goal, costing more the further from the middle.
        for (int32_t i = 0; i < length; i++)
        {
            if (!_DoorCellFree[static_cast<size_t>(i)])
            {
                continue;
            }

            MCMoveMapNode& node = NodeAt(GoalR, i + firstCol);
            node.Flags |= GoalFlag;
            numMarked++;
            node.Cost = AddCost(node.Cost, std::abs(i - half) * step);
        }
    }
    else if (DoorDirection == 1 || DoorDirection == 3)
    {
        const int32_t firstRow = GoalR - half;

        if (targetC == GoalC + DoorSide * -2 + 1 && targetR >= firstRow && targetR < length + firstRow)
        {
            NodeAt(targetR, GoalC).Flags |= GoalFlag;
            return 1;
        }

        for (int32_t i = 0; i < length; i++)
        {
            if (!_DoorCellFree[static_cast<size_t>(i)])
            {
                continue;
            }

            MCMoveMapNode& node = NodeAt(firstRow + i, GoalC);
            node.Flags |= GoalFlag;
            numMarked++;
            node.Cost = AddCost(node.Cost, std::abs(i - half) * step);
        }
    }

    return numMarked;
}

auto MCMoveMap::AdjacentCellOpen(const MCPathSearch& search, const MCScenarioMap& map, int32_t r, int32_t c,
                                 int32_t dir) const -> bool
{
    const int32_t nextR = CellShiftRow[static_cast<size_t>(dir)] + r;
    const int32_t nextC = CellShiftCol[static_cast<size_t>(dir)] + c;

    if (nextR < 0 || nextR >= CellHeight || nextC < 0 || nextC >= CellWidth)
    {
        return false;
    }

    const MCMoveMapNode& node = NodeAt(nextR, nextC);

    if ((node.Flags & MoverFlag) != 0)
    {
        return false;
    }

    // Original behaviour (OB-027): the known-mine bits are read from the map tile at the window cell's coordinates.
    const uint32_t mineBits = search.MovingObject->GetAlignment() == -1 ? 0x6000000u : 0x18000000u;
    Assert(map.OnMap(nextR, nextC), 0, " Map Tile out of bounds ");

    if ((map.TileAt(nextR, nextC).Overlay & mineBits) != 0)
    {
        return false;
    }

    return node.Cost < BlockedCost;
}

auto MCMoveMap::StepCost(const MCPathSearch& search, int32_t cellCost, int32_t offset) const -> int32_t
{
    if (offset < 8)
    {
        return IsDiagonalStep(offset) ? cellCost + cellCost / 2 : cellCost;
    }

    return search.JumpOnBlocked ? JumpCost : cellCost + JumpCost;
}

auto MCMoveMap::PropogateCost(const MCPathSearch& search, const MCScenarioMap& map, int32_t r, int32_t c, int32_t cost,
                              int32_t g) -> void
{
    Assert(cost > 0, 0, " MoveMap.propogateCost: bad cost ");

    if (g < 0)
    {
        Fatal(0, "Negative g-cost in MoveMap");
    }

    MCMoveMapNode& current = NodeAt(r, c);
    const int32_t newG = g + cost;

    if (newG >= current.G)
    {
        return;
    }

    // Original behaviour (OB-028): the cell's own cost is replaced by the step cost (diagonal and jump extras
    // included), so later searches through it see the inflated cost.
    current.Cost = cost;
    current.G = newG;
    current.FPrime = current.HPrime + newG;

    if ((current.Flags & OpenFlag) != 0)
    {
        const int32_t id = CellId(r, c);
        const int32_t itemIndex = _OpenList->Find(id);

        if (itemIndex != 0)
        {
            _OpenList->Change(itemIndex, current.FPrime);
            return;
        }

        DebugOpenList(
            *_OpenList,
            std::format("MoveMap.propogateCost: Cannot find movemap node [{}, {}, {}] for change\n", r, c, id),
            search.MovingObject);
        return;
    }

    for (int32_t i = 0; i < NumOffsets; i++)
    {
        if (IsDiagonalStep(i) && !AdjacentCellOpen(search, map, r, c, StepAdjDir[static_cast<size_t>(i)]) &&
            !AdjacentCellOpen(search, map, r, c, StepAdjDir[static_cast<size_t>(i) + 1]))
        {
            continue;
        }

        const int32_t nextR = r + CellShiftRow[static_cast<size_t>(i)];
        const int32_t nextC = c + CellShiftCol[static_cast<size_t>(i)];

        if (nextR < 0 || nextR >= CellHeight || nextC < 0 || nextC >= CellWidth)
        {
            continue;
        }

        MCMoveMapNode& next = NodeAt(nextR, nextC);

        if (next.Cost >= BlockedCost || next.HPrime == -1 || next.HPrime >= MaxHPrime)
        {
            continue;
        }

        const int32_t dir = ReverseShift[static_cast<size_t>(i)];
        Assert(next.Cost > 0, 0, " MoveMap.propogateCost: bad cost 1");
        const int32_t stepCost = StepCost(search, next.Cost, i);

        if (dir != next.Parent)
        {
            if (next.G <= current.G + stepCost)
            {
                continue;
            }

            next.Parent = dir;
        }

        PropogateCost(search, map, nextR, nextC, stepCost, current.G);
    }
}

auto MCMoveMap::SearchPath(const MCPathSearch& search, const MCScenarioMap& map, MCMovePath& path,
                           std::optional<std::reference_wrapper<MCVector3D>> goalWorldPos,
                           std::span<int32_t, 2> goalCell, bool escape) -> int32_t
{
    if (escape)
    {
        MaxHPrime = 500;
    }
    else
    {
        if (GoalR < 0 || GoalR >= CellHeight || GoalC < 0 || GoalC >= CellWidth)
        {
            const MCVector3D goal = MapCellCentre(GoalR, GoalC);
            Fatal(0, std::format(" Bad Move Goal: {} [{}({}), {}({})], ({:.2f}, {:.2f}, {:.2f})",
                                 search.DebugMovePathType, GoalR, CellHeight, GoalC, CellWidth, goal.X, goal.Y, 0.0));
        }

        const int32_t distance = std::abs(GoalR - StartR) + std::abs(GoalC - StartC);
        MaxHPrime = std::max(static_cast<int32_t>(std::floor(static_cast<double>(distance) * 2.5)), 500);
    }

    // Each cell goes on the open list at most once.
    _OpenList->Reserve(CellHeight * CellWidth);
    MCMoveMapNode& start = NodeAt(StartR, StartC);
    start.G = 0;
    const int32_t startH = escape ? 10 : std::abs(GoalR - StartR) + std::abs(GoalC - StartC);
    start.HPrime = startH;
    start.FPrime = startH;
    _OpenList->Clear();
    _OpenList->Insert(MCPQNode{startH, CellId(StartR, StartC), StartR, StartC});
    start.Flags |= OpenFlag;

    int32_t bestR = -1;
    int32_t bestC = -1;
    bool goalFound = false;

    while (!_OpenList->IsEmpty())
    {
        const MCPQNode best = _OpenList->Pop();
        bestR = best.Row;
        bestC = best.Col;
        MCMoveMapNode& current = NodeAt(bestR, bestC);
        const int32_t g = current.G;
        const uint32_t flags = current.Flags;
        current.Flags = (flags & ~OpenFlag) | ClosedFlag;

        if ((flags & GoalFlag) != 0)
        {
            goalFound = true;
            break;
        }

        for (int32_t i = 0; i < NumOffsets; i++)
        {
            if (IsDiagonalStep(i) && !AdjacentCellOpen(search, map, bestR, bestC, StepAdjDir[static_cast<size_t>(i)]) &&
                !AdjacentCellOpen(search, map, bestR, bestC, StepAdjDir[static_cast<size_t>(i) + 1]))
            {
                continue;
            }

            const int32_t nextR = CellShiftRow[static_cast<size_t>(i)] + bestR;
            const int32_t nextC = CellShiftCol[static_cast<size_t>(i)] + bestC;

            if (nextR < 0 || nextR >= CellHeight || nextC < 0 || nextC >= CellWidth)
            {
                continue;
            }

            MCMoveMapNode& next = NodeAt(nextR, nextC);

            if (next.Cost >= BlockedCost)
            {
                continue;
            }

            if (next.HPrime == -1)
            {
                next.HPrime = escape ? 10 : std::abs(GoalR - nextR) + std::abs(GoalC - nextC);
            }

            if (next.HPrime >= MaxHPrime)
            {
                continue;
            }

            const int32_t dir = ReverseShift[static_cast<size_t>(i)];
            const int32_t stepCost = StepCost(search, next.Cost, i);
            Assert(stepCost > 0, 0, " MoveMap.propogateCost: bad cost 3");
            const int32_t newG = stepCost + g;

            if ((next.Flags & OpenFlag) == 0)
            {
                if ((next.Flags & ClosedFlag) == 0)
                {
                    next.Parent = dir;
                    next.G = newG;
                    next.FPrime = newG + next.HPrime;
                    _OpenList->Insert(MCPQNode{next.FPrime, CellId(nextR, nextC), nextR, nextC});
                    next.Flags |= OpenFlag;
                }
                else if (newG < next.G)
                {
                    next.Parent = dir;
                    PropogateCost(search, map, nextR, nextC, stepCost, g);
                }
            }
            else if (newG < next.G)
            {
                next.Parent = dir;
                next.G = newG;
                next.FPrime = next.HPrime + newG;
                const int32_t id = CellId(nextR, nextC);
                const int32_t itemIndex = _OpenList->Find(id);

                if (itemIndex == 0)
                {
                    // The original passes three values for the four %d (the last prints stack garbage).
                    DebugOpenList(*_OpenList,
                                  std::format("MoveMap.{}: Cannot find movemap node [{}, {}, {}, {}] for "
                                              "change\n",
                                              escape ? "calcEscapePath" : "calcPath", nextR, nextC, id, 0),
                                  search.MovingObject);
                }
                else
                {
                    _OpenList->Change(itemIndex, next.FPrime);
                }
            }
        }
    }

    if (search.ClearBridgeTiles)
    {
        AdjustBridgeWeights(OverlayLevel, 10000);
    }

    if (!goalFound)
    {
        return 0;
    }

    goalCell[0] = bestR;
    goalCell[1] = bestC;
    int32_t count = 0;

    for (int32_t r = bestR, c = bestC; r != StartR || c != StartC;)
    {
        count++;
        const size_t parent = static_cast<size_t>(NodeAt(r, c).Parent);
        r += CellShiftRow[parent];
        c += CellShiftCol[parent];
    }

    if (DoorDirection != -1)
    {
        count++;
    }

    path.Goal.Zero();
    path.NumSteps = 0;
    path.NumStepsWhenNotPaused = 0;
    path.CurStep = 0;
    path.Cost = 0;
    path.Marked = false;
    path.GlobalStep = -1;

    if (count == 0)
    {
        return path.NumSteps;
    }

    path.SetNumSteps(count);
    path.Target = Target;
    path.Cost = NodeAt(bestR, bestC).G;
    int32_t stepIndex = count;

    if (DoorDirection == -1)
    {
        if (!goalWorldPos)
        {
            path.Goal = GoalPos;
        }
        else
        {
            goalWorldPos->get() = MapCellCentre(MinRow + bestR, MinCol + bestC);
            path.Goal = goalWorldPos->get();
        }
    }
    else
    {
        // The last step goes through the door, into the next area.
        stepIndex = count - 1;
        MCPathStep& doorStep = path.StepList[static_cast<size_t>(stepIndex)];
        doorStep.Direction = static_cast<uint8_t>(DoorDirection << 1);
        const int32_t doorRow = AdjTile[static_cast<size_t>(DoorDirection)][0] + MinRow + bestR;
        const int32_t doorCol = MinCol + AdjTile[static_cast<size_t>(DoorDirection)][1] + bestC;
        goalCell[0] = doorRow;
        goalCell[1] = doorCol;
        const MCVector3D doorPos = MapCellCentre(doorRow, doorCol);
        path.SetDestination(stepIndex, doorPos);
        doorStep.DistanceToGoal = 0.0f;
        doorStep.TileR = static_cast<int16_t>(doorRow / 3);
        doorStep.TileC = static_cast<int16_t>(doorCol / 3);
        doorStep.CellR = static_cast<int16_t>(doorRow - doorStep.TileR * 3);
        doorStep.CellC = static_cast<int16_t>(doorCol - doorStep.TileC * 3);
        path.Goal = doorPos;

        if (goalWorldPos)
        {
            goalWorldPos->get() = doorPos;
        }
    }

    // Walks back from the goal cell, filling the steps from the end.
    for (int32_t r = bestR, c = bestC; r != StartR || c != StartC;)
    {
        stepIndex--;
        MCMoveMapNode& node = NodeAt(r, c);
        MCPathStep& step = path.StepList[static_cast<size_t>(stepIndex)];
        step.Direction = static_cast<uint8_t>(ReverseShift[static_cast<size_t>(node.Parent)]);
        const int32_t mapRow = MinRow + r;
        const int32_t mapCol = MinCol + c;

        if (stepIndex == count - 1 && static_cast<int8_t>(step.Direction) < 8)
        {
            step.DistanceToGoal = 0.0f;
        }
        else
        {
            // As the original: for a final jump step this reads the step past the end (left from an earlier path).
            const MCPathStep& nextStep = path.StepList[static_cast<size_t>(stepIndex) + 1];
            step.DistanceToGoal = nextStep.DistanceToGoal + CellShiftDistance(static_cast<int8_t>(nextStep.Direction));
        }

        path.SetDestination(stepIndex, MapCellCentre(mapRow, mapCol));
        step.TileR = static_cast<int16_t>(mapRow / 3);
        step.TileC = static_cast<int16_t>(mapCol / 3);
        step.CellR = static_cast<int16_t>(mapRow - step.TileR * 3);
        step.CellC = static_cast<int16_t>(mapCol - step.TileC * 3);
        node.Flags |= PathFlag;
        r += CellShiftRow[static_cast<size_t>(node.Parent)];
        c += CellShiftCol[static_cast<size_t>(node.Parent)];
    }

    return path.NumSteps;
}

auto MCMoveMap::CalcPath(const MCPathSearch& search, const MCScenarioMap& map, MCMovePath& path,
                         std::span<int32_t, 2> goalCell) -> int32_t
{
    return SearchPath(search, map, path, std::nullopt, goalCell, false);
}

auto MCMoveMap::CalcPath(const MCPathSearch& search, const MCScenarioMap& map, MCMovePath& path,
                         MCVector3D& goalWorldPos, std::span<int32_t, 2> goalCell) -> int32_t
{
    return SearchPath(search, map, path, goalWorldPos, goalCell, false);
}

auto MCMoveMap::CalcEscapePath(const MCPathSearch& search, const MCScenarioMap& map, MCMovePath& path,
                               MCVector3D& goalWorldPos, std::span<int32_t, 2> goalCell) -> int32_t
{
    return SearchPath(search, map, path, goalWorldPos, goalCell, true);
}

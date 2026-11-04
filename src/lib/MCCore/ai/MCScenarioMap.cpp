#include "stdafx.h"
#include "ai/MCScenarioMap.h"
#include "ai/MCMoveSystem.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "object/MCBigGameObject.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCObjectBlockManager.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectType.h"

MCScenarioMap::MCScenarioMap(int32_t width, int32_t height)
    : Map(static_cast<size_t>(width * height))
    , Height(height)
    , Width(width)
    , PathMap(static_cast<size_t>(width * height))
{
}

auto MCScenarioMap::Read(MCFile& mapFile) -> std::expected<std::unique_ptr<MCScenarioMap>, std::string>
{
    const int32_t height = mapFile.ReadLong();
    const int32_t width = mapFile.ReadLong();
    const int32_t baseElevation = mapFile.ReadLong();
    const uint64_t left = mapFile.GetLength() - std::min(mapFile.GetLength(), mapFile.GetLogicalPosition());

    if (width <= 0 || height <= 0 ||
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * sizeof(MCMapTile) > left)
    {
        return std::unexpected(std::format("a map of {}x{} tiles in {} bytes", width, height, left));
    }

    auto map = std::make_unique<MCScenarioMap>(width, height);
    map->BaseElevation = baseElevation;
    mapFile.Read(std::span(reinterpret_cast<uint8_t*>(map->Map.data()), map->Map.size() * sizeof(MCMapTile)));
    return map;
}

auto MCScenarioMap::Write(MCFile& mapFile) const -> void
{
    mapFile.WriteLong(Height);
    mapFile.WriteLong(Width);
    mapFile.WriteLong(BaseElevation);
    mapFile.Write(std::span(reinterpret_cast<const uint8_t*>(Map.data()), Map.size() * sizeof(MCMapTile)));
}

auto MCScenarioMap::WorldToMapPos(MCVector3D pos, int32_t& tileR, int32_t& tileC, int32_t& cellR, int32_t& cellC)
    -> void
{
    WorldToMapTilePos(pos, tileR, tileC);
    cellC = static_cast<int32_t>((static_cast<double>(pos.X) - TileColToWorld(tileC)) / MetersPerCell());
    cellR = static_cast<int32_t>((static_cast<double>(TileRowToWorld(tileR)) - pos.Y) / MetersPerCell());
}

auto MCScenarioMap::WorldToMapTilePos(MCVector3D pos, int32_t& tileR, int32_t& tileC) -> void
{
    tileC = static_cast<int16_t>(static_cast<int32_t>(
        std::floor(static_cast<double>(MCTerrain::OneOvermetersPerVertex) * pos.X + VerticesMapSideDivTwo())));
    tileR = static_cast<int16_t>(static_cast<int32_t>(
        std::floor((static_cast<double>(MetersMapSideDivTwo()) - pos.Y) * MCTerrain::OneOvermetersPerVertex)));
}

auto MCScenarioMap::CellPassable(MCVector3D pos) const -> bool
{
    int32_t tileR = 0;
    int32_t tileC = 0;
    int32_t cellR = 0;
    int32_t cellC = 0;
    WorldToMapPos(pos, tileR, tileC, cellR, cellC);

    // Port fix: the original reads outside the map for a point off it (the mouse past the terrain edge). Off the map
    // is impassable.
    if (!OnMap(tileR, tileC))
    {
        return false;
    }

    return TileAt(tileR, tileC).GetCellPassable(cellR, cellC) != 0;
}

auto MCScenarioMap::SpreadState(int32_t cellRow, int32_t cellCol, int32_t depth) -> void
{
    if (cellRow < 0 || cellRow >= Height * MapCellDim || cellCol < 0 || cellCol >= Width * MapCellDim || depth <= 0)
    {
        return;
    }

    const int32_t tileR = cellRow / MapCellDim;
    const int32_t tileC = cellCol / MapCellDim;
    MCMapTile& tile = TileAt(tileR, tileC);

    if (PreserveMapTiles && (tile.Cells & 0x2000) == 0)
    {
        PreservedTiles.push_back(MCPreservedTile{tileR, tileC, tile.Cells});
        tile.Cells |= 0x2000;
    }

    const uint32_t shift =
        static_cast<uint32_t>(((cellRow - tileR * MapCellDim) * MapCellDim + (cellCol - tileC * MapCellDim)) * 2);
    tile.Cells &= ~(0x4000u << shift);

    for (int32_t dir = 0; dir < 8; dir++)
    {
        SpreadState(cellRow + RowShift[dir], cellCol + ColShift[dir], depth - 1);
    }
}

auto MCScenarioMap::PlaceObject(MCVector3D position, float radius) -> void
{
    int32_t cellR = 0;
    int32_t cellC = 0;
    WorldCoordToMapCell(position, cellR, cellC);
    double depth = static_cast<double>(radius) /
                   (static_cast<double>(MetersPerWorldUnit) * MCTerrain::MetersPerVertexDivMapcellDim);

    if (depth > 0.5 && depth < 1.0)
    {
        depth = 1.0;
    }

    SpreadState(cellR, cellC, static_cast<int32_t>(depth));
}

auto MCScenarioMap::PlaceObjects(MCObjectList& objectList) -> void
{
    for (std::shared_ptr<MCBaseObject> current : objectList)
    {
        const auto object = std::static_pointer_cast<MCGameObject>(current);

        if (object->GetUseMe() != 0 && object->GetObjectType() != nullptr)
        {
            PlaceObject(object->GetPosition(), object->GetObjectType()->ExtentRadius);
        }
    }
}

auto MCScenarioMap::UpdateMovingObjects() -> void
{
    PreserveMapTiles = true;
    PlaceObjects(*ClanMechList());
    PlaceObjects(*InnerSphereMechList());
    PreserveMapTiles = false;
}

auto MCScenarioMap::RestorePreservedMap() -> void
{
    for (const MCPreservedTile& preserved : PreservedTiles)
    {
        TileAt(preserved.Row, preserved.Col).Cells = preserved.Cells;
    }

    PreservedTiles.clear();
}

auto MCScenarioMap::GetTerrainElevation(MCVector3D position) const -> float
{
    return static_cast<float>(GetTerrainElevationUnrounded(position));
}

auto MCScenarioMap::GetTerrainElevationUnrounded(MCVector3D position) const -> double
{
    const float mpv = MCTerrain::MetersPerVertex;
    const float oneOver = MCTerrain::OneOvermetersPerVertex;
    const float vertexX = static_cast<float>(mpv * std::floor(static_cast<double>(oneOver) * position.X));
    const float vertexY = static_cast<float>(mpv * (std::floor(static_cast<double>(oneOver) * position.Y) + 1.0));
    const double vertexCol = static_cast<double>(oneOver) * vertexX;
    const float vertexRow = oneOver * vertexY;
    const int32_t halfSide = (MCTerrain::BlocksMapSide * MCTerrain::VerticesBlockSide) >> 1;
    const int32_t maxTile = MCTerrain::BlocksMapSide * MCTerrain::VerticesBlockSide - 2;
    const int32_t tileC = std::clamp(static_cast<int32_t>(std::floor(vertexCol)) + halfSide, 0, maxTile);
    const int32_t tileR =
        std::clamp(halfSide - static_cast<int32_t>(std::floor(static_cast<double>(vertexRow))), 0, maxTile);
    Assert(OnMap(tileR, tileC), 0, " move:terrelev MapTile Out of Bounds ");
    Assert(OnMap(tileR + 1, tileC + 1), 0, " move:terrelev2 MapTile Out of Bounds ");
    const uint32_t cells00 = TileAt(tileR, tileC).Cells;
    const uint32_t cells01 = TileAt(tileR, tileC + 1).Cells;
    const uint32_t cells11 = TileAt(tileR + 1, tileC + 1).Cells;
    const uint32_t cells10 = TileAt(tileR + 1, tileC).Cells;

    const int32_t base = BaseElevation;
    const float mpe = MCTerrain::MetersPerElevLevel;
    const auto levelOf = [base](uint32_t cells) -> double
    {
        return static_cast<double>(
            static_cast<int64_t>(static_cast<uint32_t>(static_cast<int32_t>((cells >> 7) & 0x3f) + base)));
    };

    const float cornerX = static_cast<float>(std::floor(vertexCol) * mpv);
    const float cornerY = static_cast<float>(std::floor(static_cast<double>(vertexRow)) * mpv);
    const float elevation00 = static_cast<float>(levelOf(cells00) * mpe);
    const double offsetX = std::fabs(static_cast<double>(position.X) - vertexX);
    const float dx = static_cast<float>(offsetX);
    const float dy = static_cast<float>(std::fabs(static_cast<double>(vertexY) - position.Y));
    const double cornerXPlus = static_cast<double>(cornerX) + mpv;

    // The two edges of the tile's triangle holding the point, from its upper-left corner.
    double edge1X;
    double edge1Y;
    float edge1Z;
    double edge2X;
    double edge2Y;
    double edge2Z;

    if (offsetX > dy)
    {
        const float elevationB = static_cast<float>(levelOf(cells01) * mpe);
        const float cornerYMinus = cornerY - mpv;
        const float elevationC = static_cast<float>(levelOf(cells11) * mpe);
        const double spanX = cornerXPlus - cornerX;
        edge1X = spanX;
        edge1Y = 0.0;
        edge1Z = elevationB - elevation00;
        edge2X = static_cast<float>(spanX);
        edge2Y = cornerYMinus - cornerY;
        edge2Z = elevationC - elevation00;
    }
    else
    {
        const float cornerXPlusF = static_cast<float>(cornerXPlus);
        const float cornerYMinus = cornerY - mpv;
        const float elevationC = static_cast<float>(levelOf(cells11) * mpe);
        const double elevationD = levelOf(cells10) * mpe;
        const float spanY = cornerYMinus - cornerY;
        edge1X = 0.0;
        edge1Y = spanY;
        edge1Z = static_cast<float>(elevationD - elevation00);
        edge2X = static_cast<double>(cornerXPlusF) - cornerX;
        edge2Y = spanY;
        edge2Z = elevationC - elevation00;
    }

    const float length1 =
        static_cast<float>(std::sqrt((edge1Y * edge1Y + static_cast<double>(edge1Z) * edge1Z) + edge1X * edge1X));

    if (length1 > 0.0f)
    {
        edge1X = edge1X / length1;
        edge1Y = edge1Y / length1;
        edge1Z = static_cast<float>(edge1Z / static_cast<double>(length1));
    }

    const float length2 = static_cast<float>(std::sqrt((edge2Y * edge2Y + edge2X * edge2X) + edge2Z * edge2Z));

    if (length2 > 0.0f)
    {
        edge2X = edge2X / length2;
        edge2Y = edge2Y / length2;
        edge2Z = edge2Z / length2;
    }

    float normalX = static_cast<float>(edge2Z * edge1Y - edge2Y * edge1Z);
    float normalY = static_cast<float>(edge1Z * edge2X - edge2Z * edge1X);
    float normalZ = static_cast<float>(edge2Y * edge1X - edge1Y * edge2X);

    if (normalZ == 0.0f || std::isnan(normalZ))
    {
        Fatal(0, " Vertical Terrain ");
        return 0.0;
    }

    if (normalZ < 0.0f)
    {
        normalX = -normalX;
        normalY = -normalY;
        normalZ = -normalZ;
    }

    return -((static_cast<double>(normalY) / normalZ) * -dy + (static_cast<double>(normalX) / normalZ) * dx) +
           elevation00;
}

auto MCScenarioMap::GetLos(MCVector3D position) const -> bool
{
    int32_t tileR = 0;
    int32_t tileC = 0;
    int32_t cellR = 0;
    int32_t cellC = 0;
    WorldToMapPos(position, tileR, tileC, cellR, cellC);

    // Port fix: a line walked toward a point off the map reads outside it in the original. Off the map blocks.
    if (!OnMap(tileR, tileC))
    {
        return false;
    }

    return TileAt(tileR, tileC).GetCellLos(cellR, cellC) != 0;
}

auto MCScenarioMap::GetInnerSphereMine(int32_t tileR, int32_t tileC, int32_t cellR, int32_t cellC) const -> uint32_t
{
    const uint32_t layout = (TileAt(tileR, tileC).Overlay >> 11) & 3;
    return MineLayout[layout][static_cast<size_t>(cellR * MapCellDim + cellC)];
}

auto MCScenarioMap::GetClanMine(int32_t tileR, int32_t tileC, int32_t cellR, int32_t cellC) const -> uint32_t
{
    const uint32_t layout = (TileAt(tileR, tileC).Overlay >> 13) & 3;
    return MineLayout[layout][static_cast<size_t>(cellR * MapCellDim + cellC)];
}

auto MCScenarioMap::LineOfSight(MCVector3D start, MCVector3D target) -> bool
{
    MCVector3D step = target - start;
    step.Normalize();
    const float stepLength = MetersPerWorldUnit * MCTerrain::MetersPerVertexDivMapcellDim * 0.33f;
    step.X = step.X * stepLength;
    step.Y = step.Y * stepLength;
    step.Z = step.Z * stepLength;
    const auto totalDistance = static_cast<float>((start - target).Magnitude() * MetersPerWorldUnit);
    UpdateMovingObjects();

    MCVector3D current = start + step;
    auto distance = static_cast<float>((current - start).Magnitude() * MetersPerWorldUnit);
    bool result = true;

    while (distance < totalDistance)
    {
        if (!GetLos(current))
        {
            result = false;
        }

        current.X = current.X + step.X;
        current.Y = current.Y + step.Y;
        current.Z = current.Z + step.Z;
        distance = static_cast<float>((current - start).Magnitude() * MetersPerWorldUnit);

        if (!result)
        {
            break;
        }
    }

    RestorePreservedMap();
    return result;
}

auto MCScenarioMap::LineOfFire(MCVector3D start, MCVector3D target) const -> bool
{
    double directionX = static_cast<double>(target.X) - start.X;
    const double deltaY = static_cast<double>(target.Y) - start.Y;
    float directionY = static_cast<float>(deltaY);
    const double length = std::sqrt(deltaY * directionY + directionX * directionX);

    if (length > 0.0)
    {
        directionX = directionX / length;
        directionY = static_cast<float>(directionY / length);
    }

    const float stepLength = MCTerrain::MetersPerVertexDivMapcellDim * 0.33f;
    const float stepX = static_cast<float>(directionX * stepLength);
    const float stepY = directionY * stepLength;
    const double spanX = static_cast<double>(start.X) - target.X;
    const double spanY = static_cast<double>(start.Y) - target.Y;
    const float totalDistance = static_cast<float>(std::sqrt(spanX * spanX + spanY * spanY));
    float currentX = stepX + start.X;
    float currentY = stepY + start.Y;

    while (true)
    {
        const double travelledX = static_cast<double>(currentX) - start.X;
        const double travelledY = static_cast<double>(currentY) - start.Y;

        if (totalDistance <= std::sqrt(travelledY * travelledY + travelledX * travelledX))
        {
            return true;
        }

        if (!GetLos(MCVector3D(currentX, currentY, 0.0f)))
        {
            return false;
        }

        currentX = currentX + stepX;
        currentY = currentY + stepY;
    }
}

auto MCScenarioMap::LineOfSensor(MCVector3D start, MCVector3D target, int32_t& numBlockingTiles,
                                 int32_t& numBlockingObjects) -> void
{
    MCVector3D step = target - start;
    const double length = std::sqrt((static_cast<double>(step.X) * step.X + static_cast<double>(step.Y) * step.Y) +
                                    static_cast<double>(step.Z) * step.Z);
    double directionZ = step.Z;

    if (length > 0.0)
    {
        step.X = static_cast<float>(step.X / length);
        step.Y = static_cast<float>(step.Y / length);
        directionZ = step.Z / length;
    }

    const double stepLength = static_cast<double>(MetersPerWorldUnit) * MCTerrain::MetersPerVertexDivMapcellDim * 2.0f;
    step.X = static_cast<float>(step.X * stepLength);
    step.Y = static_cast<float>(step.Y * stepLength);
    step.Z = static_cast<float>(directionZ * stepLength);
    const MCVector3D span = start - target;
    const float totalDistance =
        static_cast<float>(std::sqrt((static_cast<double>(span.X) * span.X + static_cast<double>(span.Y) * span.Y) +
                                     static_cast<double>(span.Z) * span.Z) *
                           MetersPerWorldUnit);
    UpdateMovingObjects();

    auto travelledDistance = [](const MCVector3D& travelled) -> double
    {
        return std::sqrt(
                   (static_cast<double>(travelled.Z) * travelled.Z + static_cast<double>(travelled.Y) * travelled.Y) +
                   static_cast<double>(travelled.X) * travelled.X) *
               MetersPerWorldUnit;
    };

    MCVector3D current = start + step;
    MCVector3D travelled = current - start;
    int32_t prevTileR = 0;
    int32_t prevTileC = 0;
    WorldToMapTilePos(start, prevTileR, prevTileC);
    numBlockingTiles = 0;
    numBlockingObjects = 0;
    const MCObjectMap& objectMap = GameObjectMap();

    if (!(static_cast<float>(travelledDistance(travelled)) >= totalDistance))
    {
        do
        {
            int32_t tileR = 0;
            int32_t tileC = 0;
            WorldToMapTilePos(current, tileR, tileC);

            // Port fix: the original counts blockers on tiles off the map too, reading outside it.
            if ((tileR != prevTileR || tileC != prevTileC) && OnMap(tileR, tileC))
            {
                if (GetTerrainElevationUnrounded(current) > current.Z)
                {
                    numBlockingTiles++;
                }

                numBlockingObjects += objectMap.GetNumSensorBlockingObjects(tileR, tileC);
                numBlockingObjects += (TileAt(tileR, tileC).Overlay & 0x1000000) == 0x1000000 ? 1 : 0;
                prevTileR = tileR;
                prevTileC = tileC;
            }

            current.X = current.X + step.X;
            current.Y = current.Y + step.Y;
            current.Z = step.Z + current.Z;
            travelled = current - start;
        } while (!(travelledDistance(travelled) >= totalDistance));
    }

    RestorePreservedMap();
}

auto MCScenarioMap::Print(std::string_view fileName, int32_t uLr, int32_t uLc, int32_t printHeight,
                          int32_t printWidth) const -> void
{
    MCFile debugFile;
    debugFile.Create(fileName);

    for (int32_t row = uLr; row < uLr + printHeight; row++)
    {
        std::string line;

        for (int32_t col = uLc; col < uLc + printWidth; col++)
        {
            line += (TileAt(row, col).Cells & 0x55554000) != 0 ? '.' : 'X';
        }

        line += '\n';
        debugFile.WriteString(line);
    }

    debugFile.WriteString("\n");
    debugFile.Close();
}

auto MCScenarioMap::GetOverlayWeight(int32_t tileR, int32_t tileC, int32_t cellR, int32_t cellC, MCMover& mover) const
    -> int32_t
{
    const uint32_t overlay = TileAt(tileR, tileC).OverlayType();

    if (overlay == 0)
    {
        return 0;
    }

    const int32_t level = mover.GetOverlayWeightClass();
    const int32_t weightOverlay =
        IsGateOverlay(overlay) ? GateOverlay(overlay, mover.GetAlignment()) : static_cast<int32_t>(overlay);

    if (weightOverlay == -1)
    {
        return 20000;
    }

    return OverlayWeightTable[static_cast<size_t>(OverlayWeightIndex(weightOverlay) + level * OverlayWeightLevelSize +
                                                  cellC + cellR * MapCellDim)];
}

auto MCScenarioMap::ClearMines() -> void
{
    // Overlay bits 11-12 and 13-14 (each side's mine layout), 25-28 (the mines each side knows of).
    constexpr uint32_t MineBits = 0x1E007800u;

    for (MCMapTile& tile : Map)
    {
        tile.Overlay &= ~MineBits;
    }
}

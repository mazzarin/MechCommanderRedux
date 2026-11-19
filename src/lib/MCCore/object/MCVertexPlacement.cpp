#include "stdafx.h"
#include "object/MCVertexPlacement.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "camera/MCCamera.h"
#include "engine/MCByteFlag.h"
#include "lib/MCFatal.h"
#include "object/MCMoverMath.h"
#include "object/MCObjectDrawing.h"
#include "terrain/MCTerrain.h"

namespace
{
    /// <summary>Sixty degrees in radians, as MCX.EXE stores it.</summary>
    constexpr double SixtyDegrees = 0x1.0c152382d45b2p+0;
}

auto MCVertexCell::Of(int32_t blockNumber, int32_t vertexNumber) -> MCVertexCell
{
    MCVertexCell cell;
    cell.Col = (blockNumber % MCTerrain::BlocksMapSide) * MCTerrain::VerticesBlockSide +
               vertexNumber % MCTerrain::VerticesBlockSide;
    cell.Row = vertexNumber / MCTerrain::VerticesBlockSide +
               (blockNumber / MCTerrain::BlocksMapSide) * MCTerrain::VerticesBlockSide;
    return cell;
}

auto MCVertexCell::WorldX() const -> float
{
    const int32_t halfMap = (MCTerrain::VerticesBlockSide * MCTerrain::BlocksMapSide) >> 1;
    return static_cast<float>(Col - halfMap) * MCTerrain::MetersPerVertex;
}

auto MCVertexCell::WorldY() const -> float
{
    const int32_t halfMap = (MCTerrain::VerticesBlockSide * MCTerrain::BlocksMapSide) >> 1;
    return static_cast<float>(halfMap - Row) * MCTerrain::MetersPerVertex;
}

auto MCVertexCell::Elevation(std::string_view what) const -> float
{
    const bool onMap = !(Row < 0 || GameMap().Height <= Row || Col < 0 || GameMap().Width <= Col);
    Assert(onMap, 0, what);
    Assert(onMap, 0, " Map Tile out of bounds ");
    const MCMapTile& tile = GameMap().Map[GameMap().Width * Row + Col];
    const int32_t elevationLevel = static_cast<int32_t>((tile.Cells >> 7) & 0x3f) + GameMap().BaseElevation;
    return static_cast<float>(elevationLevel) * MCTerrain::MetersPerElevLevel;
}

auto MCVertexCell::VisibleCorners() const -> int32_t
{
    MCByteFlag& visibleBits = Terrain()->HomeVisibleBits();
    const auto row = static_cast<uint32_t>(Row);
    const auto col = static_cast<uint32_t>(Col);
    int32_t count = 0;

    for (const auto& [cornerRow, cornerCol] :
         {std::pair{row, col}, std::pair{row + 1, col}, std::pair{row + 1, col + 1}, std::pair{row, col + 1}})
    {
        if (visibleBits.GetFlag(cornerRow, cornerCol) != 0)
        {
            count++;
        }
    }

    return count;
}

MCVector3D PlaceOnVertex(MCVector3D position, int32_t blockNumber, int32_t vertexNumber, int32_t pixelOffsetX,
                         int32_t pixelOffsetY)
{
    const int32_t blocksMapSide = MCTerrain::BlocksMapSide;
    const int32_t verticesBlockSide = MCTerrain::VerticesBlockSide;
    float blockX = static_cast<float>(blockNumber % blocksMapSide - blocksMapSide / 2) * MCTerrain::MetersBlockSide;
    float blockY = static_cast<float>(blocksMapSide / 2 - blockNumber / blocksMapSide) * MCTerrain::MetersBlockSide;

    if ((blocksMapSide & 1) != 0)
    {
        blockX = blockX - MCTerrain::MetersBlockSide * 0.5f;
        blockY = MCTerrain::MetersBlockSide * 0.5f + blockY;
    }

    const float vertexX = static_cast<float>(vertexNumber % verticesBlockSide) * MCTerrain::MetersPerVertex;
    const double offsetY = static_cast<double>(pixelOffsetY);
    const double offsetX = static_cast<double>(pixelOffsetX);
    double offsetAngle;

    if (offsetY == 0.0)
    {
        offsetAngle = 90.0;
    }
    else
    {
        offsetAngle = std::atan(offsetX / offsetY) * MCMoverMath::RadiansToDegrees;
    }

    position.Y = blockY - static_cast<float>(vertexNumber / verticesBlockSide) * MCTerrain::MetersPerVertex;
    const auto offsetDistance = static_cast<float>(std::sqrt(offsetY * offsetY + offsetX * offsetX));
    const double axisAngle = (60.0 - offsetAngle) * MCMoverMath::DegreesToRadians;
    const auto alongAxis = static_cast<float>(std::sin(axisAngle) * offsetDistance / std::sin(SixtyDegrees));
    position.X = vertexX + blockX;
    const float elevation = MCTerrain::GetTerrainElevation(position);
    position.X =
        static_cast<float>(std::cos(SixtyDegrees) * alongAxis + std::cos(axisAngle) * offsetDistance + position.X);
    position.Y = position.Y - alongAxis;
    position.Z = elevation;
    return position;
}

auto VertexScreenPlace(int32_t blockNumber, int32_t vertexNumber, const MCCamera& camera) -> MCVector2D
{
    MCVector2D place;

    if (MCCamera::VertexProject(blockNumber, vertexNumber, place) != 0)
    {
        return place;
    }

    const MCVertexCell cell = MCVertexCell::Of(blockNumber, vertexNumber);
    const MCVector2D screen =
        ProjectToScreen(MCVector3D(cell.WorldX(), cell.WorldY(), cell.Elevation(" vertex off the map ")), camera);
    return MCVector2D(std::floor(screen.X), std::floor(screen.Y));
}

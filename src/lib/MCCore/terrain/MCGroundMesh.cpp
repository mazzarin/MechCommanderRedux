#include "stdafx.h"
#include "terrain/MCGroundMesh.h"
#include "camera/MCCamera.h"
#include "engine/MCByteFlag.h"
#include "object/MCElemental.h"
#include "object/MCElementalType.h"
#include "object/MCElementalGameSystem.h"
#include "object/MCForces.h"
#include "platform/MCRenderer.h"
#include "terrain/MCMapBlockManager.h"
#include "terrain/MCTerrain.h"
#include "terrain/MCTerrainTiles.h"
#include "terrain/MCTerrainWindow.h"
#include "vfx/MCVfx.h"
#include "vfx/MCVfxClip.h"

namespace
{
    /// <summary>
    /// The map's ground mesh (MCTerrainMesh) and what it was made with: the steps it places vertices by, and per mesh
    /// vertex the elevation and tile it holds (to check each frame's grid against).
    /// </summary>
    struct MCGroundMesh
    {
        MCTerrainMesh Mesh;
        int32_t StepX = 0;
        int32_t StepY = 0;
        int32_t ElevStep = 0;
        /// <summary>The mesh's vertices a side ((Cols + 1) = (Rows + 1)), from (Mesh.FirstRow, Mesh.FirstCol).</summary>
        int32_t Side = 0;
        std::vector<uint8_t> Elevations;
        std::vector<int16_t> Textures;
    };

    std::unique_ptr<MCGroundMesh> GroundMesh;
    uint64_t GroundMeshVersions = 0;

    /// <summary>
    /// Builds the ground mesh over the map and a ring of <paramref name="ring"/> off-map vertices around it, reading
    /// every tile it uses.
    /// </summary>
    void BuildGroundMesh(int32_t ring, int32_t stepX, int32_t stepY, int32_t elevStep)
    {
        MCMapBlockManager& blocks = *Terrain()->MapBlocks;
        MCTerrainTiles& tileCache = *Terrain()->Tiles;
        auto built = std::make_unique<MCGroundMesh>();
        built->StepX = stepX;
        built->StepY = stepY;
        built->ElevStep = elevStep;
        const int32_t mapSide = MCTerrain::BlocksMapSide * MCTerrain::VerticesBlockSide;
        const int32_t side = mapSide + 2 * ring;
        built->Side = side;
        built->Elevations.resize(static_cast<size_t>(side) * side);
        built->Textures.resize(static_cast<size_t>(side) * side);

        for (int32_t r = 0; r < side; ++r)
        {
            for (int32_t c = 0; c < side; ++c)
            {
                const MCPrecompVertex& vertex = blocks.MapVertexAt(r - ring, c - ring);
                built->Elevations[static_cast<size_t>(r) * side + c] = vertex.Elevation;
                built->Textures[static_cast<size_t>(r) * side + c] = vertex.TextureData;
            }
        }

        MCTerrainMesh& mesh = built->Mesh;
        mesh.Version = ++GroundMeshVersions;
        mesh.FirstRow = -ring;
        mesh.FirstCol = -ring;
        mesh.Cols = side - 1;
        mesh.Rows = side - 1;
        mesh.CellTiles.resize(static_cast<size_t>(mesh.Cols) * mesh.Rows);
        mesh.CellElevations.resize(mesh.CellTiles.size());
        std::unordered_map<int32_t, uint32_t> tiles;

        for (int32_t r = 0; r < mesh.Rows; ++r)
        {
            for (int32_t c = 0; c < mesh.Cols; ++c)
            {
                const size_t at = static_cast<size_t>(r) * side + c;
                const size_t cell = static_cast<size_t>(r) * mesh.Cols + c;
                mesh.CellElevations[cell] = static_cast<uint32_t>(built->Elevations[at]) |
                                            static_cast<uint32_t>(built->Elevations[at + 1]) << 8 |
                                            static_cast<uint32_t>(built->Elevations[at + side + 1]) << 16 |
                                            static_cast<uint32_t>(built->Elevations[at + side]) << 24;

                // The tile MCTerrainBlock::Draw would draw: none for a negative number or a tile that can't be read.
                const int32_t textureData = built->Textures[at];
                uint32_t index = MCTerrainMesh::NoTile;

                if (textureData >= 0)
                {
                    if (const auto found = tiles.find(textureData); found != tiles.end())
                    {
                        index = found->second;
                    }
                    else
                    {
                        // A tile whose data doesn't fit its table (issue #25) is drawn as none, as the 2D drawer does.
                        if (const std::span<const uint8_t> data = tileCache.Lookup(textureData);
                            !data.empty() && VfxTileFits(data))
                        {
                            uint32_t size = 0;
                            std::memcpy(&size, data.subspan(4 + static_cast<size_t>(data[2]) * 4, sizeof(size)).data(),
                                        sizeof(size));
                            index = static_cast<uint32_t>(mesh.Tiles.size());
                            mesh.Tiles.emplace_back(data.begin(), data.begin() + size);
                        }

                        tiles.emplace(textureData, index);
                    }
                }

                mesh.CellTiles[cell] = index;
            }
        }

        GroundMesh = std::move(built);
    }

    /// <summary>Whether the mesh's vertices take in the grid's, from (<paramref name="row"/>, <paramref name="col"/>).</summary>
    bool MeshCovers(int32_t row, int32_t col, int32_t perSide)
    {
        const int32_t first = GroundMesh->Mesh.FirstRow;
        return row >= first && col >= first && row + perSide <= first + GroundMesh->Side &&
               col + perSide <= first + GroundMesh->Side;
    }
}

std::expected<void, std::string> MCTerrainGroundFrame(const MCTerrainWindow& window, int32_t hazeFactor, int32_t stepX,
                                                      int32_t stepY, int32_t elevStep, int32_t minX, int32_t maxX,
                                                      int32_t minY, int32_t maxY, MCTerrainFrame& frame)
{
    const int32_t perSide = window.Side();
    const std::vector<MCVertex>& vertices = window.Vertices;

    if (UseOldProject != 0)
    {
        return std::unexpected("the ground mesh needs whole-pixel vertex steps (useOldProject off)");
    }

    if (perSide < 2 || std::cmp_not_equal(vertices.size(), perSide * perSide) ||
        std::cmp_not_equal(window.Blocks.size(), (perSide - 1) * (perSide - 1)))
    {
        return std::unexpected(std::format("the terrain grid has {} vertices and {} blocks for {} vertices a side",
                                           vertices.size(), window.Blocks.size(), perSide));
    }

    const std::shared_ptr<MCTerrain> terrain = Terrain();

    if (terrain == nullptr || terrain->MapBlocks == nullptr || terrain->Tiles == nullptr || GlobalPane == nullptr)
    {
        return std::unexpected("the ground mesh is asked for without a terrain");
    }

    if (HomeTeam() == nullptr)
    {
        return std::unexpected("the ground mesh has no fog of war flags to read");
    }

    MCByteFlag& fog = terrain->HomeVisibleBits();
    const MCMapBlockManager& blocks = *terrain->MapBlocks;

    // The grid's corner, from its first vertex's map position.
    const uint32_t firstTile = vertices[0].PosTile;
    const int32_t firstRow = static_cast<int32_t>(firstTile) >> 16;
    const int32_t firstCol = static_cast<int16_t>(firstTile & 0xffff);

    if (GroundMesh == nullptr || GroundMesh->StepX != stepX || GroundMesh->StepY != stepY ||
        GroundMesh->ElevStep != elevStep || !MeshCovers(firstRow, firstCol, perSide))
    {
        BuildGroundMesh(perSide + 2, stepX, stepY, elevStep);

        if (!MeshCovers(firstRow, firstCol, perSide))
        {
            return std::unexpected(std::format(
                "the ground mesh doesn't cover the terrain grid from map vertex ({}, {})", firstRow, firstCol));
        }
    }

    // Each grid vertex must be the map's vertex at its place, projected where the mesh puts it. The mesh is built again
    // when the map's own data changed since (SetTile).
    const int32_t originX = vertices[0].Px - (firstCol - firstRow) * stepX;
    const int32_t originY = vertices[0].Py - (firstRow + firstCol) * stepY +
                            static_cast<int32_t>(blocks.Vertex(vertices[0].MapVertex).Elevation) * elevStep;

    for (bool rebuilt = false;;)
    {
        bool current = true;

        for (size_t i = 0; i < vertices.size(); ++i)
        {
            const MCVertex& vertex = vertices[i];
            const int32_t row = firstRow + static_cast<int32_t>(i) / perSide;
            const int32_t col = firstCol + static_cast<int32_t>(i) % perSide;
            const MCPrecompVertex& mapVertex = blocks.Vertex(vertex.MapVertex);
            const uint32_t elevation = mapVertex.Elevation;

            if (vertex.PosTile != static_cast<uint32_t>(row * 0x10000) + (static_cast<uint32_t>(col) & 0xffff))
            {
                return std::unexpected(std::format("terrain grid vertex {} isn't map vertex ({}, {}) (posTile {:08x})",
                                                   i, row, col, vertex.PosTile));
            }

            // OB-102's wrapped grid lands here.
            if (vertex.MapVertex != blocks.MapVertexIndex(row, col))
            {
                return std::unexpected(std::format(
                    "terrain grid vertex {} at map vertex ({}, {}) holds another vertex's data", i, row, col));
            }

            if (!vertex.Redraw)
            {
                return std::unexpected(std::format("terrain grid vertex {} isn't redrawn this frame", i));
            }

            const int32_t x = originX + (col - row) * stepX;
            const int32_t y = originY + (row + col) * stepY - static_cast<int32_t>(elevation) * elevStep;

            if (vertex.Px != x || vertex.Py != y)
            {
                return std::unexpected(std::format("terrain grid vertex {} projects to ({}, {}); the mesh puts it at "
                                                   "({}, {})",
                                                   i, vertex.Px, vertex.Py, x, y));
            }

            const size_t at = static_cast<size_t>(row - GroundMesh->Mesh.FirstRow) * GroundMesh->Side +
                              static_cast<size_t>(col - GroundMesh->Mesh.FirstCol);
            current =
                current && GroundMesh->Elevations[at] == elevation && GroundMesh->Textures[at] == mapVertex.TextureData;
        }

        if (current)
        {
            break;
        }

        if (rebuilt)
        {
            return std::unexpected("the ground mesh doesn't hold the map's data even when built again");
        }

        BuildGroundMesh(perSide + 2, stepX, stepY, elevStep);
        rebuilt = true;
    }

    frame.Mesh = GroundMesh->Mesh;
    frame.OriginX = originX;
    frame.OriginY = originY;
    frame.StepX = stepX;
    frame.StepY = stepY;
    frame.ElevStep = elevStep;
    frame.FirstRow = firstRow;
    frame.FirstCol = firstCol;
    frame.LastRow = firstRow + perSide - 2;
    frame.LastCol = firstCol + perSide - 2;
    frame.MinX = minX;
    frame.MaxX = maxX;
    frame.MinY = minY;
    frame.MaxY = maxY;

    // VfxNTileDraw's clip: the pane within its window and view.
    const std::shared_ptr<MCWindow> target = GlobalPane->Window.lock();

    if (target == nullptr)
    {
        return std::unexpected("the world pane's window is gone");
    }

    int32_t x0 = std::max(GlobalPane->X0, 0);
    int32_t y0 = std::max(GlobalPane->Y0, 0);
    int32_t x1 = GlobalPane->X1 < target->XMax + 1 ? GlobalPane->X1 : target->XMax;
    int32_t y1 = GlobalPane->Y1 < target->YMax + 1 ? GlobalPane->Y1 : target->YMax;
    MCClipToView(*target, x0, y0, x1, y1);
    frame.PaneX = GlobalPane->X0;
    frame.PaneY = GlobalPane->Y0;
    frame.Clip = MCRect{x0, y0, x1, y1};
    frame.Fog = fog.Window();
    frame.AllFilled = hazeFactor == 0x7fff;

    if (!frame.AllFilled)
    {
        for (uint32_t seen = 1; seen <= 3; ++seen)
        {
            frame.Haze[seen - 1] = MCTerrainHazePalette(hazeFactor, seen);
        }
    }

    return {};
}

void MCTerrainForgetMesh()
{
    GroundMesh.reset();
}

const MCTerrainMesh* MCTerrainMeshInUse()
{
    return GroundMesh != nullptr ? &GroundMesh->Mesh : nullptr;
}

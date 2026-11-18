#include "stdafx.h"
#include "terrain/MCGroundMesh.h"
#include "terrain/MCGroundNames.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "engine/MCByteFlag.h"
#include "main/MCMissionGlobals.h"
#include "object/MCElemental.h"
#include "object/MCElementalType.h"
#include "object/MCElementalGameSystem.h"
#include "object/MCForces.h"
#include "platform/MCFrameLog.h"
#include "platform/MCRenderer.h"
#include "platform/MCSoftwareRenderer.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCSceneLights.h"
#include "terrain/MCMapBlockManager.h"
#include "terrain/MCTerrain.h"
#include "terrain/MCTerrainTiles.h"
#include "terrain/MCTerrainWindow.h"
#include "vfx/MCVfx.h"
#include "vfx/MCVfxClip.h"
#include "vfx/MCVfxFunctions.h"

#include <unordered_set>

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

    /// <summary>Port-only: MCTerrainFogEdgeShades, as the last ground frame drew the fog's edge.</summary>
    using FogEdgeShade = std::array<float, 3>;
    constexpr FogEdgeShade Undarkened{1.0f, 1.0f, 1.0f};
    std::array<FogEdgeShade, MCTerrainFogEdgeLevels> FogEdgeShades{Undarkened, Undarkened, Undarkened};
    /// <summary>
    /// The palette's colours the fog's edge is measured on, as pale as the snow: no darker than FogEdgeLeastLuma and
    /// no further than FogEdgeMostChroma from grey (0 to 1); all of them in a palette without any.
    /// </summary>
    constexpr float FogEdgeLeastLuma = 0.5f;
    constexpr float FogEdgeMostChroma = 0.12f;
    constexpr std::array<float, 3> PaletteLuma{0.299f, 0.587f, 0.114f};
    /// <summary>A palette channel's most (6 bits).</summary>
    constexpr float PaletteChannelMost = 63.0f;

    /// <summary>
    /// How haze table <paramref name="table"/> changes the palette's pale greys, each channel (their sum through it over
    /// their sum, weighted by their brightness); unchanged without a table.
    /// </summary>
    FogEdgeShade FogEdgeShadeOf(MCPalette& palette, std::optional<MCColorTable> table)
    {
        if (!table.has_value())
        {
            return Undarkened;
        }

        const std::span<MCVfxRgb, MCPalette::ColorCount> colors = palette.Colors();
        const auto channels = [](const MCVfxRgb& rgb)
        {
            return std::array<float, 3>{rgb.R / PaletteChannelMost, rgb.G / PaletteChannelMost,
                                        rgb.B / PaletteChannelMost};
        };

        for (const bool greysOnly : {true, false})
        {
            std::array<double, 3> through{};
            std::array<double, 3> plain{};

            for (size_t i = 0; i < colors.size(); ++i)
            {
                const std::array<float, 3> own = channels(colors[i]);
                const float luma = own[0] * PaletteLuma[0] + own[1] * PaletteLuma[1] + own[2] * PaletteLuma[2];
                const float chroma = std::max({own[0], own[1], own[2]}) - std::min({own[0], own[1], own[2]});

                if (greysOnly && (luma < FogEdgeLeastLuma || chroma > FogEdgeMostChroma))
                {
                    continue;
                }

                const std::array<float, 3> hazed = channels(colors[(*table)[i]]);

                for (size_t c = 0; c < 3; ++c)
                {
                    through[c] += luma * hazed[c];
                    plain[c] += luma * own[c];
                }
            }

            if (plain[0] + plain[1] + plain[2] > 0.0)
            {
                FogEdgeShade shade{};

                for (size_t c = 0; c < 3; ++c)
                {
                    shade[c] = plain[c] > 0.0 ? static_cast<float>(std::clamp(through[c] / plain[c], 0.0, 1.0)) : 1.0f;
                }

                return shade;
            }
        }

        return Undarkened;
    }

    /// <summary>The mesh's ring of off-map vertices is a grid's side and this many more wide.</summary>
    constexpr int32_t RingPastGrid = 2;

    /// <summary>
    /// The tile MCTerrainBlock::Draw would draw for <paramref name="textureData"/> (not negative), as the ground mesh keeps
    /// it: up to its last row's end. None for a tile that can't be read or whose data doesn't fit its table (issue #25),
    /// drawn as none, as the 2D drawer does.
    /// </summary>
    std::span<const uint8_t> GroundTile(MCTerrainTiles& tileCache, int32_t textureData)
    {
        const std::span<const uint8_t> data = tileCache.Lookup(textureData);

        if (data.empty() || !VfxTileFits(data))
        {
            return {};
        }

        uint32_t size = 0;
        std::memcpy(&size, data.subspan(4 + static_cast<size_t>(data[2]) * 4, sizeof(size)).data(), sizeof(size));
        return data.first(size);
    }

    /// <summary>
    /// Builds the ground mesh over the map and a ring of <paramref name="ring"/> off-map vertices around it, reading
    /// every tile it uses; the off-map vertices at <paramref name="offMapElevation"/> when given (else the map's).
    /// </summary>
    void BuildGroundMesh(int32_t ring, int32_t stepX, int32_t stepY, int32_t elevStep,
                         std::optional<uint8_t> offMapElevation = std::nullopt)
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
                built->Elevations[static_cast<size_t>(r) * side + c] =
                    offMapElevation.has_value() && !MCMapBlockManager::OnMap(r - ring, c - ring) ? *offMapElevation
                                                                                                 : vertex.Elevation;
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
        mesh.CellBlocked.resize(mesh.CellTiles.size());
        const MCOptionalRef<const MCScenarioMap> map = LoadedGameMap();
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

                if (map && map->get().OnMap(r - ring, c - ring))
                {
                    mesh.CellBlocked[cell] = static_cast<uint8_t>(map->get().TileAt(r - ring, c - ring).BlockedCells());
                }

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
                        if (const std::span<const uint8_t> tile = GroundTile(tileCache, textureData); !tile.empty())
                        {
                            index = static_cast<uint32_t>(mesh.Tiles.size());
                            mesh.Tiles.emplace_back(tile.begin(), tile.end());
                        }

                        tiles.emplace(textureData, index);
                    }
                }

                mesh.CellTiles[cell] = index;
            }
        }

        // Each cell's neighbours' blocked counts, so the share can be blended across the seams.
        mesh.CellBlockedRound.resize(mesh.CellTiles.size());

        for (int32_t r = 0; r < mesh.Rows; ++r)
        {
            for (int32_t c = 0; c < mesh.Cols; ++c)
            {
                uint32_t round = 0;

                for (int32_t dr = -1; dr <= 1; ++dr)
                {
                    for (int32_t dc = -1; dc <= 1; ++dc)
                    {
                        const int32_t nr = r + dr;
                        const int32_t nc = c + dc;

                        if ((dr == 0 && dc == 0) || nr < 0 || nc < 0 || nr >= mesh.Rows || nc >= mesh.Cols)
                        {
                            continue;
                        }

                        round |= static_cast<uint32_t>(mesh.CellBlocked[static_cast<size_t>(nr) * mesh.Cols + nc])
                                 << MCTerrainMesh::BlockedRoundShift(dr, dc);
                    }
                }

                mesh.CellBlockedRound[static_cast<size_t>(r) * mesh.Cols + c] = round;
            }
        }

        GroundMesh = std::move(built);
    }

    /// <summary>Whether there's a mesh, placing its vertices by these steps.</summary>
    bool MeshStepsAre(int32_t stepX, int32_t stepY, int32_t elevStep)
    {
        return GroundMesh != nullptr && GroundMesh->StepX == stepX && GroundMesh->StepY == stepY &&
               GroundMesh->ElevStep == elevStep;
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

    if (!MeshStepsAre(stepX, stepY, elevStep) || !MeshCovers(firstRow, firstCol, perSide))
    {
        BuildGroundMesh(perSide + RingPastGrid, stepX, stepY, elevStep);

        if (!MeshCovers(firstRow, firstCol, perSide))
        {
            return std::unexpected(std::format(
                "the ground mesh doesn't cover the terrain grid from map vertex ({}, {})", firstRow, firstCol));
        }
    }

    const int32_t originX = vertices[0].Px - (firstCol - firstRow) * stepX;
    const int32_t originY = vertices[0].Py - (firstRow + firstCol) * stepY +
                            static_cast<int32_t>(blocks.Vertex(vertices[0].MapVertex).Elevation) * elevStep;

    // Port-only: the cells drawn are the grid's with a corner on the pane (Vertex::clipped), as the terrain's walk
    // found them (the whole grid when none has). The grid is made for the furthest zoom on the largest screen
    // (GrownGridSide), so nearer in most of its cells have every corner clipped and draw nothing: launching them was
    // most of the terrain's GPU vertex work, the kept ground's walk over them (PlanGround) and the checks below over
    // their corners a share of the CPU's frame.
    const MCGridQuads drawn = window.QuadsOnPane().value_or(
        MCGridQuads{.FirstRow = 0, .FirstCol = 0, .LastRow = perSide - 2, .LastCol = perSide - 2});

    // The vertices checked: the drawn cells' corners, and their neighbours' too. A cell holds its neighbours' blocked
    // counts (MCTerrainMesh::CellBlockedRound), read again when a neighbour's changed tile has the mesh built again, as
    // when the check took in every vertex.
    const int32_t checkFirstRow = std::max(drawn.FirstRow - 1, 0);
    const int32_t checkFirstCol = std::max(drawn.FirstCol - 1, 0);
    const int32_t checkLastRow = std::min(drawn.LastRow + 2, perSide - 1);
    const int32_t checkLastCol = std::min(drawn.LastCol + 2, perSide - 1);

    for (bool rebuilt = false;;)
    {
        bool current = true;

        // Each vertex checked must be the map's vertex at its place, projected where the mesh puts it (the shaders read
        // a drawn cell's own data and its corners' alone). The mesh is built again when the map's own data there
        // changed since (SetTile); the kept ground is drawn again whole when it is.
        for (int32_t r = checkFirstRow; r <= checkLastRow; ++r)
        {
            for (int32_t c = checkFirstCol; c <= checkLastCol; ++c)
            {
                const size_t i = static_cast<size_t>(r) * perSide + c;
                const int32_t row = firstRow + r;
                const int32_t col = firstCol + c;
                const MCVertex& vertex = vertices[i];
                const MCPrecompVertex& mapVertex = blocks.Vertex(vertex.MapVertex);
                const uint32_t elevation = mapVertex.Elevation;

                if (vertex.PosTile != static_cast<uint32_t>(row * 0x10000) + (static_cast<uint32_t>(col) & 0xffff))
                {
                    return std::unexpected(
                        std::format("terrain grid vertex {} isn't map vertex ({}, {}) (posTile {:08x})", i, row, col,
                                    vertex.PosTile));
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
                    return std::unexpected(std::format("terrain grid vertex {} projects to ({}, {}); the mesh puts it "
                                                       "at ({}, {})",
                                                       i, vertex.Px, vertex.Py, x, y));
                }

                const size_t at = static_cast<size_t>(row - GroundMesh->Mesh.FirstRow) * GroundMesh->Side +
                                  static_cast<size_t>(col - GroundMesh->Mesh.FirstCol);
                current = current && GroundMesh->Elevations[at] == elevation &&
                          GroundMesh->Textures[at] == mapVertex.TextureData;
            }
        }

        if (current)
        {
            break;
        }

        if (rebuilt)
        {
            return std::unexpected("the ground mesh doesn't hold the map's data even when built again");
        }

        BuildGroundMesh(perSide + RingPastGrid, stepX, stepY, elevStep);
        rebuilt = true;
    }

    frame.Mesh = GroundMesh->Mesh;
    frame.OriginX = originX;
    frame.OriginY = originY;
    frame.StepX = stepX;
    frame.StepY = stepY;
    frame.ElevStep = elevStep;
    frame.FirstRow = firstRow + drawn.FirstRow;
    frame.FirstCol = firstCol + drawn.FirstCol;
    frame.LastRow = firstRow + drawn.LastRow;
    frame.LastCol = firstCol + drawn.LastCol;

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
            FogEdgeShades[seen - 1] = FogEdgeShadeOf(GamePalette(), frame.Haze[seen - 1]);
        }
    }

    return {};
}

namespace
{
    /// <summary>Where a fast shape's header keeps its height and its width less one (as the renderer reads them).</summary>
    constexpr size_t FastShapeHeightAt = 8;
    constexpr size_t FastShapeWidthAt = 10;

    /// <summary>
    /// Each distinct slot (not negative) the map's vertices name in <paramref name="slotOf"/>'s field, in the order the
    /// vertices first name it, from row and column <paramref name="from"/> (negative: the off-map ring's vertices too,
    /// the off-map block's, MapVertexIndex).
    /// </summary>
    template <typename SlotOf>
    std::vector<int32_t> DistinctSlots(MCMapBlockManager& blocks, int32_t from, SlotOf slotOf)
    {
        const int32_t mapSide = MCTerrain::BlocksMapSide * MCTerrain::VerticesBlockSide;
        std::unordered_set<int32_t> seen;
        std::vector<int32_t> slots;

        for (int32_t r = from; r < mapSide; ++r)
        {
            for (int32_t c = from; c < mapSide; ++c)
            {
                const int32_t slot = slotOf(blocks.MapVertexAt(r, c));

                if (slot >= 0 && seen.insert(slot).second)
                {
                    slots.push_back(slot);
                }
            }
        }

        return slots;
    }

    /// <summary>The slots of the map's ground tiles, a block's width of off-map vertices too (the mesh's ring).</summary>
    std::vector<int32_t> GroundSlots(MCMapBlockManager& blocks)
    {
        return DistinctSlots(blocks, -MCTerrain::VerticesBlockSide,
                             [](const auto& vertex) { return static_cast<int32_t>(vertex.TextureData); });
    }

    /// <summary>The slots of the map's overlays.</summary>
    std::vector<int32_t> OverlaySlots(MCMapBlockManager& blocks)
    {
        return DistinctSlots(blocks, 0, [](const auto& vertex) { return static_cast<int32_t>(vertex.OverlayData); });
    }

    /// <summary>Whether the HD ground is drawn: HD art on and the GPU drawing (the software renderer draws no HD art).</summary>
    bool HdGroundDrawn(const MCTerrain& terrain)
    {
        return MCHdArt::Active() && MCRenderer::GpuDrawing() != MCGpuDrawing::Off && terrain.MapBlocks != nullptr &&
               terrain.Tiles != nullptr;
    }

    /// <summary>
    /// Port-only (PLAN.md's first-use item 10): whether the overlays the program's table names are rebuilt from their HD
    /// frames rather than read: the HD ground drawn, and the pack's overlays of version 2, whose frames mark the pixels
    /// the original draws apart from those their indices spread to (MCRebuildGroundOverlay).
    /// </summary>
    bool HdOverlaysRebuilt(const MCTerrain& terrain)
    {
        return HdGroundDrawn(terrain) && MCHdArt::MarksOriginalPixels(MCHdArt::OverlayFolder);
    }

    /// <summary>Whether <paramref name="known"/> names slot <paramref name="slot"/>'s overlay and carries its hot spot.</summary>
    bool OverlayKnown(const MCKnownGroundNames& known, int32_t slot)
    {
        return known.At(slot).Overlay.has_value() && known.SpotAt(slot).has_value();
    }

    /// <summary>
    /// The mines' tiles MCTerrainReadMinesAhead left unread for MCTerrainReadHdTiles to rebuild from their HD frames, and
    /// the terrain whose they are; the load's, on the game's thread.
    /// </summary>
    struct MinesLeft
    {
        std::weak_ptr<const MCTerrain> Of;
        std::vector<int32_t> Slots;
    };

    MinesLeft& MinesLeftToRebuild()
    {
        static MinesLeft left;
        return left;
    }

    /// <summary>
    /// The map's ground tiles, each once, as the ground mesh draws them (MCTerrainReadHdTiles): none while HD art is off
    /// or the software renderer draws.
    /// </summary>
    std::vector<MCHdArt::GroundAhead> HdGroundTiles()
    {
        const std::shared_ptr<MCTerrain> terrain = Terrain();
        return terrain != nullptr && HdGroundDrawn(*terrain) ? MCTerrainHdGroundByBytes(*terrain).Tiles
                                                             : std::vector<MCHdArt::GroundAhead>();
    }

    /// <summary>The names in <paramref name="slots"/>' entries of <paramref name="known"/> that <paramref name="part"/> holds.</summary>
    std::vector<MCHdArt::GroundAhead> NamedAhead(const MCKnownGroundNames& known, std::span<const int32_t> slots,
                                                 std::optional<MCGroundName> MCGroundSlotNames::* part)
    {
        std::vector<MCHdArt::GroundAhead> ahead;

        for (const int32_t slot : slots)
        {
            if (const std::optional<MCGroundName> name = known.At(slot).*part)
            {
                ahead.push_back(MCHdArt::GroundAhead{{}, name->Width, name->Height, name->Key});
            }
        }

        return ahead;
    }
}

MCHdArt::GroundAhead MCGroundTileOf(MCTerrainTiles& tiles, int32_t slot)
{
    if (const std::span<const uint8_t> tile = GroundTile(tiles, slot); !tile.empty())
    {
        if (const int32_t width = VfxTileWidth(tile); width != 0)
        {
            return MCHdArt::GroundAhead{tile, width, tile[2]};
        }
    }

    return {};
}

std::optional<MCHdArt::GroundAhead> MCGroundOverlayOf(MCTerrainTiles& tiles, int32_t slot)
{
    const std::optional<std::span<const uint8_t>> shape = FastShapeAt(tiles.Lookup(slot), 0);
    const std::optional<size_t> size = shape.has_value() ? FastShapeSize(*shape) : std::nullopt;

    if (!size.has_value())
    {
        return std::nullopt;
    }

    return MCHdArt::GroundAhead{shape->first(*size), MCRead16(*shape, FastShapeWidthAt) + 1,
                                MCRead16(*shape, FastShapeHeightAt)};
}

MCHdGroundAsks MCTerrainHdGroundByBytes(MCTerrain& terrain)
{
    MCHdGroundAsks asks;

    if (terrain.MapBlocks == nullptr || terrain.Tiles == nullptr)
    {
        return asks;
    }

    for (const int32_t slot : GroundSlots(*terrain.MapBlocks))
    {
        if (MCHdArt::GroundAhead tile = MCGroundTileOf(*terrain.Tiles, slot); !tile.Tile.empty())
        {
            asks.Tiles.push_back(tile);
        }
    }

    for (const int32_t slot : OverlaySlots(*terrain.MapBlocks))
    {
        if (std::optional<MCHdArt::GroundAhead> overlay = MCGroundOverlayOf(*terrain.Tiles, slot))
        {
            asks.Overlays.push_back(*overlay);
        }
    }

    return asks;
}

std::optional<MCHdGroundAsks> MCTerrainHdGroundByName(const MCTerrain& terrain)
{
    if (terrain.MapBlocks == nullptr || terrain.Tiles == nullptr)
    {
        return std::nullopt;
    }

    const MCOptionalRef<const MCKnownGroundNames> known = MCKnownGroundNamesOf(*terrain.Tiles);

    if (!known.has_value())
    {
        return std::nullopt;
    }

    return MCHdGroundAsks{NamedAhead(*known, GroundSlots(*terrain.MapBlocks), &MCGroundSlotNames::Tile),
                          NamedAhead(*known, OverlaySlots(*terrain.MapBlocks), &MCGroundSlotNames::Overlay)};
}

void MCTerrainLeaveRebuiltTiles(const MCTerrain& terrain, std::vector<int32_t>& slots)
{
    const MCOptionalRef<const MCKnownGroundNames> known =
        HdGroundDrawn(terrain) ? MCKnownGroundNamesOf(*terrain.Tiles) : std::nullopt;

    if (!known.has_value())
    {
        return;
    }

    // The map's ground tiles the table holds, and its overlays with a version 2 pack, as RebuildGroundFromHd goes
    // through them; any other slot (the .pre's that the map doesn't name, an overlay of a version 1 pack) is read.
    std::unordered_set<int32_t> rebuilt;

    for (const int32_t slot : GroundSlots(*terrain.MapBlocks))
    {
        if (known->get().ShapeAt(slot).has_value())
        {
            rebuilt.insert(slot);
        }
    }

    if (HdOverlaysRebuilt(terrain))
    {
        for (const int32_t slot : OverlaySlots(*terrain.MapBlocks))
        {
            if (OverlayKnown(*known, slot))
            {
                rebuilt.insert(slot);
            }
        }
    }

    std::erase_if(slots, [&rebuilt](int32_t slot) { return rebuilt.contains(slot); });
}

bool MCTerrainAskHdGroundByName(const MCTerrain& terrain)
{
    if (!HdGroundDrawn(terrain))
    {
        return false;
    }

    const std::optional<MCHdGroundAsks> asks = MCTerrainHdGroundByName(terrain);

    if (!asks.has_value())
    {
        return false;
    }

    MCHdArt::ReadGroundAhead(asks->Tiles, MCSceneLights::ReliefMapsOn());
    MCHdArt::ReadOverlaysAhead(asks->Overlays);
    return true;
}

std::vector<int32_t> MCTerrainMineSlots(const MCTerrain& terrain, MCMinesAhead cells)
{
    std::vector<int32_t> slots;

    if (terrain.MapBlocks == nullptr)
    {
        return slots;
    }

    const MCScenarioMap& map = GameMap();
    const MCMapBlockManager& blocks = *terrain.MapBlocks;
    const auto level = [&blocks](int32_t row, int32_t col) { return uint32_t{blocks.MapVertexAt(row, col).Elevation}; };
    bool mined = false;

    for (int32_t row = 0; row < map.Height; ++row)
    {
        for (int32_t col = 0; col < map.Width; ++col)
        {
            // Either side's, as the one playing it (or god mode) sees them.
            const MCMineView view = MCTerrainMineView(map.TileAt(row, col).Overlay, true, true);
            mined = mined || view.InnerSphereMine || view.ClanMine || view.Crater;

            if (cells == MCMinesAhead::Mined && !view.InnerSphereMine && !view.ClanMine)
            {
                continue;
            }

            // The cell's corners as a terrain block has them: top-left, top-right, bottom-right, bottom-left.
            const int32_t slot =
                MCMineTileNumber(level(row, col), level(row, col + 1), level(row + 1, col + 1), level(row + 1, col));

            if (!std::ranges::contains(slots, slot))
            {
                slots.push_back(slot);
            }
        }
    }

    if (mined || (cells == MCMinesAhead::EveryCell && !slots.empty()))
    {
        slots.push_back(MCMineCraterTile);
    }

    return slots;
}

void MCTerrainReadMinesAhead(MCMinesAhead cells)
{
    const std::shared_ptr<MCTerrain> terrain = Terrain();

    if (terrain == nullptr || terrain->Tiles == nullptr)
    {
        return;
    }

    const std::vector<int32_t> slots = MCTerrainMineSlots(*terrain, cells);

    if (slots.empty())
    {
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    const bool hdGround = HdGroundDrawn(*terrain);
    const MCOptionalRef<const MCKnownGroundNames> known =
        hdGround ? MCKnownGroundNamesOf(*terrain->Tiles) : std::nullopt;

    // By name, the HD overlays' reads start before the original's.
    if (known.has_value())
    {
        MCHdArt::ReadOverlaysAhead(NamedAhead(*known, slots, &MCGroundSlotNames::Overlay));
    }

    // With a version 2 pack, those the table names are rebuilt from their HD frames with the ground
    // (MCTerrainReadHdTiles) rather than read.
    std::vector<int32_t> toRead;
    size_t left = 0;

    if (known.has_value() && HdOverlaysRebuilt(*terrain))
    {
        MinesLeft& mines = MinesLeftToRebuild();

        if (mines.Of.lock() != terrain)
        {
            mines = MinesLeft{terrain, {}};
        }

        for (const int32_t slot : slots)
        {
            if (!OverlayKnown(*known, slot))
            {
                toRead.push_back(slot);
                continue;
            }

            ++left;

            if (!std::ranges::contains(mines.Slots, slot))
            {
                mines.Slots.push_back(slot);
            }
        }
    }
    else
    {
        toRead = slots;
    }

    const MCTileReads read = terrain->Tiles->ReadTiles(toRead);

    if (hdGround && !known.has_value())
    {
        std::vector<MCHdArt::GroundAhead> overlays;

        for (const int32_t slot : slots)
        {
            if (std::optional<MCHdArt::GroundAhead> overlay = MCGroundOverlayOf(*terrain->Tiles, slot))
            {
                overlays.push_back(*overlay);
            }
        }

        MCHdArt::ReadOverlaysAhead(overlays);
    }

    if (MCFrameLog::Enabled())
    {
        MCFrameLog::Note(std::format(
            "mines' tiles ({}): {}, {} left to rebuild from HD, {} read in {} reads, {:.1f} ms",
            cells == MCMinesAhead::EveryCell ? "every cell's" : "the mined cells'", slots.size(), left, read.Tiles,
            read.Reads, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()));
    }
}

void MCTerrainReadHdTilesAhead()
{
    const std::shared_ptr<MCTerrain> terrain = Terrain();

    // By name when the program knows the tile set (the readers pass over what the terrain's load asked for already),
    // so no tile is hashed; otherwise by the tiles' own bytes.
    if (terrain == nullptr || !HdGroundDrawn(*terrain) || MCTerrainAskHdGroundByName(*terrain))
    {
        return;
    }

    const MCHdGroundAsks asks = MCTerrainHdGroundByBytes(*terrain);
    MCHdArt::ReadGroundAhead(asks.Tiles, MCSceneLights::ReliefMapsOn());
    MCHdArt::ReadOverlaysAhead(asks.Overlays);
}

namespace
{
    /// <summary>
    /// Port-only (PLAN.md's first-use item 10): with a version 2 pack (HdOverlaysRebuilt), each overlay of the map and of
    /// its mines (those MCTerrainReadMinesAhead left) that the terrain's load left out, rebuilt from its HD frame
    /// (MCRebuildGroundOverlay) and put in the cache; one that isn't (no frame, or not the overlay's bytes) is added to
    /// <paramref name="unread"/>. How many it rebuilt.
    /// </summary>
    size_t RebuildOverlaysFromHd(MCTerrain& terrain, const MCKnownGroundNames& known, std::vector<int32_t>& unread)
    {
        MinesLeft& mines = MinesLeftToRebuild();
        std::vector<int32_t> slots = OverlaySlots(*terrain.MapBlocks);

        if (mines.Of.lock().get() == &terrain)
        {
            for (const int32_t slot : mines.Slots)
            {
                if (!std::ranges::contains(slots, slot))
                {
                    slots.push_back(slot);
                }
            }
        }

        mines = MinesLeft{};

        if (!HdOverlaysRebuilt(terrain))
        {
            return 0;
        }

        MCTerrainTiles& tiles = *terrain.Tiles;
        size_t rebuilt = 0;

        for (const int32_t slot : slots)
        {
            const MCOptionalRef<const MCTerrainTile> cached = tiles.Slot(slot);

            if (!OverlayKnown(known, slot) || !cached.has_value() || cached->get().Missing ||
                !cached->get().TileData().empty())
            {
                continue;
            }

            const MCGroundName name = *known.At(slot).Overlay;
            const std::shared_ptr<const MCHdArt::Frame> frame =
                MCHdArt::FindOverlayByKey(name.Key, name.Width, name.Height);
            const std::optional<std::vector<uint8_t>> packet =
                frame != nullptr ? MCRebuildGroundOverlay(MCHdArt::IndicesOf(*frame), name, *known.SpotAt(slot))
                                 : std::nullopt;

            if (packet.has_value() && tiles.Put(slot, *packet))
            {
                ++rebuilt;
                continue;
            }

            unread.push_back(slot);
        }

        return rebuilt;
    }

    /// <summary>
    /// MCTerrainReadHdTiles for a tile set the program knows (<paramref name="known"/>): each of the map's ground tiles'
    /// HD frames in hand by name, and each original tile not cached rebuilt from its frame, or read where it can't be.
    /// </summary>
    MCGroundRebuilt RebuildGroundFromHd(MCTerrain& terrain, const MCKnownGroundNames& known)
    {
        MCHdArt::WaitForLoadReads();
        MCHdArt::KeepReadAhead();
        const auto start = std::chrono::steady_clock::now();
        MCTerrainTiles& tiles = *terrain.Tiles;
        std::vector<int32_t> unread;
        size_t rebuilt = 0;

        for (const int32_t slot : GroundSlots(*terrain.MapBlocks))
        {
            const std::optional<MCGroundName> name = known.At(slot).Tile;

            if (!name.has_value())
            {
                continue;
            }

            const std::shared_ptr<const MCHdArt::Frame> frame =
                MCHdArt::FindTerrainTileByKey(name->Key, name->Width, name->Height);

            if (MCSceneLights::ReliefMapsOn())
            {
                MCHdArt::FindTerrainReliefByKey(name->Key, name->Width, name->Height);
            }

            const MCOptionalRef<const MCTerrainTile> cached = tiles.Slot(slot);

            if (!cached.has_value() || cached->get().Missing || !cached->get().TileData().empty())
            {
                continue;
            }

            const std::optional<MCGroundTileShape> shape = known.ShapeAt(slot);
            const std::optional<std::vector<uint8_t>> packet =
                frame != nullptr && shape.has_value() ? MCRebuildGroundTile(MCHdArt::IndicesOf(*frame), *name, *shape)
                                                      : std::nullopt;

            if (packet.has_value() && tiles.Put(slot, *packet))
            {
                ++rebuilt;
                continue;
            }

            unread.push_back(slot);
        }

        const size_t overlays = RebuildOverlaysFromHd(terrain, known, unread);
        const MCTileReads read = tiles.ReadTiles(unread);

        // A pack that doesn't match the game's tiles costs only the reads, so say so where it would go unseen.
        if (!unread.empty())
        {
            SDL_Log(
                "HD art: %zu of the map's %zu ground tiles and overlays weren't rebuilt from the pack (no frame, or "
                "not the tile's bytes), so they were read",
                unread.size(), unread.size() + rebuilt + overlays);
        }

        if (MCFrameLog::Enabled())
        {
            MCFrameLog::Note(std::format(
                "ground tiles: {} rebuilt from HD, {} overlays rebuilt from HD, {} read in {} reads, {:.1f} ms",
                rebuilt, overlays, read.Tiles, read.Reads,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()));
        }

        return MCGroundRebuilt{rebuilt, read.Tiles, overlays};
    }
} // namespace

MCGroundRebuilt MCTerrainReadHdTiles()
{
    const std::shared_ptr<MCTerrain> terrain = Terrain();

    if (terrain != nullptr && HdGroundDrawn(*terrain))
    {
        if (const MCOptionalRef<const MCKnownGroundNames> known = MCKnownGroundNamesOf(*terrain->Tiles))
        {
            return RebuildGroundFromHd(*terrain, known->get());
        }
    }

    const std::vector<MCHdArt::GroundAhead> tiles = HdGroundTiles();

    if (tiles.empty())
    {
        return {};
    }

    // Those read ahead (MCTerrainReadHdTilesAhead) are kept; any left, or whose read failed, are read here.
    MCHdArt::WaitForLoadReads();
    MCHdArt::KeepReadAhead();

    for (const MCHdArt::GroundAhead& tile : tiles)
    {
        MCHdArt::FindTerrainTile(tile.Tile, tile.Width, tile.Height);

        // Port-only: its relief map too, when the HD ground takes them.
        if (MCSceneLights::ReliefMapsOn())
        {
            MCHdArt::FindTerrainRelief(tile.Tile, tile.Width, tile.Height);
        }
    }

    return {};
}

MCTerrainMeshSteps MCTerrainScreenSteps(const MCCamera& camera)
{
    return MCTerrainMeshSteps{static_cast<int32_t>(std::floor(camera.CosAngle * MCTerrain::MetersPerVertex)),
                              static_cast<int32_t>(std::floor(camera.SinAngle * MCTerrain::MetersPerVertex)),
                              static_cast<int32_t>(std::floor(MCTerrain::MetersPerElevLevel))};
}

std::expected<void, std::string> MCTerrainUploadGround()
{
    const std::shared_ptr<MCTerrain> terrain = Terrain();
    const std::shared_ptr<MCRenderer> hardware = MCRenderer::Hardware();

    // The ground mesh needs whole-pixel vertex steps (MCTerrainGroundFrame).
    if (hardware == nullptr || MCRenderer::GpuDrawing() == MCGpuDrawing::Off || UseOldProject != 0 ||
        terrain == nullptr || terrain->MapBlocks == nullptr || terrain->Tiles == nullptr)
    {
        return {};
    }

    // The first frame's mesh: its terrain pass builds one for the first active camera's grid, with that camera's
    // steps, where none was built for them (MCTerrainGroundFrame).
    for (int32_t i = 0; const std::shared_ptr<MCTerrainWindow> window = terrain->GetTerrainWindow(i); ++i)
    {
        const std::shared_ptr<MCCamera> camera = window->Camera.lock();

        if (camera == nullptr || !camera->Active)
        {
            continue;
        }

        const MCTerrainMeshSteps steps = MCTerrainScreenSteps(*camera);
        const int32_t ring = window->Side() + RingPastGrid;

        // The first frame is drawn on the first turn, which raises the off-map ground (MCMapBlockManager::Update): before
        // it, the mesh has it raised already, as that frame will find it.
        if (!MeshStepsAre(steps.Across, steps.Down, steps.Up) || GroundMesh->Mesh.FirstRow != -ring)
        {
            BuildGroundMesh(ring, steps.Across, steps.Down, steps.Up,
                            Turn < 1 ? std::optional(MCMapBlockManager::RaisedOffMapElevation()) : std::nullopt);
        }

        std::expected<void, std::string> uploaded = hardware->PrepareTerrain(GroundMesh->Mesh);

        // The GPU holds the ground's HD tiles now; their copies in memory would only take room from the mission's art.
        if (uploaded)
        {
            MCHdArt::DropGround();
        }

        return uploaded;
    }

    return {};
}

void MCTerrainForgetMesh()
{
    GroundMesh.reset();
}

std::array<std::array<float, 3>, MCTerrainFogEdgeLevels> MCTerrainFogEdgeShades()
{
    return FogEdgeShades;
}

MCOptionalRef<const MCTerrainMesh> MCTerrainMeshInUse()
{
    return GroundMesh != nullptr ? MCOptionalRef<const MCTerrainMesh>(GroundMesh->Mesh) : std::nullopt;
}

std::optional<MCTerrainMeshSteps> MCTerrainMeshStepsInUse()
{
    if (GroundMesh == nullptr)
    {
        return std::nullopt;
    }

    return MCTerrainMeshSteps{GroundMesh->StepX, GroundMesh->StepY, GroundMesh->ElevStep};
}

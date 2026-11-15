#pragma once

#include "lib/MCOptionalRef.h"
#include "sprite/MCHdArt.h"

class MCCamera;
class MCTerrain;
class MCTerrainTiles;
class MCTerrainWindow;
struct MCTerrainMesh;
struct MCTerrainFrame;

/// <summary>
/// Port: the frame's terrain pass as a hardware renderer draws it from the map's ground mesh (built here, and again
/// when the map's data changed since). Checks that every vertex of <paramref name="window"/>'s grid (projected this
/// frame) is the map's vertex at its place and lies where the mesh puts it; an error, saying which and how, when one
/// doesn't (the mesh would draw another picture than the tiles).
/// </summary>
/// <param name="minX">The corner range of the projection (a vertex outside it is clipped); also maxX, minY, maxY.</param>
std::expected<void, std::string> MCTerrainGroundFrame(const MCTerrainWindow& window, int32_t hazeFactor, int32_t stepX,
                                                      int32_t stepY, int32_t elevStep, int32_t minX, int32_t maxX,
                                                      int32_t minY, int32_t maxY, MCTerrainFrame& frame);

/// <summary>
/// Port-only (SPEC-first-use.md): has HD art's readers read the pack's copies of the map's ground tiles and their relief
/// maps as soon as the terrain is in (MCHdArt::ReadGroundAhead), several at once while the rest of the mission loads:
/// by the names the program carries for its tile set when it knows it (MCTerrainAskHdGroundByName), otherwise by the
/// tiles' own bytes. Does nothing while HD art is off or the software renderer draws.
/// </summary>
void MCTerrainReadHdTilesAhead();

/// <summary>Port-only (HD art): a map's HD ground to ask for, each tile and each overlay once, as the vertices first name it.</summary>
struct MCHdGroundAsks
{
    std::vector<MCHdArt::GroundAhead> Tiles;
    std::vector<MCHdArt::GroundAhead> Overlays;
};

/// <summary>
/// Port-only (HD art): <paramref name="terrain"/>'s HD ground by its tiles' own bytes (each read when it isn't cached),
/// as the ground mesh and the renderer find their frames.
/// </summary>
MCHdGroundAsks MCTerrainHdGroundByBytes(MCTerrain& terrain);

/// <summary>
/// Port-only (PLAN.md's first-use item 6, Part B): <paramref name="terrain"/>'s HD ground by the names the program
/// carries for its tile set (terrain/MCGroundNames.h), reading no tile; none for a tile set the table lacks. The same
/// asks as MCTerrainHdGroundByBytes, keyed rather than holding the tiles' bytes.
/// </summary>
std::optional<MCHdGroundAsks> MCTerrainHdGroundByName(const MCTerrain& terrain);

/// <summary>
/// Port-only (PLAN.md's first-use item 6, Part B): as MCTerrainReadHdTilesAhead, as soon as <paramref name="terrain"/>'s
/// map blocks are in and before a tile is read, by name (MCTerrainHdGroundByName). Whether it asked: not while HD art
/// is off, the software renderer draws, or for a tile set the program's table lacks (MCTerrainReadHdTilesAhead then
/// asks by the tiles' own bytes).
/// </summary>
bool MCTerrainAskHdGroundByName(const MCTerrain& terrain);

/// <summary>
/// Port-only (PLAN.md's first-use item 10): takes out of <paramref name="slots"/> those whose ground tile
/// MCTerrainReadHdTiles rebuilds from its HD frame rather than reading it (the map's ground tiles the table holds):
/// while the HD ground is drawn, for a tile set the program's table holds (as MCTerrainAskHdGroundByName asks), so the
/// terrain's load reads only the rest.
/// </summary>
void MCTerrainLeaveRebuiltTiles(const MCTerrain& terrain, std::vector<int32_t>& slots);

/// <summary>
/// Port-only (HD art): slot <paramref name="slot"/> of <paramref name="tiles"/> as a ground tile, as the ground mesh
/// keeps it (up to its last row's end) and the pack names it, with its width and height; its tile empty when the slot
/// holds no tile the mesh would draw. Reads the tile when it isn't cached.
/// </summary>
MCHdArt::GroundAhead MCGroundTileOf(MCTerrainTiles& tiles, int32_t slot);

/// <summary>
/// Port-only (HD art): slot <paramref name="slot"/> of <paramref name="tiles"/> as an overlay, as the renderer finds its
/// HD frame: its fast shape 0 up to its last row's end, and its size; none when it holds none. Reads the tile when it
/// isn't cached.
/// </summary>
std::optional<MCHdArt::GroundAhead> MCGroundOverlayOf(MCTerrainTiles& tiles, int32_t slot);

/// <summary>
/// Port-only (PLAN.md's first-use item 10): the tiles <paramref name="terrain"/>'s mines can draw at the start, each
/// once: each mined cell's laid mine, either side's, by its corners' levels (as MCTerrainBlock::DrawMineTile picks it),
/// and the crater when the map has a mine. The map's vertices name none of them, so the ground's asks pass them by.
/// </summary>
std::vector<int32_t> MCTerrainMineSlots(const MCTerrain& terrain);

/// <summary>
/// Port-only (PLAN.md's first-use item 10): reads the tiles the map's mines can draw (MCTerrainMineSlots) while the
/// mission loads, the original's and, while the HD ground is drawn, their HD overlays (by name when the program knows
/// the tile set), so a minefield's first frame on screen reads none. Call it once the map's mines are set (a match
/// without mines clears them).
/// </summary>
void MCTerrainReadMinesAhead();

/// <summary>Port-only (PLAN.md's first-use item 10): the original ground tiles MCTerrainReadHdTiles rebuilt and read.</summary>
struct MCGroundRebuilt
{
    size_t Rebuilt = 0;
    size_t Read = 0;
};

/// <summary>
/// Port-only: has the HD art pack's copies of the map's ground tiles (MCHdArt::FindTerrainTile) in hand while the
/// mission loads, so the first frame, which uploads the ground, doesn't wait on the pack: those read ahead
/// (MCTerrainReadHdTilesAhead) waited for and kept, the rest read. For a tile set the program's table holds (PLAN.md's
/// first-use item 10), each original tile the terrain's load left out (MCTerrainLeaveRebuiltTiles) is rebuilt from its
/// HD frame (MCRebuildGroundTile) rather than read; one that isn't (no frame, or not the tile's bytes) is read. Does
/// nothing while HD art is off or the software renderer draws. How many original tiles it rebuilt and read.
/// </summary>
MCGroundRebuilt MCTerrainReadHdTiles();

/// <summary>Port: lets go of the ground mesh (the terrain is destroyed).</summary>
void MCTerrainForgetMesh();

/// <summary>Port: the ground mesh built last (its tiles, for tests and the HD art tools), or none.</summary>
MCOptionalRef<const MCTerrainMesh> MCTerrainMeshInUse();

/// <summary>The fog of war's edge's levels: a tile with one, two or three of its corners seen (the haze tables).</summary>
inline constexpr size_t MCTerrainFogEdgeLevels = 3;

/// <summary>
/// Port-only (HD weather and lighting): how the fog of war's edge darkens what the weather lays on the ground (the snow's
/// white, the wet ground's sky) and the living water's foam and glints on a tile with one, two and three of its corners
/// seen, as the haze tables darken the ground: each table's change to the palette's pale greys (red, green, blue; 1
/// where it doesn't darken), as the last ground frame drew the edge.
/// </summary>
std::array<std::array<float, 3>, MCTerrainFogEdgeLevels> MCTerrainFogEdgeShades();

/// <summary>
/// Port-only, for the HD art tools: the steps the ground mesh built last places its vertices by (across and down a
/// vertex, and up an elevation level: vertex (row, col) at elevation e lies (col - row) * across, (row + col) * down -
/// e * up from vertex (0, 0)), or none without a mesh.
/// </summary>
struct MCTerrainMeshSteps
{
    int32_t Across = 0;
    int32_t Down = 0;
    int32_t Up = 0;
};

std::optional<MCTerrainMeshSteps> MCTerrainMeshStepsInUse();

/// <summary>
/// Port: the steps <paramref name="camera"/>'s terrain grid is projected by (MCTerrainWindow::Render), in whole pixels:
/// those its ground mesh is built with.
/// </summary>
MCTerrainMeshSteps MCTerrainScreenSteps(const MCCamera& camera);

/// <summary>
/// Port-only: builds the map's ground mesh as the mission's first frame will (for the first active camera's terrain
/// window) and has the hardware renderer put it on the GPU now (MCRenderer::PrepareTerrain), while the mission loads,
/// rather than in that frame; an error says why it couldn't. Does nothing while the software renderer draws, or before a
/// camera has a terrain window.
/// </summary>
std::expected<void, std::string> MCTerrainUploadGround();

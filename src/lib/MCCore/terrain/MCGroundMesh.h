#pragma once

#include "lib/MCOptionalRef.h"

class MCCamera;
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
/// Port-only: reads the HD art pack's copies of the map's ground tiles (MCHdArt::FindTerrainTile) while the mission
/// loads, so the first frame, which uploads the ground, doesn't wait on the pack. Does nothing while HD art is off or
/// the software renderer draws.
/// </summary>
void MCTerrainReadHdTiles();

/// <summary>Port: lets go of the ground mesh (the terrain is destroyed).</summary>
void MCTerrainForgetMesh();

/// <summary>Port: the ground mesh built last (its tiles, for tests and the HD art tools), or none.</summary>
MCOptionalRef<const MCTerrainMesh> MCTerrainMeshInUse();

/// <summary>The fog of war's edge's levels: a tile with one, two or three of its corners seen (the haze tables).</summary>
inline constexpr size_t MCTerrainFogEdgeLevels = 3;

/// <summary>
/// Port-only (HD weather): how the fog of war's edge darkens what the weather lays on the ground (the snow's white, the
/// wet ground's sky) on a tile with one, two and three of its corners seen, as the haze tables darken the ground: each
/// table's change to the palette's pale greys (red, green, blue; 1 where it doesn't darken), as the last ground frame
/// drew the edge.
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

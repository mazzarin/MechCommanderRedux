#pragma once

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

/// <summary>Port: lets go of the ground mesh (the terrain is destroyed).</summary>
void MCTerrainForgetMesh();

/// <summary>Port: the ground mesh built last (its tiles, for tests and the HD art tools), or null.</summary>
const MCTerrainMesh* MCTerrainMeshInUse();

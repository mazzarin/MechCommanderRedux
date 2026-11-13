#pragma once

#include "terrain/MCVertex.h"

class MCCamera;
class MCMapBlockManager;
class MCVector3D;

/// <summary>Port-only: a rectangle of a terrain grid's quads, by the grid's rows and columns (inclusive).</summary>
struct MCGridQuads
{
    int32_t FirstRow = 0;
    int32_t FirstCol = 0;
    int32_t LastRow = 0;
    int32_t LastCol = 0;
};

/// <summary>
/// The terrain as one camera sees it: that camera's visible vertex grid and its quads, and the world position of the
/// grid's top-left corner. <see cref="MCTerrain"/> keeps <c>NumberOfWindows</c> of them; a camera takes one with
/// <see cref="MCTerrain::NewWindow"/>.
/// </summary>
class MCTerrainWindow
{
public:
    /// <summary>
    /// Window <paramref name="windowNum"/>, free, with a grid of <paramref name="side"/> x <paramref name="side"/>
    /// vertices and the quads between them.
    /// </summary>
    MCTerrainWindow(int32_t windowNum, int32_t side);

    MCTerrainWindow(const MCTerrainWindow&) = delete;
    MCTerrainWindow& operator=(const MCTerrainWindow&) = delete;

    /// <summary>Binds the window to <paramref name="cam"/> and builds its grid around the camera.</summary>
    void Bind(std::weak_ptr<MCCamera> cam);

    /// <summary>Releases the camera (the window becomes free).</summary>
    void Release();

    /// <summary>Whether the window is free (no camera, or its camera is gone).</summary>
    bool IsFree() const { return Camera.expired(); }

    /// <summary>Whether the window has a camera and that camera is active.</summary>
    bool CameraIsActive() const;

    /// <summary>
    /// Resets the visibility bits for the new frame, follows the camera and rebuilds the vertex grid.
    /// </summary>
    void Update();

    /// <summary>
    /// Projects the vertex grid, marks what needs redrawing and draws the terrain and overlay tiles of every quad with a
    /// corner on the pane (the others draw nothing).
    /// </summary>
    void Render(int32_t hazeFactor);

    /// <summary>Draws the outline of every quad (the debug terrain grid).</summary>
    void DrawLines();

    /// <summary>The vertices along a side of the grid.</summary>
    int32_t Side() const { return _Side; }

    /// <summary>
    /// The quads with vertex <paramref name="vertex"/> as a corner, as indices into <see cref="Blocks"/> in their order
    /// there: up to four (up and left of it, up, left, and the one it is the top-left corner of), the rest empty.
    /// </summary>
    std::array<std::optional<size_t>, 4> QuadsAround(uint32_t vertex) const;

    /// <summary>
    /// Port-only: the quads with a corner on the pane (not <see cref="MCVertex::Clipped"/>) at the last
    /// <see cref="Render"/>, as the smallest rectangle holding them; none when every vertex was clipped. A quad outside
    /// it has every corner clipped, so it draws nothing.
    /// </summary>
    std::optional<MCGridQuads> QuadsOnPane() const { return _QuadsOnPane; }

    /// <summary>The camera using the window (none: the window is free). The camera holds the window by
    /// <c>weak_ptr</c> too: each refers to the other, the camera list and the terrain own them.</summary>
    std::weak_ptr<MCCamera> Camera;
    /// <summary>The block cache that built the grid (<see cref="MCMapBlockManager::BuildWindow"/>), whose vertices
    /// <see cref="MCVertex::MapVertex"/> indexes.</summary>
    std::weak_ptr<const MCMapBlockManager> Cache;
    /// <summary>The visible vertex grid, row by row (filled by <see cref="MCMapBlockManager::BuildWindow"/>).</summary>
    std::vector<MCVertex> Vertices;
    /// <summary>The grid's quads, row by row: (side - 1)^2 of them.</summary>
    std::vector<MCTerrainBlock> Blocks;
    /// <summary>The window's index in the terrain's window table.</summary>
    int32_t WindowNum = 0;
    /// <summary>World x of the grid's top-left vertex (set by <see cref="MCMapBlockManager::Update"/>).</summary>
    float TopLeftX = 0.0f;
    /// <summary>World y of the grid's top-left vertex.</summary>
    float TopLeftY = 0.0f;
    /// <summary>The map block the grid's corner is measured from (set by <see cref="MCMapBlockManager::Update"/>).</summary>
    int32_t CurrentBlock = 0;
    /// <summary>The corner's vertex column and row within <see cref="CurrentBlock"/> (may lie outside it).</summary>
    float VertexOffsetX = 0.0f;
    float VertexOffsetY = 0.0f;

private:
    /// <summary>The vertices along a side of the grid.</summary>
    int32_t _Side = 0;
    /// <summary>What <see cref="QuadsOnPane"/> gives.</summary>
    std::optional<MCGridQuads> _QuadsOnPane;
};

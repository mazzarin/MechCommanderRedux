#pragma once

class MCMapBlockManager;

/// <summary>
/// One vertex of a map block as stored in the terrain's <c>.elv</c> packet file: an elevation level and the terrain
/// tile (texture) and overlay drawn on the block whose top-left corner it is. Each map block is one packet of
/// <c>verticesBlockSide * verticesBlockSide</c> of these, row by row.
/// </summary>
/// <remarks>On-disk and in-memory layout, 8 bytes.</remarks>
#pragma pack(push, 1)
struct MCPrecompVertex
{
    /// <summary>Elevation level: the height is <c>Elevation * MCTerrain::MetersPerElevLevel</c>.</summary>
    uint8_t Elevation;
    /// <summary>
    /// The map editor's group of the vertex's terrain tile: in the retail maps it follows <see cref="TextureData"/>
    /// almost one to one (0 for tiles 0-36, 1 for 79-194, 42 for 3204-3207...). The game never reads it.
    /// </summary>
    uint8_t TileGroup; // Fixed layout: .elv vertex
    /// <summary>
    /// What the map editor's memory held there: 0, 0x0101 or fragments of text ("xt", "ck") in the retail maps.
    /// Never read or written by the game.
    /// </summary>
    int16_t EditorLeftover; // Fixed layout: .elv vertex
    /// <summary>Terrain tile index into the tile cache (negative: none).</summary>
    int16_t TextureData;
    /// <summary>Overlay tile index (roads, craters... drawn over the terrain tile; negative: none).</summary>
    int16_t OverlayData;
};
#pragma pack(pop)
static_assert(sizeof(MCPrecompVertex) == 8);

/// <summary>
/// A vertex of a terrain window's visible grid: which map vertex it is, where it projects on screen this frame and
/// whether its blocks need redrawing.
/// </summary>
/// <remarks>
/// Built by <see cref="MCMapBlockManager::BuildWindow"/>; the screen position and flags are refreshed by
/// <see cref="MCTerrainWindow::Render"/>.
/// </remarks>
class MCVertex
{
public:
    /// <summary>The map vertex: its index in the block cache (<see cref="MCMapBlockManager::Vertex"/>).</summary>
    uint32_t MapVertex = 0;
    /// <summary>Projected screen x.</summary>
    int32_t Px = 0;
    /// <summary>Projected screen y.</summary>
    int32_t Py = 0;
    /// <summary>The map block the vertex belongs to (<c>row * blocksMapSide + column</c>).</summary>
    int16_t BlockNum = 0;
    /// <summary>The vertex within its block (<c>row * verticesBlockSide + column</c>).</summary>
    int16_t VertexNum = 0;
    /// <summary>The vertex's map position as vertex row (high 16 bits) and column (low 16 bits).</summary>
    uint32_t PosTile = 0;
    /// <summary>The vertex projects outside the visible pane.</summary>
    bool Clipped = false;
    /// <summary>The blocks around the vertex must be redrawn (newly exposed screen area).</summary>
    bool Redraw = false;
    /// <summary>The vertex lies near the pane edge the view is scrolling towards.</summary>
    bool EdgeRedraw = false;
};

/// <summary>One quad of a terrain window: its four corners in the window's grid and the tiles last drawn on it.</summary>
class MCTerrainBlock
{
public:
    /// <summary>A quad with the corners top-left, top-right, bottom-right and bottom-left (indexes into the grid).</summary>
    MCTerrainBlock(uint32_t v0, uint32_t v1, uint32_t v2, uint32_t v3) : Vertices{v0, v1, v2, v3} {}

    /// <summary>
    /// Draws the block's terrain tile through <c>VfxNTileDraw</c>, hazed by <paramref name="hazeFactor"/> and the fog
    /// of war (the home team's visible bits). <paramref name="grid"/> is the window's grid, <paramref name="cache"/>
    /// the block cache its vertices index.
    /// </summary>
    void Draw(std::span<MCVertex> grid, const MCMapBlockManager& cache, int32_t hazeFactor);

    /// <summary>Draws the block's overlay tile (roads, craters...) and its mine tiles.</summary>
    void DrawOverlay(std::span<MCVertex> grid, const MCMapBlockManager& cache, int32_t hazeFactor);

    /// <summary>Draws the block's outline in <paramref name="color"/> (the debug terrain grid).</summary>
    void DrawLine(std::span<const MCVertex> grid, int32_t color) const;

    /// <summary>The corners in the window's grid: top-left, top-right, bottom-right, bottom-left.</summary>
    std::array<uint32_t, 4> Vertices{};
    /// <summary>How many of the four corners were visible last frame (a change forces a redraw).</summary>
    uint8_t LastVisibleCount = 0;
};

/// <summary>
/// The haze palette a quad with <paramref name="visibleCount"/> visible corners is drawn through: the main camera's
/// haze step per visible corner added to <paramref name="hazeFactor"/>.
/// </summary>
std::optional<MCColorTable> MCTerrainHazePalette(int32_t hazeFactor, uint32_t visibleCount);

/// <summary>What the terrain draws of a map tile's mines.</summary>
struct MCMineView
{
    /// <summary>The Inner Sphere's laid mine.</summary>
    bool InnerSphereMine = false;
    /// <summary>The Clans' laid mine.</summary>
    bool ClanMine = false;
    /// <summary>The crater of a mine that went off.</summary>
    bool Crater = false;
};

/// <summary>
/// The mines a map tile shows, from its overlay word (the Inner Sphere's mine state in bits 11..12, the Clans' in
/// 13..14: 2 a laid mine, 3 one that went off): each side sees its own laid mines, the Clans' show to the Clans or in
/// god mode, and a mine that went off leaves a crater.
/// </summary>
MCMineView MCTerrainMineView(uint32_t overlay, bool playingInnerSphere, bool godMode);

/// <summary>
/// The mine tile for a quad with corner levels <paramref name="e0"/> (top-left) to <paramref name="e3"/> (bottom-left):
/// the corners' levels above the lowest as base-3 digits, from the first mine tile 0xed1.
/// </summary>
int32_t MCMineTileNumber(uint32_t e0, uint32_t e1, uint32_t e2, uint32_t e3);

/// <summary>The tile of the crater a mine leaves when it goes off.</summary>
inline constexpr int32_t MCMineCraterTile = 0xf21;

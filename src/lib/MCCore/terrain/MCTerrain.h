#pragma once

#include "gui/MCGuiRef.h"
#include "lib/MCVector2D.h"
#include "lib/MCVector3D.h"
#include "terrain/MCTerrainWindow.h"

class MCByteFlag;
class MCCamera;
class MCMapBlockManager;
class MCObjectBlockManager;
class MCTacticalMap;
class MCTerrainTiles;

/// <summary>
/// The map: the terrain's configuration (<c>&lt;name&gt;.fit</c>), its block and tile caches, the fog-of-war bits, the
/// camera windows that draw it, the object blocks and the tactical map. The scenario installs one in the game context
/// (<see cref="Terrain"/>), then loads it.
/// </summary>
/// <remarks>
/// The <c>.fit</c> file gives VerticesBlockSide, BlocksMapSide, MetersPerElevLevel, MetersPerVertex,
/// VisibleVerticesPerSide, NumberOfWindows, TerrainHeapSize, TerrainTileHeapSize and TerrainTileFile. The map's
/// geometry (the statics below) is shared by the whole game and outlives the terrain, as in the original.
/// </remarks>
class MCTerrain
{
public:
    /// <summary>A terrain with nothing loaded (<see cref="Load"/> reads it).</summary>
    MCTerrain();

    /// <summary>Frees what <see cref="Load"/> built (<see cref="Unload"/>).</summary>
    ~MCTerrain();

    /// <summary>Frees what <see cref="Load"/> built, in the original's order.</summary>
    void Unload();

    MCTerrain(const MCTerrain&) = delete;
    MCTerrain& operator=(const MCTerrain&) = delete;

    /// <summary>
    /// Reads <c>&lt;terrainPath&gt;&lt;fileName&gt;.fit</c> and builds everything: the geometry, the screen position
    /// tables, the tile cache, the fog-of-war bits, the windows, the block cache, the object blocks and the tactical
    /// map. The terrain must be the context's (<see cref="Terrain"/>): what it builds reaches for it.
    /// </summary>
    std::expected<void, std::string> Load(std::string_view fileName);

    /// <summary>Updates the first window (the main camera's).</summary>
    void Update();

    /// <summary>Window <paramref name="windowNum"/>, or null past the windows.</summary>
    std::shared_ptr<MCTerrainWindow> GetTerrainWindow(int32_t windowNum);

    /// <summary>Gives <paramref name="cam"/> the first free window.</summary>
    /// <returns>The window, or null when none is free.</returns>
    std::shared_ptr<MCTerrainWindow> NewWindow(std::weak_ptr<MCCamera> cam);

    void SetOverlayTile(int32_t blockNum, int32_t vertexNum, int32_t value) const;
    int32_t GetOverlayTile(int32_t blockNum, int32_t vertexNum) const;
    int32_t GetTile(int32_t blockNum, int32_t vertexNum) const;

    /// <summary>
    /// Projects a world position to the isometric screen, using the 30-degree view angle; whole pixels, from the map's
    /// top-left corner.
    /// </summary>
    static MCVector2D ProjectTerrain(const MCVector3D& pos);

    /// <summary>Renders the windows of <paramref name="cam"/>.</summary>
    void Render(int32_t hazeFactor, const MCCamera& cam);

    /// <summary>Draws the debug terrain grid of every active window.</summary>
    void DrawLines();

    /// <summary>The terrain height at <paramref name="pos"/>.</summary>
    static float GetTerrainElevation(const MCVector3D& pos);

    static MCVector3D GetTerrainNormal(const MCVector3D& pos);

    /// <summary>Updates the objects of every object block.</summary>
    void UpdateAllObjects() const;

    /// <summary>
    /// Marks the circle a looker at <paramref name="looker"/> sees as visible for <paramref name="who"/> (1: the
    /// Inner Sphere bits, else the Clan bits). Only full-circle looks (<paramref name="angle"/> 360) are handled;
    /// the radius comes from <c>VisualRangeTable</c> for the highest cell elevation around the looker.
    /// </summary>
    void MarkSeen(const MCVector3D& looker, const MCVector3D& lookVector, float angle, float range, uint8_t who) const;

    /// <summary>
    /// Marks a circle of <paramref name="range"/> around <paramref name="looker"/> as visible (only for a
    /// 360-degree <paramref name="angle"/>).
    /// </summary>
    void MarkRadiusSeen(const MCVector3D& looker, const MCVector3D& lookVector, float angle, float range,
                        uint8_t who) const;

    /// <summary>The home team's visible-this-frame bits (the Clans' when the home team's alignment is -1).</summary>
    MCByteFlag& HomeVisibleBits() const;

    /// <summary>Notes that map block <paramref name="blockNum"/> is drawn this frame.</summary>
    void MarkBlockUsed(int32_t blockNum);

    /// <summary>Forgets the blocks drawn (a new frame).</summary>
    void ClearUsedBlocks();

    /// <summary>
    /// Whether map block <paramref name="blockNum"/> is drawn this frame. Block -1 (no block) always is: the
    /// original's list ended at its first -1, and a search for -1 found it.
    /// </summary>
    bool BlockUsed(int32_t blockNum) const;

    /// <summary>
    /// Sets the map's geometry from the sizes in its <c>.fit</c> file and everything that follows from them: the
    /// block and map sizes in meters, the reciprocals, the map's top-left corner (at elevation 0 until
    /// <see cref="SetTopLeftElevation"/>).
    /// </summary>
    static void SetGeometry(int32_t verticesBlockSide, int32_t blocksMapSide, float metersPerVertex,
                            float metersPerElevLevel);

    /// <summary>Sets the height of the map's top-left corner and the screen projection of the corner.</summary>
    static void SetTopLeftElevation(float elevation);

    /// <summary>The terrain's name (the .fit file's base name).</summary>
    std::string Name;
    /// <summary>The camera windows (NumberOfWindows of them).</summary>
    std::vector<std::shared_ptr<MCTerrainWindow>> Windows;
    /// <summary>The tile cache.</summary>
    std::unique_ptr<MCTerrainTiles> Tiles;
    /// <summary>The block cache (shared: the windows refer to the cache that built their grids).</summary>
    std::shared_ptr<MCMapBlockManager> MapBlocks;
    /// <summary>The object blocks of the map.</summary>
    std::unique_ptr<MCObjectBlockManager> ObjectBlocks;
    /// <summary>What the Inner Sphere (the player, unless playing Clan) sees this frame, per map vertex.</summary>
    std::unique_ptr<MCByteFlag> ISVisibleBits;
    /// <summary>What the Clans see this frame.</summary>
    std::unique_ptr<MCByteFlag> ClanVisibleBits;
    /// <summary>The tactical map (the MFD) of the in-mission interface: a child of the screen window, closed with the
    /// terrain.</summary>
    MCGuiHandle<MCTacticalMap> TacticalMap;
    /// <summary>Screen x of every map vertex (by BlockOffsets[block] + vertex), 0x11111111 until it is projected.</summary>
    std::vector<int32_t> ScreenPosX;
    /// <summary>Screen y of every map vertex.</summary>
    std::vector<int32_t> ScreenPosY;
    /// <summary>Index of each block's first vertex in ScreenPosX/ScreenPosY.</summary>
    std::vector<int32_t> BlockOffsets;

    /// <summary>Vertices along a block's side.</summary>
    static int32_t VerticesBlockSide;
    /// <summary>Blocks along the map's side.</summary>
    static int32_t BlocksMapSide;
    /// <summary>blocksMapSide^2.</summary>
    static int32_t TotalBlocks;
    /// <summary>Vertices along a window's side.</summary>
    static int32_t VisibleVerticesPerSide;
    /// <summary>Meters per elevation level.</summary>
    static float MetersPerElevLevel;
    /// <summary>Meters between two vertices.</summary>
    static float MetersPerVertex;
    static float OneOvermetersPerVertex;
    static float OneOververticesBlockSide;
    /// <summary>Half the vertices along the map's side.</summary>
    static int32_t VerticesMapSide;
    /// <summary>metersPerVertex / 3 (map cells per vertex).</summary>
    static float MetersPerVertexDivMapcellDim;
    /// <summary>Meters along a block's side.</summary>
    static float MetersBlockSide;
    /// <summary>Set when the whole view must be redrawn.</summary>
    static bool ForceRedraw;
    /// <summary>sin of the isometric view angle (30 degrees).</summary>
    static float ProjectionSin;
    /// <summary>cos of the isometric view angle.</summary>
    static float ProjectionCos;
    /// <summary>The map's top-left corner at full zoom, in screen space.</summary>
    static MCVector2D MapTopLeft2d100;
    /// <summary>The map's top-left corner (x = -side/2, y = +side/2, z = the first vertex's elevation).</summary>
    static MCVector3D MapTopLeft3d100;

private:
    /// <summary>The map blocks drawn this frame (no limit: the original's list held 324).</summary>
    std::vector<int32_t> _UsedBlocks;
};

/// <summary>The scenario's terrain (null outside a mission); the caller keeps it as its pin.</summary>
std::shared_ptr<MCTerrain> Terrain();

/// <summary>The terrain's tactical map (null without a terrain or before it is made); the caller keeps it as its pin.</summary>
std::shared_ptr<MCTacticalMap> TacticalMap();

/// <summary>Reveals the whole map (the cheat): a 10000 m circle around the origin.</summary>
void RevealAll();

/// <summary>The isometric view angle (30 degrees, as MCX.EXE stores it).</summary>
inline constexpr double MCTerrainViewAngle = 0x1.0c152382d45b2p-1;

/// <summary>
/// Port: how large a world view the visible terrain grid covers, as width / cos + height / sin of the view angle in
/// pixels at camera scale 100 (0 before a terrain loads). A view's furthest zoom stays within it.
/// </summary>
extern double MCTerrainGridReach;

/// <summary>verticesBlockSide^2.</summary>
extern int32_t VerticesPerBlock;
/// <summary>Meters along the map's side.</summary>
extern float WorldUnitsMapSide;

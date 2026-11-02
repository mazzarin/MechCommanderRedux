#pragma once

#include "ai/MCMoveGeometry.h"

class MCFile;
class MCGameObject;
class MCMover;
class MCObjectBlockManager;
class MCObjectList;

/// <summary>
/// One terrain tile of the <see cref="MCScenarioMap"/>: two bit-packed words, stored raw in the map file.
/// </summary>
/// <remarks>
/// Original header: <c>ai\move.h</c>. 8 bytes (read and written raw, so the layout is fixed).
/// <para><c>Cells</c>: bits 0-6 terrain tile type, bits 7-12 elevation level (added to
/// <see cref="MCScenarioMap::BaseElevation"/>), bit 13 "preserved" (see <see cref="MCScenarioMap::SpreadState"/>),
/// then two bits per cell c = cellR * 3 + cellC: bit 14 + 2c passable, bit 15 + 2c line of sight.</para>
/// <para><c>Overlay</c>: bits 0-6 overlay type (index of <see cref="OverlayIsBridge"/> and friends), bits 7-8 set
/// from the terrain object blocks, bits 11-12 Inner Sphere mine layout, bits 13-14 Clan mine layout (rows of
/// <see cref="MineLayout"/>), bit 15 + c path locked, bits 25-26 and 27-28 the mines each side knows of.</para>
/// </remarks>
struct MCMapTile
{
    uint32_t Cells = 0;
    uint32_t Overlay = 0;

    /// <summary>Whether cell (cellR, cellC) can be entered (0 or 1).</summary>
    uint32_t GetCellPassable(int32_t cellR, int32_t cellC) const
    {
        const uint32_t shift = static_cast<uint32_t>((cellR * MapCellDim + cellC) * 2);
        return (Cells & (0x4000u << shift)) >> (shift + 14);
    }

    /// <summary>How many of the tile's cells can't be entered (0 to MapCellDim * MapCellDim; port helper).</summary>
    uint32_t BlockedCells() const
    {
        uint32_t blocked = 0;

        for (int32_t cellR = 0; cellR < MapCellDim; ++cellR)
        {
            for (int32_t cellC = 0; cellC < MapCellDim; ++cellC)
            {
                blocked += GetCellPassable(cellR, cellC) == 0 ? 1u : 0u;
            }
        }

        return blocked;
    }

    /// <summary>Whether cell (cellR, cellC) lets line of sight through (0 or 1).</summary>
    uint32_t GetCellLos(int32_t cellR, int32_t cellC) const
    {
        const uint32_t shift = static_cast<uint32_t>((cellR * MapCellDim + cellC) * 2);
        return (Cells & (0x8000u << shift)) >> (shift + 15);
    }

    /// <summary>Whether a mover's path has locked cell (cellR, cellC) (0 or 1).</summary>
    uint32_t GetCellPathLocked(int32_t cellR, int32_t cellC) const
    {
        const uint32_t shift = static_cast<uint32_t>(cellR * MapCellDim + cellC);
        return (Overlay & (0x8000u << shift)) >> (shift + 15);
    }

    /// <summary>Sets or clears the path lock of cell (cellR, cellC).</summary>
    void SetCellPathLocked(int32_t cellR, int32_t cellC, uint32_t locked)
    {
        const uint32_t shift = static_cast<uint32_t>(cellR * MapCellDim + cellC);
        Overlay = (locked << (shift + 15)) | (~(0x8000u << shift) & Overlay);
    }

    /// <summary>The tile's overlay type.</summary>
    uint32_t OverlayType() const { return Overlay & 0x7f; }

    /// <summary>The tile's terrain type.</summary>
    uint32_t TerrainType() const { return Cells & 0x7f; }
};

/// <summary>The terrain types from this one on are water.</summary>
inline constexpr uint32_t FirstWaterTerrain = 0x2b;

static_assert(sizeof(MCMapTile) == 8, "a map tile is read raw from the map file");

/// <summary>A tile whose word <see cref="MCScenarioMap::SpreadState"/> saved, put back by RestorePreservedMap.</summary>
/// <remarks>The name is the port's (the original's isn't known).</remarks>
struct MCPreservedTile
{
    int32_t Row = 0;
    int32_t Col = 0;
    uint32_t Cells = 0;
};

/// <summary>
/// The movement map of the whole scenario: one <see cref="MCMapTile"/> per terrain tile, plus a per-tile count of the
/// paths crossing it.
/// </summary>
/// <remarks>Original source: <c>ai\move.cpp</c> (inline methods in <c>ai\move.h</c>).</remarks>
class MCScenarioMap
{
public:
    /// <summary>An empty map of <paramref name="height"/> x <paramref name="width"/> tiles (nothing passable).</summary>
    MCScenarioMap(int32_t width, int32_t height);
    /// <summary>Reads a map file: height, width, base elevation, then the tiles.</summary>
    /// <returns>The map, or why the file isn't one: a size the file doesn't hold.</returns>
    /// <remarks>Issue #25: the original allocated and read whatever size the file named.</remarks>
    static std::expected<std::unique_ptr<MCScenarioMap>, std::string> Read(MCFile& mapFile);

    /// <summary>Writes the map in the format the file constructor reads.</summary>
    void Write(MCFile& mapFile) const;

    /// <summary>The tile and cell of a world position (floored).</summary>
    static void WorldToMapPos(MCVector3D pos, int32_t& tileR, int32_t& tileC, int32_t& cellR, int32_t& cellC);
    /// <summary>The tile of a world position (floored).</summary>
    static void WorldToMapTilePos(MCVector3D pos, int32_t& tileR, int32_t& tileC);

    /// <summary>
    /// Whether tile (tileR, tileC) is on the map (port helper). The original indexes <see cref="Map"/> with the tile of
    /// any world point; the port checks the ones that can fall off the map (a scattered shot, a point walked out from
    /// a unit, a player's waypoint).
    /// </summary>
    bool OnMap(int32_t tileR, int32_t tileC) const
    {
        return tileR >= 0 && tileR < Height && tileC >= 0 && tileC < Width;
    }

    /// <summary>The tile at (tileR, tileC) (on the map).</summary>
    MCMapTile& TileAt(int32_t tileR, int32_t tileC) { return Map[static_cast<size_t>(Width * tileR + tileC)]; }
    const MCMapTile& TileAt(int32_t tileR, int32_t tileC) const
    {
        return Map[static_cast<size_t>(Width * tileR + tileC)];
    }

    /// <summary>Whether a mover's path has locked cell (cellR, cellC) of tile (tileR, tileC).</summary>
    bool GetCellPathLocked(int32_t tileR, int32_t tileC, int32_t cellR, int32_t cellC) const
    {
        return TileAt(tileR, tileC).GetCellPathLocked(cellR, cellC) != 0;
    }

    /// <summary>Sets or clears the path lock of cell (cellR, cellC) of tile (tileR, tileC).</summary>
    void SetCellPathLocked(int32_t tileR, int32_t tileC, int32_t cellR, int32_t cellC, uint32_t locked)
    {
        TileAt(tileR, tileC).SetCellPathLocked(cellR, cellC, locked);
    }

    /// <summary>Whether the cell under a world position is passable (off the map isn't).</summary>
    bool CellPassable(MCVector3D pos) const;

    /// <summary>
    /// Marks cell (cellRow, cellCol) impassable and spreads outward <paramref name="depth"/> cells, saving each tile
    /// first while <see cref="PreserveMapTiles"/> is set.
    /// </summary>
    void SpreadState(int32_t cellRow, int32_t cellCol, int32_t depth);
    /// <summary>Blocks the cells within <paramref name="radius"/> of a position.</summary>
    void PlaceObject(MCVector3D position, float radius);
    /// <summary>Places every existing object of a list (see <see cref="PlaceObject"/>).</summary>
    void PlaceObjects(MCObjectList& objectList);
    /// <summary>Places both mech lists with tile preservation on.</summary>
    void UpdateMovingObjects();
    /// <summary>Puts back the tiles saved by <see cref="SpreadState"/>.</summary>
    void RestorePreservedMap();

    /// <summary>The ground height under a world position, interpolated over the tile's triangle.</summary>
    float GetTerrainElevation(MCVector3D position) const;
    /// <summary>As <see cref="GetTerrainElevation"/>, before the result is rounded to a float.</summary>
    double GetTerrainElevationUnrounded(MCVector3D position) const;
    /// <summary>Whether the cell under a position lets line of sight through (off the map doesn't).</summary>
    bool GetLos(MCVector3D position) const;
    /// <summary>The Inner Sphere mine of cell (cellR, cellC) of a tile.</summary>
    uint32_t GetInnerSphereMine(int32_t tileR, int32_t tileC, int32_t cellR, int32_t cellC) const;
    /// <summary>The Clan mine of cell (cellR, cellC) of a tile.</summary>
    uint32_t GetClanMine(int32_t tileR, int32_t tileC, int32_t cellR, int32_t cellC) const;
    /// <summary>Whether line of sight runs from <paramref name="start"/> to <paramref name="target"/>.</summary>
    bool LineOfSight(MCVector3D start, MCVector3D target);
    /// <summary>Whether line of fire runs from <paramref name="start"/> to <paramref name="target"/>.</summary>
    bool LineOfFire(MCVector3D start, MCVector3D target) const;
    /// <summary>Counts what blocks the sensor line between two positions.</summary>
    void LineOfSensor(MCVector3D start, MCVector3D target, int32_t& numBlockingTiles, int32_t& numBlockingObjects);

    /// <summary>Dumps a rectangle of the map to a text file.</summary>
    void Print(std::string_view fileName, int32_t uLr, int32_t uLc, int32_t height, int32_t width) const;

    /// <summary>
    /// The movement cost of cell (cellR, cellC) of a tile for <paramref name="mover"/>: 0 without an overlay, else
    /// the <see cref="OverlayWeightTable"/> entry for its move level (gates by team; 20000 when closed).
    /// </summary>
    int32_t GetOverlayWeight(int32_t tileR, int32_t tileC, int32_t cellR, int32_t cellC, MCMover& mover) const;

    /// <summary>The tiles, row by row.</summary>
    std::vector<MCMapTile> Map;
    int32_t Height = 0;
    int32_t Width = 0;
    /// <summary>Elevation level added to every tile's 6-bit elevation.</summary>
    int32_t BaseElevation = 0;
    /// <summary>The tiles SpreadState saved while <see cref="PreserveMapTiles"/> was set (no limit: the original held
    /// 300).</summary>
    std::vector<MCPreservedTile> PreservedTiles;
    /// <summary>While set, SpreadState saves a tile before it first changes it.</summary>
    bool PreserveMapTiles = false;
    /// <summary>Per tile, how many marked paths cross it (<see cref="MCMovePath::Mark"/>).</summary>
    std::vector<uint8_t> PathMap;
};

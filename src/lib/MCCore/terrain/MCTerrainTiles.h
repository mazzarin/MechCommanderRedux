#pragma once

#include "lib/MCOptionalRef.h"
#include "platform/MCRegisteredBlock.h"

class MCPacketFile;

/// <summary>
/// A cache slot for one terrain tile: the tile's bitmap (a VFX tile shape, one packet of the tile file) once it is
/// loaded, or a note that its packet is missing.
/// </summary>
struct MCTerrainTile
{
    /// <summary>The tile's VFX shape data (null while not loaded, or when <see cref="Missing"/>).</summary>
    std::span<uint8_t> TileData() const { return Data.Bytes(); }

    /// <summary>The loaded tile's data, registered with the renderers.</summary>
    MCRegisteredBlock Data;
    /// <summary>The tile's packet is missing or empty: it is never read again.</summary>
    bool Missing = false;
};

/// <summary>Port-only: what <see cref="MCTerrainTiles::ReadTiles"/> read.</summary>
struct MCTileReads
{
    /// <summary>The tiles read into the cache, those found missing included.</summary>
    size_t Tiles = 0;
    /// <summary>The reads of the tile files that took (a tile read alone counted as one).</summary>
    size_t Reads = 0;
};

/// <summary>
/// The terrain tile cache: the tile bitmaps of the terrain's tile set, read on demand from two packet files
/// (<c>&lt;tilePath&gt;&lt;name&gt;.pak</c> and the rotated set <c>&lt;tile90Path&gt;&lt;name&gt;90.pak</c>). The
/// original's cache was a private heap, flushed least-recently-used when it filled; the port's never fills.
/// </summary>
/// <remarks>
/// The slot table has one entry per packet of the first file; <see cref="TileSetOffset"/>[1] (half the tiles)
/// selects the second half of the table for the camera's rotated view. Tiles below that half come from the rotated
/// file, the rest from the normal one. A <c>.pre</c> file in the terrain folder lists (as int32 indices) the tiles to
/// preload.
/// </remarks>
class MCTerrainTiles
{
public:
    /// <summary>Opens both tile files of the tile set <paramref name="tileFileName"/>.</summary>
    static std::expected<std::unique_ptr<MCTerrainTiles>, std::string> Create(std::string_view tileFileName);

    /// <summary>A cache over <paramref name="tileFile"/> and <paramref name="tile90File"/> (which may be closed).</summary>
    MCTerrainTiles(std::unique_ptr<MCPacketFile> tileFile, std::unique_ptr<MCPacketFile> tile90File,
                   bool customTileSet);

    ~MCTerrainTiles();
    MCTerrainTiles(const MCTerrainTiles&) = delete;
    MCTerrainTiles& operator=(const MCTerrainTiles&) = delete;

    /// <summary>
    /// The slots of the tiles listed in <c>&lt;terrainPath&gt;&lt;terrainName&gt;.pre</c> (as int32 indices of the normal
    /// set), to preload; none without one. Port: the load reads them with the map's (<see cref="ReadTiles"/>), first.
    /// </summary>
    std::vector<int32_t> PreloadSlots(std::string_view terrainName) const;

    /// <summary>
    /// Port-only: reads each of the slots <paramref name="slots"/> of the table that isn't cached or missing into the
    /// cache (once, however often it's listed), as <see cref="ReadTile"/> would one at a time in the order listed, but
    /// together: each file's in a few large reads on threads of their own (<see cref="MCPacketFile::ReadPackets"/>),
    /// placed in the cache on this thread. A slot that can't be read so (one past the table, a file not open, a packet
    /// the batch leaves to its caller) is read by ReadTile, after the packet ReadTile would have sought last before it.
    /// </summary>
    MCTileReads ReadTiles(std::span<const int32_t> slots);

    /// <summary>
    /// Port-only: slot <paramref name="tileNum"/> of the table as it is, read or not (none outside the table's slots and
    /// the one past it).
    /// </summary>
    MCOptionalRef<const MCTerrainTile> Slot(int32_t tileNum) const;

    /// <summary>
    /// Port-only (PLAN.md's first-use item 10): puts <paramref name="packet"/> in the cache as slot
    /// <paramref name="slot"/>'s tile, as reading it would (a ground tile rebuilt from its HD frame,
    /// MCRebuildGroundTile). Whether it did: not for a slot outside the table, cached already or missing, or an empty
    /// packet.
    /// </summary>
    bool Put(int32_t slot, std::span<const uint8_t> packet);

    /// <summary>
    /// The cached tile <paramref name="tileNum"/> of the view's tile set (<paramref name="rotated"/>: the rotated
    /// set), read in when it isn't loaded.
    /// </summary>
    /// <returns>The tile's data, or none when the number is negative, past the table, or its packet is missing.</returns>
    std::span<const uint8_t> Lookup(int32_t tileNum, bool rotated = false);

    /// <summary>Reads slot <paramref name="tileNum"/> of the table into the cache.</summary>
    /// <returns>The tile's data, or none when its packet is missing or empty.</returns>
    std::span<const uint8_t> ReadTile(int32_t tileNum);

    /// <summary>Number of tile slots (packets in the tile file).</summary>
    int32_t NumTiles() const { return _NumTiles; }

    /// <summary>The tile set isn't the default one ("tiles").</summary>
    bool CustomTileSet() const { return _CustomTileSet; }

    /// <summary>
    /// Port-only (HD art, terrain/MCGroundNames.h): a 64-bit FNV-1a hash of the tile set's two files as they open: each
    /// one's seek table and length (none for a file not open). What names the set in the program's table of the HD
    /// pack's names without reading a tile.
    /// </summary>
    uint64_t FilesHash() const;

    /// <summary>Added to a vertex's tile index: [0] = 0 for the normal view, [1] = numTiles / 2 for the rotated one.</summary>
    std::array<int32_t, 2> TileSetOffset{};

private:
    /// <summary>Number of tile slots (packets in the tile file).</summary>
    int32_t _NumTiles = 0;
    /// <summary>The slots (one more than <see cref="_NumTiles"/>).</summary>
    std::vector<MCTerrainTile> _Tiles;
    /// <summary><c>&lt;name&gt;.pak</c>.</summary>
    std::unique_ptr<MCPacketFile> _TileFile;
    /// <summary><c>&lt;name&gt;90.pak</c>, the tiles for the rotated view.</summary>
    std::unique_ptr<MCPacketFile> _Tile90File;
    /// <summary>The tile set isn't the default one ("tiles").</summary>
    bool _CustomTileSet = false;
};

/// <summary>The folder of the tile files ("data\tiles\").</summary>
extern std::string TilePath;
/// <summary>The folder of the rotated tile files ("data\tiles\").</summary>
extern std::string Tile90Path;

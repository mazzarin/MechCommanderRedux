#pragma once

#include "lib/MCOptionalRef.h"
#include <expected>
#include <string>
#include <vector>

class MCTerrainTiles;

/// <summary>
/// Port-only (HD art): the name the HD pack keeps a ground tile's or an overlay's HD frame under (the 64-bit FNV-1a
/// hash of the original's bytes, sprite/MCHdArt.h's TerrainTileName), and its size in the original's pixels.
/// </summary>
struct MCGroundName
{
    uint64_t Key = 0;
    int32_t Width = 0;
    int32_t Height = 0;

    bool operator==(const MCGroundName&) const = default;
};

/// <summary>
/// A tile set's slot's names: as a ground tile (what MCGroundMesh's GroundTile keeps of it: its bytes up to its last
/// row's end) and as an overlay (its fast shape 0, up to its last row's end), each none where the slot holds no such
/// art.
/// </summary>
struct MCGroundSlotNames
{
    std::optional<MCGroundName> Tile;
    std::optional<MCGroundName> Overlay;

    bool operator==(const MCGroundSlotNames&) const = default;
};

/// <summary>
/// One of a known tile set's names, as the program's table holds it (terrain/MCGroundNamesKnown.h): its slot and its
/// name, the size in 16 bits (the tool checks that it fits).
/// </summary>
struct MCGroundNameRow
{
    uint64_t Key = 0;
    uint16_t Slot = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;

    bool operator==(const MCGroundNameRow&) const = default;
};

/// <summary>
/// Port-only (PLAN.md's first-use item 10): what the program carries of an original ground tile besides its HD frame's
/// indices, so the tile is rebuilt without reading it (MCRebuildGroundTile): its header's hot spot and box width, and
/// its tip, the row past the height it states (its diamond's bottom point, which the original never draws: kept for its
/// bytes).
/// </summary>
struct MCGroundTileShape
{
    uint8_t HotX = 0;
    uint8_t HotY = 0;
    uint8_t BoxWidth = 0;
    std::span<const uint8_t> Tip;
};

/// <summary>
/// A ground tile's shape as the program's table holds it, beside its name (MCKnownGroundNames::Shapes): its header's
/// bytes, and its tip as a stretch of the set's tips (MCKnownGroundNames::Tips).
/// </summary>
struct MCGroundShapeRow
{
    uint8_t HotX = 0;
    uint8_t HotY = 0;
    uint8_t BoxWidth = 0;
    uint8_t TipLength = 0;
    uint32_t TipAt = 0;

    bool operator==(const MCGroundShapeRow&) const = default;
};

/// <summary>
/// Port-only (PLAN.md's first-use item 10): what the program carries of an original overlay besides its HD frame's
/// indices, so it is rebuilt without reading it (MCRebuildGroundOverlay): its shape's hot spot.
/// </summary>
struct MCGroundOverlaySpot
{
    int16_t X = 0;
    int16_t Y = 0;

    bool operator==(const MCGroundOverlaySpot&) const = default;
};

/// <summary>
/// A tile set the program knows: its files' hash (MCTerrainTiles::FilesHash), its slots' count, and the names of the
/// slots that hold a ground tile and of those that hold an overlay, each in slot order; each ground tile's shape beside
/// its name (PLAN.md's first-use item 10), the bytes of their tips, and each overlay's hot spot beside its name.
/// </summary>
struct MCKnownGroundNames
{
    uint64_t FilesHash = 0;
    int32_t Slots = 0;
    std::span<const MCGroundNameRow> Tiles;
    std::span<const MCGroundNameRow> Overlays;
    std::span<const MCGroundShapeRow> Shapes;
    std::span<const uint8_t> Tips;
    std::span<const MCGroundOverlaySpot> Spots;

    /// <summary>Slot <paramref name="slot"/>'s names (none for a slot outside the set).</summary>
    MCGroundSlotNames At(int32_t slot) const;

    /// <summary>The shape of the ground tile in slot <paramref name="slot"/> (none for a slot without one).</summary>
    std::optional<MCGroundTileShape> ShapeAt(int32_t slot) const;

    /// <summary>The hot spot of the overlay in slot <paramref name="slot"/> (none for a slot without one).</summary>
    std::optional<MCGroundOverlaySpot> SpotAt(int32_t slot) const;
};

/// <summary>
/// Port-only (the version 2 HD pack's records 2 and 3, sprite/MCPackRecords.h): a tile set's names, shapes and spots,
/// owned, so that <see cref="View"/> stands for the table the program carries.
/// </summary>
struct MCGroundSet
{
    uint64_t FilesHash = 0;
    int32_t Slots = 0;
    std::vector<MCGroundNameRow> Tiles;
    std::vector<MCGroundShapeRow> Shapes;
    std::vector<uint8_t> Tips;
    std::vector<MCGroundNameRow> Overlays;
    std::vector<MCGroundOverlaySpot> Spots;

    /// <summary>The set as the program's table's type, viewing this one's rows.</summary>
    [[nodiscard]] MCKnownGroundNames View() const;
};

/// <summary>
/// Works out slot <paramref name="slot"/>'s names from its art, reading it into the cache when it isn't there: the
/// tool's way (mc_tests --ground-names-table), and the test's that holds the table to the game's data.
/// </summary>
MCGroundSlotNames MCWorkOutGroundNames(MCTerrainTiles& tiles, int32_t slot);

/// <summary>
/// Port-only (PLAN.md's first-use item 10): works out the shape of the ground tile in slot <paramref name="slot"/> from
/// its bytes, as MCWorkOutGroundNames does its names (its tip a view of the cached tile); none where the slot holds no
/// ground tile.
/// </summary>
std::optional<MCGroundTileShape> MCWorkOutGroundTileShape(MCTerrainTiles& tiles, int32_t slot);

/// <summary>
/// Port-only (PLAN.md's first-use item 10): works out the hot spot of the overlay in slot <paramref name="slot"/> from its
/// bytes; none where the slot holds no overlay.
/// </summary>
std::optional<MCGroundOverlaySpot> MCWorkOutGroundOverlaySpot(MCTerrainTiles& tiles, int32_t slot);

/// <summary>
/// Port-only (the version 2 HD pack's records 2 and 3): every slot of <paramref name="tiles"/> worked out from its art
/// (<see cref="MCWorkOutGroundNames"/>, <see cref="MCWorkOutGroundTileShape"/>, <see cref="MCWorkOutGroundOverlaySpot"/>),
/// as the program's table holds a set; an error naming the slot for a name or tip too big for its row, or a ground tile
/// or overlay whose shape or spot can't be worked out.
/// </summary>
std::expected<MCGroundSet, std::string> MCWorkOutGroundSet(MCTerrainTiles& tiles);

/// <summary>Whether shapes <paramref name="a"/> and <paramref name="b"/> are the same, their tips byte for byte.</summary>
bool MCSameGroundTileShape(const MCGroundTileShape& a, const MCGroundTileShape& b);

/// <summary>
/// Port-only (PLAN.md's first-use item 6, Part B): the names of <paramref name="tiles"/>' slots from the table the
/// program carries (terrain/MCGroundNamesKnown.h) when it holds that tile set, by its files as they open, so the HD
/// ground is asked for before a tile is read. None for a tile set it lacks (a mod's), whose tiles are read and hashed.
/// </summary>
MCOptionalRef<const MCKnownGroundNames> MCKnownGroundNamesOf(const MCTerrainTiles& tiles);

/// <summary>
/// Port-only (the version 2 HD pack's records 2 and 3): the tile sets <see cref="MCKnownGroundNamesOf"/> looks in: the
/// pack's records when its terrain.hda or overlays.hda holds one, else the program's table; valid until the pack or HD
/// art's state changes.
/// </summary>
std::span<const MCKnownGroundNames> MCGroundNamesInUse();

/// <summary>
/// Port-only (PLAN.md's first-use item 10): the original's packet of the ground tile <paramref name="name"/> names,
/// rebuilt from its HD frame's <paramref name="indices"/> (R8G8 per original pixel, as MCHdArt::Frame keeps them: R its
/// palette index, G not 0 where drawn) and <paramref name="shape"/>, laid out as the original's tiles are: the header,
/// the rows' table (each row's start, the tip's, the packet's end), each row one run from its first drawn pixel (an
/// empty row no bytes), then the tip. None unless the bytes up to its last row's end are those the name hashes
/// (MCHdArt::TerrainTileKey), so a pack or table that doesn't match the game's tiles leaves them to be read.
/// </summary>
std::optional<std::vector<uint8_t>> MCRebuildGroundTile(std::span<const uint8_t> indices, const MCGroundName& name,
                                                        const MCGroundTileShape& shape);

/// <summary>
/// Port-only (PLAN.md's first-use item 10): the original's packet of the overlay <paramref name="name"/> names, rebuilt
/// from its HD frame's <paramref name="indices"/> (as MCRebuildGroundTile's) and its hot spot <paramref name="spot"/>,
/// laid out as the original's overlays are: a shape table of one fast shape, its header, its rows' table (each row's
/// start, the rows' end, the shape's end), each row's see-through pixels as runs and every stretch of drawn ones as
/// literals (at most 127 each, the frame's last column left out, as the original's rows leave it), then a blank row.
/// The indices must mark drawn only the pixels the original draws: the shipped pack's overlay frames spread their
/// indices a few pixels past them, marked the same, so they aren't rebuilt from it (PLAN.md's item 10). None unless the
/// bytes the name hashes (FastShapeSize's, as MCGroundOverlayOf takes them) are the name's, so a pack or table that
/// doesn't match the game's overlays leaves them to be read.
/// </summary>
std::optional<std::vector<uint8_t>> MCRebuildGroundOverlay(std::span<const uint8_t> indices, const MCGroundName& name,
                                                           MCGroundOverlaySpot spot);

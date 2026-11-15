#pragma once

#include "lib/MCOptionalRef.h"

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
};

/// <summary>
/// A tile set the program knows: its files' hash (MCTerrainTiles::FilesHash), its slots' count, and the names of the
/// slots that hold a ground tile and of those that hold an overlay, each in slot order.
/// </summary>
struct MCKnownGroundNames
{
    uint64_t FilesHash = 0;
    int32_t Slots = 0;
    std::span<const MCGroundNameRow> Tiles;
    std::span<const MCGroundNameRow> Overlays;

    /// <summary>Slot <paramref name="slot"/>'s names (none for a slot outside the set).</summary>
    MCGroundSlotNames At(int32_t slot) const;
};

/// <summary>
/// Works out slot <paramref name="slot"/>'s names from its art, reading it into the cache when it isn't there: the
/// tool's way (mc_tests --ground-names-table), and the test's that holds the table to the game's data.
/// </summary>
MCGroundSlotNames MCWorkOutGroundNames(MCTerrainTiles& tiles, int32_t slot);

/// <summary>
/// Port-only (PLAN.md's first-use item 6, Part B): the names of <paramref name="tiles"/>' slots from the table the
/// program carries (terrain/MCGroundNamesKnown.h) when it holds that tile set, by its files as they open, so the HD
/// ground is asked for before a tile is read. None for a tile set it lacks (a mod's), whose tiles are read and hashed.
/// </summary>
MCOptionalRef<const MCKnownGroundNames> MCKnownGroundNamesOf(const MCTerrainTiles& tiles);

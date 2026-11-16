#pragma once

// Port-only (HD art, SPEC-pack-v2.md's "The facts"): the facts records of a version 2 HD pack that the game's own tool
// writes (mc_tests --pack-records) and its reader reads, so their layout is the game's alone. Record 1, the shape table
// (a sprite packet's own folder), is the original's packet without its frames' rows (ShapeTable), the game's own
// layout. Each other record's data, little-endian, starts with its layout's version and its count of sets or rows (u16
// each):
// - 2, the ground table (terrain.hda's own folder): for each tile set its files' hash (u64), slots (i32), ground tiles
//   (u32) and tips' bytes (u32); then a row a tile (its name's key u64, slot, width and height u16; its shape's hot
//   spot, box width and tip's length u8; 2 bytes of 0; where its tip starts in the set's tips u32); then the tips.
// - 3, the overlay spots (overlays.hda's own folder): for each tile set its files' hash (u64), slots (i32) and
//   overlays (u32); then a row an overlay (its name's key u64, slot, width and height u16, hot spot x and y i16, 2
//   bytes of 0).
// - 4, the stops key (a legs archive's own folder): a row a mech tree whose legs it holds (sprite/MCMechStops.h's
//   MCStopsKey): its layout's hash (u64); for each part its file number, first packet and count, then the mech's PAK's
//   start and bytes in the part PAK and the run's in the mech's PAK (u32 each); its art's hash (u64).

#include "sprite/MCMechStops.h"
#include "terrain/MCGroundNames.h"
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace MCPackRecords
{
    /// <summary>A facts record's type (SPEC-pack-v2.md's "The facts").</summary>
    enum class Type : uint16_t
    {
        ShapeTable = 1,
        GroundTable = 2,
        OverlaySpots = 3,
        StopsKey = 4,
    };

    /// <summary>The version of the layouts this build writes and reads (each record's first u16).</summary>
    inline constexpr uint16_t Version = 1;

    /// <summary>Record 2's data for <paramref name="sets"/>: their ground tiles' names and shapes.</summary>
    [[nodiscard]] std::vector<uint8_t> GroundTable(std::span<const MCKnownGroundNames> sets);

    /// <summary>Record 3's data for <paramref name="sets"/>: their overlays' names and hot spots.</summary>
    [[nodiscard]] std::vector<uint8_t> OverlaySpots(std::span<const MCKnownGroundNames> sets);

    /// <summary>
    /// Record 1's data for <paramref name="packet"/>, a sprite packet (a VFX shape table, or a gesture header and one):
    /// the packet without its frames' rows. It keeps the gesture header, the table's version and count, its directory and
    /// the header of each frame it holds (MCVfxHasShape), with a translucent frame's 03 00 (MCAgShapeIsAlpha); the
    /// directory points at the kept headers, and a frame the table doesn't hold at the table's end. An error for a
    /// packet with no table, or a directory past its end.
    /// </summary>
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> ShapeTable(std::span<const uint8_t> packet);

    /// <summary>Record 4's data for <paramref name="keys"/>.</summary>
    [[nodiscard]] std::vector<uint8_t> StopsKey(std::span<const MCStopsKey> keys);

    /// <summary>
    /// The tile sets records 2 and 3 hold (either empty: none of its half), joined by their files' hash; an error naming
    /// what's wrong for data cut short, with bytes past its sets, of another version, or a set in both whose slots
    /// differ.
    /// </summary>
    [[nodiscard]] std::expected<std::vector<MCGroundSet>, std::string> ReadGround(
        std::span<const uint8_t> groundTable, std::span<const uint8_t> overlaySpots);

    /// <summary>The keys record 4 holds; an error for data cut short, with bytes past its rows, or of another version.</summary>
    [[nodiscard]] std::expected<std::vector<MCStopsKey>, std::string> ReadStopsKey(std::span<const uint8_t> data);
} // namespace MCPackRecords

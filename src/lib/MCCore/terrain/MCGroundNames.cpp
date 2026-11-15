#include "stdafx.h"
#include "terrain/MCGroundNames.h"
#include "sprite/MCHdArt.h"
#include "terrain/MCGroundMesh.h"
#include "terrain/MCGroundNamesKnown.h"
#include "terrain/MCTerrainTiles.h"

MCGroundSlotNames MCWorkOutGroundNames(MCTerrainTiles& tiles, int32_t slot)
{
    MCGroundSlotNames names;

    if (const MCHdArt::GroundAhead tile = MCGroundTileOf(tiles, slot); !tile.Tile.empty())
    {
        names.Tile = MCGroundName{MCHdArt::TerrainTileKey(tile.Tile), tile.Width, tile.Height};
    }

    if (const std::optional<MCHdArt::GroundAhead> overlay = MCGroundOverlayOf(tiles, slot); overlay.has_value())
    {
        names.Overlay = MCGroundName{MCHdArt::TerrainTileKey(overlay->Tile), overlay->Width, overlay->Height};
    }

    return names;
}

namespace
{
    /// <summary>The name <paramref name="rows"/> hold for slot <paramref name="slot"/>, if any.</summary>
    auto NameAt(std::span<const MCGroundNameRow> rows, int32_t slot) -> std::optional<MCGroundName>
    {
        const auto row = std::ranges::lower_bound(rows, slot, std::less<>(),
                                                  [](const MCGroundNameRow& named) { return int32_t{named.Slot}; });

        if (row == rows.end() || row->Slot != slot)
        {
            return std::nullopt;
        }

        return MCGroundName{row->Key, row->Width, row->Height};
    }
} // namespace

MCGroundSlotNames MCKnownGroundNames::At(int32_t slot) const
{
    if (slot < 0 || slot >= Slots)
    {
        return {};
    }

    return MCGroundSlotNames{NameAt(Tiles, slot), NameAt(Overlays, slot)};
}

MCOptionalRef<const MCKnownGroundNames> MCKnownGroundNamesOf(const MCTerrainTiles& tiles)
{
    const uint64_t hash = tiles.FilesHash();

    for (const MCKnownGroundNames& known : MCGroundNamesKnown())
    {
        if (known.FilesHash == hash && known.Slots == tiles.NumTiles())
        {
            return known;
        }
    }

    return std::nullopt;
}

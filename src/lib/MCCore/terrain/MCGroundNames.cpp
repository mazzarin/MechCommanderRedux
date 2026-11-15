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
    /// <summary>VFX's tile layout (vfx/MCVfxTile.cpp): the hot spot, the height and the box's width, then the offsets.</summary>
    constexpr size_t TileHeaderBytes = 4;
    constexpr size_t TileOffsetBytes = sizeof(uint32_t);

    /// <summary>Where row <paramref name="row"/> of ground tile <paramref name="tile"/> starts.</summary>
    auto TileRowAt(std::span<const uint8_t> tile, int32_t row) -> size_t
    {
        uint32_t offset = 0;
        std::memcpy(&offset, tile.data() + TileHeaderBytes + static_cast<size_t>(row) * TileOffsetBytes,
                    TileOffsetBytes);
        return offset;
    }

    /// <summary>Where <paramref name="rows"/> hold slot <paramref name="slot"/>, if they do.</summary>
    auto RowOf(std::span<const MCGroundNameRow> rows, int32_t slot) -> std::optional<size_t>
    {
        const auto row = std::ranges::lower_bound(rows, slot, std::less<>(),
                                                  [](const MCGroundNameRow& named) { return int32_t{named.Slot}; });

        if (row == rows.end() || row->Slot != slot)
        {
            return std::nullopt;
        }

        return static_cast<size_t>(row - rows.begin());
    }

    /// <summary>The name <paramref name="rows"/> hold for slot <paramref name="slot"/>, if any.</summary>
    auto NameAt(std::span<const MCGroundNameRow> rows, int32_t slot) -> std::optional<MCGroundName>
    {
        const std::optional<size_t> at = RowOf(rows, slot);

        if (!at.has_value())
        {
            return std::nullopt;
        }

        const MCGroundNameRow& row = rows[*at];
        return MCGroundName{row.Key, row.Width, row.Height};
    }
} // namespace

std::optional<MCGroundTileShape> MCWorkOutGroundTileShape(MCTerrainTiles& tiles, int32_t slot)
{
    if (MCGroundTileOf(tiles, slot).Tile.empty())
    {
        return std::nullopt;
    }

    // The whole packet, its tip past the bytes MCGroundTileOf keeps.
    const std::span<const uint8_t> packet = tiles.Lookup(slot);
    return MCGroundTileShape{packet[0], packet[1], packet[3], packet.subspan(TileRowAt(packet, packet[2]))};
}

bool MCSameGroundTileShape(const MCGroundTileShape& a, const MCGroundTileShape& b)
{
    return a.HotX == b.HotX && a.HotY == b.HotY && a.BoxWidth == b.BoxWidth && std::ranges::equal(a.Tip, b.Tip);
}

MCGroundSlotNames MCKnownGroundNames::At(int32_t slot) const
{
    if (slot < 0 || slot >= Slots)
    {
        return {};
    }

    return MCGroundSlotNames{NameAt(Tiles, slot), NameAt(Overlays, slot)};
}

std::optional<MCGroundTileShape> MCKnownGroundNames::ShapeAt(int32_t slot) const
{
    const std::optional<size_t> at = slot >= 0 && slot < Slots ? RowOf(Tiles, slot) : std::nullopt;

    if (!at.has_value() || *at >= Shapes.size())
    {
        return std::nullopt;
    }

    const MCGroundShapeRow& row = Shapes[*at];

    if (static_cast<size_t>(row.TipAt) + row.TipLength > Tips.size())
    {
        return std::nullopt;
    }

    return MCGroundTileShape{row.HotX, row.HotY, row.BoxWidth, Tips.subspan(row.TipAt, row.TipLength)};
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

std::optional<std::vector<uint8_t>> MCRebuildGroundTile(std::span<const uint8_t> indices, const MCGroundName& name,
                                                        const MCGroundTileShape& shape)
{
    const int32_t width = name.Width;
    const int32_t height = name.Height;

    if (width <= 0 || height <= 0 || height > UINT8_MAX ||
        indices.size() != static_cast<size_t>(width) * height * MCHdArt::IndexBytes)
    {
        return std::nullopt;
    }

    // The header, then height + 2 offsets: each row's start, the tip's, the packet's end.
    std::vector<uint8_t> packet(TileHeaderBytes + (static_cast<size_t>(height) + 2) * TileOffsetBytes);
    packet[0] = shape.HotX;
    packet[1] = shape.HotY;
    packet[2] = static_cast<uint8_t>(height);
    packet[3] = shape.BoxWidth;
    const auto putOffset = [&packet](int32_t entry, size_t at)
    {
        const auto value = static_cast<uint32_t>(at);
        std::memcpy(packet.data() + TileHeaderBytes + static_cast<size_t>(entry) * TileOffsetBytes, &value,
                    TileOffsetBytes);
    };

    for (int32_t row = 0; row < height; ++row)
    {
        putOffset(row, packet.size());
        const std::span<const uint8_t> line =
            indices.subspan(static_cast<size_t>(row) * width * MCHdArt::IndexBytes, width * MCHdArt::IndexBytes);
        const auto drawn = [&line](int32_t x) { return line[static_cast<size_t>(x) * MCHdArt::IndexBytes + 1] != 0; };
        int32_t first = 0;

        while (first < width && !drawn(first))
        {
            ++first;
        }

        if (first == width)
        {
            continue;
        }

        int32_t end = width;

        while (!drawn(end - 1))
        {
            --end;
        }

        if (first > UINT8_MAX)
        {
            return std::nullopt;
        }

        packet.push_back(static_cast<uint8_t>(first));

        for (int32_t x = first; x < end; ++x)
        {
            // A tile's row is one run (DecodeTile's): a gap means these indices aren't a tile's.
            if (!drawn(x))
            {
                return std::nullopt;
            }

            packet.push_back(line[static_cast<size_t>(x) * MCHdArt::IndexBytes]);
        }
    }

    const size_t kept = packet.size();
    putOffset(height, kept);
    putOffset(height + 1, kept + shape.Tip.size());

    if (MCHdArt::TerrainTileKey(packet) != name.Key)
    {
        return std::nullopt;
    }

    packet.insert(packet.end(), shape.Tip.begin(), shape.Tip.end());
    return packet;
}

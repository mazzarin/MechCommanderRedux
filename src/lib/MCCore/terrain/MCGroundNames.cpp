#include "stdafx.h"
#include "terrain/MCGroundNames.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCPackRecords.h"
#include "terrain/MCGroundMesh.h"
#include "terrain/MCGroundNamesKnown.h"
#include "terrain/MCTerrainTiles.h"
#include "vfx/MCVfxFunctions.h"

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

namespace
{
    /// <summary>
    /// An overlay's packet (VFX's fast shapes, vfx/MCVfxFastShape.cpp, as the original's tile sets lay them out): a
    /// shape table of one shape (the tag, the count, shape 0's offset, a zero word), then shape 0: its tag, its hot
    /// spot's x and y, its height, the width its rows add up to, then its rows' table of 16-bit offsets.
    /// </summary>
    constexpr size_t OverlayTableBytes = 16;
    constexpr size_t OverlayXAt = 4;
    constexpr size_t OverlayYAt = 6;
    constexpr size_t OverlayHeightAt = 8;
    constexpr size_t OverlayWidthAt = 10;
    constexpr size_t OverlayRowsAt = 12;
    constexpr size_t OverlayOffsetBytes = sizeof(uint16_t);
    /// <summary>A row's code below this is a run of the byte after it (0xff see-through), else literals less it.</summary>
    constexpr uint8_t OverlayLiterals = 0x80;
    constexpr uint8_t OverlaySeeThrough = 0xff;
    constexpr int32_t OverlayMostInCode = 0x7f;

    void Put16(std::vector<uint8_t>& bytes, size_t at, uint16_t value)
    {
        std::memcpy(bytes.data() + at, &value, sizeof(value));
    }

    void Put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value)
    {
        std::memcpy(bytes.data() + at, &value, sizeof(value));
    }

    /// <summary>
    /// Appends a row of <paramref name="width"/> pixels, <paramref name="drawn"/>'s see-through as runs and drawn
    /// stretches as literals of <paramref name="index"/>'s, as the original's overlays encode them.
    /// </summary>
    template <typename Drawn, typename Index>
    void PutOverlayRow(std::vector<uint8_t>& bytes, int32_t width, Drawn drawn, Index index)
    {
        for (int32_t x = 0; x < width;)
        {
            const bool opaque = drawn(x);
            int32_t end = x + 1;

            while (end < width && end - x < OverlayMostInCode && drawn(end) == opaque)
            {
                ++end;
            }

            if (!opaque)
            {
                bytes.push_back(static_cast<uint8_t>(end - x));
                bytes.push_back(OverlaySeeThrough);
            }
            else
            {
                bytes.push_back(static_cast<uint8_t>(OverlayLiterals + (end - x)));

                for (int32_t at = x; at < end; ++at)
                {
                    bytes.push_back(index(at));
                }
            }

            x = end;
        }
    }
} // namespace

std::optional<MCGroundOverlaySpot> MCWorkOutGroundOverlaySpot(MCTerrainTiles& tiles, int32_t slot)
{
    if (!MCGroundOverlayOf(tiles, slot).has_value())
    {
        return std::nullopt;
    }

    const std::optional<std::span<const uint8_t>> shape = FastShapeAt(tiles.Lookup(slot), 0);
    int16_t x = 0;
    int16_t y = 0;
    std::memcpy(&x, shape->data() + OverlayXAt, sizeof(x));
    std::memcpy(&y, shape->data() + OverlayYAt, sizeof(y));
    return MCGroundOverlaySpot{x, y};
}

std::optional<std::vector<uint8_t>> MCRebuildGroundOverlay(std::span<const uint8_t> indices, const MCGroundName& name,
                                                           MCGroundOverlaySpot spot)
{
    // The rows hold a column fewer than the frame.
    const int32_t frameWidth = name.Width;
    const int32_t width = frameWidth - 1;
    const int32_t height = name.Height;

    if (width <= 0 || height <= 0 || width > UINT16_MAX || height > UINT16_MAX - 2 ||
        indices.size() != static_cast<size_t>(frameWidth) * height * MCHdArt::IndexBytes)
    {
        return std::nullopt;
    }

    std::vector<uint8_t> packet(OverlayTableBytes + OverlayRowsAt +
                                (static_cast<size_t>(height) + 2) * OverlayOffsetBytes);
    Put32(packet, 0, static_cast<uint32_t>(FastShapeTag));
    Put32(packet, 4, 1);
    Put32(packet, 8, static_cast<uint32_t>(OverlayTableBytes));
    Put32(packet, OverlayTableBytes, static_cast<uint32_t>(FastShapeTag));
    std::memcpy(packet.data() + OverlayTableBytes + OverlayXAt, &spot.X, sizeof(spot.X));
    std::memcpy(packet.data() + OverlayTableBytes + OverlayYAt, &spot.Y, sizeof(spot.Y));
    Put16(packet, OverlayTableBytes + OverlayHeightAt, static_cast<uint16_t>(height));
    Put16(packet, OverlayTableBytes + OverlayWidthAt, static_cast<uint16_t>(width));
    // Each offset from the shape's start, which must fit its 16 bits.
    const auto putOffset = [&packet](int32_t entry) -> bool
    {
        const size_t offset = packet.size() - OverlayTableBytes;

        if (offset > UINT16_MAX)
        {
            return false;
        }

        Put16(packet, OverlayTableBytes + OverlayRowsAt + static_cast<size_t>(entry) * OverlayOffsetBytes,
              static_cast<uint16_t>(offset));
        return true;
    };

    for (int32_t row = 0; row < height; ++row)
    {
        if (!putOffset(row))
        {
            return std::nullopt;
        }

        const size_t line = static_cast<size_t>(row) * frameWidth * MCHdArt::IndexBytes;
        PutOverlayRow(
            packet, width,
            [&](int32_t x) { return indices[line + static_cast<size_t>(x) * MCHdArt::IndexBytes + 1] != 0; },
            [&](int32_t x) { return indices[line + static_cast<size_t>(x) * MCHdArt::IndexBytes]; });
    }

    if (!putOffset(height))
    {
        return std::nullopt;
    }

    PutOverlayRow(packet, width, [](int32_t) { return false; }, [](int32_t) { return uint8_t{0}; });

    if (!putOffset(height + 1))
    {
        return std::nullopt;
    }

    // Checked by its name, the bytes FastShapeSize takes as the shape's (MCGroundOverlayOf): it reads the last row a
    // column wider, so a little way into the tip.
    const std::span<const uint8_t> shape = std::span<const uint8_t>(packet).subspan(OverlayTableBytes);
    const std::optional<size_t> named = FastShapeSize(shape);

    if (!named.has_value() || MCHdArt::TerrainTileKey(shape.first(*named)) != name.Key)
    {
        return std::nullopt;
    }

    return packet;
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

std::optional<MCGroundOverlaySpot> MCKnownGroundNames::SpotAt(int32_t slot) const
{
    const std::optional<size_t> at = slot >= 0 && slot < Slots ? RowOf(Overlays, slot) : std::nullopt;

    if (!at.has_value() || *at >= Spots.size())
    {
        return std::nullopt;
    }

    return Spots[*at];
}

namespace
{
    /// <summary>
    /// The tile sets a version 2 pack's records 2 and 3 hold (MCPackRecords::ReadGround), read once for each pack and
    /// HD art's state; their views stay good until the pack or that state changes.
    /// </summary>
    struct PackGround
    {
        std::mutex Lock;
        bool Read = false;
        uint64_t PackVersion = 0;
        bool Active = false;
        std::optional<std::vector<MCGroundSet>> Sets;
        std::vector<MCKnownGroundNames> Views;
    };

    PackGround& ThePackGround()
    {
        static PackGround ground;
        return ground;
    }

    /// <summary>
    /// The tile sets the pack's records give, in place of the program's table: none when neither terrain.hda nor
    /// overlays.hda holds its record (a version 1 pack, HD art not drawn) or the records can't be read (logged).
    /// </summary>
    std::optional<std::span<const MCKnownGroundNames>> PackGroundSets()
    {
        PackGround& ground = ThePackGround();
        std::scoped_lock lock(ground.Lock);
        const uint64_t version = MCHdArt::PackVersion();
        const bool active = MCHdArt::Active();

        if (!ground.Read || ground.PackVersion != version || ground.Active != active)
        {
            ground.Read = true;
            ground.PackVersion = version;
            ground.Active = active;
            ground.Sets.reset();
            ground.Views.clear();
            const std::optional<std::vector<uint8_t>> tiles =
                MCHdArt::ArchiveFacts(MCHdArt::TerrainFolder, static_cast<uint16_t>(MCPackRecords::Type::GroundTable));
            const std::optional<std::vector<uint8_t>> spots =
                MCHdArt::ArchiveFacts(MCHdArt::OverlayFolder, static_cast<uint16_t>(MCPackRecords::Type::OverlaySpots));

            if (tiles.has_value() || spots.has_value())
            {
                std::expected<std::vector<MCGroundSet>, std::string> read = MCPackRecords::ReadGround(
                    tiles.value_or(std::vector<uint8_t>{}), spots.value_or(std::vector<uint8_t>{}));

                if (read.has_value())
                {
                    ground.Sets = std::move(*read);
                    std::ranges::transform(*ground.Sets, std::back_inserter(ground.Views), &MCGroundSet::View);
                }
                else
                {
                    SDL_Log("HdArt: the HD pack's ground records: %s", read.error().c_str());
                }
            }
        }

        return ground.Sets.has_value() ? std::optional(std::span<const MCKnownGroundNames>(ground.Views))
                                       : std::nullopt;
    }
} // namespace

std::span<const MCKnownGroundNames> MCGroundNamesInUse()
{
    return PackGroundSets().value_or(MCGroundNamesKnown());
}

MCOptionalRef<const MCKnownGroundNames> MCKnownGroundNamesOf(const MCTerrainTiles& tiles)
{
    const uint64_t hash = tiles.FilesHash();

    for (const MCKnownGroundNames& known : MCGroundNamesInUse())
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

auto MCGroundSet::View() const -> MCKnownGroundNames
{
    return MCKnownGroundNames{.FilesHash = FilesHash,
                              .Slots = Slots,
                              .Tiles = Tiles,
                              .Overlays = Overlays,
                              .Shapes = Shapes,
                              .Tips = Tips,
                              .Spots = Spots};
}

namespace
{
    /// <summary>Slot <paramref name="slot"/>'s <paramref name="name"/> as a table's row; none where it doesn't fit.</summary>
    auto NameRow(int32_t slot, const MCGroundName& name) -> std::optional<MCGroundNameRow>
    {
        constexpr int32_t Most = std::numeric_limits<uint16_t>::max();

        if (slot < 0 || slot > Most || name.Width <= 0 || name.Width > Most || name.Height <= 0 || name.Height > Most ||
            name.Key == 0)
        {
            return std::nullopt;
        }

        return MCGroundNameRow{name.Key, static_cast<uint16_t>(slot), static_cast<uint16_t>(name.Width),
                               static_cast<uint16_t>(name.Height)};
    }
} // namespace

auto MCWorkOutGroundSet(MCTerrainTiles& tiles) -> std::expected<MCGroundSet, std::string>
{
    MCGroundSet set{.FilesHash = tiles.FilesHash(), .Slots = tiles.NumTiles()};

    for (int32_t slot = 0; slot < tiles.NumTiles(); ++slot)
    {
        const MCGroundSlotNames names = MCWorkOutGroundNames(tiles, slot);

        if (names.Tile.has_value())
        {
            const std::optional<MCGroundNameRow> row = NameRow(slot, *names.Tile);
            const std::optional<MCGroundTileShape> shape = MCWorkOutGroundTileShape(tiles, slot);

            if (!row.has_value() || !shape.has_value() || shape->Tip.size() > std::numeric_limits<uint8_t>::max() ||
                set.Tips.size() > std::numeric_limits<uint32_t>::max())
            {
                return std::unexpected(std::format("tile set {:016x}, slot {}: its ground tile's name or shape doesn't "
                                                   "fit the table",
                                                   set.FilesHash, slot));
            }

            set.Tiles.push_back(*row);
            set.Shapes.push_back(MCGroundShapeRow{shape->HotX, shape->HotY, shape->BoxWidth,
                                                  static_cast<uint8_t>(shape->Tip.size()),
                                                  static_cast<uint32_t>(set.Tips.size())});
            set.Tips.insert(set.Tips.end(), shape->Tip.begin(), shape->Tip.end());
        }

        if (names.Overlay.has_value())
        {
            const std::optional<MCGroundNameRow> row = NameRow(slot, *names.Overlay);
            const std::optional<MCGroundOverlaySpot> spot = MCWorkOutGroundOverlaySpot(tiles, slot);

            if (!row.has_value() || !spot.has_value())
            {
                return std::unexpected(std::format("tile set {:016x}, slot {}: its overlay's name or hot spot doesn't "
                                                   "fit the table",
                                                   set.FilesHash, slot));
            }

            set.Overlays.push_back(*row);
            set.Spots.push_back(*spot);
        }
    }

    return set;
}

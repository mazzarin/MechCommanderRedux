#include "stdafx.h"
#include "sprite/MCPackRecords.h"
#include "sprite/MCShape.h"
#include "vfx/MCAgShape.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The bytes of <paramref name="value"/> added to <paramref name="out"/> (the platforms are little-endian).</summary>
    template <typename T> auto Put(std::vector<uint8_t>& out, T value) -> void
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
        out.insert(out.end(), bytes, bytes + sizeof(T));
    }

    /// <summary>A record's data read in order, each value refused once the data runs out.</summary>
    struct Taker
    {
        std::span<const uint8_t> Data;
        size_t At = 0;

        template <typename T> auto Take(T& value) -> bool
        {
            static_assert(std::is_trivially_copyable_v<T>);

            if (Data.size() - At < sizeof(T))
            {
                return false;
            }

            std::memcpy(&value, Data.data() + At, sizeof(T));
            At += sizeof(T);
            return true;
        }

        auto TakeBytes(size_t count, std::vector<uint8_t>& bytes) -> bool
        {
            if (Data.size() - At < count)
            {
                return false;
            }

            bytes.assign(Data.begin() + static_cast<ptrdiff_t>(At), Data.begin() + static_cast<ptrdiff_t>(At + count));
            At += count;
            return true;
        }

        [[nodiscard]] auto Done() const -> bool { return At == Data.size(); }
    };

    /// <summary>A count as its u16 or u32 field holds it, which it must fit (the tool's data always does).</summary>
    template <typename T> auto Count(size_t count) -> T
    {
        if (count > std::numeric_limits<T>::max())
        {
            throw std::length_error(
                std::format("pack records: {} rows don't fit a field of {} bytes", count, sizeof(T)));
        }

        return static_cast<T>(count);
    }

    /// <summary>Starts a record's data: its layout's version and its count of sets or rows.</summary>
    auto Begin(size_t count) -> std::vector<uint8_t>
    {
        std::vector<uint8_t> out;
        Put(out, MCPackRecords::Version);
        Put(out, Count<uint16_t>(count));
        return out;
    }

    /// <summary>Reads a record's version and count; an error naming <paramref name="what"/> for another version.</summary>
    auto Opened(Taker& taker, std::string_view what) -> std::expected<uint16_t, std::string>
    {
        uint16_t version = 0;
        uint16_t count = 0;

        if (!taker.Take(version) || !taker.Take(count))
        {
            return std::unexpected(std::format("{}: cut short before its count", what));
        }

        if (version != MCPackRecords::Version)
        {
            return std::unexpected(
                std::format("{}: layout version {}, this build reads {}", what, version, MCPackRecords::Version));
        }

        return count;
    }

    /// <summary>A name's row: its key, slot, width and height.</summary>
    auto PutName(std::vector<uint8_t>& out, const MCGroundNameRow& row) -> void
    {
        Put(out, row.Key);
        Put(out, row.Slot);
        Put(out, row.Width);
        Put(out, row.Height);
    }

    auto TakeName(Taker& taker, MCGroundNameRow& row) -> bool
    {
        return taker.Take(row.Key) && taker.Take(row.Slot) && taker.Take(row.Width) && taker.Take(row.Height);
    }

    /// <summary>The set of <paramref name="sets"/> with files' hash <paramref name="hash"/>, made at the end if none.</summary>
    auto SetOf(std::vector<MCGroundSet>& sets, uint64_t hash, int32_t slots)
        -> std::expected<std::reference_wrapper<MCGroundSet>, std::string>
    {
        for (MCGroundSet& set : sets)
        {
            if (set.FilesHash == hash)
            {
                if (set.Slots != slots)
                {
                    return std::unexpected(std::format("tile set {:016x}: {} slots in one record, {} in the other",
                                                       hash, set.Slots, slots));
                }

                return std::ref(set);
            }
        }

        sets.push_back(MCGroundSet{.FilesHash = hash, .Slots = slots});
        return std::ref(sets.back());
    }

    constexpr uint16_t Padding = 0;
} // namespace

auto MCPackRecords::GroundTable(std::span<const MCKnownGroundNames> sets) -> std::vector<uint8_t>
{
    std::vector<uint8_t> out = Begin(sets.size());

    for (const MCKnownGroundNames& set : sets)
    {
        if (set.Shapes.size() != set.Tiles.size())
        {
            throw std::invalid_argument(std::format("pack records: tile set {:016x} has {} tiles and {} shapes",
                                                    set.FilesHash, set.Tiles.size(), set.Shapes.size()));
        }

        Put(out, set.FilesHash);
        Put(out, set.Slots);
        Put(out, Count<uint32_t>(set.Tiles.size()));
        Put(out, Count<uint32_t>(set.Tips.size()));

        for (size_t i = 0; i < set.Tiles.size(); ++i)
        {
            const MCGroundShapeRow& shape = set.Shapes[i];
            PutName(out, set.Tiles[i]);
            Put(out, shape.HotX);
            Put(out, shape.HotY);
            Put(out, shape.BoxWidth);
            Put(out, shape.TipLength);
            Put(out, Padding);
            Put(out, shape.TipAt);
        }

        out.insert(out.end(), set.Tips.begin(), set.Tips.end());
    }

    return out;
}

auto MCPackRecords::OverlaySpots(std::span<const MCKnownGroundNames> sets) -> std::vector<uint8_t>
{
    std::vector<uint8_t> out = Begin(sets.size());

    for (const MCKnownGroundNames& set : sets)
    {
        if (set.Spots.size() != set.Overlays.size())
        {
            throw std::invalid_argument(std::format("pack records: tile set {:016x} has {} overlays and {} spots",
                                                    set.FilesHash, set.Overlays.size(), set.Spots.size()));
        }

        Put(out, set.FilesHash);
        Put(out, set.Slots);
        Put(out, Count<uint32_t>(set.Overlays.size()));

        for (size_t i = 0; i < set.Overlays.size(); ++i)
        {
            PutName(out, set.Overlays[i]);
            Put(out, set.Spots[i].X);
            Put(out, set.Spots[i].Y);
            Put(out, Padding);
        }
    }

    return out;
}

auto MCPackRecords::ShapeTable(std::span<const uint8_t> packet) -> std::expected<std::vector<uint8_t>, std::string>
{
    constexpr size_t CountAt = 4;
    constexpr size_t DirectoryAt = 8;
    constexpr size_t EntryBytes = 8;
    constexpr size_t OffsetBytes = 4;
    constexpr std::array<uint8_t, 2> TranslucentMark = {3, 0};
    const size_t start = MCShape::TableStart(packet);

    if (packet.size() < start + DirectoryAt || !VfxIsShapeTable(packet.subspan(start)))
    {
        return std::unexpected(std::format("a packet of {} bytes holds no shape table", packet.size()));
    }

    const std::span<const uint8_t> table = packet.subspan(start);
    const int32_t count = MCVfxRead32(table, CountAt);

    if (count < 0 || DirectoryAt + static_cast<uint64_t>(count) * EntryBytes > table.size())
    {
        return std::unexpected(std::format("a shape table of {} bytes lists {} frames", table.size(), count));
    }

    const size_t directoryEnd = DirectoryAt + static_cast<size_t>(count) * EntryBytes;
    std::vector<std::optional<size_t>> offsets(static_cast<size_t>(count));
    std::vector<uint8_t> headers;

    for (int32_t frame = 0; frame < count; ++frame)
    {
        if (!MCVfxHasShape(table, frame))
        {
            continue;
        }

        offsets[static_cast<size_t>(frame)] = directoryEnd + headers.size();
        const std::span<const uint8_t> header = MCVfxShape(table, frame).first(sizeof(MCVfxShapeHeader));
        headers.insert(headers.end(), header.begin(), header.end());

        if (MCAgShapeIsAlpha(table, frame))
        {
            headers.insert(headers.end(), TranslucentMark.begin(), TranslucentMark.end());
        }
    }

    std::vector<uint8_t> out(packet.begin(), packet.begin() + static_cast<ptrdiff_t>(start + DirectoryAt));
    const size_t tableEnd = directoryEnd + headers.size();

    for (int32_t frame = 0; frame < count; ++frame)
    {
        const size_t entry = DirectoryAt + static_cast<size_t>(frame) * EntryBytes;
        Put(out, static_cast<uint32_t>(offsets[static_cast<size_t>(frame)].value_or(tableEnd)));
        const std::span<const uint8_t> rest = table.subspan(entry + OffsetBytes, EntryBytes - OffsetBytes);
        out.insert(out.end(), rest.begin(), rest.end());
    }

    out.insert(out.end(), headers.begin(), headers.end());
    return out;
}

auto MCPackRecords::StopsKey(std::span<const MCStopsKey> keys) -> std::vector<uint8_t>
{
    std::vector<uint8_t> out = Begin(keys.size());

    for (const MCStopsKey& key : keys)
    {
        Put(out, key.Layout);

        for (const MCStopsKeyPart& part : key.Parts)
        {
            Put(out, part.FileNumber);
            Put(out, part.First);
            Put(out, part.Count);
            Put(out, part.Place.MechAt);
            Put(out, part.Place.MechBytes);
            Put(out, part.Place.RunAt);
            Put(out, part.Place.RunBytes);
        }

        Put(out, key.ArtHash);
    }

    return out;
}

auto MCPackRecords::ReadGround(std::span<const uint8_t> groundTable, std::span<const uint8_t> overlaySpots)
    -> std::expected<std::vector<MCGroundSet>, std::string>
{
    std::vector<MCGroundSet> sets;

    if (!groundTable.empty())
    {
        Taker taker{groundTable};
        const std::expected<uint16_t, std::string> count = Opened(taker, "the ground table");

        if (!count.has_value())
        {
            return std::unexpected(count.error());
        }

        for (uint16_t s = 0; s < *count; ++s)
        {
            uint64_t hash = 0;
            int32_t slots = 0;
            uint32_t tiles = 0;
            uint32_t tipBytes = 0;

            if (!taker.Take(hash) || !taker.Take(slots) || !taker.Take(tiles) || !taker.Take(tipBytes))
            {
                return std::unexpected(std::format("the ground table: cut short in set {}'s head", s));
            }

            const auto got = SetOf(sets, hash, slots);

            if (!got.has_value())
            {
                return std::unexpected(got.error());
            }

            MCGroundSet& set = got->get();

            for (uint32_t t = 0; t < tiles; ++t)
            {
                MCGroundNameRow name;
                MCGroundShapeRow shape;
                uint16_t padding = 0;

                if (!TakeName(taker, name) || !taker.Take(shape.HotX) || !taker.Take(shape.HotY) ||
                    !taker.Take(shape.BoxWidth) || !taker.Take(shape.TipLength) || !taker.Take(padding) ||
                    !taker.Take(shape.TipAt))
                {
                    return std::unexpected(std::format("the ground table: cut short in set {}'s tile {}", s, t));
                }

                set.Tiles.push_back(name);
                set.Shapes.push_back(shape);
            }

            if (!taker.TakeBytes(tipBytes, set.Tips))
            {
                return std::unexpected(std::format("the ground table: cut short in set {}'s tips", s));
            }

            for (const MCGroundShapeRow& shape : set.Shapes)
            {
                if (static_cast<size_t>(shape.TipAt) + shape.TipLength > set.Tips.size())
                {
                    return std::unexpected(
                        std::format("the ground table: set {}'s tip at {} runs past its tips", s, shape.TipAt));
                }
            }
        }

        if (!taker.Done())
        {
            return std::unexpected(
                std::format("the ground table: {} bytes past its sets", groundTable.size() - taker.At));
        }
    }

    if (!overlaySpots.empty())
    {
        Taker taker{overlaySpots};
        const std::expected<uint16_t, std::string> count = Opened(taker, "the overlay spots");

        if (!count.has_value())
        {
            return std::unexpected(count.error());
        }

        for (uint16_t s = 0; s < *count; ++s)
        {
            uint64_t hash = 0;
            int32_t slots = 0;
            uint32_t overlays = 0;

            if (!taker.Take(hash) || !taker.Take(slots) || !taker.Take(overlays))
            {
                return std::unexpected(std::format("the overlay spots: cut short in set {}'s head", s));
            }

            const auto got = SetOf(sets, hash, slots);

            if (!got.has_value())
            {
                return std::unexpected(got.error());
            }

            MCGroundSet& set = got->get();

            for (uint32_t o = 0; o < overlays; ++o)
            {
                MCGroundNameRow name;
                MCGroundOverlaySpot spot;
                uint16_t padding = 0;

                if (!TakeName(taker, name) || !taker.Take(spot.X) || !taker.Take(spot.Y) || !taker.Take(padding))
                {
                    return std::unexpected(std::format("the overlay spots: cut short in set {}'s overlay {}", s, o));
                }

                set.Overlays.push_back(name);
                set.Spots.push_back(spot);
            }
        }

        if (!taker.Done())
        {
            return std::unexpected(
                std::format("the overlay spots: {} bytes past its sets", overlaySpots.size() - taker.At));
        }
    }

    return sets;
}

auto MCPackRecords::ReadStopsKey(std::span<const uint8_t> data) -> std::expected<std::vector<MCStopsKey>, std::string>
{
    Taker taker{data};
    const std::expected<uint16_t, std::string> count = Opened(taker, "the stops key");

    if (!count.has_value())
    {
        return std::unexpected(count.error());
    }

    std::vector<MCStopsKey> keys(*count);

    for (MCStopsKey& key : keys)
    {
        bool whole = taker.Take(key.Layout);

        for (MCStopsKeyPart& part : key.Parts)
        {
            whole = whole && taker.Take(part.FileNumber) && taker.Take(part.First) && taker.Take(part.Count) &&
                    taker.Take(part.Place.MechAt) && taker.Take(part.Place.MechBytes) && taker.Take(part.Place.RunAt) &&
                    taker.Take(part.Place.RunBytes);
        }

        if (!whole || !taker.Take(key.ArtHash))
        {
            return std::unexpected(
                std::format("the stops key: cut short in row {}", static_cast<size_t>(&key - keys.data())));
        }
    }

    if (!taker.Done())
    {
        return std::unexpected(std::format("the stops key: {} bytes past its rows", data.size() - taker.At));
    }

    return keys;
}

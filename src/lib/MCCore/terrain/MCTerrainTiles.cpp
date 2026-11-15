#include "stdafx.h"
#include "terrain/MCTerrainTiles.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"

std::string TilePath = "data\\tiles\\";
std::string Tile90Path = "data\\tiles\\";

auto MCTerrainTiles::Create(std::string_view tileFileName)
    -> std::expected<std::unique_ptr<MCTerrainTiles>, std::string>
{
    auto tileFile = std::make_unique<MCPacketFile>();

    if (const int32_t result = tileFile->Open(GamePath(TilePath, tileFileName, ".pak")); result != 0)
    {
        return std::unexpected(
            std::format("could not open the tile file {}.pak ({:#x})", tileFileName, static_cast<uint32_t>(result)));
    }

    // The rotated set is optional: the result isn't checked.
    auto tile90File = std::make_unique<MCPacketFile>();
    tile90File->Open(GamePath(Tile90Path, tileFileName, "90.pak"));
    return std::make_unique<MCTerrainTiles>(std::move(tileFile), std::move(tile90File),
                                            !MCIEquals("tiles", tileFileName));
}

MCTerrainTiles::MCTerrainTiles(std::unique_ptr<MCPacketFile> tileFile, std::unique_ptr<MCPacketFile> tile90File,
                               bool customTileSet)
    : _TileFile(std::move(tileFile)), _Tile90File(std::move(tile90File)), _CustomTileSet(customTileSet)
{
    _NumTiles = _TileFile->GetNumPackets();
    TileSetOffset = {0, _NumTiles / 2};
    // One slot more than the packets: ReadTile takes tileNum == numTiles (the original wrote past the table).
    _Tiles.resize(static_cast<size_t>(std::max(_NumTiles, 0)) + 1);
}

MCTerrainTiles::~MCTerrainTiles() = default;

auto MCTerrainTiles::PreloadSlots(std::string_view terrainName) const -> std::vector<int32_t>
{
    MCFile preloadFile;

    if (preloadFile.Open(GamePath(TerrainPath, terrainName, ".pre")) != 0)
    {
        return {};
    }

    std::vector<int32_t> slots;

    for (uint32_t count = preloadFile.FileSize() >> 2; count != 0; count--)
    {
        const int32_t tileNum = preloadFile.ReadLong();
        slots.push_back((_NumTiles >> 1) + tileNum);
    }

    return slots;
}

auto MCTerrainTiles::ReadTiles(std::span<const int32_t> slots) -> MCTileReads
{
    // The slots ReadTile would read, in the order listed: in the table or one past it, once, not cached or missing.
    std::vector<int32_t> wanted;
    std::vector<bool> listed(_Tiles.size(), false);

    for (const int32_t slot : slots)
    {
        if (slot < 0 || slot > _NumTiles || listed[static_cast<size_t>(slot)])
        {
            continue;
        }

        listed[static_cast<size_t>(slot)] = true;
        const MCTerrainTile& tile = _Tiles[static_cast<size_t>(slot)];

        if (!tile.Missing && tile.TileData().empty())
        {
            wanted.push_back(slot);
        }
    }

    // Each file's: [0] the rotated set's (the table's lower half), [1] the normal set's.
    const auto fileOf = [this](int32_t slot) { return slot < (_NumTiles >> 1) ? size_t{0} : size_t{1}; };
    const std::array<std::reference_wrapper<MCPacketFile>, 2> files{*_Tile90File, *_TileFile};
    std::array<std::vector<int32_t>, 2> fileSlots;

    for (const int32_t slot : wanted)
    {
        fileSlots[fileOf(slot)].push_back(slot);
    }

    const std::array<MCPacketBatch, 2> batches{files[0].get().ReadPackets(fileSlots[0]),
                                               files[1].get().ReadPackets(fileSlots[1])};
    MCTileReads read{.Tiles = wanted.size(), .Reads = batches[0].Reads + batches[1].Reads};
    // Each file's next result, and the last of its slots placed from it since its last ReadTile.
    std::array<size_t, 2> next{};
    std::array<std::optional<int32_t>, 2> placed;

    for (const int32_t slot : wanted)
    {
        const size_t file = fileOf(slot);
        const std::optional<std::vector<uint8_t>>& bytes = batches[file].Packets[next[file]++];
        MCTerrainTile& tile = _Tiles[static_cast<size_t>(slot)];

        if (bytes.has_value())
        {
            tile.Missing = bytes->empty();

            if (!bytes->empty())
            {
                tile.Data = MCRegisteredBlock(bytes->size(), MCDataKind::Shapes);
                std::ranges::copy(*bytes, tile.Data.Bytes().begin());
            }

            placed[file] = slot;
            continue;
        }

        // ReadTile's result can hang on the packet its file sought before (a packet stored other than raw, LZ or Nul
        // keeps that one's size): seek the one it would have.
        if (placed[file].has_value())
        {
            files[file].get().SeekPacket(*placed[file]);
            placed[file].reset();
        }

        ReadTile(slot);
        ++read.Reads;
    }

    return read;
}

auto MCTerrainTiles::Slot(int32_t tileNum) const -> MCOptionalRef<const MCTerrainTile>
{
    if (tileNum < 0 || tileNum > _NumTiles)
    {
        return std::nullopt;
    }

    return _Tiles[static_cast<size_t>(tileNum)];
}

auto MCTerrainTiles::Put(int32_t slot, std::span<const uint8_t> packet) -> bool
{
    if (slot < 0 || slot >= _NumTiles || packet.empty())
    {
        return false;
    }

    MCTerrainTile& tile = _Tiles[static_cast<size_t>(slot)];

    if (tile.Missing || !tile.TileData().empty())
    {
        return false;
    }

    tile.Data = MCRegisteredBlock(packet.size(), MCDataKind::Shapes);
    std::ranges::copy(packet, tile.Data.Bytes().begin());
    return true;
}

auto MCTerrainTiles::Lookup(int32_t tileNum, bool rotated) -> std::span<const uint8_t>
{
    if (tileNum < 0)
    {
        return {};
    }

    tileNum += TileSetOffset[rotated ? 1 : 0];

    if (tileNum >= _NumTiles)
    {
        return {};
    }

    const MCTerrainTile& tile = _Tiles[static_cast<size_t>(tileNum)];

    if (tile.Missing)
    {
        return {};
    }

    if (tile.TileData().empty())
    {
        return ReadTile(tileNum);
    }

    return tile.TileData();
}

auto MCTerrainTiles::ReadTile(int32_t tileNum) -> std::span<const uint8_t>
{
    // Faithful: tileNum == numTiles passes this test (one past the table).
    if (tileNum < 0 || tileNum > _NumTiles)
    {
        return {};
    }

    // The lower half of the table is the rotated set.
    MCPacketFile& file = tileNum < (_NumTiles >> 1) ? *_Tile90File : *_TileFile;
    MCTerrainTile& tile = _Tiles[static_cast<size_t>(tileNum)];
    // Original behaviour: a failed seek marked the tile missing, which the read below overwrote unless the packet size
    // it then found was 0.
    file.SeekPacket(tileNum);
    const int32_t size = file.GetPacketSize();

    if (size == 0)
    {
        tile.Missing = true;
        return {};
    }

    // The original flushed the cache when its heap was full; the port's never is.
    tile.Data = MCRegisteredBlock(static_cast<size_t>(size), MCDataKind::Shapes);
    file.ReadPacket(tileNum, tile.Data.Bytes());
    tile.Missing = false;
    return tile.TileData();
}

auto MCTerrainTiles::FilesHash() const -> uint64_t
{
    constexpr uint64_t FnvOffset = 0xcbf29ce484222325ull;
    constexpr uint64_t FnvPrime = 0x100000001b3ull;
    uint64_t hash = FnvOffset;
    const auto mix = [&hash](std::span<const uint8_t> bytes)
    {
        for (const uint8_t byte : bytes)
        {
            hash = (hash ^ byte) * FnvPrime;
        }
    };

    for (const std::unique_ptr<MCPacketFile>& file : {std::cref(_Tile90File), std::cref(_TileFile)})
    {
        if (file == nullptr || !file->IsOpen())
        {
            mix(std::span<const uint8_t>());
            continue;
        }

        // The length is cached as the file opens, so this reads nothing.
        const uint32_t length = file->GetLength();
        mix(std::span(reinterpret_cast<const uint8_t*>(&length), sizeof(length)));
        const std::span<const uint32_t> table = file->SeekTable();
        mix(std::span(reinterpret_cast<const uint8_t*>(table.data()), table.size_bytes()));
    }

    return hash;
}

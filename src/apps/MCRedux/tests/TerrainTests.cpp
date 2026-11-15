#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "camera/MCCamera.h"
#include "camera/MCViewWindow.h"
#include "object/MCGameObject.h"
#include "engine/MCByteFlag.h"
#include "fakes/MCMemoryFileSource.h"
#include "MCScratchFolder.h"
#include "fixtures/MCPakBytes.h"
#include "fixtures/MCTerrainGeometryScope.h"
#include "fixtures/MCTinyMap.h"
#include "lib/MCPacketFile.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCMissionGlobals.h"
#include "platform/MCFileSystem.h"
#include "platform/MCFrameLog.h"
#include "object/MCObjectBlockManager.h"
#include "object/MCObjectSystem.h"
#include "terrain/MCMapBlockManager.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCVertex.h"
#include "terrain/MCTerrain.h"
#include "terrain/MCTerrainTiles.h"
#include "terrain/MCTerrainWindow.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The tiny maps' metres between two vertices, and a height level.</summary>
    constexpr float MetersPerVertex = MCTinyMap::MetersPerTile;
    constexpr float MetersPerLevel = 16.0f;

    /// <summary>
    /// A tiny map of <paramref name="tiles"/> tiles a side with the terrain's whole geometry set for it (one block of
    /// that many vertices).
    /// </summary>
    struct TinyTerrain
    {
        explicit TinyTerrain(int32_t tiles) : map(tiles), geometry(tiles, 1, MetersPerVertex, MetersPerLevel) {}

        /// <summary>Sets the height level of map vertex (tile) (<paramref name="row"/>, <paramref name="col"/>).</summary>
        void SetLevel(int32_t row, int32_t col, uint32_t level)
        {
            MCMapTile& tile = GameMap().Map[row * GameMap().Width + col];
            tile.Cells = (tile.Cells & ~(0x3fu << 7)) | (level << 7);
        }

        /// <summary>The world point at <paramref name="x"/>, <paramref name="y"/> vertices from the map's top-left corner.</summary>
        static MCVector3D At(float x, float y)
        {
            return MCVector3D(MCTerrain::MapTopLeft3d100.X + x * MetersPerVertex,
                              MCTerrain::MapTopLeft3d100.Y - y * MetersPerVertex, 0.0f);
        }

        MCTinyMap map;
        MCTerrainGeometryScope geometry;
    };

    /// <summary>Writes the PAK <paramref name="name"/> of <paramref name="packets"/> to the memory source's folder.</summary>
    void WritePak(std::string_view name, const std::vector<std::vector<uint8_t>>& packets)
    {
        MCPacketFile pak;
        REQUIRE_EQ(pak.Create(name), NO_ERR);
        pak.Reserve(static_cast<int32_t>(packets.size()));

        for (size_t i = 0; i < packets.size(); ++i)
        {
            pak.WritePacket(static_cast<int32_t>(i), packets[i], MCPacketStorage::Raw);
        }
    }

    /// <summary>The tile path globals pointed at the memory source's root for the test's lifetime.</summary>
    struct TilePathScope
    {
        TilePathScope() : savedTilePath(std::exchange(TilePath, "")), savedTile90Path(std::exchange(Tile90Path, "")) {}

        ~TilePathScope()
        {
            TilePath = savedTilePath;
            Tile90Path = savedTile90Path;
        }

        std::string savedTilePath;
        std::string savedTile90Path;
    };

    /// <summary>The terrain path global pointed at the memory source's root for the test's lifetime.</summary>
    struct TerrainPathScope
    {
        TerrainPathScope() : savedTerrainPath(std::exchange(TerrainPath, "")) {}

        ~TerrainPathScope() { TerrainPath = savedTerrainPath; }

        std::string savedTerrainPath;
    };

    /// <summary>A .bdg file: the record count, then each record's block, vertex, kind and destroyed flag.</summary>
    std::vector<uint8_t> BdgFile(int32_t count, const std::vector<std::array<int32_t, 4>>& records)
    {
        std::vector<uint8_t> bytes(sizeof(int32_t) + records.size() * sizeof(std::array<int32_t, 4>));
        std::memcpy(bytes.data(), &count, sizeof(count));

        if (!records.empty())
        {
            std::memcpy(bytes.data() + sizeof(int32_t), records.data(), records.size() * sizeof(records[0]));
        }

        return bytes;
    }

    /// <summary>An .obj block packet holding <paramref name="objects"/>.</summary>
    std::vector<uint8_t> ObjectBlock(const std::vector<MCObjData>& objects)
    {
        std::vector<uint8_t> bytes(objects.size() * sizeof(MCObjData));

        if (!objects.empty())
        {
            std::memcpy(bytes.data(), objects.data(), bytes.size());
        }

        return bytes;
    }
}

TEST_CASE("terrain: the geometry follows the terrain file's sizes")
{
    // A retail-sized map: 6 x 6 blocks of 20 vertices, 128 m apart.
    MCTerrainGeometryScope geometry(20, 6, 128.0f, 16.0f);
    CHECK_EQ(MCTerrain::TotalBlocks, 36);
    CHECK_EQ(VerticesPerBlock, 400);
    CHECK_EQ(MCTerrain::MetersBlockSide, 2560.0f);
    CHECK_EQ(WorldUnitsMapSide, 15360.0f);
    CHECK_EQ(MCTerrain::VerticesMapSide, 60);
    CHECK_EQ(MCTerrain::OneOvermetersPerVertex, 1.0f / 128.0f);
    CHECK_EQ(MCTerrain::MetersPerVertexDivMapcellDim, 128.0f / 3.0f);
    // The world's origin is the map's centre: the top-left corner is half a map left and up.
    CHECK_EQ(MCTerrain::MapTopLeft3d100.X, -7680.0f);
    CHECK_EQ(MCTerrain::MapTopLeft3d100.Y, 7680.0f);
    // The corner projects to the screen with the 30-degree view: x = (x + y) cos 30, y = (x - y) sin 30.
    CHECK_EQ(MCTerrain::MapTopLeft2d100.X, 0.0f);
    CHECK(std::fabs(MCTerrain::MapTopLeft2d100.Y - -7680.0f) < 0.01f);
}

TEST_CASE("terrain: a map vertex is found in its block, and off the map in the off-map block")
{
    MCTerrainGeometryScope geometry(4, 2, 128.0f, 16.0f);
    // Two by two blocks of 4 x 4 vertices; each vertex tagged with its block * 100 + place, the off-map block 900+.
    std::vector<MCPrecompVertex> vertices(5 * 16);

    for (size_t i = 0; i < vertices.size(); ++i)
    {
        vertices[i].TextureData = static_cast<int16_t>((i / 16 == 4 ? 900 : (i / 16) * 100) + i % 16);
    }

    MCMapBlockManager blocks(vertices, 4);
    CHECK_EQ(blocks.MapVertexAt(0, 0).TextureData, int16_t{0});
    CHECK_EQ(blocks.MapVertexAt(1, 2).TextureData, int16_t{6});
    CHECK_EQ(blocks.MapVertexAt(2, 5).TextureData, int16_t{100 + 9});
    CHECK_EQ(blocks.MapVertexAt(5, 1).TextureData, int16_t{200 + 5});
    CHECK_EQ(blocks.MapVertexAt(7, 7).TextureData, int16_t{300 + 15});
    // Off the map: the off-map block, at the same place within a block.
    CHECK_EQ(blocks.MapVertexAt(-1, 0).TextureData, int16_t{900 + 12});
    CHECK_EQ(blocks.MapVertexAt(8, 9).TextureData, int16_t{900 + 1});
    CHECK_EQ(blocks.BlockStart(-1), blocks.BlockStart(4));
    CHECK_EQ(blocks.BlockStart(37), blocks.BlockStart(4));
    CHECK_EQ(blocks.MapVertexIndex(2, 5), blocks.BlockStart(1) + 9);

    // Tiles placed by the map's objects add to the stored numbers.
    blocks.SetTile(3, 2, 5);
    blocks.SetOverlayTile(3, 2, -3);
    CHECK_EQ(blocks.GetTile(3, 2), 300 + 2 + 5);
    CHECK_EQ(blocks.GetOverlayTile(3, 2), -3);
}

TEST_CASE("terrain: on the first turn the off-map ground is raised a level above the map's base")
{
    TinyTerrain terrain(4);
    GameMap().BaseElevation = 3;
    std::vector<MCPrecompVertex> vertices(2 * 16);
    MCMapBlockManager blocks(vertices, 1);
    blocks.RaiseOffMapBlock();

    for (int32_t i = 0; i < 16; ++i)
    {
        CHECK_EQ(blocks.Block(1)[static_cast<size_t>(i)].Elevation, uint8_t{4});
        CHECK_EQ(blocks.Block(0)[static_cast<size_t>(i)].Elevation, uint8_t{0});
    }
}

TEST_CASE("terrain: the elevation at a point lies on the face of its map tile")
{
    TinyTerrain terrain(8);
    GameMap().BaseElevation = 1;

    // Flat ground: the base plus the tile's level, everywhere on the tile.
    for (int32_t row = 0; row < 8; ++row)
    {
        for (int32_t col = 0; col < 8; ++col)
        {
            terrain.SetLevel(row, col, 2);
        }
    }

    CHECK_EQ(TerrainElevationAt(TinyTerrain::At(3.5f, 3.5f)), 3 * MetersPerLevel);
    CHECK_EQ(TerrainElevationAt(TinyTerrain::At(1.25f, 6.75f)), 3 * MetersPerLevel);

    // A slope from vertex (2, 2) up to (2, 3) (the next column): along the top edge the height rises evenly.
    terrain.SetLevel(2, 3, 6);
    CHECK_EQ(TerrainElevationAt(TinyTerrain::At(2.0f, 2.0f)), 3 * MetersPerLevel);
    CHECK(std::fabs(TerrainElevationAt(TinyTerrain::At(2.25f, 2.0f)) - 4 * MetersPerLevel) < 0.01f);
    CHECK(std::fabs(TerrainElevationAt(TinyTerrain::At(2.75f, 2.0f)) - 6 * MetersPerLevel) < 0.01f);
    // Along the left edge (towards row 3) it doesn't rise: that edge's corners are both level 2.
    CHECK(std::fabs(TerrainElevationAt(TinyTerrain::At(2.0f, 2.5f)) - 3 * MetersPerLevel) < 0.01f);
}

TEST_CASE("terrain: a bridge deck stands at its highest corner")
{
    TinyTerrain terrain(8);
    GameMap().BaseElevation = 0;

    for (int32_t row = 0; row < 8; ++row)
    {
        for (int32_t col = 0; col < 8; ++col)
        {
            terrain.SetLevel(row, col, 1);
        }
    }

    // Overlay types 0x25..0x28 are bridge decks: drawn flat at the highest of the tile's four corners...
    GameMap().Map[4 * 8 + 4].Overlay = 0x26;
    terrain.SetLevel(5, 5, 3);
    CHECK_EQ(TerrainElevationAt(TinyTerrain::At(4.5f, 4.5f)), 3 * MetersPerLevel);
    // ...and a level above the ground when the corners are level.
    GameMap().Map[1 * 8 + 1].Overlay = 0x25;
    CHECK_EQ(TerrainElevationAt(TinyTerrain::At(1.5f, 1.5f)), 2 * MetersPerLevel);
    // Other overlays are ground.
    GameMap().Map[1 * 8 + 1].Overlay = 0x24;
    CHECK_EQ(TerrainElevationAt(TinyTerrain::At(1.5f, 1.5f)), 1 * MetersPerLevel);
}

TEST_CASE("terrain: off the map the elevation is 0")
{
    TinyTerrain terrain(8);
    GameMap().BaseElevation = 4;
    CHECK_EQ(TerrainElevationAt(TinyTerrain::At(-0.5f, 3.0f)), 0.0f);
    CHECK_EQ(TerrainElevationAt(TinyTerrain::At(3.0f, 8.5f)), 0.0f);
    // The last row and column have no tile beyond them to make a face with.
    CHECK_EQ(TerrainElevationAt(TinyTerrain::At(7.5f, 3.0f)), 0.0f);
    CHECK(TerrainElevationAt(TinyTerrain::At(6.5f, 3.0f)) > 0.0f);
}

TEST_CASE("terrain: a looker marks the vertices around it as seen by its own side")
{
    TinyTerrain terrain(16);
    MCTestContextScope scope;
    auto land = std::make_unique<MCTerrain>();
    land->ISVisibleBits = std::make_unique<MCByteFlag>(16, 16, false);
    land->ClanVisibleBits = std::make_unique<MCByteFlag>(16, 16, false);
    MCTerrain& view = *land;
    scope.Context().SetTerrain(std::move(land));

    // The radius in vertices is the range in meters times WorldUnitsPerMeter, over the meters a vertex: at the
    // default 3.34 (no mission loaded), 80 m makes 2 vertices.
    view.MarkRadiusSeen(TinyTerrain::At(8.5f, 6.5f), MCVector3D(), 360.0f, 80.0f, 1);
    CHECK(view.ISVisibleBits->GetFlag(6, 8));
    CHECK(view.ISVisibleBits->GetFlag(6, 9));
    CHECK(view.ISVisibleBits->GetFlag(5, 8));
    CHECK(!view.ISVisibleBits->GetFlag(6, 11));
    CHECK(!view.ISVisibleBits->GetFlag(6, 12));
    CHECK(!view.ISVisibleBits->GetFlag(10, 8));
    // Who 1 is the Inner Sphere: the Clans saw nothing.
    CHECK(!view.ClanVisibleBits->GetFlag(6, 8));

    // At a mission's scale (5.01, as nearly all of them set it), 80 m makes 3 vertices.
    const float defaultScale = WorldUnitsPerMeter;
    WorldUnitsPerMeter = 5.01f;
    view.MarkRadiusSeen(TinyTerrain::At(8.5f, 6.5f), MCVector3D(), 360.0f, 80.0f, 1);
    WorldUnitsPerMeter = defaultScale;
    CHECK(view.ISVisibleBits->GetFlag(6, 11));
    CHECK(!view.ISVisibleBits->GetFlag(6, 12));
    CHECK(!view.ISVisibleBits->GetFlag(10, 8));

    // Anyone else marks the Clan bits; only full-circle looks count.
    view.MarkRadiusSeen(TinyTerrain::At(2.5f, 12.5f), MCVector3D(), 360.0f, 40.0f, 2);
    view.MarkRadiusSeen(TinyTerrain::At(12.5f, 2.5f), MCVector3D(), 90.0f, 40.0f, 2);
    CHECK(view.ClanVisibleBits->GetFlag(12, 2));
    CHECK(!view.ClanVisibleBits->GetFlag(2, 12));
    CHECK(!view.ISVisibleBits->GetFlag(12, 2));
}

TEST_CASE("terrain: the blocks drawn this frame have no limit, and no block (-1) always counts")
{
    MCTerrain terrain;
    // Block -1 (an object on no block) always counts, as the original's -1-ended list made it.
    CHECK(terrain.BlockUsed(-1));
    CHECK(!terrain.BlockUsed(3));

    // The original's list stopped at 324 blocks; the port's keeps them all.
    for (int32_t block = 0; block < 400; ++block)
    {
        terrain.MarkBlockUsed(block);
        terrain.MarkBlockUsed(block);
    }

    CHECK(terrain.BlockUsed(0));
    CHECK(terrain.BlockUsed(399));
    CHECK(!terrain.BlockUsed(400));
    terrain.ClearUsedBlocks();
    CHECK(!terrain.BlockUsed(399));
    CHECK(terrain.BlockUsed(-1));
}

TEST_CASE("terrain: a mine shows to its own side, the Clans' to the Clans or in god mode, and leaves a crater")
{
    // The overlay word: the Inner Sphere's mine state in bits 11..12, the Clans' in 13..14 (2 a mine laid, 3 one that
    // went off); bits 25..28 count the spread and don't show.
    constexpr auto innerSphere = [](uint32_t state) { return state << 11; };
    constexpr auto clan = [](uint32_t state) { return state << 13; };
    constexpr uint32_t spread = 0xfu << 25;

    MCMineView view = MCTerrainMineView(innerSphere(2) | spread, true, false);
    CHECK(view.InnerSphereMine);
    CHECK(!view.ClanMine);
    CHECK(!view.Crater);

    // Playing the Inner Sphere, the Clans' mines are hidden unless in god mode.
    view = MCTerrainMineView(clan(2), true, false);
    CHECK(!view.InnerSphereMine);
    CHECK(!view.ClanMine);
    view = MCTerrainMineView(clan(2), true, true);
    CHECK(view.ClanMine);
    // Playing the Clans, their own mines show and the Inner Sphere's don't.
    view = MCTerrainMineView(clan(2) | innerSphere(2), false, false);
    CHECK(view.ClanMine);
    CHECK(!view.InnerSphereMine);

    // A mine that went off leaves a crater, whoever laid it and whoever plays.
    CHECK(MCTerrainMineView(innerSphere(3), true, false).Crater);
    CHECK(MCTerrainMineView(clan(3), true, false).Crater);
    CHECK(MCTerrainMineView(innerSphere(3), false, false).Crater);
    CHECK(MCTerrainMineView(clan(2) | innerSphere(3), false, false).Crater);
    CHECK(!MCTerrainMineView(innerSphere(1) | clan(1), true, true).Crater);
    CHECK(!MCTerrainMineView(0, false, true).InnerSphereMine);
}

TEST_CASE("terrain: the mine tile follows the corners' heights above the lowest")
{
    // 81 mine tiles from 0xed1: the corners' levels above the lowest as base-3 digits, top-left first.
    CHECK_EQ(MCMineTileNumber(4, 4, 4, 4), 0xed1);
    CHECK_EQ(MCMineTileNumber(5, 4, 4, 4), 0xed1 + 27);
    CHECK_EQ(MCMineTileNumber(4, 4, 4, 5), 0xed1 + 1);
    CHECK_EQ(MCMineTileNumber(0, 1, 2, 1), 0xed1 + 9 + 2 * 3 + 1);
    CHECK_EQ(MCMineTileNumber(7, 9, 8, 7), 0xed1 + 2 * 9 + 1 * 3);
}

TEST_CASE("terrain: tiles are read from the tile PAKs once, and a missing one stays missing")
{
    MCTestContextScope scope;
    scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    TilePathScope paths;
    // Four tiles a set: the lower half of the table comes from the rotated PAK, the upper from the normal one.
    WritePak("ttest.pak", {{1}, {2}, {3, 3}, {}});
    WritePak("ttest90.pak", {{91}, {}, {93}, {94}});
    std::expected<std::unique_ptr<MCTerrainTiles>, std::string> made = MCTerrainTiles::Create("ttest");
    REQUIRE(made.has_value());
    MCTerrainTiles& tiles = **made;
    CHECK_EQ(tiles.NumTiles(), 4);
    CHECK(tiles.CustomTileSet());
    CHECK_EQ(tiles.TileSetOffset[1], 2);

    const std::span<const uint8_t> first = tiles.Lookup(0);
    REQUIRE(!first.empty());
    CHECK_EQ(first[0], uint8_t{91});
    // Read once: the second lookup gives the same cached data.
    CHECK(tiles.Lookup(0).data() == first.data());
    CHECK_EQ(tiles.Lookup(2).size(), size_t{2});
    // The rotated view's numbers start half way.
    CHECK_EQ(tiles.Lookup(0, true)[0], uint8_t{3});
    // Packet 1 of the rotated PAK is empty: missing, and not read again.
    CHECK(tiles.Lookup(1).empty());
    CHECK(tiles.Lookup(1).empty());
    CHECK(tiles.Lookup(-1).empty());
    CHECK(tiles.Lookup(4).empty());
    CHECK(tiles.Lookup(2, true).empty());
}

namespace
{
    /// <summary>
    /// Whether every slot of <paramref name="read"/>, and those just outside the table, is as <paramref name="expected"/>'s:
    /// read or not, missing or not, the same bytes.
    /// </summary>
    void CheckSameSlots(const MCTerrainTiles& read, const MCTerrainTiles& expected)
    {
        REQUIRE_EQ(read.NumTiles(), expected.NumTiles());

        for (int32_t slot = -1; slot <= expected.NumTiles() + 1; ++slot)
        {
            const MCOptionalRef<const MCTerrainTile> got = read.Slot(slot);
            const MCOptionalRef<const MCTerrainTile> want = expected.Slot(slot);
            REQUIRE_EQ(got.has_value(), want.has_value());

            if (want.has_value() && (got->get().Missing != want->get().Missing ||
                                     !std::ranges::equal(got->get().TileData(), want->get().TileData())))
            {
                FAIL_CHECK(std::format("slot {}: missing {} ({} bytes), expected missing {} ({} bytes)", slot,
                                       got->get().Missing, got->get().TileData().size(), want->get().Missing,
                                       want->get().TileData().size()));
            }
        }
    }

    /// <summary>
    /// ReadTile on each slot of <paramref name="slots"/> Lookup would read (in the table or one past it, not read and not
    /// missing), one at a time in the order listed.
    /// </summary>
    void ReadOneAtATime(MCTerrainTiles& tiles, std::span<const int32_t> slots)
    {
        for (const int32_t slot : slots)
        {
            const MCOptionalRef<const MCTerrainTile> tile = tiles.Slot(slot);

            if (tile.has_value() && !tile->get().Missing && tile->get().TileData().empty())
            {
                tiles.ReadTile(slot);
            }
        }
    }

    /// <summary>Slots <paramref name="first"/> to <paramref name="last"/>, either way round.</summary>
    std::vector<int32_t> SlotsFrom(int32_t first, int32_t last)
    {
        std::vector<int32_t> slots;

        for (int32_t slot = first; slot != last + (last >= first ? 1 : -1); slot += last >= first ? 1 : -1)
        {
            slots.push_back(slot);
        }

        return slots;
    }

    /// <summary>Where a test's tile files are: on disk (the memory source's scratch folder), or in memory.</summary>
    enum class MCPakPlace : uint8_t
    {
        Disk,
        Memory
    };

    /// <summary>Puts the file <paramref name="name"/> of <paramref name="bytes"/> where <paramref name="place"/> says.</summary>
    void PutFile(MCMemoryFileSource& files, MCPakPlace place, std::string_view name, const std::vector<uint8_t>& bytes)
    {
        if (place == MCPakPlace::Memory)
        {
            files.AddFile(name, bytes);
            return;
        }

        MCFile out;
        REQUIRE_EQ(out.Create(name), NO_ERR);
        REQUIRE_EQ(out.Write(bytes), static_cast<int32_t>(bytes.size()));
    }
}

/// <summary>
/// Port-only: tiles read together (MCTerrainTiles::ReadTiles, the load's) leave the cache as ReadTile leaves it, read
/// one at a time in the order listed, slot by slot: from both files, raw, LZ, empty and Nul packets; those the batch
/// leaves to ReadTile (a packet stored as Fwf, which takes the size of the packet its file sought before; an LZ packet
/// claiming more than its data could make, or failing to unpack to its size, its block as far as it went; a packet past
/// the file's end; one past the table) read by it, after the packet ReadTile would have sought last before. Slots listed
/// twice, negative or two past the table are passed over; a slot read or missing isn't read again. On disk and in memory.
/// </summary>
TEST_CASE("terrain: tiles read together leave the cache as ReadTile leaves it, one at a time in the order listed")
{
    using namespace MCPakBytes;
    const std::vector<uint8_t> data = Filled(4000, 50);
    const auto lzMost = static_cast<int32_t>((Lz(data).Bytes.size() - sizeof(int32_t)) * 4096);
    // Twelve slots: 0-5 the rotated PAK's packets, 6-11 the normal PAK's (its packets 0-5 never read), 12 one past.
    const std::vector<uint8_t> rotated =
        Pak({Lz(Filled(5000, 0)), As(MCPacketStorage::Fwf, Filled(300, 1)), Raw(Filled(2000, 2)),
             As(MCPacketStorage::Nul, {}), Lz(data, lzMost + 1), Lz(data, static_cast<int32_t>(data.size()) + 50)});
    std::vector<MCStored> normalPackets;

    for (uint32_t i = 0; i < 6; ++i)
    {
        normalPackets.push_back(Raw(Filled(10, 100 + i)));
    }

    normalPackets.push_back(Raw({}));
    normalPackets.push_back(Lz(Filled(8000, 7)));
    normalPackets.push_back(Raw(Filled(70000, 8)));
    normalPackets.push_back(Raw(Filled(100, 9)));
    normalPackets.push_back(As(MCPacketStorage::Fwf, Filled(200, 10)));
    normalPackets.push_back(MCStored{.Offset = 0x100000});
    const std::vector<uint8_t> normal = Pak(normalPackets);

    std::vector<int32_t> ascending = SlotsFrom(0, 12);
    ascending.insert(ascending.end(), {-1, 13, 14, 3, 0});
    const std::vector<std::vector<int32_t>> lists{ascending, SlotsFrom(12, 0), {1, 0}, {2, 1}, {10, 7, 11, 6, 8}};

    for (const MCPakPlace place : {MCPakPlace::Disk, MCPakPlace::Memory})
    {
        for (size_t list = 0; list < lists.size(); ++list)
        {
            MCTest::Scope scope(std::format("{}, list {}", place == MCPakPlace::Disk ? "on disk" : "in memory", list));
            MCTestContextScope context;
            MCMemoryFileSource& files = context.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
            TilePathScope paths;
            PutFile(files, place, "rtest90.pak", rotated);
            PutFile(files, place, "rtest.pak", normal);
            std::expected<std::unique_ptr<MCTerrainTiles>, std::string> one = MCTerrainTiles::Create("rtest");
            std::expected<std::unique_ptr<MCTerrainTiles>, std::string> together = MCTerrainTiles::Create("rtest");
            REQUIRE(one.has_value() && together.has_value());
            REQUIRE_EQ((*together)->NumTiles(), 12);
            ReadOneAtATime(**one, lists[list]);
            const MCTileReads read = (*together)->ReadTiles(lists[list]);
            CheckSameSlots(**together, **one);

            if (list == 0)
            {
                // Each file's batch one read (the rotated file's 0, 2, 4 and 5; the normal one's 6-9), and ReadTile's six
                // (1, 4, 5; 10, 11, 12).
                CHECK_EQ(read.Tiles, size_t{13});
                CHECK_EQ(read.Reads, size_t{8});
                // Read or missing: nothing read again, the same bytes kept.
                const std::span<const uint8_t> first = (*together)->Lookup(0);
                CHECK_EQ((*together)->ReadTiles(ascending).Tiles, size_t{0});
                CHECK((*together)->Lookup(0).data() == first.data());
            }
        }
    }
}

/// <summary>
/// Port-only: a tile set without its rotated file reads the table's lower half as missing whether read together or
/// one at a time, and its upper half as ever.
/// </summary>
TEST_CASE("terrain: tiles read together from a set without its rotated file are as ReadTile reads them")
{
    using namespace MCPakBytes;
    MCTestContextScope context;
    MCMemoryFileSource& files = context.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    TilePathScope paths;
    files.AddFile("ctest.pak", Pak({Raw(Filled(10, 0)), Raw(Filled(10, 1)), Lz(Filled(900, 2)), Raw(Filled(90, 3))}));
    std::expected<std::unique_ptr<MCTerrainTiles>, std::string> one = MCTerrainTiles::Create("ctest");
    std::expected<std::unique_ptr<MCTerrainTiles>, std::string> together = MCTerrainTiles::Create("ctest");
    REQUIRE(one.has_value() && together.has_value());
    const std::vector<int32_t> slots = SlotsFrom(0, 4);
    ReadOneAtATime(**one, slots);
    CHECK_EQ((*together)->ReadTiles(slots).Tiles, size_t{5});
    CheckSameSlots(**together, **one);
    CHECK((*together)->Slot(0)->get().Missing);
    CHECK_EQ((*together)->Lookup(2).size(), size_t{900});
}

/// <summary>
/// Port-only: every slot of the retail tile sets, the original's (TILES) and the expansion's (GTILES), read together
/// comes as ReadTile reads it alone, in a few reads (a file's packets lie in order, so its whole is about a read a
/// megabyte).
/// </summary>
TEST_CASE("game: every retail tile read together is as ReadTile reads it, in a read a megabyte")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    for (const std::string_view set : {"tiles", "gtiles"})
    {
        MCTest::Scope scope(set);
        std::expected<std::unique_ptr<MCTerrainTiles>, std::string> one = MCTerrainTiles::Create(set);
        std::expected<std::unique_ptr<MCTerrainTiles>, std::string> together = MCTerrainTiles::Create(set);
        REQUIRE(one.has_value() && together.has_value());
        REQUIRE((*together)->NumTiles() > 0);
        const std::vector<int32_t> slots = SlotsFrom(0, (*together)->NumTiles());
        ReadOneAtATime(**one, slots);
        const auto start = std::chrono::steady_clock::now();
        const MCTileReads read = (*together)->ReadTiles(slots);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        CheckSameSlots(**together, **one);
        CHECK_EQ(read.Tiles, slots.size());
        // The two files' bytes, at least half a megabyte a read (each up to a megabyte, a packet short of it at most),
        // and the one past the table's ReadTile.
        const uint64_t bytes = std::filesystem::file_size(MCFileSystem::Resolve(GamePath(TilePath, set, ".pak"))) +
                               std::filesystem::file_size(MCFileSystem::Resolve(GamePath(Tile90Path, set, "90.pak")));
        CHECK(read.Reads <= bytes / (MCDiskFile::RunMostBytes / 2) + 1);
        std::cout << std::format("  {}: {} tiles in {} reads, {:.1f} ms\n", set, read.Tiles, read.Reads, ms);
    }
}

/// <summary>
/// Port-only: the tiles a map uses (MCMapBlockManager::TilesUsed, which the load reads together) are every vertex's
/// terrain tile and overlay that isn't negative, once each, the off-map block's included.
/// </summary>
TEST_CASE("terrain: the tiles a map uses are every vertex's tile and overlay once, the off-map block's included")
{
    MCTerrainGeometryScope geometry(2, 1, MetersPerVertex, MetersPerLevel);
    const auto vertex = [](int16_t tile, int16_t overlay)
    { return MCPrecompVertex{.TextureData = tile, .OverlayData = overlay}; };
    // The map's one block, then the off-map block.
    const std::vector<MCPrecompVertex> vertices{vertex(7, -1),  vertex(3, 7),  vertex(3, 12), vertex(-1, -1),
                                                vertex(40, -1), vertex(7, 41), vertex(7, -5), vertex(7, -1)};
    const MCMapBlockManager blocks(vertices, 1);
    const std::vector<int32_t> expected{3, 7, 12, 40, 41};
    CHECK(blocks.TilesUsed() == expected);
}

/// <summary>
/// Port-only: a mission's load reads every tile its map uses together (its frame log's note says how many in how many
/// reads, a few dozen), so none is read one at a time as the ground is first drawn.
/// </summary>
TEST_CASE_ISOLATED("game: a mission's load reads every tile its map uses together, in a few reads")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCScratchFolder folder("mc_terrain_tests", "load-tiles");
    const std::filesystem::path log = folder.Path() / "load.framelog";
    REQUIRE(MCFrameLog::Open(log));
    REQUIRE(MCTestGame::StartMission(1));
    // The test game's frames don't move the log's on: its first frame, which the notes so far go to, then written.
    MCFrameLog::NextFrame();
    MCFrameLog::NextFrame();
    MCFrameLog::Close();
    const std::shared_ptr<MCTerrain> terrain = Terrain();
    REQUIRE(terrain != nullptr && terrain->MapBlocks != nullptr && terrain->Tiles != nullptr);
    size_t used = 0;

    for (const int32_t slot : terrain->MapBlocks->TilesUsed())
    {
        if (const MCOptionalRef<const MCTerrainTile> tile = terrain->Tiles->Slot(slot); tile.has_value())
        {
            ++used;
            CHECK(tile->get().Missing || !tile->get().TileData().empty());
        }
    }

    std::ifstream in(log);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    const size_t at = text.find("original tiles: ");
    REQUIRE(at != std::string::npos);
    size_t tiles = 0;
    size_t reads = 0;
    const int scanned = std::sscanf(text.c_str() + at, "original tiles: %zu in %zu reads", &tiles, &reads);
    REQUIRE_EQ(scanned, 2);
    CHECK(tiles >= used);
    CHECK(reads * 10 <= tiles);
    std::cout << "  " << text.substr(at, text.find('\n', at) - at) << "\n";
}

TEST_CASE("terrain: a map's .bdg or .obj placing objects off the map, or counting more than it holds, is refused")
{
    MCTestContextScope scope;
    MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    TerrainPathScope paths;
    // 2 x 2 blocks of 4 x 4 vertices: blocks 0-3, vertices 0-15.
    MCTerrainGeometryScope geometry(4, 2, 128.0f, 16.0f);
    scope.Context().SetObjectSystem(std::make_shared<MCObjectSystem>(nullptr));
    WritePak("tmap.obj", {ObjectBlock({})});

    // Whether the map is refused, and for the reason given.
    const auto refusedFor = [](std::string_view reason)
    {
        const auto made = MCObjectBlockManager::Create("tmap");
        return !made.has_value() && made.error().contains(reason);
    };

    files.AddFile("tmap.bdg", BdgFile(0, {}));
    CHECK(MCObjectBlockManager::Create("tmap").has_value());

    // The count past the records the file holds (once a huge allocation), or negative.
    for (const int32_t count : {2, 1000000, std::numeric_limits<int32_t>::max(), -1})
    {
        MCTest::Scope entry(std::format("count {}", count));
        files.AddFile("tmap.bdg", BdgFile(count, {{0, 0, 5, 0}}));
        CHECK(refusedFor("terrain objects"));
    }

    // A record's block or vertex off the map (once a write past the vertex counts).
    for (const std::array<int32_t, 4> record :
         {std::array<int32_t, 4>{4, 0, 5, 0}, {-1, 0, 5, 0}, {0, 16, 5, 0}, {0, -1, 5, 0}})
    {
        MCTest::Scope entry(std::format("block {} vertex {}", record[0], record[1]));
        files.AddFile("tmap.bdg", BdgFile(1, {record}));
        CHECK(refusedFor("off the map"));
    }

    // The same for an object in an .obj block.
    files.AddFile("tmap.bdg", BdgFile(0, {}));

    for (const auto [vertex, block] : {std::pair<int16_t, int16_t>{16, 0}, {0, 4}, {-1, 0}, {0, -1}})
    {
        MCTest::Scope entry(std::format("object block {} vertex {}", block, vertex));
        MCObjData object;
        object.ObjTypeNum = 1;
        object.VertexNumber = vertex;
        object.BlockNumber = block;
        WritePak("tmap.obj", {ObjectBlock({object})});
        CHECK(refusedFor("off the map"));
    }
}

TEST_CASE("tacmap: world points and map pixels round trip at every zoom and scroll")
{
    MCTacmapProjection map;
    map.MapWidth = 256;
    map.MapHeight = 256;
    // A 64-vertex map's diagonal over the map area's 260 pixels at 1x.
    const float diagonal = std::sqrt(2.0f * 64.0f * 64.0f) * MetersPerVertex;

    for (const int32_t zoom : {1, 2, 4, 8})
    {
        map.Zoom = zoom;
        map.MetersPerPixel = (diagonal / 260.0f) / static_cast<float>(zoom);

        for (const auto& [scrollX, scrollY] : {std::pair{0, 0}, std::pair{20, -12}})
        {
            MCTest::Scope where(std::format("zoom {} scroll ({}, {})", zoom, scrollX, scrollY));
            map.ScrollX = scrollX;
            map.ScrollY = scrollY;

            for (const MCVector3D& point : {MCVector3D(0.0f, 0.0f, 0.0f), MCVector3D(1200.0f, -340.0f, 0.0f),
                                            MCVector3D(-2500.0f, 2800.0f, 0.0f)})
            {
                for (const bool scrolled : {false, true})
                {
                    MCVector3D pixel = point;
                    map.WorldToMap(pixel, scrolled);
                    MCVector3D back = pixel;
                    map.MapToWorld(back, scrolled);
                    CHECK(std::fabs(back.X - point.X) < 0.05f);
                    CHECK(std::fabs(back.Y - point.Y) < 0.05f);
                    CHECK_EQ(back.Z, 0.0f);
                }
            }
        }
    }

    // Unscrolled at 1x, the world's centre is the 130-pixel map's centre; on the MFD it is the map area's (71, 99).
    map.Zoom = 1;
    map.MetersPerPixel = diagonal / 260.0f;
    map.ScrollX = 0;
    map.ScrollY = 0;
    MCVector3D centre(0.0f, 0.0f, 0.0f);
    map.WorldToMap(centre, false);
    CHECK_EQ(centre.X, 65.0f);
    CHECK_EQ(centre.Y, 65.0f);
    centre = MCVector3D(0.0f, 0.0f, 0.0f);
    map.WorldToMap(centre, true);
    CHECK_EQ(centre.X, 71.0f);
    CHECK_EQ(centre.Y, 99.0f);
    // North-east is straight up and east is up and right: the map is turned 45 degrees.
    MCVector3D northEast(1000.0f, 1000.0f, 0.0f);
    map.WorldToMap(northEast, false);
    CHECK(std::fabs(northEast.Y - 65.0f) < 0.001f);
    CHECK(northEast.X > 65.0f);
    // On the MFD, at 2x the same point lies twice as far from the centre; on the whole map it stays put.
    MCVector3D east(1000.0f, 0.0f, 0.0f);
    MCVector3D eastOnMap = east;
    map.WorldToMap(east, true);
    map.WorldToMap(eastOnMap, false);
    map.Zoom = 2;
    map.MetersPerPixel = (diagonal / 260.0f) / 2.0f;
    MCVector3D eastZoomed(1000.0f, 0.0f, 0.0f);
    MCVector3D eastZoomedOnMap = eastZoomed;
    map.WorldToMap(eastZoomed, true);
    map.WorldToMap(eastZoomedOnMap, false);
    CHECK(std::fabs((eastZoomed.X - 71.0f) - 2.0f * (east.X - 71.0f)) < 0.001f);
    CHECK(std::fabs(eastZoomedOnMap.X - eastOnMap.X) < 0.001f);
}

TEST_CASE("camera: a ground point projected to the view comes back through InverseProject at every zoom")
{
    // Two by two blocks of the retail maps' 20 vertices, all at height 0.
    MCTerrainGeometryScope geometry(20, 2, MetersPerVertex, MetersPerLevel);
    std::vector<MCPrecompVertex> vertices(5 * 400);
    const auto blocks = std::make_shared<MCMapBlockManager>(vertices, 4);

    // The view sizes of the zoom range: 480 lines closest, 1080, and 2160 furthest, at 4:3.
    for (const float height : {480.0f, 1080.0f, 2160.0f})
    {
        MCTest::Scope where(std::format("{} lines", height));
        MCCamera camera;
        camera.SetViewSize(height * 4.0f / 3.0f, height);
        camera.Position = MCVector3D(0.0f, 0.0f, 0.0f);
        camera.ScreenUL = MCTerrain::ProjectTerrain(camera.Position);
        const auto shared = std::make_shared<MCTerrainWindow>(0, 30);
        MCTerrainWindow& window = *shared;
        blocks->Update(camera.Position, window);
        blocks->BuildWindow(window);

        // The grid as the terrain's render projects it (flat ground).
        for (int32_t i = 0; i < 30 * 30; ++i)
        {
            const MCVector3D world(window.TopLeftX + static_cast<float>(i % 30) * MetersPerVertex,
                                   window.TopLeftY - static_cast<float>(i / 30) * MetersPerVertex, 0.0f);
            const MCVector2D screen = camera.Project(world);
            window.Vertices[static_cast<size_t>(i)].Px = static_cast<int32_t>(std::floor(screen.X));
            window.Vertices[static_cast<size_t>(i)].Py = static_cast<int32_t>(std::floor(screen.Y));
        }

        camera.TerrainWindow = shared;

        for (const MCVector3D point : {MCVector3D(0.0f, 0.0f, 0.0f), MCVector3D(300.0f, 150.0f, 0.0f),
                                       MCVector3D(-400.0f, -230.0f, 0.0f), MCVector3D(510.0f, -620.0f, 0.0f)})
        {
            const MCVector2D screen = camera.Project(point);
            MCVector3D back;
            camera.InverseProject(screen, back);
            // Within the pixel the vertices were rounded to (about a metre and a half on the ground).
            CHECK(std::fabs(back.X - point.X) < 2.5f);
            CHECK(std::fabs(back.Y - point.Y) < 2.5f);
            CHECK_EQ(back.Z, 0.0f);
        }

        // The camera's position is the view's centre.
        const MCVector2D middle = camera.Project(camera.Position);
        CHECK_EQ(middle.X, camera.HalfWidth);
        CHECK_EQ(middle.Y, camera.HalfHeight);
    }
}

TEST_CASE("tacmap: the salvage list keeps every object, and drops one only once")
{
    std::vector<std::shared_ptr<MCGameObject>> objects;

    for (size_t i = 0; i < 150; ++i)
    {
        objects.push_back(std::make_shared<MCGameObject>());
    }

    const auto object = [&objects](size_t i) { return objects[i]; };
    MCSalvageList salvage;

    // The original's list held 100 and then hid the 100th (OB-052); the port's keeps all.
    for (size_t i = 0; i < objects.size(); ++i)
    {
        CHECK(salvage.Add(object(i)));
    }

    CHECK(!salvage.Add(object(7)));
    CHECK_EQ(salvage.size(), size_t{150});
    CHECK(salvage[149] == object(149));

    // Removing an object twice removes it once (OB-051: the original dropped its last entry instead).
    CHECK(salvage.Remove(*object(149)));
    CHECK(!salvage.Remove(*object(149)));
    CHECK(salvage.Remove(*object(3)));
    CHECK(!salvage.Remove(*object(3)));
    CHECK_EQ(salvage.size(), size_t{148});
    CHECK(salvage[3] == object(4));
    CHECK(salvage[147] == object(148));

    // An object that left the world reads as none (OB-170) and stays listed until the list drops it.
    object(11)->MarkRemoved();
    CHECK(salvage[10] == nullptr);
    CHECK_EQ(salvage.Pins().size(), size_t{148});
    salvage.RemoveIf([](const std::shared_ptr<MCGameObject>& obj) { return obj == nullptr; });
    CHECK_EQ(salvage.size(), size_t{147});
}

/// <summary>
/// Every retail terrain tile passes the drawer's check (issue #25: its row table and spans inside its data and width),
/// so the check refuses only damaged tiles. The tile files also hold the overlays, fast shapes tagged "DNAH".
/// </summary>
TEST_CASE("game: every retail terrain tile fits its row table")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::OpenFastFiles();
    constexpr std::array<uint8_t, 4> fastShapeTag = {'D', 'N', 'A', 'H'};
    int32_t tiles = 0;

    for (const std::string_view name :
         {"data\\tiles\\tiles.pak", "data\\tiles\\tiles90.pak", "data\\tiles\\gtiles.pak", "data\\tiles\\gtiles90.pak"})
    {
        MCTest::Scope scope(name);
        MCPacketFile file;
        REQUIRE_EQ(file.Open(name), 0);

        for (int32_t packet = 0; packet < file.GetNumPackets(); packet++)
        {
            if (file.SeekPacket(packet) != 0 || file.GetPacketSize() < 4)
            {
                continue;
            }

            std::vector<uint8_t> data(static_cast<size_t>(file.GetPacketSize()));
            file.ReadPacket(packet, data);

            if (std::ranges::equal(std::span(data).first(4), fastShapeTag))
            {
                continue;
            }

            CHECK_EQ(VfxTileFits(data) ? -1 : packet, -1);
            tiles++;
        }
    }

    CHECK(tiles > 1000);
}

/// <summary>
/// Mission 1 at the closest zoom, the start's at 1440p and the furthest, the view at the middle and towards each of the
/// map's edges: the quads the terrain draws (MCTerrainWindow::QuadsOnPane) are the smallest rectangle of the grid that
/// holds every quad with a corner on the pane, so every quad outside it has its four corners clipped.
/// </summary>
TEST_CASE_ISOLATED("game: the terrain draws the grid's quads with a corner on the pane, and passes over no others")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const MCVector3D middle = Eye()->GetPosition();
    const float far =
        static_cast<float>(MCTerrain::VerticesBlockSide * MCTerrain::BlocksMapSide) * MCTerrain::MetersPerVertex * 2.0f;
    const std::array<std::array<float, 2>, 5> directions{{{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

    for (const float zoom : {480.0f, 1440.0f, 2160.0f})
    {
        MCFixedZoomHeight = zoom;

        for (const auto& direction : directions)
        {
            MCTest::Scope scope(std::format("{} lines, towards ({}, {})", zoom, direction[0], direction[1]));
            MCVector3D position = middle;
            position.X += direction[0] * far;
            position.Y += direction[1] * far;
            Eye()->SetPosition(position);
            MCTestGame::RunFrame(1.0f / 15.0f);
            MCTestGame::RunFrame(1.0f / 15.0f);

            const std::shared_ptr<MCTerrainWindow> window = Eye()->TerrainWindow.lock();
            REQUIRE(window != nullptr);
            const int32_t quadsSide = window->Side() - 1;
            std::optional<MCGridQuads> expected;
            int32_t onPane = 0;

            for (int32_t row = 0; row < quadsSide; row++)
            {
                for (int32_t col = 0; col < quadsSide; col++)
                {
                    const MCTerrainBlock& quad = window->Blocks[static_cast<size_t>(row * quadsSide + col)];

                    if (std::ranges::all_of(quad.Vertices,
                                            [&](uint32_t corner) { return window->Vertices[corner].Clipped; }))
                    {
                        continue;
                    }

                    onPane++;
                    expected = expected.has_value()
                                   ? MCGridQuads{.FirstRow = std::min(expected->FirstRow, row),
                                                 .FirstCol = std::min(expected->FirstCol, col),
                                                 .LastRow = std::max(expected->LastRow, row),
                                                 .LastCol = std::max(expected->LastCol, col)}
                                   : MCGridQuads{.FirstRow = row, .FirstCol = col, .LastRow = row, .LastCol = col};
                }
            }

            CHECK(onPane > 0);
            const std::optional<MCGridQuads> drawn = window->QuadsOnPane();
            REQUIRE(drawn.has_value() == expected.has_value());

            if (drawn.has_value())
            {
                CHECK_EQ(drawn->FirstRow, expected->FirstRow);
                CHECK_EQ(drawn->FirstCol, expected->FirstCol);
                CHECK_EQ(drawn->LastRow, expected->LastRow);
                CHECK_EQ(drawn->LastCol, expected->LastCol);
                // The grid is made for the furthest zoom on the largest screen: nearer in, most of it is off the pane.
                CHECK(std::cmp_less((drawn->LastRow - drawn->FirstRow + 1) * (drawn->LastCol - drawn->FirstCol + 1),
                                    window->Blocks.size()));
            }
        }
    }
}

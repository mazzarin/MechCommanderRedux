#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "ai/MCMoveGeometry.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "camera/MCCamera.h"
#include "mission/MCScenario.h"
#include "object/MCGroundVehicle.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCMechStops.h"
#include "sprite/MCPackRecords.h"
#include "sprite/MCSpriteTree.h"
#include "terrain/MCGroundMesh.h"
#include "terrain/MCGroundNames.h"
#include "terrain/MCGroundNamesKnown.h"
#include "terrain/MCMapBlockManager.h"
#include "terrain/MCTerrain.h"
#include "terrain/MCTerrainTiles.h"

// Port-only (HD art, PLAN.md's first-use item 6, Part B): the names the HD pack keeps the original's tile sets' ground
// tiles and overlays under, held in the table the program carries (terrain/MCGroundNamesKnown.h), which
// --ground-names-table writes.

namespace
{
    /// <summary>The original's tile sets (data/tiles/&lt;name&gt;.pak and &lt;name&gt;90.pak).</summary>
    constexpr std::array<std::string_view, 2> ShippedTileSets = {"tiles", "gtiles"};

    /// <summary>The C++ name of tile set <paramref name="name"/>'s rows in the table.</summary>
    auto RowsName(std::string_view name, std::string_view part) -> std::string
    {
        return std::format("MCGroundNames_{}_{}", name, part);
    }

    /// <summary>A row of the table, as the source writes it.</summary>
    auto RowText(const MCGroundNameRow& row) -> std::string
    {
        return std::format("    {{0x{:016x}, {}, {}, {}}},\n", row.Key, row.Slot, row.Width, row.Height);
    }

    /// <summary>Rows <paramref name="rows"/> as the table's array <paramref name="arrayName"/> of <paramref name="type"/>.</summary>
    auto ArrayText(std::string_view type, const std::string& arrayName, const std::string& rows) -> std::string
    {
        return std::format("inline constexpr {} {}[] = {{\n{}}};\n\n", type, arrayName, rows);
    }

    /// <summary>Bytes <paramref name="bytes"/> as a table array's rows, sixteen a line.</summary>
    auto BytesText(std::span<const uint8_t> bytes) -> std::string
    {
        constexpr size_t PerLine = 16;
        std::string text;

        for (size_t i = 0; i < bytes.size(); ++i)
        {
            text += std::format("{}0x{:02x},{}", i % PerLine == 0 ? "    " : "", bytes[i],
                                i % PerLine == PerLine - 1 || i + 1 == bytes.size() ? "\n" : " ");
        }

        return text;
    }

    /// <summary>The names <paramref name="asks"/> ask for, in order: each one's key, or its tile's bytes' key.</summary>
    auto NamesOf(std::span<const MCHdArt::GroundAhead> asks) -> std::vector<MCGroundName>
    {
        std::vector<MCGroundName> names;
        names.reserve(asks.size());

        for (const MCHdArt::GroundAhead& ask : asks)
        {
            names.push_back(MCGroundName{ask.Key.value_or(MCHdArt::TerrainTileKey(ask.Tile)), ask.Width, ask.Height});
        }

        return names;
    }

    /// <summary>Tile set <paramref name="name"/> opened as a mission's terrain opens it.</summary>
    auto OpenTileSet(std::string_view name) -> std::unique_ptr<MCTerrainTiles>
    {
        std::expected<std::unique_ptr<MCTerrainTiles>, std::string> tiles = MCTerrainTiles::Create(name);

        if (!tiles.has_value())
        {
            FAIL(std::format("tile set {} didn't open: {}", name, tiles.error()));
        }

        return std::move(*tiles);
    }

    /// <summary>A ground tile's header (VFX's tile layout, vfx/MCVfxTile.cpp), before its rows' table.</summary>
    constexpr size_t TileHeaderBytes = 4;

    /// <summary>Where row <paramref name="row"/> of ground tile <paramref name="tile"/> starts.</summary>
    auto TileRowAt(std::span<const uint8_t> tile, int32_t row) -> size_t
    {
        uint32_t offset = 0;
        std::memcpy(&offset, tile.data() + TileHeaderBytes + static_cast<size_t>(row) * sizeof(offset), sizeof(offset));
        return offset;
    }

    /// <summary>
    /// Why ground tile <paramref name="packet"/> (named <paramref name="name"/>) wasn't rebuilt from HD frame indices
    /// <paramref name="indices"/>: its pixels the indices don't hold, or its rows laid out otherwise than the rebuild
    /// lays them.
    /// </summary>
    auto WhyNotRebuilt(std::span<const uint8_t> packet, const MCGroundName& name, std::span<const uint8_t> indices)
        -> std::string
    {
        size_t differ = 0;
        size_t bareRows = 0;
        size_t emptyRows = 0;

        for (int32_t row = 0; row < name.Height; ++row)
        {
            const size_t start = TileRowAt(packet, row);
            const size_t end = TileRowAt(packet, row + 1);
            bareRows += end == start + 1 ? 1 : 0;
            emptyRows += end == start ? 1 : 0;

            for (int32_t x = 0; x < name.Width; ++x)
            {
                const size_t at = (static_cast<size_t>(row) * name.Width + x) * MCHdArt::IndexBytes;
                const bool drawn =
                    end > start + 1 && x >= packet[start] && x < packet[start] + static_cast<int32_t>(end - start - 1);
                const bool hdDrawn = indices[at + 1] != 0;
                differ += drawn != hdDrawn || (drawn && indices[at] != packet[start + 1 + (x - packet[start])]) ? 1 : 0;
            }
        }

        if (differ != 0)
        {
            return "its HD frame's indices aren't its pixels";
        }

        if (bareRows != 0)
        {
            return "a row of its first column alone";
        }

        return std::format("laid out otherwise ({} empty rows, its first row at {} of {})", emptyRows,
                           TileRowAt(packet, 0),
                           TileHeaderBytes + (static_cast<size_t>(name.Height) + 2) * sizeof(uint32_t));
    }

    /// <summary>An overlay packet's shape 0 (after its one-shape table) and its row table's place in the shape.</summary>
    constexpr size_t OverlayShapeAt = 16;
    constexpr size_t OverlayRowTableAt = 12;
    /// <summary>A row's code from this up is that many literals less it; below it a run of the byte after it.</summary>
    constexpr uint8_t OverlayLiterals = 0x80;
    constexpr uint8_t OverlaySeeThrough = 0xff;

    /// <summary>
    /// The index picture overlay <paramref name="packet"/>'s own pixels make (R8G8, as MCHdArt::Frame::Indices: its
    /// frame a column wider than its rows, each drawn pixel's green HasIndex): what the original draws, which an HD
    /// frame's indices spread past. Empty where a row's codes run past the packet.
    /// </summary>
    auto OverlayOwnIndices(std::span<const uint8_t> packet, const MCGroundName& name) -> std::vector<uint8_t>
    {
        const std::span<const uint8_t> shape = packet.subspan(OverlayShapeAt);
        std::vector<uint8_t> indices(static_cast<size_t>(name.Width) * name.Height * MCHdArt::IndexBytes);

        for (int32_t row = 0; row < name.Height; ++row)
        {
            uint16_t at = 0;
            std::memcpy(&at, shape.data() + OverlayRowTableAt + static_cast<size_t>(row) * sizeof(at), sizeof(at));
            const size_t line = static_cast<size_t>(row) * name.Width;

            for (int32_t x = 0; x < name.Width - 1;)
            {
                if (at + size_t{1} >= shape.size())
                {
                    return {};
                }

                const uint8_t code = shape[at++];
                const bool literals = code >= OverlayLiterals;
                const int32_t count = literals ? code - OverlayLiterals : code;

                for (int32_t n = 0; n < count; ++n, ++x)
                {
                    const uint8_t index = literals ? shape[at++] : shape[at];

                    if (at > shape.size() || x >= name.Width - 1)
                    {
                        return {};
                    }

                    if (literals || index != OverlaySeeThrough)
                    {
                        indices[(line + x) * MCHdArt::IndexBytes] = index;
                        indices[(line + x) * MCHdArt::IndexBytes + 1] = MCHdArt::HasIndex;
                    }
                }

                at += literals ? 0 : 1;
            }
        }

        return indices;
    }

    /// <summary>
    /// The mine tiles (a laid mine's, the crater, to <paramref name="last"/>: a cell whose corner stands more than two
    /// levels over its lowest picks a slot past the crater) a draw or a read has looked at: read, or found missing.
    /// </summary>
    auto MineTilesTouched(const MCTerrain& terrain, int32_t last = MCMineCraterTile) -> std::vector<int32_t>
    {
        std::vector<int32_t> seen;

        for (int32_t slot = MCMineTileNumber(0, 0, 0, 0); slot <= last; ++slot)
        {
            const MCOptionalRef<const MCTerrainTile> tile = terrain.Tiles->Slot(slot);

            if (tile.has_value() && (tile->get().Missing || !tile->get().TileData().empty()))
            {
                seen.push_back(slot);
            }
        }

        return seen;
    }

    /// <summary>Why overlay <paramref name="own"/>'s pixels weren't rebuilt from its HD frame's <paramref name="hd"/>.</summary>
    auto WhyOverlayNotRebuilt(std::span<const uint8_t> own, std::span<const uint8_t> hd) -> std::string
    {
        if (own.size() != hd.size())
        {
            return "an overlay whose HD frame's indices are another size";
        }

        size_t spread = 0;

        for (size_t at = 0; at < own.size(); at += MCHdArt::IndexBytes)
        {
            const bool drawn = own[at + 1] != 0;

            if (drawn && (hd[at + 1] == 0 || hd[at] != own[at]))
            {
                return "an overlay whose HD frame's indices differ where the original draws";
            }

            spread += !drawn && hd[at + 1] != 0 ? 1 : 0;
        }

        return spread == 0 ? "an overlay not rebuilt, its HD frame's indices the original's"
                           : "an overlay whose HD frame's indices spread past the original's pixels, marked as drawn";
    }
} // namespace

namespace
{
    /// <summary>Whether sets <paramref name="a"/> and <paramref name="b"/> hold the same rows, tips and spots.</summary>
    auto SameSet(const MCKnownGroundNames& a, const MCKnownGroundNames& b) -> bool
    {
        return a.FilesHash == b.FilesHash && a.Slots == b.Slots && std::ranges::equal(a.Tiles, b.Tiles) &&
               std::ranges::equal(a.Overlays, b.Overlays) && std::ranges::equal(a.Shapes, b.Shapes) &&
               std::ranges::equal(a.Tips, b.Tips) && std::ranges::equal(a.Spots, b.Spots);
    }

    /// <summary>Every shipped tile set worked out from its art (MCWorkOutGroundSet), as the records hold them.</summary>
    auto WorkedOutSets() -> std::vector<MCGroundSet>
    {
        std::vector<MCGroundSet> sets;

        for (const std::string_view setName : ShippedTileSets)
        {
            const std::unique_ptr<MCTerrainTiles> tiles = OpenTileSet(setName);
            std::expected<MCGroundSet, std::string> set = MCWorkOutGroundSet(*tiles);

            if (!set.has_value())
            {
                FAIL(std::format("tile set {}: {}", setName, set.error()));
            }

            sets.push_back(std::move(*set));
        }

        return sets;
    }

    /// <summary>Views of <paramref name="sets"/>, for the records' writers.</summary>
    auto ViewsOf(std::span<const MCGroundSet> sets) -> std::vector<MCKnownGroundNames>
    {
        std::vector<MCKnownGroundNames> views;

        for (const MCGroundSet& set : sets)
        {
            views.push_back(set.View());
        }

        return views;
    }
} // namespace

/// <summary>
/// The version 2 HD pack's records 2 and 3 (sprite/MCPackRecords.h): each shipped tile set worked out from its art is
/// the program's table's, and written as the ground table and the overlay spots it reads back the same; either record
/// alone gives its half; a damaged one is refused, saying how.
/// </summary>
TEST_CASE_ISOLATED("ground names: the pack's ground table and overlay spots hold the tile sets as the program's table")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::vector<MCGroundSet> sets = WorkedOutSets();
    const std::vector<MCKnownGroundNames> views = ViewsOf(sets);

    for (const std::string_view setName : ShippedTileSets)
    {
        MCTest::Scope scope(setName);
        const std::unique_ptr<MCTerrainTiles> tiles = OpenTileSet(setName);
        const MCOptionalRef<const MCKnownGroundNames> known = MCKnownGroundNamesOf(*tiles);
        REQUIRE(known.has_value());
        const auto worked = std::ranges::find(views, tiles->FilesHash(), &MCKnownGroundNames::FilesHash);
        REQUIRE(worked != views.end());
        CHECK(SameSet(*worked, known->get()));
    }

    const std::vector<uint8_t> ground = MCPackRecords::GroundTable(views);
    const std::vector<uint8_t> spots = MCPackRecords::OverlaySpots(views);
    const std::expected<std::vector<MCGroundSet>, std::string> read = MCPackRecords::ReadGround(ground, spots);
    REQUIRE(read.has_value());
    REQUIRE_EQ(read->size(), sets.size());

    for (size_t i = 0; i < sets.size(); ++i)
    {
        CHECK(SameSet((*read)[i].View(), views[i]));
    }

    const std::expected<std::vector<MCGroundSet>, std::string> tilesOnly = MCPackRecords::ReadGround(ground, {});
    REQUIRE(tilesOnly.has_value());
    CHECK(std::ranges::equal(tilesOnly->front().Tiles, sets.front().Tiles));
    CHECK(tilesOnly->front().Overlays.empty());

    for (const std::vector<uint8_t>& whole : {ground, spots})
    {
        std::vector<uint8_t> damaged = whole;
        damaged.pop_back();
        CHECK(!MCPackRecords::ReadGround(damaged, {}).has_value());
        damaged = whole;
        damaged.push_back(0);
        CHECK(!MCPackRecords::ReadGround({}, damaged).has_value());
    }

    std::vector<uint8_t> otherVersion = ground;
    otherVersion[0] = static_cast<uint8_t>(MCPackRecords::Version + 1);
    CHECK(!MCPackRecords::ReadGround(otherVersion, spots).has_value());
}

/// <summary>
/// Tool: <c>--pack-records &lt;folder&gt;</c> writes the version 2 HD pack's records the game owns (SPEC-pack-v2.md's
/// "The facts"), one file a record holding its data as it goes in the archive, under the archive's path below the pack:
/// <c>terrain.hda/2.rec</c> (the ground table), <c>overlays.hda/3.rec</c> (the overlay spots) and each
/// <c>mechs/legs90/&lt;n&gt;.hda/4.rec</c> (the stops key of the trees whose legs are mech <c>n</c>'s).
/// </summary>
TEST_CASE_ISOLATED("tool: --pack-records writes the HD pack's ground table, overlay spots and stops keys")
{
    const std::optional<std::string_view> out = MCTest::Option("pack-records");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::filesystem::path folder{*out};
    const auto write =
        [&](const std::filesystem::path& archive, MCPackRecords::Type type, std::span<const uint8_t> data)
    {
        const std::filesystem::path path = folder / archive / std::format("{}.rec", static_cast<uint16_t>(type));
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file{path, std::ios::binary};
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        REQUIRE(file.good());
        std::cout << std::format("{}: {} bytes\n", path.lexically_relative(folder).generic_string(), data.size());
    };

    const std::vector<MCGroundSet> sets = WorkedOutSets();
    const std::vector<MCKnownGroundNames> views = ViewsOf(sets);
    write("terrain.hda", MCPackRecords::Type::GroundTable, MCPackRecords::GroundTable(views));
    write("overlays.hda", MCPackRecords::Type::OverlaySpots, MCPackRecords::OverlaySpots(views));

    // Each tree's key in the legs archive of its legs (mech n's legs90 packet: mechs/legs90/<n>.hda).
    std::map<uint32_t, std::vector<MCStopsKey>> byLegs;

    for (const std::shared_ptr<MCSpriteTree>& tree : MCTestGame::EveryMechTree())
    {
        REQUIRE(tree != nullptr);
        MCTest::Scope scope(tree->Name);
        std::optional<MCStopsKey> key = MCStopsKeyOf(*tree);
        REQUIRE(key.has_value());
        key->ArtHash = MCStopsArtHash(*tree);
        REQUIRE(MCFindKnownStops(key->ArtHash).has_value());
        const uint32_t legs = tree->FileNumbers[MCMechPart::Legs];
        REQUIRE(legs != NoFileNumber);
        byLegs[legs].push_back(*key);
    }

    for (const auto& [legs, keys] : byLegs)
    {
        write(std::filesystem::path("mechs") / "legs90" / std::format("{}.hda", legs), MCPackRecords::Type::StopsKey,
              MCPackRecords::StopsKey(keys));
    }
}

/// <summary>
/// Tool: <c>--ground-names-table &lt;file&gt;</c> works out every slot's names in each of the original's tile sets from
/// its tiles (MCWorkOutGroundNames) and writes them as terrain/MCGroundNamesKnown.h, the table the program carries.
/// </summary>
TEST_CASE_ISOLATED("tool: --ground-names-table writes the original's tile sets' HD names for the program's table")
{
    const std::optional<std::string_view> out = MCTest::Option("ground-names-table");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::string arrays;
    std::string sets;

    for (const std::string_view setName : ShippedTileSets)
    {
        const std::unique_ptr<MCTerrainTiles> tiles = OpenTileSet(setName);
        const std::expected<MCGroundSet, std::string> set = MCWorkOutGroundSet(*tiles);

        if (!set.has_value())
        {
            FAIL(set.error());
        }

        std::string tileRows;
        std::string overlayRows;
        std::string shapeRows;
        std::string spotRows;
        const std::vector<uint8_t>& tips = set->Tips;
        const size_t named = set->Tiles.size() + set->Overlays.size();

        for (const MCGroundNameRow& row : set->Tiles)
        {
            tileRows += RowText(row);
        }

        // Each tile's shape beside it (MCKnownGroundNames::Shapes), so the tile is rebuilt from its HD frame.
        for (const MCGroundShapeRow& shape : set->Shapes)
        {
            shapeRows += std::format("    {{{}, {}, {}, {}, {}}},\n", shape.HotX, shape.HotY, shape.BoxWidth,
                                     shape.TipLength, shape.TipAt);
        }

        for (const MCGroundNameRow& row : set->Overlays)
        {
            overlayRows += RowText(row);
        }

        // Each overlay's hot spot beside it (MCKnownGroundNames::Spots), so the overlay is rebuilt from its HD frame.
        for (const MCGroundOverlaySpot& spot : set->Spots)
        {
            spotRows += std::format("    {{{}, {}}},\n", spot.X, spot.Y);
        }

        std::cout << std::format("{}: {} slots, {} names, {} bytes of tips, files 0x{:016x}\n", setName,
                                 tiles->NumTiles(), named, tips.size(), tiles->FilesHash());
        arrays += std::format("// {}\n", setName) + ArrayText("MCGroundNameRow", RowsName(setName, "tiles"), tileRows) +
                  ArrayText("MCGroundNameRow", RowsName(setName, "overlays"), overlayRows) +
                  ArrayText("MCGroundShapeRow", RowsName(setName, "shapes"), shapeRows) +
                  ArrayText("uint8_t", RowsName(setName, "tips"), BytesText(tips)) +
                  ArrayText("MCGroundOverlaySpot", RowsName(setName, "spots"), spotRows);
        sets += std::format("    {{0x{:016x}, {}, {}, {}, {}, {}, {}}},\n", tiles->FilesHash(), tiles->NumTiles(),
                            RowsName(setName, "tiles"), RowsName(setName, "overlays"), RowsName(setName, "shapes"),
                            RowsName(setName, "tips"), RowsName(setName, "spots"));
    }

    std::ofstream file{std::filesystem::path(*out)};
    REQUIRE(file.good());
    file << "#pragma once\n\n"
            "// Port-only (HD art): the names the HD pack keeps the original's tile sets' ground tiles and overlays "
            "under\n"
            "// (terrain/MCGroundNames.h), worked out from the game's tiles by mc_tests --ground-names-table\n"
            "// (tests/GroundNamesTests.cpp), which writes this file. Don't edit it.\n\n"
            "#include \"terrain/MCGroundNames.h\"\n\n"
            "// clang-format off\n"
         << arrays
         << "/// <summary>The tile sets' names, by their files' hash (MCTerrainTiles::FilesHash).</summary>\n"
            "inline constexpr MCKnownGroundNames MCGroundNamesKnownSets[] = {\n"
         << sets
         << "};\n\n"
            "// clang-format on\n\n"
            "/// <summary>The tile sets the program knows.</summary>\n"
            "inline auto MCGroundNamesKnown() -> std::span<const MCKnownGroundNames>\n"
            "{\n"
            "    return MCGroundNamesKnownSets;\n"
            "}\n";
    REQUIRE(file.good());
}

TEST_CASE_ISOLATED("ground names: the program's table holds the original's tile sets, each slot named as its art is")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));

    for (const std::string_view setName : ShippedTileSets)
    {
        MCTest::Scope scope{std::string(setName)};
        const std::unique_ptr<MCTerrainTiles> tiles = OpenTileSet(setName);
        const MCOptionalRef<const MCKnownGroundNames> known = MCKnownGroundNamesOf(*tiles);
        REQUIRE(known.has_value());
        const MCKnownGroundNames& names = known->get();
        REQUIRE_EQ(names.Slots, tiles->NumTiles());
        REQUIRE_EQ(names.Shapes.size(), names.Tiles.size());
        REQUIRE_EQ(names.Spots.size(), names.Overlays.size());
        size_t wrong = 0;
        size_t wrongShapes = 0;
        size_t wrongSpots = 0;

        for (int32_t slot = 0; slot < tiles->NumTiles(); ++slot)
        {
            wrong += MCWorkOutGroundNames(*tiles, slot) == names.At(slot) ? 0 : 1;
            const std::optional<MCGroundTileShape> shape = MCWorkOutGroundTileShape(*tiles, slot);
            const std::optional<MCGroundTileShape> held = names.ShapeAt(slot);
            wrongShapes +=
                shape.has_value() == held.has_value() && (!shape.has_value() || MCSameGroundTileShape(*shape, *held))
                    ? 0
                    : 1;
            wrongSpots += MCWorkOutGroundOverlaySpot(*tiles, slot) == names.SpotAt(slot) ? 0 : 1;
        }

        CHECK_EQ(wrong, 0);
        CHECK_EQ(wrongShapes, 0);
        CHECK_EQ(wrongSpots, 0);
    }
}

/// <summary>
/// The HD ground a mission asks for by the names the program carries is what it would ask for by its tiles' bytes,
/// tile for tile and overlay for overlay in the same order, so no tile is read late for want of a name. Mission 1, or
/// <c>--ground-names-mission &lt;n&gt;</c>'s (every mission was checked so, 15 Nov).
/// </summary>
TEST_CASE_ISOLATED("ground names: a mission's HD ground asked by name is what its tiles' bytes ask for")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    int32_t mission = 1;

    if (const std::optional<std::string_view> asked = MCTest::Option("ground-names-mission"))
    {
        REQUIRE(std::from_chars(asked->data(), asked->data() + asked->size(), mission).ec == std::errc());
    }

    REQUIRE(MCTestGame::StartMission(mission));
    const std::shared_ptr<MCTerrain> terrain = Terrain();
    REQUIRE(terrain != nullptr);
    const std::optional<MCHdGroundAsks> byName = MCTerrainHdGroundByName(*terrain);
    REQUIRE(byName.has_value());
    const MCHdGroundAsks byBytes = MCTerrainHdGroundByBytes(*terrain);
    // The map's laid mines, either side's (overlay bits 11-12 and 13-14, 2 a laid one), whose tiles the map's overlays
    // don't name (MCTerrainBlock::DrawOverlay draws them by the corners' levels).
    constexpr uint32_t LaidMine = 2;
    const auto laid = std::ranges::count_if(
        GameMap().Map, [](const MCMapTile& tile)
        { return ((tile.Overlay >> 11) & 3) == LaidMine || ((tile.Overlay >> 13) & 3) == LaidMine; });
    // Its mine layers, every side's, which may lay a mine on any cell (MCScenarioLoad's MissionLaysMines).
    const auto layers = std::ranges::count_if(ObjectList()->DefaultList(),
                                              [](const std::shared_ptr<MCBaseObject>& object)
                                              {
                                                  return object->ObjectClass == MCObjectClass::GroundVehicle &&
                                                         static_cast<const MCGroundVehicle&>(*object).MineLayer;
                                              });
    std::cout << std::format("  mission {}: tile set 0x{:016x}, {} tiles and {} overlays by name, {} and {} by bytes, "
                             "{} cells mined, {} mine tiles, {} mine layers, {} mine tiles any cell can draw\n",
                             mission, terrain->Tiles->FilesHash(), byName->Tiles.size(), byName->Overlays.size(),
                             byBytes.Tiles.size(), byBytes.Overlays.size(), laid, MCTerrainMineSlots(*terrain).size(),
                             layers, MCTerrainMineSlots(*terrain, MCMinesAhead::EveryCell).size());
    CHECK(!byBytes.Tiles.empty());
    CHECK(NamesOf(byName->Tiles) == NamesOf(byBytes.Tiles));
    CHECK(NamesOf(byName->Overlays) == NamesOf(byBytes.Overlays));
}

/// <summary>
/// Tool: <c>--ground-rebuild-check &lt;pack folder&gt;</c> rebuilds every ground tile of the original's tile sets from the
/// pack's HD frame (its indices) and the shape the program's table carries for it (MCRebuildGroundTile, PLAN.md's
/// first-use item 10), held to the tile's packet byte for byte; those that aren't, counted by why, with an example each.
/// Every overlay too (MCRebuildGroundOverlay, with its hot spot from the table): from its own pixels, which must come
/// back byte for byte (the encoding is the original's), and from its HD frame, counted by why not.
/// Reads the pack and the game, writes nothing.
/// </summary>
TEST_CASE_ISOLATED("tool: --ground-rebuild-check rebuilds the original's ground tiles from a pack's HD frames")
{
    const std::optional<std::string_view> pack = MCTest::Option("ground-rebuild-check");

    if (!pack.has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    GHdArt = 1;
    MCHdArt::SetPackFolder(std::filesystem::absolute(std::filesystem::path(*pack)));
    REQUIRE(MCHdArt::Active());
    constexpr std::string_view Rebuilt = "rebuilt byte for byte";
    constexpr std::string_view RebuiltOverlay = "an overlay rebuilt byte for byte from its HD frame";
    constexpr std::string_view RebuiltOwn = "an overlay rebuilt byte for byte from its own pixels";

    for (const std::string_view setName : ShippedTileSets)
    {
        MCTest::Scope scope{std::string(setName)};
        const std::unique_ptr<MCTerrainTiles> tiles = OpenTileSet(setName);
        const MCOptionalRef<const MCKnownGroundNames> known = MCKnownGroundNamesOf(*tiles);
        REQUIRE(known.has_value());
        std::map<std::string, std::pair<size_t, int32_t>> outcomes;
        const auto note = [&outcomes](std::string outcome, int32_t slot)
        { outcomes.try_emplace(std::move(outcome), size_t{0}, slot).first->second.first++; };

        for (const MCGroundNameRow& row : known->get().Tiles)
        {
            const int32_t slot = row.Slot;
            tiles->ReadTiles(std::array{slot});
            const MCOptionalRef<const MCTerrainTile> cached = tiles->Slot(slot);
            REQUIRE(cached.has_value());
            const std::span<const uint8_t> packet = cached->get().TileData();
            const MCGroundName name{row.Key, row.Width, row.Height};
            const std::span<const uint8_t> kept = packet.first(TileRowAt(packet, packet[2]));
            const std::shared_ptr<const MCHdArt::Frame> frame = MCHdArt::FindTerrainTile(kept, name.Width, name.Height);

            if (frame == nullptr)
            {
                note("no HD frame", slot);
                continue;
            }

            if (frame->Indices.size() != static_cast<size_t>(name.Width) * name.Height * MCHdArt::IndexBytes)
            {
                note(std::format("its HD frame's indices {}x{}", frame->OriginalWidth(), frame->OriginalHeight()),
                     slot);
                continue;
            }

            const std::optional<MCGroundTileShape> shape = known->get().ShapeAt(slot);
            REQUIRE(shape.has_value());
            const std::optional<std::vector<uint8_t>> rebuilt = MCRebuildGroundTile(frame->Indices, name, *shape);

            if (!rebuilt.has_value())
            {
                note(WhyNotRebuilt(packet, name, frame->Indices), slot);
                continue;
            }

            note(std::ranges::equal(*rebuilt, packet) ? std::string(Rebuilt) : "its name's, but not its packet", slot);
        }

        for (const MCGroundNameRow& row : known->get().Overlays)
        {
            const int32_t slot = row.Slot;
            const std::span<const uint8_t> packet = tiles->Lookup(slot);
            const MCGroundName name{row.Key, row.Width, row.Height};
            const std::optional<MCHdArt::GroundAhead> overlay = MCGroundOverlayOf(*tiles, slot);
            REQUIRE(overlay.has_value());
            const std::shared_ptr<const MCHdArt::Frame> frame =
                MCHdArt::FindOverlay(overlay->Tile, name.Width, name.Height);
            const std::optional<MCGroundOverlaySpot> spot = known->get().SpotAt(slot);
            REQUIRE(spot.has_value());
            const std::vector<uint8_t> own = OverlayOwnIndices(packet, name);
            const std::optional<std::vector<uint8_t>> fromOwn = MCRebuildGroundOverlay(own, name, *spot);
            note(fromOwn.has_value() && std::ranges::equal(*fromOwn, packet)
                     ? std::string(RebuiltOwn)
                     : "an overlay not rebuilt from its own pixels",
                 slot);

            if (frame == nullptr)
            {
                note("an overlay without an HD frame", slot);
                continue;
            }

            const std::optional<std::vector<uint8_t>> rebuilt = MCRebuildGroundOverlay(frame->Indices, name, *spot);
            note(!rebuilt.has_value()                   ? WhyOverlayNotRebuilt(own, frame->Indices)
                 : std::ranges::equal(*rebuilt, packet) ? std::string(RebuiltOverlay)
                                                        : std::string("an overlay of its name, but not its packet"),
                 slot);
        }

        std::cout << std::format("{}: {} ground tiles, {} overlays\n", setName, known->get().Tiles.size(),
                                 known->get().Overlays.size());

        for (const auto& [outcome, count] : outcomes)
        {
            std::cout << std::format("  {:5} {} (e.g. slot {})\n", count.first, outcome, count.second);
        }

        const auto countOf = [&outcomes](std::string_view outcome)
        {
            const auto found = outcomes.find(std::string(outcome));
            return found == outcomes.end() ? size_t{0} : found->second.first;
        };

        CHECK_EQ(countOf(Rebuilt), known->get().Tiles.size());
        // From the HD frames only once the pack marks the pixels the original draws apart from those its indices
        // spread to (PLAN.md's item 10): counted above, not held.
        CHECK_EQ(countOf(RebuiltOwn), known->get().Overlays.size());
    }

    GHdArt = 0;
    MCHdArt::SetPackFolder({});
}

/// <summary>
/// Port-only (PLAN.md's first-use item 10): the load reads the tiles a map's mines draw (MCTerrainMineSlots, which the
/// map's vertices don't name) with the ground, so drawing every mined cell, either side's mines shown, reads no mine
/// tile the load didn't: the corners' levels picked as the draw picks them. Mission 8, which starts with 39 mined cells.
/// </summary>
TEST_CASE_ISOLATED("ground names: the load reads the tiles its mines draw, so drawing every mined cell reads none")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr int32_t MinedMission = 8;
    constexpr float FrameSeconds = 1.0f / 15.0f;
    REQUIRE(MCTestGame::StartMission(MinedMission));
    const std::shared_ptr<MCTerrain> terrain = Terrain();
    REQUIRE(terrain != nullptr);
    const std::vector<int32_t> slots = MCTerrainMineSlots(*terrain);
    REQUIRE(!slots.empty());
    CHECK(std::ranges::contains(slots, MCMineCraterTile));
    const std::vector<int32_t> loaded = MineTilesTouched(*terrain);

    for (const int32_t slot : slots)
    {
        CHECK(std::ranges::contains(loaded, slot));
    }

    RevealAll();
    Scenario()->GodMode = 1;
    size_t mined = 0;

    for (int32_t row = 0; row < GameMap().Height; ++row)
    {
        for (int32_t col = 0; col < GameMap().Width; ++col)
        {
            const MCMineView view = MCTerrainMineView(GameMap().TileAt(row, col).Overlay, true, true);

            if (view.InnerSphereMine || view.ClanMine)
            {
                ++mined;
                Eye()->SetPosition(MCVector3D(TileColToWorld(col), TileRowToWorld(row), 0.0f));
                MCTestGame::RunFrame(FrameSeconds);
            }
        }
    }

    std::cout << std::format("  mission {}: {} mined cells, {} mine tiles read at the load\n", MinedMission, mined,
                             slots.size());
    CHECK(mined > 0);
    CHECK(MineTilesTouched(*terrain) == loaded);
    Scenario()->GodMode = 0;
}

/// <summary>
/// Port-only (PLAN.md's first-use item 10): with a mine layer in the mission (a match's bought one: no campaign or
/// expansion mission starts with one), the load reads every cell's mine tile (MCTerrainReadMinesAhead's EveryCell), so
/// a mine laid on any cell reads none at its first draw: read so here, every cell mined as a mine layer lays one
/// (MCGroundVehicle's Update), then the camera over a cell of each tile, the draw picking its tile by the corners'
/// levels.
/// </summary>
TEST_CASE_ISOLATED("ground names: every cell's mine tile read ahead, a mine laid on any cell reads no tile at its draw")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr int32_t LayersMission = 8;
    constexpr float FrameSeconds = 1.0f / 15.0f;
    // A laid mine's overlay bits, the Inner Sphere's (13-14) and the Clans' (11-12), as the mine layer sets them.
    constexpr uint32_t InnerSphereLaid = 0x4000;
    constexpr uint32_t ClanLaid = 0x1000;
    constexpr uint32_t MineBits = 0x7800;
    REQUIRE(MCTestGame::StartMission(LayersMission));
    const std::shared_ptr<MCTerrain> terrain = Terrain();
    REQUIRE(terrain != nullptr);
    MCTerrainReadMinesAhead(MCMinesAhead::EveryCell);
    const std::vector<int32_t> slots = MCTerrainMineSlots(*terrain, MCMinesAhead::EveryCell);
    REQUIRE(!slots.empty());
    const int32_t last = std::ranges::max(slots);
    const std::vector<int32_t> loaded = MineTilesTouched(*terrain, last);

    for (const int32_t slot : slots)
    {
        CHECK(std::ranges::contains(loaded, slot));
    }

    MCScenarioMap& map = GameMap();
    const MCMapBlockManager& blocks = *terrain->MapBlocks;
    const auto level = [&blocks](int32_t row, int32_t col) { return uint32_t{blocks.MapVertexAt(row, col).Elevation}; };
    // A cell of each mine tile, by its slot.
    std::map<int32_t, std::pair<int32_t, int32_t>> cellOf;

    for (int32_t row = 0; row < map.Height; ++row)
    {
        for (int32_t col = 0; col < map.Width; ++col)
        {
            MCMapTile& tile = map.TileAt(row, col);
            tile.Overlay = ((row + col) % 2 == 0 ? InnerSphereLaid : ClanLaid) | (tile.Overlay & ~MineBits);
            cellOf.try_emplace(
                MCMineTileNumber(level(row, col), level(row, col + 1), level(row + 1, col + 1), level(row + 1, col)),
                row, col);
        }
    }

    RevealAll();
    Scenario()->GodMode = 1;

    for (const auto& picked : cellOf)
    {
        const auto [row, col] = picked.second;
        Eye()->SetPosition(MCVector3D(TileColToWorld(col), TileRowToWorld(row), 0.0f));
        MCTestGame::RunFrame(FrameSeconds);
    }

    std::cout << std::format("  mission {}: {} mine tiles any cell can draw, read ahead; drawn over {} cells\n",
                             LayersMission, slots.size(), cellOf.size());
    CHECK_EQ(cellOf.size() + 1, slots.size());
    CHECK(MineTilesTouched(*terrain, last) == loaded);
    Scenario()->GodMode = 0;
}

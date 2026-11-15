#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "sprite/MCHdArt.h"
#include "terrain/MCGroundMesh.h"
#include "terrain/MCGroundNames.h"
#include "terrain/MCGroundNamesKnown.h"
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

    /// <summary>
    /// A row of the table, for slot <paramref name="slot"/>'s <paramref name="name"/>, which must fit its 16 bits.
    /// </summary>
    auto RowText(int32_t slot, const MCGroundName& name) -> std::string
    {
        constexpr int32_t Most = std::numeric_limits<uint16_t>::max();
        REQUIRE(slot <= Most);
        REQUIRE(name.Width > 0 && name.Width <= Most);
        REQUIRE(name.Height > 0 && name.Height <= Most);
        REQUIRE(name.Key != 0);
        return std::format("    {{0x{:016x}, {}, {}, {}}},\n", name.Key, slot, name.Width, name.Height);
    }

    /// <summary>Rows <paramref name="rows"/> as the table's array <paramref name="arrayName"/>.</summary>
    auto ArrayText(const std::string& arrayName, const std::string& rows) -> std::string
    {
        return std::format("inline constexpr MCGroundNameRow {}[] = {{\n{}}};\n\n", arrayName, rows);
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
} // namespace

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
        std::string tileRows;
        std::string overlayRows;
        size_t named = 0;

        for (int32_t slot = 0; slot < tiles->NumTiles(); ++slot)
        {
            const MCGroundSlotNames names = MCWorkOutGroundNames(*tiles, slot);

            if (names.Tile.has_value())
            {
                tileRows += RowText(slot, *names.Tile);
                ++named;
            }

            if (names.Overlay.has_value())
            {
                overlayRows += RowText(slot, *names.Overlay);
                ++named;
            }
        }

        std::cout << std::format("{}: {} slots, {} names, files 0x{:016x}\n", setName, tiles->NumTiles(), named,
                                 tiles->FilesHash());
        arrays += std::format("// {}\n", setName) + ArrayText(RowsName(setName, "tiles"), tileRows) +
                  ArrayText(RowsName(setName, "overlays"), overlayRows);
        sets += std::format("    {{0x{:016x}, {}, {}, {}}},\n", tiles->FilesHash(), tiles->NumTiles(),
                            RowsName(setName, "tiles"), RowsName(setName, "overlays"));
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
        size_t wrong = 0;

        for (int32_t slot = 0; slot < tiles->NumTiles(); ++slot)
        {
            wrong += MCWorkOutGroundNames(*tiles, slot) == names.At(slot) ? 0 : 1;
        }

        CHECK_EQ(wrong, 0);
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
    std::cout << std::format("  mission {}: tile set 0x{:016x}, {} tiles and {} overlays by name, {} and {} by bytes, "
                             "{} cells mined\n",
                             mission, terrain->Tiles->FilesHash(), byName->Tiles.size(), byName->Overlays.size(),
                             byBytes.Tiles.size(), byBytes.Overlays.size(), laid);
    CHECK(!byBytes.Tiles.empty());
    CHECK(NamesOf(byName->Tiles) == NamesOf(byBytes.Tiles));
    CHECK(NamesOf(byName->Overlays) == NamesOf(byBytes.Overlays));
}

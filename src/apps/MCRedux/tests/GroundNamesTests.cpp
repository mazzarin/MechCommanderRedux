#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "ai/MCMoveGeometry.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "camera/MCCamera.h"
#include "mission/MCScenario.h"
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

    /// <summary>Rows <paramref name="rows"/> as the table's array <paramref name="arrayName"/> of <paramref name="type"/>.</summary>
    auto ArrayText(std::string_view type, const std::string& arrayName, const std::string& rows) -> std::string
    {
        return std::format("inline constexpr {} {}[] = {{\n{}}};\n\n", type, arrayName, rows);
    }

    /// <summary>
    /// A shape row of the table, for <paramref name="shape"/>, whose tip starts <paramref name="tipAt"/> bytes into its
    /// set's tips and must fit its row's 8 bits.
    /// </summary>
    auto ShapeRowText(const MCGroundTileShape& shape, size_t tipAt) -> std::string
    {
        REQUIRE(shape.Tip.size() <= std::numeric_limits<uint8_t>::max());
        REQUIRE(tipAt <= std::numeric_limits<uint32_t>::max());
        return std::format("    {{{}, {}, {}, {}, {}}},\n", shape.HotX, shape.HotY, shape.BoxWidth, shape.Tip.size(),
                           tipAt);
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
        std::string shapeRows;
        std::string spotRows;
        std::vector<uint8_t> tips;
        size_t named = 0;

        for (int32_t slot = 0; slot < tiles->NumTiles(); ++slot)
        {
            const MCGroundSlotNames names = MCWorkOutGroundNames(*tiles, slot);

            if (names.Tile.has_value())
            {
                tileRows += RowText(slot, *names.Tile);
                // Its shape beside it (MCKnownGroundNames::Shapes), so the tile is rebuilt from its HD frame.
                const std::optional<MCGroundTileShape> shape = MCWorkOutGroundTileShape(*tiles, slot);
                REQUIRE(shape.has_value());
                shapeRows += ShapeRowText(*shape, tips.size());
                tips.insert(tips.end(), shape->Tip.begin(), shape->Tip.end());
                ++named;
            }

            if (names.Overlay.has_value())
            {
                overlayRows += RowText(slot, *names.Overlay);
                // Its hot spot beside it (MCKnownGroundNames::Spots), so the overlay is rebuilt from its HD frame.
                const std::optional<MCGroundOverlaySpot> spot = MCWorkOutGroundOverlaySpot(*tiles, slot);
                REQUIRE(spot.has_value());
                spotRows += std::format("    {{{}, {}}},\n", spot->X, spot->Y);
                ++named;
            }
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
    std::cout << std::format("  mission {}: tile set 0x{:016x}, {} tiles and {} overlays by name, {} and {} by bytes, "
                             "{} cells mined\n",
                             mission, terrain->Tiles->FilesHash(), byName->Tiles.size(), byName->Overlays.size(),
                             byBytes.Tiles.size(), byBytes.Overlays.size(), laid);
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
    // The mine tiles a draw has looked at: read, or found missing.
    const auto touched = [&terrain]
    {
        std::vector<int32_t> seen;

        for (int32_t slot = MCMineTileNumber(0, 0, 0, 0); slot <= MCMineCraterTile; ++slot)
        {
            const MCOptionalRef<const MCTerrainTile> tile = terrain->Tiles->Slot(slot);

            if (tile.has_value() && (tile->get().Missing || !tile->get().TileData().empty()))
            {
                seen.push_back(slot);
            }
        }

        return seen;
    };

    const std::vector<int32_t> loaded = touched();

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
    CHECK(touched() == loaded);
    Scenario()->GodMode = 0;
}

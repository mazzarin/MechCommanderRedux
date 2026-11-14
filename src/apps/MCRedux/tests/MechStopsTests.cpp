#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "appear/MCAppearanceTypeList.h"
#include "object/MCBattleMech.h"
#include "mission/MCScenario.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCMechStops.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteManager.h"
#include "sprite/MCSpriteTree.h"
#include "vfx/MCVfxFunctions.h"

// Port-only (MechBraking): the stops braking reads of every mech tree's art (sprite/MCMechStops.h's MCTreeStops), held
// in the table the program carries (sprite/MCMechStopsKnown.h), which --mech-stops-table writes.

namespace
{
    using Clock = std::chrono::steady_clock;

    /// <summary>The milliseconds since <paramref name="start"/>.</summary>
    auto MsSince(Clock::time_point start) -> double
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    /// <summary>Every mech tree of the game's data, once each (two trees may share a name: their packets differ).</summary>
    auto EveryTree() -> std::vector<std::shared_ptr<MCSpriteTree>>
    {
        std::vector<std::shared_ptr<MCSpriteTree>> trees;

        for (const auto& [name, packet] : MCTestGame::MechTrees())
        {
            trees.push_back(std::static_pointer_cast<MCSpriteTree>(
                AppearanceTypeList().GetAppearance(0x01000000u | static_cast<uint32_t>(packet))));
        }

        return trees;
    }

    /// <summary>Forgets <paramref name="tree"/>'s stops, as a tree just loaded has none.</summary>
    auto ForgetStops(MCSpriteTree& tree) -> void
    {
        tree.StopsPrepared = false;
        tree.GestureArtDrawn.clear();
        tree.StopEntries.clear();
        tree.MadeRunToWalk.reset();
        tree.MadeRunToWalkKnown = false;
    }

    /// <summary>A stride's ways into the stop, as the table's source writes them.</summary>
    auto EntriesText(const std::vector<int32_t>& entries) -> std::string
    {
        std::string text = "{";

        for (size_t i = 0; i < entries.size(); ++i)
        {
            text += std::format("{}{}", i == 0 ? "" : ", ", entries[i]);
        }

        return text + "}";
    }

    /// <summary>A tree's stops, as the table's source writes them.</summary>
    auto StopsText(const MCTreeStops& stops) -> std::string
    {
        const std::string made = stops.MadeRunToWalk.has_value()
                                     ? std::format("MCMadeRunToWalk{{{}, {}}}", stops.MadeRunToWalk->RunFrame,
                                                   stops.MadeRunToWalk->WalkFrame)
                                     : "std::nullopt";
        return std::format("{{{{{}, {}, {}}}, {}, {}, {}}}", stops.Drawn[0], stops.Drawn[1], stops.Drawn[2],
                           EntriesText(stops.WalkEntries), EntriesText(stops.RunEntries), made);
    }

    /// <summary>
    /// The milliseconds visiting every pixel of gesture <paramref name="gesture"/>'s frames at the sixteen facings, legs
    /// and torso, as braking first did (a call a pixel, into vectors of offsets), its shapes loaded.
    /// </summary>
    auto PixelVisitMs(MCSpriteTree& tree, int32_t gesture) -> double
    {
        if (gesture >= tree.NumGestures || tree.Gestures[static_cast<size_t>(gesture)].NumFrames == 0)
        {
            return 0.0;
        }

        const uint32_t frames = tree.Gestures[static_cast<size_t>(gesture)].NumFrames;
        // The legs' sixteen facings braking compares, round a whole turn.
        constexpr int32_t Facings = 16;
        constexpr float FullTurn = 360.0f;
        double ms = 0.0;

        for (int32_t facing = 0; facing < Facings; ++facing)
        {
            for (const int32_t part : {0, 1})
            {
                int32_t reverse = 0;
                float frameRate = 0.0f;
                const std::shared_ptr<MCShape> shape = tree.GetGesture(
                    gesture, part, FullTurn * static_cast<float>(facing) / Facings, reverse, frameRate, true);

                if (shape == nullptr)
                {
                    continue;
                }

                const Clock::time_point start = Clock::now();
                std::vector<std::vector<std::pair<int32_t, int32_t>>> pixels(frames);

                for (uint32_t frame = 0; frame < frames; ++frame)
                {
                    VfxShapeVisit(shape->FrameList(), static_cast<int32_t>(frame),
                                  [&](int32_t x, int32_t y, uint8_t) { pixels[frame].emplace_back(x, y); });
                }

                ms += MsSince(start);
            }
        }

        return ms;
    }
} // namespace

/// <summary>
/// Tool: <c>--mech-stops-table &lt;file&gt;</c> works out every mech tree's stops as braking first did
/// (MCMakeStops) and writes them as sprite/MCMechStopsKnown.h, the table the program carries. Prints, a tree a line,
/// what that took: its shapes' loads (cold less warm), its pixels visited a call a pixel, the rest (the comparisons),
/// the fast way's (MCMakeStopsFast) cold and warm, and its art hash's time.
/// </summary>
TEST_CASE_ISOLATED("tool: --mech-stops-table writes every mech tree's stops for the program's table")
{
    const std::optional<std::string_view> out = MCTest::Option("mech-stops-table");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCSpriteManager> spriteManager = SpriteManager();
    REQUIRE(spriteManager != nullptr);
    std::string rows;
    std::set<uint64_t> hashes;
    std::array<double, 8> totals{};
    std::cout << "tree             today cold  loads  visits  compare | fast cold  warm | hash ms    MB\n";

    for (const std::shared_ptr<MCSpriteTree>& tree : EveryTree())
    {
        REQUIRE(tree != nullptr);
        MCTest::Scope scope(tree->Name);

        // Today's: as play first asks, every shape loaded from the file (the walk's likeness to the stop twice: the
        // walk's ways in, then the run's).
        spriteManager->DumpAll();
        ForgetStops(*tree);
        Clock::time_point start = Clock::now();
        const MCTreeStops stops = MCMakeStops(*tree);
        static_cast<void>(MCGestureLikeness(*tree, MCStopGesture::Walk, MCStopGesture::WalkToStand));
        const double cold = MsSince(start);
        start = Clock::now();
        CHECK(MCMakeStops(*tree) == stops);
        static_cast<void>(MCGestureLikeness(*tree, MCStopGesture::Walk, MCStopGesture::WalkToStand));
        const double warm = MsSince(start);
        const double walk = PixelVisitMs(*tree, MCStopGesture::Walk);
        const double stop = PixelVisitMs(*tree, MCStopGesture::WalkToStand);
        const double run = PixelVisitMs(*tree, MCStopGesture::Run);
        const double visits = 2.0 * (walk + stop) + run + stop + (stops.MadeRunToWalk.has_value() ? run + walk : 0.0);

        spriteManager->DumpAll();
        start = Clock::now();
        const MCTreeStops fast = MCMakeStopsFast(*tree);
        const double fastCold = MsSince(start);
        start = Clock::now();
        CHECK(MCMakeStopsFast(*tree) == fast);
        const double fastWarm = MsSince(start);
        CHECK(fast == stops);

        start = Clock::now();
        const uint64_t hash = MCStopsArtHash(*tree);
        const double hashMs = MsSince(start);
        REQUIRE(hashes.insert(hash).second);
        CHECK_EQ(MCStopsArtHash(*tree), hash);

        // The bytes the hash reads: each part PAK's packets of the gestures it looks at.
        size_t hashBytes = 0;

        for (int32_t part = 0; part < MechPartCount; ++part)
        {
            uint32_t from = std::numeric_limits<uint32_t>::max();
            uint32_t to = 0;
            MCMechPart filePart = MCMechPart::Legs;

            for (const int32_t gesture : {MCStopGesture::Walk, MCStopGesture::WalkToStand, MCStopGesture::WalkToRun,
                                          MCStopGesture::Run, MCStopGesture::RunToWalk})
            {
                if (const std::optional<MCGesturePacket> where = tree->GesturePacket(gesture, part, 0.0f))
                {
                    filePart = where->FilePart;
                    from = std::min(from, where->FirstPacket);
                    to = std::max(to, where->FirstPacket + static_cast<uint32_t>(where->Facings));
                }
            }

            if (from < to)
            {
                hashBytes +=
                    spriteManager->MechPacketsStored(tree->FileNumbers[filePart], filePart, from, to - from, true)
                        .size();
            }
        }

        const std::array<double, 8> row = {cold,     cold - warm, visits, warm - visits,
                                           fastCold, fastWarm,    hashMs, static_cast<double>(hashBytes) / 1e6};

        for (size_t i = 0; i < row.size(); ++i)
        {
            totals[i] += row[i];
        }

        std::cout << std::format(
            "{:<16} {:>10.1f} {:>6.1f} {:>7.1f} {:>8.1f} | {:>9.2f} {:>5.2f} | {:>7.2f} {:>5.2f}\n", tree->Name, row[0],
            row[1], row[2], row[3], row[4], row[5], row[6], row[7]);
        rows += std::format("        {{\"{}\", 0x{:016x}ull, {}}},\n", tree->Name, hash, StopsText(stops));
        ForgetStops(*tree);
    }

    std::cout << std::format("{:<16} {:>10.1f} {:>6.1f} {:>7.1f} {:>8.1f} | {:>9.2f} {:>5.2f} | {:>7.2f} {:>5.2f}\n",
                             "all", totals[0], totals[1], totals[2], totals[3], totals[4], totals[5], totals[6],
                             totals[7]);

    std::ofstream file{std::filesystem::path(*out)};
    file << "#pragma once\n\n"
            "// Port-only (MechBraking): every shipped mech tree's stops (sprite/MCMechStops.h's MCTreeStops), worked "
            "out from the\n"
            "// game's art by mc_tests --mech-stops-table (tests/MechStopsTests.cpp), which writes this file. Don't "
            "edit it.\n\n"
            "#include \"sprite/MCMechStops.h\"\n\n"
            "/// <summary>The trees' stops, by their art's hash (MCStopsArtHash).</summary>\n"
            "inline auto MCMechStopsKnown() -> const std::vector<MCKnownStops>&\n"
            "{\n"
            "    static const std::vector<MCKnownStops> known = {\n"
         << rows
         << "    };\n"
            "    return known;\n"
            "}\n";
    REQUIRE(file.good());
}

/// <summary>
/// The table holds every mech tree of the game's data, the original's and the expansion's, by its art's hash: there is
/// never art braking would work out in play. A tree whose art changed (its hash) fails here until the table is made
/// again (mc_tests --mech-stops-table). The fast way the program falls back on for art it hasn't (a mod's) gives every
/// tree's stops as the table has them.
/// </summary>
TEST_CASE_ISOLATED("game: the program's table holds every mech tree's stops, and the fast way gives them the same")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::vector<std::shared_ptr<MCSpriteTree>> trees = EveryTree();
    REQUIRE(trees.size() >= 24);
    std::set<uint64_t> held;

    for (const std::shared_ptr<MCSpriteTree>& tree : trees)
    {
        REQUIRE(tree != nullptr);
        MCTest::Scope scope(tree->Name);
        const MCOptionalRef<const MCKnownStops> known = MCFindKnownStops(MCStopsArtHash(*tree));
        REQUIRE(known.has_value());
        CHECK_EQ(known->get().Tree, tree->Name);
        CHECK(MCMakeStopsFast(*tree) == known->get().Stops);
        held.insert(known->get().ArtHash);
    }

    // Nothing the game's data hasn't.
    CHECK_EQ(held.size(), MCKnownStopsTable().size());
}

/// <summary>
/// The table's stops are braking's own, worked out as it first did (MCMakeStops: frame by frame, pixel by pixel), for a
/// few trees: one of the original's, the Mad Cat (its own run to walk), and two of the expansion's without one drawn,
/// whose run to walk is made (Wendigo, ShadowCat).
/// </summary>
TEST_CASE_ISOLATED("game: the program's table of stops is braking's, worked out pixel by pixel")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));

    for (const std::string_view name : {"thor", "madcat", "Wendigo", "ShadowCat"})
    {
        MCTest::Scope scope(name);
        const std::shared_ptr<MCSpriteTree> tree = MCTestGame::FindMechTree(name);
        REQUIRE(tree != nullptr);
        const MCOptionalRef<const MCKnownStops> known = MCFindKnownStops(MCStopsArtHash(*tree));
        REQUIRE(known.has_value());
        CHECK(MCMakeStops(*tree) == known->get().Stops);
    }
}

/// <summary>
/// A mission's load gives every mech in it, either side's, its stops from the table, so braking works none out in
/// play; what braking then reads is the table's.
/// </summary>
TEST_CASE_ISOLATED("game: a mission's load gives every mech its stops from the table")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    size_t mechs = 0;

    for (const std::weak_ptr<MCMover>& each : MoverRoster)
    {
        const std::shared_ptr<MCMover> object = each.lock();

        if (object == nullptr || object->ObjectClass != MCObjectClass::BattleMech)
        {
            continue;
        }

        const auto tree = std::static_pointer_cast<MCSpriteTree>(object->GetAppearance()->GetAppearanceType());
        REQUIRE(tree != nullptr);
        MCTest::Scope scope(tree->Name);
        CHECK(tree->StopsPrepared);
        const MCOptionalRef<const MCKnownStops> known = MCFindKnownStops(MCStopsArtHash(*tree));
        REQUIRE(known.has_value());
        CHECK(MCStopEntries(*tree, MCStopGesture::Walk) == known->get().Stops.WalkEntries);
        CHECK(MCStopEntries(*tree, MCStopGesture::Run) == known->get().Stops.RunEntries);
        CHECK(MCRunToWalkMade(*tree, true) ==
              (known->get().Stops.Drawn[2] ? std::nullopt : known->get().Stops.MadeRunToWalk));
        ++mechs;
    }

    CHECK(mechs > 0);
}

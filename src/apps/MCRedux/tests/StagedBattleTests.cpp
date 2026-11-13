#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "lib/MCFastFileSet.h"
#include "lib/MCFile.h"
#include "main/MCBenchmark.h"
#include "main/MCGameContext.h"
#include "mission/MCStagedBattle.h"
#include "mission/MCTrailerMission.h"
#include "platform/MCFileSystem.h"

// Port-only: the trailer's staged battle (mission/MCStagedBattle.h), its missions (mission/MCTrailerMission.h) and the
// benchmark that plays it (main/MCBenchmark.h).

namespace
{
    /// <summary>The trailer's missions: the canyon (mission 16) and mission 29.</summary>
    constexpr std::array<std::string_view, 2> TrailerMissions = {"mis0304", "mis0503"};
    /// <summary>The trailer's company: ten pilots in one group of twelve places, the last two empty.</summary>
    constexpr size_t Company = 10;
    constexpr size_t GroupPlaces = 12;

    /// <summary>The first `key = value` line's value in <paramref name="text"/> (to its line's end or comment).</summary>
    std::string ValueOf(std::string_view text, std::string_view key)
    {
        const size_t at = text.find(std::format(" {} ", key));
        REQUIRE(at != std::string_view::npos);
        const size_t equals = text.find('=', at);
        REQUIRE(equals != std::string_view::npos);
        const size_t from = text.find_first_not_of(' ', equals + 1);
        const size_t end = text.find_first_of("/\r\n", from);
        std::string value(text.substr(from, end - from));

        while (!value.empty() && value.back() == ' ')
        {
            value.pop_back();
        }

        return value;
    }
}

/// <summary>
/// The trailer's missions are made from the install's own (the game reads them in place of its own, PutImage): one
/// group of ten pilots, its last part and warrior the mission's counts, each pilot's mech named. With
/// <c>--trailer-missions &lt;folder&gt;</c> each is written there as &lt;mission&gt;.fit (to set beside
/// trailer_mission.py's).
/// </summary>
TEST_CASE("game: the trailer's missions are made from the install's own, ten pilots in one group")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::OpenFastFiles();
    const std::optional<std::string_view> out = MCTest::Option("trailer-missions");

    for (const std::string_view mission : TrailerMissions)
    {
        MCTest::Scope scope{std::string(mission)};
        const std::string path = MCTrailerMission::FilePath(mission);
        const std::optional<std::vector<uint8_t>> original = MCGameContext::Current().FastFiles().Read(path);
        REQUIRE(original.has_value());
        const std::string_view text(reinterpret_cast<const char*>(original->data()), original->size());
        const std::expected<std::string, std::string> made = MCTrailerMission::Make(mission, text);

        if (!made.has_value())
        {
            FAIL(made.error());
        }

        // The ten in one group, every place of twelve filled but two; the last of them the mission's last part.
        std::vector<int32_t> mates;

        for (const auto mate : std::views::split(ValueOf(*made, "Mates"), std::string_view(", ")))
        {
            mates.push_back(std::stoi(std::string(mate.begin(), mate.end())));
        }

        REQUIRE_EQ(mates.size(), GroupPlaces);
        CHECK(std::ranges::none_of(mates | std::views::take(Company), [](int32_t mate) { return mate == 0; }));
        CHECK(std::ranges::all_of(mates | std::views::drop(Company), [](int32_t mate) { return mate == 0; }));
        const int32_t last = mates[Company - 1];
        CHECK_EQ(std::stoi(ValueOf(*made, "NumParts")), last);
        CHECK_EQ(std::stoi(ValueOf(*made, "NumWarriors")), last);
        CHECK(made->contains(std::format("[Part{}]\r\n", last)));
        CHECK(made->contains(std::format("[Warrior{}]\r\n", last)));
        CHECK(made->contains("// Turkina\r\n"));
        CHECK(made->contains("Profile = \"PMW00059\""));
        CHECK(!made->contains("[Commander0Group:1]"));
        CHECK(made->size() > text.size());

        if (out.has_value())
        {
            const std::filesystem::path folder = MCFileSystem::FromUtf8(*out);
            std::filesystem::create_directories(folder);
            std::ofstream(folder / std::format("{}.fit", mission), std::ios::binary) << *made;
        }
    }

    CHECK(!MCTrailerMission::Make("mis0101", "").has_value());
    CHECK(!MCTrailerMission::Make(TrailerMissions.front(), "[Part1]\r\nFITend").has_value());
}

/// <summary>
/// A put mission reads as the trailer's whatever the folders hold, opened in any case or slashes, and as the game's
/// own once the put files are cleared.
/// </summary>
TEST_CASE("game: the game reads a trailer's mission put in place of its own")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::OpenFastFiles();
    const std::string path = MCTrailerMission::FilePath(TrailerMissions.front());
    const std::optional<std::vector<uint8_t>> original = MCGameContext::Current().FastFiles().Read(path);
    REQUIRE(original.has_value());
    REQUIRE(MCTrailerMission::Put(TrailerMissions.front()).has_value());
    CHECK(!MCTrailerMission::Put("mis0101").has_value());

    const auto Read = [](std::string_view name)
    {
        MCFile file;
        REQUIRE_EQ(file.Open(name), 0);
        std::string bytes(file.GetLength(), '\0');
        file.Read(std::span(reinterpret_cast<uint8_t*>(bytes.data()), bytes.size()));
        return bytes;
    };

    const std::string put = Read("DATA/Missions/MIS0304.FIT");
    CHECK(put.contains("// Turkina\r\n"));
    CHECK_EQ(Read(path), put);
    MCFileSystem::ClearPutImages();
    CHECK_EQ(Read(path), std::string(original->begin(), original->end()));
}

TEST_CASE("benchmark: -benchmark's words name the weather, the time of day and the look")
{
    const auto name = [](std::string_view words) -> std::string
    {
        MCTest::Scope scope{std::string(words)};
        const std::expected<MCBenchmark::MCVariant, std::string> variant = MCBenchmark::ReadVariant(words);

        if (!variant.has_value())
        {
            FAIL(variant.error());
        }

        return variant->Name();
    };

    CHECK_EQ(name(""), "clear-day-hd");
    CHECK_EQ(name("Rain,NIGHT"), "rain-night-hd");
    CHECK_EQ(name("blizzard"), "blizzard-day-hd");
    CHECK_EQ(name("dusk,,lightsnow"), "lightsnow-dusk-hd");
    CHECK_EQ(name("original"), "clear-day-original");
    CHECK_EQ(name("day,original,clear"), "clear-day-original");

    const auto refused = [](std::string_view words) -> std::string
    {
        MCTest::Scope scope{std::string(words)};
        const std::expected<MCBenchmark::MCVariant, std::string> variant = MCBenchmark::ReadVariant(words);
        REQUIRE(!variant.has_value());
        return variant.error();
    };

    CHECK(refused("fog").contains("\"fog\" isn't a weather"));
    CHECK(refused("rain,snow").contains("\"snow\" names a second weather"));
    CHECK(refused("night,dusk").contains("\"dusk\" names a second time of day"));
    CHECK(refused("hd,original").contains("\"original\" names a second look"));
    CHECK(refused("original,rain").contains("the original's look has no weather"));
    CHECK(refused("night,original").contains("the original's look has no weather, dusk or night"));
}

TEST_CASE("benchmark: the canyon's spec is built in, and a spec's text is read line by line")
{
    const std::optional<std::string_view> canyon = MCBattleSpec::BuiltIn("canyon");
    REQUIRE(canyon.has_value());
    CHECK(!MCBattleSpec::BuiltIn("desert").has_value());
    const std::expected<MCBattleSpec, std::string> spec = MCBattleSpec::Parse(*canyon);

    if (!spec.has_value())
    {
        FAIL(spec.error());
    }

    // lines-canyon.json's battle, as the trailer's tools wrote it.
    CHECK_EQ(spec->Mission, 16);
    CHECK_EQ(spec->Force, "mis0304");
    CHECK_EQ(spec->Frames, 2680);
    CHECK_EQ(spec->Zoom, 540);
    CHECK_EQ(spec->Walk, 860);
    CHECK_EQ(spec->Answer, 860);
    CHECK_EQ(spec->Contact, 620);
    CHECK_EQ(spec->ContactStep, 260);
    CHECK((spec->Watch == std::vector<int32_t>{620, 900}));
    CHECK_EQ(spec->Converge.value_or(""), "Masakari,Thor");
    REQUIRE(!spec->Cameras.empty());
    CHECK_EQ(spec->Cameras.front().Frame, 900);
    CHECK_EQ(spec->Cameras.front().Mech, 3);
    REQUIRE_EQ(spec->Volleys.size(), size_t{1});
    CHECK_EQ(spec->Volleys.front().Frame, 2545);
    CHECK_EQ(spec->Volleys.front().Mech, 5);
    REQUIRE(!spec->Losses.empty());
    CHECK_EQ(spec->Losses.front().What, "hit");

    // Comments and blank lines aside; a value's own spaces kept.
    const std::expected<MCBattleSpec, std::string> plain =
        MCBattleSpec::Parse("# a battle\n\n  frames 30   # a second\nmission 5\r\nconverge Thor, Masakari\n");

    if (!plain.has_value())
    {
        FAIL(plain.error());
    }

    CHECK_EQ(plain->Frames, 30);
    CHECK_EQ(plain->Mission, 5);
    CHECK_EQ(plain->Converge.value_or(""), "Thor, Masakari");
    CHECK(plain->Force.empty());
    CHECK(plain->Cameras.empty());

    const auto refused = [](std::string_view text) -> std::string
    {
        MCTest::Scope scope{std::string(text)};
        const std::expected<MCBattleSpec, std::string> parsed = MCBattleSpec::Parse(text);
        REQUIRE(!parsed.has_value());
        return parsed.error();
    };

    CHECK_EQ(refused("frames 10\nframes 20"), "the battle's spec: line 2: frames twice");
    CHECK_EQ(refused("colour red"), "the battle's spec: line 1: no spec has colour");
    CHECK_EQ(refused("zoom"), "the battle's spec: line 1: zoom has no value");
    CHECK_EQ(refused("zoom wide"), "the battle's spec: zoom wide: not a whole number");
    CHECK_EQ(refused("camera 900:3,x:1"), "the battle's spec: camera x:1: not frame:mech[:what]");
}

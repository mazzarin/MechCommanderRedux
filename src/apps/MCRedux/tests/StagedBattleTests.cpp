#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "lib/MCFastFileSet.h"
#include "lib/MCFile.h"
#include "main/MCGameContext.h"
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

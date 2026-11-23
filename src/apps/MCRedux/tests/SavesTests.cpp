#include "stdafx.h"
#include "MCTest.h"
#include "MCScratchFolder.h"
#include "TestGame.h"
#include "lib/MCFile.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCMissionNames.h"
#include "logistics/MCSaves.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCGamePaths.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "platform/MCFileSystem.h"

// Port-only (SPEC-menus.md's stage 5, "Models and views"): the saves list LOAD GAME and SAVE GAME view (and CONTINUE
// will), the missions' names it shows them by, and the dates it tells them by.

namespace
{
    constexpr int64_t NanosecondsPerSecond = 1'000'000'000;
    constexpr int64_t SecondsPerHour = 60 * 60;
    constexpr int64_t SecondsPerDay = 24 * SecondsPerHour;
    /// <summary>The game's names for its own "before the mission" saves (Port Arthur's), as the game writes them.</summary>
    constexpr uint32_t PortArthurBefore = 0x37b;
    constexpr uint32_t PortArthurAfter = 0x37a;
    constexpr int SaveNameLength = 199;

    /// <summary>Today's noon, local time, as SDL tells time (the dates' tests count from it, clear of midnight).</summary>
    SDL_Time TodaysNoon()
    {
        constexpr int Noon = 12;
        SDL_Time now = 0;
        REQUIRE(SDL_GetCurrentTime(&now));
        SDL_DateTime date{};
        REQUIRE(SDL_TimeToDateTime(now, &date, true));
        const int64_t intoTheDay =
            (static_cast<int64_t>(date.hour) * SecondsPerHour + date.minute * 60LL + date.second) *
                NanosecondsPerSecond +
            date.nanosecond;
        return now - intoTheDay + Noon * SecondsPerHour * NanosecondsPerSecond;
    }

    /// <summary>Runs the logistics screens for two seconds: a screen change or a dialog settles.</summary>
    void Settle()
    {
        constexpr int32_t Frames = 30;

        for (int32_t frame = 0; frame < Frames; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }

    /// <summary>Sets when the save <paramref name="name"/> was written to <paramref name="secondsAgo"/> seconds ago.</summary>
    void SetSavedAgo(std::string_view name, int64_t secondsAgo)
    {
        const std::filesystem::path file = MCFileSystem::Resolve(GamePath(SavePath, name, ".sav"));
        std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() -
                                                   std::chrono::seconds(secondsAgo));
    }
}

TEST_CASE("saves: a briefing names its operation on its FEDCOM line and its mission on the starred lines after it")
{
    // Its formatting codes dropped, a title on two lines joined, the rule between them skipped.
    // As brf0104.txt writes them, a line of each (its %n), with its centring and colours.
    const std::string briefing = "%fm%fc1%c****CLASSIFIED****%n\r\n"
                                 "%fs%fc4%cFEDCOM:SKYHOOK/02PRDN03%n\r\n"
                                 "%fc5%c----------%n\r\n"
                                 "%fc6%c***DESTROY CLAN***%n\r\n"
                                 "%c***HEADQUARTERS BUILDING***%n\r\n"
                                 "%fc21-Destroy Clan Base Headquarters%n\r\n"
                                 "***NOT A TITLE***%n\r\n";
    const MCMissionNames::MCWritten written = MCMissionNames::FromBriefing(briefing);
    CHECK_EQ(written.Operation, std::string("SKYHOOK"));
    CHECK_EQ(written.Mission, std::string("DESTROY CLAN HEADQUARTERS BUILDING"));

    // One without them names nothing.
    const MCMissionNames::MCWritten nothing = MCMissionNames::FromBriefing("Commander,\r\n***A TITLE***\r\n");
    CHECK(nothing.Operation.empty());
    CHECK(nothing.Mission.empty());
}

TEST_CASE("saves: the menus' names come from the data file, case ignored, and one it lacks shows as written")
{
    const MCScratchFolder folder("mc_saves_tests", "names");
    const std::filesystem::path file = folder.Path() / "missions.json";
    {
        std::ofstream out(file);
        out << R"({"operations": {"SKYHOOK": "Skyhook"}, "missions": {"brf0203.txt": "Destroy enemy convoy", "x": 3}})";
    }

    // "x" has no name: left out.
    REQUIRE(MCMissionNames::Load(file).has_value());
    CHECK_EQ(MCMissionNames::Operation("SKYHOOK"), std::string("Skyhook"));
    CHECK_EQ(MCMissionNames::Operation("HYDRA"), std::string("HYDRA"));
    CHECK_EQ(MCMissionNames::Mission("BRF0203.TXT", "DESTROY ENEMY CONVOY"), std::string("Destroy enemy convoy"));
    CHECK_EQ(MCMissionNames::Mission("brf0204.txt", "DESTROY ENEMY ASSETS"), std::string("DESTROY ENEMY ASSETS"));

    // A file that isn't JSON leaves no names: every one as written.
    {
        std::ofstream out(file);
        out << "not json";
    }

    CHECK(!MCMissionNames::Load(file).has_value());
    CHECK_EQ(MCMissionNames::Operation("SKYHOOK"), std::string("SKYHOOK"));

    // The app's own, read again for the tests after this one.
    REQUIRE(MCMissionNames::Load(MCMissionNames::AppFile()).has_value());
}

TEST_CASE("saves: a save's date is today's or yesterday's time, else its day and month, and its year if not this one")
{
    MCSaves saves;
    const SDL_Time noon = TodaysNoon();
    saves.SetClock([noon] { return noon; });
    const auto ago = [noon](int64_t seconds) { return noon - seconds * NanosecondsPerSecond; };
    CHECK_EQ(saves.DateText(ago(SecondsPerHour)), std::string("Today 11:00"));
    CHECK_EQ(saves.DateSentence(ago(SecondsPerHour)), std::string("today at 11:00"));
    CHECK_EQ(saves.DateText(ago(SecondsPerDay)), std::string("Yesterday 12:00"));
    CHECK_EQ(saves.DateSentence(ago(SecondsPerDay)), std::string("yesterday at 12:00"));

    // Two days and two years back: a day and month (and the year, without the time).
    SDL_DateTime twoDays{};
    REQUIRE(SDL_TimeToDateTime(ago(2 * SecondsPerDay), &twoDays, true));
    const std::string dayText = saves.DateText(ago(2 * SecondsPerDay));
    CHECK(dayText.starts_with(std::format("{} ", twoDays.day)));
    CHECK(!dayText.starts_with("Today") && !dayText.starts_with("Yesterday"));
    constexpr int64_t TwoYears = 2 * 365 * SecondsPerDay;
    SDL_DateTime earlier{};
    REQUIRE(SDL_TimeToDateTime(ago(TwoYears), &earlier, true));
    CHECK(saves.DateText(ago(TwoYears)).ends_with(std::format(" {}", earlier.year)));
    CHECK(saves.DateSentence(ago(TwoYears)).starts_with("on "));
    CHECK(saves.DateText(0).empty());
}

/// <summary>
/// The saves list reads the game's own saves and the player's: each by its mission's briefing name, its operation, its
/// campaign, when it was written and RP, newest first; one that can't be read by its name; deleted and written again.
/// </summary>
TEST_CASE_ISOLATED("game: the saves list names each save by its mission, newest first, and deletes and writes them")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    std::optional<MCTestGame::PinnedSaves> pinned;
    static constexpr std::array<std::string_view, 1> Unreadable = {"Broken"};
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(Unreadable); }));
    Settle();
    NewCampaign();
    Settle();
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    REQUIRE_EQ(logistics->CurrentMission, 0);

    // The campaign's first mission saved as the game names its own (before and after it) and as the player names one.
    const std::string before = MCFormatPrintf(LoadGameString(PortArthurBefore, SaveNameLength).c_str(), 1, 1);
    const std::string after = MCFormatPrintf(LoadGameString(PortArthurAfter, SaveNameLength).c_str(), 1, 1);
    REQUIRE_EQ(MCLogistics::SaveCampaign(before), 0);
    ResourcePoints += 500;
    REQUIRE_EQ(MCLogistics::SaveCampaign("Kaio, second try"), 0);
    REQUIRE_EQ(MCLogistics::SaveCampaign(after), 0);
    SetSavedAgo(before, 3 * SecondsPerHour);
    SetSavedAgo("Kaio, second try", 2 * SecondsPerHour);
    SetSavedAgo(after, SecondsPerHour);
    SetSavedAgo("Broken", SecondsPerDay);

    MCSaves& saves = MCSaves::Game();
    const uint64_t revision = saves.Revision();
    saves.Refresh();
    CHECK(saves.Revision() > revision);
    const std::vector<MCSaves::MCSave>& all = saves.All();
    REQUIRE_EQ(all.size(), size_t{4});
    CHECK_EQ(all[0].Name, after);
    CHECK_EQ(all[1].Name, std::string("Kaio, second try"));
    CHECK_EQ(all[2].Name, before);
    CHECK_EQ(all[3].Name, std::string("Broken"));
    CHECK(!all[3].Read);

    for (size_t index = 0; index < 3; index++)
    {
        MCTest::Scope entry(all[index].Name);
        const MCSaves::MCSave& save = all[index];
        REQUIRE(save.Read);
        CHECK(save.Campaign == MCSaves::MCCampaign::Original);
        CHECK_EQ(save.Operation, 1);
        CHECK_EQ(save.Mission, 1);
        CHECK_EQ(save.OperationName, std::string("Beachhead"));
        CHECK_EQ(save.MissionName, std::string("Liberate work camps"));
        CHECK_EQ(save.Scenario, std::string("mis0101"));
        CHECK(save.InUserFolder);
    }

    CHECK(all[0].When == MCSaves::MCWhen::After);
    CHECK(all[1].When == MCSaves::MCWhen::Named);
    CHECK(all[2].When == MCSaves::MCWhen::Before);
    CHECK_EQ(all[1].ResourcePoints, all[2].ResourcePoints + 500);

    // The newest that can be read, of any campaign or one; none of a campaign with none.
    REQUIRE(saves.Latest().has_value());
    CHECK_EQ(saves.Latest()->get().Name, after);
    CHECK(!saves.Latest(MCSaves::MCCampaign::Expansion).has_value());
    CHECK_EQ(saves.Of(MCSaves::MCCampaign::Original).size(), size_t{4});
    // The unreadable one is listed in every campaign, so it can be deleted.
    CHECK_EQ(saves.Of(MCSaves::MCCampaign::Expansion).size(), size_t{1});
    CHECK(saves.Find("kaio, SECOND try").has_value());

    // Deleted: gone from the disk and the list.
    CHECK(saves.Delete("Broken"));
    CHECK(!saves.Find("Broken").has_value());
    CHECK(!FileExists(GamePath(SavePath, "Broken", ".sav")));

    // Written: the default name is the game's first free one; a write over a name replaces it.
    const std::optional<std::string> free = MCSaves::FreeName();
    REQUIRE(free.has_value());
    CHECK_EQ(*free, MCFormatPrintf(LoadGameString(0x37c, 0x95).c_str(), 0));
    CHECK(saves.Write(*free) == MCSaves::MCWritten::Saved);
    REQUIRE(saves.Find(*free).has_value());
    CHECK(saves.Find(*free)->get().When == MCSaves::MCWhen::Named);
    CHECK_EQ(saves.Latest()->get().Name, *free);
    CHECK(MCSaves::FreeName() != free);
}

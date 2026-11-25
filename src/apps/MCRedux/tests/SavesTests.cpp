#include "stdafx.h"
#include "MCTest.h"
#include "MCMenusPictures.h"
#include "MCScratchFolder.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "lib/MCFile.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCSavesMenu.h"
#include "logistics/MCMissionNames.h"
#include "logistics/MCSaves.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCGamePaths.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "ui/MCRmlChecks.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlMenus.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

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

TEST_CASE(
    "saves: a briefing names its operation on its FEDCOM or STARCOM line and its mission on the starred lines after it")
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

    // A single mission against a Clan's aces has a STARCOM line in its place (xsbrf0401.txt's).
    const MCMissionNames::MCWritten clan = MCMissionNames::FromBriefing("%fm%fc1%c****CLASSIFIED****%n\r\n"
                                                                        "%fs%fc4%cSTARCOM:SOLO/04PRDN01%n\r\n"
                                                                        "%fc5%c----------%n\r\n"
                                                                        "%fc6%c***DESTROY ALL ACES***%n\r\n");
    CHECK_EQ(clan.Operation, std::string("SOLO"));
    CHECK_EQ(clan.Mission, std::string("DESTROY ALL ACES"));

    // A match's side briefing (ccaldera.txt): no FEDCOM line, its title the starred line under the dispatch's head.
    const MCMissionNames::MCWritten side = MCMissionNames::FromBriefing(
        "%fm%fc1%c****CLASSIFIED****%n\r\n%fs%fc5%c----------%n\r\n%fc6%c***Caldera***%n%n\r\n%fc5%c----------%n\r\n"
        "%fc4Kill them.  Kill them all.%n%n\r\n");
    CHECK(side.Operation.empty());
    CHECK_EQ(side.Mission, std::string("Caldera"));

    // Most name their sender before the rule (isbrf0703.txt, isbrf0705rs.txt), the title perhaps on three lines.
    const MCMissionNames::MCWritten arena = MCMissionNames::FromBriefing(
        "%fm%fc1%c****CLASSIFIED****%n\r\n%fs%fc4%cCOMMAND DESIGNATION:GAMBIT/7PRDN3%n\r\n%fc5%c----------%n\r\n"
        "%fc6%c*** ARENA COMBAT ***%n\r\n%fc21-Fight to the Death%n%n\r\n");
    CHECK(arena.Operation.empty());
    CHECK_EQ(arena.Mission, std::string("ARENA COMBAT"));
    const MCMissionNames::MCWritten sheriff = MCMissionNames::FromBriefing(
        "%fm%fc1%c****CLASSIFIED****%n\r\n%fs%fc4%cCOMMAND DESIGNATION:GAMBIT/7PRDN5%n\r\n%fc5%c----------%n\r\n"
        "%fc6%c*** BIG BASE TANGO ***%n\r\n%c**  ROBIN HOOD AND  **%n\r\n%c**   THE SHERIFF    **%n\r\n"
        "%fc5%c----------%n\r\n");
    CHECK_EQ(sheriff.Mission, std::string("BIG BASE TANGO ROBIN HOOD AND THE SHERIFF"));

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

TEST_CASE("saves: a name no file can have on Windows or the Mac is refused")
{
    constexpr std::array<std::string_view, 9> Taken = {
        "Kaio, second try", "savegame000", "con game", "Console", "COM10", "lpt", "a.b", "nul-and-void", "Compass1"};
    constexpr std::array<std::string_view, 22> Refused = {
        "",        "   ", "a/b",  "a\\b",      "c:",       "CON",       "con.sav",     "Nul",
        "AUX.txt", "prn", "COM1", "lpt9",      "com0",     "LPT1 .txt", "COM\xc2\xb9", "Kaio.",
        "Kaio ",   ".",   "..",   "tab\there", "bell\x07", "del\x7f"};
    std::string wrong;

    for (const std::string_view name : Taken)
    {
        if (!MCSaves::CanName(name))
        {
            wrong += std::format("'{}' refused; ", name);
        }
    }

    for (const std::string_view name : Refused)
    {
        if (MCSaves::CanName(name))
        {
            wrong += std::format("'{}' taken; ", name);
        }
    }

    CHECK_EQ(wrong, std::string());
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

namespace
{
    constexpr float FlowFrame = 1.0f / 60.0f;
    constexpr int32_t MostChangeFrames = 600;

    /// <summary>Frames until the change of screen running (and one queued behind it) is done.</summary>
    void RunWhileChanging()
    {
        for (int32_t pass = 0; pass < 2; pass++)
        {
            for (int32_t frame = 0; frame < MostChangeFrames && MCScreenChange::Running(); frame++)
            {
                MCTestGame::RunFrame(FlowFrame);
            }

            MCTestGame::RunFrame(FlowFrame);
        }
    }

    /// <summary>The id of what has the keys' focus on <paramref name="screen"/>'s layer; empty for none.</summary>
    std::string FocusedId(const MCRmlScreen& screen)
    {
        const Rml::Element* focus = screen.Layer().Context().GetFocusElement();
        return focus != nullptr ? focus->GetId() : std::string();
    }

    /// <summary>The hint line's keys on <paramref name="screen"/>, as "Esc Cancel" each, joined by commas.</summary>
    std::string KeysText(const MCRmlScreen& screen)
    {
        std::string text;

        for (const MCRmlKeyHint& hint : screen.Keys)
        {
            text += std::format("{}{} {}", text.empty() ? "" : ", ", hint.Key, hint.Does);
        }

        return text;
    }

    /// <summary>The campaign's first mission saved as the game names its own before it, and by a player's name.</summary>
    std::pair<std::string, std::string> SaveFirstMission()
    {
        const std::string before = MCFormatPrintf(LoadGameString(PortArthurBefore, SaveNameLength).c_str(), 1, 1);
        const std::string named = "Kaio, second try";
        REQUIRE_EQ(MCLogistics::SaveCampaign(before), 0);
        ResourcePoints += 500;
        REQUIRE_EQ(MCLogistics::SaveCampaign(named), 0);
        SetSavedAgo(before, 2 * SecondsPerHour);
        SetSavedAgo(named, SecondsPerHour);
        return {before, named};
    }
}

/// <summary>
/// The main menu's LOAD GAME opens the remade screen over it: every campaign's saves newest first, the newest picked
/// with its facts and its mission's map; Q and E change the campaign shown; Escape goes back; LOAD plays the picked save,
/// its briefing up and LOAD GAME given way to it.
/// </summary>
TEST_CASE_ISOLATED("game: LOAD GAME lists the saves, picks the newest and loads it into its briefing")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    RunWhileChanging();
    NewCampaign();
    RunWhileChanging();
    const auto [before, named] = SaveFirstMission();
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();
    REQUIRE_EQ(MCScreenFlow::StackText(), std::string("main"));

    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Load()));
    RunWhileChanging();
    REQUIRE(MCSavesMenu::Screen().has_value());
    MCSavesScreen& screen = MCSavesMenu::Screen()->get();
    CHECK(screen.Shown());
    CHECK(screen.Mode() == MCSavesScreen::MCMode::Load);
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > load"));
    REQUIRE_EQ(screen.Rows().size(), size_t{2});
    CHECK_EQ(screen.Rows()[0].Mission, named);
    CHECK_EQ(screen.Rows()[0].When, std::string("Named"));
    CHECK_EQ(screen.Rows()[1].Mission, std::string("Liberate work camps"));
    CHECK_EQ(screen.Rows()[1].When, std::string("Before"));
    CHECK_EQ(screen.Rows()[1].Campaign, std::string("Original"));
    CHECK_EQ(screen.Picked(), 0);
    CHECK_EQ(screen.MapSource(), std::string("map:mis0101"));
    CHECK(!screen.Campaign().has_value());

    // E: the original campaign's (both), again: the expansion's (none), Q twice back to ALL.
    MCScreenInput::RealKey(SDL_SCANCODE_E);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(screen.Campaign() == MCSaves::MCCampaign::Original);
    CHECK_EQ(screen.Rows().size(), size_t{2});
    MCScreenInput::RealKey(SDL_SCANCODE_E);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(screen.Campaign() == MCSaves::MCCampaign::Expansion);
    CHECK(screen.Rows().empty());
    CHECK_EQ(screen.Picked(), -1);
    MCScreenInput::RealKey(SDL_SCANCODE_Q);
    MCScreenInput::RealKey(SDL_SCANCODE_Q);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(!screen.Campaign().has_value());

    // Escape: the main menu again.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    RunWhileChanging();
    CHECK(!screen.Shown());
    CHECK(MCMenuScreens::Top() == MCMenuScreens::Main());
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main"));

    // LOAD the game's own save (Down to the second row, then Enter): its briefing, the stack the main menu and the
    // campaign.
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Load()));
    RunWhileChanging();
    CHECK_EQ(screen.Picked(), 0);
    MCScreenInput::RealKey(SDL_SCANCODE_DOWN);
    MCTestGame::RunFrame(FlowFrame);
    CHECK_EQ(screen.PickedSave(), std::optional<std::string>(before));
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    RunWhileChanging();
    CHECK(!screen.Shown());
    CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
    CHECK_EQ(GlobalLog()->CurrentMission, 0);
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > campaign"));
}

namespace
{
    /// <summary>Sets when the save <paramref name="name"/> was written to <paramref name="at"/> (as SDL tells time).</summary>
    void SetSavedAt(std::string_view name, SDL_Time at)
    {
        SDL_Time now = 0;
        REQUIRE(SDL_GetCurrentTime(&now));
        const std::filesystem::path file = MCFileSystem::Resolve(GamePath(SavePath, name, ".sav"));
        std::filesystem::last_write_time(file,
                                         std::filesystem::file_time_type::clock::now() +
                                             std::chrono::duration_cast<std::filesystem::file_time_type::duration>(
                                                 std::chrono::nanoseconds(at - now)));
    }

    /// <summary>
    /// The window's picture now, its hash named by <paramref name="what"/> and its size ("load 640x480: 0x..."), saved
    /// with <c>--shots</c>.
    /// </summary>
    std::string DrawnWindow(MCDisplay& display, std::string_view what)
    {
        constexpr int32_t SettleFrames = 4;

        for (int32_t frame = 0; frame < SettleFrames; frame++)
        {
            MCTestGame::RunFrame(FlowFrame);
        }

        const auto window = display.ReadWindow();
        REQUIRE(window.has_value());

        if (const std::optional<std::filesystem::path> path =
                MCMenusPictures::ShotPath(std::format("saves_{}_{}x{}", what, window->Width, window->Height));
            path.has_value())
        {
            REQUIRE(display.SaveWindowScreenshot(*path).has_value());
        }

        return std::format("{} {}x{}: {:#010x}", what, window->Width, window->Height,
                           MCMenusPictures::PictureHash(*window));
    }
}

/// <summary>
/// LOAD GAME, DELETE's question over it and SAVE GAME over the game on the software renderer, the whole window at
/// 640x480, 1280x720 and 1280x1024 (the 1998 briefing map among it), the hashes recorded: the Mac's and Windows' alike
/// with the game's data. Its saves' times are fixed against today's noon, so their dates read the same every day.
/// The main menu's painting is under them at 640x480; at the larger two the software path draws the stone in its place
/// (the GPU's has the painting), its own question, so their pictures are recorded as it draws them now.
/// With <c>--shots</c> each is saved.
/// </summary>
TEST_CASE_ISOLATED("game: LOAD GAME, its DELETE question and SAVE GAME draw the same on every machine")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    GRenderer = static_cast<int>(MCRendererKind::Software);
    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    REQUIRE(display->RendererKind() == MCRendererKind::Software);
    RunWhileChanging();
    NewCampaign();
    RunWhileChanging();

    // Three saves: the game's before and after the first mission, and one the player named, today and yesterday.
    const SDL_Time noon = TodaysNoon();
    MCSaves::Game().SetClock([noon] { return noon; });
    const std::string before = MCFormatPrintf(LoadGameString(PortArthurBefore, SaveNameLength).c_str(), 1, 1);
    const std::string after = MCFormatPrintf(LoadGameString(PortArthurAfter, SaveNameLength).c_str(), 1, 1);
    REQUIRE_EQ(MCLogistics::SaveCampaign(before), 0);
    REQUIRE_EQ(MCLogistics::SaveCampaign("Kaio, second try"), 0);
    ResourcePoints += 2250;
    REQUIRE_EQ(MCLogistics::SaveCampaign(after), 0);
    SetSavedAt(after, noon - SecondsPerHour * NanosecondsPerSecond);
    SetSavedAt(before, noon - 2 * SecondsPerHour * NanosecondsPerSecond);
    SetSavedAt("Kaio, second try", noon - (SecondsPerDay - SecondsPerHour) * NanosecondsPerSecond);
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();

    // On LOAD GAME from the main menu, and its DELETE's question; then SAVE GAME, opened between missions.
    struct MCDrawn
    {
        std::string_view What;
        std::array<uint32_t, MCMenusPictures::FrameWindows.size()> Hashes;
    };

    constexpr std::array<MCDrawn, 3> Drawn = {{
        {"load", {0x7a39ac75, 0x60cb8fd0, 0x7ea16a57}},
        {"delete", {0xec945728, 0x6146ccbe, 0x7ad4b467}},
        {"save", {0xd5d58b2d, 0x4969b2ad, 0xd931d954}},
    }};

    const auto drawEach = [&display](const MCDrawn& drawn)
    {
        for (size_t i = 0; i < MCMenusPictures::FrameWindows.size(); i++)
        {
            const MCMenusPictures::MCWindowSize& size = MCMenusPictures::FrameWindows[i];
            REQUIRE(MCMenusPictures::SetWindowPixels(*display, size.Width, size.Height));
            CHECK_EQ(DrawnWindow(*display, drawn.What),
                     std::format("{} {}x{}: {:#010x}", drawn.What, size.Width, size.Height, drawn.Hashes[i]));
        }
    };

    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Load()));
    RunWhileChanging();
    REQUIRE(MCSavesMenu::Screen().has_value());
    MCSavesScreen& screen = MCSavesMenu::Screen()->get();
    drawEach(Drawn[0]);
    screen.PressDelete();
    REQUIRE(screen.Dialog());
    CHECK(screen.Dialog()->Shown());
    CHECK_EQ(screen.Dialog()->Saying().Question, std::string(MCSavesMenu::DeleteQuestion));
    CHECK_EQ(screen.Dialog()->Saying().First, std::string("Operation 1, mission 1: Liberate work camps."));
    CHECK_EQ(screen.Dialog()->Saying().Second, std::string("Saved after the mission, today at 11:00."));
    drawEach(Drawn[1]);
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(!screen.Dialog()->Shown());
    CHECK(screen.Shown());
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    RunWhileChanging();

    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Save()));
    RunWhileChanging();
    CHECK(screen.Shown());
    CHECK(screen.Mode() == MCSavesScreen::MCMode::Save);
    drawEach(Drawn[2]);
}

/// <summary>
/// LOAD GAME, its DELETE question and SAVE GAME at every window the players' reports name, with the longest names
/// they show (the campaigns' longest mission's, a player's longest name): every element inside the
/// window, no two siblings overlapping, no text spilling or cut (MCRmlChecks::BoxProblems); every text at least 8
/// units high, at its contrast (MCRmlChecks::Illegible).
/// </summary>
TEST_CASE_ISOLATED("game: LOAD GAME and SAVE GAME lay out and read at every window the players' reports name")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    RunWhileChanging();
    NewCampaign();
    RunWhileChanging();

    // The saves named as the game names its own for the original's longest mission's (operation 4, mission 6) and a
    // player's save under the longest name the box takes.
    constexpr int32_t LongestOperation = 4;
    constexpr int32_t LongestMission = 6;
    const std::string longest =
        MCFormatPrintf(LoadGameString(PortArthurBefore, SaveNameLength).c_str(), LongestOperation, LongestMission);
    const std::string longestName(31, 'W');
    REQUIRE_EQ(MCLogistics::SaveCampaign(longest), 0);
    REQUIRE_EQ(MCLogistics::SaveCampaign(longestName), 0);
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();

    const auto check = [](std::string_view what)
    {
        REQUIRE(MCRmlMenus::Running().has_value());
        MCRmlMenus& menus = MCRmlMenus::Running()->get();

        for (const MCMenusPictures::MCWindowSize& window : MCMenusPictures::PlayersWindows)
        {
            menus.SetWindow(window.Width, window.Height);
            menus.Frame();
            const float unit = MCRmlMenus::MenusUnit(window.Width, window.Height);
            const std::string where = std::format("{} {}x{}: ", what, window.Width, window.Height);
            CHECK_EQ(where + MCRmlChecks::BoxProblems(menus.Menus().Context()), where);
            CHECK_EQ(where + MCRmlChecks::Illegible(menus.Menus().Context(), unit), where);
        }
    };

    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Load()));
    RunWhileChanging();
    REQUIRE(MCSavesMenu::Screen().has_value());
    MCSavesScreen& screen = MCSavesMenu::Screen()->get();
    REQUIRE_EQ(screen.Rows().size(), size_t{2});
    const auto row = std::ranges::find_if(screen.Rows(), [](const MCSavesScreen::MCRow& shown)
                                          { return shown.Mission == "Destroy Smoke Jaguar orbital guns"; });
    REQUIRE(row != screen.Rows().end());
    screen.Pick(static_cast<int>(row - screen.Rows().begin()));
    check("load");
    screen.PressDelete();
    check("delete");
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    RunWhileChanging();

    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Save()));
    RunWhileChanging();
    screen.Pick(0);
    check("save");
}

namespace
{
    /// <summary>Types <paramref name="text"/> as the keyboard does, into what has the focus.</summary>
    void TypeText(const char* text)
    {
        SDL_Event typed{};
        typed.type = SDL_EVENT_TEXT_INPUT;
        typed.text.windowID = SDL_GetWindowID(&MCInput::Display()->Window());
        typed.text.text = text;
        MCInput::HandleEvent(typed);
        MCTestGame::RunFrame(FlowFrame);
    }
}

/// <summary>
/// SAVE GAME, opened between missions: the first free default name typed, Enter in it saves, "Game saved." closes
/// itself after 3 seconds back to the campaign; a save picked puts its name in the box and SAVE asks before writing
/// over it; a name no file can have, and a full disk, each say so. LOAD GAME's DELETE asks, then deletes the save and
/// picks the next; Escape keeps it.
/// </summary>
TEST_CASE_ISOLATED("game: SAVE GAME saves under the name typed, asks before writing over one, and says why one fails")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    RunWhileChanging();
    NewCampaign();
    RunWhileChanging();
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();
    double clock = 0.0;
    MCRmlFlow::Menus().SetClock([&clock] { return clock; });

    // The default name, saved by Enter in it: "Game saved.", gone by itself 3 seconds on, the campaign again.
    const std::string first = MCFormatPrintf(LoadGameString(0x37c, 0x95).c_str(), 0);
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Save()));
    RunWhileChanging();
    REQUIRE(MCSavesMenu::Screen().has_value());
    MCSavesScreen& screen = MCSavesMenu::Screen()->get();
    CHECK(screen.Mode() == MCSavesScreen::MCMode::Save);
    CHECK_EQ(screen.Name(), first);
    CHECK_EQ(screen.Picked(), -1);
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > save"));
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    REQUIRE(screen.Dialog());
    CHECK(screen.Dialog()->Shown());
    CHECK_EQ(screen.Dialog()->Saying().Question, std::string(MCSavesMenu::SavedText));
    CHECK(FileExists(GamePath(SavePath, first, ".sav")));
    REQUIRE_EQ(screen.Saves().size(), size_t{1});
    clock += MCSavesMenu::SavedSeconds + 0.1;
    MCTestGame::RunFrame(FlowFrame);
    RunWhileChanging();
    CHECK(!screen.Dialog()->Shown());
    CHECK(!screen.Shown());
    CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > campaign"));

    // Again: the next free name; the save picked fills the box, and SAVE asks before writing over it.
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Save()));
    RunWhileChanging();
    CHECK_EQ(screen.Name(), MCFormatPrintf(LoadGameString(0x37c, 0x95).c_str(), 1));
    screen.Pick(0);
    CHECK_EQ(screen.Name(), first);
    screen.PressMain();
    CHECK(screen.Dialog()->Shown());
    CHECK_EQ(screen.Dialog()->Saying().Question, std::string(MCSavesMenu::OverwriteQuestion));
    CHECK(screen.Dialog()->Saying().Asks);
    // Escape keeps it; so does Enter in NAME then Enter again, the question opening on CANCEL; asked again, OVERWRITE
    // writes it.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    CHECK(!screen.Dialog()->Shown());
    CHECK(screen.Shown());
    SetSavedAgo(first, SecondsPerHour);
    MCSaves::Game().Refresh();
    const SDL_Time written = MCSaves::Game().Find(first)->get().Saved;
    CHECK_EQ(FocusedId(screen), std::string("name"));
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    REQUIRE(screen.Dialog()->Shown());
    CHECK_EQ(screen.Dialog()->Saying().Question, std::string(MCSavesMenu::OverwriteQuestion));
    CHECK_EQ(FocusedId(*screen.Dialog()), std::string("cancel"));
    CHECK_EQ(KeysText(*screen.Dialog()), std::string("Esc Cancel"));
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    CHECK(!screen.Dialog()->Shown());
    CHECK(screen.Shown());
    MCSaves::Game().Refresh();
    // Not written again: its time as it was, to the second (MCSaves reads a file's time through the file clock's and
    // the system clock's now, a microsecond or so apart from one read to the next; written again it'd be an hour on).
    constexpr SDL_Time ReadBackSlack = SDL_NS_PER_SECOND;
    CHECK(std::llabs(MCSaves::Game().Find(first)->get().Saved - written) < ReadBackSlack);
    screen.PressMain();
    screen.Dialog()->Answer();
    CHECK_EQ(screen.Dialog()->Saying().Question, std::string(MCSavesMenu::SavedText));
    CHECK_EQ(screen.Saves().size(), size_t{1});

    // OK at once (not waiting): the campaign.
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    RunWhileChanging();
    CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));

    // A name no file can have, then a full disk: each says why, and SAVE GAME stays.
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Save()));
    RunWhileChanging();
    TypeText("/");
    CHECK(screen.Name().find('/') != std::string::npos);
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    CHECK_EQ(screen.Dialog()->Saying().Question, std::string(MCSavesMenu::NameRefusedText));
    CHECK_EQ(FocusedId(*screen.Dialog()), std::string("answer"));
    CHECK_EQ(KeysText(*screen.Dialog()), std::string("Enter OK"));
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    CHECK(!screen.Dialog()->Shown());
    CHECK(screen.Shown());
    MCSaves::Game().SetRoom([] { return std::uintmax_t{0}; });
    screen.Pick(0);
    screen.PressMain();
    screen.Dialog()->Answer();
    CHECK_EQ(screen.Dialog()->Saying().Question, std::string(MCSavesMenu::DiskFullText));
    MCSaves::Game().SetRoom({});
    screen.Dialog()->Answer();
    CHECK(screen.Shown());
    // The disk refused before anything was written: the old save is kept.
    CHECK_EQ(screen.Saves().size(), size_t{1});
    CHECK(FileExists(GamePath(SavePath, first, ".sav")));
    // Escape with the focus in NAME: the main menu again.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    RunWhileChanging();
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main"));
}

/// <summary>LOAD GAME's DELETE (the key and the button): the question names the save; Escape keeps it, DELETE takes it.</summary>
TEST_CASE_ISOLATED("game: LOAD GAME's DELETE asks first, then deletes the save and picks the next")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    RunWhileChanging();
    NewCampaign();
    RunWhileChanging();
    const auto [before, named] = SaveFirstMission();
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Load()));
    RunWhileChanging();
    MCSavesScreen& screen = MCSavesMenu::Screen()->get();
    REQUIRE_EQ(screen.Saves().size(), size_t{2});
    CHECK_EQ(screen.PickedSave(), std::optional<std::string>(named));

    MCScreenInput::RealKey(SDL_SCANCODE_DELETE);
    REQUIRE(screen.Dialog());
    CHECK(screen.Dialog()->Shown());
    CHECK_EQ(screen.Dialog()->Saying().Question, std::string(MCSavesMenu::DeleteQuestion));
    CHECK_EQ(screen.Dialog()->Saying().First, std::string("Operation 1, mission 1: Liberate work camps."));
    CHECK(screen.Dialog()->Saying().Second.starts_with("Saved as Kaio, second try, "));
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    CHECK(!screen.Dialog()->Shown());
    CHECK_EQ(screen.Saves().size(), size_t{2});

    // The button, then Enter: the question opens on CANCEL, so the save is kept.
    screen.PressDelete();
    REQUIRE(screen.Dialog()->Shown());
    CHECK_EQ(FocusedId(*screen.Dialog()), std::string("cancel"));
    CHECK_EQ(KeysText(*screen.Dialog()), std::string("Esc Cancel"));
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    CHECK(!screen.Dialog()->Shown());
    CHECK_EQ(screen.Saves().size(), size_t{2});
    CHECK(FileExists(GamePath(SavePath, named, ".sav")));

    // Right to DELETE, its Enter named on the hint line; a delete that fails says so, and the row stays picked.
    MCSaves::Game().SetRemove([](std::string_view) { return false; });
    screen.PressDelete();
    MCScreenInput::RealKey(SDL_SCANCODE_RIGHT);
    CHECK_EQ(FocusedId(*screen.Dialog()), std::string("answer"));
    CHECK_EQ(KeysText(*screen.Dialog()), std::string("Enter Delete, Esc Cancel"));
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    REQUIRE(screen.Dialog()->Shown());
    CHECK_EQ(screen.Dialog()->Saying().Question, std::string(MCSavesMenu::DeleteFailedText));
    MCSaves::Game().SetRemove({});
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    CHECK(!screen.Dialog()->Shown());
    CHECK_EQ(screen.Saves().size(), size_t{2});
    CHECK_EQ(screen.PickedSave(), std::optional<std::string>(named));

    // Right to DELETE, then Enter: deleted, the next picked.
    screen.PressDelete();
    MCScreenInput::RealKey(SDL_SCANCODE_RIGHT);
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    CHECK(!screen.Dialog()->Shown());
    REQUIRE_EQ(screen.Saves().size(), size_t{1});
    CHECK(!FileExists(GamePath(SavePath, named, ".sav")));
    CHECK_EQ(screen.PickedSave(), std::optional<std::string>(before));
    CHECK(screen.Shown());

    // SAVE GAME: a deleted save's NAME follows the pick to the save after it.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    RunWhileChanging();
    SaveFirstMission();
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Save()));
    RunWhileChanging();
    REQUIRE_EQ(screen.Saves().size(), size_t{2});
    screen.Pick(0);
    CHECK_EQ(screen.Name(), named);
    screen.PressDelete();
    MCScreenInput::RealKey(SDL_SCANCODE_RIGHT);
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    REQUIRE_EQ(screen.Saves().size(), size_t{1});
    CHECK_EQ(screen.PickedSave(), std::optional<std::string>(before));
    CHECK_EQ(screen.Name(), before);

    // LOAD GAME: the Delete key, then Enter twice, keeps the save.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    RunWhileChanging();
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Load()));
    RunWhileChanging();
    MCScreenInput::RealKey(SDL_SCANCODE_DELETE);
    REQUIRE(screen.Dialog()->Shown());
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    CHECK(!screen.Dialog()->Shown());
    CHECK(FileExists(GamePath(SavePath, before, ".sav")));
    // The second on the picked row, the focus back on it: LOAD, into its briefing.
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    RunWhileChanging();
    CHECK(FileExists(GamePath(SavePath, before, ".sav")));
    CHECK_EQ(MCSaves::Game().All().size(), size_t{1});
    CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
}

namespace
{
    /// <summary>A left click in the middle of <paramref name="element"/>, as the mouse sends it: SDL's events in the window's points.</summary>
    void ClickOn(Rml::Element& element)
    {
        SDL_Window& window = MCInput::Display()->Window();
        const float pixelDensity = SDL_GetWindowPixelDensity(&window);
        const float density = pixelDensity > 0.0f ? pixelDensity : 1.0f;
        const Rml::Vector2f middle =
            element.GetAbsoluteOffset(Rml::BoxArea::Border) + element.GetBox().GetSize(Rml::BoxArea::Border) / 2.0f;
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = SDL_GetWindowID(&window);
        motion.motion.x = middle.x / density;
        motion.motion.y = middle.y / density;
        MCInput::HandleEvent(motion);
        MCTestGame::RunFrame(FlowFrame);

        for (const bool down : {true, false})
        {
            SDL_Event button{};
            button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            button.button.windowID = motion.motion.windowID;
            button.button.button = SDL_BUTTON_LEFT;
            button.button.down = down;
            button.button.clicks = 1;
            button.button.x = motion.motion.x;
            button.button.y = motion.motion.y;
            MCInput::HandleEvent(button);
            MCTestGame::RunFrame(FlowFrame);
        }
    }

    /// <summary><paramref name="screen"/>'s element <paramref name="id"/>, which must be there.</summary>
    Rml::Element& ElementOf(const MCRmlScreen& screen, const char* id)
    {
        REQUIRE(screen.Document().has_value());
        Rml::Element* element = screen.Document()->get().GetElementById(id);
        REQUIRE(element != nullptr);
        return *element;
    }

    /// <summary>The list's row <paramref name="row"/> on <paramref name="screen"/>.</summary>
    Rml::Element& RowOf(const MCRmlScreen& screen, int row)
    {
        Rml::Element& list = ElementOf(screen, "list");
        REQUIRE(row < list.GetNumChildren());
        return *list.GetChild(row);
    }
}

/// <summary>
/// The mouse alone: a row clicked picks it, the switch's tabs filter the list, DELETE and its CANCEL, BACK; SAVE GAME's
/// row fills NAME, SAVE and OVERWRITE write it, OK goes back to the campaign; LOAD loads.
/// </summary>
TEST_CASE_ISOLATED("game: LOAD GAME and SAVE GAME work from the mouse")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    RunWhileChanging();
    NewCampaign();
    RunWhileChanging();
    const auto [before, named] = SaveFirstMission();
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();

    // LOAD GAME: a row, the switch, DELETE's CANCEL, BACK.
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Load()));
    RunWhileChanging();
    MCSavesScreen& screen = MCSavesMenu::Screen()->get();
    REQUIRE_EQ(screen.Saves().size(), size_t{2});
    ClickOn(RowOf(screen, 1));
    CHECK_EQ(screen.Picked(), 1);
    CHECK_EQ(screen.PickedSave(), std::optional<std::string>(before));
    Rml::Element& tabs = ElementOf(screen, "which");
    REQUIRE_EQ(tabs.GetNumChildren(), 3);
    ClickOn(*tabs.GetChild(2));
    CHECK(screen.Campaign() == std::optional<MCSaves::MCCampaign>(MCSaves::MCCampaign::Expansion));
    CHECK(screen.Saves().empty());
    ClickOn(*tabs.GetChild(0));
    CHECK(!screen.Campaign().has_value());
    REQUIRE_EQ(screen.Saves().size(), size_t{2});
    ClickOn(ElementOf(screen, "delete"));
    REQUIRE(screen.Dialog());
    CHECK(screen.Dialog()->Shown());
    ClickOn(ElementOf(*screen.Dialog(), "cancel"));
    CHECK(!screen.Dialog()->Shown());
    CHECK_EQ(screen.Saves().size(), size_t{2});
    ClickOn(ElementOf(screen, "back"));
    RunWhileChanging();
    CHECK(!screen.Shown());
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main"));

    // SAVE GAME: a row fills NAME; SAVE, OVERWRITE, OK: the campaign.
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Save()));
    RunWhileChanging();
    REQUIRE_EQ(screen.Saves().size(), size_t{2});
    ClickOn(RowOf(screen, 0));
    CHECK_EQ(screen.Name(), screen.Saves()[0]);
    ClickOn(ElementOf(screen, "main"));
    CHECK_EQ(screen.Dialog()->Saying().Question, std::string(MCSavesMenu::OverwriteQuestion));
    ClickOn(ElementOf(*screen.Dialog(), "answer"));
    CHECK_EQ(screen.Dialog()->Saying().Question, std::string(MCSavesMenu::SavedText));
    ClickOn(ElementOf(*screen.Dialog(), "answer"));
    RunWhileChanging();
    CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > campaign"));

    // LOAD GAME's LOAD: the briefing the save stands at.
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Load()));
    RunWhileChanging();
    ClickOn(ElementOf(screen, "main"));
    RunWhileChanging();
    CHECK(!screen.Shown());
    CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > campaign"));
}

/// <summary>
/// OB-161 on the remade SAVE GAME: a save picked puts its name in NAME, so a shorter one's goes in after a longer one's;
/// a key typed after it then follows the shorter name alone. 1998's text box ended a text set into it but not a
/// character typed into it, so a key typed after the shorter name brought back what the longer one had left past its
/// end ("Annz, second try" for "Annz": the 1998 box's own, kept there as OB-161).
/// </summary>
TEST_CASE_ISOLATED("game: SAVE GAME's NAME, a shorter save's name put in after a longer one's, takes a key typed after "
                   "it alone (OB-161)")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view Longer = "Kaio, second try";
    constexpr std::string_view Shorter = "Ann";
    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    RunWhileChanging();
    NewCampaign();
    RunWhileChanging();
    REQUIRE_EQ(MCLogistics::SaveCampaign(Longer), 0);
    REQUIRE_EQ(MCLogistics::SaveCampaign(Shorter), 0);
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Save()));
    RunWhileChanging();
    REQUIRE(MCSavesMenu::Screen().has_value());
    MCSavesScreen& screen = MCSavesMenu::Screen()->get();
    const auto rowOf = [&screen](std::string_view name)
    {
        const auto found = std::ranges::find(screen.Saves(), name);
        REQUIRE(found != screen.Saves().end());
        return static_cast<int>(found - screen.Saves().begin());
    };

    Rml::Element& name = ElementOf(screen, "name");
    const auto shown = [&name] { return name.GetAttribute<Rml::String>("value", ""); };

    // The longer save's name, typed into at its end; then the shorter one's.
    ClickOn(RowOf(screen, rowOf(Longer)));
    CHECK_EQ(screen.Name(), std::string(Longer));
    ClickOn(name);
    MCScreenInput::RealKey(SDL_SCANCODE_END);
    ClickOn(RowOf(screen, rowOf(Shorter)));
    CHECK_EQ(screen.Name(), std::string(Shorter));
    CHECK_EQ(shown(), std::string(Shorter));

    // A key typed after it: the shorter name and the key, nothing of the longer.
    ClickOn(name);
    MCScreenInput::RealKey(SDL_SCANCODE_END);
    TypeText("z");
    CHECK_EQ(screen.Name(), std::string(Shorter) + "z");
    CHECK_EQ(shown(), std::string(Shorter) + "z");
}

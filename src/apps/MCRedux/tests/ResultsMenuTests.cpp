#include "stdafx.h"
#include "MCTest.h"
#include "RemadeScreens.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "gui/MCUpdateDisplay.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCMatchMaps.h"
#include "logistics/MCMenuScreens.h"
#include "main/MCGameContext.h"
#include "main/MCGameSession.h"
#include "main/MCLogistics.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "mission/MCResultsMenu.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "ui/MCRmlChecks.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlMenus.h"

#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/Element.h>

// Port-only (SPEC-menus.md's stage 9, step 1): the results remade over the battlefield (mission/MCResultsMenu): a
// campaign mission won and lost, a match's, each counted as 1998's and laid out at the frames' windows and every
// INTERFACE SIZE, from the keys and the mouse, each way out through the flow; and no 1998 results screen made.

namespace
{
    using MCScreenFlow::MCScreenId;

    /// <summary>A test frame's length: the game's pace.</summary>
    constexpr float FrameSeconds = 1.0f / 15.0f;
    /// <summary>The most frames a change of screen, or a mission's start or end, may take (20 seconds).</summary>
    constexpr int32_t MostFrames = 300;
    /// <summary>Frames a change takes to show (the menus' layout, then the presenter's frame).</summary>
    constexpr int32_t SettleFrames = 3;
    /// <summary>The mouse's ticks a test frame counts (the count's steps are 20, a resource point step one).</summary>
    constexpr uint32_t TicksPerFrame = 5;
    /// <summary>The most frames the count may take (1998's took 55 for mission 1 won).</summary>
    constexpr int32_t MostCountFrames = 3000;

    /// <summary>Results: won, lost (single player's: above 3 a win).</summary>
    constexpr uint32_t Won = 5;
    constexpr uint32_t Lost = 2;

    /// <summary>The results' places (ui/results.rml).</summary>
    constexpr const char* PanelId = "results";
    constexpr const char* MoveOnId = "move-on";
    constexpr const char* RematchId = "rematch";
    /// <summary>How far the panel's middle may be from the window's (a pixel's rounding).</summary>
    constexpr float MiddleSlack = 1.0f;

    struct MCWindowSize
    {
        int32_t Width;
        int32_t Height;
    };

    /// <summary>A remade screen's recorded frames' windows (SPEC-menus.md, "Testing"): 4:3, 16:9 and 5:4.</summary>
    constexpr std::array<MCWindowSize, 3> FrameWindows = {{{640, 480}, {1280, 720}, {1280, 1024}}};
    /// <summary>The window the INTERFACE SIZEs are each checked in (the players' commonest).</summary>
    constexpr MCWindowSize InterfaceWindow{1920, 1080};
    /// <summary>How near the results' unit must be to the battle's (a float's rounding).</summary>
    constexpr float UnitSlack = 0.01f;

    void RunFrames(int32_t frames)
    {
        for (int32_t frame = 0; frame < frames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }
    }

    /// <summary>Runs frames until no change of screen runs and none waits (at most <see cref="MostFrames"/>).</summary>
    void RunWhileChanging()
    {
        for (int32_t frame = 0; frame < MostFrames && MCScreenChange::Running(); frame++)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }

        MCTestGame::RunFrame(FrameSeconds);

        for (int32_t frame = 0; frame < MostFrames && MCScreenChange::Running(); frame++)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }
    }

    std::vector<MCScreenId> StackNow()
    {
        const std::span<const MCScreenId> stack = MCScreenFlow::Stack();
        return {stack.begin(), stack.end()};
    }

    /// <summary>The window made <paramref name="width"/> x <paramref name="height"/> pixels (in points on a dense screen).</summary>
    bool SetWindowPixels(int width, int height)
    {
        SDL_Window& window = MCInput::Display()->Window();
        const float density = std::max(SDL_GetWindowPixelDensity(&window), 1.0f);
        SDL_SetWindowSize(&window, static_cast<int>(std::lround(static_cast<float>(width) / density)),
                          static_cast<int>(std::lround(static_cast<float>(height) / density)));
        SDL_SyncWindow(&window);
        int pixelWidth = 0;
        int pixelHeight = 0;
        SDL_GetWindowSizeInPixels(&window, &pixelWidth, &pixelHeight);
        return pixelWidth == width && pixelHeight == height;
    }

    MCRmlMenus& Menus()
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlMenus::Running();
        REQUIRE(menus.has_value());
        return menus->get();
    }

    /// <summary>
    /// What is wrong with <paramref name="layer"/>'s shown documents at the window: boxes outside it or spilling, and
    /// texts under the floor or without their contrast at the layer's unit; empty: nothing.
    /// </summary>
    std::string Problems(const MCRmlLayer& layer)
    {
        return MCRmlChecks::BoxProblems(layer.Context()) + MCRmlChecks::Illegible(layer.Context(), layer.Unit());
    }

    /// <summary>A mouse event at window pixel (<paramref name="x"/>, <paramref name="y"/>) as SDL sends it, then a frame.</summary>
    void SendMouse(SDL_EventType type, float x, float y)
    {
        SDL_Window& window = MCInput::Display()->Window();
        const float density = std::max(SDL_GetWindowPixelDensity(&window), 1.0f);
        SDL_Event event{};
        event.type = type;

        if (type == SDL_EVENT_MOUSE_MOTION)
        {
            event.motion.windowID = SDL_GetWindowID(&window);
            event.motion.x = x / density;
            event.motion.y = y / density;
        }
        else
        {
            event.button.windowID = SDL_GetWindowID(&window);
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.clicks = 1;
            event.button.x = x / density;
            event.button.y = y / density;
        }

        MCInput::HandleEvent(event);
        MCTestGame::RunFrame(FrameSeconds);
    }

    /// <summary>A player's click in the middle of the results' element <paramref name="id"/>.</summary>
    void ClickOn(const MCRmlScreen& screen, const std::string& id)
    {
        Rml::Element* element = MCRemadeTest::Find(screen, id);
        REQUIRE(element != nullptr);
        REQUIRE(element->IsVisible(true));
        const Rml::Vector2f middle =
            element->GetAbsoluteOffset(Rml::BoxArea::Border) + element->GetBox().GetSize(Rml::BoxArea::Border) / 2.0f;
        SendMouse(SDL_EVENT_MOUSE_MOTION, middle.x, middle.y);
        SendMouse(SDL_EVENT_MOUSE_BUTTON_DOWN, middle.x, middle.y);
        SendMouse(SDL_EVENT_MOUSE_BUTTON_UP, middle.x, middle.y);
    }

    std::shared_ptr<MCResultsScreen> TheResults()
    {
        return MCRemadeTest::OnTop<MCResultsScreen>(MCRemadeTest::MCLayer::Mission);
    }

    /// <summary>The mission's end as the game decides it, its count's clock the test's (the mouse timer stopped).</summary>
    void EndMission(uint32_t result, bool quit)
    {
        MouseTimerKill();
        ScenarioResult = result;
        Mission()->EndScenarioRequested = quit ? -1 : 0;

        for (int32_t frame = 0; frame < MostFrames && Mission()->State != MCMissionState::Results; frame++)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }

        REQUIRE(Mission()->State == MCMissionState::Results);
        REQUIRE(Mission()->ResultsUp());
    }

    /// <summary>The count run to its end, the test's ticks a frame; the frames it took.</summary>
    int32_t CountToTheEnd()
    {
        int32_t frame = 0;

        for (; frame < MostCountFrames && !Mission()->ResultsFinished(); ++frame)
        {
            MouseTicks += TicksPerFrame;
            MCTestGame::RunFrame(FrameSeconds);
        }

        RunFrames(SettleFrames);
        return frame;
    }

    /// <summary>
    /// The results laid out with nothing spilling or cut and every text over the floor at the frames' windows (with
    /// <c>--shots</c> each saved as <paramref name="name"/>).
    /// </summary>
    void CheckLaidOut(std::string_view name)
    {
        for (const MCWindowSize& window : FrameWindows)
        {
            REQUIRE(SetWindowPixels(window.Width, window.Height));
            RunFrames(SettleFrames);
            const std::string where = std::format("{}x{}: ", window.Width, window.Height);
            CHECK_EQ(where + Problems(Menus().Mission()), where);
            MCScreenInput::SaveWindowShot(std::format("{}_{}x{}", name, window.Width, window.Height));
        }
    }

    /// <summary>
    /// A campaign mission won in the players' commonest window at INTERFACE SIZE <paramref name="size"/> (the battle's,
    /// which the results keep): the results drawn at the battle's unit (a line of its screen), that many times fewer
    /// lines than UI RESOLUTION's, nothing spilling or cut and every text over the floor (with <c>--shots</c> saved).
    /// </summary>
    void CheckAtInterfaceSize(int size)
    {
        if (!MCTestGame::Available())
        {
            return;
        }

        const MCTestGame::SaveGameCleanup saves;
        MCTestGame::BootInLargeWindow(InterfaceWindow.Height, size);
        MCTestGame::BootOptions.WindowWidth = InterfaceWindow.Width;
        MCTestGame::BootOptions.WindowHeight = InterfaceWindow.Height;
        REQUIRE(MCTestGame::ReachFirstResults(Won));
        REQUIRE(TheResults() != nullptr);
        MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
        RunFrames(SettleFrames);
        REQUIRE(Mission()->ResultsFinished());

        const int lines = MCInput::Display()->Height();
        CHECK_EQ(lines, static_cast<int>(std::lround(static_cast<double>(InterfaceWindow.Height) *
                                                     OriginalInterfaceSize / static_cast<double>(size))));
        CHECK(std::fabs(Menus().Mission().Unit() -
                        static_cast<float>(InterfaceWindow.Height) / static_cast<float>(lines)) < UnitSlack);
        const std::string where = std::format("interface size {}: ", size);
        CHECK_EQ(where + Problems(Menus().Mission()), where);
        MCScreenInput::SaveWindowShot(std::format("results_interface_{}", size));
    }

    /// <summary>The results' panel in the middle of the window over the battlefield (SPEC-menus.md, "Over the battlefield").</summary>
    void CheckCentred(const MCResultsScreen& results)
    {
        Rml::Element* panel = MCRemadeTest::Find(results, PanelId);
        REQUIRE(panel != nullptr);
        const float left = panel->GetAbsoluteOffset(Rml::BoxArea::Border).x;
        const float width = panel->GetBox().GetSize(Rml::BoxArea::Border).x;
        const float window = static_cast<float>(Menus().Width());
        CHECK(std::fabs(left + width / 2.0f - window / 2.0f) <= MiddleSlack);
    }

    /// <summary>The campaign's briefing for mission <paramref name="mission"/>, the main menu under it.</summary>
    void CheckOnBriefing(int32_t mission)
    {
        REQUIRE(Mission()->State == MCMissionState::Logistics);
        CHECK(Scenario() == nullptr);
        CHECK(!Mission()->ResultsUp());
        CHECK(!Menus().Mission().Showing());
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();
        REQUIRE(logistics != nullptr);
        CHECK_EQ(logistics->CurrentMission, mission);
        CHECK(MCCampaignMenu::Shown());
        const std::vector<MCScreenId> landed = {MCMenuScreens::Main(), MCMenuScreens::Campaign()};
        CHECK(StackNow() == landed);
        CHECK_EQ(MCScreenFlow::Unrouted(), 0u);
    }
}

/// <summary>
/// A campaign mission won: the remade results come up over the battlefield, MISSION SUCCESSFUL under the mission's name,
/// and count as 1998's did (the RP, the five numbers, the objectives, the pilots, then the salvage), laid out at the
/// frames' windows and every INTERFACE SIZE; ENTER (NEXT MISSION) fades out to the next mission's briefing, the main
/// menu under it. No 1998 results screen is made.
/// </summary>
TEST_CASE_ISOLATED(
    "game: a campaign mission won: the remade results count up, and NEXT MISSION goes to the next briefing")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::SaveGameCleanup saves;
    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCTestGame::StartCampaignMission());

    for (int32_t i = 0; i < Scenario()->Objectives.Count(); i++)
    {
        Scenario()->Objectives[i].Status = MCScenarioObjective::Succeeded;
    }

    EndMission(Won, false);
    std::shared_ptr<MCResultsScreen> results = TheResults();
    REQUIRE(results != nullptr);
    CHECK(results->Eyebrow().starts_with("Operation 1 · Mission 1 · "));
    CHECK_EQ(results->Title(), std::string(MCResultsMenu::WonTitle));
    CHECK_EQ(results->MoveOnWord(), std::string(MCResultsMenu::NextMissionWord));
    CHECK(!results->RematchShown());
    CHECK_EQ(MCRemadeTest::Words(*results, MoveOnId), std::string(MCResultsMenu::NextMissionWord));

    // At first nothing counted; the count shows a step at a time.
    CHECK(!results->Finished());
    CHECK(std::ranges::all_of(results->Pilots(), [](const MCResultsScreen::MCPilotRow& row) { return !row.Shown; }));
    const int32_t frames = CountToTheEnd();
    CHECK(results->Finished());
    CHECK(frames > 1);
    // Every objective succeeded: their RP earned, each listed done.
    CHECK_EQ(results->Earned(), MCGroupedPoints(results->Results().ResourcePointsEarned));
    CHECK(results->Results().ResourcePointsEarned > 0);
    REQUIRE(!results->Objectives().empty());
    CHECK(std::ranges::all_of(results->Objectives(), [](const MCResultsScreen::MCObjectiveRow& row)
                              { return row.Shown && row.State == "done"; }));
    REQUIRE(!results->Pilots().empty());
    CHECK(std::ranges::all_of(results->Pilots(), [](const MCResultsScreen::MCPilotRow& row)
                              { return row.Shown && !row.Name.empty() && row.Skills.size() == 4; }));
    CHECK(std::ranges::all_of(results->Figures(),
                              [](const MCResultsScreen::MCFigure& figure) { return !figure.Number.empty(); }));
    CheckCentred(*results);
    CheckLaidOut("results_won");

    // ENTER: NEXT MISSION, the next briefing.
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    RunWhileChanging();
    CheckOnBriefing(1);
}

/// <summary>
/// A campaign mission lost (ABORT MISSION's way: the player quit it): MISSION FAILED, no RP, REPLAY MISSION; Escape
/// shows the rest of the count at once; a click on REPLAY MISSION fades out to the same mission's briefing.
/// </summary>
TEST_CASE_ISOLATED(
    "game: a campaign mission lost: Escape skips the count, and REPLAY MISSION goes to its briefing again")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::SaveGameCleanup saves;
    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCTestGame::StartCampaignMission());
    EndMission(Lost, true);
    std::shared_ptr<MCResultsScreen> results = TheResults();
    REQUIRE(results != nullptr);
    CHECK_EQ(results->Title(), std::string(MCResultsMenu::LostTitle));
    CHECK_EQ(results->MoveOnWord(), std::string(MCResultsMenu::ReplayMissionWord));
    CHECK(results->Earned().empty());
    CHECK(!results->Finished());

    // Escape: everything at once, the count's time not moved on.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    RunFrames(SettleFrames);
    CHECK(results->Finished());
    CHECK(Mission()->State == MCMissionState::Results);
    CHECK(std::ranges::all_of(results->Pilots(), [](const MCResultsScreen::MCPilotRow& row) { return row.Shown; }));
    CheckLaidOut("results_lost");

    // Escape once counted does nothing: the results' ways on are their buttons.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    RunFrames(SettleFrames);
    CHECK(Mission()->State == MCMissionState::Results);
    CHECK(TheResults() == results);

    ClickOn(*results, MoveOnId);
    RunWhileChanging();
    CheckOnBriefing(0);
}

/// <summary>
/// A match's results, as a LAN guest's game ends its match: MISSION SUCCESSFUL or FAILED for the player's side, the
/// match's time, its numbers at once, every pilot by kills with the best first; DONE alone (no REMATCH: a guest's);
/// ENTER leaves the match's mission.
/// </summary>
TEST_CASE_ISOLATED("game: a match's results show every pilot by kills, the best pilot, and DONE leaves the match")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::SaveGameCleanup saves;
    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCTestGame::StartCampaignMission());
    // The campaign mission under way made a LAN match's, as a guest's game plays it.
    auto game = std::make_shared<MCMultiPlayer>();
    MCGameContext::Current().SetMultiPlayer(game);
    REQUIRE_EQ(game->Start(), 0);
    game->SessionManager->CurrentConnection = MCNetProtocol::TcpIp;
    game->InMission = true;
    EndMission(Won, false);

    std::shared_ptr<MCResultsScreen> results = TheResults();
    REQUIRE(results != nullptr);
    CHECK(results->Finished());
    CHECK_EQ(results->MoveOnWord(), std::string(MCResultsMenu::DoneWord));
    CHECK(!results->RematchShown());
    CHECK(!MCRemadeTest::Visible(*results, RematchId));
    CHECK_EQ(results->MatchTime().size(), 5u);
    CHECK(results->Objectives().empty());
    CHECK(results->Pilots().empty());
    CHECK(!results->List().empty());
    CHECK(std::ranges::all_of(results->Figures(),
                              [](const MCResultsScreen::MCFigure& figure) { return !figure.Number.empty(); }));
    CheckLaidOut("results_match");

    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    RunWhileChanging();
    CHECK(Mission()->State == MCMissionState::Logistics);
    CHECK(Scenario() == nullptr);
    CHECK(!Mission()->ResultsUp());
    CHECK(!Menus().Mission().Showing());
}

/// <summary>
/// A launcher's match: its results close by themselves after 1998's 90 seconds, ending the game as DONE does (its room
/// doesn't take on after the match: MCMission::EndsAtResults).
/// </summary>
TEST_CASE_ISOLATED("game: a launcher's match's results end the game after 90 seconds")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::SaveGameCleanup saves;
    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCTestGame::StartCampaignMission());
    MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
    MultiPlayer()->InMission = true;
    IsMPlayerGame = true;
    double now = 0.0;
    Menus().SetClock([&now] { return now; });
    EndMission(Won, false);
    REQUIRE(TheResults() != nullptr);

    // Up until the time is up.
    now = static_cast<double>(MCMissionEnd::MatchTimeout) / 1000.0 - 1.0;
    RunFrames(SettleFrames);
    CHECK(Mission()->ResultsUp());

    std::optional<int32_t> quit;
    now += 2.0;

    try
    {
        RunFrames(SettleFrames);
    }
    catch (const MCGameQuit& thrown)
    {
        quit = thrown.ExitCode;
    }

    CHECK(quit == std::optional<int32_t>(0));
    CHECK(Scenario() == nullptr);
    Menus().SetClock({});
    IsMPlayerGame = false;
}

/// <summary>The results at each INTERFACE SIZE, over a battle played at it (CheckAtInterfaceSize).</summary>
TEST_CASE_ISOLATED("game: the results at INTERFACE SIZE 100% keep the battle's size, nothing spilling")
{
    CheckAtInterfaceSize(InterfaceSizes[0]);
}

TEST_CASE_ISOLATED("game: the results at INTERFACE SIZE 125% keep the battle's size, nothing spilling")
{
    CheckAtInterfaceSize(InterfaceSizes[1]);
}

TEST_CASE_ISOLATED("game: the results at INTERFACE SIZE 150% keep the battle's size, nothing spilling")
{
    CheckAtInterfaceSize(InterfaceSizes[2]);
}

TEST_CASE_ISOLATED("game: the results at INTERFACE SIZE 175% keep the battle's size, nothing spilling")
{
    CheckAtInterfaceSize(InterfaceSizes[3]);
}

TEST_CASE_ISOLATED("game: the results at INTERFACE SIZE 200% keep the battle's size, nothing spilling")
{
    CheckAtInterfaceSize(InterfaceSizes[4]);
}

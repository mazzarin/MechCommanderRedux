#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "network/MCIntroNote.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"

// Port-only (SPEC-menus.md's stage 2, "The screens' flow"): every change of screen through MCScreenFlow with the one
// fade, Escape back one screen on every screen, the landings' stacks, the stack's ends, and the waits a change's work
// can hold.

namespace
{
    using MCScreenChange::MCPhase;
    using MCScreenFlow::MCScreenId;

    /// <summary>The most frames a change of screen may take in the tests (20 seconds at 15 frames a second).</summary>
    constexpr int32_t MostFrames = 300;
    /// <summary>A test frame's length: the game's pace.</summary>
    constexpr float FrameSeconds = 1.0f / 15.0f;

    /// <summary>Runs frames until no change of screen runs and none waits (at most <see cref="MostFrames"/>).</summary>
    void RunWhileChanging()
    {
        for (int32_t frame = 0; frame < MostFrames && MCScreenChange::Running(); frame++)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }

        // A change queued behind the last starts the frame after it.
        MCTestGame::RunFrame(FrameSeconds);

        for (int32_t frame = 0; frame < MostFrames && MCScreenChange::Running(); frame++)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }
    }

    /// <summary>The stack, as ids.</summary>
    std::vector<MCScreenId> StackNow()
    {
        const std::span<const MCScreenId> stack = MCScreenFlow::Stack();
        return {stack.begin(), stack.end()};
    }

    /// <summary>
    /// What a change of screen showed, frame by frame: it faded out and then in (a frame part way each way), and the
    /// screen logistics shows changed only while it was dark.
    /// </summary>
    struct MCFades
    {
        bool FadedOut = false;
        bool FadedIn = false;
        int MostFade = 0;
    };

    /// <summary>Records each frame's fade into <paramref name="fades"/> from now until it goes.</summary>
    class MCRecordFades
    {
    public:
        explicit MCRecordFades(MCFades& fades)
        {
            MCTestGame::OnPresent = [&fades]
            {
                const int fade = MCInput::Display()->Fade();
                fades.MostFade = std::max(fades.MostFade, fade);
                fades.FadedOut = fades.FadedOut || (MCScreenChange::Phase() == MCPhase::Out && fade > 0);
                fades.FadedIn = fades.FadedIn || (MCScreenChange::Phase() == MCPhase::In && fade > 0);
            };
        }

        ~MCRecordFades() { MCTestGame::OnPresent = {}; }

        MCRecordFades(const MCRecordFades&) = delete;
        MCRecordFades& operator=(const MCRecordFades&) = delete;
    };

    /// <summary>
    /// Presses <paramref name="press"/> and runs frames until its change is over: it was the one fade (out live, then
    /// in), no change of screen was made outside it, and the stack is <paramref name="stack"/> after.
    /// </summary>
    void PressAndCheck(std::string_view what, const std::function<void()>& press, const std::vector<MCScreenId>& stack)
    {
        MCTest::Scope scope{std::string(what)};
        const size_t unrouted = MCScreenFlow::Unrouted();
        const size_t finished = MCScreenChange::Finished();
        MCFades fades;
        {
            const MCRecordFades record(fades);
            press();
            RunWhileChanging();
        }

        CHECK(MCScreenChange::Finished() > finished);
        CHECK(fades.FadedOut);
        CHECK(fades.FadedIn);
        CHECK_EQ(fades.MostFade, MCScreenChange::Black);
        CHECK_EQ(MCScreenFlow::Unrouted(), unrouted);
        CHECK_EQ(MCScreenFlow::StackText(),
                 [&stack]
                 {
                     std::string text;

                     for (const MCScreenId id : stack)
                     {
                         text += text.empty() ? "" : " > ";
                         text += MCScreenFlow::Kind(id)->get().Name;
                     }

                     return text;
                 }());
    }

    /// <summary>Escape pressed and let go, as the player does it (each screen takes it on its own key event).</summary>
    void PressEscape()
    {
        MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    }

    /// <summary>Whether the message dialog shows text <paramref name="text"/>.</summary>
    bool Asks(std::string_view text)
    {
        MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
        return dialog.IsShowing() && dialog.Text == text;
    }
}

/// <summary>
/// A change's wait (Until) is asked once a frame until it's over, the pieces after it waiting for it, the screen live
/// meanwhile; a change from black (a movie ended) starts dark and fades the next screen in.
/// </summary>
TEST_CASE_ISOLATED("screen change: a job's wait holds the pieces after it; a change can start from black")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();

    int32_t asked = 0;
    bool after = false;
    REQUIRE(MCScreenChange::Start({.Name = "a test's",
                                   .Job = {{.Part = {}, .Until = [&asked] { return ++asked >= 3; }},
                                           {.Part = {}, .Run = [&after] { after = true; }}},
                                   .FadeOut = MCScreenChange::MCFadeOut::AfterJob}));
    MCTestGame::RunFrame(FrameSeconds);
    CHECK_EQ(asked, 1);
    CHECK(!after);
    CHECK(!MCScreenChange::JobDone());
    CHECK(MCScreenChange::Phase() == MCPhase::Working);
    CHECK_EQ(MCInput::Display()->Fade(), 0);
    MCTestGame::RunFrame(FrameSeconds);
    CHECK_EQ(asked, 2);
    CHECK(!after);
    MCTestGame::RunFrame(FrameSeconds);
    CHECK_EQ(asked, 3);
    CHECK(after);
    CHECK(MCScreenChange::JobDone());
    RunWhileChanging();
    CHECK_EQ(asked, 3);

    bool dark = false;
    REQUIRE(MCScreenChange::Start({.Name = "from black",
                                   .FadeOut = MCScreenChange::MCFadeOut::FromBlack,
                                   .Dark = {{.Part = {}, .Run = [&dark] { dark = MCScreenChange::Dark(); }}}}));
    CHECK(MCScreenChange::Dark());
    MCFades fades;
    {
        const MCRecordFades record(fades);
        RunWhileChanging();
    }

    CHECK(dark);
    CHECK(!fades.FadedOut);
    CHECK(fades.FadedIn);
}

/// <summary>
/// Escape goes back one screen on every screen, as each screen's own way back does, each change the one fade: the main
/// menu's PREFERENCES and LOAD GAME back to it; NEW CAMPAIGN's briefing back to the main menu (which RETURN TO GAME
/// leaves for again); on the main menu, the stack's end, Escape asks to quit, and Escape on the question cancels it.
/// </summary>
TEST_CASE_ISOLATED("screen flow: Escape goes back one screen, and on the main menu asks to quit")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::SaveGameCleanup saves;
    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();
    const MCScreenId main = MCMenuScreens::Main();
    REQUIRE(StackNow() == std::vector{main});

    PressAndCheck("PREFERENCES", MCScreenFlow::Via("preferences", ShowPreferences),
                  {main, MCMenuScreens::Preferences()});
    PressAndCheck("Escape from PREFERENCES", PressEscape, {main});
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->MainScreen);

    PressAndCheck("LOAD GAME", [] { MCScreenFlow::Open(MCMenuScreens::Load()); }, {main, MCMenuScreens::Load()});
    PressAndCheck("Escape from LOAD GAME", PressEscape, {main});

    PressAndCheck("NEW CAMPAIGN", NewCampaign, {main, MCMenuScreens::Campaign()});
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->BriefingScreen);
    // The operation's movie (none in the test's game) would take the first Escape, as the original's did.
    GlobalLog()->BriefingScreen->StopSmackerMovies();
    PressAndCheck("Escape from the briefing", PressEscape, {main});
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->MainScreen);
    PressAndCheck("RETURN TO GAME", MCScreenFlow::Via("return to game", ReturnToGame),
                  {main, MCMenuScreens::Campaign()});
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->BriefingScreen);
    PressAndCheck("MAIN MENU", MCMenuScreens::LeaveCampaign, {main});

    // The stack's end: QUIT's question, and Escape on a question is its CANCEL (OB-074 fixed).
    PressEscape();
    MCTestGame::RunFrame(FrameSeconds);
    CHECK(!MCScreenChange::Running());
    CHECK(Asks(MCMenuScreens::QuitQuestion));
    PressEscape();
    MCTestGame::RunFrame(FrameSeconds);
    CHECK(!GlobalLog()->MessageDialog->IsShowing());
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->MainScreen);
    CHECK(StackNow() == std::vector{main});
}

/// <summary>
/// A screen registered with the flow (as a remade one is) opens over an old one and hands back to it: its Prepare runs
/// live from the press while the old screen fades out, its Show and Hide once dark, its Ready asked until its first frame
/// is whole before it fades in; Escape (its Leave empty) shows the old screen below it again.
/// </summary>
TEST_CASE_ISOLATED("screen flow: a registered screen and an old one hand over to each other")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();

    int32_t prepared = 0;
    bool preparedLive = false;
    bool shown = false;
    int32_t readyAsked = 0;
    const MCScreenId remade = MCScreenFlow::Register({.Name = "a remade test screen",
                                                      .Prepare =
                                                          [&]
                                                      {
                                                          return std::vector<MCScreenChange::MCPiece>{
                                                              {.Part = {},
                                                               .Run =
                                                                   [&]
                                                               {
                                                                   prepared++;
                                                                   preparedLive = !MCScreenChange::Dark();
                                                               }},
                                                              {.Part = {}, .Run = [&] { prepared++; }}};
                                                      },
                                                      .Show =
                                                          [&]
                                                      {
                                                          CHECK(MCScreenChange::Dark());
                                                          CHECK(!GlobalLog()->MainScreen->IsShowing());
                                                          shown = true;
                                                      },
                                                      .Hide = [&] { shown = false; },
                                                      .Ready = [&] { return ++readyAsked >= 2; }});
    const MCScreenId main = MCMenuScreens::Main();

    PressAndCheck("the remade screen opened", [remade] { MCScreenFlow::Open(remade); }, {main, remade});
    CHECK_EQ(prepared, 2);
    CHECK(preparedLive);
    CHECK(shown);
    CHECK_EQ(readyAsked, 2);
    CHECK(MCScreenFlow::Shown() == remade);

    PressAndCheck("back to the main menu", [] { MCScreenFlow::Back(); }, {main});
    CHECK(!shown);
    CHECK(GlobalLog()->MainScreen->IsShowing());
}

/// <summary>
/// A launcher's game starts at its room, the stack's end: its LEAVE and Escape ask "Leave the game?" (whose OK ends the
/// game), and the question's CANCEL keeps the room.
/// </summary>
TEST_CASE_ISOLATED("screen flow: a launcher's room is the stack's end, and asks to leave the game")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLobby("-host -port 28977 -player BOB"));
    RunWhileChanging();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
    CHECK(StackNow() == std::vector{MCMenuScreens::Room()});

    // A dialog the room opens with (a new key's question) answered first.
    while (GlobalLog()->MessageDialog->IsShowing() && !Asks(MCMenuScreens::LeaveGameQuestion))
    {
        GlobalLog()->MessageDialog->Deactivate(0);
        MCTestGame::RunFrame(FrameSeconds);
    }

    CHECK(MCScreenFlow::Back());
    MCTestGame::RunFrame(FrameSeconds);
    CHECK(Asks(MCMenuScreens::LeaveGameQuestion));
    PressEscape();
    MCTestGame::RunFrame(FrameSeconds);
    CHECK(!GlobalLog()->MessageDialog->IsShowing());
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
}

/// <summary>
/// The way out of a mission lands with the stack as if walked there: NEXT MISSION's briefing with the main menu under it
/// (Escape there goes to the main menu).
/// </summary>
TEST_CASE_ISOLATED("screen flow: NEXT MISSION lands on the briefing with the main menu under it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::SaveGameCleanup saves;
    REQUIRE(MCTestGame::ReachFirstResults(5));
    RunWhileChanging();
    Mission()->LeaveResults();
    RunWhileChanging();
    REQUIRE(GlobalLog() != nullptr);
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->BriefingScreen);
    const std::vector<MCScreenId> landed = {MCMenuScreens::Main(), MCMenuScreens::Campaign()};
    CHECK(StackNow() == landed);
    CHECK_EQ(MCScreenFlow::Unrouted(), 0u);

    GlobalLog()->BriefingScreen->StopSmackerMovies();
    PressAndCheck("Escape from the next briefing", PressEscape, {MCMenuScreens::Main()});
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->MainScreen);
}

/// <summary>
/// The campaign's last mission won: its two end movies, then the main menu (MCX.EXE quit the game), fading in from
/// black, the main menu alone on the stack.
/// </summary>
TEST_CASE_ISOLATED("screen flow: the campaign's end movies lead to the main menu")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();

    // As the last movie ends (MoviePlaying, its window gone).
    Mission()->GameOver = true;
    Mission()->CurrentMovie = 4;
    Mission()->State = MCMissionState::MoviePlaying;
    Mission()->NextState = MCMissionState::PlayMovie;
    MCFades fades;
    {
        const MCRecordFades record(fades);
        MCTestGame::RunFrame(FrameSeconds);
        RunWhileChanging();
    }

    CHECK(Mission()->State == MCMissionState::Logistics);
    CHECK(!Mission()->GameOver);
    REQUIRE(GlobalLog() != nullptr);
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->MainScreen);
    CHECK(GlobalLog()->MainScreen->IsShowing());
    CHECK(StackNow() == std::vector{MCMenuScreens::Main()});
    CHECK(fades.FadedIn);
}

/// <summary>
/// The intro note: written by RESTART, taken by the next start within its life (and gone), too old or missing not.
/// </summary>
TEST_CASE("intro note: a restart's note skips the intro once, within its life")
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "mc_tests_intro_note";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    constexpr int64_t Now = 1'800'000'000;
    CHECK(!MCIntroNote::TakeAtStart(folder, Now));

    REQUIRE(MCIntroNote::Write(folder, Now).has_value());
    CHECK(MCIntroNote::TakeAtStart(folder, Now + MCIntroNote::Life.count()));
    CHECK(!std::filesystem::exists(folder / MCIntroNote::FileName));
    CHECK(!MCIntroNote::TakeAtStart(folder, Now));

    REQUIRE(MCIntroNote::Write(folder, Now).has_value());
    CHECK(!MCIntroNote::TakeAtStart(folder, Now + MCIntroNote::Life.count() + 1));
    CHECK(!std::filesystem::exists(folder / MCIntroNote::FileName));
    std::filesystem::remove_all(folder);
}

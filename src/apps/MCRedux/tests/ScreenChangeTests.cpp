#include "stdafx.h"
#include "MCTest.h"
#include "RemadeScreens.h"
#include "TestGame.h"
#include "fakes/MCManualClock.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiMessageBox.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCScreenChange.h"
#include "gui/MCUpdateDisplay.h"
#include "lib/MCFilesAhead.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCMainMenuModel.h"
#include "main/MCLogMech.h"
#include "main/MCLogMechList.h"
#include "main/MCGameContext.h"
#include "main/MCLogistics.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "mission/MCMissionResultsScreen.h"
#include "mission/MCResultsMenu.h"
#include "mission/MCScenario.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCPresenter.h"
#include "ui/MCRmlLayer.h"
#include "vfx/MCVfx.h"

#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>

#include <thread>

// Port-only (SPEC-menus.md's stage 1): the changes of screen into a mission and back, as continuous sequences (the
// screen fading out live, dark only while work is left, the next fading in from its first frame), and the menus' frame
// in the middle of the window.

namespace
{
    using MCScreenChange::MCPhase;

    /// <summary>The colour of the window round the menus' 640x480 frame (SPEC-menus.md's stone, #383b3c).</summary>
    constexpr SDL_Color MenuStone{0x38, 0x3b, 0x3c, SDL_ALPHA_OPAQUE};
    /// <summary>The colour round a mission's view, which fills the window.</summary>
    constexpr SDL_Color Black{0, 0, 0, SDL_ALPHA_OPAQUE};
    /// <summary>The most frames a change of screen into a mission or back may take in the tests (20 seconds).</summary>
    constexpr int32_t MostFrames = 300;
    /// <summary>The remade results' panel (ui/results.rml), and how far from the window's middle its own may be (a pixel's rounding).</summary>
    constexpr const char* ResultsPanel = "results";
    constexpr float MiddleSlack = 1.0f;

    /// <summary>A frame shown, as the tests read it at its present.</summary>
    struct MCShown
    {
        int Fade = 0;
        bool FadeScales = false;
        MCPhase Phase = MCPhase::None;
        SDL_Color Surround{};
        SDL_Rect View{};
        MCMissionState State = MCMissionState::Logistics;
        bool Loading = false;
        bool Menus = false;
        bool Results = false;
        bool Battle = false;
    };

    bool SameColor(SDL_Color a, SDL_Color b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    /// <summary>Records each frame shown into <paramref name="shown"/> from now until it goes.</summary>
    class MCRecordPresents
    {
    public:
        explicit MCRecordPresents(std::vector<MCShown>& shown)
        {
            MCTestGame::OnPresent = [&shown]
            {
                const MCDisplay& display = *MCInput::Display();
                shown.push_back(MCShown{.Fade = display.Fade(),
                                        .FadeScales = display.FadeScales(),
                                        .Phase = MCScreenChange::Phase(),
                                        .Surround = display.Surround(),
                                        .View = display.View(),
                                        .State = Mission()->State,
                                        .Loading = LoadingScreenUp,
                                        .Menus = GlobalLog() != nullptr,
                                        .Results = Mission()->ResultsUp(),
                                        .Battle = Scenario() != nullptr});
            };
        }

        ~MCRecordPresents() { MCTestGame::OnPresent = {}; }

        MCRecordPresents(const MCRecordPresents&) = delete;
        MCRecordPresents& operator=(const MCRecordPresents&) = delete;
    };

    /// <summary>Runs frames until no change of screen runs (at most <see cref="MostFrames"/>).</summary>
    void RunWhileChanging()
    {
        for (int32_t frame = 0; frame < MostFrames && MCScreenChange::Running(); frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }

    /// <summary>
    /// Checks the frames <paramref name="frames"/> of one fade (out when <paramref name="out"/>, else in): they move one
    /// way only, end at its end (black out, clear in), and at least one is part way, so the fade is seen, not cut.
    /// </summary>
    void CheckFade(std::span<const MCShown> frames, bool out, std::string_view what)
    {
        MCTest::Scope scope{std::format("{} ({} frames)", what, frames.size())};
        REQUIRE(!frames.empty());
        const auto moves = [out](const MCShown& before, const MCShown& after)
        { return out ? after.Fade >= before.Fade : after.Fade <= before.Fade; };
        CHECK(std::ranges::adjacent_find(frames, std::not_fn(moves)) == frames.end());
        CHECK_EQ(frames.back().Fade, out ? MCScreenChange::Black : 0);
        CHECK(std::ranges::any_of(frames, [](const MCShown& frame)
                                  { return frame.Fade > 0 && frame.Fade < MCScreenChange::Black; }));
        // Each component scaled toward black, so a dark screen fades evenly over the fade's length.
        CHECK(std::ranges::all_of(frames, [](const MCShown& frame) { return frame.FadeScales || frame.Fade == 0; }));
    }

    /// <summary>The frames of <paramref name="shown"/> from <paramref name="from"/> while <paramref name="keep"/> holds.</summary>
    std::span<const MCShown> RunOf(std::span<const MCShown> shown, size_t from,
                                   const std::function<bool(const MCShown&)>& keep)
    {
        size_t end = from;

        while (end < shown.size() && keep(shown[end]))
        {
            end++;
        }

        return shown.subspan(from, end - from);
    }

    /// <summary>
    /// The frames of <paramref name="shown"/> from <paramref name="from"/>, after the change's last: none (the frame loop
    /// ran until the change was over).
    /// </summary>
    void CheckAfterChange(std::span<const MCShown> shown, size_t from)
    {
        REQUIRE(from <= shown.size());
        CHECK_EQ(shown.size(), from);
        CHECK(std::ranges::all_of(shown.subspan(from), [](const MCShown& frame)
                                  { return frame.Phase == MCPhase::None && frame.Fade == 0; }));
    }

    /// <summary>Whether <paramref name="box"/> sits in the middle of <paramref name="area"/> (to a unit, as halves round).</summary>
    bool CentredOn(MCGuiObject& box, SDL_Rect area)
    {
        const int32_t left = area.x + (area.w - box.Width()) / 2;
        const int32_t top = area.y + (area.h - box.Height()) / 2;
        return std::abs(box.GlobalX() - left) <= 1 && std::abs(box.GlobalY() - top) <= 1;
    }

    /// <summary>A message box made on the screen (as the version dialog is) sits in the middle of <paramref name="area"/>.</summary>
    void CheckMessageBoxCentredOn(SDL_Rect area)
    {
        // The 1998 box's own placement (where the menus can't start).
        const MCTestGame::OldBoxAllowed allowed;
        const MCGuiHandle<MCGuiMessageBox> box =
            ScreenWindow()->CreateChild<MCGuiMessageBox>([](MCGuiMessageBox& made) { made.Init("Centred"); });
        REQUIRE(box != nullptr);
        CHECK(CentredOn(*box, area));
        CloseGuiObject(box);
    }
}

/// <summary>
/// The shown colour round the view is faded as the picture's colours are (the levels taken off each component, or each
/// scaled toward black for a change of screen), so the window round the menus' frame fades with it.
/// </summary>
TEST_CASE("presenter: the window round the view fades with the picture")
{
    MCWindow screen;
    MCFrame frame{.Screen = screen};
    frame.Surround = MenuStone;
    CHECK(SameColor(MCFadedSurround(frame), MenuStone));

    frame.Fade = 0x30;
    CHECK(SameColor(MCFadedSurround(frame), SDL_Color{0x08, 0x0b, 0x0c, SDL_ALPHA_OPAQUE}));

    frame.Fade = MCScreenChange::Black;
    CHECK(SameColor(MCFadedSurround(frame), Black));

    // A change of screen's fade scales each component (rounded), evenly whatever the colour's brightness.
    frame.FadeScales = true;
    frame.Fade = 0x30;
    CHECK(SameColor(MCFadedSurround(frame), SDL_Color{0x2d, 0x30, 0x31, SDL_ALPHA_OPAQUE}));
    CHECK(SameColor(MCFadedColor(SDL_Color{0x10, 0x80, 0xff, 0x40}, 0x80, true), SDL_Color{0x08, 0x40, 0x7f, 0x40}));
    frame.Fade = MCScreenChange::Black;
    CHECK(SameColor(MCFadedSurround(frame), Black));
}

/// <summary>
/// A change whose job runs first runs it live, a piece at a time within the frame's budget, the screen shown clear and
/// the player's input given to it; one change at a time. Done, the screen fades out, the dark work runs and the next
/// screen fades in, and the change's Then follows. Called off while its job runs, the change undoes it (CalledOff) and the
/// screen shown stays, clear.
/// </summary>
TEST_CASE_ISOLATED("screen change: a job runs live before the fade-out, and can be called off")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::SaveGameCleanup saves;
    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();

    int32_t jobPieces = 0;
    int32_t darkPieces = 0;
    bool calledOff = false;
    bool then = false;
    const MCScreenChange::MCPiece job{.Part = {}, .Run = [&jobPieces] { jobPieces++; }};
    REQUIRE(MCScreenChange::Start({.Name = "a test's",
                                   .Job = {job, job},
                                   .FadeOut = MCScreenChange::MCFadeOut::AfterJob,
                                   .Dark = {{.Part = {}, .Run = [&darkPieces] { darkPieces++; }}},
                                   .CalledOff = [&calledOff] { calledOff = true; },
                                   .Then = [&then] { then = true; }}));
    CHECK(MCScreenChange::Phase() == MCPhase::Working);
    CHECK(!MCScreenChange::HoldsInput());
    CHECK_EQ(MCScreenChange::FadeNow(), 0);
    CHECK(!MCScreenChange::Start({.Name = "a second"}));

    // Both pieces in the frame's budget, then the fade-out from that frame.
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK_EQ(jobPieces, 2);
    CHECK(MCScreenChange::Phase() == MCPhase::Out);
    CHECK(MCScreenChange::HoldsInput());
    RunWhileChanging();
    CHECK_EQ(darkPieces, 1);
    CHECK(then);
    CHECK(!calledOff);
    CHECK(!MCScreenChange::HoldsInput());

    // A piece longer than the frame's budget leaves the next for the next frame, the screen still clear; called off
    // there, the rest isn't run.
    jobPieces = 0;
    darkPieces = 0;
    then = false;
    // The budget is the game's time: the test's clock moved on twice the budget.
    constexpr double NanosecondsPerMs = 1e6;
    const auto budget = static_cast<uint64_t>(MCScreenChange::FrameBudgetMs * 2 * NanosecondsPerMs);
    const MCScreenChange::MCPiece slow{
        .Part = {},
        .Run = [&jobPieces, budget]
        {
            dynamic_cast<MCManualClock&>(MCGameContext::Current().Clock()).Advance(budget);
            jobPieces++;
        }};
    REQUIRE(MCScreenChange::Start({.Name = "a test's",
                                   .Job = {slow, job},
                                   .FadeOut = MCScreenChange::MCFadeOut::AfterJob,
                                   .Dark = {{.Part = {}, .Run = [&darkPieces] { darkPieces++; }}},
                                   .CalledOff = [&calledOff] { calledOff = true; },
                                   .Then = [&then] { then = true; }}));
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK_EQ(jobPieces, 1);
    CHECK(MCScreenChange::Phase() == MCPhase::Working);
    CHECK_EQ(MCInput::Display()->Fade(), 0);
    MCScreenChange::Cancel();
    CHECK(calledOff);
    CHECK(!MCScreenChange::Running());
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK_EQ(jobPieces, 1);
    CHECK_EQ(darkPieces, 0);
    CHECK(!then);
    CHECK_EQ(MCInput::Display()->Fade(), 0);
}

/// <summary>
/// BEGIN MISSION fades the briefing out live (it keeps drawing), straight into the loading screen fading in, without a
/// dark frame between; the save, the menus' teardown and the load run behind the loading screen (the next change's job,
/// live), which then fades out,
/// and the battlefield fades in at the window's size from its first frame. The player's clicks and keys are held
/// meanwhile. The menus sit in their 640x480 frame on the stone; the battlefield fills the window.
/// </summary>
TEST_CASE_ISOLATED("game: BEGIN MISSION fades into the loading screen, and the loading screen into the battlefield")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::SaveGameCleanup saves;
    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();
    NewCampaign();

    for (int32_t frame = 0; frame < 30; frame++)
    {
        MCTestGame::RunFrame(1.0f / 15.0f);
    }

    {
        // As StartCampaignMission deploys: the force's first mech.
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();
        REQUIRE(logistics->CurrentScreen() == logistics->BriefingScreen);
        std::shared_ptr<MCLogMech> mech;
        REQUIRE_EQ(logistics->ForceMechList->GetMechInfo(0, mech), 0);
        logistics->DeploySlots[0][0].Unit = 0;
        mech->Deployed = true;
    }

    const MCDisplay& display = *MCInput::Display();
    CHECK(SameColor(display.Surround(), MenuStone));
    CHECK(display.View().w == MenuFrameWidth);
    CHECK(display.View().h == MenuFrameHeight);

    const size_t finished = MCScreenChange::Finished();
    std::vector<MCShown> shown;
    {
        const MCRecordPresents record(shown);
        // The briefing's BEGIN MISSION (MCBriefingScreen::Launch).
        Mission()->RequestScenario(Mission()->Scenarios[0]);
        MCTestGame::RunFrame(1.0f / 15.0f);
        CHECK(MCScreenChange::Running());
        CHECK(MCScreenChange::HoldsInput());
        RunWhileChanging();
    }

    CHECK_EQ(MCScreenChange::Finished(), finished + 2);
    CHECK(!MCScreenChange::HoldsInput());
    REQUIRE(Mission()->State == MCMissionState::Scenario);
    REQUIRE(Scenario() != nullptr);
    CHECK(!LoadingScreenUp);
    CHECK(SameColor(display.Surround(), Black));
    CHECK_EQ(display.View().w, GuiSystem().Width());
    CHECK_EQ(display.View().h, GuiSystem().Height());
    CHECK_EQ(display.Fade(), 0);

    // The briefing fades out live, the menus up and drawing.
    const std::span<const MCShown> briefing =
        RunOf(shown, 0, [](const MCShown& frame) { return frame.Phase == MCPhase::Out && !frame.Loading; });
    CheckFade(briefing, true, "the briefing's fade-out");
    CHECK(std::ranges::all_of(briefing, [](const MCShown& frame)
                              { return frame.Menus && frame.State == MCMissionState::Logistics; }));
    CHECK(std::ranges::all_of(briefing, [](const MCShown& frame) { return SameColor(frame.Surround, MenuStone); }));

    // The loading screen next, fading in at once: no frame dark between.
    REQUIRE(briefing.size() < shown.size());
    const std::span<const MCShown> loading =
        RunOf(shown, briefing.size(), [](const MCShown& frame) { return frame.Loading; });
    REQUIRE(!loading.empty());
    CHECK(loading.front().Phase == MCPhase::In);
    const std::span<const MCShown> loadingIn =
        RunOf(shown, briefing.size(), [](const MCShown& frame) { return frame.Loading && frame.Phase == MCPhase::In; });
    CheckFade(loadingIn, false, "the loading screen's fade-in");
    // The menus taken down behind it, the battlefield loaded, then the loading screen fades out.
    CHECK(std::ranges::any_of(loading, [](const MCShown& frame) { return !frame.Menus; }));
    CHECK(!loading.back().Menus);
    CHECK(loading.back().Battle);
    const auto loadingOut =
        std::ranges::find_if(loading, [](const MCShown& frame) { return frame.Phase == MCPhase::Out; });
    REQUIRE(loadingOut != loading.end());
    CheckFade(loading.subspan(static_cast<size_t>(loadingOut - loading.begin())), true,
              "the loading screen's fade-out");
    CHECK(std::ranges::all_of(loading, [](const MCShown& frame) { return SameColor(frame.Surround, MenuStone); }));
    // The load's own frames, the change into the battle's job, show the loading screen clear.
    CHECK(std::ranges::all_of(loading,
                              [](const MCShown& frame) { return frame.Phase != MCPhase::Working || frame.Fade == 0; }));

    // The battlefield fades in from its first frame, filling the window.
    const size_t battleFrom = briefing.size() + loading.size();
    REQUIRE(battleFrom < shown.size());
    const std::span<const MCShown> battle =
        RunOf(shown, battleFrom, [](const MCShown& frame) { return frame.Phase == MCPhase::In; });
    CheckFade(battle, false, "the battlefield's fade-in");
    CHECK(std::ranges::all_of(battle, [](const MCShown& frame)
                              { return frame.State == MCMissionState::Scenario && SameColor(frame.Surround, Black); }));
    CheckAfterChange(shown, battleFrom + battle.size());
}

/// <summary>
/// At a player's large window, a mission's results sit in the middle of the battlefield, and a message box in the
/// middle of the window. Their DONE fades them out live over the battlefield; dark, the scenario ends and the menus are
/// made again, a piece a frame, and the next screen fades in from its first frame, in the menus' frame on the stone,
/// where a message box sits in the frame's middle.
/// </summary>
TEST_CASE_ISOLATED("game: a mission's results sit in the middle of the battlefield, and fade out to the next screen")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::SaveGameCleanup saves;
    MCTestGame::BootInLargeWindow(1080, OriginalInterfaceSize);
    REQUIRE(MCTestGame::ReachFirstResults(5));
    RunWhileChanging();

    const MCDisplay& display = *MCInput::Display();
    const int32_t width = GuiSystem().Width();
    const int32_t height = GuiSystem().Height();
    REQUIRE(width > MenuFrameWidth);
    REQUIRE(height > MenuFrameHeight);
    // The remade results over the battlefield, their panel in the window's middle (no 1998 screen made).
    CHECK(Mission()->ResultsScreen == nullptr);
    const MCOptionalRef<MCResultsScreen> results = MCResultsMenu::Screen();
    REQUIRE(results.has_value());
    Rml::Element* panel = MCRemadeTest::Find(results->get(), ResultsPanel);
    REQUIRE(panel != nullptr);
    const Rml::Vector2f middle =
        panel->GetAbsoluteOffset(Rml::BoxArea::Border) + panel->GetBox().GetSize(Rml::BoxArea::Border) / 2.0f;
    const Rml::Vector2i window = results->get().Layer().Context().GetDimensions();
    CHECK(std::fabs(middle.x - static_cast<float>(window.x) / 2.0f) <= MiddleSlack);
    CHECK(std::fabs(middle.y - static_cast<float>(window.y) / 2.0f) <= MiddleSlack);
    CHECK_EQ(display.View().w, width);
    CHECK_EQ(display.View().h, height);
    CHECK(SameColor(display.Surround(), Black));
    CheckMessageBoxCentredOn(SDL_Rect{0, 0, width, height});

    const size_t finished = MCScreenChange::Finished();
    std::vector<MCShown> shown;
    {
        const MCRecordPresents record(shown);
        // DONE (the results' move-on button).
        Mission()->LeaveResults();
        RunWhileChanging();
    }

    CHECK_EQ(MCScreenChange::Finished(), finished + 1);
    REQUIRE(Mission()->State == MCMissionState::Logistics);
    CHECK(Scenario() == nullptr);
    CHECK(!Mission()->ResultsUp());
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    REQUIRE(logistics != nullptr);
    CHECK(logistics->CurrentScreen() != nullptr);
    CHECK_EQ(MCFilesAhead::HeldCount(), 0u);
    CHECK(SameColor(display.Surround(), MenuStone));
    CHECK_EQ(display.View().w, MenuFrameWidth);
    CHECK_EQ(display.View().h, MenuFrameHeight);
    CheckMessageBoxCentredOn(SDL_Rect{0, 0, MenuFrameWidth, MenuFrameHeight});

    // The results fade out live over the battlefield.
    const std::span<const MCShown> out =
        RunOf(shown, 0, [](const MCShown& frame) { return frame.Phase == MCPhase::Out; });
    CheckFade(out, true, "the results' fade-out");
    CHECK(std::ranges::all_of(out, [](const MCShown& frame)
                              { return frame.Results && frame.Battle && frame.State == MCMissionState::Results; }));

    // Dark only while the way back's pieces are left, each frame black: none here, where the frame's budget is the
    // test's clock, which doesn't move within a frame (the pieces all fit the frame after the fade-out's black one).
    const std::span<const MCShown> dark =
        RunOf(shown, out.size(), [](const MCShown& frame) { return frame.Phase == MCPhase::Dark; });
    CHECK(std::ranges::all_of(dark, [](const MCShown& frame) { return frame.Fade == MCScreenChange::Black; }));

    // The next screen fades in from its first frame, in the menus' frame.
    const size_t inFrom = out.size() + dark.size();
    const std::span<const MCShown> in =
        RunOf(shown, inFrom, [](const MCShown& frame) { return frame.Phase == MCPhase::In; });
    CheckFade(in, false, "the next screen's fade-in");
    CHECK(std::ranges::all_of(in, [](const MCShown& frame)
                              { return frame.Menus && !frame.Battle && SameColor(frame.Surround, MenuStone); }));
    CheckAfterChange(shown, inFrom + in.size());
}

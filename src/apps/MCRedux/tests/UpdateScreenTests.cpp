#include "stdafx.h"
#include "MCFakeUpdaterBackend.h"
#include "MCTest.h"
#include "RemadeScreens.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCUpdatePage.h"
#include "logistics/MCUpdateScreen.h"
#include "main/MCLogistics.h"
#include "main/MCUpdater.h"
#include "network/MCIntroNote.h"
#include "network/MCRejoinNote.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCPresenter.h"
#include "ui/MCRmlChecks.h"

// Port-only (SPEC-menus.md's stage 6, "UPDATE"): the UPDATE page remade, opened from the main menu's UPDATE through the
// screens' flow; its words for each of the updater's states, its buttons, and its way back.

namespace
{
    using MCScreenFlow::MCScreenId;
    using MCTestUpdater::MCFakeUpdater;
    using MCUpdater::MCAnswer;
    using MCUpdater::MCStage;
    using MCUpdater::MCStatus;
    using MCUpdateWork::MCFailureKind;

    constexpr float FrameSeconds = 1.0f / 15.0f;
    /// <summary>The most frames a change of screen may take (20 seconds).</summary>
    constexpr int32_t MostFrames = 300;
    /// <summary>Frames a change takes to show (the menus' layout, then the presenter's frame).</summary>
    constexpr int32_t SettleFrames = 3;

    constexpr std::string_view NewVersion = "0.2.2 (480)";
    constexpr std::string_view Notes =
        "- Night missions: dusk and night lighting.\n\n- The main menu updates the game.";
    constexpr uint64_t Gigabyte = 1'000'000'000;
    constexpr uint64_t Megabyte = 1'000'000;

    struct MCWindowSize
    {
        int32_t Width;
        int32_t Height;
    };

    /// <summary>A remade screen's recorded frames' windows (SPEC-menus.md, "Testing"): 4:3, 16:9 and 5:4.</summary>
    constexpr std::array<MCWindowSize, 3> FrameWindows = {{{640, 480}, {1280, 720}, {1280, 1024}}};

    void RunFrames(int32_t frames)
    {
        for (int32_t frame = 0; frame < frames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }
    }

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

    /// <summary>The main menu's UPDATE (ORIGINAL CINEMA's place) clicked as a player does.</summary>
    void PressUpdate()
    {
        const std::shared_ptr<MCLogButton> update = GlobalLog()->MainScreen->CinemaButton.Lock();
        REQUIRE(update != nullptr);
        MCScreenInput::RealClick(update->GlobalX() + update->Width() / 2, update->GlobalY() + update->Height() / 2);
        RunWhileChanging();
    }

    /// <summary>The remade UPDATE on top, opened from the main menu through the flow.</summary>
    std::shared_ptr<MCUpdateScreen> OpenUpdate()
    {
        PressUpdate();
        const std::vector<MCScreenId> stack = {MCMenuScreens::Main(), MCMenuScreens::Update()};
        CHECK(StackNow() == stack);
        std::shared_ptr<MCUpdateScreen> update = MCRemadeTest::OnTop<MCUpdateScreen>();
        REQUIRE(update != nullptr);
        CHECK(MCUpdateScreen::Shows());
        CHECK(UpdatePageShown());
        CHECK(!GlobalLog()->MainScreen->IsShowing());
        return update;
    }

    /// <summary>Back on the main menu, alone on the stack.</summary>
    void CheckOnMainMenu()
    {
        RunWhileChanging();
        CHECK(StackNow() == std::vector{MCMenuScreens::Main()});
        CHECK(GlobalLog()->MainScreen->IsShowing());
        CHECK(!MCUpdateScreen::Shows());
    }

    /// <summary>With <c>--shots</c>, the whole window at each recorded frame's window as <c>update_&lt;name&gt;_WxH</c>; checked.</summary>
    void CheckAtFrameWindows(std::string_view name)
    {
        const MCRmlLayer& layer = MCRmlMenus::Running()->get().Menus();

        for (const MCWindowSize& window : FrameWindows)
        {
            REQUIRE(SetWindowPixels(window.Width, window.Height));
            RunFrames(SettleFrames);
            const std::string where = std::format("{} at {}x{}: ", name, window.Width, window.Height);
            CHECK_EQ(where + MCRmlChecks::BoxProblems(layer.Context()) +
                         MCRmlChecks::Illegible(layer.Context(), layer.Unit()),
                     where);

            MCScreenInput::SaveWindowShot(std::format("update_{}_{}x{}", name, window.Width, window.Height));
        }
    }
}

/// <summary>
/// The status line says each of the updater's states in the game's words, in sentence case, the version without its
/// build; a failure by its kind: the server out of reach, the disk's room (what's needed and what's free), a damaged
/// download, else the updater's own sentence.
/// </summary>
TEST_CASE("update screen: the status line says each state in the game's words")
{
    MCUpdater::SetCanStartAgain(true);
    const auto text = [](MCStatus status)
    {
        status.Version = std::string(NewVersion);
        return MCUpdateScreen::StatusText(status);
    };

    CHECK_EQ(text({.Stage = MCStage::None}), "");
    CHECK_EQ(text({.Stage = MCStage::Found}), "A newer version is out.");
    CHECK_EQ(text({.Stage = MCStage::Found, .Expected = 74 * Gigabyte / 10}),
             "A newer version is out: 7.4 GB to download.");
    CHECK_EQ(text({.Stage = MCStage::Downloading, .Received = 31 * Gigabyte / 10, .Expected = 74 * Gigabyte / 10}),
             "Downloading: 3.1 GB of 7.4 GB.");
    CHECK_EQ(text({.Stage = MCStage::Ready, .GoesIn = MCUpdater::MCGoesIn::NextStart}),
             "Version 0.2.2 is in. Restart to play it.");
    CHECK_EQ(text({.Stage = MCStage::Ready, .GoesIn = MCUpdater::MCGoesIn::AsItQuits}),
             "Version 0.2.2 installs as you quit.");
    CHECK_EQ(text({.Stage = MCStage::Ready, .GoesIn = MCUpdater::MCGoesIn::PlayersStart}),
             "Version 0.2.2 installs when you start Redux HD yourself.");
    CHECK_EQ(text({.Stage = MCStage::Installing}), "Installing. The game will restart by itself.");
    CHECK_EQ(text({.Stage = MCStage::Failed, .Failure = MCFailureKind::Unreachable}),
             "Couldn't reach the update server.");
    CHECK_EQ(text({.Stage = MCStage::Failed,
                   .Failure = MCFailureKind::DiskFull,
                   .NeededBytes = 74 * Gigabyte / 10,
                   .FreeBytes = 21 * Gigabyte / 10}),
             "Not enough space: 7.4 GB needed, 2.1 GB free.");
    CHECK_EQ(text({.Stage = MCStage::Failed, .Failure = MCFailureKind::Damaged}), "The download was damaged.");
    CHECK_EQ(text({.Stage = MCStage::Failed, .Error = "another copy of the game is installing it"}),
             "The update failed: another copy of the game is installing it.");
}

/// <summary>
/// The main menu's UPDATE opens the remade page through the screens' flow (the old main menu under it), named by the
/// version running, the new one's notes a line each; INSTALL and SKIP while it's found; the download's progress over a
/// meter (no STOP for the program's own download); RESTART once it's in, which leaves the next start a note to skip the
/// intro; RETRY after a failure, which looks again; SKIP and BACK (Escape) go back to the main menu. Each state laid out
/// and readable at the recorded windows (with <c>--shots</c>, saved).
/// </summary>
TEST_CASE_ISOLATED("game: UPDATE opens remade through the flow: INSTALL, the download, RESTART, SKIP, RETRY and BACK")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs keptPrefs;
    MCFakeUpdater updater;
    GRenderer = static_cast<int>(MCRendererKind::Software);
    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();
    MCUpdater::Found(std::string(NewVersion), std::string(Notes));
    RunFrames(SettleFrames);

    std::shared_ptr<MCUpdateScreen> update = OpenUpdate();
    CHECK_EQ(MCRemadeTest::Words(*update, "running"),
             std::format("Running {}", ReleaseOf(MCTestUpdater::FakeRunningVersion)));
    CHECK_EQ(MCRemadeTest::Words(*update, "version"), "Version 0.2.2");
    CHECK_EQ(MCRemadeTest::Words(*update, "brings"), "What 0.2.2 brings");
    CHECK_EQ(MCRemadeTest::Words(*update, "changes"), "2 changes");
    CHECK_EQ(MCRemadeTest::Words(*update, "status"), "A newer version is out.");
    CHECK(MCRemadeTest::Visible(*update, "act"));
    CHECK_EQ(MCRemadeTest::Words(*update, "act"), "Install");
    CHECK_EQ(MCRemadeTest::Words(*update, "second"), "Skip");
    CheckAtFrameWindows("found");

    // INSTALL: the download under way, its meter; the program's own download has no STOP.
    REQUIRE(MCRemadeTest::Press(*update, "act"));
    REQUIRE_EQ(updater.Log->Answers.size(), size_t{1});
    CHECK(updater.Log->Answers[0] == MCAnswer::Install);
    MCUpdater::Downloading(Megabyte * 3 / 2, Megabyte * 5);
    RunFrames(SettleFrames);
    CHECK_EQ(MCRemadeTest::Words(*update, "status"), "Downloading: 1.5 MB of 5.0 MB.");
    CHECK(MCRemadeTest::Visible(*update, "meter"));
    CHECK(!MCRemadeTest::Visible(*update, "act"));
    CHECK(!MCRemadeTest::Visible(*update, "second"));
    CheckAtFrameWindows("downloading");

    // In: RESTART.
    updater.Log->GoesIn = MCUpdater::MCGoesIn::NextStart;
    MCUpdater::Ready();
    RunFrames(SettleFrames);
    CHECK_EQ(MCRemadeTest::Words(*update, "status"), "Version 0.2.2 is in. Restart to play it.");
    CHECK_EQ(MCRemadeTest::Words(*update, "act"), "Restart");
    CHECK(!MCRemadeTest::Visible(*update, "meter"));
    CheckAtFrameWindows("in");

    // BACK (Escape) leaves it waiting; RESTART installs it now, and the next start skips the intro.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    CheckOnMainMenu();
    CHECK(MCUpdater::Status().Stage == MCStage::Ready);
    update = OpenUpdate();
    REQUIRE(MCRemadeTest::Press(*update, "act"));
    REQUIRE_EQ(updater.Log->Answers.size(), size_t{2});
    CHECK(updater.Log->Answers[1] == MCAnswer::Install);
    CHECK(MCUpdater::Status().Stage == MCStage::Installing);
    CHECK(MCIntroNote::TakeAtStart(MCTestGame::UserFolder(), MCRejoinNote::Now()));
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    CheckOnMainMenu();

    // SKIP: this version isn't offered again, the main menu again.
    MCUpdater::Found("0.2.3 (490)", "More.");
    RunFrames(SettleFrames);
    update = OpenUpdate();
    REQUIRE(MCRemadeTest::Press(*update, "second"));
    REQUIRE_EQ(updater.Log->Answers.size(), size_t{3});
    CHECK(updater.Log->Answers[2] == MCAnswer::Skip);
    CheckOnMainMenu();

    // A failed download: RETRY looks again, then downloads.
    MCUpdater::Found("0.2.4 (500)", "Even more.");
    MCUpdater::Answer(MCAnswer::Install);
    MCUpdater::Failed("the download was cut off");
    RunFrames(SettleFrames);
    update = OpenUpdate();
    CHECK_EQ(MCRemadeTest::Words(*update, "status"), "The update failed: the download was cut off.");
    CHECK_EQ(MCRemadeTest::Words(*update, "act"), "Retry");
    CHECK(!MCRemadeTest::Visible(*update, "second"));
    CheckAtFrameWindows("failed");
    const size_t checks = updater.Log->Checks.size();
    REQUIRE(MCRemadeTest::Press(*update, "act"));
    CHECK_EQ(updater.Log->Checks.size(), checks + 1);
    CHECK(MCUpdater::Status().Stage == MCStage::Downloading);
    // The fake's download can't be stopped (it stages no content): no act once the screen has followed it.
    RunFrames(SettleFrames);
    CHECK(!MCRemadeTest::Visible(*update, "act"));
    REQUIRE(MCRemadeTest::Press(*update, "back"));
    CheckOnMainMenu();
}

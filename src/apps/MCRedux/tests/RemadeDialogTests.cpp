#include "stdafx.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "RemadeScreens.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCScreenChange.h"
#include "lib/MCFatal.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCMenuScreens.h"
#include "main/MCLogistics.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "ui/MCRmlChecks.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlSystem.h"

// Port-only (SPEC-menus.md's stage 9): every dialog the game said in the 1998 box says itself in the remade one
// (ui/dialog.rml): with no remade screen under it, and over the battlefield at the mission's INTERFACE SIZE (the 1998
// box is gone); where the menus can't go on, the game stops with the start check's words (MCRmlFlow::Broken).

namespace
{
    constexpr float FrameSeconds = 1.0f / 15.0f;
    constexpr int32_t SettleFrames = 3;
    constexpr int32_t MostFrames = 300;

    struct MCWindowSize
    {
        int32_t Width;
        int32_t Height;
    };

    /// <summary>A remade screen's recorded frames' windows (SPEC-menus.md, "Testing"): 4:3, 16:9 and 5:4.</summary>
    constexpr std::array<MCWindowSize, 3> FrameWindows = {{{640, 480}, {1280, 720}, {1280, 1024}}};

    /// <summary>A message in the 1998 dialog's capitals, and the remade dialog's sentence for it.</summary>
    constexpr std::string_view OldMessage = "THE GAME SAYS SOMETHING.";
    constexpr std::string_view SaidMessage = "The game says something.";
    /// <summary>The version box's words in the remade dialog, before the version.</summary>
    constexpr std::string_view VersionWords = "Release version: ";

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
    }

    MCRmlMenus& Menus()
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlMenus::Running();
        REQUIRE(menus.has_value());
        return menus->get();
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

    std::string Problems(const MCRmlLayer& layer)
    {
        return MCRmlChecks::BoxProblems(layer.Context()) + MCRmlChecks::Illegible(layer.Context(), layer.Unit());
    }

    /// <summary>
    /// The dialog on <paramref name="layer"/> laid out with nothing spilling or cut and every text over the floor at
    /// the frames' windows (with <c>--shots</c> each saved as <paramref name="name"/>).
    /// </summary>
    void CheckLaidOut(const MCRmlLayer& layer, std::string_view name)
    {
        for (const MCWindowSize& window : FrameWindows)
        {
            REQUIRE(SetWindowPixels(window.Width, window.Height));
            RunFrames(SettleFrames);
            const std::string where = std::format("{}x{}: ", window.Width, window.Height);
            CHECK_EQ(where + Problems(layer), where);
            MCScreenInput::SaveWindowShot(std::format("{}_{}x{}", name, window.Width, window.Height));
        }
    }

    /// <summary>The report the game stopped with, thrown by the fatal hook to end the game in the test, not the process.</summary>
    struct MCStopped
    {
        std::string Report;
    };

    /// <summary>The report the game stops with while <paramref name="run"/> runs (MCFatal's hook); none if it goes on.</summary>
    std::optional<std::string> StoppedBy(const std::function<void()>& run)
    {
        MCFatalHook = [](std::string_view report) { throw MCStopped{std::string(report)}; };
        std::optional<std::string> stopped;

        try
        {
            run();
        }
        catch (const MCStopped& stop)
        {
            stopped = stop.Report;
        }

        MCFatalHook = nullptr;
        return stopped;
    }
}

/// <summary>
/// A message said in logistics with no remade screen showing (each hidden, as a launcher's game shows none before its
/// join) is the remade dialog's, in sentence case, laid out at the frames' windows; its OK (Enter) runs what the message
/// asked.
/// </summary>
TEST_CASE_ISOLATED("game: a message with no remade screen under it is the remade dialog, never the 1998 box")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    // Logistics with nothing remade showing.
    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();
    Menus().Menus().HideAll();
    REQUIRE(!MCRmlFlow::Showing());

    std::optional<int32_t> answered;
    ShowMenuMessage(OldMessage, [&answered](int32_t result) { answered = result; });
    RunFrames(SettleFrames);
    REQUIRE(MCRmlDialogs::Shown());
    CHECK_EQ(MCRmlDialogs::Dialog()->Said().Text, std::string(SaidMessage));
    CHECK(GlobalLog()->Message->IsShowing());
    CheckLaidOut(Menus().Menus(), "dialog_alone");

    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    RunFrames(SettleFrames);
    CHECK(!MCRmlDialogs::Shown());
    CHECK(answered.has_value());
    CHECK(!GlobalLog()->Message->IsShowing());
}

/// <summary>
/// The version box (Ctrl+Alt+V with cheats on) in a mission: the remade dialog over the battlefield, on the mission's
/// layer at its INTERFACE SIZE, laid out at the frames' windows; Enter closes it.
/// </summary>
TEST_CASE_ISOLATED("game: the version box over the battlefield is the remade dialog at the mission's size")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const bool cheats = CheatsOn;
    CheatsOn = true;
    MCScreenInput::RealKeyEvent(SDL_SCANCODE_LCTRL, true, SDL_KMOD_LCTRL);
    MCScreenInput::RealKeyEvent(SDL_SCANCODE_LALT, true, static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_LALT));
    MCScreenInput::RealKey(SDL_SCANCODE_V, static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_LALT));
    MCScreenInput::RealKeyEvent(SDL_SCANCODE_LALT, false, SDL_KMOD_LCTRL);
    MCScreenInput::RealKeyEvent(SDL_SCANCODE_LCTRL, false, SDL_KMOD_NONE);
    CheatsOn = cheats;
    RunFrames(SettleFrames);

    const MCGuiHandle<MCRmlDialog> dialog = MCRmlDialogs::Dialog(MCRmlFlow::MCOn::Mission);
    REQUIRE(dialog);
    REQUIRE(dialog->Shown());
    CHECK(dialog->Said().Text.starts_with(VersionWords));
    CHECK(dialog->Said().Text.size() > VersionWords.size());
    CHECK(!Menus().Menus().Showing());
    CheckLaidOut(Menus().Mission(), "dialog_version");

    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    RunFrames(SettleFrames);
    CHECK(!dialog->Shown());
    CHECK(!MCRmlDialogs::Shown());
}

/// <summary>
/// Port-only (SPEC-menus.md's stage 9): the menus' RmlUi failing once the game is going (here it can't start, another
/// held, after the start's check passed) stops the game as the start's check does: MCFatal with its words and how to
/// repair the copy, never a 1998 screen in the remade one's place.
/// </summary>
TEST_CASE_ISOLATED("game: the menus failing once the game is going stop it with the start check's words")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    std::unique_ptr<MCRmlMenus> held;
    // The game's own RmlUi stopped and another started in its place (RmlUi is one per program): the main menu's screen
    // finds the game's can't start.
    const std::optional<std::string> stopped = StoppedBy(
        [&held]
        {
            static_cast<void>(MCTestGame::StartLogistics(
                [&held]
                {
                    MCRmlFlow::Stop();
                    auto made = MCRmlMenus::Create(MCRmlMenus::MCOptions{});
                    REQUIRE(made.has_value());
                    held = std::move(*made);
                }));
        });
    REQUIRE(stopped.has_value());
    CHECK_EQ(*stopped, MCWithRepair(MCRmlFlow::FilesMissing));
}

/// <summary>
/// Port-only (SPEC-menus.md's stage 9, menus-design's answer on commit 9): a remade screen with no other in its place
/// whose document can't be read stops the game as the start's check does, never showing nothing: here LOAD GAME (a
/// flow's kind, as SAVE GAME, Preferences, UPDATE, LAN and INTERNET are), its saves.rml gone from the menus' folder after
/// the start's check passed.
/// </summary>
TEST_CASE_ISOLATED("game: a remade screen that can't be read stops the game with the start check's words")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view SavesDocument = "saves.rml";
    const MCScratchFolder folder("mc_remade_dialog_tests", "no saves");
    std::filesystem::copy(MCRmlFiles::AppFolder(), folder.Path(), std::filesystem::copy_options::recursive);
    REQUIRE(std::filesystem::remove(folder.Path() / SavesDocument));

    // The game's menus started from that folder in its own one's place, at its window (MCRmlFlow::Menus finds them).
    REQUIRE(MCTestGame::StartLogistics(
        [&folder]
        {
            MCRmlFlow::Stop();
            int width = 0;
            int height = 0;
            SDL_GetWindowSizeInPixels(&MCInput::Display()->Window(), &width, &height);
            const auto started =
                MCRmlMenus::Start(MCRmlMenus::MCOptions{.Width = width, .Height = height, .Folder = folder.Path()});
            REQUIRE_EQ(started.has_value() ? std::string() : started.error(), "");
        }));
    RunWhileChanging();
    REQUIRE(MCRmlFlow::Showing());

    const std::optional<std::string> stopped = StoppedBy(
        []
        {
            REQUIRE(MCScreenFlow::Open(MCMenuScreens::Load()));
            RunWhileChanging();
        });
    REQUIRE(stopped.has_value());
    CHECK_EQ(*stopped, MCWithRepair(MCRmlFlow::FilesMissing));
}

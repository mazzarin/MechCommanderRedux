#include "stdafx.h"
#include "MCTest.h"
#include "RemadeScreens.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "iface/MCKeyBindings.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCPauseScreen.h"
#include "logistics/MCPrefsScreen.h"
#include "logistics/MCQuestionWindow.h"
#include "ui/MCRmlFlow.h"
#include "logistics/MCSettingsModel.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCLogistics.h"
#include "main/MCMissionGlobals.h"
#include "main/MCSystemConfig.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "platform/MCPresenter.h"
#include "sound/MCHdAudio.h"
#include "ui/MCRmlChecks.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlScreen.h"

#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

// Port-only (SPEC-menus.md's stage 6): Preferences remade, opened from the main menu through the screens' flow and from
// the pause in a mission; its sections at the players' windows, from the keys and the mouse; CONTROLS' boxes; the
// pause over the battlefield and its ways out.

namespace
{
    using MCScreenFlow::MCScreenId;
    using MCSettingsModel::MCSection;

    /// <summary>A test frame's length: the game's pace.</summary>
    constexpr float FrameSeconds = 1.0f / 15.0f;
    /// <summary>The most frames a change of screen, or a mission's start or end, may take (20 seconds).</summary>
    constexpr int32_t MostFrames = 300;
    /// <summary>Frames a change takes to show (the menus' layout, then the presenter's frame).</summary>
    constexpr int32_t SettleFrames = 3;

    /// <summary>The look's classes (ui/look.rcss, ui/prefs.rcss).</summary>
    constexpr const char* HeldClass = "held";
    constexpr const char* HiddenClass = "hidden";

    /// <summary>The words a held row's line gives in a mission (logistics/MCSettingsModel).</summary>
    constexpr std::string_view NotInMission = "Not in a mission.";
    constexpr std::string_view BetweenMissions = "Between missions.";

    /// <summary>A mission's result at most this is lost (above it, won: MCTestGame::ReachFirstResults).</summary>
    constexpr uint32_t MostLostResult = 3;
    /// <summary>The result a player who leaves a match gets (1998's "EXIT MULTIPLAYER?" box's).</summary>
    constexpr uint32_t LeftMatchResult = 3;

    /// <summary>The scan code (set 1) of the left Alt key, bound alone.</summary>
    constexpr int16_t LeftAltScanCode = 0x38;

    struct MCWindowSize
    {
        int32_t Width;
        int32_t Height;
    };

    /// <summary>The windows the players' reports name (MenuUiTests.cpp's, 90 days to 19 Nov).</summary>
    constexpr std::array<MCWindowSize, 16> PlayersWindows = {{
        {640, 480},
        {1280, 960},
        {1366, 768},
        {1440, 900},
        {1600, 900},
        {1920, 1080},
        {1920, 1200},
        {2560, 1440},
        {2560, 1920},
        {3440, 1440},
        {3456, 2168},
        {3600, 2024},
        {3600, 2260},
        {3840, 1600},
        {3840, 2160},
        {3840, 2410},
    }};

    /// <summary>A remade screen's recorded frames' windows (SPEC-menus.md, "Testing"): 4:3, 16:9 and 5:4.</summary>
    constexpr std::array<MCWindowSize, 3> FrameWindows = {{{640, 480}, {1280, 720}, {1280, 1024}}};

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

        // A change queued behind the last starts the frame after it.
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

    Rml::Element& Find(const MCRmlScreen& screen, const std::string& id)
    {
        Rml::Element* element = MCRemadeTest::Find(screen, id);

        if (element == nullptr)
        {
            FAIL("the screen has no #" + id);
        }

        return *element;
    }

    /// <summary>The words <paramref name="stepper"/> shows between its arrows.</summary>
    std::string StepWords(Rml::Element& stepper)
    {
        Rml::ElementList parts;
        stepper.GetElementsByClassName(parts, "step-value");
        return parts.size() == 1 ? parts.front()->GetInnerRML() : std::string();
    }

    /// <summary>A mouse event at window pixel (<paramref name="x"/>, <paramref name="y"/>) as SDL sends it, then a frame.</summary>
    void SendMouse(SDL_EventType type, float x, float y, uint8_t button = SDL_BUTTON_LEFT)
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
            event.button.button = button;
            event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.clicks = 1;
            event.button.x = x / density;
            event.button.y = y / density;
        }

        MCInput::HandleEvent(event);
        MCTestGame::RunFrame(FrameSeconds);
    }

    /// <summary>A player's click (<paramref name="button"/>) in the middle of <paramref name="element"/>.</summary>
    void ClickOn(Rml::Element& element, uint8_t button = SDL_BUTTON_LEFT)
    {
        if (!element.IsVisible(true))
        {
            FAIL("#" + element.GetId() + " isn't shown to click");
        }

        const Rml::Vector2f middle =
            element.GetAbsoluteOffset(Rml::BoxArea::Border) + element.GetBox().GetSize(Rml::BoxArea::Border) / 2.0f;
        SendMouse(SDL_EVENT_MOUSE_MOTION, middle.x, middle.y);
        SendMouse(SDL_EVENT_MOUSE_BUTTON_DOWN, middle.x, middle.y, button);
        SendMouse(SDL_EVENT_MOUSE_BUTTON_UP, middle.x, middle.y, button);
    }

    /// <summary>
    /// What is wrong with <paramref name="layer"/>'s shown documents at the window: boxes outside it or spilling, and
    /// texts under the floor or without their contrast at the layer's unit; empty: nothing.
    /// </summary>
    std::string Problems(const MCRmlLayer& layer)
    {
        return MCRmlChecks::BoxProblems(layer.Context()) + MCRmlChecks::Illegible(layer.Context(), layer.Unit());
    }

    /// <summary><paramref name="words"/> in lower case (a section's name in a picture's name).</summary>
    std::string Lower(std::string_view words)
    {
        std::string lower(words);
        std::ranges::transform(lower, lower.begin(),
                               [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        return lower;
    }

    /// <summary>Preferences opened from the main menu through the flow (its button's way); the screen.</summary>
    std::shared_ptr<MCPrefsScreen> OpenPreferences()
    {
        MCScreenFlow::Open(MCMenuScreens::Preferences());
        RunWhileChanging();
        const std::vector<MCScreenId> stack = {MCMenuScreens::Main(), MCMenuScreens::Preferences()};
        CHECK(StackNow() == stack);
        std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OnTop<MCPrefsScreen>();
        REQUIRE(prefs != nullptr);
        CHECK(prefs->Place() == MCPrefsScreen::MCPlace::Menus);
        CHECK(!GlobalLog()->MainScreen->IsShowing());
        return prefs;
    }

    /// <summary>Escape pressed and let go, as the player does it.</summary>
    void PressEscape()
    {
        MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    }

    /// <summary>Back on the main menu (the old one until stage 7), the main menu alone on the stack.</summary>
    void CheckOnMainMenu()
    {
        RunWhileChanging();
        CHECK(StackNow() == std::vector{MCMenuScreens::Main()});
        CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->MainScreen);
        CHECK(GlobalLog()->MainScreen->IsShowing());
        CHECK(!Menus().Menus().Showing());
    }

    /// <summary>A campaign's first mission up and past its start, so Escape reaches the battle's own keys.</summary>
    void StartBattle()
    {
        GRenderer = static_cast<int>(MCRendererKind::Software);
        REQUIRE(MCTestGame::StartLogistics());
        REQUIRE(MCTestGame::StartCampaignMission());

        for (int32_t frame = 0; frame < MostFrames && Turn <= Scenario()->StartUpTurns; frame++)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }

        REQUIRE(Turn > Scenario()->StartUpTurns);
    }

    std::shared_ptr<MCPauseScreen> ThePause()
    {
        return MCRemadeTest::OnTop<MCPauseScreen>(MCRemadeTest::MCLayer::Mission);
    }
}

/// <summary>
/// The main menu's PREFERENCES opens the remade Preferences through the screens' flow (the old main menu under it):
/// each section laid out with nothing spilling or cut, and every text over the floor and its contrast, at every window
/// the players' reports name; Escape goes back to the main menu. With <c>--shots</c> each section is saved at the
/// recorded frames' windows.
/// </summary>
TEST_CASE_ISOLATED("game: Preferences opens through the flow and reads in every section at the players' windows")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs keptPrefs;
    GRenderer = static_cast<int>(MCRendererKind::Software);
    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();
    const std::shared_ptr<MCPrefsScreen> prefs = OpenPreferences();
    const MCRmlLayer& layer = Menus().Menus();

    for (const MCWindowSize& window : PlayersWindows)
    {
        REQUIRE(SetWindowPixels(window.Width, window.Height));
        const bool recorded =
            std::ranges::any_of(FrameWindows, [&window](const MCWindowSize& frame)
                                { return frame.Width == window.Width && frame.Height == window.Height; });

        for (const MCSection section : MCSettingsModel::Sections)
        {
            prefs->ShowSection(section);
            RunFrames(SettleFrames);
            const std::string where =
                std::format("{}x{}, {}: ", window.Width, window.Height, MCSettingsModel::SectionName(section));
            CHECK_EQ(where + Problems(layer), where);

            if (recorded)
            {
                MCScreenInput::SaveWindowShot(std::format(
                    "prefs_{}_{}x{}", Lower(MCSettingsModel::SectionName(section)), window.Width, window.Height));
            }
        }
    }

    for (const MCWindowSize& window : FrameWindows)
    {
        REQUIRE(SetWindowPixels(window.Width, window.Height));
        RunFrames(SettleFrames);
        CHECK_EQ(Problems(layer), "");
    }

    PressEscape();
    CheckOnMainMenu();
}

/// <summary>
/// From the keys: Q and E change section (round from the last to the first), Down moves the focus to the next row, Right
/// changes the row's setting at once; Escape puts back what Preferences opened with. ACCEPT (clicked) keeps a change.
/// </summary>
TEST_CASE_ISOLATED("game: Preferences from the keys: sections, rows, Escape puts back, ACCEPT keeps")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs keptPrefs;
    GRenderer = static_cast<int>(MCRendererKind::Software);
    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();
    const int scrollSpeed = GScrollSpeed;
    {
        const std::shared_ptr<MCPrefsScreen> prefs = OpenPreferences();
        CHECK(prefs->Section() == MCSection::Game);
        MCScreenInput::RealKey(SDL_SCANCODE_E);
        CHECK(prefs->Section() == MCSection::Display);
        MCScreenInput::RealKey(SDL_SCANCODE_Q);
        MCScreenInput::RealKey(SDL_SCANCODE_Q);
        CHECK(prefs->Section() == MCSettingsModel::Sections.back());
        MCScreenInput::RealKey(SDL_SCANCODE_E);
        MCScreenInput::RealKey(SDL_SCANCODE_E);
        CHECK(prefs->Section() == MCSection::Display);

        // The scroll speed's stepper, the section's last row: Up goes to the row above (the arrows stay in the rows),
        // Tab to the buttons at the foot, and Right on it changes the speed as it moves.
        Rml::Element& speed = Find(*prefs, "scroll-speed");
        speed.Focus();
        MCScreenInput::RealKey(SDL_SCANCODE_UP);
        CHECK(Menus().Menus().Context().GetFocusElement() == &Find(*prefs, "renderer"));
        speed.Focus();
        MCScreenInput::RealKey(SDL_SCANCODE_TAB);
        CHECK(Menus().Menus().Context().GetFocusElement() == &Find(*prefs, "cancel"));
        const std::string speedWords = StepWords(speed);
        CHECK(!speedWords.empty());
        speed.Focus();
        MCScreenInput::RealKey(SDL_SCANCODE_RIGHT);
        CHECK(GScrollSpeed != scrollSpeed);
        CHECK(!StepWords(speed).empty());
        CHECK(StepWords(speed) != speedWords);
        PressEscape();
        CheckOnMainMenu();
        CHECK_EQ(GScrollSpeed, scrollSpeed);
    }

    const std::shared_ptr<MCPrefsScreen> prefs = OpenPreferences();
    prefs->ShowSection(MCSection::Display);
    RunFrames(SettleFrames);
    Find(*prefs, "scroll-speed").Focus();
    MCScreenInput::RealKey(SDL_SCANCODE_RIGHT);
    const int changed = GScrollSpeed;
    CHECK(changed != scrollSpeed);
    ClickOn(Find(*prefs, "accept"));
    CheckOnMainMenu();
    CHECK_EQ(GScrollSpeed, changed);
}

/// <summary>
/// CONTROLS: a click on a key's box waits for a key (PRESS A KEY) and takes the next one, whatever it is; a modifier let
/// go alone binds alone; Escape stops the wait without leaving Preferences; a right-click clears a box; LAPTOP sets
/// 1998's laptop keys; CANCEL leaves the player's keys as they were.
/// </summary>
TEST_CASE_ISOLATED("game: CONTROLS' boxes take the next key, a modifier alone, and a right-click clears one")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs keptPrefs;
    GRenderer = static_cast<int>(MCRendererKind::Software);
    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();
    const MCKeyBindings::MCKeyTable before = MCKeyBindings::Player();
    const std::shared_ptr<MCPrefsScreen> prefs = OpenPreferences();
    prefs->ShowSection(MCSection::Controls);
    RunFrames(SettleFrames);

    const auto row = std::ranges::find_if(MCKeyBindings::ControlRows(),
                                          [](const MCKeyBindings::MCControlRow& found) { return !found.IsHeading(); });
    REQUIRE(row != MCKeyBindings::ControlRows().end());
    const auto command = static_cast<size_t>(row->Command);
    const std::string boxId = std::format("key-{}", command);

    ClickOn(Find(*prefs, boxId));
    CHECK(prefs->Binding());
    MCScreenInput::RealKey(SDL_SCANCODE_K);
    CHECK(!prefs->Binding());
    CHECK_EQ(MCKeyBindings::KeyName(prefs->EditedKeys()[command]), "K");

    ClickOn(Find(*prefs, boxId));
    REQUIRE(prefs->Binding());
    MCScreenInput::RealKeyEvent(SDL_SCANCODE_LALT, true, SDL_KMOD_LALT);
    MCScreenInput::RealKeyEvent(SDL_SCANCODE_LALT, false, SDL_KMOD_NONE);
    CHECK(!prefs->Binding());
    CHECK_EQ(prefs->EditedKeys()[command], MCKeyBindings::FromKeyEvent(LeftAltScanCode, false, false, false));

    // Escape while a box waits is the wait's: Preferences stays.
    ClickOn(Find(*prefs, boxId));
    REQUIRE(prefs->Binding());
    const uint32_t kept = prefs->EditedKeys()[command];
    PressEscape();
    CHECK(!prefs->Binding());
    CHECK_EQ(prefs->EditedKeys()[command], kept);
    CHECK(MCRemadeTest::OnTop<MCPrefsScreen>() == prefs);
    CHECK(MCScreenFlow::Shown() == MCMenuScreens::Preferences());

    ClickOn(Find(*prefs, boxId), SDL_BUTTON_RIGHT);
    CHECK_EQ(prefs->EditedKeys()[command], MCKeyBindings::NoKey);

    ClickOn(Find(*prefs, "laptop"));
    CHECK(prefs->EditedKeys() == MCKeyBindings::Laptop());

    ClickOn(Find(*prefs, "cancel"));
    CheckOnMainMenu();
    CHECK(MCKeyBindings::Player() == before);
}

/// <summary>
/// HD AUDIO's ORCHESTRAL without the pack's orchestral music plays the stereo music, as ON does: the switch shows ON,
/// and the player's ORCHESTRAL stays theirs (a screen opened and cancelled, or ON picked again, leaves it) until they
/// pick another.
/// </summary>
TEST_CASE_ISOLATED("game: ORCHESTRAL without orchestral music shows as ON and stays the player's through CANCEL")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs keptPrefs;
    GRenderer = static_cast<int>(MCRendererKind::Software);
    REQUIRE(MCTestGame::StartLogistics());
    RunWhileChanging();
    REQUIRE(!MCHdAudio::HasOrchestral());
    const MCOptionalRef<const MCSettingsModel::MCSetting> audio = MCSettingsModel::Find("hd-audio");
    REQUIRE(audio.has_value());
    GHdAudio = HdAudioOrchestral;
    CHECK_EQ(audio->get().Get(), int{HdAudioOn});

    const std::shared_ptr<MCPrefsScreen> prefs = OpenPreferences();
    CHECK_EQ(GHdAudio, int{HdAudioOrchestral});
    audio->get().Set(HdAudioOn);
    CHECK_EQ(GHdAudio, int{HdAudioOrchestral});
    ClickOn(Find(*prefs, "cancel"));
    CheckOnMainMenu();
    CHECK_EQ(GHdAudio, int{HdAudioOrchestral});

    audio->get().Set(HdAudioOff);
    CHECK_EQ(GHdAudio, int{HdAudioOff});
}

/// <summary>
/// Escape in a single player mission opens the pause over the battlefield (its interface size; nothing spilling, every
/// text readable at the recorded windows), the mission waiting behind it; Escape and RESUME go back to the battle. ALL
/// PREFERENCES opens Preferences over the battlefield with the rows a mission can't take held, saying why, and Escape
/// comes back to the pause. ABORT MISSION ends the mission as 1998's Enter while paused did: its results a loss, then
/// the same mission's briefing, the main menu under it.
/// </summary>
TEST_CASE_ISOLATED("game: Escape in a mission opens the pause; RESUME, ALL PREFERENCES and ABORT MISSION")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs keptPrefs;
    const MCTestGame::SaveGameCleanup saves;
    StartBattle();

    PressEscape();
    REQUIRE(MCPauseScreen::Shows());
    std::shared_ptr<MCPauseScreen> pause = ThePause();
    REQUIRE(pause != nullptr);
    CHECK(GamePaused);
    CHECK_EQ(Find(*pause, "name").GetInnerRML(), "Paused");
    CHECK_EQ(Find(*pause, "leave").GetInnerRML(), "Abort mission");
    CHECK(!StepWords(Find(*pause, "scroll-speed")).empty());
    const int32_t turn = Turn;
    RunFrames(SettleFrames);
    CHECK_EQ(Turn, turn);

    for (const MCWindowSize& window : FrameWindows)
    {
        REQUIRE(SetWindowPixels(window.Width, window.Height));
        RunFrames(SettleFrames);
        CHECK_EQ(std::format("{}x{}: {}", window.Width, window.Height, Problems(Menus().Mission())),
                 std::format("{}x{}: ", window.Width, window.Height));
        MCScreenInput::SaveWindowShot(std::format("pause_{}x{}", window.Width, window.Height));
    }

    PressEscape();
    CHECK(!MCPauseScreen::Shows());
    CHECK(!GamePaused);
    RunFrames(SettleFrames);
    CHECK(Turn > turn);

    PressEscape();
    pause = ThePause();
    REQUIRE(pause != nullptr);
    ClickOn(Find(*pause, "resume"));
    CHECK(!MCPauseScreen::Shows());
    CHECK(!GamePaused);

    // ALL PREFERENCES: over the battlefield, the pause hidden, the held rows saying why.
    PressEscape();
    pause = ThePause();
    REQUIRE(pause != nullptr);
    ClickOn(Find(*pause, "all"));
    const std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OnTop<MCPrefsScreen>(MCRemadeTest::MCLayer::Mission);
    REQUIRE(prefs != nullptr);
    CHECK(prefs->Place() == MCPrefsScreen::MCPlace::Mission);
    CHECK(!pause->Shown());
    CHECK(GamePaused);

    prefs->ShowSection(MCSection::Display);
    RunFrames(SettleFrames);
    CHECK(Find(*prefs, "row-renderer").IsClassSet(HeldClass));
    CHECK_EQ(Find(*prefs, "line-renderer").GetInnerRML(), NotInMission);
    prefs->ShowSection(MCSection::Game);
    RunFrames(SettleFrames);
    CHECK(Find(*prefs, "row-difficulty").IsClassSet(HeldClass));
    CHECK_EQ(Find(*prefs, "line-difficulty").GetInnerRML(), BetweenMissions);

    for (const MCWindowSize& window : FrameWindows)
    {
        REQUIRE(SetWindowPixels(window.Width, window.Height));

        for (const MCSection section : {MCSection::Game, MCSection::Display})
        {
            prefs->ShowSection(section);
            RunFrames(SettleFrames);
            const std::string where =
                std::format("{}x{}, {}: ", window.Width, window.Height, MCSettingsModel::SectionName(section));
            CHECK_EQ(where + Problems(Menus().Mission()), where);
            MCScreenInput::SaveWindowShot(std::format(
                "prefs_mission_{}_{}x{}", Lower(MCSettingsModel::SectionName(section)), window.Width, window.Height));
        }
    }

    PressEscape();
    CHECK(MCRemadeTest::OnTop<MCPauseScreen>(MCRemadeTest::MCLayer::Mission) == pause);
    CHECK(pause->Shown());
    CHECK(GamePaused);

    // ABORT MISSION: the results as a loss, then the mission's briefing again (REPLAY), the main menu under it.
    ClickOn(Find(*pause, "leave"));
    CHECK(!MCPauseScreen::Shows());
    CHECK(!GamePaused);

    for (int32_t frame = 0; frame < MostFrames && Mission()->State != MCMissionState::Results; frame++)
    {
        MCTestGame::RunFrame(FrameSeconds);
    }

    REQUIRE(Mission()->State == MCMissionState::Results);
    CHECK(ScenarioResult <= MostLostResult);
    RunWhileChanging();
    Mission()->LeaveResults();
    RunWhileChanging();
    REQUIRE(GlobalLog() != nullptr);
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->BriefingScreen);
    CHECK_EQ(GlobalLog()->CurrentMission, 0);
    const std::vector<MCScreenId> landed = {MCMenuScreens::Main(), MCMenuScreens::Campaign()};
    CHECK(StackNow() == landed);
    CHECK_EQ(MCScreenFlow::Unrouted(), 0u);
    CHECK(!Menus().Mission().Showing());
}

/// <summary>
/// A key changed in CONTROLS over the battlefield (Escape, ALL PREFERENCES, CONTROLS, ACCEPT) works as the battle
/// resumes: the battle's own copy of the keys, taken as the mission starts, is taken again as Preferences keeps them.
/// </summary>
TEST_CASE_ISOLATED("game: a key changed from the pause works as the battle resumes")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs keptPrefs;
    const MCTestGame::SaveGameCleanup saves;
    StartBattle();
    PressEscape();
    const std::shared_ptr<MCPauseScreen> pause = ThePause();
    REQUIRE(pause != nullptr);
    ClickOn(Find(*pause, "all"));
    const std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OnTop<MCPrefsScreen>(MCRemadeTest::MCLayer::Mission);
    REQUIRE(prefs != nullptr);
    prefs->ShowSection(MCSection::Controls);
    RunFrames(SettleFrames);

    const auto row = std::ranges::find_if(MCKeyBindings::ControlRows(),
                                          [](const MCKeyBindings::MCControlRow& found) { return !found.IsHeading(); });
    REQUIRE(row != MCKeyBindings::ControlRows().end());
    const auto command = static_cast<size_t>(row->Command);
    ClickOn(Find(*prefs, std::format("key-{}", command)));
    REQUIRE(prefs->Binding());
    MCScreenInput::RealKey(SDL_SCANCODE_K);
    const uint32_t changed = prefs->EditedKeys()[command];
    CHECK_EQ(MCKeyBindings::KeyName(changed), "K");
    const std::shared_ptr<MCTacticalInterface> tactical = TacticalInterface();
    REQUIRE(tactical != nullptr);
    REQUIRE(tactical->Key(row->Command) != changed);

    ClickOn(Find(*prefs, "accept"));
    RunFrames(SettleFrames);
    REQUIRE(ThePause() == pause);
    ClickOn(Find(*pause, "resume"));
    CHECK(!MCPauseScreen::Shows());
    CHECK(!GamePaused);
    CHECK_EQ(tactical->Key(row->Command), changed);
}

/// <summary>
/// A match's pause (MCPauseScreen for multiplayer, made as Escape in a match makes it): MENU, the battle playing on
/// behind it; LEAVE MATCH asks "Exit multiplayer?" first, CANCEL (Escape) keeps the pause, EXIT leaves the match as
/// 1998's box did (the mission ends with the leaver's result).
/// </summary>
TEST_CASE_ISOLATED("game: a match's pause: the battle goes on, and LEAVE MATCH asks first")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs keptPrefs;
    const MCTestGame::SaveGameCleanup saves;
    StartBattle();
    const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();
    REQUIRE(menus.has_value());
    const MCGuiHandle<MCPauseScreen> pause = menus->get().Mission().CreateChild<MCPauseScreen>(
        [](MCPauseScreen& made) { made.Back = [&made] { made.Resume(); }; }, true);
    REQUIRE(pause->Open().has_value());
    const std::shared_ptr<MCPauseScreen> shown = pause.Lock();
    REQUIRE(shown != nullptr);
    RunFrames(SettleFrames);
    CHECK_EQ(Find(*shown, "name").GetInnerRML(), "Menu");
    CHECK_EQ(Find(*shown, "leave").GetInnerRML(), "Leave match");
    CHECK(!GamePaused);
    const int32_t turn = Turn;
    RunFrames(SettleFrames);
    CHECK(Turn > turn);

    for (const MCWindowSize& window : FrameWindows)
    {
        REQUIRE(SetWindowPixels(window.Width, window.Height));
        RunFrames(SettleFrames);
        CHECK_EQ(std::format("{}x{}: {}", window.Width, window.Height, Problems(Menus().Mission())),
                 std::format("{}x{}: ", window.Width, window.Height));
        MCScreenInput::SaveWindowShot(std::format("pause_mp_{}x{}", window.Width, window.Height));
    }

    ClickOn(Find(*shown, "leave"));
    std::shared_ptr<MCQuestionWindow> question = MCRemadeTest::OnTop<MCQuestionWindow>(MCRemadeTest::MCLayer::Mission);
    REQUIRE(question != nullptr);
    CHECK_EQ(Find(*question, "heading").GetInnerRML(), "Exit multiplayer?");
    CHECK_EQ(Find(*question, "yes").GetInnerRML(), "Exit");
    CHECK(Find(*question, "eyebrow").IsClassSet(HiddenClass));
    PressEscape();
    CHECK(MCRemadeTest::OnTop<MCPauseScreen>(MCRemadeTest::MCLayer::Mission) == shown);
    CHECK_EQ(Mission()->EndScenarioRequested, 0);

    ClickOn(Find(*shown, "leave"));
    question = MCRemadeTest::OnTop<MCQuestionWindow>(MCRemadeTest::MCLayer::Mission);
    REQUIRE(question != nullptr);
    ClickOn(Find(*question, "yes"));
    CHECK(!Menus().Mission().Showing());
    CHECK_EQ(Mission()->EndScenarioRequested, -1);
    CHECK_EQ(ScenarioResult, LeftMatchResult);
}

/// <summary>
/// A match that ends while something is open over its pause: Preferences' changes, not yet accepted, are put back and
/// never kept (the pause's own quick change is), and LEAVE MATCH's question goes with the pause, so nothing holds the
/// keys over the results.
/// </summary>
TEST_CASE_ISOLATED("game: a match that ends under its pause keeps the pause's change, not Preferences', and leaves "
                   "no window")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs keptPrefs;
    const MCTestGame::SaveGameCleanup saves;
    StartBattle();
    const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();
    REQUIRE(menus.has_value());
    const auto openMatchPause = [&menus]
    {
        const MCGuiHandle<MCPauseScreen> made = menus->get().Mission().CreateChild<MCPauseScreen>(
            [](MCPauseScreen& pause) { pause.Back = [&pause] { pause.Resume(); }; }, true);
        REQUIRE(made->Open().has_value());
        RunFrames(SettleFrames);
        return made.Lock();
    };

    // The pause's scroll speed changed (kept as it closes), then Preferences' changed again, not accepted.
    std::shared_ptr<MCPauseScreen> pause = openMatchPause();
    REQUIRE(pause != nullptr);
    Find(*pause, "scroll-speed").Focus();
    MCScreenInput::RealKey(SDL_SCANCODE_RIGHT);
    const int quick = GScrollSpeed;
    ClickOn(Find(*pause, "all"));
    const std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OnTop<MCPrefsScreen>(MCRemadeTest::MCLayer::Mission);
    REQUIRE(prefs != nullptr);
    prefs->ShowSection(MCSection::Display);
    RunFrames(SettleFrames);
    Find(*prefs, "scroll-speed").Focus();
    MCScreenInput::RealKey(SDL_SCANCODE_RIGHT);
    CHECK(GScrollSpeed != quick);

    // The match ends: MissionEnding resumes the pause it opened (this test opens a match's pause itself).
    pause->Resume();
    RunFrames(SettleFrames);
    CHECK(!Menus().Mission().Showing());
    CHECK_EQ(GScrollSpeed, quick);
    {
        MCFitIniFile kept;
        REQUIRE_EQ(kept.Open(MCFileSystem::ToUtf8(MCFileSystem::UserRoot() / PrefsFile)), 0);
        ReadPreferences(kept);
    }

    CHECK_EQ(GScrollSpeed, quick);

    // LEAVE MATCH's question up as the match ends: it goes with the pause.
    pause = openMatchPause();
    REQUIRE(pause != nullptr);
    ClickOn(Find(*pause, "leave"));
    REQUIRE(MCRemadeTest::OnTop<MCQuestionWindow>(MCRemadeTest::MCLayer::Mission) != nullptr);
    pause->Resume();
    RunFrames(SettleFrames);
    CHECK(!Menus().Mission().Showing());
}

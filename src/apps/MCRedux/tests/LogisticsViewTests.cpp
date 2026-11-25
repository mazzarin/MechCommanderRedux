#include "stdafx.h"
#include "MCTest.h"
#include "RemadeScreens.h"
#include "ScreenInput.h"
#include "GuiTree.h"
#include "TestGame.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCPurchaseDlg.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCGameList.h"
#include "logistics/MCLogComboBox.h"
#include "logistics/MCLogSlider.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCLogToolButton.h"
#include "logistics/MCLogChatInput.h"
#include "logistics/MCPlayerNameObject.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCLogChatWindow.h"
#include "logistics/MCLogInvScreen.h"
#include "logistics/MCLogObject.h"
#include "main/MCLogistics.h"
#include "main/MCPrefs.h"
#include "platform/MCFileSystem.h"
#include "platform/MCPresenter.h"
#include "sound/MCSoundSystem.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>
    /// The chat history as the original kept it: a picture wiped to 0x10, moved up by each line's height, the strip
    /// along the bottom wiped and the line written there (LogChatWindow::processChatString @ 0x0070b2a0).
    /// </summary>
    void AddLineAsOriginal(MCLogPort& picture, std::string_view line)
    {
        std::string text(line);
        const int32_t width = picture.Width();
        const int32_t height = picture.Height();
        const int32_t used = GuiSystem().TextFormatter.Process(text, std::nullopt, width, 0);
        const std::span<uint8_t> pixels = picture.Bitmap().Bytes();
        std::ranges::copy(
            pixels.subspan(static_cast<size_t>(width * used), static_cast<size_t>((height - used) * width)),
            pixels.begin());
        MCPane bottom = picture.Frame();
        bottom.X0 = 0;
        bottom.Y0 = height - used - 1;
        bottom.X1 = width - 1;
        bottom.Y1 = height - 1;
        VfxPaneWipe(bottom, 0x10);
        GuiSystem().TextFormatter.Process(text, picture, 0, height - used - 1);
    }

    /// <summary>Draws the chat window's history view into a picture of its size (scrolled to its top).</summary>
    std::vector<uint8_t> DrawHistory(MCLogChatWindow& chat)
    {
        std::shared_ptr<MCLogPort> view = chat.HistoryPane->ContentPort;
        MCLogPort picture;
        picture.Init(view->Width(), view->Height());
        VfxPaneWipe(picture.Frame(), 0xff);
        view->OpenView(picture.SharedBitmap(), 0, 0, MCRect{0, 0, view->Width() - 1, view->Height() - 1}, false);
        view->DrawContent(*view);
        view->CloseView();
        const std::span<const uint8_t> pixels =
            picture.Bitmap().Bytes().first(static_cast<size_t>(view->Width()) * view->Height());
        std::vector<uint8_t> result(pixels.begin(), pixels.end());
        return result;
    }
}

/// <summary>
/// Renderer phase 3 step 4g: the logistics chat history, now a list of lines drawn each frame, shows exactly what the
/// original's picture held after the same lines: wrapped and coloured lines, lines pushed off the top, and the history
/// kept across a resize and gone after a reset.
/// </summary>
TEST_CASE_ISOLATED("game: the logistics chat history draws as the original's picture")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    // The session screen's window of the chat.
    REQUIRE(GlobalLog()->Chat != nullptr);
    const std::shared_ptr<MCLogChatWindow> window = GlobalLog()->ChatWindowOn(*GlobalLog()->SessionScreen);
    REQUIRE(window != nullptr);
    MCLogChatWindow& chat = *window;
    MCLogChat& history = *GlobalLog()->Chat;
    std::shared_ptr<MCLogPort> view = chat.HistoryPane->ContentPort;
    REQUIRE(view->IsView());

    MCLogPort original;
    original.Init(view->Width(), view->Height());
    VfxPaneWipe(original.Frame(), 0x10);

    const auto same = [&]
    {
        const std::vector<uint8_t> drawn = DrawHistory(chat);
        return std::ranges::equal(drawn, original.Bitmap().Bytes().first(drawn.size()));
    };

    CHECK(same());

    std::vector<std::string> lines = {
        "%fc4Alpha: %fc6hello",
        "%fc2Bravo: %fc4a line long enough to wrap onto a second line and then onto a third one in the history",
        "%fc5Charlie: %fc6short",
        "%fc3Delta: %fc4%%100 sure",
    };

    // Enough lines to push the first ones off the top.
    for (int32_t i = 0; i < 80; i++)
    {
        lines.push_back(std::format("%fc{}Echo: %fc6message number {} of the flood", i % 8, i));
    }

    for (size_t i = 0; i < lines.size(); i++)
    {
        MCTest::Scope scope(std::format("line {}", i));
        AddLineAsOriginal(original, lines[i]);
        history.AddLine(lines[i].c_str());
        CHECK(same());
    }

    // A resize keeps the history (the original copied its picture into the new pane).
    chat.Resize(0xe7);
    CHECK(chat.HistoryPane->ContentPort->IsView());
    CHECK(same());

    // A reset clears it.
    history.Clear();
    chat.ResetHistoryPane();
    VfxPaneWipe(original.Frame(), 0x10);
    CHECK(same());
    AddLineAsOriginal(original, lines[1]);
    history.AddLine(lines[1].c_str());
    CHECK(same());
}

namespace
{
    /// <summary>
    /// The preferences screen's drop-downs, as AddPreferencesPanel places them: each field 11 tall at (left, top), 104
    /// wide in the right column and 96 in the left, its list's rows 10 tall from top + 11. DIFFICULTY's field is at
    /// (488, 176) (EASY, REGULAR, HARD), RENDERER's at (488, 222) (VULKAN, SOFTWARE), SCROLL SPEED's at (488, 360)
    /// (AUTO, 1.5X to 3X).
    /// </summary>
    struct PrefsDropDown
    {
        int32_t Top = 0;
        int32_t Left = 488;

        int32_t FieldX() const { return Left + 28; }
        int32_t FieldY() const { return Top + 5; }
        int32_t RowY(int32_t row) const { return Top + 11 + row * 10 + 5; }
    };

    constexpr PrefsDropDown DifficultyDropDown{176};
    constexpr PrefsDropDown RendererDropDown{222};

    /// <summary>The drop-down under <paramref name="parent"/> (at any depth) whose field lies at <paramref name="dropDown"/>.</summary>
    std::shared_ptr<MCLogComboBox> FindDropDownIn(MCGuiObject& parent, const PrefsDropDown& dropDown)
    {
        for (const std::shared_ptr<MCGuiObject>& child : parent.Children())
        {
            if (auto combo = std::dynamic_pointer_cast<MCLogComboBox>(child);
                combo != nullptr && combo->GlobalY() == dropDown.Top)
            {
                return combo;
            }

            if (std::shared_ptr<MCLogComboBox> found = FindDropDownIn(*child, dropDown); found != nullptr)
            {
                return found;
            }
        }

        return nullptr;
    }

    /// <summary>The preferences screen's drop-down whose field lies at <paramref name="dropDown"/>.</summary>
    std::shared_ptr<MCLogComboBox> FindDropDown(const PrefsDropDown& dropDown)
    {
        return FindDropDownIn(*GlobalLog()->PrefScreen, dropDown);
    }

    /// <summary>
    /// Whether <paramref name="object"/> draws in front of everything on <paramref name="screen"/>: it, and each of its
    /// parents below the screen, is the last of its parent's children.
    /// </summary>
    bool InFrontOnScreen(MCGuiObject& screen, MCGuiObject& object)
    {
        std::shared_ptr<MCGuiObject> child = object.shared_from_this();

        while (const std::shared_ptr<MCGuiObject> parent = child->ParentObject())
        {
            if (parent->Child(parent->NumberOfChildren() - 1) != child)
            {
                return false;
            }

            if (parent.get() == &screen)
            {
                return true;
            }

            child = parent;
        }

        return false;
    }
}

/// <summary>
/// Preferences' RENDERER: SOFTWARE chosen, its line says it takes a restart (nothing asked); VULKAN back and again
/// changes it back and then nothing; CANCEL puts the old choice back (and the switch shows it when Preferences opens
/// again); ACCEPT writes it to PREFS "Renderer"; the running game keeps its renderer.
/// </summary>
TEST_CASE_ISOLATED("game: the preferences screen chooses the renderer and saves it in PREFS")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCTestGame::FinishChanges());
    // The tests' user folder overlays the install, so a PREFS written here would be every later test's.
    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    std::filesystem::remove(prefsPath);
    const int32_t running = GRenderer;
    const int32_t chosen = GRendererPreference;
    REQUIRE_EQ(chosen, static_cast<int32_t>(MCRendererKind::Vulkan));
    constexpr std::string_view Restart = "Takes a restart.";

    // Another renderer than the running one: its line says it takes a restart, and nothing is asked.
    std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Display);
    REQUIRE(prefs != nullptr);
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "renderer"), "Vulkan");
    CHECK_EQ(MCRemadeTest::Words(*prefs, "line-renderer"), Restart);
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "renderer", SDL_SCANCODE_RIGHT));
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Software));
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "renderer"), "Software");
    CHECK_EQ(MCRemadeTest::Words(*prefs, "line-renderer"), Restart);
    CHECK(!MCRmlDialogs::Shown());

    // VULKAN back (the running renderer); VULKAN again: no change, nothing asked.
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "renderer", SDL_SCANCODE_LEFT));
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Vulkan));
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "renderer"), "Vulkan");
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "renderer", SDL_SCANCODE_LEFT));
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Vulkan));
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "renderer"), "Vulkan");
    CHECK(!MCRmlDialogs::Shown());
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "renderer", SDL_SCANCODE_RIGHT));
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "renderer"), "Software");

    // CANCEL puts the choice back, and the switch shows it when Preferences opens again.
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "cancel"));
    CHECK_EQ(GRendererPreference, chosen);
    prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Display);
    REQUIRE(prefs != nullptr);
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "renderer"), "Vulkan");

    REQUIRE(MCRemadeTest::KeyOn(*prefs, "renderer", SDL_SCANCODE_RIGHT));
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "accept"));
    CHECK_EQ(GRenderer, running);

    char renderer[32] = {};
    {
        MCFitIniFile prefsFile;
        REQUIRE_EQ(prefsFile.Open(MCFileSystem::ToUtf8(prefsPath).c_str()), 0);
        REQUIRE_EQ(prefsFile.SeekBlock("MechCommander"), 0);
        CHECK_EQ(prefsFile.ReadIdString("Renderer", std::span(renderer).first(sizeof(renderer) - 1)), 0);
    }

    std::filesystem::remove(prefsPath);
    CHECK(std::string_view(renderer) == "software");
}

/// <summary>
/// The preferences screen's drop-downs with a player's mouse and keys (SDL events through CheckMouse and the key
/// messages): a click on the field opens the list in front of the screen and takes the mouse, a click on a row chooses
/// it and closes, a press outside closes without a change and goes no further, a drag from the field to a row chooses
/// on the release; up/down and the wheel move the choice of a closed field under the mouse and the lit row of an open
/// list, Return chooses the lit row and Escape closes. Switching back and forth many times keeps working, the restart
/// message shows each time SOFTWARE is chosen and never for the same item again, and the screen looks as it did once
/// everything is back.
/// </summary>
TEST_CASE_ISOLATED("game: the preferences drop-downs work with a player's mouse and keys")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    // The 1998 screens' own test: their 1998 box with them, as where the menus can't start (until stage 9's step 2).
    MCReusableDialog::OldBoxOnly() = true;

    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Vulkan));
    REQUIRE(GRenderer != static_cast<int32_t>(MCRendererKind::Software));
    MCMenuDialog& dialog = *GlobalLog()->Message;
    MCGenericScreen& screen = *GlobalLog()->PrefScreen;
    ShowPreferences();
    MCScreenInput::RealMove(320, 400);
    const std::shared_ptr<MCLogComboBox> renderer = FindDropDown(RendererDropDown);
    const std::shared_ptr<MCLogComboBox> difficulty = FindDropDown(DifficultyDropDown);
    REQUIRE(renderer != nullptr);
    REQUIRE(difficulty != nullptr);
    const int32_t startDifficulty = GameDifficulty;
    const uint32_t startShown = MCScreenInput::ScreenHash();
    MCScreenInput::SaveShot("dropdown closed", startShown);

    // The original's DIFFICULTY checks are gone from the screen.
    for (int32_t i = 0; i < 3; i++)
    {
        CHECK(screen.Element<MCGuiObject>(8 + i)->IsShowing() == 0);
    }

    const auto closeNotice = [&]
    {
        if (dialog.View()->ShowWindow != 0)
        {
            MCLogDialogButton& ok = *dialog.View()->OkButton;
            MCScreenInput::RealClick(ok.GlobalX() + ok.Width() / 2, ok.GlobalY() + ok.Height() / 2);
            CHECK(dialog.View()->ShowWindow == 0);
        }
    };

    const auto chooseRenderer = [&](int32_t row, MCRendererKind expected, bool notice)
    {
        MCScreenInput::RealClick(RendererDropDown.FieldX(), RendererDropDown.FieldY());
        CHECK(renderer->IsOpen());
        CHECK(GuiSystem().GrabbedObject() == renderer);
        MCScreenInput::RealClick(RendererDropDown.FieldX(), RendererDropDown.RowY(row));
        CHECK(!renderer->IsOpen());
        // (The message, when it shows, takes the mouse itself.)
        CHECK(GuiSystem().GrabbedObject() != renderer);
        CHECK_EQ(GRendererPreference, static_cast<int32_t>(expected));
        CHECK_EQ(dialog.View()->ShowWindow != 0, notice);
        closeNotice();
        CHECK(GuiSystem().GrabbedObject() == nullptr);
    };

    // Open: in front of everything on the screen, with its list; the look differs from the closed one.
    MCScreenInput::RealClick(RendererDropDown.FieldX(), RendererDropDown.FieldY());
    REQUIRE(renderer->IsOpen());
    CHECK(InFrontOnScreen(screen, *renderer));
    CHECK_EQ(renderer->Height(), MCLogComboBox::FieldHeight + 2 * MCLogComboBox::RowHeight + 1);
    CHECK_EQ(renderer->Hovered(), 0);
    MCScreenInput::RealMove(RendererDropDown.FieldX(), RendererDropDown.RowY(1));
    CHECK_EQ(renderer->Hovered(), 1);
    const uint32_t openShown = MCScreenInput::ScreenHash();
    MCScreenInput::SaveShot("dropdown open", openShown);
    CHECK(openShown != startShown);

    // A press outside closes it with no change, and doesn't reach what's there (ACCEPT would leave the screen).
    constexpr int32_t AcceptX = 499;
    constexpr int32_t AcceptY = 397;
    CHECK(std::dynamic_pointer_cast<MCLogButton>(screen.FindObject(AcceptX, AcceptY)) != nullptr);
    MCScreenInput::RealClick(AcceptX, AcceptY);
    CHECK(!renderer->IsOpen());
    CHECK(GuiSystem().GrabbedObject() == nullptr);
    CHECK(screen.IsShowing() != 0);
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Vulkan));
    MCScreenInput::RealMove(320, 400);
    CHECK_EQ(MCScreenInput::ScreenHash(), startShown);

    // A click on the field again closes it.
    MCScreenInput::RealClick(RendererDropDown.FieldX(), RendererDropDown.FieldY());
    CHECK(renderer->IsOpen());
    MCScreenInput::RealClick(RendererDropDown.FieldX() + 30, RendererDropDown.FieldY());
    CHECK(!renderer->IsOpen());
    CHECK(GuiSystem().GrabbedObject() == nullptr);

    for (int32_t round = 0; round < 3; round++)
    {
        MCTest::Scope scope(std::format("round {}", round));
        chooseRenderer(1, MCRendererKind::Software, true);
        chooseRenderer(0, MCRendererKind::Vulkan, false);
        // The boxes' titles and outlines are not controls: clicks there change nothing.
        MCScreenInput::RealClick(540, 232);
        MCScreenInput::RealClick(481, 250);
        MCScreenInput::RealClick(540, 165);
        CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Vulkan));
        CHECK(dialog.View()->ShowWindow == 0);
        CHECK(GuiSystem().GrabbedObject() == nullptr);
    }

    // The chosen item again: no change, no message.
    chooseRenderer(0, MCRendererKind::Vulkan, false);
    chooseRenderer(1, MCRendererKind::Software, true);
    chooseRenderer(1, MCRendererKind::Software, false);
    chooseRenderer(0, MCRendererKind::Vulkan, false);

    // A drag from the field to a row chooses it on the release.
    MCScreenInput::RealMove(RendererDropDown.FieldX(), RendererDropDown.FieldY());
    MCScreenInput::RealButton(true);
    CHECK(renderer->IsOpen());
    MCScreenInput::RealMove(RendererDropDown.FieldX(), RendererDropDown.RowY(1));
    MCScreenInput::RealButton(false);
    CHECK(!renderer->IsOpen());
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Software));
    CHECK(dialog.View()->ShowWindow != 0);
    closeNotice();

    // Keys on the open list: down/up move the lit row, Escape closes with no change, Return chooses.
    MCScreenInput::RealClick(RendererDropDown.FieldX(), RendererDropDown.FieldY());
    CHECK_EQ(renderer->Hovered(), 1);
    MCScreenInput::RealKey(SDL_SCANCODE_UP);
    CHECK_EQ(renderer->Hovered(), 0);
    MCScreenInput::RealKey(SDL_SCANCODE_UP);
    CHECK_EQ(renderer->Hovered(), 0);
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    CHECK(!renderer->IsOpen());
    CHECK(screen.IsShowing() != 0);
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Software));
    MCScreenInput::RealClick(RendererDropDown.FieldX(), RendererDropDown.FieldY());
    MCScreenInput::RealKey(SDL_SCANCODE_UP);
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    CHECK(!renderer->IsOpen());
    CHECK(GuiSystem().GrabbedObject() == nullptr);
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Vulkan));
    CHECK(dialog.View()->ShowWindow == 0);

    // The wheel on the open list moves the lit row (down is the next row); Return chooses it.
    MCScreenInput::RealClick(RendererDropDown.FieldX(), RendererDropDown.FieldY());
    CHECK_EQ(renderer->Hovered(), 0);
    MCScreenInput::RealWheel(-1);
    CHECK_EQ(renderer->Hovered(), 1);
    CHECK(renderer->IsOpen());
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Software));
    CHECK(dialog.View()->ShowWindow != 0);
    closeNotice();

    // A closed field under the mouse: the wheel and up/down move the choice, clamped at the ends.
    MCScreenInput::RealMove(DifficultyDropDown.FieldX(), DifficultyDropDown.FieldY());
    GameDifficulty = 1;
    MCScreenInput::RealWheel(1);
    CHECK_EQ(GameDifficulty, 0);
    MCScreenInput::RealWheel(1);
    CHECK_EQ(GameDifficulty, 0);
    MCScreenInput::RealWheel(-1);
    CHECK_EQ(GameDifficulty, 1);
    MCScreenInput::RealKey(SDL_SCANCODE_DOWN);
    CHECK_EQ(GameDifficulty, 2);
    MCScreenInput::RealKey(SDL_SCANCODE_DOWN);
    CHECK_EQ(GameDifficulty, 2);
    MCScreenInput::RealKey(SDL_SCANCODE_UP);
    CHECK_EQ(GameDifficulty, 1);
    CHECK(!difficulty->IsOpen());

    // DIFFICULTY with the mouse: each row in turn, several rounds.
    for (int32_t round = 0; round < 2; round++)
    {
        for (int32_t level : {2, 0, 1, 1, 2})
        {
            MCTest::Scope scope(std::format("round {}, difficulty {}", round, level));
            MCScreenInput::RealClick(DifficultyDropDown.FieldX(), DifficultyDropDown.FieldY());
            CHECK(difficulty->IsOpen());
            CHECK(InFrontOnScreen(screen, *difficulty));
            MCScreenInput::RealClick(DifficultyDropDown.FieldX(), DifficultyDropDown.RowY(level));
            CHECK(!difficulty->IsOpen());
            CHECK_EQ(GameDifficulty, level);
            CHECK(dialog.View()->ShowWindow == 0);
        }
    }

    // Back to where it started: the screen shows what it showed then.
    GameDifficulty = startDifficulty;
    chooseRenderer(0, MCRendererKind::Vulkan, false);
    MCScreenInput::RealMove(320, 400);
    CHECK_EQ(MCScreenInput::ScreenHash(), startShown);
    CancelPrefs();
    MCScreenInput::RealMove(320, 400);
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Vulkan));
    CHECK_EQ(GameDifficulty, startDifficulty);
}

namespace
{
    /// <summary>Every slider under <paramref name="parent"/>, at any depth, top to bottom as the tree lists them.</summary>
    void CollectSliders(MCGuiObject& parent, std::vector<std::shared_ptr<MCLogSlider>>& sliders)
    {
        for (const std::shared_ptr<MCGuiObject>& child : parent.Children())
        {
            if (auto slider = std::dynamic_pointer_cast<MCLogSlider>(child); slider != nullptr)
            {
                sliders.push_back(slider);
            }

            CollectSliders(*child, sliders);
        }
    }
}

/// <summary>
/// The preferences panel: five sliders (brightness, music, radio, effects, interface), each drawing its own track,
/// whose thumbs sit on the pixels the fit's sliders put them on (x 365 plus the value's share of 91, at y 175, 222,
/// 248 and 275), INTERFACE VOLUME's a row under the effects'; a press reads the same value at the same place. A
/// click on the panel art around the panel, or on the panel between its boxes, changes nothing on the screen.
/// </summary>
TEST_CASE_ISOLATED("game: the preferences panel keeps the sliders where they were and adds INTERFACE VOLUME")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCGenericScreen& screen = *GlobalLog()->PrefScreen;
    ShowPreferences();
    MCScreenInput::RealMove(320, 400);

    std::vector<std::shared_ptr<MCLogSlider>> sliders;
    CollectSliders(screen, sliders);
    REQUIRE_EQ(sliders.size(), size_t{5});

    // The fit's sliders 3..6, then INTERFACE VOLUME.
    for (int32_t i = 0; i < 4; i++)
    {
        CHECK(sliders[static_cast<size_t>(i)] == screen.Element<MCLogSlider>(3 + i));
    }

    constexpr std::array<int32_t, 5> Tops = {175, 222, 248, 275, 301};
    constexpr int32_t OldLeft = 365;
    constexpr int32_t OldTravel = 91;

    for (size_t i = 0; i < sliders.size(); i++)
    {
        MCTest::Scope scope(std::format("slider {}", i));
        MCLogSlider& slider = *sliders[i];
        CHECK(slider.TrackArt != nullptr);
        CHECK_EQ(slider.GlobalY(), Tops[i]);
        const int32_t saved = slider.CurrentValue;

        for (int32_t value : {0, 1, slider.MaxValue / 2, slider.MaxValue - 1, slider.MaxValue})
        {
            MCTest::Scope valueScope(std::format("value {}", value));
            slider.SetCurrentValue(value);
            const auto share =
                static_cast<int32_t>(static_cast<double>(OldTravel) * (static_cast<double>(value - slider.MinValue) /
                                                                       (slider.MaxValue - slider.MinValue)));
            CHECK_EQ(slider.GlobalX() + slider.ThumbX(), OldLeft + share);
        }

        CHECK_EQ(slider.ValueAt(OldLeft), slider.MinValue);
        CHECK_EQ(slider.ValueAt(OldLeft + OldTravel), slider.MaxValue);
        slider.SetCurrentValue(saved);
    }

    MCTestGame::RunFrame(1.0f / 15.0f);
    const uint32_t shown = MCScreenInput::ScreenHash();
    MCScreenInput::SaveShot("prefs panel", shown);

    // The PREFERENCES title (the art), the art's frame below the panel, and the panel between its boxes (between SOUND
    // and HD, and between the columns).
    for (const auto [x, y] : {std::pair{400, 105}, std::pair{480, 425}, std::pair{380, 321}, std::pair{470, 300}})
    {
        MCTest::Scope scope(std::format("click at {}, {}", x, y));
        MCScreenInput::RealClick(x, y);
        MCScreenInput::RealMove(320, 400);
        CHECK(screen.IsShowing() != 0);
        CHECK_EQ(MCScreenInput::ScreenHash(), shown);
    }
}

/// <summary>
/// Preferences' INTERFACE volume shows the interface level when Preferences opens; its slider sets the level (and only
/// it: the effects volume stays) from silence to full, and CANCEL puts the level back.
/// </summary>
TEST_CASE_ISOLATED("game: INTERFACE VOLUME sets the interface level, and CANCEL puts it back")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCTestGame::FinishChanges());
    MCSoundSystem& sound = *SoundSystem();
    sound.InterfaceLevel = 90;
    sound.DigitalMasterVolume = 40;
    const MCOptionalRef<const MCSettingsModel::MCSetting> found = MCSettingsModel::Find("interface");
    REQUIRE(found.has_value());
    const MCSettingsModel::MCSetting& volume = found->get();
    const std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Sound);
    REQUIRE(prefs != nullptr);
    Rml::Element* slider = MCRemadeTest::Find(*prefs, "interface");
    REQUIRE(slider != nullptr);
    const auto shown = [slider] { return slider->GetAttribute<int>("value", -1); };
    CHECK_EQ(shown(), volume.Get());
    CHECK_EQ(MCRemadeTest::Words(*prefs, "level-interface"),
             std::to_string(MCSettingsModel::LevelPercent(volume, volume.Get())));

    // The slider's ends, by its steps: silence, then full.
    constexpr int32_t Steps = 25;
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "interface", SDL_SCANCODE_LEFT, Steps));
    CHECK_EQ(shown(), 0);
    CHECK_EQ(sound.InterfaceLevel, 0);
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "interface", SDL_SCANCODE_RIGHT, Steps));
    CHECK_EQ(sound.InterfaceLevel, 127);
    CHECK_EQ(sound.DigitalMasterVolume, 40);

    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "cancel"));
    CHECK_EQ(sound.InterfaceLevel, 90);
    CHECK_EQ(sound.DigitalMasterVolume, 40);
}

/// <summary>
/// Preferences' SCROLL SPEED shows the scroll speed when Preferences opens (AUTO, the original's pace) and offers 1.5X
/// to 3X in half steps besides; a choice sets the speed, CANCEL puts it back (and the stepper shows it), and ACCEPT
/// writes it to PREFS "ScrollSpeed".
/// </summary>
TEST_CASE_ISOLATED("game: SCROLL SPEED sets the scroll speed, CANCEL puts it back and ACCEPT saves it in PREFS")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCTestGame::FinishChanges());
    // The tests' user folder overlays the install, so a PREFS written here would be every later test's.
    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    std::filesystem::remove(prefsPath);
    REQUIRE_EQ(GScrollSpeed, AutoScrollSpeed);

    std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Display);
    REQUIRE(prefs != nullptr);
    constexpr std::array<std::string_view, 5> Labels = {"Auto", "1.5x", "2x", "2.5x", "3x"};
    REQUIRE_EQ(Labels.size(), ScrollSpeeds.size());

    // Each choice in turn, its words and its speed; then back to AUTO.
    for (size_t i = 0; i < Labels.size(); i++)
    {
        const MCTest::Scope scope(Labels[i]);
        CHECK_EQ(MCRemadeTest::StepWords(*prefs, "scroll-speed"), Labels[i]);
        CHECK_EQ(GScrollSpeed, ScrollSpeeds[i]);
        REQUIRE(MCRemadeTest::KeyOn(*prefs, "scroll-speed", SDL_SCANCODE_RIGHT));
    }

    REQUIRE(MCRemadeTest::KeyOn(*prefs, "scroll-speed", SDL_SCANCODE_LEFT, static_cast<int32_t>(Labels.size())));
    CHECK_EQ(GScrollSpeed, AutoScrollSpeed);

    // 2X, then CANCEL: AUTO again, shown when Preferences opens.
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "scroll-speed", SDL_SCANCODE_RIGHT, 2));
    CHECK_EQ(GScrollSpeed, 200);
    CHECK_EQ(MCRemadeTest::StepWords(*prefs, "scroll-speed"), "2x");
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "cancel"));
    CHECK_EQ(GScrollSpeed, AutoScrollSpeed);
    prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Display);
    REQUIRE(prefs != nullptr);
    CHECK_EQ(MCRemadeTest::StepWords(*prefs, "scroll-speed"), "Auto");

    // 3X, then ACCEPT: written to PREFS.
    REQUIRE(MCRemadeTest::KeyOn(*prefs, "scroll-speed", SDL_SCANCODE_RIGHT, 4));
    CHECK_EQ(GScrollSpeed, 300);
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "accept"));
    CHECK_EQ(GScrollSpeed, 300);
    int32_t saved = 0;
    {
        MCFitIniFile prefsFile;
        REQUIRE_EQ(prefsFile.Open(prefsPath.string().c_str()), 0);
        REQUIRE_EQ(prefsFile.SeekBlock("MechCommander"), 0);
        saved = prefsFile.Read<int32_t>("ScrollSpeed").value_or(0);
    }

    std::filesystem::remove(prefsPath);
    CHECK_EQ(saved, 300);
}

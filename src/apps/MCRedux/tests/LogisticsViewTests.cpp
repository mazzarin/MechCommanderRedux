#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFitIniFile.h"
#include "gui/MCScrollPane.h"
#include "logistics/MCPurchaseDlg.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCFileScrollPane.h"
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
        picture.Destroy();
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
    REQUIRE(GlobalLog()->ChatWindow != nullptr);
    MCLogChatWindow& chat = *GlobalLog()->ChatWindow;
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
        chat.AddLine(lines[i].c_str());
        CHECK(same());
    }

    // A resize keeps the history (the original copied its picture into the new pane).
    chat.Resize(0xe7);
    CHECK(chat.HistoryPane->ContentPort->IsView());
    CHECK(same());

    // A reset clears it.
    chat.Reset();
    VfxPaneWipe(original.Frame(), 0x10);
    CHECK(same());
    AddLineAsOriginal(original, lines[1]);
    chat.AddLine(lines[1].c_str());
    CHECK(same());
    original.Destroy();
}

namespace
{
    /// <summary>
    /// The preferences screen's drop-downs, as AddPreferencesPanel places them: each field 76 wide and 11 tall at
    /// (512, top), its list's rows 10 tall from top + 11. DIFFICULTY's field is at y 176 (EASY, REGULAR, HARD),
    /// RENDERER's at y 243 (VULKAN, SOFTWARE).
    /// </summary>
    struct PrefsDropDown
    {
        int32_t Top = 0;

        int32_t FieldX() const { return 540; }
        int32_t FieldY() const { return Top + 5; }
        int32_t RowY(int32_t row) const { return Top + 11 + row * 10 + 5; }
    };

    constexpr PrefsDropDown DifficultyDropDown{176};
    constexpr PrefsDropDown RendererDropDown{243};

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
/// The preferences screen's RENDERER drop-down: choosing SOFTWARE says it takes effect after a restart, CANCEL puts
/// the old choice back (and the field shows it), ACCEPT writes it to PREFS "Renderer"; the running game keeps its
/// renderer.
/// </summary>
TEST_CASE_ISOLATED("game: the preferences screen chooses the renderer and saves it in PREFS")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    // The tests' user folder overlays the install, so a PREFS written here would be every later test's.
    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    std::filesystem::remove(prefsPath);
    const int32_t running = GRenderer;
    const int32_t chosen = GRendererPreference;
    REQUIRE_EQ(chosen, static_cast<int32_t>(MCRendererKind::Vulkan));

    const auto frames = []
    {
        for (int32_t frame = 0; frame < 5; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    };

    // Choosing another renderer than the running one says it needs a restart; OK closes the message.
    MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
    const auto closeRestartNotice = [&](std::string_view shot)
    {
        CHECK(dialog.ShowWindow != 0);
        MCScreenInput::SaveShot(std::string(shot), MCScreenInput::ScreenHash());
        MCLogDialogButton& ok = *dialog.OkButton;
        MCScreenInput::Click(ok.GlobalX() + ok.Width() / 2, ok.GlobalY() + ok.Height() / 2);
        frames();
        CHECK(dialog.ShowWindow == 0);
    };

    const auto choose = [&](int32_t row)
    {
        MCScreenInput::Click(RendererDropDown.FieldX(), RendererDropDown.FieldY());
        frames();
        MCScreenInput::Click(RendererDropDown.FieldX(), RendererDropDown.RowY(row));
        frames();
    };

    ShowPreferences();
    frames();
    const uint32_t vulkanShown = MCScreenInput::ScreenHash();
    MCScreenInput::SaveShot("prefs vulkan", vulkanShown);
    choose(1);
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Software));
    closeRestartNotice("prefs restart notice");
    const uint32_t softwareShown = MCScreenInput::ScreenHash();
    MCScreenInput::SaveShot("prefs software", softwareShown);
    CHECK(softwareShown != vulkanShown);

    // VULKAN back: no message (it's the running renderer); VULKAN again: no change, no message.
    choose(0);
    CHECK(dialog.ShowWindow == 0);
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Vulkan));
    CHECK_EQ(MCScreenInput::ScreenHash(), vulkanShown);
    choose(0);
    CHECK(dialog.ShowWindow == 0);
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Vulkan));
    CHECK_EQ(MCScreenInput::ScreenHash(), vulkanShown);
    choose(1);
    closeRestartNotice("prefs restart notice again");
    CHECK_EQ(MCScreenInput::ScreenHash(), softwareShown);

    // CANCEL puts the choice back, and the field shows it when the screen opens again.
    CancelPrefs();
    frames();
    CHECK_EQ(GRendererPreference, chosen);
    ShowPreferences();
    frames();
    CHECK_EQ(MCScreenInput::ScreenHash(), vulkanShown);

    choose(1);
    closeRestartNotice("prefs restart notice before accept");
    WritePrefs();
    frames();
    CHECK_EQ(GRenderer, running);

    char renderer[32] = {};
    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(prefsPath.string().c_str()), 0);
        REQUIRE_EQ(prefs.SeekBlock("MechCommander"), 0);
        CHECK_EQ(prefs.ReadIdString("Renderer", std::span(renderer).first(sizeof(renderer) - 1)), 0);
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

    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Vulkan));
    REQUIRE(GRenderer != static_cast<int32_t>(MCRendererKind::Software));
    MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
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
        if (dialog.ShowWindow != 0)
        {
            MCLogDialogButton& ok = *dialog.OkButton;
            MCScreenInput::RealClick(ok.GlobalX() + ok.Width() / 2, ok.GlobalY() + ok.Height() / 2);
            CHECK(dialog.ShowWindow == 0);
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
        CHECK_EQ(dialog.ShowWindow != 0, notice);
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
    constexpr int32_t AcceptY = 401;
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
        CHECK(dialog.ShowWindow == 0);
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
    CHECK(dialog.ShowWindow != 0);
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
    CHECK(dialog.ShowWindow == 0);

    // The wheel on the open list moves the lit row (down is the next row); Return chooses it.
    MCScreenInput::RealClick(RendererDropDown.FieldX(), RendererDropDown.FieldY());
    CHECK_EQ(renderer->Hovered(), 0);
    MCScreenInput::RealWheel(-1);
    CHECK_EQ(renderer->Hovered(), 1);
    CHECK(renderer->IsOpen());
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    CHECK_EQ(GRendererPreference, static_cast<int32_t>(MCRendererKind::Software));
    CHECK(dialog.ShowWindow != 0);
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
            CHECK(dialog.ShowWindow == 0);
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
        CHECK(slider.TrackPort != nullptr);
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

    // The PREFERENCES title (the art), the art's frame below the panel, and the panel between its boxes (above the
    // CONTROLS button at its bottom left).
    for (const auto [x, y] : {std::pair{400, 105}, std::pair{480, 425}, std::pair{380, 360}, std::pair{470, 300}})
    {
        MCTest::Scope scope(std::format("click at {}, {}", x, y));
        MCScreenInput::RealClick(x, y);
        MCScreenInput::RealMove(320, 400);
        CHECK(screen.IsShowing() != 0);
        CHECK_EQ(MCScreenInput::ScreenHash(), shown);
    }
}

/// <summary>
/// INTERFACE VOLUME shows the interface level when the screen opens; a click on its track sets the level (and only
/// it: the effects volume stays), and CANCEL puts the level back.
/// </summary>
TEST_CASE_ISOLATED("game: INTERFACE VOLUME sets the interface level, and CANCEL puts it back")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCGenericScreen& screen = *GlobalLog()->PrefScreen;
    MCSoundSystem& sound = *SoundSystem();
    sound.InterfaceLevel = 90;
    sound.DigitalMasterVolume = 40;
    ShowPreferences();
    MCScreenInput::RealMove(320, 400);

    std::vector<std::shared_ptr<MCLogSlider>> sliders;
    CollectSliders(screen, sliders);
    REQUIRE_EQ(sliders.size(), size_t{5});
    MCLogSlider& slider = *sliders[4];
    CHECK_EQ(slider.CurrentValue, 90);
    CHECK_EQ(screen.Element<MCLogSlider>(6)->CurrentValue, 40);

    // The track's ends (the thumb's travel: x 365..456): silence, then full.
    const int32_t middleY = slider.GlobalY() + slider.Height() / 2;
    MCScreenInput::RealClick(365, middleY);
    CHECK_EQ(slider.CurrentValue, 0);
    CHECK_EQ(sound.InterfaceLevel, 0);
    MCScreenInput::RealClick(365 + 91, middleY);
    CHECK_EQ(sound.InterfaceLevel, 127);
    CHECK_EQ(sound.DigitalMasterVolume, 40);

    CancelPrefs();
    CHECK_EQ(sound.InterfaceLevel, 90);
    CHECK_EQ(sound.DigitalMasterVolume, 40);
}

/// <summary>
/// OB-131: a scroll pane's arrow lets go wherever the mouse button comes up. The original let go only on a release
/// over the slider column: let go anywhere else, the arrow stayed pressed and the slider would not drag.
/// </summary>
TEST_CASE_ISOLATED("game: a scroll pane's arrow lets go wherever the mouse button comes up")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());

    // A pane 0x80 x 0x60 at (100, 100) showing content four times its height: the slider is a quarter of the track.
    const MCGuiOwned<MCScrollPane> pane = MCMakeGui<MCScrollPane>();
    pane->Init(0x80, 0x60, 100, 100, std::string_view());
    auto content = std::make_shared<MCLogPort>();
    content->Init(0x80 - 13, 0x180);
    pane->SetDisplayPort(std::move(content), true);
    REQUIRE(pane->SliderHeight > 0);

    const auto send = [pane](int32_t type, int32_t xPos, int32_t yPos)
    {
        MCGuiEvent event;
        event.Clear();
        event.Type = type;
        event.X = xPos;
        event.Y = yPos;
        pane->HandleEvent(event);
    };

    // Press the down arrow (it shows pressed while held), then let go left of the column.
    const int32_t column = pane->GlobalX() + pane->Width() - 6;
    send(1, column, pane->GlobalY() + pane->Height() - 5);
    CHECK(pane->HeldArrow() == MCScrollPane::Arrow::Down);
    send(4, pane->GlobalX() + 10, pane->GlobalY() + 10);
    CHECK(pane->HeldArrow() == MCScrollPane::Arrow::None);

    // The slider drags.
    const int32_t before = pane->SliderPos;
    const int32_t sliderY = pane->GlobalY() + before + pane->SliderHeight / 2;
    send(1, column, sliderY);
    send(7, column, sliderY + 10);
    send(4, column, sliderY + 10);
    CHECK_EQ(pane->SliderPos, before + 10);
}

/// <summary>
/// The save list's slider: a list longer than the pane gets a slider with its picture (the port made none for the
/// file panes, so drawing the column read a null texture).
/// </summary>
TEST_CASE_ISOLATED("game: a save list longer than its pane draws its slider")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    const MCGuiOwned<MCFileScrollPane> pane = MCMakeGui<MCFileScrollPane>();
    pane->Init(10, 10, 0xc0, 0x80);

    // Four panes' worth of files.
    std::shared_ptr<MCLogPort> content = pane->ContentPort;
    content->InitView(0xc0 - 13, 0x200);
    pane->SetDisplayPort(content, true);
    REQUIRE(pane->SliderHeight > 0);
    CHECK(!pane->SliderTexture.expired());
    CHECK_EQ(pane->SliderTexture.lock()->Height, pane->SliderHeight);

    // The column draws: the slider's rows land at its place.
    MCLogPort picture;
    picture.Init(13, 0x80);
    VfxPaneWipe(picture.Frame(), 0);
    pane->DrawSliderColumn(picture.Frame(), 0, 0, false);
    const std::span<const uint8_t> pixels = picture.Bitmap().Bytes();
    CHECK_EQ(pixels[static_cast<size_t>((pane->SliderPos + 1) * 13 + 5)], uint8_t{0x1a});
    picture.Destroy();
}

#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCKeyBindings.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCControlsPanel.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogKeyList.h"
#include "logistics/MCLogTextButton.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCLogistics.h"
#include "main/MCSystemConfig.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"

// The player's key bindings (iface/MCKeyBindings) and the preferences' CONTROLS panel that edits them: the defaults
// are the original's, every command a player can bind is listed once, the names read back, a key moves from the
// command that had it, PREFS keeps the changes, and the panel binds keys with a player's mouse and keyboard.

namespace
{
    using MCKeyBindings::MCKeyTable;

    /// <summary>The key cell of <paramref name="command"/> (column 0) or its alternate (column 1).</summary>
    MCLogKeyList::Cell CellOf(MCKeyCommand command)
    {
        const std::optional<MCLogKeyList::Cell> cell = MCLogKeyList::CellOf(command);
        REQUIRE(cell.has_value());
        return *cell;
    }

    /// <summary>The commands a player can bind: every row's command and alternate, in the list's order.</summary>
    std::vector<MCKeyCommand> ListedCommands()
    {
        std::vector<MCKeyCommand> commands;

        for (const MCKeyBindings::MCControlRow& row : MCKeyBindings::ControlRows())
        {
            if (row.IsHeading())
            {
                continue;
            }

            commands.push_back(row.Command);

            if (row.Alternate != MCKeyCommand::Unused)
            {
                commands.push_back(row.Alternate);
            }
        }

        return commands;
    }

    /// <summary>The text button showing <paramref name="label"/> under <paramref name="parent"/> (at any depth), or null.</summary>
    std::shared_ptr<MCLogTextButton> FindTextButton(MCGuiObject& parent, std::string_view label)
    {
        for (const std::shared_ptr<MCGuiObject>& child : parent.Children())
        {
            if (auto button = std::dynamic_pointer_cast<MCLogTextButton>(child);
                button != nullptr && button->Label() == label)
            {
                return button;
            }

            if (std::shared_ptr<MCLogTextButton> found = FindTextButton(*child, label); found != nullptr)
            {
                return found;
            }
        }

        return nullptr;
    }

    /// <summary>Runs a few frames, as a player's screen would between inputs.</summary>
    void Frames()
    {
        constexpr int32_t frames = 3;

        for (int32_t frame = 0; frame < frames; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }

    /// <summary>Clicks the middle of <paramref name="object"/> as a player does.</summary>
    void RealClickOn(MCGuiObject& object)
    {
        MCScreenInput::RealClick(object.GlobalX() + object.Width() / 2, object.GlobalY() + object.Height() / 2);
        Frames();
    }

    /// <summary>Scrolls <paramref name="cell"/> into the list and clicks it as a player does.</summary>
    void RealClickCell(MCLogKeyList& list, MCLogKeyList::Cell cell)
    {
        list.ScrollTo(cell.Row - list.VisibleRows() / 2);
        const std::optional<std::pair<int32_t, int32_t>> centre = list.CellCentre(cell);
        REQUIRE(centre.has_value());
        MCScreenInput::RealClick(centre->first, centre->second);
        Frames();
    }

    /// <summary>Sends one SDL key event as play does, then a frame.</summary>
    void RealKeyEvent(SDL_Scancode scancode, bool down, SDL_Keymod mod)
    {
        SDL_Event key{};
        key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        key.key.windowID = SDL_GetWindowID(&MCInput::Display()->Window());
        key.key.scancode = scancode;
        key.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
        key.key.mod = mod;
        key.key.down = down;
        MCInput::HandleEvent(key);
        MCTestGame::RunFrame(1.0f / 15.0f);
    }

    /// <summary>Holds <paramref name="modifier"/>, presses and lets go of <paramref name="scancode"/>, lets go of the modifier.</summary>
    void RealChord(SDL_Scancode modifier, SDL_Keymod mod, SDL_Scancode scancode)
    {
        RealKeyEvent(modifier, true, mod);
        RealKeyEvent(scancode, true, mod);
        RealKeyEvent(scancode, false, mod);
        RealKeyEvent(modifier, false, SDL_KMOD_NONE);
    }
}

/// <summary>
/// The defaults are the original's (MCX.EXE's table at InterfaceObject +0x118, as MCTacticalInterface::Init set it):
/// a sample of each kind, the alternates with their modifiers and the alt key alone with its own modifier.
/// </summary>
TEST_CASE("controls: the default keys are the original's")
{
    const MCKeyTable& keys = MCKeyBindings::Defaults();
    const auto key = [&](MCKeyCommand command) { return keys[static_cast<size_t>(command)]; };
    CHECK_EQ(key(MCKeyCommand::Eject), 0x147u);
    CHECK_EQ(key(MCKeyCommand::Run), 0x39u);
    CHECK_EQ(key(MCKeyCommand::RunAlternate), 0x39u | KeyCtrl);
    CHECK_EQ(key(MCKeyCommand::Jump), 0x24u);
    CHECK_EQ(key(MCKeyCommand::AimHead), 0x48u);
    CHECK_EQ(key(MCKeyCommand::AddLance1), 0x3bu | KeyShift);
    CHECK_EQ(key(MCKeyCommand::LinkLance4), 0x3eu | KeyCtrl);
    CHECK_EQ(key(MCKeyCommand::HoldTacticalMap), 0x38u | KeyAlt);
    CHECK_EQ(key(MCKeyCommand::SalvagePage), 0x1fu | KeyAlt);
    CHECK_EQ(key(MCKeyCommand::Artillery4), 5u);
    CHECK_EQ(key(MCKeyCommand::Unbound), MCKeyBindings::NoKey);
    CHECK_EQ(key(MCKeyCommand::Unused), 0u);

    // Before PREFS or the CONTROLS screen changes them, the player's are the defaults.
    CHECK(MCKeyBindings::Player() == MCKeyBindings::Defaults());
}

/// <summary>
/// Every command with a default key is listed once (the debug strike, a cheat's, isn't), with a name PREFS keeps it
/// under, and no two listed commands share a default key.
/// </summary>
TEST_CASE("controls: every command a player can bind is listed once, with its own key")
{
    const std::vector<MCKeyCommand> listed = ListedCommands();
    const MCKeyTable& keys = MCKeyBindings::Defaults();
    std::set<std::string_view> ids;

    for (size_t index = 0; index < keys.size(); index++)
    {
        const auto command = static_cast<MCKeyCommand>(index);
        const bool bindable =
            keys[index] != 0 && command != MCKeyCommand::Unbound && command != MCKeyCommand::DebugStrike;
        MCTest::Scope scope(std::format("command {}", index));
        CHECK_EQ(std::ranges::count(listed, command), bindable ? 1 : 0);

        if (bindable)
        {
            const std::string_view id = MCKeyBindings::CommandId(command);
            CHECK(!id.empty());
            CHECK(ids.insert(id).second);
            CHECK(!MCKeyBindings::CommandLabel(command).empty());
            CHECK_EQ(MCKeyBindings::FindCommand(keys, keys[index], command), MCKeyCommand::Unused);
        }
    }
}

/// <summary>
/// A binding's name reads back as the same binding: every default, the modifiers in any case, a key holding a
/// plus, a modifier alone, no key, and a scan code without a name.
/// </summary>
TEST_CASE("controls: key names read back as the same keys")
{
    for (const MCKeyCommand command : ListedCommands())
    {
        const uint32_t key = MCKeyBindings::Defaults()[static_cast<size_t>(command)];
        MCTest::Scope scope(MCKeyBindings::KeyName(key));
        CHECK_EQ(MCKeyBindings::ParseKey(MCKeyBindings::KeyName(key)), std::optional(key));
    }

    CHECK_EQ(MCKeyBindings::KeyName(0x21 | KeyCtrl | KeyShift), "CTRL+SHIFT+F");
    CHECK_EQ(MCKeyBindings::KeyName(0x4e | KeyCtrl), "CTRL+KEYPAD +");
    CHECK_EQ(MCKeyBindings::KeyName(0x38 | KeyAlt), "ALT");
    CHECK_EQ(MCKeyBindings::KeyName(MCKeyBindings::NoKey), "NONE");
    CHECK_EQ(MCKeyBindings::KeyName(0x56), "KEY 56");
    CHECK_EQ(MCKeyBindings::ParseKey("ctrl+shift+f"), std::optional(0x21u | KeyCtrl | KeyShift));
    CHECK_EQ(MCKeyBindings::ParseKey("ALT"), std::optional(0x38u | KeyAlt));
    CHECK_EQ(MCKeyBindings::ParseKey("KEY 56"), std::optional(0x56u));
    CHECK_EQ(MCKeyBindings::ParseKey("NONE"), std::optional(MCKeyBindings::NoKey));
    CHECK(!MCKeyBindings::ParseKey("HYPER+J").has_value());
    CHECK(!MCKeyBindings::ParseKey("").has_value());
    CHECK(!MCKeyBindings::ParseKey("KEY 0").has_value());

    // The CONTROLS screen shows no key as nothing, and on a Mac the keys by the Mac keyboard's names.
    CHECK(MCKeyBindings::KeyLabel(MCKeyBindings::NoKey).empty());
#if defined(__APPLE__)
    CHECK_EQ(MCKeyBindings::KeyLabel(0x38 | KeyAlt), "OPTION");
    CHECK_EQ(MCKeyBindings::KeyLabel(0x24 | KeyAlt), "OPTION+J");
    CHECK_EQ(MCKeyBindings::KeyLabel(0x0e), "DELETE");
#else
    CHECK_EQ(MCKeyBindings::KeyLabel(0x38 | KeyAlt), "ALT");
#endif
}

/// <summary>
/// A key event's binding: the modifiers held, right ctrl and right alt as the left ones (the interface folds them), and
/// a modifier key alone with its own modifier.
/// </summary>
TEST_CASE("controls: a key event makes the binding the interface matches")
{
    CHECK_EQ(MCKeyBindings::FromKeyEvent(0x24, false, true, false), 0x24u | KeyCtrl);
    CHECK_EQ(MCKeyBindings::FromKeyEvent(0x3b, true, false, false), 0x3bu | KeyShift);
    CHECK_EQ(MCKeyBindings::FromKeyEvent(0x138, false, false, false), 0x38u | KeyAlt);
    CHECK_EQ(MCKeyBindings::FromKeyEvent(0x11d, false, true, false), 0x1du | KeyCtrl);
    CHECK_EQ(MCKeyBindings::FromKeyEvent(0x36, false, false, false), 0x36u | KeyShift);
    CHECK(MCKeyBindings::IsModifierKey(0x2a));
    CHECK(MCKeyBindings::IsModifierKey(0x138));
    CHECK(!MCKeyBindings::IsModifierKey(0x24));
}

/// <summary>
/// Binding a key another listed command has moves it: that command is left without a key, and is the one returned; a
/// free key moves nothing; a command can't be found by no key.
/// </summary>
TEST_CASE("controls: binding a key another command has moves it from that command")
{
    MCKeyTable keys = MCKeyBindings::Defaults();
    const uint32_t f1 = keys[static_cast<size_t>(MCKeyCommand::SelectLance1)];
    CHECK_EQ(MCKeyBindings::Bind(keys, MCKeyCommand::Jump, f1), MCKeyCommand::SelectLance1);
    CHECK_EQ(keys[static_cast<size_t>(MCKeyCommand::Jump)], f1);
    CHECK_EQ(keys[static_cast<size_t>(MCKeyCommand::SelectLance1)], MCKeyBindings::NoKey);
    CHECK_EQ(MCKeyBindings::Bind(keys, MCKeyCommand::SelectLance1, 0x25), MCKeyCommand::Unused);
    CHECK_EQ(MCKeyBindings::FindCommand(keys, MCKeyBindings::NoKey, MCKeyCommand::Unused), MCKeyCommand::Unused);

    // The same key with another modifier is another key.
    CHECK_EQ(MCKeyBindings::Bind(keys, MCKeyCommand::Guard, 0x25 | KeyShift), MCKeyCommand::Unused);
}

/// <summary>
/// PREFS keeps only the keys that differ from the defaults, in the Controls block by name, and reading it gives the
/// player those keys over the defaults; a name it doesn't know leaves that command's key as it was, and a PREFS without
/// the block gives the defaults.
/// </summary>
TEST_CASE_ISOLATED("game: PREFS keeps the changed keys and reads them back")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    std::filesystem::remove(prefsPath);

    // The preferences' ACCEPT writes the keys the CONTROLS panel edited.
    ShowPreferences();
    MCKeyTable& keys = EditedKeys();
    MCKeyBindings::Bind(keys, MCKeyCommand::Jump, 0x25 | KeyCtrl);
    MCKeyBindings::Bind(keys, MCKeyCommand::AimHead, 0x10 | KeyShift);
    keys[static_cast<size_t>(MCKeyCommand::Eject)] = MCKeyBindings::NoKey;
    const MCKeyTable written = keys;
    WritePrefs();
    Frames();

    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(prefsPath.string()), 0);
        REQUIRE_EQ(prefs.SeekBlock("Controls"), 0);
        CHECK_EQ(prefs.Read<std::string>("Jump"), MCFitResult<std::string>("CTRL+K"));
        CHECK_EQ(prefs.Read<std::string>("AimHead"), MCFitResult<std::string>("SHIFT+Q"));
        CHECK_EQ(prefs.Read<std::string>("Eject"), MCFitResult<std::string>("NONE"));
        CHECK(!prefs.Read<std::string>("Run").has_value());
    }

    // Read back over the defaults.
    MCKeyBindings::SetPlayer(MCKeyBindings::Defaults());
    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(prefsPath.string()), 0);
        ReadPreferences(prefs);
    }

    CHECK(MCKeyBindings::Player() == written);

    // A name PREFS gives that isn't a key leaves that command's key alone.
    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Create(prefsPath.string()), 0);
        prefs.WriteBlock("MechCommander");
        prefs.WriteIdLong("Difficulty", 1);
        prefs.WriteBlock("Controls");
        prefs.WriteIdString("Jump", "HYPER+J");
        prefs.WriteIdString("Guard", "SHIFT+G");
    }

    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(prefsPath.string()), 0);
        ReadPreferences(prefs);
    }

    CHECK_EQ(MCKeyBindings::Player()[static_cast<size_t>(MCKeyCommand::Jump)],
             MCKeyBindings::Defaults()[static_cast<size_t>(MCKeyCommand::Jump)]);
    CHECK_EQ(MCKeyBindings::Player()[static_cast<size_t>(MCKeyCommand::Guard)], 0x22u | KeyShift);

    std::filesystem::remove(prefsPath);
}

/// <summary>
/// The CONTROLS panel with a player's mouse and keys: CONTROLS on the preferences shows it, a click on a key listens, the
/// next key (with its modifiers) binds, a key another command has moves from it (the status line says so), Escape
/// stops listening without leaving the screen, alt alone binds when let go, a right click clears a key, DEFAULTS puts
/// the original's back, DONE goes back to the preferences, CANCEL drops the changes and ACCEPT keeps and writes them.
/// </summary>
TEST_CASE_ISOLATED("game: the CONTROLS panel binds keys with a player's mouse and keyboard")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    std::filesystem::remove(prefsPath);
    MCGenericScreen& screen = *GlobalLog()->PrefScreen;
    const auto key = [](MCKeyCommand command) { return EditedKeys()[static_cast<size_t>(command)]; };

    ShowPreferences();
    Frames();
    CHECK(!ControlsShown());
    MCScreenInput::SaveShot("controls prefs", MCScreenInput::ScreenHash());
    const std::shared_ptr<MCLogTextButton> controlsButton = FindTextButton(screen, "CONTROLS");
    REQUIRE(controlsButton != nullptr);
    RealClickOn(*controlsButton);
    REQUIRE(ControlsShown());
    const std::shared_ptr<MCLogKeyList> list = ControlsList();
    REQUIRE(list != nullptr);
    MCScreenInput::SaveShot("controls", MCScreenInput::ScreenHash());

    // A click on JUMP's key listens; K binds.
    RealClickCell(*list, CellOf(MCKeyCommand::Jump));
    CHECK(list->Listening() == CellOf(MCKeyCommand::Jump));
    CHECK(GuiSystem().HasText(*list));
    MCScreenInput::SaveShot("controls listening", MCScreenInput::ScreenHash());
    MCScreenInput::RealKey(SDL_SCANCODE_K);
    CHECK(!list->Listening().has_value());
    CHECK_EQ(key(MCKeyCommand::Jump), 0x25u);
    MCScreenInput::SaveShot("controls jump on k", MCScreenInput::ScreenHash());

    // F1 for GUARD moves it from SELECT LANCE 1, and says so.
    RealClickCell(*list, CellOf(MCKeyCommand::Guard));
    MCScreenInput::RealKey(SDL_SCANCODE_F1);
    CHECK_EQ(key(MCKeyCommand::Guard), 0x3bu);
    CHECK_EQ(key(MCKeyCommand::SelectLance1), MCKeyBindings::NoKey);
    CHECK_EQ(list->Status(), "F1 MOVED FROM SELECT LANCE 1.");
    MCScreenInput::SaveShot("controls moved", MCScreenInput::ScreenHash());

    // Ctrl+R for RUN's alternate; Escape while listening keeps the key and the screen.
    RealClickCell(*list, CellOf(MCKeyCommand::RunAlternate));
    CHECK_EQ(list->Status(), "PRESS THE NEW KEY FOR RUN ALTERNATE. ESC CANCELS.");
    RealChord(SDL_SCANCODE_LCTRL, SDL_KMOD_LCTRL, SDL_SCANCODE_R);
    CHECK_EQ(key(MCKeyCommand::RunAlternate), 0x13u | KeyCtrl);
    RealClickCell(*list, CellOf(MCKeyCommand::Stop));

    // A key the game has no code for (F13) binds nothing: it keeps listening.
    MCScreenInput::RealKey(SDL_SCANCODE_F13);
    CHECK(list->Listening() == CellOf(MCKeyCommand::Stop));
    CHECK_EQ(list->Status(), "PRESS THE NEW KEY FOR STOP. ESC CANCELS.");
    CHECK_EQ(key(MCKeyCommand::Stop), MCKeyBindings::Defaults()[static_cast<size_t>(MCKeyCommand::Stop)]);
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    CHECK(!list->Listening().has_value());
    CHECK_EQ(key(MCKeyCommand::Stop), MCKeyBindings::Defaults()[static_cast<size_t>(MCKeyCommand::Stop)]);
    CHECK(GlobalLog()->CurrentScreen() == GlobalLog()->PrefScreen);
    CHECK(ControlsShown());

    // Alt alone, let go, binds alt (moving it from SHOW MAP).
    RealClickCell(*list, CellOf(MCKeyCommand::MapPage));
    RealKeyEvent(SDL_SCANCODE_LALT, true, SDL_KMOD_LALT);
    CHECK(list->Listening().has_value());
    RealKeyEvent(SDL_SCANCODE_LALT, false, SDL_KMOD_NONE);
    CHECK_EQ(key(MCKeyCommand::MapPage), 0x38u | KeyAlt);
    CHECK_EQ(key(MCKeyCommand::HoldTacticalMap), MCKeyBindings::NoKey);

    // A right click clears a key.
    const MCLogKeyList::Cell eject = CellOf(MCKeyCommand::Eject);
    list->ScrollTo(eject.Row);
    const std::optional<std::pair<int32_t, int32_t>> ejectCentre = list->CellCentre(eject);
    REQUIRE(ejectCentre.has_value());
    MCScreenInput::RealMove(ejectCentre->first, ejectCentre->second);
    MCScreenInput::SendMouse(MCGuiEventType::RightButtonDown, ejectCentre->first, ejectCentre->second, false);
    Frames();
    CHECK_EQ(key(MCKeyCommand::Eject), MCKeyBindings::NoKey);

    // DONE, then CANCEL: the player's keys are as they were, and the panel edits them afresh next time.
    const std::shared_ptr<MCLogTextButton> done = FindTextButton(screen, "DONE");
    REQUIRE(done != nullptr);
    RealClickOn(*done);
    CHECK(!ControlsShown());
    CancelPrefs();
    Frames();
    CHECK(MCKeyBindings::Player() == MCKeyBindings::Defaults());
    ShowPreferences();
    Frames();
    CHECK(EditedKeys() == MCKeyBindings::Defaults());

    // Changed again, DEFAULTS puts the original's back; changed once more, ACCEPT keeps and writes it.
    ShowControls();
    Frames();
    RealClickCell(*list, CellOf(MCKeyCommand::Jump));
    MCScreenInput::RealKey(SDL_SCANCODE_K);
    const std::shared_ptr<MCLogTextButton> defaults = FindTextButton(screen, "DEFAULTS");
    REQUIRE(defaults != nullptr);
    RealClickOn(*defaults);
    CHECK(EditedKeys() == MCKeyBindings::Defaults());
    CHECK_EQ(list->Status(), "THE ORIGINAL KEYS ARE BACK.");
    RealClickCell(*list, CellOf(MCKeyCommand::Jump));
    MCScreenInput::RealKey(SDL_SCANCODE_K);
    WritePrefs();
    Frames();
    CHECK_EQ(MCKeyBindings::Player()[static_cast<size_t>(MCKeyCommand::Jump)], 0x25u);

    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(prefsPath.string()), 0);
        REQUIRE_EQ(prefs.SeekBlock("Controls"), 0);
        CHECK_EQ(prefs.Read<std::string>("Jump"), MCFitResult<std::string>("K"));
    }

    std::filesystem::remove(prefsPath);
}

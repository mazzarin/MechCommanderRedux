#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiConsole.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCConsoleCommands.h"
#include "iface/MCTacticalInterface.h"
#include "object/MCBattleMech.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMover.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "sprite/MCMechActor.h"

namespace
{
    /// <summary>Types <paramref name="text"/> into whatever has the keyboard, a character event each.</summary>
    void Type(std::string_view text)
    {
        for (const char c : text)
        {
            MCScreenInput::SendKey(static_cast<uint8_t>(c));
        }
    }

    /// <summary>Presses and lets go of the key that types <paramref name="letter"/> in the keyboard layout.</summary>
    void RealLetter(char letter)
    {
        MCScreenInput::RealKey(SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(letter), nullptr));
    }
}

/// <summary>
/// The debug console: Enter opens it with -debug only, Escape closes it without running the line, and Enter runs the
/// line and closes it; "destroyarm right" blows the selected mech's right arm off.
/// </summary>
TEST_CASE_ISOLATED("game: the debug console opens on Enter with -debug and blows an arm off")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCMover> mover = GetMoverFromPartId(0x200);
    REQUIRE(mover != nullptr);
    REQUIRE(mover->ObjectClass == MCObjectClass::BattleMech);
    const auto mech = std::static_pointer_cast<MCBattleMech>(mover);
    const auto actor = std::static_pointer_cast<MCMechActor>(mech->Appearance);
    TacticalInterface()->SelectMech(mech->PartId);

    // Without -debug, Enter does nothing.
    DebugOn = false;
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    CHECK(GuiSystem().Console == nullptr);

    // Escape closes it, and the line isn't run.
    DebugOn = true;
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    REQUIRE(GuiSystem().Console != nullptr);
    CHECK(GuiSystem().TextObject() == GuiSystem().Console);
    Type("destroyarm left");
    CHECK_EQ(GuiSystem().Console->Text, std::string("destroyarm left"));
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    CHECK(GuiSystem().Console == nullptr);
    CHECK(GuiSystem().TextObject() == nullptr);
    CHECK(!GamePaused);
    CHECK(mech->BodyAt(MechLeftArm).DamageState != 2);

    // Enter runs it.
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    REQUIRE(GuiSystem().Console != nullptr);
    Type("destroyarm right");
    MCTestGame::RunFrame(1.0f / 15.0f);
    MCScreenInput::SaveShownShot("console");
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    CHECK(GuiSystem().Console == nullptr);
    CHECK_EQ(mech->BodyAt(MechRightArm).DamageState, 2);
    CHECK(mech->BodyAt(MechLeftArm).DamageState != 2);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(actor->RightArmGone);
    CHECK(!actor->LeftArmGone);

    // What a wrong line says.
    CHECK(!RunConsoleCommand("nonsense").has_value());
    CHECK(!RunConsoleCommand("destroyarm up").has_value());
    CHECK(RunConsoleCommand("").has_value());
    TacticalInterface()->ClearMechSelection();
    CHECK(!RunConsoleCommand("destroyarm left").has_value());
}

/// <summary>
/// SDL's text input (the system's input methods: an IME, macOS's press-and-hold accents) runs only while a text field
/// has the keyboard. Elsewhere the keys make their own characters, so a cheat code typed in a mission still works; in
/// the console's line, the text is text input's and the keys make none of their own.
/// </summary>
TEST_CASE_ISOLATED("game: keys go through text input only while a text field has the keyboard")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    SDL_Window* window = &MCInput::Display()->Window();
    CHECK(GuiSystem().TextObject() == nullptr);
    CHECK(!SDL_TextInputActive(window));

    CheatsOn = true;
    const bool duh = Duh;

    for (const char letter : std::string_view("duh"))
    {
        RealLetter(letter);
    }

    CHECK(Duh != duh);
    Duh = duh;
    CheatsOn = false;

    DebugOn = true;
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    REQUIRE(GuiSystem().Console != nullptr);
    CHECK(SDL_TextInputActive(window));
    RealLetter('o');
    CHECK(GuiSystem().Console->Text.empty());
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.windowID = SDL_GetWindowID(window);
    text.text.text = "o";
    MCInput::HandleEvent(text);
    CHECK_EQ(GuiSystem().Console->Text, std::string("o"));

    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    CHECK(GuiSystem().Console == nullptr);
    CHECK(!SDL_TextInputActive(window));
    DebugOn = false;
}

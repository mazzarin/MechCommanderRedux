#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiConsole.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCConsoleCommands.h"
#include "iface/MCTacticalInterface.h"
#include "main/MCArtModes.h"
#include "object/MCBattleMech.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMover.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCRenderer.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteManager.h"

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

    /// <summary>
    /// Presses and lets go of the key that types <paramref name="letter"/> in the keyboard layout, with
    /// <paramref name="mod"/> held.
    /// </summary>
    void RealLetter(char letter, SDL_Keymod mod = SDL_KMOD_NONE)
    {
        MCScreenInput::RealKey(SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(letter), nullptr), mod);
    }

    /// <summary>How many pixels of two shown frames differ in red, green or blue (all of them, sizes apart).</summary>
    size_t DifferentPixels(const std::vector<SDL_Color>& a, const std::vector<SDL_Color>& b)
    {
        if (a.size() != b.size())
        {
            return std::max(a.size(), b.size());
        }

        return static_cast<size_t>(
            std::ranges::count_if(std::views::iota(size_t{0}, a.size()), [&](size_t at)
                                  { return a[at].r != b[at].r || a[at].g != b[at].g || a[at].b != b[at].b; }));
    }

    /// <summary>The shown frame's mean brightness (red, green and blue together, 0 to 255).</summary>
    double Brightness(const std::vector<SDL_Color>& frame)
    {
        constexpr double Channels = 3.0;
        double sum = 0.0;

        for (const SDL_Color& pixel : frame)
        {
            sum += pixel.r + pixel.g + pixel.b;
        }

        return frame.empty() ? 0.0 : sum / (Channels * static_cast<double>(frame.size()));
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
/// has the keyboard. Elsewhere the keys make their own characters, so a cheat code typed in a mission still works
/// (Command chords, the system's shortcuts, type nothing); in the console's line, the text is text input's and the keys
/// make none of their own.
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

    RealLetter('d');
    RealLetter('u');
    RealLetter('h', SDL_KMOD_LGUI);
    CHECK(Duh == duh);

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

/// <summary>
/// The console's art command switches the art from the next frame: over mission 1 drawn by the GPU, paused so frames
/// repeat, with no pack "art hd" is refused and changes nothing; with a pack "art dusk" turns HD art and dusk lighting
/// on (the next frame darker) and ties the shapes loaded before to the pack, "art night" darkens it further, "art day"
/// lights it as by day, "art hd" only draws HD art, and "art original" shows the frame exactly as it was before any
/// switch.
/// </summary>
TEST_CASE_ISOLATED("game: the debug console's art command switches the art from the next frame")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    REQUIRE(MCTestGame::StartMission(1));

    if (MCRenderer::GpuDrawing() != MCGpuDrawing::On)
    {
        std::cout << "  (skipped: no GPU renderer on this machine)\n";
        return;
    }

    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mc_tests_art_command";
    std::filesystem::remove_all(scratch);
    const std::filesystem::path pack = scratch / "pack";
    MCHdArt::SetPackFolder(scratch / "none");
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    const auto frame = [&display]
    {
        MCTestGame::RunFrame(1.0f / 15.0f);
        std::expected<std::vector<SDL_Color>, std::string> shown = display->ReadFrame();
        REQUIRE(shown.has_value());
        return std::move(*shown);
    };

    GamePaused = true;
    frame();
    const std::vector<SDL_Color> original = frame();
    CHECK_EQ(DifferentPixels(frame(), original), size_t{0});

    // Wrong lines, and HD art with no pack.
    CHECK_EQ(RunConsoleCommand("art").error_or(""), std::string("usage: art original|hd|day|dusk|night"));
    CHECK(!RunConsoleCommand("art noon").has_value());
    CHECK(!RunConsoleCommand("art hd").has_value());
    CHECK_EQ(GHdArt, 0);
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingOff));
    CHECK_EQ(DifferentPixels(frame(), original), size_t{0});

    // A pack holding a folder for a shape loaded with HD art off: switching HD art on ties the shape to it.
    const std::shared_ptr<MCSpriteManager> sprites = SpriteManager();
    REQUIRE(sprites != nullptr);
    const auto loaded =
        std::ranges::find_if(sprites->Shapes(), [](const std::shared_ptr<MCShape>& shape)
                             { return shape != nullptr && !shape->HdFolder.empty() && !shape->FrameList().empty(); });
    REQUIRE(loaded != sprites->Shapes().end());
    const std::shared_ptr<MCShape> shape = *loaded;
    std::filesystem::create_directories(pack / shape->HdFolder);
    MCHdArt::SetPackFolder(pack);
    CHECK(!MCHdArt::AttachedFolder(shape->FrameList()).has_value());

    REQUIRE_EQ(RunConsoleCommand("ART Dusk").error_or(""), std::string());
    CHECK_EQ(GHdArt, 1);
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingDusk));
    CHECK(MCHdArt::AttachedFolder(shape->FrameList()).has_value());
    const std::vector<SDL_Color> dusk = frame();
    std::cout << std::format("  brightness: {:.1f} original, {:.1f} at dusk\n", Brightness(original), Brightness(dusk));
    CHECK(Brightness(dusk) < Brightness(original));

    REQUIRE_EQ(RunConsoleCommand("art night").error_or(""), std::string());
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingNight));
    const float night = Brightness(frame());
    std::cout << std::format("  brightness: {:.1f} at night\n", night);
    CHECK(night < Brightness(dusk));

    REQUIRE_EQ(RunConsoleCommand("art day").error_or(""), std::string());
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingOn));
    CHECK(Brightness(frame()) > Brightness(dusk));

    REQUIRE_EQ(RunConsoleCommand("art hd").error_or(""), std::string());
    CHECK_EQ(GHdArt, 1);
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingOff));
    frame();

    REQUIRE_EQ(RunConsoleCommand("art original").error_or(""), std::string());
    CHECK_EQ(GHdArt, 0);
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingOff));
    CHECK_EQ(DifferentPixels(frame(), original), size_t{0});

    GamePaused = false;
    MCHdArt::SetPackFolder({});
    std::filesystem::remove_all(scratch);
}

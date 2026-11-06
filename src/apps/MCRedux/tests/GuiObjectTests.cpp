#include "stdafx.h"
#include "MCTest.h"
#include "GuiTree.h"
#include "TestGame.h"
#include "fakes/MCManualClock.h"
#include "color/MCPalette.h"
#include "color/MCWaterCycle.h"
#include "gui/MCGuiButton.h"
#include "gui/MCGuiObject.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCGuiTimerManager.h"
#include "main/MCGameContext.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "platform/MCDisplay.h"

// The GUI's object tree, buttons, callbacks and timers, on a GUI system with a 640x480 screen window and no display.

namespace
{
    /// <summary>
    /// A test context with a manual clock and a GUI system whose screen window is 640x480 (no display, fonts or art).
    /// </summary>
    struct GuiScreen
    {
        GuiScreen() : Clock(Scope.Context().SetClock(std::make_unique<MCManualClock>()))
        {
            Scope.Context().SetGuiSystem(std::make_unique<MCGuiSystem>());
            Gui = Scope.Context().GuiSystem();
            Gui->ScreenWidth = 640;
            Gui->ScreenHeight = 480;
            Gui->TimerManager = std::make_unique<MCGuiTimerManager>();
            Gui->MakeScreen(640, 480);
        }

        /// <summary>Advances the clock by <paramref name="milliseconds"/> and runs the frame callbacks.</summary>
        void Frame(uint32_t milliseconds)
        {
            Clock.Advance(static_cast<uint64_t>(milliseconds) * 1000000);
            Gui->RunFrameCallbacks();
        }

        MCTestContextScope Scope;
        MCManualClock& Clock;
        std::shared_ptr<MCGuiSystem> Gui;
    };

    /// <summary>Where each of <paramref name="objects"/> lies, in order (to compare with <c>Children</c>).</summary>
    std::vector<MCAddressKey> Objects(std::initializer_list<std::reference_wrapper<const MCGuiObject>> objects)
    {
        std::vector<MCAddressKey> keys;

        for (const MCGuiObject& object : objects)
        {
            keys.push_back(MCAddressKey::Of(object));
        }

        return keys;
    }

    /// <summary>An event's type and data.</summary>
    using Event = std::pair<int32_t, int32_t>;
    using Events = std::vector<Event>;

    /// <summary>An object that keeps the type and data of every event it is given.</summary>
    class MCEventRecorder : public MCGuiObject
    {
    public:
        void HandleEvent(MCGuiEvent& event) override
        {
            Events.emplace_back(event.Type, event.Data);
            MCGuiObject::HandleEvent(event);
        }

        /// <summary>How many events of <paramref name="type"/> came.</summary>
        int32_t Count(int32_t type) const
        {
            return static_cast<int32_t>(std::ranges::count(Events, type, &Event::first));
        }

        ::Events Events;
    };

    /// <summary>
    /// A new object of type <typeparamref name="T"/> made on <paramref name="parent"/> at (x, y) with the given size
    /// (the parent owns it).
    /// </summary>
    template <typename T = MCGuiObject>
    MCGuiHandle<T> Make(int32_t x, int32_t y, int32_t width, int32_t height, MCGuiObject& parent)
    {
        return MCGuiTree::MakeChild<T>(parent, x, y, width, height);
    }

    /// <summary>
    /// A new object made on <paramref name="parent"/> at (x, y) with the given size and <paramref name="depth"/>, set
    /// before it is added.
    /// </summary>
    MCGuiHandle<MCGuiObject> MakeAtDepth(int32_t x, int32_t y, int32_t width, int32_t height, int32_t depth,
                                         MCGuiObject& parent)
    {
        return parent.CreateChild<MCGuiObject>(
            [&](MCGuiObject& made)
            {
                made.Init(x, y, width, height, {});
                made.SetDepth(depth);
            });
    }

    /// <summary>A mouse event of <paramref name="type"/> at screen point (x, y).</summary>
    MCGuiEvent Mouse(int32_t type, int32_t x, int32_t y)
    {
        MCGuiEvent event;
        event.Type = type;
        event.X = x;
        event.Y = y;
        return event;
    }

    /// <summary>Where the children of <paramref name="parent"/> lie, back to front.</summary>
    std::vector<MCAddressKey> Children(MCGuiObject& parent)
    {
        std::vector<MCAddressKey> children;

        for (int32_t i = 0; i < parent.NumberOfChildren(); i++)
        {
            children.push_back(MCAddressKey::Of(*parent.Child(i)));
        }

        return children;
    }
}

TEST_CASE("gui: a child sits on its parent, in front of the children of its depth and behind deeper ones")
{
    GuiScreen screen;
    auto window = Make(100, 50, 200, 100, *ScreenWindow());
    auto first = Make(10, 20, 30, 30, *window);
    auto second = Make(40, 20, 30, 30, *window);

    CHECK(window == first->ParentObject());
    CHECK(Children(*window) == Objects({*first, *second}));
    CHECK_EQ(second->GlobalX(), 140);
    CHECK_EQ(second->GlobalY(), 70);
    CHECK_EQ(second->Frame().X0, 140);
    CHECK_EQ(second->Frame().X1, 169);
    CHECK_EQ(second->Frame().Y1, 99);

    // Children are kept in depth order: a lower depth is behind, a higher one in front.
    auto back = MakeAtDepth(0, 0, 10, 10, -1, *window);
    auto front = MakeAtDepth(0, 0, 10, 10, 5, *window);
    auto third = Make(70, 20, 30, 30, *window);
    CHECK(Children(*window) == Objects({*back, *first, *second, *third, *front}));
    CHECK(window->ForemostChild(0) == third);
    CHECK(window->ForemostChild(5) == front);
    CHECK(window->ForemostChild(3) == nullptr);

    // Raising one brings it to the front of its own depth only.
    first->BringToFront(true);
    CHECK(Children(*window) == Objects({*back, *second, *third, *first, *front}));

    // Taking a child off destroys it: its parent was its owner.
    window->RemoveChild(*second);
    CHECK(!second);
    CHECK_EQ(window->NumberOfChildren(), 4);

    // Closing a child takes it off its parent, and destroys it.
    third->Close();
    CHECK(!third);
    CHECK(Children(*window) == Objects({*back, *first, *front}));
}

TEST_CASE("gui: no limit on a window's children")
{
    GuiScreen screen;
    auto window = Make(0, 0, 640, 480, *ScreenWindow());
    std::vector<MCGuiHandle<MCGuiObject>> children;

    for (int32_t i = 0; i < 300; i++)
    {
        children.push_back(Make(i, 0, 1, 1, *window));
    }

    CHECK_EQ(window->NumberOfChildren(), 300);
    CHECK(window->Child(299) == children.back());
    CHECK(window->Child(300) == nullptr);
}

TEST_CASE("gui: a point finds the front-most shown object under it")
{
    GuiScreen screen;
    auto window = Make(100, 100, 200, 200, *ScreenWindow());
    auto under = Make(10, 10, 50, 50, *window);
    auto over = Make(30, 30, 50, 50, *window);

    CHECK(ScreenWindow()->FindObject(145, 145) == over);
    CHECK(ScreenWindow()->FindObject(115, 115) == under);
    CHECK(ScreenWindow()->FindObject(250, 250) == window);
    CHECK(ScreenWindow()->FindObject(5, 5) == ScreenWindow());

    // The right and bottom edges are inside, the pixel past them isn't.
    CHECK(ScreenWindow()->FindObject(179, 179) == over);
    CHECK(ScreenWindow()->FindObject(180, 180) == window);

    under->BringToFront(true);
    CHECK(ScreenWindow()->FindObject(145, 145) == under);

    // A hidden window and its children aren't found.
    under->ShowGuiWindow(false);
    CHECK(ScreenWindow()->FindObject(145, 145) == over);
    window->ShowGuiWindow(false);
    CHECK(ScreenWindow()->FindObject(145, 145) == ScreenWindow());
}

TEST_CASE("gui: moving a window moves its children, and a temporary move keeps its home")
{
    GuiScreen screen;
    auto window = Make(100, 50, 200, 100, *ScreenWindow());
    auto child = Make(10, 20, 30, 30, *window);
    auto grandchild = Make(1, 2, 5, 5, *child);

    window->MoveTo(300, 200);
    CHECK_EQ(child->Frame().X0, 310);
    CHECK_EQ(child->Frame().Y0, 220);
    CHECK_EQ(grandchild->Frame().X0, 311);
    CHECK_EQ(grandchild->Frame().Y0, 222);
    CHECK_EQ(child->X(), 10);
    CHECK_EQ(window->HomeX, 300);
    CHECK_EQ(window->HomeY, 200);

    window->MoveTo(0, 0, true);
    CHECK_EQ(child->Frame().X0, 10);
    CHECK_EQ(window->HomeX, 300);
    CHECK_EQ(window->HomeY, 200);

    window->Resize(120, 80);
    CHECK_EQ(window->Width(), 120);
    CHECK_EQ(window->Frame().X1, 119);
    CHECK_EQ(window->Frame().Y1, 79);
    CHECK_EQ(window->Port().Bitmap().XMax, 119);

    // On the 40-pixel grid.
    window->GridAligned = true;
    window->Resize(130, 90);
    CHECK_EQ(window->Width() % 40, 0);
    CHECK_EQ(window->Height() % 40, 0);
}

TEST_CASE("gui: the screen-resized event reaches every child")
{
    GuiScreen screen;
    auto window = Make<MCEventRecorder>(0, 0, 200, 100, *ScreenWindow());
    auto child = Make<MCEventRecorder>(10, 10, 30, 30, *window);
    auto grandchild = Make<MCEventRecorder>(1, 1, 5, 5, *child);

    MCGuiEvent event;
    event.Type = MCGuiEventType::ScreenResized;
    window->HandleEvent(event);
    CHECK_EQ(window->Count(MCGuiEventType::ScreenResized), 1);
    CHECK_EQ(child->Count(MCGuiEventType::ScreenResized), 1);
    CHECK_EQ(grandchild->Count(MCGuiEventType::ScreenResized), 1);
}

TEST_CASE("gui: a click raises a window among its siblings")
{
    GuiScreen screen;
    auto window = Make(0, 0, 400, 400, *ScreenWindow());
    auto left = Make(0, 0, 100, 100, *window);
    auto right = Make(50, 0, 100, 100, *window);

    MCGuiEvent click = Mouse(MCGuiEventType::LeftButtonDown, 60, 10);
    left->HandleEvent(click);
    CHECK(Children(*window) == Objects({*right, *left}));
    CHECK(ScreenWindow()->FindObject(60, 10) == left);
}

TEST_CASE("gui: the text focus tells the old and new owners, and a destroyed object lets go of the focus")
{
    GuiScreen screen;
    auto first = Make<MCEventRecorder>(0, 0, 10, 10, *ScreenWindow());
    auto second = Make<MCEventRecorder>(20, 0, 10, 10, *ScreenWindow());

    screen.Gui->SetText(first);
    CHECK(first->Events == Events({Event(MCGuiEventType::Focus, 7)}));
    screen.Gui->SetText(second);
    CHECK(first->Events.back() == Event(MCGuiEventType::Focus, 8));
    CHECK(second->Events.back() == Event(MCGuiEventType::Focus, 7));
    CHECK(second == screen.Gui->TextObject());

    screen.Gui->Grab(second);
    screen.Gui->SetModalObject(*second);
    screen.Gui->SetCurrentObject(second);
    second->Close();
    CHECK(screen.Gui->TextObject() == nullptr);
    CHECK(screen.Gui->GrabbedObject() == nullptr);
    CHECK(screen.Gui->ModalObject() == nullptr);
    CHECK(screen.Gui->CurrentObject() != nullptr);
    CHECK(!(first == screen.Gui->CurrentObject()) || first->PointInside(0, 0));
}

TEST_CASE("gui: a button runs its callback when released over it after being pressed on it")
{
    GuiScreen screen;
    auto window = Make(100, 100, 200, 200, *ScreenWindow());
    auto button = Make<MCGuiButton>(10, 10, 40, 20, *window);
    int32_t left = 0;
    int32_t right = 0;
    button->Callback().SetExec([&] { left++; });
    button->RightCallback().SetExec([&] { right++; });

    const auto send = [&](int32_t type, int32_t x, int32_t y)
    {
        MCGuiEvent event = Mouse(type, x, y);
        button->HandleEvent(event);
    };

    send(MCGuiEventType::LeftButtonDown, 115, 115);
    CHECK(button == screen.Gui->GrabbedObject());
    send(MCGuiEventType::LeftButtonUp, 149, 129);
    CHECK_EQ(left, 1);
    CHECK(screen.Gui->GrabbedObject() == nullptr);

    // Released off the button (its right and bottom edges are outside): nothing.
    send(MCGuiEventType::LeftButtonDown, 115, 115);
    send(MCGuiEventType::LeftButtonUp, 150, 115);
    CHECK_EQ(left, 1);
    CHECK(screen.Gui->GrabbedObject() == nullptr);

    // Released without a press on it: nothing.
    send(MCGuiEventType::LeftButtonUp, 115, 115);
    CHECK_EQ(left, 1);

    send(MCGuiEventType::RightButtonDown, 115, 115);
    send(MCGuiEventType::RightButtonUp, 115, 115);
    CHECK_EQ(right, 1);
    CHECK_EQ(left, 1);

    // A disabled button ignores the mouse.
    button->Disabled = true;
    send(MCGuiEventType::LeftButtonDown, 115, 115);
    CHECK(screen.Gui->GrabbedObject() == nullptr);
    send(MCGuiEventType::LeftButtonUp, 115, 115);
    CHECK_EQ(left, 1);
}

TEST_CASE("gui: a tool button flips and runs its callback on each press")
{
    GuiScreen screen;
    auto window = Make(0, 0, 200, 200, *ScreenWindow());
    auto button = Make<MCGuiToolButton>(10, 10, 40, 20, *window);
    int32_t runs = 0;
    button->Callback().SetExec([&] { runs++; });

    MCGuiEvent press = Mouse(MCGuiEventType::LeftButtonDown, 15, 15);
    button->HandleEvent(press);
    CHECK(button->Pushed);
    CHECK_EQ(runs, 1);
    button->HandleEvent(press);
    CHECK(!button->Pushed);
    CHECK_EQ(runs, 2);

    button->Disabled = true;
    button->HandleEvent(press);
    CHECK(!button->Pushed);
    CHECK_EQ(runs, 2);
}

TEST_CASE("gui: a callback posts its message after its function, unless the function deleted it (OB-109)")
{
    GuiScreen screen;
    auto target = Make<MCEventRecorder>(0, 0, 10, 10, *ScreenWindow());
    constexpr int32_t message = MCGuiEventType::FirstPosted + 3;

    auto callback = std::make_shared<MCGuiCallback>();
    int32_t runs = 0;
    callback->SetExec([&] { runs++; });
    callback->SetMessage(target, message);
    MCGuiCallback::Run(callback);
    CHECK_EQ(runs, 1);
    CHECK_EQ(target->Count(message), 1);

    // As the mech bar's dance does: the function deletes its own callback (its holder lets go).
    auto owned = std::make_shared<MCGuiCallback>();
    const std::weak_ptr<MCGuiCallback> doomed = owned;
    owned->SetExec([&] { owned.reset(); });
    owned->SetMessage(target, message);
    MCGuiCallback::Run(doomed);
    CHECK(owned == nullptr);
    CHECK(doomed.expired());
    CHECK_EQ(target->Count(message), 1);

    // A cleared callback does nothing, and neither does one that is gone or whose object is gone.
    callback->Clear();
    MCGuiCallback::Run(callback);
    MCGuiCallback::Run(doomed);
    CHECK_EQ(runs, 1);
    CHECK_EQ(target->Count(message), 1);
    callback->SetMessage(target, message);
    target->Close();
    MCGuiCallback::Run(callback);
}

TEST_CASE("gui: a repeating timer fires once its interval has passed, then an interval after that")
{
    GuiScreen screen;
    auto target = Make<MCEventRecorder>(0, 0, 10, 10, *ScreenWindow());
    REQUIRE_EQ(screen.Gui->AddTimer(target, 7, 100, 0, 0, false), 0);
    CHECK_EQ(screen.Gui->Callbacks().size(), 1u);

    screen.Frame(100);
    CHECK_EQ(target->Count(MCGuiEventType::Timer), 0);
    screen.Frame(1);
    REQUIRE_EQ(target->Count(MCGuiEventType::Timer), 1);
    CHECK_EQ(target->Events.back().second, 7);

    screen.Frame(100);
    CHECK_EQ(target->Count(MCGuiEventType::Timer), 1);
    screen.Frame(1);
    CHECK_EQ(target->Count(MCGuiEventType::Timer), 2);
    CHECK_EQ(screen.Gui->TimerManager->NumTimers(), 1);

    // Destroying the target takes its timers, and the last timer takes the timers' callback.
    target->Close();
    CHECK_EQ(screen.Gui->TimerManager->NumTimers(), 0);
    CHECK(screen.Gui->Callbacks().empty());
}

TEST_CASE("gui: a one-shot timer sends its own event once and goes")
{
    GuiScreen screen;
    auto target = Make<MCEventRecorder>(0, 0, 10, 10, *ScreenWindow());
    constexpr int32_t type = MCGuiEventType::FirstPosted + 5;
    REQUIRE_EQ(screen.Gui->AddTimer(target, 1, 50, type, 42, false), 0);

    screen.Frame(51);
    REQUIRE_EQ(target->Count(type), 1);
    CHECK_EQ(target->Events.back().second, 42);
    CHECK_EQ(screen.Gui->TimerManager->NumTimers(), 0);
    CHECK(screen.Gui->Callbacks().empty());

    screen.Frame(100);
    CHECK_EQ(target->Count(type), 1);
}

TEST_CASE("gui: a timer on scenario time waits for the scenario's clock, not the real one")
{
    GuiScreen screen;
    auto target = Make<MCEventRecorder>(0, 0, 10, 10, *ScreenWindow());
    const float savedTime = ScenarioTime;
    ScenarioTime = 10.0f;
    REQUIRE_EQ(screen.Gui->AddTimer(target, 3, 500, 0, 0, true), 0);

    screen.Frame(5000);
    CHECK_EQ(target->Count(MCGuiEventType::Timer), 0);
    ScenarioTime = 10.501f;
    screen.Frame(0);
    CHECK_EQ(target->Count(MCGuiEventType::Timer), 1);
    ScenarioTime = savedTime;
}

TEST_CASE("gui: a timer's handler removes its own timer at once, the next timer waiting a frame")
{
    GuiScreen screen;
    auto target = Make<MCEventRecorder>(0, 0, 10, 10, *ScreenWindow());
    auto other = Make<MCEventRecorder>(0, 0, 10, 10, *ScreenWindow());
    REQUIRE_EQ(screen.Gui->AddTimer(target, 1, 10, 0, 0, false), 0);
    REQUIRE_EQ(screen.Gui->AddTimer(other, 2, 10, 0, 0, false), 0);
    int32_t timersInHandler = -1;
    target->SetEventRoutine(
        [&](MCGuiObject& object, MCGuiEvent& event)
        {
            if (event.Type == MCGuiEventType::Timer)
            {
                GuiSystem().RemoveTimer(object, 1);
                timersInHandler = GuiSystem().TimerManager->NumTimers();
            }
        });

    screen.Frame(11);
    CHECK_EQ(target->Count(MCGuiEventType::Timer), 1);
    CHECK_EQ(timersInHandler, 1);
    REQUIRE_EQ(screen.Gui->TimerManager->NumTimers(), 1);
    CHECK(screen.Gui->TimerManager->GetTimer(0)->Target.lock() == other);
    CHECK_EQ(other->Count(MCGuiEventType::Timer), 0);

    screen.Frame(0);
    CHECK_EQ(target->Count(MCGuiEventType::Timer), 1);
    CHECK_EQ(other->Count(MCGuiEventType::Timer), 1);
}

TEST_CASE("gui: a timer's handler removing another timer takes effect once the handler returns")
{
    GuiScreen screen;
    auto target = Make<MCEventRecorder>(0, 0, 10, 10, *ScreenWindow());
    auto other = Make<MCEventRecorder>(0, 0, 10, 10, *ScreenWindow());
    REQUIRE_EQ(screen.Gui->AddTimer(target, 1, 10, 0, 0, false), 0);
    REQUIRE_EQ(screen.Gui->AddTimer(other, 2, 10, 0, 0, false), 0);
    int32_t timersInHandler = -1;
    target->SetEventRoutine(
        [&](MCGuiObject&, MCGuiEvent& event)
        {
            if (event.Type == MCGuiEventType::Timer)
            {
                GuiSystem().RemoveTimers(*other);
                timersInHandler = GuiSystem().TimerManager->NumTimers();
            }
        });

    screen.Frame(11);
    CHECK_EQ(timersInHandler, 2);
    REQUIRE_EQ(screen.Gui->TimerManager->NumTimers(), 1);
    CHECK(screen.Gui->TimerManager->GetTimer(0)->Target.lock() == target);
    CHECK_EQ(other->Count(MCGuiEventType::Timer), 0);
}

TEST_CASE("gui: a timer can't be added twice as a unique timer, and there is no limit on timers")
{
    GuiScreen screen;
    auto target = Make<MCEventRecorder>(0, 0, 10, 10, *ScreenWindow());
    CHECK_EQ(screen.Gui->AddUniqueTimer(target, 1, 10, 0, 0, false), 0);
    CHECK_EQ(screen.Gui->AddUniqueTimer(target, 1, 10, 0, 0, false), -1);
    CHECK_EQ(screen.Gui->AddUniqueTimer(target, 1, 20, 0, 0, false), 0);

    for (int16_t id = 2; id < 202; id++)
    {
        REQUIRE_EQ(screen.Gui->AddTimer(target, id, 10, 0, 0, false), 0);
    }

    CHECK_EQ(screen.Gui->TimerManager->NumTimers(), 202);
    screen.Frame(11);
    CHECK_EQ(target->Count(MCGuiEventType::Timer), 201);
    screen.Frame(10);
    CHECK_EQ(target->Count(MCGuiEventType::Timer), 202);
}

TEST_CASE("gui: the water colours cycle once per cycle length while palette cycling is on")
{
    GuiScreen screen;

    // A 256-colour palette file: a 4-byte header with the count, then 6-bit RGB triples.
    std::vector<uint8_t> file = {0, 0, 0, 1};

    for (int32_t i = 0; i < 256; i++)
    {
        file.insert(file.end(), {static_cast<uint8_t>(i % 64), static_cast<uint8_t>(i / 4), 0});
    }

    screen.Scope.Context().SetPalette(std::make_unique<MCPalette>(file, std::vector<uint8_t>{}, 0));
    screen.Scope.Context().SetScenario(std::make_unique<MCScenario>());
    Scenario()->CycleLength = 0.5f;
    const uint8_t savedMagic = CurrentMagic;

    // Off: the cycle's time passes, the colours stay.
    screen.Gui->PaletteCycle = 0;
    screen.Clock.Advance(10'000'000'000ull);
    CycleColors();
    CurrentMagic = 0;
    screen.Clock.Advance(600'000'000ull);
    CycleColors();
    CHECK_EQ(CurrentMagic, 0);

    // On: a step once more than the cycle length has passed since the last one.
    screen.Gui->PaletteCycle = 1;
    screen.Clock.Advance(400'000'000ull);
    CycleColors();
    CHECK_EQ(CurrentMagic, 0);
    screen.Clock.Advance(101'000'000ull);
    CycleColors();
    CHECK_EQ(CurrentMagic, 1);
    CHECK_EQ(GamePalette().Colors()[FirstWaterColor].R, WaterMagicColors[0] % 64);
    screen.Clock.Advance(500'000'000ull);
    CycleColors();
    CHECK_EQ(CurrentMagic, 1);
    screen.Clock.Advance(1'000'000ull);
    CycleColors();
    CHECK_EQ(CurrentMagic, 2);
    CurrentMagic = savedMagic;
}

namespace
{
    /// <summary>Display palette entries [first, first + count).</summary>
    std::vector<MCVfxRgb> Shown(int32_t first, int32_t count)
    {
        std::vector<MCVfxRgb> colors(static_cast<size_t>(count));
        GuiSystem().Display()->GetPalette(first, colors);
        return colors;
    }

    /// <summary>Whether two colours are the same.</summary>
    bool Same(const MCVfxRgb& a, const MCVfxRgb& b)
    {
        return a.R == b.R && a.G == b.G && a.B == b.B;
    }

    /// <summary>Whether two lists of colours are the same.</summary>
    bool Same(const std::vector<MCVfxRgb>& a, const std::vector<MCVfxRgb>& b)
    {
        return std::ranges::equal(a, b, [](const MCVfxRgb& x, const MCVfxRgb& y) { return Same(x, y); });
    }
}

TEST_CASE_ISOLATED("game: a palette change shows the game's colours through the gamma level, and leaves Windows' own")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCGuiSystem& gui = GuiSystem();
    REQUIRE(gui.Display() != nullptr);
    gui.GammaCorrectCurrentPalette(0);
    const std::vector<MCVfxRgb> windows = Shown(0, 10);
    const std::vector<MCVfxRgb> windowsHigh = Shown(0xf6, 10);

    // Six-bit colours are made 8-bit; entries 0..9 and 246..255 stay Windows'.
    std::array<MCVfxRgb, 256> colors = {};

    for (size_t i = 0; i < colors.size(); i++)
    {
        colors[i] = {static_cast<uint8_t>(i % 64), static_cast<uint8_t>((i * 7) % 64), static_cast<uint8_t>(20)};
    }

    gui.TweakPalette(0, 256, colors, true);
    const std::vector<MCVfxRgb> plain = Shown(10, 236);
    CHECK(Same(Shown(0, 10), windows));
    CHECK(Same(Shown(0xf6, 10), windowsHigh));

    for (size_t i = 10; i < 0xf6; i++)
    {
        MCTest::Scope entry(std::format("entry {}", i));
        CHECK_EQ(gui.CurrentPalette[i].R, colors[i].R << 2);
        CHECK_EQ(plain[i - 10].G, colors[i].G << 2);
        CHECK_EQ(plain[i - 10].B, 80);
    }

    // Each gamma level brightens the game's colours one more step; the current palette keeps them unbrightened.
    gui.GammaCorrectCurrentPalette(1);
    const std::vector<MCVfxRgb> level1 = Shown(10, 236);
    gui.GammaCorrectCurrentPalette(2);
    const std::vector<MCVfxRgb> level2 = Shown(10, 236);
    gui.GammaCorrectCurrentPalette(3);
    const std::vector<MCVfxRgb> level3 = Shown(10, 236);
    CHECK_EQ(gui.CurrentPalette[100].B, 80);
    CHECK(level1[0].B > plain[0].B);
    CHECK(level2[0].B > level1[0].B);
    CHECK(level3[0].B > level2[0].B);
    CHECK(Same(Shown(0, 10), windows));

    // GammaCorrectCurrentPalette() steps to the next level, from 3 back to 0.
    gui.GammaCorrectCurrentPalette();
    CHECK_EQ(gui.GammaLevel, 0);
    CHECK(Same(Shown(10, 236), plain));

    // A palette set at a gamma level shows through it: level 1 once, level 2 twice...
    gui.GammaCorrectCurrentPalette(1);
    gui.TweakPalette(10, 236, std::span(colors).subspan(10), true);
    CHECK(Same(Shown(10, 236), level1));
    gui.GammaCorrectCurrentPalette(2);
    gui.TweakPalette(10, 236, std::span(colors).subspan(10), true);
    CHECK(Same(Shown(10, 236), level2));

    // ...but level 3 only once, as at level 1 (OB-159).
    gui.GammaCorrectCurrentPalette(3);
    gui.TweakPalette(10, 236, std::span(colors).subspan(10), true);
    CHECK(Same(Shown(10, 236), level1));
    gui.GammaCorrectCurrentPalette(0);
}

TEST_CASE_ISOLATED("game: a whole palette waits for the next frame, part of one shows at once")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    MCGuiSystem& gui = GuiSystem();
    gui.GammaCorrectCurrentPalette(0);
    std::array<MCVfxRgb, 256> colors = {};
    colors.fill({1, 2, 3});
    const std::vector<MCVfxRgb> before = Shown(10, 236);

    gui.ActivatePalette(std::span(reinterpret_cast<const uint8_t*>(colors.data()), colors.size() * 3), 0, 256);
    CHECK_EQ(gui.PendingPaletteCount, 256);
    CHECK(Same(Shown(10, 236), before));

    gui.ActivatePalette(std::span(reinterpret_cast<const uint8_t*>(colors.data()), colors.size() * 3), 20, 5);
    const std::vector<MCVfxRgb> part = Shown(20, 5);
    CHECK(std::ranges::all_of(part, [](const MCVfxRgb& color) { return Same(color, {4, 8, 12}); }));
    CHECK(Same(Shown(10, 10), std::vector<MCVfxRgb>(before.begin(), before.begin() + 10)));
}

#include "stdafx.h"
#include "MCTest.h"
#include "GuiTree.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fakes/MCManualClock.h"
#include "gui/MCGuiButton.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiObject.h"
#include "gui/MCGuiScrollTextObject.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCGuiTimerManager.h"
#include "lib/MCFatal.h"
#include "logistics/MCCompPurchaseBlock.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCTicker.h"
#include "logistics/MCTickerLine.h"
#include "main/MCGameContext.h"
#include "main/MCInventoryList.h"
#include "main/MCLogistics.h"

// How GUI objects are destroyed: the tree owns them, a parent's destroy destroys its children, destroy is an event on
// an object that is still alive (as the original's and Windows' WM_DESTROY), and nothing outside the tree keeps a UI
// object alive. A failed Assert (the old "Number of Children NOT Zero") fails the test that sees it.

using namespace MCGuiTree;
using namespace MCScreenInput;

namespace
{
    /// <summary>
    /// A test context with a manual clock and a GUI system whose screen window is 640x480 (no display or art), and every
    /// font a letterless one.
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
            SetFonts(NoLetters);
        }

        ~GuiScreen() { SetFonts(nullptr); }

        GuiScreen(const GuiScreen&) = delete;
        GuiScreen& operator=(const GuiScreen&) = delete;

        /// <summary>Points the font globals and the font table at <paramref name="font"/>.</summary>
        static void SetFonts(const std::shared_ptr<MCGuiFont>& font)
        {
            for (std::shared_ptr<MCGuiFont>& global :
                 {std::ref(GreyFont), std::ref(WhiteFont), std::ref(BlackFont), std::ref(LgGreyFont),
                  std::ref(LgWhiteFont), std::ref(MedWhiteFont)})
            {
                global = font;
            }

            for (auto& row : Fonts)
            {
                row.fill(font);
            }
        }

        /// <summary>Advances the clock by <paramref name="milliseconds"/> and runs the frame callbacks.</summary>
        void Frame(uint32_t milliseconds)
        {
            Clock.Advance(static_cast<uint64_t>(milliseconds) * 1000000);
            Gui->RunFrameCallbacks();
        }

        /// <summary>Tears the GUI system down, as the game does when it ends.</summary>
        void TearDown()
        {
            Gui.reset();
            Scope.Context().SetGuiSystem(nullptr);
        }

        MCTestContextScope Scope;
        MCManualClock& Clock;
        std::shared_ptr<MCGuiSystem> Gui;
        std::shared_ptr<MCGuiFont> NoLetters = std::make_shared<MCGuiFont>();
    };

    /// <summary>What a probe saw as it was destroyed.</summary>
    struct DestroyRecord
    {
        /// <summary>The probe's name.</summary>
        std::string Name;
        /// <summary>Whether it was still alive (a handle to it could be locked): destroy is an event on a live object.</summary>
        bool Live = false;
        /// <summary>Whether it still had a parent: a child destroyed by its parent's destroy has none by then.</summary>
        bool HasParent = false;
        /// <summary>How many children it had: a parent's own cleanup runs while its children are still on it.</summary>
        int32_t Children = 0;

        bool operator==(const DestroyRecord&) const = default;
    };

    /// <summary>The destroys the probes of one test saw, in order.</summary>
    using DestroyLog = std::vector<DestroyRecord>;

    /// <summary>
    /// A GUI object of type <typeparamref name="Base"/> that writes a <see cref="DestroyRecord"/> into a shared log when
    /// its own cleanup runs.
    /// </summary>
    template <typename Base = MCGuiObject> class MCDestroyProbe : public Base
    {
    public:
        MCDestroyProbe(std::shared_ptr<DestroyLog> log, std::string name) : _Log(std::move(log)), _Name(std::move(name))
        {
        }

        /// <summary>The class's own cleanup: records what the probe sees, then the base's.</summary>
        void OnDestroy() override
        {
            _Log->push_back(
                {_Name, !this->weak_from_this().expired(), !this->Parent.expired(), this->NumberOfChildren()});
            Base::OnDestroy();
        }

    private:
        std::shared_ptr<DestroyLog> _Log;
        std::string _Name;
    };

    /// <summary>An object that counts how often it is displayed.</summary>
    class MCDisplayCounter : public MCGuiObject
    {
    public:
        void Display() override
        {
            Displays++;
            MCGuiObject::Display();
        }

        /// <summary>How often it was displayed.</summary>
        int32_t Displays = 0;
    };

    /// <summary>An object that counts its timer events.</summary>
    class MCTimerCounter : public MCGuiObject
    {
    public:
        void HandleEvent(MCGuiEvent& event) override
        {
            if (event.Type == MCGuiEventType::Timer)
            {
                Timers++;
            }

            MCGuiObject::HandleEvent(event);
        }

        /// <summary>How many timer events came.</summary>
        int32_t Timers = 0;
    };

    /// <summary>A record of a probe destroyed while alive.</summary>
    DestroyRecord Destroyed(std::string_view name, bool hasParent, int32_t children)
    {
        return {std::string(name), true, hasParent, children};
    }

    /// <summary>The destroys in <paramref name="log"/>, in order, for a failure message.</summary>
    std::string Names(const DestroyLog& log)
    {
        if (log.empty())
        {
            return "destroyed: none";
        }

        std::string names = "destroyed:";

        for (const DestroyRecord& record : log)
        {
            names += std::format(" {}{}{}({} children)", record.Name, record.Live ? "" : "(dead)",
                                 record.HasParent ? "(on its parent)" : "", record.Children);
        }

        return names;
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
}

TEST_CASE("gui destroy: a parent's destroy destroys its child once, alive, after the parent's own cleanup")
{
    GuiScreen screen;
    const auto log = std::make_shared<DestroyLog>();
    auto parent = MakeChild<MCDestroyProbe<>>(*ScreenWindow(), 10, 10, 200, 100, log, "parent");
    auto child = MakeChild<MCDestroyProbe<>>(*parent, 5, 5, 20, 20, log, "child");
    const auto parentHandle = HandleOf(parent);
    const auto childHandle = HandleOf(child);

    {
        // A pin keeps the parent's memory, to look at its list once it is destroyed.
        const auto pin = Pin(parentHandle);
        Close(*parent);
        MCTest::Scope destroys(Names(*log));
        CHECK(!Alive(parentHandle));
        CHECK(!Alive(childHandle));
        // The parent's cleanup runs with its child still on it; the child's after, taken off its parent first.
        CHECK(*log == DestroyLog({Destroyed("parent", true, 1), Destroyed("child", false, 0)}));
        REQUIRE(pin != nullptr);
        CHECK_EQ(pin->NumberOfChildren(), 0);
    }

    CHECK_EQ(ScreenWindow()->NumberOfChildren(), 0);
}

TEST_CASE("gui destroy: three levels deep, every level is destroyed once, the deepest last")
{
    GuiScreen screen;
    const auto log = std::make_shared<DestroyLog>();
    auto window = MakeChild<MCDestroyProbe<>>(*ScreenWindow(), 0, 0, 400, 300, log, "screen");
    auto pane = MakeChild<MCDestroyProbe<>>(*window, 10, 10, 300, 200, log, "pane");
    auto text = MakeChild<MCDestroyProbe<MCGuiScrollTextObject>>(*pane, 10, 10, 100, 40, log, "text");
    REQUIRE(!text->InitFailed);
    const auto scrollBar = HandleOf(text->ScrollTab);
    REQUIRE(Alive(scrollBar));
    const auto windowHandle = HandleOf(window);
    const auto paneHandle = HandleOf(pane);
    const auto textHandle = HandleOf(text);

    Close(*window);
    MCTest::Scope destroys(Names(*log));
    CHECK(*log == DestroyLog({Destroyed("screen", true, 1), Destroyed("pane", false, 1), Destroyed("text", false, 1)}));
    CHECK(!Alive(windowHandle));
    CHECK(!Alive(paneHandle));
    CHECK(!Alive(textHandle));
    CHECK(!Alive(scrollBar));
}

TEST_CASE("gui destroy: closing a child destroys it and takes it off its parent")
{
    GuiScreen screen;
    const auto log = std::make_shared<DestroyLog>();
    auto window = MakeChild<MCDestroyProbe<>>(*ScreenWindow(), 0, 0, 200, 200, log, "window");
    auto child = MakeChild<MCDestroyProbe<>>(*window, 10, 10, 20, 20, log, "child");
    auto sibling = MakeChild<MCDestroyProbe<>>(*window, 40, 10, 20, 20, log, "sibling");
    const auto childHandle = HandleOf(child);

    Close(*child);
    MCTest::Scope destroys(Names(*log));
    // Destroyed on its own, it still has its parent while its cleanup runs.
    CHECK(*log == DestroyLog({Destroyed("child", true, 0)}));
    CHECK(!Alive(childHandle));
    CHECK_EQ(window->NumberOfChildren(), 1);
    CHECK(window->Child(0) == Pin(HandleOf(sibling)));
}

TEST_CASE("gui destroy: a handle held outside the tree is empty once its screen is destroyed")
{
    GuiScreen screen;

    /// <summary>As logistics holds the views on its screens: by handle.</summary>
    struct ViewHolder
    {
        decltype(HandleOf(std::shared_ptr<MCGuiObject>())) View;
    };

    auto window = MakeChild<MCGuiObject>(*ScreenWindow(), 0, 0, 640, 480);
    auto view = MakeChild<MCGuiObject>(*window, 10, 10, 100, 100);
    ViewHolder holder{HandleOf(view)};
    CHECK(Alive(holder.View));

    Close(*window);
    CHECK(!Alive(holder.View));
    CHECK(Pin(holder.View) == nullptr);
}

TEST_CASE("gui destroy: a button's callback can close the window it is on")
{
    GuiScreen screen;
    const auto log = std::make_shared<DestroyLog>();
    auto window = MakeChild<MCDestroyProbe<>>(*ScreenWindow(), 100, 100, 200, 200, log, "window");
    auto button = MakeChild<MCDestroyProbe<MCGuiButton>>(*window, 10, 10, 40, 20, log, "button");
    const auto windowHandle = HandleOf(window);
    const auto buttonHandle = HandleOf(button);
    bool ranOn = false;
    button->Callback().SetExec(
        [&]
        {
            // As the version box's OK does: the handler closes the window its button is on, and carries on.
            if (const auto shown = Pin(windowHandle); shown != nullptr)
            {
                Close(*shown);
            }

            ranOn = true;
        });

    MCGuiEvent press = Mouse(MCGuiEventType::LeftButtonDown, 115, 115);
    button->HandleEvent(press);
    MCGuiEvent release = Mouse(MCGuiEventType::LeftButtonUp, 115, 115);
    button->HandleEvent(release);

    MCTest::Scope destroys(Names(*log));
    CHECK(ranOn);
    CHECK(*log == DestroyLog({Destroyed("window", true, 1), Destroyed("button", false, 0)}));
    CHECK(!Alive(windowHandle));
    CHECK(!Alive(buttonHandle));
    CHECK(screen.Gui->GrabbedObject() == nullptr);
    CHECK_EQ(ScreenWindow()->NumberOfChildren(), 0);
}

TEST_CASE("gui destroy: the root's owner letting go destroys it at once, alive, even while it is pinned")
{
    GuiScreen screen;
    const auto log = std::make_shared<DestroyLog>();
    auto root = MakeRoot<MCDestroyProbe<>>(0, 0, 100, 100, log, "root");
    auto child = MakeChild<MCDestroyProbe<>>(*root, 10, 10, 20, 20, log, "child");
    const auto rootHandle = HandleOf(root);
    const auto childHandle = HandleOf(child);

    {
        const auto pin = Pin(rootHandle);
        root.reset();
        MCTest::Scope destroys(Names(*log));
        CHECK(*log == DestroyLog({Destroyed("root", false, 1), Destroyed("child", false, 0)}));
        CHECK(!Alive(rootHandle));
        CHECK(!Alive(childHandle));
        // The pin keeps the memory: the object can still be read, destroyed.
        REQUIRE(pin != nullptr);
        CHECK_EQ(pin->Width(), 100);
        CHECK_EQ(pin->NumberOfChildren(), 0);
    }

    CHECK_EQ(log->size(), 2u);
}

TEST_CASE("gui destroy: the GUI system's teardown destroys every window still on the screen")
{
    GuiScreen screen;
    const auto log = std::make_shared<DestroyLog>();
    auto first = MakeChild<MCDestroyProbe<>>(*ScreenWindow(), 0, 0, 100, 100, log, "first");
    auto inner = MakeChild<MCDestroyProbe<>>(*first, 10, 10, 20, 20, log, "inner");
    auto second = MakeChild<MCDestroyProbe<>>(*ScreenWindow(), 200, 0, 100, 100, log, "second");
    const auto firstHandle = HandleOf(first);
    const auto innerHandle = HandleOf(inner);
    const auto secondHandle = HandleOf(second);

    screen.TearDown();
    MCTest::Scope destroys(Names(*log));
    CHECK(*log ==
          DestroyLog({Destroyed("first", false, 1), Destroyed("inner", false, 0), Destroyed("second", false, 0)}));
    CHECK(!Alive(firstHandle));
    CHECK(!Alive(innerHandle));
    CHECK(!Alive(secondHandle));
}

TEST_CASE("gui destroy: a hidden window isn't drawn or found, its timers fire, and shown again it is as it was")
{
    GuiScreen screen;
    auto window = MakeChild<MCTimerCounter>(*ScreenWindow(), 100, 100, 200, 200);
    auto inside = MakeChild<MCDisplayCounter>(*window, 10, 10, 50, 50);
    REQUIRE_EQ(screen.Gui->AddTimer(window, 1, 100, 0, 0, false), 0);
    window->MoveTo(120, 110);

    // Hidden, it returns before drawing itself or its children.
    window->ShowGuiWindow(false);
    window->Display();
    CHECK_EQ(inside->Displays, 0);
    CHECK(ScreenWindow()->FindObject(135, 125) == ScreenWindow());
    screen.Frame(101);
    CHECK_EQ(window->Timers, 1);

    window->ShowGuiWindow(true);
    CHECK(window->IsShowing());
    CHECK(ScreenWindow()->FindObject(135, 125) == Pin(HandleOf(inside)));
    CHECK_EQ(window->X(), 120);
    CHECK_EQ(window->Y(), 110);
    CHECK_EQ(window->NumberOfChildren(), 1);
    screen.Frame(101);
    CHECK_EQ(window->Timers, 2);
}

// What logistics keeps across screen changes. One ticker and one block per unit move from screen to screen today; once
// the screens make their own views of the shared data (a ticker line each, a block per pane entry), the player must see
// the same: these hold before the change and after it.

namespace
{
    /// <summary>Runs the logistics screens for two seconds: a screen change settles.</summary>
    void Settle()
    {
        for (int32_t frame = 0; frame < 30; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }

    /// <summary>A new campaign's purchase screen, with the points to buy anything.</summary>
    bool StartPurchaseScreen()
    {
        if (!MCTestGame::StartLogistics())
        {
            return false;
        }

        Settle();
        NewCampaign();
        Settle();
        GlobalLog()->SetUpPurchaseScreen(true);
        Settle();
        return GlobalLog()->CurrentScreen() == GlobalLog()->PurchaseScreen;
    }

    /// <summary>To the repair screen and back to the purchase screen.</summary>
    void ToRepairAndBack()
    {
        GlobalLog()->SetUpRepairScreen(true);
        Settle();
        REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->RepairScreen);
        GlobalLog()->SetUpPurchaseScreen(true);
        Settle();
        REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->PurchaseScreen);
    }

    /// <summary>The variant picked for <paramref name="mech"/> in the store.</summary>
    int32_t PickedVariant(const MCPurMech& mech)
    {
        return mech.CurVariant;
    }

    /// <summary>The store's components by master id, in the order the store lays them out.</summary>
    std::vector<uint8_t> StoreOrder()
    {
        std::vector<uint8_t> order;

        for (const MCGuiHandle<MCListRow>& handle : MCLogInvScreen::StoreList()->Rows())
        {
            const std::shared_ptr<MCCompPurchaseBlock> row =
                std::dynamic_pointer_cast<MCCompPurchaseBlock>(handle.Lock());
            CHECK(row != nullptr);

            if (row != nullptr)
            {
                order.push_back(row->Item()->MasterID);
            }
        }

        return order;
    }
}

TEST_CASE_ISOLATED("game: the ticker's text and scroll position stay the same from one logistics screen to the next")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(StartPurchaseScreen());
    MCTicker& ticker = *GlobalLog()->Ticker;
    ticker.SetString(std::string(300, 'W'));

    // Stepped as its step timer does. (Its own timer is 4, which never scrolls it: OB-072.)
    for (int32_t step = 0; step < 5; step++)
    {
        ticker.Advance(true);
    }

    const std::string text = ticker.Text;
    const int32_t position = ticker.ScrollPos;
    REQUIRE(position > 0);

    // The line showing is the current screen's own, and the only one.
    auto shownOn = []
    {
        int32_t shown = 0;
        bool onCurrent = false;

        for (const MCGuiHandle<MCTickerLine>& handle : GlobalLog()->TickerLines)
        {
            if (const std::shared_ptr<MCTickerLine> line = handle.Lock(); line != nullptr && line->IsShowing())
            {
                shown++;
                onCurrent = line->ParentObject() == GlobalLog()->CurrentScreen();
            }
        }

        return shown == 1 && onCurrent;
    };

    CHECK(shownOn());
    GlobalLog()->SetUpRepairScreen(false);
    CHECK(shownOn());
    CHECK_EQ(ticker.Text, text);
    CHECK_EQ(ticker.ScrollPos, position);
    GlobalLog()->SetUpPurchaseScreen(false);
    CHECK(shownOn());
    CHECK_EQ(ticker.Text, text);
    CHECK_EQ(ticker.ScrollPos, position);
}

TEST_CASE_ISOLATED("game: a variant picked in the store holds until the store is filled again, which shows the first "
                   "on sale")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(StartPurchaseScreen());
    MCPurMech& mech = *GlobalLog()->PurMechList->Mechs.front();
    REQUIRE_EQ(mech.Variants.size(), 3u);
    REQUIRE_EQ(PickedVariant(mech), 0);

    // The second variant button of its row.
    const std::shared_ptr<MCMechPurchaseBlock> block = MCLogInvScreen::StoreList()->RowOf<MCMechPurchaseBlock>(mech);
    REQUIRE(block != nullptr);
    Click(block->GlobalX() + 0xb5, block->GlobalY() + 0xd);
    Settle();
    CHECK_EQ(PickedVariant(mech), 1);

    // Coming back fills the store again, each mech showing its first variant on sale.
    ToRepairAndBack();
    CHECK_EQ(PickedVariant(mech), 0);
    mech.Variants[0]->NumAvailable = 0;
    mech.Variants[1]->NumAvailable = 1;
    ToRepairAndBack();
    CHECK_EQ(PickedVariant(mech), 1);
}

TEST_CASE_ISOLATED("game: the store lists its components in the same order after leaving it and coming back")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(StartPurchaseScreen());
    // The components tab.
    Click(200, 280);
    Settle();
    const std::vector<uint8_t> order = StoreOrder();
    REQUIRE(order.size() > 1);

    ToRepairAndBack();
    Click(200, 280);
    Settle();
    CHECK(StoreOrder() == order);
}

TEST_CASE("test framework: an expected assert is counted, not a failure")
{
    MCTest::ExpectAssert expected("expected by the test", 2);
    Assert(false, 0, " expected by the test ");
    Assert(true, 0, " expected by the test ");
    Assert(false, 0, " EXPECTED BY THE TEST ");
    CHECK_EQ(expected.Seen(), 2);
}

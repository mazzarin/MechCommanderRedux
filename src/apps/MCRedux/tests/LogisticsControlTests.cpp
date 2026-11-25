#include "stdafx.h"
#include "MCTest.h"
#include "GuiTree.h"
#include "RemadeScreens.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fakes/MCManualClock.h"
#include "fakes/MCMemoryFileSource.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCGuiTimerManager.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCLogisticsTabs.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCSmuti.h"
#include "logistics/MCStore.h"
#include "main/MCGamePaths.h"
#include "main/MCGameStrings.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCPilotPurchaseBlock.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "logistics/MCCompPurchaseBlock.h"
#include "logistics/MCUnitLimits.h"
#include "logistics/MCPurProfile.h"
#include "main/MCGameContext.h"
#include "main/MCLogMech.h"
#include "main/MCLogistics.h"
#include "object/MCMasterComponent.h"
#include "ui/MCRmlDialog.h"

// The logistics screens' controls (P3-log-1): the ports and the base object, the text formatter, the ticker, text
// fields, scrolling text, the game list, the slider, the save list's sort, ini-built screens, the purchase dialog and
// the message dialogs. Most run on a GUI system with a 640x480 screen window and letterless fonts (they measure 0 and
// draw nothing); the ones that need art or real letters read them from the retail install.

namespace
{
    /// <summary>
    /// A test context with a manual clock and a GUI system whose screen window is 640x480 (no display or art), and
    /// every font a letterless one (0 high, 0 wide).
    /// </summary>
    struct LogisticsScreen
    {
        LogisticsScreen() : Clock(Scope.Context().SetClock(std::make_unique<MCManualClock>()))
        {
            Scope.Context().SetGuiSystem(std::make_unique<MCGuiSystem>());
            Gui = Scope.Context().GuiSystem();
            Gui->ScreenWidth = 640;
            Gui->ScreenHeight = 480;
            Gui->TimerManager = std::make_unique<MCGuiTimerManager>();
            Gui->MakeScreen(640, 480);
            SetFonts(NoLetters);
        }

        ~LogisticsScreen()
        {
            SetFonts(nullptr);
            ClearLogArt();

            if (Retail)
            {
                FontPath = SavedFontPath;
                ArtPath = SavedArtPath;
            }
        }

        /// <summary>Points the font globals the controls use and the font table at <paramref name="font"/>.</summary>
        static void SetFonts(const std::shared_ptr<MCGuiFont>& font)
        {
            for (std::shared_ptr<MCGuiFont>& global :
                 {std::ref(GreyFont), std::ref(WhiteFont), std::ref(BlackFont), std::ref(GreenFont),
                  std::ref(LgGreyFont), std::ref(LgWhiteFont), std::ref(LgBlackFont), std::ref(MedWhiteFont),
                  std::ref(MedBlackFont), std::ref(MedRedFont), std::ref(MedBlueFont)})
            {
                global = font;
            }

            for (auto& row : Fonts)
            {
                row.fill(font);
            }
        }

        /// <summary>
        /// Points the fonts at the install's white font and the art at its folders; false without an install (the
        /// test then passes as skipped).
        /// </summary>
        bool LoadRetail()
        {
            if (!MCTestGame::Available())
            {
                return false;
            }

            MCTestGame::OpenFastFiles();
            Retail = true;
            SavedFontPath = FontPath;
            SavedArtPath = ArtPath;
            FontPath = "data\\fonts\\";
            ArtPath = "data\\art\\";
            White = std::move(*MCGuiFont::Create("white.fnt"));
            SetFonts(White);
            return true;
        }

        MCTestContextScope Scope;
        MCManualClock& Clock;
        std::shared_ptr<MCGuiSystem> Gui;
        std::shared_ptr<MCGuiFont> NoLetters = std::make_shared<MCGuiFont>();
        std::shared_ptr<MCGuiFont> White;
        bool Retail = false;
        std::string SavedFontPath;
        std::string SavedArtPath;
    };

    /// <summary>The columns of <paramref name="port"/>'s pixels that aren't 0, as (first, last); (-1, -1) for none.</summary>
    std::pair<int32_t, int32_t> InkedColumns(const MCGuiPort& port)
    {
        int32_t first = -1;
        int32_t last = -1;

        for (int32_t y = 0; y < port.Height(); y++)
        {
            for (int32_t x = 0; x < port.Width(); x++)
            {
                if (port.Pixels().data()[y * port.Width() + x] != 0)
                {
                    first = first < 0 ? x : std::min(first, x);
                    last = std::max(last, x);
                }
            }
        }

        return {first, last};
    }
}

// Ports and the base object

TEST_CASE("logistics: a port makes a new zeroed bitmap on every Init")
{
    LogisticsScreen screen;
    MCLogPort port;
    REQUIRE_EQ(port.Init(4, 3), 0);
    REQUIRE(port.Pixels().data() != nullptr);
    std::fill_n(port.Pixels().data(), 12, uint8_t{7});

    // Unlike the GUI's ports, the same size makes a new bitmap too.
    REQUIRE_EQ(port.Init(4, 3), 0);
    CHECK(std::all_of(port.Pixels().data(), port.Pixels().data() + 12, [](uint8_t pixel) { return pixel == 0; }));
    CHECK_EQ(port.Frame().X1, 3);
    CHECK_EQ(port.Frame().Y1, 2);
}

TEST_CASE("logistics: an object made without a port has none; with one, a picture")
{
    LogisticsScreen screen;
    MCGuiRef<MCLogObject> bare = MCGuiTree::NewRoot<MCLogObject>();
    bare->InitWithoutPort(1, 2, 30, 40);
    CHECK(bare->Lport() == nullptr);
    CHECK_EQ(bare->Width(), 30);
    CHECK_EQ(bare->Height(), 40);

    MCGuiRef<MCLogObject> plain = MCGuiTree::NewRoot<MCLogObject>();
    REQUIRE_EQ(plain->Init(0, 0, 8, 6), 0);
    REQUIRE(plain->Lport() != nullptr);
    CHECK(!plain->Lport()->IsView());
    CHECK_EQ(plain->Lport()->Width(), 8);

    // Destroyed, it frees its port (the pin keeps its memory to look).
    const std::shared_ptr<MCLogObject> destroyed = plain.Pin();
    plain.reset();
    CHECK(destroyed->Lport() == nullptr);
}

// The text formatter

TEST_CASE("logistics: the text formatter measures %n lines a font height and a row apart, and needs no port to")
{
    LogisticsScreen screen;
    MCSmuti formatter;
    // Letterless fonts are 0 high: each %n moves one row.
    CHECK_EQ(formatter.Process("", std::nullopt, 100, 0), 0);
    CHECK_EQ(formatter.Process("a%nb%nc", std::nullopt, 100, 5), 7);
    CHECK_EQ(formatter.Process("100%% sure", std::nullopt, 100, 0), 0);
    // The text ends at a NUL.
    CHECK_EQ(formatter.Process(std::string_view("a\0%n%n", 6), std::nullopt, 100, 0), 0);
}

TEST_CASE("game: the text formatter wraps a line at the last space that fits and centres a %c line")
{
    LogisticsScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    MCSmuti formatter;
    const int32_t height = GreenFont->Height();
    const int32_t wide = GreenFont->Width(std::string_view("aaaa bbbb")) + 10;
    CHECK_EQ(formatter.Process("aaaa bbbb", std::nullopt, wide, 0), height);
    // Too narrow for both words: "bbbb" goes on a second line.
    const int32_t narrow = GreenFont->Width(std::string_view("aaaa bb"));
    CHECK_EQ(formatter.Process("aaaa bbbb", std::nullopt, narrow, 0), height + height + 1);

    // A centred line is written in the middle of the port.
    MCLogPort port;
    REQUIRE_EQ(port.Init(200, height + 2), 0);
    formatter.Process("%cHI", port, 0, 0);
    const auto [first, last] = InkedColumns(port);
    const int32_t textWidth = GreenFont->Width(std::string_view("HI"));
    REQUIRE(first >= 0);
    CHECK(first >= (200 - textWidth) / 2);
    CHECK(last < (200 - textWidth) / 2 + textWidth);
}

TEST_CASE("game: a colour code at the end of a text writes the line before it again after itself (OB-163)")
{
    LogisticsScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    MCSmuti formatter;
    const int32_t textWidth = GreenFont->Width(std::string_view("AB"));
    MCLogPort once;
    REQUIRE_EQ(once.Init(200, GreenFont->Height() + 2), 0);
    formatter.Process("AB", once, 0, 0);
    MCLogPort twice;
    REQUIRE_EQ(twice.Init(200, GreenFont->Height() + 2), 0);
    formatter.Process("AB%fc5", twice, 0, 0);
    // The second write starts where the first ended.
    CHECK(InkedColumns(once).second < textWidth);
    CHECK(InkedColumns(twice).second >= textWidth);
}

// The purchase dialog

namespace
{
    /// <summary>A new campaign's first briefing on the remade screens, on PURCHASING with the store's <paramref name="kind"/>.</summary>
    MCLogisticsTabs& ShowStore(MCLogisticsTabs::MCKind kind)
    {
        REQUIRE(MCTestGame::StartLogistics());
        NewCampaign();
        MCLogisticsTabs& tabs = MCRemadeTest::CampaignScreen(MCCampaignScreen::MCTab::Purchasing).Logistics();
        tabs.ChooseKind(kind);
        MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
        return tabs;
    }

    /// <summary>The refit question's words before and after its parts (1998's refit dialog's), and their buffer.</summary>
    constexpr uint32_t RefitHeadString = 0x54;
    constexpr uint32_t RefitFootString = 0x62;
    constexpr int32_t StringBuffer = 0xfe;

    /// <summary>Key <paramref name="key"/> pressed <paramref name="times"/> times on the deal window, a frame after each.</summary>
    void DealKey(SDL_Scancode key, int32_t times = 1)
    {
        for (int32_t press = 0; press < times; press++)
        {
            MCScreenInput::RealKey(key);
            MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
        }
    }

    /// <summary><paramref name="place"/>'s card <paramref name="index"/> offered (its right button), the deal window open.</summary>
    void OpenDeal(MCLogisticsTabs& tabs, MCLogisticsTabs::MCPlace place, size_t index)
    {
        tabs.Send(place, index);
        MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
        REQUIRE(tabs.DealOpen());
        CHECK_EQ(tabs.DealCountShown(), 1);
    }
}

/// <summary>
/// PURCHASING's deal window counts from one as 1998's purchase dialog: Right steps up while the resource points pay for one
/// more, Left back down to one (1998's spinner went on to 0, which bought nothing), a refusal at each end; ESCAPE cancels
/// the deal, RETURN buys the count shown. With one in the store it counts no further (1998's dialog had no spinner); a
/// part the store has no limit of counts to 199 (1998's spinner's most); a part's sale counts up to the spares owned, a
/// 'Mech's sale is one (1998's locked spinner).
/// </summary>
TEST_CASE_ISOLATED("game: the deal window counts up to what the resource points pay for, and down to one; a part with "
                   "no limit up to 199")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCLogisticsTabs& tabs = ShowStore(MCLogisticsTabs::MCKind::Components);
    const std::vector<std::shared_ptr<MCLogInventoryItem>> store = MCStore::ListedParts();
    REQUIRE(!store.empty());
    REQUIRE_EQ(tabs.StoreGoods().size(), store.size());
    constexpr int32_t InStore = 5;
    constexpr int32_t PaidFor = 3;
    const std::shared_ptr<MCLogInventoryItem> part = store.front();
    part->Count = InStore;
    const int32_t each = MasterComponentList[part->MasterID].ResourcePoints;
    REQUIRE(each > 0);
    // Points for three and a half.
    const int32_t points = each * PaidFor + each / 2;
    ResourcePoints = points;

    OpenDeal(tabs, MCLogisticsTabs::MCPlace::Store, 0);
    DealKey(SDL_SCANCODE_RIGHT, PaidFor + 1);
    CHECK_EQ(tabs.DealCountShown(), PaidFor);
    DealKey(SDL_SCANCODE_LEFT, PaidFor + 1);
    CHECK_EQ(tabs.DealCountShown(), 1);
    DealKey(SDL_SCANCODE_ESCAPE);
    CHECK(!tabs.DealOpen());
    CHECK_EQ(part->Count, InStore);
    CHECK_EQ(ResourcePoints, points);

    constexpr int32_t Bought = 2;
    OpenDeal(tabs, MCLogisticsTabs::MCPlace::Store, 0);
    DealKey(SDL_SCANCODE_RIGHT, Bought - 1);
    DealKey(SDL_SCANCODE_RETURN);
    CHECK(!tabs.DealOpen());
    CHECK_EQ(part->Count, InStore - Bought);
    CHECK_EQ(ResourcePoints, points - each * Bought);

    // One in the store: no more to count.
    {
        MCTest::Scope scope("one in the store");
        part->Count = 1;
        ResourcePoints = points;
        MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
        OpenDeal(tabs, MCLogisticsTabs::MCPlace::Store, 0);
        DealKey(SDL_SCANCODE_RIGHT);
        CHECK_EQ(tabs.DealCountShown(), 1);
        DealKey(SDL_SCANCODE_ESCAPE);
    }

    // A part the store has no limit of (its stock below zero), with the points for more: up to 199, no further.
    {
        MCTest::Scope scope("no limit");
        constexpr int32_t NoLimit = -1;
        constexpr int32_t MostCounted = 199;
        part->Count = NoLimit;
        ResourcePoints = each * (MostCounted + PaidFor);
        MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
        OpenDeal(tabs, MCLogisticsTabs::MCPlace::Store, 0);
        DealKey(SDL_SCANCODE_RIGHT, MostCounted + 1);
        CHECK_EQ(tabs.DealCountShown(), MostCounted);
        DealKey(SDL_SCANCODE_ESCAPE);
        CHECK_EQ(part->Count, NoLimit);
        ResourcePoints = points;
    }

    // A part's sale: up to the spares owned (three), from the INVENTORY's card dragged onto the store.
    {
        MCTest::Scope scope("part sale");
        constexpr int32_t Spares = 3;
        MCInventoryList& components = *GlobalLog()->ComponentInventory;
        const std::shared_ptr<MCLogInventoryItem> spare =
            components.GetItemInfo(components.GetIndexFromMasterID(part->MasterID));
        REQUIRE(spare != nullptr);
        spare->Count = Spares;
        MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
        const std::string& name = MasterComponentList[part->MasterID].Name;
        const auto card = std::ranges::find(tabs.StockGoods(), name, &MCLogisticsTabs::MCGood::Who);
        REQUIRE(card != tabs.StockGoods().end());
        tabs.Drop(MCLogisticsTabs::MCPlace::Stock, static_cast<size_t>(card - tabs.StockGoods().begin()),
                  MCLogisticsTabs::MCPlace::Store);
        MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
        REQUIRE(tabs.DealOpen());
        CHECK_EQ(tabs.DealTitle(), std::string("Sell"));
        DealKey(SDL_SCANCODE_RIGHT, Spares);
        CHECK_EQ(tabs.DealCountShown(), Spares);
        DealKey(SDL_SCANCODE_ESCAPE);
        CHECK_EQ(spare->Count, Spares);
    }

    // A 'Mech's sale: one, whatever the arrows.
    {
        MCTest::Scope scope("mech sale");
        tabs.ChooseKind(MCLogisticsTabs::MCKind::Mechs);
        const std::shared_ptr<MCPurMech> purMech = GlobalLog()->PurMechList->Mechs.front();
        MCPurMechData& data = *purMech->Variants[static_cast<size_t>(purMech->CurVariant)];
        data.NumAvailable = 1;
        ResourcePoints = points + data.Cost;
        MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
        OpenDeal(tabs, MCLogisticsTabs::MCPlace::Store, 0);
        DealKey(SDL_SCANCODE_RETURN);
        const std::shared_ptr<MCLogMech> bought = GlobalLog()->MechList->Mechs.back();
        size_t spareCard = 0;

        while (spareCard < tabs.SpareCards().size() &&
               tabs.UnitAt(MCLogisticsTabs::MCPlace::Spare, spareCard) != bought)
        {
            spareCard++;
        }

        REQUIRE(spareCard < tabs.SpareCards().size());
        tabs.Drop(MCLogisticsTabs::MCPlace::Spare, spareCard, MCLogisticsTabs::MCPlace::Store);
        MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
        REQUIRE(tabs.DealOpen());
        CHECK_EQ(tabs.DealTitle(), std::string("Sell"));
        DealKey(SDL_SCANCODE_RIGHT);
        CHECK_EQ(tabs.DealCountShown(), 1);
        DealKey(SDL_SCANCODE_ESCAPE);
    }
}

// The message dialogs

/// <summary>
/// A message dialog with a timeout (a launcher's leave, the room's countdown) closes after its time with its result:
/// said by the remade dialog (the 1998 box is gone), its time counted by the GUI's timers in the game's clock.
/// </summary>
TEST_CASE_ISOLATED("game: the message dialog's timeout closes it after its time, with its result")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    constexpr int32_t Timeout = 3000;
    constexpr int32_t TimeoutResult = 7;
    constexpr float SecondsAMillisecond = 0.001f;
    constexpr float TimeoutSeconds = static_cast<float>(Timeout) * SecondsAMillisecond;
    const auto dialog = std::make_shared<MCMenuDialog>();
    dialog->SetText("Game saved.");
    dialog->SetTwoButton(false);
    std::vector<int32_t> results;
    dialog->Callback = [&](int32_t result) { results.push_back(result); };
    dialog->Timeout = Timeout;
    dialog->TimeoutResult = TimeoutResult;
    dialog->Activate();
    CHECK(dialog->IsShowing());
    REQUIRE(MCRmlDialogs::Shown());
    CHECK_EQ(MCRmlDialogs::Dialog()->Said().Text, std::string("Game saved."));

    // Its time is up only past its milliseconds, as the GUI's timers count (each frame's whole milliseconds).
    MCTestGame::RunFrame(TimeoutSeconds);
    CHECK(dialog->IsShowing());
    CHECK(results.empty());
    MCTestGame::RunFrame(SecondsAMillisecond);
    CHECK(!dialog->IsShowing());
    REQUIRE_EQ(results.size(), 1u);
    CHECK_EQ(results[0], TimeoutResult);
    // Cleared with the callbacks: shown again, it waits for an answer.
    CHECK_EQ(dialog->Timeout, 0);
    CHECK_EQ(dialog->TimeoutResult, 0);
    dialog->Activate();
    MCTestGame::RunFrame(2 * TimeoutSeconds);
    CHECK(dialog->IsShowing());
    dialog->Deactivate(0);

    // Kept callbacks (a launcher's leave, KeepCallbacks) keep its timeout's timer: it answers again each time.
    dialog->Callback = [&](int32_t result) { results.push_back(result); };
    dialog->Timeout = Timeout;
    dialog->TimeoutResult = TimeoutResult;
    dialog->Activate();
    dialog->KeepCallbacks = true;
    MCTestGame::RunFrame(TimeoutSeconds);
    MCTestGame::RunFrame(SecondsAMillisecond);
    REQUIRE_EQ(results.size(), 2u);
    CHECK(dialog->Callback);
    dialog->Deactivate(0);
}

/// <summary>
/// The 'MECH BAY's REFIT ALL, with damaged parts it has no spare for, asks whether to strip them: 1998's refit dialog's
/// words before and after, each part on a line of its own between them (three, then one).
/// </summary>
TEST_CASE_ISOLATED("game: REFIT ALL's question has a line per part it has no spare for between its words")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    NewCampaign();
    MCLogisticsTabs& tabs = MCRemadeTest::CampaignScreen(MCCampaignScreen::MCTab::MechBay).Logistics();
    const std::shared_ptr<MCLogMech> mech = GlobalLog()->ForceMechList->Mechs.front();
    tabs.Pick(MCLogisticsTabs::MCPlace::Force, 0);
    MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    REQUIRE(tabs.LabUnit() == mech);

    // Three of its weapons and equipment damaged, with none of their kind in the INVENTORY.
    constexpr size_t Damaged = 3;
    MCInventoryList& components = *GlobalLog()->ComponentInventory;
    std::vector<std::shared_ptr<MCLogInventoryItem>> damaged;

    for (const std::shared_ptr<MCLogInventoryItem>& item : mech->Inventory->Items)
    {
        const MCComponentForm form = ComponentForm(item->MasterID);

        if (damaged.size() < Damaged && (IsWeapon(form) || IsEquipment(form)) && !item->Stats.empty())
        {
            item->Stats.front()->Hits = 1;
            damaged.push_back(item);

            if (const std::shared_ptr<MCLogInventoryItem> spare =
                    components.GetItemInfo(components.GetIndexFromMasterID(item->MasterID));
                spare != nullptr)
            {
                spare->Count = 0;
            }
        }
    }

    REQUIRE_EQ(damaged.size(), Damaged);
    const auto question = [](const std::vector<std::shared_ptr<MCLogInventoryItem>>& parts)
    {
        std::string text = LoadGameString(RefitHeadString, StringBuffer);

        for (const std::shared_ptr<MCLogInventoryItem>& part : parts)
        {
            text += "\n" + part->Name;
        }

        return text + "\n" + LoadGameString(RefitFootString, StringBuffer);
    };

    tabs.RefitAll();
    MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    REQUIRE(MCRmlDialogs::Shown());
    CHECK_EQ(MCRmlDialogs::Dialog()->Said().Text, question(damaged));
    // Cancel: the parts stay as they are, still damaged.
    MCRemadeTest::AnswerDialog(false);
    CHECK_EQ(damaged.back()->Stats.front()->Hits, 1);

    // One left damaged: one line.
    for (size_t part = 1; part < Damaged; part++)
    {
        damaged[part]->Stats.front()->Hits = 0;
    }

    tabs.RefitAll();
    MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    REQUIRE(MCRmlDialogs::Shown());
    CHECK_EQ(MCRmlDialogs::Dialog()->Said().Text, question({damaged.front()}));
    MCRemadeTest::AnswerDialog(false);
    CHECK_EQ(MCRmlDialogs::OldBoxes(), 0u);
}

// The resource figure

TEST_CASE("logistics: the resource figure shows the points in state 0 and nothing in the heap's old states")
{
    const int32_t savedPoints = ResourcePoints;
    const int32_t savedState = ResourceDisplayState;
    ResourcePoints = 12345;
    ResourceDisplayState = 0;
    CHECK_EQ(ResourceFigureText(), std::string("12345"));
    ResourceDisplayState = 1;
    CHECK(ResourceFigureText().empty());
    ResourceDisplayState = 2;
    CHECK(ResourceFigureText().empty());
    ResourcePoints = savedPoints;
    ResourceDisplayState = savedState;
}

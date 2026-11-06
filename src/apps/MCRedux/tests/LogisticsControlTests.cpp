#include "stdafx.h"
#include "MCTest.h"
#include "GuiTree.h"
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
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGameList.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogScrollTextObject.h"
#include "logistics/MCLogSlider.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCPurchaseDlg.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSmuti.h"
#include "logistics/MCTicker.h"
#include "logistics/MCTickerLine.h"
#include "main/MCGamePaths.h"
#include "logistics/MCConnectMenu.h"
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

    /// <summary>An object that keeps the focus notices (<see cref="MCLogNotice"/>) its children send it.</summary>
    class MCNoticeRecorder : public MCLogObject
    {
    public:
        void HandleEvent(MCGuiEvent& event) override
        {
            if (event.Type == MCGuiEventType::Focus)
            {
                Notices.push_back(event.Data);
            }
        }

        std::vector<int32_t> Notices;
    };

    /// <summary>A logistics object that counts the ones destroyed (deleted).</summary>
    class MCCountedObject : public MCLogObject
    {
    public:
        ~MCCountedObject() override { Deleted++; }

        static inline int32_t Deleted = 0;
    };

    /// <summary>Gives <paramref name="object"/> an event of <paramref name="type"/>.</summary>
    void Send(MCGuiObject& object, int32_t type, int32_t x = 0, int32_t y = 0, uint8_t key = 0, int32_t data = 0)
    {
        MCGuiEvent event;
        event.Type = type;
        event.X = x;
        event.Y = y;
        event.Key = key;
        event.Data = data;
        object.HandleEvent(event);
    }

    /// <summary>Types <paramref name="text"/> into <paramref name="field"/>, a character event each.</summary>
    void Type(MCGuiObject& field, std::string_view text)
    {
        for (const char c : text)
        {
            Send(field, MCGuiEventType::Character, 0, 0, static_cast<uint8_t>(c));
        }
    }

    /// <summary>Sets <paramref name="field"/> up at (10, 10), taking <paramref name="size"/> - 1 characters of <paramref name="type"/>.</summary>
    void SetUpField(MCLogTextObject& field, int32_t size, MCLogInputType type)
    {
        field.MCLogObject::Init(10, 10, 100, 12);
        field.Font = WhiteFont;
        field.InitBuffer(size, type);
    }

    /// <summary>A text field at (10, 10) taking <paramref name="size"/> - 1 characters of <paramref name="type"/>.</summary>
    MCGuiRef<MCLogTextObject> MakeField(int32_t size, MCLogInputType type)
    {
        MCGuiRef<MCLogTextObject> field = MCGuiTree::NewRoot<MCLogTextObject>();
        SetUpField(*field, size, type);
        return field;
    }

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

TEST_CASE("logistics: an object made without a port has none; with one, a picture, or a view when it draws itself")
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

    // A button draws itself each frame: its port is a view of its size.
    MCGuiRef<MCLogButton> button = MCGuiTree::NewRoot<MCLogButton>();
    REQUIRE_EQ(button->Init(0, 0, 20, 10, {}), 0);
    REQUIRE(button->Lport() != nullptr);
    CHECK(button->Lport()->IsView());

    // Destroyed, it frees its port (the pin keeps its memory to look).
    const std::shared_ptr<MCLogObject> destroyed = plain.Pin();
    plain.reset();
    CHECK(destroyed->Lport() == nullptr);
}

TEST_CASE("logistics: destroying an object destroys and deletes the children made on it (OB-071 gone)")
{
    LogisticsScreen screen;
    MCCountedObject::Deleted = 0;
    MCGuiRef<MCLogObject> parent = MCGuiTree::NewRoot<MCLogObject>();
    parent->Init(0, 0, 50, 50);
    std::vector<MCGuiHandle<MCCountedObject>> children;

    for (int32_t i = 0; i < 3; i++)
    {
        children.push_back(parent->CreateChild<MCCountedObject>([&](MCCountedObject& made)
                                                                { made.InitWithoutPort(i * 10, 0, 5, 5); }));
    }

    // A child closed leaves its parent and is deleted: its parent was its only owner.
    children[0]->Close();
    CHECK(!children[0]);
    CHECK_EQ(MCCountedObject::Deleted, 1);
    CHECK_EQ(parent->NumberOfChildren(), 2);

    // The parent's destroy takes the rest with it.
    parent.reset();
    CHECK_EQ(MCCountedObject::Deleted, 3);

    for (const MCGuiHandle<MCCountedObject>& child : children)
    {
        CHECK(!child);
    }
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

// The ticker

TEST_CASE("game: the ticker keeps a wide text whole and scrolls it two pixels a step on timer 7, never on its own "
          "timer (OB-072)")
{
    LogisticsScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    // The ticker, and a screen's line of it, which passes its events on.
    const auto ticker = std::make_shared<MCTicker>(0x40, MedWhiteFont);
    const MCGuiRef<MCTickerLine> line = MCGuiRef<MCTickerLine>::Make();
    line->Init(3, 3, 0x40, 1, ticker);
    const std::string text(300, 'W');
    ticker->SetString(text);
    CHECK_EQ(ticker->Text.size(), 300u);
    const int32_t textWidth = MedWhiteFont->Width(text);
    REQUIRE(textWidth > 0x40);
    // A wide text gets half a line of gap before it repeats.
    CHECK_EQ(ticker->TextWidth, 0x20 + textWidth);

    Send(*line, MCGuiEventType::Timer, 0, 0, 0, MCTicker::ScrollTimer);
    CHECK(!ticker->ScrollShown);
    CHECK_EQ(ticker->ScrollPos, 0);

    Send(*line, MCGuiEventType::Timer, 0, 0, 0, MCTicker::ScrollStepTimer);
    CHECK(ticker->ScrollShown);
    CHECK_EQ(ticker->ScrollPos, 2);

    // Past the end it comes round again.
    ticker->ScrollPos = ticker->TextWidth + 1;
    Send(*line, MCGuiEventType::Timer, 0, 0, 0, MCTicker::ScrollStepTimer);
    CHECK_EQ(ticker->ScrollPos, 1);

    // The same text again changes nothing; an empty one clears the line.
    ticker->SetString(text);
    CHECK_EQ(ticker->ScrollPos, 1);
    ticker->SetString({});
    CHECK(ticker->Text.empty());
    CHECK_EQ(ticker->TextWidth, 0);

    // A text that fits doesn't scroll.
    ticker->SetString("W");
    CHECK_EQ(ticker->TextWidth, MedWhiteFont->Width(std::string_view("W")));
    Send(*line, MCGuiEventType::Timer, 0, 0, 0, MCTicker::ScrollStepTimer);
    CHECK_EQ(ticker->ScrollPos, 0);
}

// Text fields

TEST_CASE("logistics: a text field takes the characters its type allows, one less than its buffer holds")
{
    LogisticsScreen screen;
    MCGuiRef<MCLogTextObject> digits = MakeField(5, MCLogInputType::Digits);
    Type(*digits, "1a2b34567");
    CHECK_EQ(digits->Text(), std::string_view("1234"));
    CHECK_EQ(digits->TextLength, 4);

    MCGuiRef<MCLogTextObject> port = MakeField(8, MCLogInputType::Port);
    Type(*port, "1234567");
    CHECK_EQ(port->Text(), std::string_view("23456"));

    MCGuiRef<MCLogTextObject> text = MakeField(8, MCLogInputType::Text);
    Type(*text, std::string_view("a\tb~\x7f", 5));
    CHECK_EQ(text->Text(), std::string_view("ab~"));

    MCGuiRef<MCLogTextObject> none = MakeField(8, MCLogInputType::None);
    Type(*none, "abc");
    CHECK(none->Text().empty());
}

TEST_CASE("logistics: backspace on a text field's untouched text clears it all, after an edit it takes one character")
{
    LogisticsScreen screen;
    MCGuiRef<MCLogTextObject> field = MakeField(16, MCLogInputType::Any);
    CHECK_EQ(field->SetStringBuffer("abc"), 0);
    CHECK_EQ(field->CursorPos, 3);
    Send(*field, MCGuiEventType::Character, 0, 0, 8);
    CHECK(field->Text().empty());
    CHECK_EQ(field->TextLength, 0);
    CHECK_EQ(field->CursorPos, 0);

    field->SetStringBuffer("abc");
    Type(*field, "d");
    Send(*field, MCGuiEventType::Character, 0, 0, 8);
    CHECK_EQ(field->Text(), std::string_view("abc"));
    CHECK_EQ(field->CursorPos, 3);

    // A text too long is cut to the field, and its length is left as it was.
    CHECK_EQ(field->SetStringBuffer(std::string(40, 'x')), -1);
    CHECK_EQ(field->Text(), std::string_view(std::string(15, 'x')));
    CHECK_EQ(field->TextLength, 3);
    CHECK_EQ(field->CursorPos, 15);
}

TEST_CASE("logistics: typing after a shorter text was set brings back the end of the longer one (OB-161)")
{
    LogisticsScreen screen;
    MCGuiRef<MCLogTextObject> field = MakeField(16, MCLogInputType::Any);
    field->SetStringBuffer("abcdef");
    field->SetStringBuffer("xy");
    CHECK_EQ(field->Text(), std::string_view("xy"));
    Type(*field, "z");
    CHECK_EQ(field->Text(), std::string_view("xyzdef"));
}

TEST_CASE("logistics: Enter in a text field tells its parent, and the focus clears a save slot's empty name")
{
    LogisticsScreen screen;
    const std::string savedEmpty = EmptyFile;
    EmptyFile = "<empty>";
    MCGuiRef<MCNoticeRecorder> parent = MCGuiTree::NewRoot<MCNoticeRecorder>();
    parent->Init(0, 0, 200, 200);
    const MCGuiHandle<MCLogTextObject> field =
        parent->CreateChild<MCLogTextObject>([](MCLogTextObject& made) { SetUpField(made, 16, MCLogInputType::Any); });
    field->SetStringBuffer("<empty>");

    // Without ClearEmptyOnFocus the name stays.
    Send(*field, MCGuiEventType::Focus, 0, 0, 0, MCLogNotice::FocusGained);
    CHECK_EQ(field->Text(), std::string_view("<empty>"));
    field->ClearEmptyOnFocus = true;
    Send(*field, MCGuiEventType::Focus, 0, 0, 0, MCLogNotice::FocusGained);
    CHECK(field->Text().empty());
    CHECK_EQ(field->CursorPos, 0);

    // The blink timer flips the cursor; losing the focus lights it.
    Send(*field, MCGuiEventType::Timer, 0, 0, 0, 0);
    CHECK(!field->CursorLit);
    Send(*field, MCGuiEventType::Focus, 0, 0, 0, MCLogNotice::FocusLost);
    CHECK(field->CursorLit);

    Send(*field, MCGuiEventType::Character, 0, 0, 0x0d);
    REQUIRE_EQ(parent->Notices.size(), 1u);
    CHECK_EQ(parent->Notices[0], MCLogNotice::EntryDone);
    EmptyFile = savedEmpty;
}

// Scrolling text

TEST_CASE("logistics: scrolling text keeps every line (no 4 KB limit); lines 0 and 1 are both the first")
{
    LogisticsScreen screen;
    MCGuiRef<MCLogScrollTextObject> text = MCGuiTree::NewRoot<MCLogScrollTextObject>();
    REQUIRE_EQ(text->Init(0, 0, 100, 60, {}), 0);
    CHECK(!text->Scrolling);

    for (int32_t i = 0; i < 500; i++)
    {
        text->Print(std::format("line {:03}", i), 0x1f);
    }

    CHECK_EQ(text->NumLines, 500);
    CHECK(text->Text.size() > 0x1000);
    CHECK(text->GetTextLine(0) == std::optional<std::string>("line 000"));
    CHECK(text->GetTextLine(1) == std::optional<std::string>("line 000"));
    CHECK(text->GetTextLine(2) == std::optional<std::string>("line 001"));
    CHECK(text->GetTextLine(500) == std::optional<std::string>("line 499"));
    CHECK(!text->GetTextLine(501).has_value());
    CHECK(!text->GetTextLine(-1).has_value());

    text->Clear();
    CHECK_EQ(text->NumLines, 0);
    CHECK(!text->GetTextLine(1).has_value());
    CHECK_EQ(text->HighlightLine[0], -1);
}

TEST_CASE("logistics: a scrolling text line in colour 0 ends the text, and a blank line is just its colour")
{
    LogisticsScreen screen;
    MCGuiRef<MCLogScrollTextObject> text = MCGuiTree::NewRoot<MCLogScrollTextObject>();
    REQUIRE_EQ(text->Init(0, 0, 100, 60, "first"), 0);
    CHECK(text->Scrolling);
    text->PrintBlank(0x1f);
    CHECK(text->GetTextLine(2) == std::optional<std::string>(""));
    text->Print("hidden", 0);
    text->Print("after", 0x1f);
    CHECK_EQ(text->NumLines, 4);
    CHECK(text->GetTextLine(1) == std::optional<std::string>("first"));
    CHECK(!text->GetTextLine(3).has_value());
    CHECK(!text->GetTextLine(4).has_value());
}

TEST_CASE("game: wrapped logistics text breaks at the last space that fits; a first word too wide takes the rest")
{
    LogisticsScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    MCGuiRef<MCLogScrollTextObject> text = MCGuiTree::NewRoot<MCLogScrollTextObject>();
    REQUIRE_EQ(text->Init(0, 0, 100, 60, {}), 0);
    const int32_t width = Fonts[0][0]->Width(std::string_view("aaa aaa")) + 6;
    text->PrintWrapped("aaa aaa aaa aaa", 0x1f, width);
    CHECK_EQ(text->NumLines, 2);
    CHECK(text->GetTextLine(1) == std::optional<std::string>("aaa aaa"));
    CHECK(text->GetTextLine(2) == std::optional<std::string>("aaa aaa"));

    text->Clear();
    text->PrintWrapped("wwwwwwwwwwwwwwwwwwww xx", 0x1f, width);
    CHECK_EQ(text->NumLines, 1);
    CHECK(text->GetTextLine(1) == std::optional<std::string>("wwwwwwwwwwwwwwwwwwww xx"));
}

// The game list

TEST_CASE("logistics: the game list is unlisted until refreshed, and a selection past it asks for a refresh")
{
    LogisticsScreen screen;
    MCGuiRef<MCGameList> games = MCGuiTree::NewRoot<MCGameList>();
    REQUIRE_EQ(games->Init(0, 0, 200, 100, {}), 0);
    CHECK_EQ(games->NumSessions(), -1);
    CHECK_EQ(games->TabColumn, 0x73);
    games->SelectedGuid.Data1 = 7;

    // Before the first refresh, clearing the selection leaves it.
    games->ClearSelection();
    CHECK(games->SelectedGuid.Data1 == 7);

    // No selection, nothing listed: -1 is past the (unlisted) end, so the list is refreshed.
    CHECK(!games->GetSelectedGame().has_value());
    CHECK_EQ(games->NumSessions(), 0);
    games->ClearSelection();
    CHECK(games->SelectedGuid.Data1 == 0);
    CHECK(!games->GetSelectedGame().has_value());
}

// The slider

TEST_CASE("game: a slider's value is the mouse's share of the thumb's travel, clamped, the minimum winning")
{
    LogisticsScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    MCGuiRef<MCLogSlider> slider = MCGuiTree::NewRoot<MCLogSlider>();
    REQUIRE_EQ(slider->Init(20, 0, 120, 10, {}), 0);
    slider->MinValue = 0;
    slider->MaxValue = 100;
    const int32_t travel = 120 - slider->ThumbPort->Width();
    CHECK_EQ(slider->ValueAt(20), 0);
    CHECK_EQ(slider->ValueAt(20 + travel), 100);
    CHECK_EQ(slider->ValueAt(20 + travel / 2), static_cast<int32_t>(static_cast<double>(travel / 2) / travel * 100));
    slider->SetCurrentValue(100);
    CHECK_EQ(slider->ThumbX(), travel);
    slider->SetCurrentValue(250);
    CHECK_EQ(slider->CurrentValue, 100);
    slider->SetCurrentValue(-5);
    CHECK_EQ(slider->CurrentValue, 0);

    slider->MinValue = 10;
    slider->MaxValue = 0;
    slider->SetCurrentValue(5);
    CHECK_EQ(slider->CurrentValue, 10);
}

// The save list

TEST_CASE("logistics: the save list sorts by name as the original's exchange sort, the selection following its file")
{
    using File = MCFileScrollPane::File;
    std::vector<File> files = {{"b", 1}, {"c", 2}, {"a", 3}};
    int32_t selected = 0;
    MCFileScrollPane::SortByName(files, selected);
    REQUIRE_EQ(files.size(), 3u);
    CHECK_EQ(files[0].Name, std::string("a"));
    CHECK_EQ(files[1].Name, std::string("b"));
    CHECK_EQ(files[2].Name, std::string("c"));
    CHECK_EQ(selected, 1);
    CHECK_EQ(files[1].Operation, 1);

    // Equal names: the exchange sort isn't stable; the selection goes to the first of the name.
    files = {{"b", 1}, {"b", 2}, {"a", 3}};
    selected = 0;
    MCFileScrollPane::SortByName(files, selected);
    CHECK_EQ(files[0].Operation, 3);
    CHECK_EQ(files[1].Operation, 2);
    CHECK_EQ(files[2].Operation, 1);
    CHECK_EQ(selected, 1);

    selected = -1;
    MCFileScrollPane::SortByName(files, selected);
    CHECK_EQ(selected, -1);
}

// Screens built from an ini file

TEST_CASE("logistics: a generic screen makes the elements its ini lists, leaving other element types empty")
{
    LogisticsScreen screen;
    MCMemoryFileSource& files = screen.Scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    files.AddFile("data\\screen.fit",
                  "FITini\r\n[Elements]\r\nl NumElements = 3\r\n"
                  "[Element0]\r\nl ElementType = 4\r\nl Left = 1\r\nl Top = 2\r\nl Width = 30\r\nl Height = 10\r\n"
                  "st NormalArt = \"NONE\"\r\n"
                  "[Element1]\r\nl ElementType = 2\r\nl Left = 0\r\nl Top = 0\r\nl Width = 0\r\nl Height = 0\r\n"
                  "st NormalArt = \"NONE\"\r\n"
                  "[Element2]\r\nl ElementType = 4\r\nl Left = 5\r\nl Top = 50\r\nl Width = 40\r\nl Height = 12\r\n"
                  "st NormalArt = \"NONE\"\r\nFITend\r\n");
    MCFitIniFile file;
    REQUIRE_EQ(file.Open("data\\screen.fit"), 0);
    // Without a background element the screen isn't placed by its ini: the test places it.
    MCGuiRef<MCGenericScreen> generic = MCGuiTree::NewRoot<MCGenericScreen>();
    generic->InitWithoutPort(0, 0, 640, 480);
    CHECK_EQ(generic->Init(file), 0);
    REQUIRE_EQ(generic->NumElements(), 3);
    CHECK(generic->Elements[1].expired());
    auto first = generic->Element<MCLogTextObject>(0);
    auto last = generic->Element<MCLogTextObject>(2);
    REQUIRE(first != nullptr);
    REQUIRE(last != nullptr);
    CHECK_EQ(last->Width(), 40);
    CHECK_EQ(last->BackgroundColor, 0x1f);
    CHECK(last->ParentObject() == generic.Pin());
    CHECK(!generic->ShowWindow);

    // Destroyed (its owner lets go), it lets go of its elements and children; the pin keeps its memory to look.
    const std::shared_ptr<MCGenericScreen> destroyed = generic.Pin();
    generic.reset();
    CHECK_EQ(destroyed->NumElements(), 0);
    CHECK(destroyed->ChildList.empty());
    CHECK(first->WasDestroyed());
}

// The purchase dialog

TEST_CASE("logistics: the purchase spinner counts up to what the resource points pay for, and down to 0")
{
    LogisticsScreen screen;
    const int32_t savedPoints = ResourcePoints;
    ResourcePoints = 350;
    MCGuiRef<MCPurchaseDlg> dialog = MCGuiTree::NewRoot<MCPurchaseDlg>();
    dialog->MCLogDialogBox::Init(0xe5, 0xa2, 0xb5, 0x9c);
    std::vector<std::pair<int32_t, int32_t>> answers;
    dialog->SetCallback([&](int32_t result, int32_t quantity) { answers.emplace_back(result, quantity); });
    dialog->Init(0, 100, 5, "Commando", {}, std::nullopt);
    CHECK(dialog->Spinner);
    CHECK_EQ(dialog->Quantity, 1);

    const int32_t upX = dialog->GlobalX() + 0x95;
    const int32_t upY = dialog->GlobalY() + 0x55;
    const int32_t downY = dialog->GlobalY() + 0x5d;

    for (int32_t i = 0; i < 4; i++)
    {
        Send(*dialog, MCGuiEventType::LeftButtonDown, upX, upY);
        Send(*dialog, MCGuiEventType::LeftButtonUp, upX, upY);
    }

    CHECK_EQ(dialog->Quantity, 3);
    // Held, the arrow repeats on its timer, as far as the points go.
    Send(*dialog, MCGuiEventType::LeftButtonDown, upX, upY);
    Send(*dialog, MCGuiEventType::Timer, 0, 0, 0, MCPurchaseDlg::RepeatTimer);
    CHECK_EQ(dialog->Quantity, 3);
    CHECK(dialog->Pressed == MCLogDialogBox::PressedPart::Up);
    Send(*dialog, MCGuiEventType::LeftButtonUp, upX, upY);
    CHECK(dialog->Pressed == MCLogDialogBox::PressedPart::None);

    for (int32_t i = 0; i < 5; i++)
    {
        Send(*dialog, MCGuiEventType::LeftButtonDown, upX, downY);
        Send(*dialog, MCGuiEventType::LeftButtonUp, upX, downY);
    }

    CHECK_EQ(dialog->Quantity, 0);

    // Enter answers OK (a two-button box), Escape cancel, with the quantity.
    Send(*dialog, MCGuiEventType::KeyDown, 0, 0, 0x0d);
    Send(*dialog, MCGuiEventType::KeyDown, 0, 0, 0x1b);
    REQUIRE_EQ(answers.size(), 2u);
    CHECK(answers[0] == std::make_pair(-1, 0));
    CHECK(answers[1] == std::make_pair(0, 0));
    ResourcePoints = savedPoints;
}

TEST_CASE("logistics: a sale's spinner is locked (but a component sale's), one item has none, and no limit means 199")
{
    LogisticsScreen screen;
    const int32_t savedPoints = ResourcePoints;
    ResourcePoints = 100000;
    MCGuiRef<MCPurchaseDlg> dialog = MCGuiTree::NewRoot<MCPurchaseDlg>();
    dialog->MCLogDialogBox::Init(0, 0, 0xb5, 0x9c);
    const int32_t upX = dialog->GlobalX() + 0x95;
    const int32_t upY = dialog->GlobalY() + 0x55;
    auto clickUp = [&]
    {
        Send(*dialog, MCGuiEventType::LeftButtonDown, upX, upY);
        Send(*dialog, MCGuiEventType::LeftButtonUp, upX, upY);
    };

    dialog->Init(1, -100, 3, "Commando", {}, std::nullopt);
    clickUp();
    CHECK_EQ(dialog->Quantity, 1);

    dialog->Init(5, -10, 3, "Laser", {}, std::nullopt);
    clickUp();
    CHECK_EQ(dialog->Quantity, 2);

    dialog->Init(4, 10, 1, "Laser", {}, std::nullopt);
    CHECK(!dialog->Spinner);
    clickUp();
    CHECK_EQ(dialog->Quantity, 1);

    dialog->Init(4, 1, -1, "Laser", {}, std::nullopt);
    CHECK_EQ(dialog->MaxQuantity, MCPurchaseDlg::MaxSpinnerQuantity);
    ResourcePoints = savedPoints;
}

// The message dialogs

TEST_CASE("game: the message dialog grows a middle piece for every two lines, and its keys answer (Escape runs OK too, "
          "OB-074)")
{
    LogisticsScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    MCGuiRef<MCReusableDialog> dialog = MCGuiTree::NewRoot<MCReusableDialog>();
    REQUIRE_EQ(dialog->Init(0, 0, 4, 4, {}), 0);
    dialog->SetText("Short.");
    CHECK_EQ(dialog->NumMiddlePieces, 1);
    const int32_t pieces = dialog->TopPiece->Height() + dialog->BottomPiece->Height();
    CHECK_EQ(dialog->Height(), pieces + dialog->MiddlePiece->Height());

    std::string longText;

    for (int32_t i = 0; i < 40; i++)
    {
        longText += "word ";
    }

    dialog->SetText(longText);
    const int32_t wrap = dialog->Width() - 0x14;
    int32_t lines = 1;

    for (std::string_view rest = dialog->Text;;)
    {
        const auto fit = static_cast<size_t>(MedBlueFont->CharactersToWidth(rest, wrap, true));

        if (fit < 1 || fit >= rest.size())
        {
            break;
        }

        rest = rest.substr(fit + 1);
        lines++;
    }

    REQUIRE(lines > 2);
    CHECK_EQ(dialog->NumMiddlePieces, (lines + 1) / 2);

    std::vector<int32_t> results;
    int32_t okRuns = 0;
    dialog->SetTwoButton(true);
    dialog->Callback = [&](int32_t result) { results.push_back(result); };
    dialog->OkButton->Callback().SetExec([&] { okRuns++; });
    dialog->KeepCallbacks = true;
    Send(*dialog, MCGuiEventType::KeyDown, 0, 0, 0x0d);
    Send(*dialog, MCGuiEventType::KeyDown, 0, 0, 0x1b);
    CHECK_EQ(okRuns, 2);
    REQUIRE_EQ(results.size(), 2u);
    CHECK_EQ(results[0], -1);
    CHECK_EQ(results[1], 0);
    // The second answer cleared the callbacks (KeepCallbacks kept them for one).
    CHECK(!dialog->Callback);

    // The timeout answers with its result.
    dialog->Callback = [&](int32_t result) { results.push_back(result); };
    dialog->TimeoutResult = 7;
    Send(*dialog, MCGuiEventType::Timer, 0, 0, 0, MCReusableDialog::TimeoutTimer);
    CHECK_EQ(results.back(), 7);
}

TEST_CASE("game: the refit dialog has a line per listed item between its six lines of text")
{
    LogisticsScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    MCGuiRef<MCRefitDialog> dialog = MCGuiTree::NewRoot<MCRefitDialog>();
    REQUIRE_EQ(dialog->Init(0, 0, 4, 4, {}), 0);
    dialog->SetText("Laser,Missile,Armor");
    CHECK_EQ(dialog->NumItems, 3);
    CHECK_EQ(dialog->NumMiddlePieces, 4);
    dialog->SetText("Laser");
    CHECK_EQ(dialog->NumItems, 1);
    CHECK_EQ(dialog->NumMiddlePieces, 3);
}

// Buttons and the resource figure

TEST_CASE("logistics: one button shows pressed at a time, and the mouse leaving lets the press go")
{
    LogisticsScreen screen;
    MCGuiRef<MCLogButton> first = MCGuiTree::NewRoot<MCLogButton>();
    MCGuiRef<MCLogButton> second = MCGuiTree::NewRoot<MCLogButton>();
    first->Init(0, 0, 10, 10, {});
    second->Init(20, 0, 10, 10, {});
    first->Press();
    CHECK(first->Pressed);
    second->Press();
    CHECK(!first->Pressed);
    CHECK(second->Pressed);
    CHECK(MCLogButton::HeldButton.lock() == second.Pin());

    second->Enter();
    CHECK(second->OverState);
    // Coming back over the button lets the press go too.
    CHECK(!second->Pressed);
    second->Press();
    second->Leave();
    CHECK(!second->OverState);
    CHECK(!second->Pressed);
    CHECK(MCLogButton::HeldButton.expired());

    // A disabled button ignores clicks (it only refuses with a sound).
    int32_t runs = 0;
    first->Callback().SetExec([&] { runs++; });
    first->Disabled = true;
    Send(*first, MCGuiEventType::LeftButtonDown);
    CHECK_EQ(runs, 0);
    CHECK(!first->Pressed);

    // A destroyed button lets go of the press.
    first->Disabled = false;
    first->Press();
    first.reset();
    CHECK(MCLogButton::HeldButton.expired());
}

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

#include "stdafx.h"
#include "MCTest.h"
#include "GuiTree.h"
#include "TestGame.h"
#include "fakes/MCManualClock.h"
#include "engine/MCFont.h"
#include "gui/MCFloatHelp.h"
#include "gui/MCGuiButton.h"
#include "gui/MCGuiButtonBar.h"
#include "gui/MCGuiChatWindow.h"
#include "gui/MCGuiComboBox.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiListBox.h"
#include "gui/MCGuiMenu.h"
#include "gui/MCGuiScrollBar.h"
#include "gui/MCGuiScrollTextObject.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCGuiTextObject.h"
#include "gui/MCGuiTimerManager.h"
#include "gui/MCListView.h"
#include "gui/MCScrollPane.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "main/MCGameContext.h"

// The GUI's controls: text fields, list and combo boxes, menus, button bars, scroll bars, scrolling text, the chat
// line, scroll panes and help tags. Most run on a GUI system with a 640x480 screen window and letterless fonts (they
// measure 0 and draw nothing); the ones that need art or real letters read them from the retail install.

namespace
{
    /// <summary>
    /// A test context with a manual clock and a GUI system whose screen window is 640x480 (no display or art), and
    /// every font a letterless one (0 high, 0 wide).
    /// </summary>
    struct ControlScreen
    {
        ControlScreen() : Clock(Scope.Context().SetClock(std::make_unique<MCManualClock>()))
        {
            Scope.Context().SetGuiSystem(std::make_unique<MCGuiSystem>());
            Gui = Scope.Context().GuiSystem();
            Gui->ScreenWidth = 640;
            Gui->ScreenHeight = 480;
            Gui->TimerManager = std::make_unique<MCGuiTimerManager>();
            Gui->MakeScreen(640, 480);
            SetFonts(NoLetters);
        }

        ~ControlScreen()
        {
            SetFonts(nullptr);

            if (Retail)
            {
                FontPath = SavedFontPath;
                ArtPath = SavedArtPath;
            }
        }

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

        /// <summary>
        /// Reads the art file and the fonts the tests use from the install; null without one (the test then passes as
        /// skipped).
        /// </summary>
        bool LoadRetail()
        {
            // The art file's pictures are read as child files of it, so the files come from the install itself.
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
            Gui->ArtFile = std::make_unique<MCPacketFile>();
            REQUIRE_EQ(Gui->ArtFile->Open("data\\art\\art.pak"), 0);
            Grey = std::move(*MCGuiFont::Create("gryfnt.fnt"));
            White = std::move(*MCGuiFont::Create("white.fnt"));
            GreyFont = Grey;
            WhiteFont = White;
            return true;
        }

        MCTestContextScope Scope;
        MCManualClock& Clock;
        std::shared_ptr<MCGuiSystem> Gui;
        std::shared_ptr<MCGuiFont> NoLetters = std::make_shared<MCGuiFont>();
        std::shared_ptr<MCGuiFont> Grey;
        std::shared_ptr<MCGuiFont> White;
        bool Retail = false;
        std::string SavedFontPath;
        std::string SavedArtPath;
    };

    /// <summary>An object that keeps the type of every event it is given.</summary>
    class MCTypeRecorder : public MCGuiObject
    {
    public:
        void HandleEvent(MCGuiEvent& event) override
        {
            Types.push_back(event.Type);
            MCGuiObject::HandleEvent(event);
        }

        /// <summary>How many events of <paramref name="type"/> came.</summary>
        int32_t Count(int32_t type) const { return static_cast<int32_t>(std::ranges::count(Types, type)); }

        std::vector<int32_t> Types;
    };

    /// <summary>
    /// A new object of type <typeparamref name="T"/> made on <paramref name="parent"/> at (x, y) with the given size (the
    /// parent owns it).
    /// </summary>
    template <typename T> MCGuiHandle<T> Make(int32_t x, int32_t y, int32_t width, int32_t height, MCGuiObject& parent)
    {
        return MCGuiTree::MakeChild<T>(parent, x, y, width, height);
    }

    /// <summary>A new object of type <typeparamref name="T"/> made on the screen window at (x, y) with the given size.</summary>
    template <typename T> MCGuiHandle<T> Make(int32_t x, int32_t y, int32_t width, int32_t height)
    {
        return Make<T>(x, y, width, height, *ScreenWindow());
    }

    /// <summary>Gives <paramref name="object"/> an event of <paramref name="type"/> at screen point (x, y).</summary>
    void Send(MCGuiObject& object, int32_t type, int32_t x = 0, int32_t y = 0, uint8_t key = 0)
    {
        MCGuiEvent event;
        event.Type = type;
        event.X = x;
        event.Y = y;
        event.Key = key;
        object.HandleEvent(event);
    }

    /// <summary>Types <paramref name="text"/> into <paramref name="object"/>, a character event each.</summary>
    void Type(MCGuiObject& object, std::string_view text)
    {
        for (const char c : text)
        {
            Send(object, MCGuiEventType::Character, 0, 0, static_cast<uint8_t>(c));
        }
    }

    /// <summary>The lines of a scroll text's buffer: (colour, text).</summary>
    std::vector<std::pair<uint8_t, std::string>> Lines(const MCGuiScrollTextObject& text)
    {
        std::vector<std::pair<uint8_t, std::string>> lines;
        std::string_view rest = text.TextBuffer;

        while (!rest.empty())
        {
            const size_t end = rest.find('\n');
            lines.emplace_back(static_cast<uint8_t>(rest[0]), std::string(rest.substr(1, end - 1)));
            rest.remove_prefix(end + 1);
        }

        return lines;
    }
}

// Text fields

TEST_CASE("gui: a text field takes printable characters only while it has the keyboard, and Enter tells its parent")
{
    ControlScreen screen;
    auto parent = Make<MCTypeRecorder>(0, 0, 200, 100);
    auto field = Make<MCGuiTextObject>(10, 10, 100, 20, *parent);

    Type(*field, "x");
    CHECK_EQ(field->Text, std::string());

    screen.Gui->SetText(field);
    Type(*field, "ab\x01"
                 "c\x80");
    CHECK_EQ(field->Text, std::string("abc"));
    CHECK_EQ(field->TextLength, 3);

    Type(*field, "\r");
    CHECK_EQ(parent->Count(MCGuiEventType::TextEntered), 1);
    CHECK_EQ(field->Text, std::string("abc"));
}

TEST_CASE("gui: a text field's backspace leaves the deleted character until the next edit (OB-067)")
{
    ControlScreen screen;
    auto field = Make<MCGuiTextObject>(10, 10, 100, 20);
    screen.Gui->SetText(field);
    Type(*field, "abc");

    // The first backspace moves the caret back over "c" but leaves it in the text.
    Type(*field, "\b");
    CHECK_EQ(field->TextLength, 2);
    CHECK_EQ(field->Text, std::string("abc"));

    // The second clears "c" (the character after the caret) and moves back over "b".
    Type(*field, "\b");
    CHECK_EQ(field->TextLength, 1);
    CHECK_EQ(field->Text, std::string("ab"));

    // Typing replaces what is after the caret.
    Type(*field, "x");
    CHECK_EQ(field->Text, std::string("ax"));
    CHECK_EQ(field->TextLength, 2);

    // Backspace at the start does nothing.
    Type(*field, "\b\b\b\b");
    CHECK_EQ(field->TextLength, 0);
}

TEST_CASE("gui: a text field has no length limit, a read-only one ignores the keyboard, and text ends at a NUL")
{
    ControlScreen screen;
    auto field = Make<MCGuiTextObject>(10, 10, 100, 20);
    screen.Gui->SetText(field);
    Type(*field, std::string(300, 'w'));
    CHECK_EQ(field->TextLength, 300);

    field->SetText(std::string_view("ab\0cd", 5));
    CHECK_EQ(field->Text, std::string("ab"));
    CHECK_EQ(field->TextLength, 2);

    auto locked = Make<MCGuiTextObject>(10, 40, 100, 20);
    locked->ReadOnly = true;
    Send(*locked, MCGuiEventType::LeftButtonDown, 15, 45);
    CHECK(!(locked == screen.Gui->TextObject()));
    screen.Gui->SetText(locked);
    Type(*locked, "abc");
    CHECK_EQ(locked->Text, std::string());
}

// List boxes

TEST_CASE("gui: a list box rounds its height up to whole lines and, without a scroll bar, grows with its items")
{
    ControlScreen screen;
    // Letterless font: a line is 0 + 8 pixels.
    auto list = Make<MCGuiListBox>(10, 10, 100, 20);
    CHECK_EQ(list->ItemHeight, 8);
    CHECK_EQ(list->Height(), 24);

    for (const std::string_view item : {"alpha", "beta", "gamma", "delta"})
    {
        list->AddItem(item);
    }

    CHECK_EQ(list->NumItems(), 4);
    CHECK_EQ(list->Height(), 32);
    REQUIRE(list->GetItemString(2).has_value());
    CHECK_EQ(*list->GetItemString(2), std::string("gamma"));
    CHECK(!list->GetItemString(4).has_value());

    // No item limit (the original held 100 strings of 39 characters).
    for (int32_t i = 0; i < 150; i++)
    {
        list->AddItem(std::string(60, 'x'));
    }

    CHECK_EQ(list->NumItems(), 154);
    CHECK_EQ(list->GetItemString(153)->size(), 60u);
}

TEST_CASE("gui: a list box selects the line clicked and moves the selection with the keys, scrolling to keep it shown")
{
    ControlScreen screen;
    auto list = Make<MCGuiListBox>(10, 10, 100, 24);

    for (int32_t i = 0; i < 10; i++)
    {
        list->Items.push_back(std::format("item {}", i));
    }

    REQUIRE_EQ(list->VisibleItems(), 3);

    // A click on the second line.
    Send(*list, MCGuiEventType::LeftButtonDown, 20, list->GlobalY() + 8 + 3);
    CHECK_EQ(list->SelectedItem, 1);
    CHECK_EQ(list->HighlightedItem, 1);
    Send(*list, MCGuiEventType::LeftButtonUp, 20, list->GlobalY() + 8 + 3);

    auto key = [&](uint8_t code) { Send(*list, MCGuiEventType::KeyDown, 0, 0, code); };
    constexpr uint8_t pageUp = 0x21;
    constexpr uint8_t pageDown = 0x22;
    constexpr uint8_t end = 0x23;
    constexpr uint8_t home = 0x24;
    constexpr uint8_t down = 0x28;

    key(home);
    CHECK_EQ(list->TopItem, 0);
    CHECK_EQ(list->SelectedItem, 0);

    // Down past the third line scrolls a line.
    key(down);
    key(down);
    CHECK_EQ(list->TopItem, 0);
    key(down);
    CHECK_EQ(list->SelectedItem, 3);
    CHECK_EQ(list->TopItem, 1);

    // End shows the last page; Page Up goes back a page of lines; Page Down stops at the last page.
    key(end);
    CHECK_EQ(list->TopItem, 7);
    CHECK_EQ(list->SelectedItem, 9);
    key(pageUp);
    CHECK_EQ(list->TopItem, 4);
    CHECK_EQ(list->SelectedItem, 6);
    key(pageDown);
    CHECK_EQ(list->TopItem, 7);
    CHECK_EQ(list->SelectedItem, 9);

    // Down on the last line does nothing.
    key(down);
    CHECK_EQ(list->SelectedItem, 9);
}

TEST_CASE("gui: dragging below a list box scrolls it a line and selects the bottom line")
{
    ControlScreen screen;
    auto list = Make<MCGuiListBox>(10, 10, 100, 24);

    for (int32_t i = 0; i < 5; i++)
    {
        list->Items.push_back(std::format("item {}", i));
    }

    Send(*list, MCGuiEventType::LeftButtonDown, 20, list->GlobalY() + 2);
    REQUIRE(list == screen.Gui->GrabbedObject());
    Send(*list, MCGuiEventType::MouseMove, 20, list->GlobalY() + 40);
    CHECK_EQ(list->TopItem, 1);
    CHECK_EQ(list->SelectedItem, 3);
    Send(*list, MCGuiEventType::MouseMove, 20, list->GlobalY() + 40);
    Send(*list, MCGuiEventType::MouseMove, 20, list->GlobalY() + 40);
    CHECK_EQ(list->TopItem, 2);
    CHECK_EQ(list->SelectedItem, 4);

    // Above: back up a line at a time, the top line selected.
    Send(*list, MCGuiEventType::MouseMove, 20, list->GlobalY() - 5);
    CHECK_EQ(list->TopItem, 1);
    CHECK_EQ(list->SelectedItem, 1);
    Send(*list, MCGuiEventType::LeftButtonUp, 20, list->GlobalY() - 5);
}

// Menus

TEST_CASE("gui: a menu runs the item released over, hides itself, and sizes itself to its items")
{
    ControlScreen screen;
    auto menu = Make<MCGuiMenu>(100, 100, 10, 10);
    std::vector<int32_t> ran;

    for (int32_t i = 0; i < 3; i++)
    {
        CHECK_EQ(menu->AddItem(std::format("item {}", i)), i);
        menu->SetCallback(i, [&ran, i]() { ran.push_back(i); });
    }

    // Letterless font: an item is 8 high; the width is a quarter more than the text's (0) plus 4.
    CHECK_EQ(menu->ItemHeight, 8);
    CHECK_EQ(menu->Height(), 24);
    CHECK_EQ(menu->Width(), 5);
    menu->ShowGuiWindow(true);

    Send(*menu, MCGuiEventType::LeftButtonUp, 102, menu->GlobalY() + 8 + 3);
    CHECK(ran == std::vector<int32_t>{1});
    CHECK(!menu->IsShowing());
    CHECK(menu->Shown);

    // A release outside runs nothing but still hides it.
    menu->ShowGuiWindow(true);
    Send(*menu, MCGuiEventType::LeftButtonUp, 300, 300);
    CHECK_EQ(ran.size(), 1u);
    CHECK(!menu->IsShowing());
}

TEST_CASE("gui: a menu's items keep their letters and data when one before them is removed, and there is no item limit")
{
    ControlScreen screen;
    auto menu = Make<MCGuiMenu>(100, 100, 10, 10);
    menu->AddItem("Open");
    menu->AddItem("Save");
    menu->AddSeparator();
    menu->AddItem("Quit");
    menu->SetItemLetter(1, 'S');
    menu->SetItemData(1, 42);
    menu->SetItemLetter(3, 'Q');

    CHECK_EQ(menu->NumItems(), 4);
    CHECK(menu->Items[2].Callback == nullptr);
    CHECK_EQ(menu->Items[2].Text, std::string(MCGuiMenu::Separator));

    // Removing by text is case-insensitive.
    CHECK(menu->RemoveItem("OPEN"));
    CHECK(!menu->RemoveItem("Close"));
    CHECK_EQ(menu->Items[0].Text, std::string("Save"));
    CHECK_EQ(menu->GetItemLetter(0), 'S');
    CHECK_EQ(menu->GetItemData(0), 42);
    CHECK_EQ(menu->GetItemLetter(2), 'Q');
    CHECK_EQ(menu->GetItemData(7), MCGuiMenu::BadIndex);
    CHECK_EQ(menu->GetItemLetter(7), '\x03');
    CHECK(!menu->ChangeItemString(7, "x"));

    // The original held 25 items of 39 characters.
    for (int32_t i = 0; i < 40; i++)
    {
        menu->AddItem(std::string(50, 'm'));
    }

    CHECK_EQ(menu->NumItems(), 43);
    CHECK_EQ(menu->Items[42].Text.size(), 50u);
}

// Button bars

TEST_CASE("gui: a button bar lays its buttons out in rows (or columns) of its length and sizes itself to them")
{
    ControlScreen screen;
    auto bar = Make<MCGuiWindowBar>(0, 0, 10, 10);
    std::vector<MCGuiHandle<MCGuiButton>> buttons;

    for (int32_t i = 0; i < 10; i++)
    {
        buttons.push_back(bar->AddButton([](MCGuiButton& made) { made.Init(0, 0, 4, 4, {}); }));
    }

    bar->SetMaxLength(4);
    // Rows of 4: 3 rows, 4 across, 32x32 each.
    CHECK_EQ(bar->Width(), 128);
    CHECK_EQ(bar->Height(), 96);
    CHECK_EQ(buttons[6]->X(), 64);
    CHECK_EQ(buttons[6]->Y(), 32);

    // Columns of 4.
    bar->SetHorizontal(false);
    CHECK_EQ(bar->Width(), 96);
    CHECK_EQ(bar->Height(), 128);
    CHECK_EQ(buttons[6]->X(), 32);
    CHECK_EQ(buttons[6]->Y(), 64);

    // Removing a button moves the rest up a place.
    bar->SetHorizontal(true);
    CHECK(bar->RemoveButton(0));
    CHECK(!bar->RemoveButton(9));
    CHECK_EQ(bar->NumButtons(), 9);
    CHECK(bar->GetButton(0) == buttons[1].Lock());
    // The removed button went with its place on the bar.
    CHECK(buttons[0] == nullptr);
    CHECK_EQ(buttons[1]->X(), 0);
    CHECK_EQ(buttons[5]->X(), 0);
    CHECK_EQ(buttons[5]->Y(), 32);
    CHECK(bar->GetButton(9) == nullptr);
}

TEST_CASE("gui: a button bar takes more than 25 buttons, and removing from a full one keeps its rows (OB-064 fixed)")
{
    ControlScreen screen;
    auto bar = Make<MCGuiWindowBar>(0, 0, 10, 10);

    const auto makeButton = [](MCGuiButton& made) { made.Init(0, 0, 4, 4, {}); };

    for (int32_t i = 0; i < 30; i++)
    {
        bar->AddButton(makeButton);
    }

    CHECK_EQ(bar->NumButtons(), 30);
    CHECK(bar->RemoveButton(29));
    CHECK(bar->Horizontal);

    // Insert in the middle; past the end makes nothing.
    const MCGuiHandle<MCGuiButton> inserted = bar->InsertButton(3, makeButton);
    REQUIRE(inserted != nullptr);
    CHECK(bar->GetButton(3) == inserted.Lock());
    CHECK_EQ(inserted->X(), 96);
    CHECK(bar->InsertButton(31, makeButton) == nullptr);
    CHECK_EQ(bar->NumButtons(), 30);
}

// Scrolling text

TEST_CASE("gui: scrolling text grows its picture with its lines and shows the thumb as the visible share")
{
    ControlScreen screen;
    // Letterless font: a line is 0 + 2 pixels; the window is 40 high, so its thumb track is 40 - 32 = 8.
    auto text = Make<MCGuiScrollTextObject>(10, 10, 100, 40);
    REQUIRE(!text->InitFailed);

    for (int32_t i = 0; i < 10; i++)
    {
        text->Print(std::format("line {}", i), 0x1f);
    }

    CHECK_EQ(text->NumLines, 10);
    CHECK_EQ(text->Port().Height(), 40);
    CHECK(!text->ScrollTab->IsShowing());

    for (int32_t i = 10; i < 30; i++)
    {
        text->Print(std::format("line {}", i), 0x10);
    }

    // 30 lines are 60 pixels; the thumb is 40/60 of the track.
    CHECK_EQ(text->Port().Height(), 60);
    CHECK(text->ScrollTab->IsShowing());
    CHECK_EQ(text->ScrollTab->Height(), 5);
    CHECK_EQ(text->ScrollTab->Y(), 0x10);

    // A blank line counts but doesn't grow the picture.
    text->PrintBlank(0x1f);
    CHECK_EQ(text->NumLines, 31);
    CHECK_EQ(text->Port().Height(), 60);

    const auto lines = Lines(*text);
    REQUIRE_EQ(lines.size(), 31u);
    CHECK_EQ(lines[12].first, uint8_t{0x10});
    CHECK_EQ(lines[12].second, std::string("line 12"));
    CHECK_EQ(lines[30].second, std::string());
}

TEST_CASE("gui: scrolling text steps a line per arrow or wheel notch and a page per track click, within its picture")
{
    ControlScreen screen;
    auto text = Make<MCGuiScrollTextObject>(10, 10, 100, 40);

    for (int32_t i = 0; i < 30; i++)
    {
        text->Print("x", 0x1f);
    }

    // The picture is 60 high and 40 shown: 20 pixels of scrolling.
    text->ReceiveClick(1, 0);
    CHECK_EQ(text->FirstPixel, 2);
    text->ReceiveClick(-1, 0);
    text->ReceiveClick(-1, 0);
    CHECK_EQ(text->FirstPixel, 0);

    // A click on the track below the thumb pages down (stopping at the end), above it up.
    text->ReceiveClick(0, text->ScrollTab->Bottom() + 1);
    CHECK_EQ(text->FirstPixel, 20);
    CHECK_EQ(text->ScrollTab->Y(), 0x10 + (8 - 5));
    text->ReceiveClick(0, 0);
    CHECK_EQ(text->FirstPixel, 0);

    CHECK(text->MouseWheel(3, 0, 0));
    CHECK_EQ(text->FirstPixel, 6);
    CHECK(text->MouseWheel(-1, 0, 0));
    CHECK_EQ(text->FirstPixel, 4);

    // The thumb at the bottom of its track shows the end.
    text->CalcFirstPixel(8 - 5);
    CHECK_EQ(text->FirstPixel, 20);

    // Text that fits doesn't take the wheel.
    text->Clear();
    auto small = Make<MCGuiScrollTextObject>(10, 100, 100, 40);
    small->Print("x", 0x1f);
    CHECK(!small->MouseWheel(1, 0, 0));
}

TEST_CASE("gui: scrolling text keeps more than the original's 4 KB, and Clear empties it and its sections")
{
    ControlScreen screen;
    auto text = Make<MCGuiScrollTextObject>(10, 10, 100, 40);
    const std::string line(30, 'k');

    for (int32_t i = 0; i < 300; i++)
    {
        text->Print(line, 0x1f);
    }

    CHECK_EQ(text->NumLines, 300);
    CHECK_EQ(text->TextBuffer.size(), 300u * 32);
    CHECK_EQ(text->Port().Height(), 600);

    text->SectionStarts[2] = 5;
    text->SectionColors[2] = 0x44;
    text->FirstPixel = 30;
    text->Clear();
    CHECK_EQ(text->NumLines, 0);
    CHECK(text->TextBuffer.empty());
    CHECK_EQ(text->FirstPixel, 0);
    CHECK_EQ(text->SectionStarts[2], -1);
    CHECK_EQ(text->SectionColors[2], uint8_t{0x44});
}

// Help tags

TEST_CASE("gui: a help tag keeps its whole text (the original cut it at 63 characters) and is skipped by hit tests")
{
    ControlScreen screen;
    auto tag = Make<MCFloatHelp>(0, 0, 10, 10);
    CHECK_EQ(tag->ObjectType, MCFloatHelp::TagObjectType);
    CHECK(tag->FindObject(5, 5) == nullptr);

    const std::string text = std::string(80, 'h') + "\nsecond";
    tag->SetHelpText(text);
    CHECK_EQ(tag->HelpText, text);
    tag->SetHelpText(std::string_view("ab\0cd", 5));
    CHECK_EQ(tag->HelpText, std::string("ab"));
}

// With the install's art and fonts

TEST_CASE("game: a scroll bar's arrows step a line, its track a page of 10, within its range, and tell its parent")
{
    ControlScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    auto parent = Make<MCTypeRecorder>(0, 0, 200, 200);
    auto bar = Make<MCGuiScrollBar>(10, 10, 0, 100, *parent);
    CHECK_EQ(bar->Width(), 0x12);
    bar->SetScrollMax(20);

    APostMessage(*bar, MCGuiScrollMessage::LineDown);
    CHECK_EQ(bar->ScrollPos, 1);
    CHECK_EQ(parent->Count(MCGuiScrollMessage::Changed), 1);
    APostMessage(*bar, MCGuiScrollMessage::PageDown);
    CHECK_EQ(bar->ScrollPos, 11);
    APostMessage(*bar, MCGuiScrollMessage::PageDown);
    CHECK_EQ(bar->ScrollPos, 20);

    // The thumb (16 high) travels between the 9-pixel arrows: at the end it is 100 - 34 down from the top arrow.
    CHECK_EQ(bar->ScrollTab->Y(), 9 + 66);
    CHECK_EQ(bar->DownArea->Y(), 9 + 66 + 16);

    // Setting the position quietly tells nobody; it is clamped.
    MCGuiEvent quiet;
    quiet.Type = MCGuiScrollMessage::SetPositionQuietly;
    quiet.LParam = -5;
    bar->HandleEvent(quiet);
    CHECK_EQ(bar->ScrollPos, 0);
    CHECK_EQ(parent->Count(MCGuiScrollMessage::Changed), 3);
    CHECK_EQ(bar->ScrollTab->Y(), 9);

    // Pressing the down arrow steps at once, and again each time its repeat timer fires.
    Send(*bar->DownButton, MCGuiEventType::LeftButtonDown);
    CHECK_EQ(bar->ScrollPos, 1);
    MCGuiEvent timer;
    timer.Type = MCGuiEventType::Timer;
    timer.Data = 4;
    bar->DownButton->HandleEvent(timer);
    CHECK_EQ(bar->ScrollPos, 2);
    Send(*bar->DownButton, MCGuiEventType::LeftButtonUp);

    // No range: the thumb hides.
    bar->SetScrollMax(0);
    CHECK(!bar->ScrollTab->IsShowing());
}

TEST_CASE("game: a list box with a scroll bar sets its range to the hidden lines and follows the bar")
{
    ControlScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    auto list = Make<MCGuiListBox>(10, 10, 100, 40);
    const int32_t visible = list->VisibleItems();
    REQUIRE_EQ(list->ActivateScrollbar(), 0);
    REQUIRE(list->ScrollBar != nullptr);
    CHECK(!list->ScrollBar->IsShowing());

    for (int32_t i = 0; i < 12; i++)
    {
        list->AddItem(std::format("item {}", i));
    }

    // The box keeps its size; the bar's range is the lines past the shown ones.
    CHECK_EQ(list->Height(), visible * list->ItemHeight);
    CHECK_EQ(list->ScrollBar->ScrollMax, 12 - visible);

    APostMessage(*list->ScrollBar, MCGuiScrollMessage::PageDown);
    CHECK_EQ(list->ScrollBar->ScrollPos, std::min(10, 12 - visible));
    CHECK_EQ(list->TopItem, list->ScrollBar->ScrollPos);
}

TEST_CASE("game: a combo box opens its list on its button and takes the list's selection into its field")
{
    ControlScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    auto combo = Make<MCGuiComboBox>(10, 10, 120, 20);
    combo->ListBox->AddItem("alpha");
    combo->ListBox->AddItem("beta");
    CHECK(combo->TextField->ReadOnly);
    CHECK(!combo->ListBox->IsShowing());

    CHECK(combo->SelectItem(1));
    CHECK_EQ(combo->TextField->Text, std::string("beta"));
    CHECK(!combo->SelectItem(2));

    combo->DropButton->RunCallback();
    CHECK(combo->ListBox->IsShowing());
    CHECK(combo->DropButton->Pushed);

    combo->ListBox->SelectItem(0);
    APostMessage(*combo, MCGuiComboBox::ToggleList);
    CHECK(!combo->ListBox->IsShowing());
    CHECK(!combo->DropButton->Pushed);
    CHECK_EQ(combo->TextField->Text, std::string("alpha"));

    CHECK(combo->ChangeItemString(0, "first"));
    CHECK_EQ(combo->TextField->Text, std::string("first"));
}

TEST_CASE("game: wrapped scrolling text breaks at spaces so each line fits, keeping a word too long for a line whole")
{
    ControlScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    // Fonts[0][0] is the font the wrapping measures with.
    Fonts[0][0] = screen.Grey;
    auto text = Make<MCGuiScrollTextObject>(10, 10, 120, 40);
    const std::string sentence = "the quick brown fox jumps over the lazy dog and keeps on running far away";
    text->PrintWrapped(sentence, 0x1f, -1);

    const auto lines = Lines(*text);
    REQUIRE(lines.size() > 1);
    std::string joined;

    for (const auto& [color, line] : lines)
    {
        CHECK_EQ(color, uint8_t{0x1f});
        CHECK(screen.Grey->Width(line) <= 120 - 6);
        joined += (joined.empty() ? "" : " ") + line;
    }

    CHECK_EQ(joined, sentence);

    // A first word wider than the line: the rest is printed whole.
    text->Clear();
    const std::string longWord = std::string(60, 'W') + " end";
    text->PrintWrapped(longWord, 0x1f, -1);
    REQUIRE_EQ(Lines(*text).size(), 1u);
    CHECK_EQ(Lines(*text)[0].second, longWord);
}

TEST_CASE("game: the chat line takes chat characters up to three lines, and backspace and Enter edit it")
{
    ControlScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    auto chat = Make<MCGuiChatWindow>(10, 10, 0x82, 0x40);
    MCGuiChatInput& input = *chat->ChatInput;
    screen.Gui->SetText(input.weak_from_this());

    // '%' is the chat formatter's code character.
    Type(input, "hi %there");
    CHECK_EQ(input.Text, std::string("hi there"));
    Type(input, "\b\b");
    CHECK_EQ(input.Text, std::string("hi the"));

    // A character that would start a fourth line is taken back.
    Type(input, std::string(400, 'm'));
    CHECK(input.Text.size() < 406u);
    CHECK(input.Text.size() <= MCGuiChatInput::MaxLength);
    CHECK(input.DrawAndCheck(2));
    const size_t full = input.Text.size();
    Type(input, "m");
    CHECK_EQ(input.Text.size(), full);

    // Enter (without a network game) clears the line and lets go of the keyboard.
    Type(input, "\r");
    CHECK(input.Text.empty());
    CHECK(screen.Gui->TextObject().get() != &input);
    CHECK_EQ(input.CursorX, 0x14);
}

TEST_CASE("game: a scroll pane's position is a percentage of its content, and the slider follows it")
{
    ControlScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    // A pane 0x80 x 0x60 showing content four times its height.
    auto pane = MCGuiTree::MakeRoot<MCScrollPane>(0x80, 0x60, 100, 100);
    auto content = std::make_shared<MCLogPort>();
    content->Init(0x80 - 13, 0x180);
    pane->SetDisplayPort(std::move(content), true);

    // A scroll unit is 1% of the content; the most it scrolls is the content less the pane: 75%.
    CHECK_EQ(pane->ScrollUnit, static_cast<float>(0x180 * 0.01));
    CHECK_EQ(pane->MaxScroll, static_cast<float>(0x180 - 0x60) / pane->ScrollUnit);
    // The slider is the pane's share of the track (the column less its arrows): a quarter of 0x60 - 32.
    CHECK_EQ(pane->SliderHeight, 16);
    CHECK_EQ(pane->SliderPos, 16);

    // Halfway: the offset is half the content, the slider half way down the track (each a pixel short: the unit and
    // the 1% are single-precision, a hair under their values, and the results are truncated).
    pane->SetScrollPos(50.0f);
    CHECK_EQ(pane->GetScrollOffset(), 0xc0 - 1);
    CHECK_EQ(pane->GetScrollBottom(), 0xc0 + 0x60 - 1);
    CHECK_EQ(pane->SliderPos, 16 + (0x60 - 32) / 2 - 1);

    // Past the end and before the start are clamped.
    pane->SetScrollPos(500.0f);
    CHECK_EQ(pane->ScrollPos, pane->MaxScroll);
    pane->SetScrollPos(-5.0f);
    CHECK_EQ(pane->ScrollPos, 0.0f);

    // The slider dragged to the bottom of its travel scrolls to the end.
    pane->SetSliderPos(1000);
    CHECK_EQ(pane->SliderPos, pane->SliderMax);
    CHECK_EQ(pane->SliderMax, 0x60 - 16 - 16);
    CHECK_EQ(
        pane->ScrollPos,
        std::min(static_cast<float>(static_cast<double>(pane->SliderMax - 16) / (0x60 - 32) * 100.0), pane->MaxScroll));

    // Content that fits has no slider.
    auto small = std::make_shared<MCLogPort>();
    small->Init(0x80 - 13, 0x40);
    pane->SetDisplayPort(std::move(small), true);
    CHECK_EQ(pane->SliderHeight, 0);
    CHECK(!pane->MouseWheel(1, 0, 0));
}

TEST_CASE("game: a scroll pane shares its content: content no one else keeps goes when the pane shows other content")
{
    ControlScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    // A port that notes its own destruction.
    struct WatchedPort : MCLogPort
    {
        explicit WatchedPort(bool& destroyed) : Destroyed(destroyed) {}
        ~WatchedPort() override { Destroyed = true; }
        bool& Destroyed;
    };

    auto pane = MCGuiTree::MakeRoot<MCScrollPane>(0x80, 0x60, 100, 100);
    REQUIRE(pane->ContentPort != nullptr);

    bool givenGone = false;
    auto given = std::make_shared<WatchedPort>(givenGone);
    given->Init(0x80 - 13, 0x100);
    const std::weak_ptr<WatchedPort> givenRef = given;
    pane->SetDisplayPort(std::move(given), true);

    // Shown again (the logistics store does), it stays.
    pane->SetDisplayPort(givenRef.lock(), false);
    CHECK(!givenGone);
    CHECK(pane->ContentPort == givenRef.lock());

    // Content its maker keeps outlives the pane's share.
    bool keptGone = false;
    auto kept = std::make_shared<WatchedPort>(keptGone);
    kept->Init(0x80 - 13, 0x100);
    pane->SetDisplayPort(kept, true);
    CHECK(givenGone);
    CHECK(pane->ContentPort == kept);

    pane->ClearDisplayPort();
    CHECK(pane->ContentPort == nullptr);
    pane.reset();
    CHECK(!keptGone);
}

namespace
{
    /// <summary>An item of a test list: a key it is drawn with, whether it gets a row and whether its row takes clicks.</summary>
    struct TestListItem
    {
        int32_t Key = 0;
        bool Listed = true;
        bool Clickable = true;
    };

    /// <summary>What the test rows drew: each row's item key and its top, in drawing order.</summary>
    std::vector<std::pair<int32_t, int32_t>> DrawnRows;

    /// <summary>A test list's row: 0x40 x 0x10, it notes what it draws and how often it was refreshed.</summary>
    class ListItemRow : public MCListRow
    {
    public:
        void Init(std::shared_ptr<TestListItem> item)
        {
            _Item = std::move(item);
            InitWithoutPort(0, 0, 0x40, 0x10);
        }

        void DrawRow(MCLogPort&, int32_t top) override { DrawnRows.emplace_back(_Item.lock()->Key, top); }
        void OnRefresh() override { ++Refreshes; }

        /// <summary>How many refreshes placed the row.</summary>
        int32_t Refreshes = 0;

    private:
        std::weak_ptr<TestListItem> _Item;
    };

    /// <summary>A list over <paramref name="items"/>: the listed ones, by key; the row height 0x10, at least 0x30 tall.</summary>
    void ShowItems(MCListView& list, std::vector<std::shared_ptr<TestListItem>>& items, bool resetScroll = true)
    {
        MCListSource<TestListItem> source;
        source.Items = [&items]() -> const std::vector<std::shared_ptr<TestListItem>>& { return items; };
        source.Filter = [](const TestListItem& item) { return item.Listed; };
        source.Sort = [](const TestListItem& first, const TestListItem& second) { return first.Key < second.Key; };
        source.Clickable = [](const TestListItem& item) { return item.Clickable; };
        source.MakeRow = [](MCListView& view, std::shared_ptr<TestListItem> item)
        { return view.CreateRow<ListItemRow>([&](ListItemRow& row) { row.Init(item); }); };
        list.Show(std::move(source), MCListLayout{0x10, 0x40, 0x30, 0x10}, resetScroll);
    }

    /// <summary>The key of the item each of <paramref name="list"/>'s rows shows, in order.</summary>
    std::vector<int32_t> RowKeys(const MCListView& list)
    {
        std::vector<int32_t> keys;

        for (const MCGuiHandle<MCListRow>& row : list.Rows())
        {
            keys.push_back(std::static_pointer_cast<TestListItem>(row->ListItem().lock())->Key);
        }

        return keys;
    }

    /// <summary>Draws <paramref name="list"/>'s content into a picture; returns what the rows drew.</summary>
    std::vector<std::pair<int32_t, int32_t>> DrawList(MCListView& list)
    {
        DrawnRows.clear();
        MCLogPort picture;
        picture.Init(0x40, 0x30);
        list.DrawContentTo(picture.Frame(), 0, 0);
        return std::exchange(DrawnRows, {});
    }
}

TEST_CASE("game: a list view makes a row per listed item, in sort order, each placed at its row")
{
    ControlScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    auto list = MCGuiTree::MakeRoot<MCListView>(0x4d, 0x30, 100, 100);
    std::vector<std::shared_ptr<TestListItem>> items = {
        std::make_shared<TestListItem>(TestListItem{3}), std::make_shared<TestListItem>(TestListItem{1}),
        std::make_shared<TestListItem>(TestListItem{2, false}), std::make_shared<TestListItem>(TestListItem{0})};
    ShowItems(*list, items);

    // Item 2 isn't listed; the rest come by key, a row height apart, as children of the list.
    CHECK((RowKeys(*list) == std::vector<int32_t>{0, 1, 3}));
    REQUIRE_EQ(list->NumberOfChildren(), 3);

    for (size_t index = 0; index < list->Rows().size(); ++index)
    {
        MCTest::Scope row(std::format("row {}", index));
        const std::shared_ptr<MCListRow> shown = list->Rows()[index].Lock();
        CHECK_EQ(shown->Y(), static_cast<int32_t>(index) * 0x10);
        CHECK(shown->IsShowing());
        CHECK(shown->ParentObject() == list.Pin());
    }

    // The content is as tall as the rows, or the smallest height: three rows fill 0x30, five need 0x50.
    CHECK_EQ(list->ContentPort->Height(), 0x30);
    items.push_back(std::make_shared<TestListItem>(TestListItem{4}));
    items.push_back(std::make_shared<TestListItem>(TestListItem{5}));
    list->Refresh(true);
    CHECK_EQ(list->ContentPort->Height(), 0x50);
    CHECK((RowKeys(*list) == std::vector<int32_t>{0, 1, 3, 4, 5}));

    // A row is found by its item.
    CHECK(list->RowOf<ListItemRow>(*items[1]) == list->Rows()[1].Lock());
    CHECK(list->RowOf<ListItemRow>(*items[2]) == nullptr);
}

TEST_CASE("game: a list view's refresh keeps the rows of items still there and destroys the rows of items gone")
{
    ControlScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    auto list = MCGuiTree::MakeRoot<MCListView>(0x4d, 0x30, 100, 100);
    std::vector<std::shared_ptr<TestListItem>> items = {std::make_shared<TestListItem>(TestListItem{0}),
                                                        std::make_shared<TestListItem>(TestListItem{1}),
                                                        std::make_shared<TestListItem>(TestListItem{2})};
    ShowItems(*list, items);
    const MCGuiHandle<ListItemRow> kept = list->RowOf<ListItemRow>(*items[2]);
    const MCGuiHandle<ListItemRow> gone = list->RowOf<ListItemRow>(*items[1]);
    REQUIRE(kept != nullptr && gone != nullptr);

    // Item 1 leaves, item 2 moves to the front, a new one comes in.
    items[2]->Key = -1;
    items.erase(items.begin() + 1);
    items.push_back(std::make_shared<TestListItem>(TestListItem{7}));
    list->Refresh(true);

    CHECK((RowKeys(*list) == std::vector<int32_t>{-1, 0, 7}));
    CHECK(gone == nullptr);
    REQUIRE(kept != nullptr);
    CHECK(list->Rows()[0].Lock() == kept.Lock());
    CHECK_EQ(kept->Y(), 0);
    CHECK_EQ(kept->Refreshes, 2);
    CHECK_EQ(list->NumberOfChildren(), 3);

    // An item filtered out loses its row the same way.
    items[0]->Listed = false;
    list->Refresh(true);
    CHECK((RowKeys(*list) == std::vector<int32_t>{-1, 7}));

    // Clear destroys every row.
    list->Clear();
    CHECK(kept == nullptr);
    CHECK_EQ(list->NumberOfChildren(), 0);
    CHECK(list->Rows().empty());
}

TEST_CASE("game: a list view draws every row from the source each frame; a row that takes no clicks is drawn, hidden")
{
    ControlScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    auto list = MCGuiTree::MakeRoot<MCListView>(0x4d, 0x30, 100, 100);
    std::vector<std::shared_ptr<TestListItem>> items = {std::make_shared<TestListItem>(TestListItem{0}),
                                                        std::make_shared<TestListItem>(TestListItem{1, true, false}),
                                                        std::make_shared<TestListItem>(TestListItem{2})};
    ShowItems(*list, items);

    // Every row is drawn at its place; the one that takes no clicks is hidden, so a click passes over it.
    using Drawn = std::vector<std::pair<int32_t, int32_t>>;
    CHECK((DrawList(*list) == Drawn{{0, 0}, {1, 0x10}, {2, 0x20}}));
    CHECK(list->Rows()[0]->IsShowing());
    CHECK(!list->Rows()[1]->IsShowing());

    // A change in the model shows in the next frame, before a refresh: the order is the source's now and an item gone
    // isn't drawn. An item with no row yet takes its place in the order, but nothing draws it.
    items[0]->Key = 5;
    items.erase(items.begin() + 1);
    items.push_back(std::make_shared<TestListItem>(TestListItem{3}));
    CHECK((DrawList(*list) == Drawn{{2, 0}, {5, 0x20}}));

    // Scrolled to the end, the last row is drawn and the rows above the view aren't (the percentage can leave the
    // offset a pixel short of a row, which shows a line of the row above).
    items.push_back(std::make_shared<TestListItem>(TestListItem{4}));
    list->Refresh(true);
    CHECK_EQ(list->ContentPort->Height(), 0x40);
    list->SetScrollPos(list->MaxScroll);
    const int32_t offset = list->GetScrollOffset();
    CHECK(offset == 0xf || offset == 0x10);
    const Drawn drawn = DrawList(*list);
    CHECK((drawn.back() == std::pair<int32_t, int32_t>{5, 0x30}));
    CHECK_EQ(drawn.size(), offset == 0x10 ? size_t{3} : size_t{4});
}

TEST_CASE("game: a list view keeps a dragged row whose item leaves, hidden, until a refresh after the drag")
{
    ControlScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    auto list = MCGuiTree::MakeRoot<MCListView>(0x4d, 0x30, 100, 100);
    std::vector<std::shared_ptr<TestListItem>> items = {std::make_shared<TestListItem>(TestListItem{0}),
                                                        std::make_shared<TestListItem>(TestListItem{1})};
    ShowItems(*list, items);
    const MCGuiHandle<ListItemRow> dragged = list->RowOf<ListItemRow>(*items[1]);
    REQUIRE(dragged != nullptr);
    screen.Gui->Grab(dragged);

    // Picked up, the item leaves the list (as a pilot dragged off the inventory moves to the assigned list): its
    // row lives on, hidden and out of the order, so the drag can end on it.
    const std::shared_ptr<TestListItem> carried = items[1];
    items.pop_back();
    list->Refresh(false);
    REQUIRE(dragged != nullptr);
    CHECK(!dragged->IsShowing());
    CHECK(list->IsRow(*dragged));
    CHECK((RowKeys(*list) == std::vector<int32_t>{0}));
    CHECK(list->RowOf<ListItemRow>(*carried) == dragged.Lock());

    // Dropped back: its item returns, and the same row is in the order again.
    items.push_back(carried);
    list->Refresh(false);
    CHECK((RowKeys(*list) == std::vector<int32_t>{0, 1}));
    CHECK(list->Rows()[1].Lock() == dragged.Lock());
    CHECK(dragged->IsShowing());

    // Taken away again and let go: the first refresh after the drag destroys it.
    items.pop_back();
    list->Refresh(false);
    REQUIRE(dragged != nullptr);
    screen.Gui->Release();
    list->Refresh(false);
    CHECK(dragged == nullptr);
    CHECK_EQ(list->NumberOfChildren(), 1);
}

TEST_CASE("game: a help tag sizes itself to its widest line and its line count")
{
    ControlScreen screen;

    if (!screen.LoadRetail())
    {
        return;
    }

    std::expected<std::unique_ptr<MCFont>, std::string> lineFont = MCFont::Create("font");
    REQUIRE(lineFont.has_value());
    screen.Gui->EngineFont = std::move(*lineFont);
    const std::shared_ptr<MCFont> font = LineFont();
    REQUIRE(font != nullptr);

    auto tag = MCGuiTree::MakeRoot<MCFloatHelp>(0, 0, 10, 10);
    tag->SetHelpText("Atlas AS7-D\nCaptain");
    font->Scale = 1.0f;
    font->Scaled = 0;
    CHECK_EQ(tag->Width(), font->PrintWidth("Atlas AS7-D\nCaptain", true) + 4);
    CHECK_EQ(tag->Height(), (font->FontHeight + 2) * 2);

    // A text longer than the original's 63 characters sizes the tag to all of it.
    const std::string longText(90, 'w');
    tag->SetHelpText(longText);
    CHECK_EQ(tag->Width(), font->PrintWidth(longText, true) + 4);
    CHECK_EQ(tag->Height(), font->FontHeight + 2);
}

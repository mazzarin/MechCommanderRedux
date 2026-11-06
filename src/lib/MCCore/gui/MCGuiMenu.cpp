#include "stdafx.h"
#include "gui/MCGuiMenu.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSystem.h"
#include "vfx/MCVfxFunctions.h"

auto MCGuiMenu::ShowGuiWindow(bool show) -> void
{
    ShowWindow = show;

    if (show)
    {
        Shown = true;
    }
}

auto MCGuiMenu::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) -> int32_t
{
    Font = nullptr;
    Items.clear();
    SelectedItem = -1;

    if (const int32_t result = MCGuiObject::Init(xPos, yPos, width, height, name); result != 0)
    {
        return result;
    }

    Font = WhiteFont;
    SetBackColor(0);
    MoveTo(xPos, yPos);
    ItemHeight = Font->Height() + 8;
    HasLetters = false;
    RightAligned = false;
    return 0;
}

auto MCGuiMenu::OnDestroy() -> void
{
    Items.clear();
    SelectedItem = -1;
    MCGuiObject::OnDestroy();
}

auto MCGuiMenu::HandleEvent(MCGuiEvent& event) -> void
{
    if (event.Type == MCGuiEventType::LeftButtonUp)
    {
        // A release runs the item under the cursor, then hides the menu wherever it happened.
        SelectedItem = (event.Y - GlobalY()) / ItemHeight;

        if (PointInside(event.X, event.Y) && IsItem(SelectedItem) && Items[SelectedItem].Callback != nullptr)
        {
            MCGuiCallback::Run(Items[SelectedItem].Callback);
        }

        ShowGuiWindow(false);
        GuiSystem().Release();
    }
    else if (event.Type == MCGuiEventType::MouseMove)
    {
        const int32_t item = (event.Y - GlobalY()) / ItemHeight;

        if (item != SelectedItem)
        {
            SelectedItem = item;
            Draw();
        }
    }

    MCGuiObject::HandleEvent(event);
}

auto MCGuiMenu::Draw() -> void
{
    MCPane& pane = DisplayPort->Frame();
    VfxPaneWipe(pane, BackgroundColor);
    MCGuiObject::Draw();

    if (Dragging())
    {
        return;
    }

    VfxLineDraw(pane, 0, 0, Width() - 1, 0, 0xf);
    VfxLineDraw(pane, Width() - 1, 0, Width() - 1, Height() - 1, 0xf);
    VfxLineDraw(pane, 0, Height() - 1, Width() - 1, Height() - 1, 0xf);
    VfxLineDraw(pane, 0, 0, 0, Height() - 1, 0xf);
    int32_t itemY = 0;

    for (int32_t i = 0; i < NumItems(); i++)
    {
        const Item& item = Items[i];

        if (item.Text == Separator)
        {
            const int32_t lineY = ItemHeight / 2 + itemY;
            VfxLineDraw(pane, 4, lineY, Width() - 8, lineY, 9);
        }
        else
        {
            if (i == SelectedItem)
            {
                FillBox(1, static_cast<int16_t>(itemY + 1), static_cast<int16_t>(Width() - 1),
                        static_cast<int16_t>(itemY + ItemHeight), 0xb);
            }

            const int32_t textX = RightAligned ? Width() + (-6 - Font->Width(item.Text)) : 2;
            Font->WriteString(pane, textX, itemY + 4, item.Text);

            if (item.Letter != 0)
            {
                const int32_t letterX = Width() + (-3 - Font->Width(static_cast<uint8_t>(item.Letter)));
                Font->WriteChar(pane, letterX, itemY + 4, item.Letter);
            }
        }

        VfxLineDraw(pane, 1, itemY, Width() - 2, itemY, 0xf);
        itemY += ItemHeight;
    }
}

auto MCGuiMenu::ResizeMenu() -> void
{
    const int32_t menuHeight = (Font->Height() + 8) * NumItems();
    int32_t menuWidth = 0;

    // Each item a quarter wider than its text.
    for (const Item& item : Items)
    {
        const double itemWidth = static_cast<double>(Font->Width(item.Text) + 4) * 1.25;

        if (static_cast<double>(menuWidth) < itemWidth)
        {
            menuWidth = static_cast<int32_t>(itemWidth);
        }
    }

    if (HasLetters)
    {
        menuWidth += 6 + Font->Width("W");
    }

    if (Width() == menuWidth && Height() == menuHeight)
    {
        return;
    }

    Resize(menuWidth, menuHeight);
}

auto MCGuiMenu::AddItem(std::string_view text) -> int32_t
{
    Items.push_back(Item{std::string(text), std::make_shared<MCGuiCallback>(), 0, 0});
    ResizeMenu();
    return NumItems() - 1;
}

auto MCGuiMenu::RemoveItem(std::string_view text) -> bool
{
    const auto found = std::ranges::find_if(Items, [text](const Item& item) { return MCIEquals(item.Text, text); });
    return RemoveItem(static_cast<int32_t>(found - Items.begin()));
}

auto MCGuiMenu::RemoveItem(int32_t index) -> bool
{
    if (!IsItem(index))
    {
        return false;
    }

    Items.erase(Items.begin() + index);
    ResizeMenu();
    return true;
}

auto MCGuiMenu::AddSeparator() -> int32_t
{
    Items.push_back(Item{std::string(Separator), nullptr, -1, 0});
    return NumItems();
}

auto MCGuiMenu::SetCallback(int32_t index, std::function<void()> exec) -> void
{
    if (IsItem(index) && Items[index].Callback != nullptr)
    {
        Items[index].Callback->SetExec(std::move(exec));
    }
}

auto MCGuiMenu::SetMessage(int32_t index, std::weak_ptr<MCGuiObject> target, int32_t message) -> void
{
    if (IsItem(index) && Items[index].Callback != nullptr)
    {
        Items[index].Callback->SetMessage(std::move(target), message);
    }
}

auto MCGuiMenu::ChangeItemString(int32_t index, std::string_view text) -> bool
{
    if (!IsItem(index))
    {
        return false;
    }

    Items[index].Text = text;
    ResizeMenu();
    return true;
}

auto MCGuiMenu::KeepOnScreen() -> void
{
    const tagRECT area = GuiSystem().ScrollRect;

    if (X() < area.left)
    {
        MoveTo(area.left, Y());
    }

    if (Y() < area.top)
    {
        MoveTo(X(), area.top);
    }

    if (area.right < X() + Width())
    {
        MoveTo(area.right - Width() - 10, Y());
    }

    if (area.bottom < Y() + Height())
    {
        MoveTo(X(), area.bottom - Height() - 10);
    }
}

auto MCGuiMenu::SetItemData(int32_t index, int32_t data) -> void
{
    if (IsItem(index))
    {
        Items[index].Data = data;
    }
}

auto MCGuiMenu::GetItemData(int32_t index) -> int32_t
{
    return IsItem(index) ? Items[index].Data : BadIndex;
}

auto MCGuiMenu::SetItemLetter(int32_t index, char letter) -> void
{
    if (IsItem(index))
    {
        Items[index].Letter = letter;
        HasLetters = true;
        ResizeMenu();
    }
}

auto MCGuiMenu::GetItemLetter(int32_t index) -> char
{
    return IsItem(index) ? Items[index].Letter : 3;
}

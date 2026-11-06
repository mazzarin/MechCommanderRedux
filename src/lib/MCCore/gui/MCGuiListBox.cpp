#include "stdafx.h"
#include "gui/MCGuiListBox.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiScrollBar.h"
#include "gui/MCGuiSystem.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The event type that shares the key handling with <see cref="MCGuiEventType::KeyDown"/> (the original's jump table).</summary>
    constexpr int32_t KeyEventAlias = 5;
    /// <summary>The event type that shares the scroll bar's message (the original's jump table).</summary>
    constexpr int32_t ScrollEventAlias = 2;

    /// <summary>The keys the list box takes (virtual-key codes).</summary>
    constexpr uint8_t KeyPageUp = 0x21;
    constexpr uint8_t KeyPageDown = 0x22;
    constexpr uint8_t KeyEnd = 0x23;
    constexpr uint8_t KeyHome = 0x24;
    constexpr uint8_t KeyUp = 0x26;
    constexpr uint8_t KeyDown = 0x28;
}

MCGuiListBox::~MCGuiListBox() = default;

auto MCGuiListBox::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) -> int32_t
{
    if (const int32_t result = MCGuiObject::Init(xPos, yPos, width, height, name); result != 0)
    {
        return result;
    }

    ItemFont = GreyFont;
    ItemHeight = ItemFont->Height() + 8;

    if (const int32_t partLine = height % ItemHeight; partLine != 0)
    {
        Resize(width, (ItemHeight - partLine) + height);
    }

    Items.clear();
    SetBackColor(0);
    return 0;
}

auto MCGuiListBox::OnDestroy() -> void
{
    Items.clear();
    SelectedItem = -1;
    MCGuiObject::OnDestroy();
}

auto MCGuiListBox::VisibleItems() const -> int32_t
{
    return WinHeight / ItemHeight;
}

auto MCGuiListBox::SetBarPosition(int32_t position) const -> void
{
    if (ScrollBar != nullptr)
    {
        MCGuiEvent barEvent;
        barEvent.Clear();
        barEvent.Type = MCGuiScrollMessage::SetPositionQuietly;
        barEvent.LParam = position;
        ScrollBar->HandleEvent(barEvent);
    }
}

auto MCGuiListBox::Choose(int32_t item) -> void
{
    SelectedItem = item;
    HighlightedItem = item;
}

auto MCGuiListBox::HandleEvent(MCGuiEvent& event) -> void
{
    const int32_t visibleItems = VisibleItems();

    // Tells an active scroll bar to step a line, as its arrows do.
    auto stepBar = [this](int32_t message)
    {
        if (ScrollBar != nullptr)
        {
            APostMessage(*ScrollBar, message);
        }
    };

    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            GuiSystem().SetText(weak_from_this());
            GuiSystem().Grab(weak_from_this());
            Choose((event.Y - GlobalY()) / ItemHeight + TopItem);
            break;
        }
        case MCGuiEventType::LeftButtonUp:
            GuiSystem().Release();
            break;
        case MCGuiEventType::MouseMove:
        {
            if (!GuiSystem().Grabs(*this))
            {
                break;
            }

            if (event.Y < GlobalY())
            {
                // Dragged above the box: scroll up a line.
                if (TopItem > 0)
                {
                    TopItem--;
                    stepBar(MCGuiScrollMessage::LineUp);
                    Choose(TopItem);
                }
            }
            else if (event.Y > GlobalY() + Height())
            {
                // Dragged below the box: scroll down a line.
                if (TopItem + visibleItems < NumItems())
                {
                    TopItem++;
                    stepBar(MCGuiScrollMessage::LineDown);
                    Choose(TopItem + visibleItems - 1);
                }
            }
            else
            {
                Choose((event.Y - GlobalY()) / ItemHeight + TopItem);
            }

            break;
        }
        case KeyEventAlias:
        case MCGuiEventType::KeyDown:
        {
            switch (event.Key)
            {
                case KeyPageUp:
                {
                    TopItem = std::max(TopItem - visibleItems, 0);
                    SetBarPosition(TopItem);

                    if (SelectedItem > 0)
                    {
                        Choose(std::max(SelectedItem - visibleItems, 0));
                    }

                    break;
                }
                case KeyPageDown:
                {
                    TopItem += visibleItems;

                    if (TopItem + visibleItems > NumItems())
                    {
                        TopItem = NumItems() - visibleItems;
                    }

                    SetBarPosition(TopItem);

                    if (SelectedItem < NumItems() && SelectedItem != -1)
                    {
                        Choose(std::min(SelectedItem + visibleItems, NumItems() - 1));
                    }

                    break;
                }
                case KeyEnd:
                {
                    TopItem = NumItems() - visibleItems;
                    SetBarPosition(TopItem);
                    Choose(NumItems() - 1);
                    break;
                }
                case KeyHome:
                {
                    TopItem = 0;
                    SetBarPosition(0);
                    Choose(0);
                    break;
                }
                case KeyUp:
                {
                    if (HighlightedItem < 1)
                    {
                        break;
                    }

                    SelectedItem--;

                    if (SelectedItem < TopItem)
                    {
                        TopItem = SelectedItem;
                        stepBar(MCGuiScrollMessage::LineUp);
                    }

                    HighlightedItem = SelectedItem;
                    break;
                }
                case KeyDown:
                {
                    if (HighlightedItem >= NumItems() - 1 || HighlightedItem == -1)
                    {
                        break;
                    }

                    SelectedItem++;

                    if (SelectedItem >= TopItem + visibleItems)
                    {
                        TopItem++;
                        stepBar(MCGuiScrollMessage::LineDown);
                    }

                    HighlightedItem = SelectedItem;
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case ScrollEventAlias:
        case MCGuiScrollMessage::Changed:
        {
            // The scroll bar moved.
            const int32_t position = ScrollBar != nullptr ? ScrollBar->ScrollPos : 0;
            TopItem = std::min(position, NumItems() - visibleItems);
            break;
        }
        default:
            break;
    }

    MCGuiObject::HandleEvent(event);
}

auto MCGuiListBox::Draw() -> void
{
    MCPane& pane = DisplayPort->Frame();
    VfxPaneWipe(pane, BackgroundColor);
    MCGuiObject::Draw();
    VfxLineDraw(pane, 0, 0, Width() - 1, 0, 0xf);
    VfxLineDraw(pane, Width() - 1, 0, Width() - 1, Height() - 1, 0xf);
    VfxLineDraw(pane, 0, Height() - 1, Width() - 1, Height() - 1, 0xf);
    VfxLineDraw(pane, 0, 0, 0, Height() - 1, 0xf);

    // The whole lines that fit, from the top one.
    for (int32_t item = TopItem, lineY = 0; item < NumItems() && lineY + ItemHeight <= Height();
         item++, lineY += ItemHeight)
    {
        if (SelectedItem == item)
        {
            // The selected line's inside is filled with colour 0xb. (The original wrote the port's pixels directly;
            // the box draws itself now, so it wipes the same rectangle.)
            MCPane line = pane;
            line.X0 = 1;
            line.Y0 = lineY + 1;
            line.X1 = Width() - 2;
            line.Y1 = lineY + ItemHeight - 1;
            VfxPaneWipe(line, 0xb);
        }

        ItemFont->WriteString(pane, 2, lineY + 4, Items[static_cast<size_t>(item)]);
        VfxLineDraw(pane, 1, lineY, Width() - 2, lineY, 0xf);
    }
}

auto MCGuiListBox::UpdateScrollRange() const -> void
{
    if (const int32_t visibleItems = VisibleItems(); visibleItems < NumItems())
    {
        ScrollBar->SetScrollMax(NumItems() - visibleItems);
        ScrollBar->SetScrollPos(TopItem);
    }
}

auto MCGuiListBox::AddItem(std::string_view text) -> void
{
    Items.emplace_back(text);

    if (ScrollBar == nullptr)
    {
        // No scroll bar: the box grows to show every line.
        Resize(Width(), (ItemFont->Height() + 8) * NumItems());
    }
    else
    {
        UpdateScrollRange();
    }
}

auto MCGuiListBox::ChangeItemString(int32_t item, std::string_view text) -> bool
{
    if (item < 0 || item >= NumItems())
    {
        return false;
    }

    Items[static_cast<size_t>(item)] = text;
    return true;
}

auto MCGuiListBox::SelectItem(int32_t item) -> bool
{
    if (item >= NumItems())
    {
        return false;
    }

    SelectedItem = item;
    return true;
}

auto MCGuiListBox::ActivateScrollbar() -> int32_t
{
    if (ScrollBar != nullptr)
    {
        return 0;
    }

    auto bar = TryCreateChild<MCGuiScrollBar>([&](MCGuiScrollBar& made) { return made.Init(10, 10, 0, Height(), {}); });

    if (!bar)
    {
        return bar.error();
    }

    ScrollBar = *bar;
    ScrollBar->MoveTo(Width() + 1, 0);

    if (VisibleItems() < NumItems())
    {
        UpdateScrollRange();
        return 0;
    }

    ScrollBar->ShowGuiWindow(false);
    return 0;
}

auto MCGuiListBox::GetItemString(int32_t item) const -> std::optional<std::string>
{
    return item >= 0 && item < NumItems() ? std::optional(Items[static_cast<size_t>(item)]) : std::nullopt;
}

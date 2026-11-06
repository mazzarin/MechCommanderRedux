#include "stdafx.h"
#include "gui/MCGuiButtonBar.h"

template <typename TButton>
auto MCGuiButtonLayout::LayOut(MCGuiObject& bar, const std::vector<MCGuiHandle<TButton>>& buttons) const -> void
{
    const auto numButtons = static_cast<int32_t>(buttons.size());
    const int32_t across = std::min(numButtons, MaxLength);
    const int32_t lines = (numButtons + MaxLength - 1) / MaxLength;
    const int32_t columns = Horizontal ? across : lines;
    const int32_t rows = Horizontal ? lines : across;
    bar.Resize(columns * ButtonWidth, rows * ButtonHeight);

    for (int32_t i = 0; i < numButtons; i++)
    {
        const int32_t line = i / MaxLength;
        const int32_t step = i % MaxLength;
        const int32_t column = Horizontal ? step : line;
        const int32_t row = Horizontal ? line : step;
        buttons[i]->MoveTo(column * ButtonWidth, row * ButtonHeight);
    }
}

// MCGuiToolBar

auto MCGuiToolBar::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) -> int32_t
{
    const int32_t result = MCGuiTitleWindow::Init(xPos, yPos, width, height, name);
    SetBackColor(0);

    if (TitleBar != nullptr)
    {
        TitleBar->ShowCloseButton(false);
    }

    MaxLength = 8;
    return result;
}

auto MCGuiToolBar::RemoveButton(int32_t index) -> bool
{
    if (index < 0 || index >= NumButtons())
    {
        return false;
    }

    // Taken off the bar, which destroys it.
    const MCGuiHandle<MCGuiToolButton> button = Buttons[static_cast<size_t>(index)];
    Buttons.erase(Buttons.begin() + index);

    if (const std::shared_ptr<MCGuiToolButton> removed = button.Lock(); removed != nullptr)
    {
        RemoveChild(*removed);
    }

    PlaceButtons();
    return true;
}

auto MCGuiToolBar::GetButton(int32_t index) -> std::shared_ptr<MCGuiToolButton>
{
    return index >= 0 && index < NumButtons() ? Buttons[static_cast<size_t>(index)].Lock() : nullptr;
}

auto MCGuiToolBar::IsPushed(int32_t index) -> bool
{
    const std::shared_ptr<MCGuiToolButton> button = GetButton(index);
    return button != nullptr && button->Pushed;
}

auto MCGuiToolBar::SetHorizontal(bool on) -> void
{
    Horizontal = on;
    PlaceButtons();
}

auto MCGuiToolBar::SetMaxLength(int32_t length) -> void
{
    MaxLength = length;
    PlaceButtons();
}

auto MCGuiToolBar::PlaceButtons() -> void
{
    LayOut(*this, Buttons);
}

auto MCGuiToolBar::SetButtonSize(int32_t width, int32_t height) -> void
{
    ButtonWidth = width;
    ButtonHeight = height;
}

// MCGuiWindowBar

auto MCGuiWindowBar::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) -> int32_t
{
    const int32_t result = MCGuiObject::Init(xPos, yPos, width, height, name);
    SetBackColor(0);
    MaxLength = 8;
    return result;
}

auto MCGuiWindowBar::RemoveButton(int32_t index) -> bool
{
    if (index < 0 || index >= NumButtons())
    {
        return false;
    }

    // Taken off the bar, which destroys it.
    const MCGuiHandle<MCGuiButton> button = Buttons[static_cast<size_t>(index)];
    Buttons.erase(Buttons.begin() + index);

    if (const std::shared_ptr<MCGuiButton> removed = button.Lock(); removed != nullptr)
    {
        RemoveChild(*removed);
    }

    PlaceButtons();
    return true;
}

auto MCGuiWindowBar::GetButton(int32_t index) -> std::shared_ptr<MCGuiButton>
{
    return index >= 0 && index < NumButtons() ? Buttons[static_cast<size_t>(index)].Lock() : nullptr;
}

auto MCGuiWindowBar::SetHorizontal(bool on) -> void
{
    Horizontal = on;
    PlaceButtons();
}

auto MCGuiWindowBar::SetMaxLength(int32_t length) -> void
{
    MaxLength = length;
    PlaceButtons();
}

auto MCGuiWindowBar::PlaceButtons() -> void
{
    LayOut(*this, Buttons);
}

auto MCGuiWindowBar::SetButtonSize(int32_t width, int32_t height) -> void
{
    ButtonWidth = width;
    ButtonHeight = height;
}

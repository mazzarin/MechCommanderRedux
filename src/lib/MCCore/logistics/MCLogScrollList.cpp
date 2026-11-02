#include "stdafx.h"
#include "logistics/MCLogScrollList.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogLabel.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The list's fill and outline.</summary>
    constexpr uint8_t FillColor = 0x10;
    constexpr uint8_t OutlineColor = 0x13;
    /// <summary>The scroll bar's track and thumb.</summary>
    constexpr uint8_t TrackColor = 0x12;
    constexpr uint8_t ThumbColor = 0x17;

    /// <summary>The thumb's least height.</summary>
    constexpr int32_t MinThumbHeight = 8;

    /// <summary>The line under the column titles, and the status line's text's offset under its line.</summary>
    constexpr int32_t TitleLineY = 14;
    constexpr int32_t StatusTextOffset = 5;
}

MCLogScrollList::~MCLogScrollList()
{
    MCLogScrollList::Destroy();
}

auto MCLogScrollList::Destroy() -> void
{
    ReleaseThumb();
    MCLogObject::Destroy();
}

auto MCLogScrollList::VisibleRows() -> int32_t
{
    return std::max((Height() - RowsTop - StatusHeight) / _RowHeight, 1);
}

auto MCLogScrollList::TrackHeight() -> int32_t
{
    return VisibleRows() * _RowHeight;
}

auto MCLogScrollList::Thumb() -> std::pair<int32_t, int32_t>
{
    const int32_t track = TrackHeight();
    const int32_t height = std::clamp(track * VisibleRows() / std::max(RowCount(), 1), MinThumbHeight, track);
    const int32_t scrollable = std::max(RowCount() - VisibleRows(), 1);
    return {RowsTop + (track - height) * _FirstRow / scrollable, height};
}

auto MCLogScrollList::RowForThumb(int32_t thumbTop) -> int32_t
{
    const auto [top, height] = Thumb();
    const int32_t travel = std::max(TrackHeight() - height, 1);
    const int32_t scrollable = RowCount() - VisibleRows();
    return (thumbTop - RowsTop) * scrollable / travel;
}

auto MCLogScrollList::RowAt(int32_t yPos) -> std::optional<int32_t>
{
    const int32_t localY = yPos - GlobalY() - RowsTop;

    if (localY < 0)
    {
        return std::nullopt;
    }

    const int32_t shown = localY / _RowHeight;

    if (shown >= VisibleRows() || _FirstRow + shown >= RowCount())
    {
        return std::nullopt;
    }

    return _FirstRow + shown;
}

auto MCLogScrollList::RowTop(int32_t row) -> std::optional<int32_t>
{
    const int32_t shown = row - _FirstRow;

    if (shown < 0 || shown >= VisibleRows())
    {
        return std::nullopt;
    }

    return RowsTop + shown * _RowHeight;
}

auto MCLogScrollList::OnScrollBar(int32_t xPos, int32_t yPos) -> bool
{
    const int32_t localX = xPos - GlobalX();
    const int32_t localY = yPos - GlobalY();
    return localX >= ScrollBarLeft() && localX < Width() - 1 && localY >= RowsTop && localY < RowsTop + TrackHeight();
}

auto MCLogScrollList::ScrollTo(int32_t row) -> void
{
    _FirstRow = std::clamp(row, 0, std::max(RowCount() - VisibleRows(), 0));
}

auto MCLogScrollList::PressScrollBar(int32_t yPos) -> void
{
    const int32_t localY = yPos - GlobalY();
    const auto [top, height] = Thumb();

    if (localY >= top && localY < top + height)
    {
        _ThumbGrab = localY - top;
        GuiSystem().Grab(weak_from_this());
        return;
    }

    ScrollTo(_FirstRow + (localY < top ? -VisibleRows() : VisibleRows()));
}

auto MCLogScrollList::ReleaseThumb() -> void
{
    _ThumbGrab.reset();

    if (GuiSystem().Grabs(*this))
    {
        GuiSystem().Release();
    }
}

auto MCLogScrollList::ResetScroll() -> void
{
    ReleaseThumb();
    _FirstRow = 0;
}

auto MCLogScrollList::HandleScrollMouse(const MCGuiEvent& event) -> bool
{
    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            if (!OnScrollBar(event.X, event.Y))
            {
                return false;
            }

            PressScrollBar(event.Y);
            return true;
        }
        case MCGuiEventType::LeftButtonUp:
        {
            if (!_ThumbGrab.has_value())
            {
                return false;
            }

            ReleaseThumb();
            return true;
        }
        case MCGuiEventType::MouseMove:
        {
            if (!_ThumbGrab.has_value())
            {
                return false;
            }

            ScrollTo(RowForThumb(event.Y - GlobalY() - *_ThumbGrab));
            return true;
        }
        default:
            return false;
    }
}

auto MCLogScrollList::PassToScreen(MCGuiEvent& event) -> void
{
    for (std::shared_ptr<MCGuiObject> parent = ParentObject(); parent != nullptr; parent = parent->ParentObject())
    {
        if (dynamic_cast<MCGenericScreen*>(parent.get()) != nullptr)
        {
            parent->HandleEvent(event);
            return;
        }
    }
}

auto MCLogScrollList::ScrollByKey(int32_t key) -> bool
{
    if (key == VK_UP || key == VK_DOWN)
    {
        ScrollTo(_FirstRow + (key == VK_UP ? -1 : 1));
        return true;
    }

    if (key == VK_PRIOR || key == VK_NEXT)
    {
        ScrollTo(_FirstRow + (key == VK_PRIOR ? -VisibleRows() : VisibleRows()));
        return true;
    }

    return false;
}

auto MCLogScrollList::MouseWheel(int32_t steps, [[maybe_unused]] int32_t xPos, [[maybe_unused]] int32_t yPos) -> bool
{
    ScrollTo(_FirstRow + steps);
    return true;
}

auto MCLogScrollList::Write(int32_t xPos, int32_t yPos, std::string_view text, uint8_t color, int32_t maxWidth)
    -> int32_t
{
    const MCGuiFont& font = *WhiteFont;
    const auto fits = static_cast<size_t>(std::max(font.CharactersToWidth(text, maxWidth, false), 0));
    const std::string_view shown = text.substr(0, fits);
    VfxStringDraw(_Port->Frame(), xPos, yPos, font.FontData.Bytes(), shown, FontColors(color));
    return font.Width(shown);
}

auto MCLogScrollList::DrawOutline(int32_t left, int32_t top, int32_t right, int32_t bottom, uint8_t color) -> void
{
    const auto x0 = static_cast<int16_t>(left);
    const auto y0 = static_cast<int16_t>(top);
    const auto x1 = static_cast<int16_t>(right);
    const auto y1 = static_cast<int16_t>(bottom);
    FillBox(x0, y0, x1, y0, color);
    FillBox(x0, y1, x1, y1, color);
    FillBox(x0, y0, x0, y1, color);
    FillBox(x1, y0, x1, y1, color);
}

auto MCLogScrollList::DrawFrame(std::string_view status, bool news) -> void
{
    const int32_t right = Width() - 1;
    const int32_t bottom = Height() - 1;
    FillBox(0, 0, static_cast<int16_t>(right), static_cast<int16_t>(bottom), FillColor);
    DrawOutline(0, 0, right, bottom, OutlineColor);
    FillBox(1, TitleLineY, static_cast<int16_t>(right - 1), TitleLineY, LineColor);

    // The scroll bar: the track, and the thumb showing where the rows shown are.
    const auto trackLeft = static_cast<int16_t>(ScrollBarLeft());
    const auto trackRight = static_cast<int16_t>(Width() - 3);
    const auto trackTop = static_cast<int16_t>(RowsTop);
    const auto trackBottom = static_cast<int16_t>(RowsTop + TrackHeight() - 1);
    FillBox(trackLeft, trackTop, trackRight, trackBottom, TrackColor);
    const auto [thumbTop, thumbHeight] = Thumb();
    FillBox(trackLeft, static_cast<int16_t>(thumbTop), trackRight, static_cast<int16_t>(thumbTop + thumbHeight - 1),
            ThumbColor);

    // The status line.
    const auto statusTop = static_cast<int16_t>(Height() - StatusHeight);
    FillBox(1, statusTop, static_cast<int16_t>(right - 1), statusTop, LineColor);
    Write(HeadingLeft, statusTop + StatusTextOffset, status, news ? NewsColor : HintColor, Width() - 2 * HeadingLeft);
}

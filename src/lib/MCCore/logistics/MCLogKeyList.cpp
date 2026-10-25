#include "stdafx.h"
#include "logistics/MCLogKeyList.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "lib/MCFatal.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogLabel.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    using MCKeyBindings::MCControlRow;

    /// <summary>The list's colours: its fill and outline, the lines under the titles and around the keys.</summary>
    constexpr uint8_t FillColor = 0x10;
    constexpr uint8_t OutlineColor = 0x13;
    constexpr uint8_t LineColor = 0x14;
    /// <summary>The text's colours: headings (amber), commands (the labels' colour), column titles (grey).</summary>
    constexpr uint8_t HeadingColor = 0x31;
    constexpr uint8_t CommandColor = 0xe3;
    constexpr uint8_t TitleColor = 0x19;
    /// <summary>A key's name: the original's key, and a key the player changed (amber).</summary>
    constexpr uint8_t KeyColor = 0x1d;
    constexpr uint8_t ChangedKeyColor = 0x33;
    /// <summary>A key cell's outline under the mouse, while listening (with its fill), and when it lost its key.</summary>
    constexpr uint8_t HoverLineColor = 0xe3;
    constexpr uint8_t ListenLineColor = 0x33;
    constexpr uint8_t ListenFillColor = 0x12;
    constexpr uint8_t LostLineColor = 0xef;
    /// <summary>The status line: a hint, and news of a key that moved (amber).</summary>
    constexpr uint8_t HintColor = 0xe3;
    constexpr uint8_t NewsColor = 0x33;
    /// <summary>The scroll bar's track and thumb.</summary>
    constexpr uint8_t TrackColor = 0x12;
    constexpr uint8_t ThumbColor = 0x17;

    /// <summary>
    /// Places in the list's pixels: the column titles, the line under them, the first row; the text's offset in a row;
    /// the status line's height (its line included) and its text's offset.
    /// </summary>
    constexpr int32_t TitleTop = 4;
    constexpr int32_t TitleLineY = 14;
    constexpr int32_t RowsTop = 16;
    constexpr int32_t TextOffset = 2;
    constexpr int32_t StatusHeight = 16;
    constexpr int32_t StatusTextOffset = 5;
    /// <summary>The left edges of the headings and the commands, and the text's offset in a key cell.</summary>
    constexpr int32_t HeadingLeft = 6;
    constexpr int32_t CommandLeft = 14;
    constexpr int32_t CellTextLeft = 4;
    /// <summary>A key cell's width, the gap after it, and the scroll bar's width.</summary>
    constexpr int32_t CellWidth = 128;
    constexpr int32_t CellGap = 8;
    constexpr int32_t ScrollBarWidth = 9;
    /// <summary>The thumb's least height.</summary>
    constexpr int32_t MinThumbHeight = 8;

    /// <summary>The column titles.</summary>
    constexpr std::string_view CommandTitle = "COMMAND";
    constexpr std::array<std::string_view, 2> KeyTitles = {"KEY", "ALTERNATE"};

    /// <summary>The status line's hints.</summary>
    constexpr std::string_view Hint = "CLICK A KEY TO CHANGE IT. RIGHT-CLICK A KEY TO CLEAR IT.";
    constexpr std::string_view DefaultsNews = "THE ORIGINAL KEYS ARE BACK.";

    /// <summary>The scan code of Escape, which stops listening (it is the game's own key, not one to bind).</summary>
    constexpr int16_t EscapeScanCode = 0x01;

    /// <summary>The samples played: listening starts, a key is bound or cleared.</summary>
    constexpr uint32_t ListenSound = 16;
    constexpr uint32_t BoundSound = 15;

    /// <summary>The rows.</summary>
    std::span<const MCControlRow> Rows()
    {
        return MCKeyBindings::ControlRows();
    }

    /// <summary>The number of rows.</summary>
    int32_t RowCount()
    {
        return static_cast<int32_t>(Rows().size());
    }

    /// <summary>Passes <paramref name="event"/> to the screen <paramref name="object"/> is on (Escape leaves it).</summary>
    void PassToScreen(MCGuiObject& object, MCGuiEvent& event)
    {
        for (std::shared_ptr<MCGuiObject> parent = object.ParentObject(); parent != nullptr;
             parent = parent->ParentObject())
        {
            if (dynamic_cast<MCGenericScreen*>(parent.get()) != nullptr)
            {
                parent->HandleEvent(event);
                return;
            }
        }
    }
}

MCLogKeyList::~MCLogKeyList()
{
    MCLogKeyList::Destroy();
}

auto MCLogKeyList::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, MCKeyBindings::MCKeyTable& keys)
    -> void
{
    MCLogObject::Init(xPos, yPos, width, height);
    _Keys = keys;
    Reset();
}

auto MCLogKeyList::Destroy() -> void
{
    if (GuiSystem().HasText(*this))
    {
        GuiSystem().ReleaseText();
    }

    if (GuiSystem().Grabs(*this))
    {
        GuiSystem().Release();
    }

    _Listening.reset();
    _ThumbGrab.reset();
    MCLogObject::Destroy();
}

auto MCLogKeyList::Keys() const -> MCKeyBindings::MCKeyTable&
{
    if (!_Keys.has_value())
    {
        FatalMsg("MCLogKeyList: used before Init");
    }

    return _Keys->get();
}

auto MCLogKeyList::Reset() -> void
{
    StopListening();
    _FirstRow = 0;
    _Hovered.reset();
    _Lost.reset();
    _Status = Hint;
}

auto MCLogKeyList::RestoreDefaults() -> void
{
    StopListening();
    Keys() = MCKeyBindings::Defaults();
    _Lost.reset();
    _Status = DefaultsNews;
}

auto MCLogKeyList::VisibleRows() -> int32_t
{
    return std::max((Height() - RowsTop - StatusHeight) / RowHeight, 1);
}

auto MCLogKeyList::KeyLeft(int32_t column) -> int32_t
{
    const int32_t scrollLeft = Width() - 1 - ScrollBarWidth;
    return scrollLeft - (2 - column) * (CellWidth + CellGap);
}

auto MCLogKeyList::TrackTop() const -> int32_t
{
    return RowsTop;
}

auto MCLogKeyList::TrackHeight() -> int32_t
{
    return VisibleRows() * RowHeight;
}

auto MCLogKeyList::Thumb() -> std::pair<int32_t, int32_t>
{
    const int32_t track = TrackHeight();
    const int32_t height = std::clamp(track * VisibleRows() / RowCount(), MinThumbHeight, track);
    const int32_t scrollable = std::max(RowCount() - VisibleRows(), 1);
    return {TrackTop() + (track - height) * _FirstRow / scrollable, height};
}

auto MCLogKeyList::RowForThumb(int32_t thumbTop) -> int32_t
{
    const auto [top, height] = Thumb();
    const int32_t travel = std::max(TrackHeight() - height, 1);
    const int32_t scrollable = RowCount() - VisibleRows();
    return (thumbTop - TrackTop()) * scrollable / travel;
}

auto MCLogKeyList::CellCommand(Cell cell) -> MCKeyCommand
{
    if (cell.Row < 0 || cell.Row >= RowCount())
    {
        return MCKeyCommand::Unused;
    }

    const MCControlRow& row = Rows()[static_cast<size_t>(cell.Row)];
    return cell.Column == 0 ? row.Command : row.Alternate;
}

auto MCLogKeyList::CellOf(MCKeyCommand command) -> std::optional<Cell>
{
    for (int32_t row = 0; row < RowCount(); row++)
    {
        for (int32_t column = 0; column < 2; column++)
        {
            if (CellCommand({row, column}) == command && command != MCKeyCommand::Unused)
            {
                return Cell{row, column};
            }
        }
    }

    return std::nullopt;
}

auto MCLogKeyList::RowAt(int32_t yPos) -> std::optional<int32_t>
{
    const int32_t localY = yPos - GlobalY() - RowsTop;

    if (localY < 0)
    {
        return std::nullopt;
    }

    const int32_t shown = localY / RowHeight;

    if (shown >= VisibleRows() || _FirstRow + shown >= RowCount())
    {
        return std::nullopt;
    }

    return _FirstRow + shown;
}

auto MCLogKeyList::CellAt(int32_t xPos, int32_t yPos) -> std::optional<Cell>
{
    const std::optional<int32_t> row = RowAt(yPos);

    if (!row.has_value())
    {
        return std::nullopt;
    }

    const int32_t localX = xPos - GlobalX();

    for (int32_t column = 0; column < 2; column++)
    {
        const int32_t left = KeyLeft(column);
        const Cell cell{*row, column};

        if (localX >= left && localX < left + CellWidth && CellCommand(cell) != MCKeyCommand::Unused)
        {
            return cell;
        }
    }

    return std::nullopt;
}

auto MCLogKeyList::CellCentre(Cell cell) -> std::optional<std::pair<int32_t, int32_t>>
{
    const int32_t shown = cell.Row - _FirstRow;

    if (shown < 0 || shown >= VisibleRows())
    {
        return std::nullopt;
    }

    return std::pair{GlobalX() + KeyLeft(cell.Column) + CellWidth / 2,
                     GlobalY() + RowsTop + shown * RowHeight + RowHeight / 2};
}

auto MCLogKeyList::OnScrollBar(int32_t xPos, int32_t yPos) -> bool
{
    const int32_t localX = xPos - GlobalX();
    const int32_t localY = yPos - GlobalY();
    const int32_t left = Width() - 1 - ScrollBarWidth;
    return localX >= left && localX < Width() - 1 && localY >= TrackTop() && localY < TrackTop() + TrackHeight();
}

auto MCLogKeyList::ScrollTo(int32_t row) -> void
{
    _FirstRow = std::clamp(row, 0, std::max(RowCount() - VisibleRows(), 0));
}

auto MCLogKeyList::PressScrollBar(int32_t yPos) -> void
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

auto MCLogKeyList::Listen(Cell cell) -> void
{
    const MCKeyCommand command = CellCommand(cell);

    if (command == MCKeyCommand::Unused)
    {
        return;
    }

    _Listening = cell;
    _PendingModifier.reset();
    _Lost.reset();
    _Status = std::format("PRESS THE NEW KEY FOR {}. ESC CANCELS.", MCKeyBindings::CommandLabel(command));

    if (!GuiSystem().HasText(*this))
    {
        GuiSystem().SetText(weak_from_this());
    }

    PlayLogSound(ListenSound);
}

auto MCLogKeyList::StopListening() -> void
{
    const bool listened = _Listening.has_value();
    _Listening.reset();
    _PendingModifier.reset();

    if (GuiSystem().HasText(*this))
    {
        GuiSystem().ReleaseText();
    }

    if (listened)
    {
        _Status = Hint;
    }
}

auto MCLogKeyList::Assign(uint32_t key) -> void
{
    if (!_Listening.has_value())
    {
        return;
    }

    const MCKeyCommand command = CellCommand(*_Listening);
    StopListening();
    const MCKeyCommand lost = MCKeyBindings::Bind(Keys(), command, key);
    PlayLogSound(BoundSound);

    if (lost == MCKeyCommand::Unused)
    {
        return;
    }

    _Lost = CellOf(lost);
    _Status = std::format("{} MOVED FROM {}.", MCKeyBindings::KeyLabel(key), MCKeyBindings::CommandLabel(lost));
}

auto MCLogKeyList::Clear(Cell cell) -> void
{
    const MCKeyCommand command = CellCommand(cell);

    if (command == MCKeyCommand::Unused)
    {
        return;
    }

    StopListening();
    Keys()[static_cast<size_t>(command)] = MCKeyBindings::NoKey;
    _Lost.reset();
    _Status = Hint;
    PlayLogSound(BoundSound);
}

auto MCLogKeyList::HandleEvent(MCGuiEvent& event) -> void
{
    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            const std::optional<Cell> cell = CellAt(event.X, event.Y);

            if (OnScrollBar(event.X, event.Y))
            {
                PressScrollBar(event.Y);
            }
            else if (cell.has_value() && cell != _Listening)
            {
                Listen(*cell);
            }
            else
            {
                StopListening();
            }
            break;
        }
        case MCGuiEventType::RightButtonDown:
        {
            if (const std::optional<Cell> cell = CellAt(event.X, event.Y); cell.has_value())
            {
                Clear(*cell);
            }
            break;
        }
        case MCGuiEventType::LeftButtonUp:
        {
            if (_ThumbGrab.has_value())
            {
                _ThumbGrab.reset();

                if (GuiSystem().Grabs(*this))
                {
                    GuiSystem().Release();
                }
            }
            break;
        }
        case MCGuiEventType::MouseMove:
        {
            if (_ThumbGrab.has_value())
            {
                ScrollTo(RowForThumb(event.Y - GlobalY() - *_ThumbGrab));
            }
            else
            {
                _Hovered = CellAt(event.X, event.Y);
            }
            break;
        }
        case MCGuiEventType::KeyDown:
        {
            if (_Listening.has_value())
            {
                if (event.ScanCode == EscapeScanCode)
                {
                    StopListening();
                }
                else if (MCKeyBindings::IsModifierKey(event.ScanCode))
                {
                    _PendingModifier = event.ScanCode;
                }
                else
                {
                    Assign(MCKeyBindings::FromKeyEvent(event.ScanCode, event.ShiftKey != 0, event.CtrlKey != 0,
                                                       event.AltKey != 0));
                }
            }
            else if (event.Key == VK_UP || event.Key == VK_DOWN)
            {
                ScrollTo(_FirstRow + (event.Key == VK_UP ? -1 : 1));
            }
            else if (event.Key == VK_PRIOR || event.Key == VK_NEXT)
            {
                ScrollTo(_FirstRow + (event.Key == VK_PRIOR ? -VisibleRows() : VisibleRows()));
            }
            else
            {
                PassToScreen(*this, event);
            }
            break;
        }
        case MCGuiEventType::KeyUp:
        {
            // A modifier let go with no other key since it went down binds alone.
            if (_Listening.has_value() && _PendingModifier == event.ScanCode)
            {
                Assign(MCKeyBindings::FromKeyEvent(event.ScanCode, event.ShiftKey != 0, event.CtrlKey != 0,
                                                   event.AltKey != 0));
            }
            break;
        }
        case MCGuiEventType::Focus:
        {
            // The keyboard went elsewhere (a click outside the list): listening stops.
            if (event.Data == MCLogNotice::FocusLost && _Listening.has_value())
            {
                _Listening.reset();
                _PendingModifier.reset();
                _Status = Hint;
            }
            break;
        }
        default:
            break;
    }
}

auto MCLogKeyList::MouseWheel(int32_t steps, [[maybe_unused]] int32_t xPos, [[maybe_unused]] int32_t yPos) -> bool
{
    ScrollTo(_FirstRow + steps);
    return true;
}

auto MCLogKeyList::Leave() -> void
{
    _Hovered.reset();
}

auto MCLogKeyList::Write(int32_t xPos, int32_t yPos, std::string_view text, uint8_t color, int32_t maxWidth) -> void
{
    const MCGuiFont& font = *WhiteFont;
    const auto fits = static_cast<size_t>(std::max(font.CharactersToWidth(text, maxWidth, false), 0));
    VfxStringDraw(_Port->Frame(), xPos, yPos, font.FontData.Bytes(), text.substr(0, fits), FontColors(color));
}

auto MCLogKeyList::DrawCell(Cell cell, int32_t top) -> void
{
    const MCKeyCommand command = CellCommand(cell);

    if (command == MCKeyCommand::Unused)
    {
        return;
    }

    const auto left = static_cast<int16_t>(KeyLeft(cell.Column));
    const auto right = static_cast<int16_t>(left + CellWidth - 1);
    const auto cellTop = static_cast<int16_t>(top);
    const auto bottom = static_cast<int16_t>(top + RowHeight - 2);
    const bool listening = _Listening == cell;
    uint8_t line = LineColor;

    if (listening)
    {
        line = ListenLineColor;
        FillBox(left, cellTop, right, bottom, ListenFillColor);
    }
    else if (_Lost == cell)
    {
        line = LostLineColor;
    }
    else if (_Hovered == cell)
    {
        line = HoverLineColor;
    }

    FillBox(left, cellTop, right, cellTop, line);
    FillBox(left, bottom, right, bottom, line);
    FillBox(left, cellTop, left, bottom, line);
    FillBox(right, cellTop, right, bottom, line);

    const auto index = static_cast<size_t>(command);
    const uint32_t key = Keys()[index];
    const int32_t textWidth = CellWidth - 2 * CellTextLeft;

    if (listening)
    {
        Write(left + CellTextLeft, top + 1, "PRESS A KEY", ListenLineColor, textWidth);
        return;
    }

    const uint8_t color = key == MCKeyBindings::Defaults()[index] ? KeyColor : ChangedKeyColor;
    Write(left + CellTextLeft, top + 1, MCKeyBindings::KeyLabel(key), color, textWidth);
}

auto MCLogKeyList::Draw() -> void
{
    if (!_Port->ViewOpen())
    {
        return;
    }

    const auto right = static_cast<int16_t>(Width() - 1);
    const auto bottom = static_cast<int16_t>(Height() - 1);
    FillBox(0, 0, right, bottom, FillColor);
    FillBox(0, 0, right, 0, OutlineColor);
    FillBox(0, bottom, right, bottom, OutlineColor);
    FillBox(0, 0, 0, bottom, OutlineColor);
    FillBox(right, 0, right, bottom, OutlineColor);

    // The column titles, and a line under them.
    Write(HeadingLeft, TitleTop, CommandTitle, TitleColor, KeyLeft(0) - HeadingLeft);

    for (int32_t column = 0; column < 2; column++)
    {
        Write(KeyLeft(column) + CellTextLeft, TitleTop, KeyTitles[static_cast<size_t>(column)], TitleColor, CellWidth);
    }

    FillBox(1, TitleLineY, right - 1, TitleLineY, LineColor);

    // The rows shown.
    const int32_t rows = std::min(VisibleRows(), RowCount() - _FirstRow);

    for (int32_t shown = 0; shown < rows; shown++)
    {
        const int32_t index = _FirstRow + shown;
        const MCControlRow& row = Rows()[static_cast<size_t>(index)];
        const int32_t top = RowsTop + shown * RowHeight;

        if (row.IsHeading())
        {
            Write(HeadingLeft, top + TextOffset, row.Label, HeadingColor, KeyLeft(0) - HeadingLeft);
            continue;
        }

        Write(CommandLeft, top + TextOffset, row.Label, CommandColor, KeyLeft(0) - CommandLeft);
        DrawCell({index, 0}, top);
        DrawCell({index, 1}, top);
    }

    // The scroll bar: the track, and the thumb showing where the rows shown are.
    const auto trackLeft = static_cast<int16_t>(Width() - 1 - ScrollBarWidth);
    const auto trackRight = static_cast<int16_t>(Width() - 3);
    const auto trackTop = static_cast<int16_t>(TrackTop());
    const auto trackBottom = static_cast<int16_t>(TrackTop() + TrackHeight() - 1);
    FillBox(trackLeft, trackTop, trackRight, trackBottom, TrackColor);
    const auto [thumbTop, thumbHeight] = Thumb();
    FillBox(trackLeft, static_cast<int16_t>(thumbTop), trackRight, static_cast<int16_t>(thumbTop + thumbHeight - 1),
            ThumbColor);

    // The status line.
    const auto statusTop = static_cast<int16_t>(Height() - StatusHeight);
    FillBox(1, statusTop, right - 1, statusTop, LineColor);
    const bool news = _Status != Hint && !_Listening.has_value();
    Write(HeadingLeft, statusTop + StatusTextOffset, _Status, news ? NewsColor : HintColor, Width() - 2 * HeadingLeft);
}

#include "stdafx.h"
#include "logistics/MCLogKeyList.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiGlobals.h"
#include "lib/MCFatal.h"

namespace
{
    using MCKeyBindings::MCControlRow;

    /// <summary>A key the player changed (amber; the original's key is the boxes' text colour).</summary>
    constexpr uint8_t ChangedKeyColor = 0x33;
    /// <summary>A key cell's outline while listening (with its fill), and when it lost its key.</summary>
    constexpr uint8_t ListenLineColor = 0x33;
    constexpr uint8_t ListenFillColor = 0x12;
    constexpr uint8_t LostLineColor = 0xef;
    /// <summary>A key cell's width and the gap after it.</summary>
    constexpr int32_t CellWidth = 128;
    constexpr int32_t CellGap = 8;

    /// <summary>The column titles.</summary>
    constexpr std::string_view CommandTitle = "COMMAND";
    constexpr std::array<std::string_view, 2> KeyTitles = {"KEY", "ALTERNATE"};

    /// <summary>The status line's hints.</summary>
    constexpr std::string_view Hint = "CLICK A KEY TO CHANGE IT. RIGHT-CLICK A KEY TO CLEAR IT.";
    constexpr std::string_view DefaultsNews = "THE ORIGINAL KEYS ARE BACK.";
    constexpr std::string_view LaptopNews = "LAPTOP KEYS: NO KEYPAD, F-KEYS OR PAGE KEYS NEEDED.";
    /// <summary>What listening says about a key the game has no code for (F13 and up, media keys): it can't be bound.</summary>
    constexpr std::string_view UnreadableKey = "THE GAME CAN'T READ THAT KEY. PRESS ANOTHER, OR ESC.";

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
    int32_t ControlRowCount()
    {
        return static_cast<int32_t>(Rows().size());
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

    _Listening.reset();
    MCLogScrollList::Destroy();
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
    ResetScroll();
    _Hovered.reset();
    _Lost.reset();
    _Status = Hint;
}

auto MCLogKeyList::RestoreDefaults() -> void
{
    UsePreset(MCKeyBindings::Defaults(), DefaultsNews);
}

auto MCLogKeyList::UseLaptopKeys() -> void
{
    UsePreset(MCKeyBindings::Laptop(), LaptopNews);
}

auto MCLogKeyList::UsePreset(const MCKeyBindings::MCKeyTable& keys, std::string_view news) -> void
{
    StopListening();
    Keys() = keys;
    _Lost.reset();
    _Status = news;
}

auto MCLogKeyList::RowCount() -> int32_t
{
    return ControlRowCount();
}

auto MCLogKeyList::KeyLeft(int32_t column) -> int32_t
{
    return ScrollBarLeft() - (2 - column) * (CellWidth + CellGap);
}

auto MCLogKeyList::CellCommand(Cell cell) -> MCKeyCommand
{
    if (cell.Row < 0 || cell.Row >= ControlRowCount())
    {
        return MCKeyCommand::Unused;
    }

    const MCControlRow& row = Rows()[static_cast<size_t>(cell.Row)];
    return cell.Column == 0 ? row.Command : row.Alternate;
}

auto MCLogKeyList::CellOf(MCKeyCommand command) -> std::optional<Cell>
{
    for (int32_t row = 0; row < ControlRowCount(); row++)
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
    const std::optional<int32_t> top = RowTop(cell.Row);

    if (!top.has_value())
    {
        return std::nullopt;
    }

    return std::pair{GlobalX() + KeyLeft(cell.Column) + CellWidth / 2, GlobalY() + *top + RowHeight / 2};
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
    if (HandleScrollMouse(event))
    {
        return;
    }

    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            const std::optional<Cell> cell = CellAt(event.X, event.Y);

            if (cell.has_value() && cell != _Listening)
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
        case MCGuiEventType::MouseMove:
        {
            _Hovered = CellAt(event.X, event.Y);
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
                else if (event.ScanCode == 0)
                {
                    // MCInput gives every key it passes on a code, but a key without one would be every such key in a
                    // mission, and PREFS couldn't name it.
                    _Status = UnreadableKey;
                }
                else
                {
                    Assign(MCKeyBindings::FromKeyEvent(event.ScanCode, event.ShiftKey != 0, event.CtrlKey != 0,
                                                       event.AltKey != 0));
                }
            }
            else if (!ScrollByKey(event.Key))
            {
                PassToScreen(event);
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

auto MCLogKeyList::Leave() -> void
{
    _Hovered.reset();
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

    DrawOutline(left, cellTop, right, bottom, line);

    const auto index = static_cast<size_t>(command);
    const uint32_t key = Keys()[index];
    const int32_t textWidth = CellWidth - 2 * BoxTextLeft;

    if (listening)
    {
        Write(left + BoxTextLeft, top + 1, "PRESS A KEY", ListenLineColor, textWidth);
        return;
    }

    const uint8_t color = key == MCKeyBindings::Defaults()[index] ? BoxTextColor : ChangedKeyColor;
    Write(left + BoxTextLeft, top + 1, MCKeyBindings::KeyLabel(key), color, textWidth);
}

auto MCLogKeyList::Draw() -> void
{
    if (!_Port->ViewOpen())
    {
        return;
    }

    DrawFrame(_Status, _Status != Hint && !_Listening.has_value());

    // The column titles.
    Write(HeadingLeft, TitleTop, CommandTitle, TitleColor, KeyLeft(0) - HeadingLeft);

    for (int32_t column = 0; column < 2; column++)
    {
        Write(KeyLeft(column) + BoxTextLeft, TitleTop, KeyTitles[static_cast<size_t>(column)], TitleColor, CellWidth);
    }

    // The rows shown.
    const int32_t rows = std::min(VisibleRows(), RowCount() - FirstRow());

    for (int32_t shown = 0; shown < rows; shown++)
    {
        const int32_t index = FirstRow() + shown;
        const MCControlRow& row = Rows()[static_cast<size_t>(index)];
        const int32_t top = RowsTop + shown * RowHeight;

        if (row.IsHeading())
        {
            Write(HeadingLeft, top + TextOffset, row.Label, HeadingColor, KeyLeft(0) - HeadingLeft);
            continue;
        }

        Write(NameLeft, top + TextOffset, row.Label, NameColor, KeyLeft(0) - NameLeft);
        DrawCell({index, 0}, top);
        DrawCell({index, 1}, top);
    }
}

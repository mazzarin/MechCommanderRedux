#pragma once

#include "iface/MCKeyBindings.h"
#include "logistics/MCLogObject.h"

/// <summary>
/// Port-only: the CONTROLS screen's list of key bindings. A row per command (<see cref="MCKeyBindings::ControlRows"/>)
/// with its key and its alternate key, under headings; a status line under the rows says what to do and what changed.
/// The list edits the bindings it points at (its model) and draws every frame from them and its state, in the
/// preferences panel's style.
/// </summary>
/// <remarks>
/// A click on a key starts listening for its new one: the list takes the keyboard (Escape stops it; a click elsewhere
/// too), and the next key pressed, with the shift, ctrl and alt held, becomes the binding. Shift, ctrl or alt alone
/// binds when let go without another key. A key another command had moves: that command is left without it, and the
/// status line says so. A right click clears a key. Keys a command has that differ from the original's show in amber.
/// The wheel, the up and down keys (while not listening) and the bar at the right scroll the rows.
/// </remarks>
class MCLogKeyList : public MCLogObject
{
public:
    /// <summary>A key's place in the list: the row (in <see cref="MCKeyBindings::ControlRows"/>) and the column (0 key, 1 alternate).</summary>
    struct Cell
    {
        int32_t Row = 0;
        int32_t Column = 0;

        bool operator==(const Cell&) const = default;
    };

    /// <summary>The height of a row.</summary>
    static constexpr int32_t RowHeight = 11;

    /// <summary>Calls <see cref="Destroy"/>.</summary>
    ~MCLogKeyList() override;

    /// <summary>
    /// Places the list at (<paramref name="xPos"/>, <paramref name="yPos"/>), <paramref name="width"/> x
    /// <paramref name="height"/>, editing <paramref name="keys"/>.
    /// </summary>
    void Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, MCKeyBindings::MCKeyTable& keys);

    /// <summary>Stops listening before the object goes.</summary>
    void Destroy() override;

    /// <summary>Draws the headings, the rows, the scroll bar and the status line from the state.</summary>
    void Draw() override;

    bool DrawsLive() override { return true; }

    /// <summary>The mouse and keys, as the class remarks say.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>The wheel scrolls the rows.</summary>
    bool MouseWheel(int32_t steps, int32_t xPos, int32_t yPos) override;

    /// <summary>The mouse left the list: no key is lit.</summary>
    void Leave() override;

    /// <summary>Back to the top, not listening, with the status line's hint (the screen opened).</summary>
    void Reset();

    /// <summary>
    /// Puts back the original's bindings (DEFAULTS), saying so on the status line.
    /// </summary>
    void RestoreDefaults();

    /// <summary>The key cell under the screen point (<paramref name="xPos"/>, <paramref name="yPos"/>), if any.</summary>
    std::optional<Cell> CellAt(int32_t xPos, int32_t yPos);

    /// <summary>The screen point at the middle of <paramref name="cell"/> when its row is shown.</summary>
    std::optional<std::pair<int32_t, int32_t>> CellCentre(Cell cell);

    /// <summary>The command <paramref name="cell"/> binds, or <see cref="MCKeyCommand::Unused"/> for none.</summary>
    static MCKeyCommand CellCommand(Cell cell);

    /// <summary>The row of <paramref name="command"/>'s key (column 0) or alternate key (column 1), if it is listed.</summary>
    static std::optional<Cell> CellOf(MCKeyCommand command);

    /// <summary>Starts listening for <paramref name="cell"/>'s new key: the list takes the keyboard.</summary>
    void Listen(Cell cell);

    /// <summary>Stops listening (lets go of the keyboard) with the status line's hint back.</summary>
    void StopListening();

    /// <summary>The cell listening for a key, if any.</summary>
    std::optional<Cell> Listening() const { return _Listening; }

    /// <summary>Scrolls so the first row shown is <paramref name="row"/> (clamped).</summary>
    void ScrollTo(int32_t row);

    /// <summary>The first row shown.</summary>
    int32_t FirstRow() const { return _FirstRow; }

    /// <summary>The rows shown at once.</summary>
    int32_t VisibleRows();

    /// <summary>The status line's text.</summary>
    const std::string& Status() const { return _Status; }

private:
    /// <summary>The bindings the list edits (set by <see cref="Init"/>; fatal before).</summary>
    MCKeyBindings::MCKeyTable& Keys() const;

    /// <summary>Binds <paramref name="key"/> to the cell listening, stops listening and says what moved.</summary>
    void Assign(uint32_t key);

    /// <summary>Clears <paramref name="cell"/>'s key.</summary>
    void Clear(Cell cell);

    /// <summary>The row under the screen y, if it is a shown row.</summary>
    std::optional<int32_t> RowAt(int32_t yPos);

    /// <summary>The left edge of key column <paramref name="column"/>, in the list's pixels.</summary>
    int32_t KeyLeft(int32_t column);

    /// <summary>The top of the scroll bar's track and its height, in the list's pixels.</summary>
    int32_t TrackTop() const;
    int32_t TrackHeight();

    /// <summary>The scroll bar's thumb: its top and height in the list's pixels.</summary>
    std::pair<int32_t, int32_t> Thumb();

    /// <summary>Whether the screen point lies on the scroll bar.</summary>
    bool OnScrollBar(int32_t xPos, int32_t yPos);

    /// <summary>The row the thumb's top at the list's y <paramref name="thumbTop"/> shows first.</summary>
    int32_t RowForThumb(int32_t thumbTop);

    /// <summary>Scrolls by the press at the scroll bar's screen y: drags the thumb, or pages toward the press.</summary>
    void PressScrollBar(int32_t yPos);

    /// <summary>Writes <paramref name="text"/> at (<paramref name="xPos"/>, <paramref name="yPos"/>) in <paramref name="color"/>, cut at <paramref name="maxWidth"/>.</summary>
    void Write(int32_t xPos, int32_t yPos, std::string_view text, uint8_t color, int32_t maxWidth);

    /// <summary>Draws a key cell's box and its key's name.</summary>
    void DrawCell(Cell cell, int32_t top);

    /// <summary>The bindings the list edits.</summary>
    MCOptionalRef<MCKeyBindings::MCKeyTable> _Keys;
    /// <summary>The first row shown.</summary>
    int32_t _FirstRow = 0;
    /// <summary>The key cell under the mouse, if any.</summary>
    std::optional<Cell> _Hovered;
    /// <summary>The key cell listening for its new key, if any.</summary>
    std::optional<Cell> _Listening;
    /// <summary>A modifier key went down while listening: it binds alone when let go first.</summary>
    std::optional<int16_t> _PendingModifier;
    /// <summary>The key cell that lost its key to the last binding, lit until the next change.</summary>
    std::optional<Cell> _Lost;
    /// <summary>The thumb is being dragged: the press's offset from the thumb's top.</summary>
    std::optional<int32_t> _ThumbGrab;
    /// <summary>The status line's text.</summary>
    std::string _Status;
};

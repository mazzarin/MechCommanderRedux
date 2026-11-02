#pragma once

#include "iface/MCKeyBindings.h"
#include "logistics/MCLogScrollList.h"

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
/// The wheel, the up and down keys (while not listening) and the bar at the right scroll the rows (<see cref="MCLogScrollList"/>).
/// </remarks>
class MCLogKeyList : public MCLogScrollList
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

    MCLogKeyList() : MCLogScrollList(RowHeight) {}

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

    /// <summary>The mouse left the list: no key is lit.</summary>
    void Leave() override;

    /// <summary>Back to the top, not listening, with the status line's hint (the screen opened).</summary>
    void Reset();

    /// <summary>
    /// Puts back the original's bindings (DEFAULTS), saying so on the status line.
    /// </summary>
    void RestoreDefaults();

    /// <summary>Takes the LAPTOP bindings (<see cref="MCKeyBindings::Laptop"/>), saying so on the status line.</summary>
    void UseLaptopKeys();

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

    /// <summary>The status line's text.</summary>
    const std::string& Status() const { return _Status; }

protected:
    /// <summary>The number of rows (<see cref="MCKeyBindings::ControlRows"/>).</summary>
    int32_t RowCount() override;

private:
    /// <summary>The bindings the list edits (set by <see cref="Init"/>; fatal before).</summary>
    MCKeyBindings::MCKeyTable& Keys() const;

    /// <summary>Binds <paramref name="key"/> to the cell listening, stops listening and says what moved.</summary>
    void Assign(uint32_t key);

    /// <summary>Clears <paramref name="cell"/>'s key.</summary>
    void Clear(Cell cell);

    /// <summary>Takes <paramref name="keys"/> (DEFAULTS, LAPTOP), saying <paramref name="news"/> on the status line.</summary>
    void UsePreset(const MCKeyBindings::MCKeyTable& keys, std::string_view news);

    /// <summary>The left edge of key column <paramref name="column"/>, in the list's pixels.</summary>
    int32_t KeyLeft(int32_t column);

    /// <summary>Draws a key cell's box and its key's name.</summary>
    void DrawCell(Cell cell, int32_t top);

    /// <summary>The bindings the list edits.</summary>
    MCOptionalRef<MCKeyBindings::MCKeyTable> _Keys;
    /// <summary>The key cell under the mouse, if any.</summary>
    std::optional<Cell> _Hovered;
    /// <summary>The key cell listening for its new key, if any.</summary>
    std::optional<Cell> _Listening;
    /// <summary>A modifier key went down while listening: it binds alone when let go first.</summary>
    std::optional<int16_t> _PendingModifier;
    /// <summary>The key cell that lost its key to the last binding, lit until the next change.</summary>
    std::optional<Cell> _Lost;
    /// <summary>The status line's text.</summary>
    std::string _Status;
};

#pragma once

#include "gui/MCListView.h"
#include "logistics/MCDragIcon.h"

class MCGuiEvent;
class MCLogInvScreen;

/// <summary>
/// One row of an inventory list on the logistics inventory screens (a mech, pilot, vehicle or component the player
/// owns): a 0xad x 0x2b <see cref="MCLogObject"/> drawn into the inventory tab's view at row <see cref="ListIndex"/>.
/// </summary>
/// <remarks>Original source: <c>logistics\invblock.cpp</c> (<c>InventoryBlock</c>).</remarks>
class MCInventoryBlock : public MCListRow
{
public:
    /// <summary>Copies the blank row art (<c>Logistics::InvBlockPort</c>) into <paramref name="port"/> at this row.</summary>
    void DrawBackground(MCLogPort& port);

    /// <summary>Draws the row greyed out. Empty in the original.</summary>
    void DrawDisabled();

    /// <summary>Sets <see cref="Enabled"/> and redraws.</summary>
    void SetEnabled(bool enable);

    /// <summary>
    /// Port: draws the row into <paramref name="port"/> with its top at <paramref name="top"/>: what the original's
    /// <c>drawBackground</c> painted into the inventory tab's picture (the tab is now drawn from its rows each frame,
    /// and <c>drawBackground</c> keeps only what it did besides painting). The base draws the blank row art.
    /// </summary>
    void DrawRow(MCLogPort& port, int32_t top) override;

    /// <summary>
    /// Port: draws the drag icon's picture into <paramref name="surface"/> (0x20 square): the row's square at (2, 1)
    /// over colour 0x10.
    /// </summary>
    void OnBeginDrag(MCLogPort& surface);

    /// <summary>The row is usable; otherwise <c>Draw</c> calls <see cref="DrawDisabled"/>.</summary>
    bool Enabled = false;
    /// <summary>
    /// The row of the list this block is drawn in (its y is <c>ListIndex * height</c>): its unit's, pilot's or item's
    /// <c>ListIndex</c> (-1 once that is gone).
    /// </summary>
    virtual int32_t ListIndex() const = 0;

    /// <summary>A sale's price: half the value, except in a multiplayer or solo game.</summary>
    static int32_t SalePrice(int32_t value);

protected:
    /// <summary>Places the row object at (0, 0) without a port of its own (it draws into the tab's view); enables it.</summary>
    void InitRow();

    /// <summary>
    /// What every row does first with an event: passes keys up to the parent when nothing is dragged, and when the
    /// mouse is off the inventory pane vertically, blanks the info box and hands the event to the screen.
    /// </summary>
    /// <returns>False when the event was handled (or the row is hidden) and the row should do nothing more.</returns>
    bool PreHandleEvent(const MCDragState& drag, MCGuiEvent& event);

    /// <summary>
    /// Makes the drag icon: a 0x20 square of the row at (2, 1) (<see cref="OnBeginDrag"/>), framed, added to the
    /// screen and centred on the cursor. <see cref="MCDragIcon::Raise"/> shows it.
    /// </summary>
    void MakeDragIcon(MCDragState& drag, MCGuiEvent& event);

    /// <summary>The logistics screen shown, as the inventory screen it is when a row gets events.</summary>
    static std::shared_ptr<MCLogInvScreen> Screen();

    /// <summary>Shows the blank info box of tab <paramref name="tab"/> (the "drop here" art the original loaded again).</summary>
    static void DrawDropArt(MCLogInvScreen& screen, int32_t tab);

    /// <summary>
    /// Adds one to every filled drop zone slot's unit (or, for a <paramref name="vehicle"/>, its vehicle) index: a unit
    /// joining the force goes in front of the others.
    /// </summary>
    static void BumpDeploySlots(bool vehicle);
};

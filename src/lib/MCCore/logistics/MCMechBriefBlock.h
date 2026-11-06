#pragma once

#include "gui/MCListView.h"

class MCGuiEvent;
class MCLogMech;
class MCLogPart;
class MCLogVehicle;

/// <summary>
/// One mech or vehicle in the briefing screen's deploy pane or a drop slot: its picture, name and status; it can be
/// dragged into a drop slot (left button: the slot under the mouse; right button: the first free slot, of the lance
/// whose number key is held).
/// </summary>
/// <remarks>
/// Original source: <c>logistics\logbri.cpp</c> (<c>MechBriefBlock</c>). The original's unit owned its block, which
/// moved between the deploy pane and the screen; the port's blocks are views the deploy pane (a row of its list) or the
/// screen (a drop slot's, <see cref="MCBriefingScreen::ShowSlots"/>) makes over the unit. A drop changes the deploy
/// data, and the views follow it.
/// </remarks>
class MCMechBriefBlock : public MCListRow
{
public:
    /// <summary>Shows <paramref name="part"/> (a mech or a vehicle) at (<paramref name="xPos"/>, <paramref name="yPos"/>).</summary>
    void Init(const std::shared_ptr<MCLogPart>& part, int32_t xPos, int32_t yPos);

    /// <summary>Forgets the unit shown.</summary>
    void OnDestroy() override;

    /// <summary>Drags the block and drops it into a slot (or back), updating the tonnage.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>The deploy pane's row: the block unframed, at the row's place.</summary>
    void DrawRow(MCLogPort& port, int32_t top) override;

    /// <summary>
    /// Port: draws the block's picture (the unit's picture and name, darkened when another player's, with a bevelled
    /// frame when <paramref name="framed"/>) at (<paramref name="xPos"/>, <paramref name="yPos"/>) in
    /// <paramref name="target"/>: what <see cref="DrawBackground"/> painted into its parent's picture.
    /// </summary>
    void PaintBlock(const MCPane& target, int32_t xPos, int32_t yPos, bool framed);

    /// <summary>
    /// Port: draws the drag icon's picture into <paramref name="surface"/> (0x34 x 0x2e): the block framed over the
    /// empty slot when it is in a drop slot, unframed over colour 0x10 in the deploy pane.
    /// </summary>
    void OnBeginDrag(MCLogPort& surface);

    /// <summary>The mech shown, or null for a vehicle.</summary>
    std::shared_ptr<MCLogMech> Mech() const { return _Mech.lock(); }
    /// <summary>The vehicle shown, or null for a mech.</summary>
    std::shared_ptr<MCLogVehicle> Vehicle() const { return _Vehicle.lock(); }
    /// <summary>The unit shown (the mech or the vehicle), or null once it is gone.</summary>
    std::shared_ptr<MCLogPart> Part() const;

private:
    /// <summary>The mech shown, or null for a vehicle.</summary>
    std::weak_ptr<MCLogMech> _Mech;
    /// <summary>The vehicle shown, or null for a mech.</summary>
    std::weak_ptr<MCLogVehicle> _Vehicle;

    /// <summary>A button went down on the block: its briefing shows, and (unless another player's) it is picked up.</summary>
    void PickUp(MCGuiEvent& event);

    /// <summary>The block was let go at the event's place: into a slot, or back to the deploy pane.</summary>
    void Drop(MCGuiEvent& event);

    /// <summary>
    /// Puts the unit into empty slot <paramref name="slot"/> of <paramref name="lance"/>: false when it is too heavy
    /// (without the hammer down).
    /// </summary>
    bool PlaceInEmptySlot(int32_t lance, int32_t slot) const;

    /// <summary>
    /// The unit is in its slot: the screen shows the slots again (a block of its own there) and recounts the tonnage.
    /// </summary>
    static void Settle();

    /// <summary>The right-button drop: the first free slot (of the lance whose key 1..3 is held); whether it was placed.</summary>
    bool DropInFreeSlot();

    /// <summary>The left-button drop at <paramref name="point"/>: the slot under it; whether it was placed.</summary>
    bool DropInSlotAt(POINT point, int32_t firstLance);
};

#pragma once

#include "gui/MCScrollPane.h"
#include "logistics/MCLogObject.h"

class MCGuiEvent;
class MCLogMech;
class MCLogPart;
class MCLogVehicle;
class MCMechRepairBlock;
class MCVehicleRepairBlock;

/// <summary>The panel of the briefing screen showing the selected mech or vehicle (as its repair panel shows it).</summary>
/// <remarks>
/// Original source: <c>logistics\mrblock.cpp</c> (<c>BriefingBox</c>). The original made one per unit (the unit owned
/// it); the port's briefing screen makes the one it shows (<see cref="MCBriefingScreen::ShowBriefing"/>).
/// </remarks>
class MCBriefingBox : public MCLogObject
{
public:
    /// <summary>Shows <paramref name="part"/>: a mech (with a weapon-list pane) or a vehicle.</summary>
    void Init(const std::shared_ptr<MCLogPart>& part);

    /// <summary>Refills the mech's weapon list (the screen draws the box each frame).</summary>
    void DrawBackground();

    /// <summary>
    /// Port: draws the box (the unit's repair panel, its weapon list and tonnage bar, darkened) at
    /// (<paramref name="xPos"/>, <paramref name="yPos"/>) in <paramref name="target"/>: what
    /// <see cref="DrawBackground"/> painted into the briefing screen's picture.
    /// </summary>
    void PaintBox(const MCPane& target, int32_t xPos, int32_t yPos) const;

    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// Port-only: over the weapon list the mouse wheel scrolls it, as its arrows do (the list's pane is hidden, so the
    /// wheel finds the box).
    /// </summary>
    bool MouseWheel(int32_t steps, int32_t xPos, int32_t yPos) override;

    void Draw() override;

    /// <summary>Empty (a bare <c>ret</c> the export has no function for).</summary>
    void Display() override;

    /// <summary>The mech (null once it is gone); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogMech> Mech() const { return _Mech.lock(); }
    /// <summary>The vehicle (null once it is gone); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogVehicle> Vehicle() const { return _Vehicle.lock(); }
    /// <summary>The unit shown (the mech or the vehicle), or null once it is gone.</summary>
    std::shared_ptr<MCLogPart> Part() const;
    /// <summary>The mech's weapon lists (a child; none for a vehicle).</summary>
    MCGuiHandle<MCScrollPane> InventoryPane;
    /// <summary>
    /// Port: the mech's repair panel in the briefing layout, a hidden child the box paints through (the original
    /// painted the repair screen's block of the mech; none for a vehicle).
    /// </summary>
    MCGuiHandle<MCMechRepairBlock> MechPanel;
    /// <summary>Port: as <see cref="MechPanel"/>, for a vehicle.</summary>
    MCGuiHandle<MCVehicleRepairBlock> VehiclePanel;

private:
    /// <summary>The mech the block refers to (<see cref="Mech"/>).</summary>
    std::weak_ptr<MCLogMech> _Mech;
    /// <summary>The vehicle the block refers to (<see cref="Vehicle"/>).</summary>
    std::weak_ptr<MCLogVehicle> _Vehicle;
};

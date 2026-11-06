#pragma once

#include "gui/MCListView.h"
#include "logistics/MCLogObject.h"

class MCGuiEvent;
class MCLogVehicle;

/// <summary>A vehicle's panel on the repair screen (vehicles can't be refitted: mostly a status bar).</summary>
/// <remarks>
/// Original source: <c>logistics\mrblock.cpp</c> (<c>VehicleRepairBlock</c>). A row of the repair screen's force list
/// (<see cref="MCRepairScreen::ShowForce"/>), or a panel of the briefing box (outside any list).
/// </remarks>
class MCVehicleRepairBlock : public MCListRow
{
public:
    /// <summary>Shows <paramref name="logVehicle"/>.</summary>
    void Init(std::shared_ptr<MCLogVehicle> logVehicle);

    /// <summary>Selecting the vehicle and dragging it back to the inventory.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>Draws the damage diagram into <paramref name="port"/> (the vehicle's mask).</summary>
    void DrawDamageDiagram(MCLogPort& port) const;

    /// <summary>As <see cref="MCMechRepairBlock::DrawBackground"/>.</summary>
    void DrawBackground(int32_t row, MCOptionalRef<MCLogPort> port);

    /// <summary>Port: as <see cref="MCMechRepairBlock::DrawRow"/>.</summary>
    void DrawRow(MCLogPort& port, int32_t top) override;

    /// <summary>
    /// Port: draws the picked-up vehicle's drag icon into <paramref name="surface"/> (0x1e square): its body diagram
    /// at (2, 0) over colour 0x10.
    /// </summary>
    void OnBeginDrag(MCLogPort& surface) const;

    /// <summary>Draws the status bar into <paramref name="port"/> at <paramref name="xPos"/>.</summary>
    void SetBar(MCLogPort& port, int32_t xPos) const;

    /// <summary>The vehicle's force row, after the mechs (<see cref="MCLogPart::SlotIndex"/>; 0 once it is gone).</summary>
    int32_t SlotIndex() const;
    /// <summary>The vehicle (null once it is gone); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogVehicle> Vehicle() const { return _Vehicle.lock(); }

private:
    /// <summary>The vehicle the block refers to (<see cref="Vehicle"/>).</summary>
    std::weak_ptr<MCLogVehicle> _Vehicle;

    /// <summary>Port: the panel into <paramref name="port"/> with its top at <paramref name="top"/>.</summary>
    void PaintRow(MCLogPort& port, int32_t top, bool briefing, bool framed);

    /// <summary>The vehicle dropped on the inventory: it leaves the force.</summary>
    void LeaveForce() const;
};

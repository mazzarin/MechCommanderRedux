#pragma once

#include "logistics/MCLogInvScreen.h"

class MCGuiEvent;
class MCLogMech;
class MCLogPart;
class MCLogVehicle;
class MCMechRepairBlock;
class MCVehicleRepairBlock;

/// <summary>
/// The logistics repair (mech lab) screen: the inventory on the left, the force's mechs and vehicles in the unit pane;
/// the selected unit can be repaired, refitted and given a pilot.
/// </summary>
/// <remarks>Original source: <c>logistics\logrep.cpp</c> (<c>RepairScreen</c>).</remarks>
class MCRepairScreen : public MCLogInvScreen
{
public:
    /// <summary>Makes the full-screen object and its two panes.</summary>
    void Init();

    /// <summary>Frees the panes.</summary>
    void OnDestroy() override;

    /// <summary>Selects <paramref name="mech"/> (deselecting any vehicle) and redraws the affected blocks.</summary>
    void SelectMech(std::shared_ptr<MCLogMech> mech);

    /// <summary>Selects <paramref name="vehicle"/> (deselecting any mech) and redraws the affected blocks.</summary>
    void SelectVehicle(std::shared_ptr<MCLogVehicle> vehicle);

    /// <summary>A mech joined the force (first in its list): the rows move down one.</summary>
    void AddMechToList(MCLogMech& mech);

    /// <summary>A vehicle joined the force (first among the vehicles, after the mechs): the vehicle rows move down one.</summary>
    void AddVehicleToList(MCLogVehicle& vehicle);

    /// <summary>A mech left the force: the rows below its row move up one.</summary>
    void RemoveMechFromList(MCLogMech& mech);

    /// <summary>A vehicle left the force: the rows below its row move up one.</summary>
    void RemoveVehicleFromList(MCLogVehicle& vehicle);

    /// <summary>
    /// Lists the force in the unit pane, a row per unit (the mechs, then the vehicles), from the top when
    /// <paramref name="resetScroll"/>, and numbers the units' <see cref="MCLogPart::SlotIndex"/> by their row.
    /// </summary>
    /// <remarks>
    /// The pane shows the force as it was at the last call (the original painted its rows' picture only then): a unit
    /// picked up from the inventory joins the force list under the drag, but its row comes with the drop's call.
    /// </remarks>
    void ShowForce(bool resetScroll);

    /// <summary>
    /// A unit left the force list without a new listing (the lists cleared): its row stays drawn until the next
    /// <see cref="ShowForce"/>, as the original's picture kept it, but takes no clicks, as its block left the pane.
    /// </summary>
    void HideUnitRow(const MCLogPart& unit);

    /// <summary>The unit pane's row of <paramref name="mech"/>, or null; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCMechRepairBlock> RowOf(const MCLogMech& mech) const;

    /// <summary>The unit pane's row of <paramref name="vehicle"/>, or null; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCVehicleRepairBlock> RowOf(const MCLogVehicle& vehicle) const;

    /// <summary>Redraws the screen's art (the shared places and the info box go back to the art's).</summary>
    void DrawBackground();

    /// <summary>The tabs, the screen buttons, the resource cheat and the help text for what the mouse is over.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>Shows or hides the screen and its panes.</summary>
    void ShowGuiWindow(bool show) override;

    /// <summary>
    /// Displays the screen and, on the inventory, briefing and session screens, updates the clock (the screens draw it
    /// with the resource figure, <see cref="ResourceFigureText"/>).
    /// </summary>
    void Display() override;

    /// <summary>The selected mech, if any.</summary>
    std::shared_ptr<MCLogMech> SelectedMech() const { return _SelectedMech.lock(); }
    /// <summary>Forgets the selected mech (without drawing anything).</summary>
    void ClearSelectedMech() { _SelectedMech.reset(); }
    /// <summary>The selected vehicle, if any.</summary>
    std::shared_ptr<MCLogVehicle> SelectedVehicle() const { return _SelectedVehicle.lock(); }
    /// <summary>Forgets the selected vehicle (without drawing anything).</summary>
    void ClearSelectedVehicle() { _SelectedVehicle.reset(); }
    /// <summary>Forgets the selected mech and vehicle (without drawing anything).</summary>
    void ClearSelection()
    {
        ClearSelectedMech();
        ClearSelectedVehicle();
    }

private:
    /// <summary>
    /// The units the unit pane lists, in row order (the force's mechs, then its vehicles), as <see cref="ShowForce"/>
    /// last took them.
    /// </summary>
    std::vector<std::shared_ptr<MCLogPart>> _ShownUnits;
    /// <summary>The selected mech, if any.</summary>
    std::weak_ptr<MCLogMech> _SelectedMech;
    /// <summary>The selected vehicle, if any.</summary>
    std::weak_ptr<MCLogVehicle> _SelectedVehicle;
};

/// <summary>Which resource figure the screens show (0: the resource points; 1 and 2 were the logistics heap's).</summary>
extern int32_t ResourceDisplayState;

/// <summary>
/// Port: the resource figure the screens show (<see cref="ResourceDisplayState"/>: the resource points, or the
/// logistics heap's total or largest free block, gone with the heap: empty).
/// </summary>
std::string ResourceFigureText();

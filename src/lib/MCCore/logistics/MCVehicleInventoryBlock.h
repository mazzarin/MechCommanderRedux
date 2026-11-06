#pragma once

#include "logistics/MCInventoryBlock.h"

class MCLogVehicle;

/// <summary>
/// An owned vehicle's row in the inventory list (a row of <see cref="MCListView"/>): name, tonnage and class, and a
/// small damage diagram.
/// </summary>
/// <remarks>Original source: <c>logistics\invblock.cpp</c> (<c>VehicleInventoryBlock</c>).</remarks>
class MCVehicleInventoryBlock : public MCInventoryBlock
{
public:
    /// <summary>Shows <paramref name="logVehicle"/>.</summary>
    void Init(std::shared_ptr<MCLogVehicle> logVehicle);

    /// <summary>Frees the picture.</summary>
    void OnDestroy() override;

    /// <summary>Empty (a bare <c>ret</c> the export has no function for).</summary>
    void Draw() override;

    /// <summary>Selection, drag to a drop slot or the store (a sale), and the info display.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// Makes the damage diagram when it is missing (the original's <c>drawBackground</c>, which also painted the row).
    /// </summary>
    void OnRefresh() override;

    /// <summary>Port: the vehicle's name, tonnage/class line and diagram.</summary>
    void DrawRow(MCLogPort& port, int32_t top) override;

    /// <summary>
    /// The sale dialog's answer for <paramref name="sold"/>: a confirmed sale removes the vehicle; otherwise it goes
    /// back to the inventory.
    /// </summary>
    static void OnSellConfirmed(MCLogVehicle& sold, int32_t result);

    /// <summary>The cached damage diagram (0x1c x 0x1e).</summary>
    std::shared_ptr<MCLogPort> PicturePort;
    /// <summary>The body locations the diagram shows.</summary>
    static constexpr int32_t DiagramLocations = 5;
    /// <summary>The diagram's locations' damage states (none: a location without armor, not drawn).</summary>
    using DiagramLook = std::array<std::optional<int32_t>, DiagramLocations>;
    /// <summary>
    /// Port: the diagram as <see cref="OnRefresh"/> made it. The rows and the info box draw it in place from this
    /// (<see cref="PaintDiagram"/>; the original copied the picture), so with HD art on it shows from the pack.
    /// </summary>
    std::optional<DiagramLook> DiagramStates;

    /// <summary>
    /// Draws <paramref name="vehicle"/>'s diagram as <paramref name="states"/> hold it into <paramref name="port"/>
    /// (its corner at 0, 0), as the original's picture held it.
    /// </summary>
    static void PaintDiagram(const MCLogVehicle& vehicle, const DiagramLook& states, MCLogPort& port);

    /// <summary>
    /// Port: <paramref name="vehicle"/>'s details in the info box: its <paramref name="diagram"/>, tonnage, classes,
    /// speed and description.
    /// </summary>
    static void DrawVehicleInfo(const MCLogVehicle& vehicle, MCOptionalRef<const DiagramLook> diagram, MCLogPort& port);
    /// <summary>The vehicle (null once it is gone); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogVehicle> Vehicle() const { return _Vehicle.lock(); }
    /// <summary>The vehicle's <see cref="MCLogPart::ListIndex"/>.</summary>
    int32_t ListIndex() const override;

private:
    /// <summary>The vehicle the block refers to (<see cref="Vehicle"/>).</summary>
    std::weak_ptr<MCLogVehicle> _Vehicle;

    /// <summary>Offers the vehicle for sale unless the mission requires it; true when the dialog opened.</summary>
    bool OfferSale();
};

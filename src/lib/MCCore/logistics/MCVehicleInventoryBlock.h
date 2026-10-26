#pragma once

#include "logistics/MCInventoryBlock.h"

class MCLogVehicle;

/// <summary>An owned vehicle in the inventory list: name, tonnage and class, and a small damage diagram.</summary>
/// <remarks>Original source: <c>logistics\invblock.cpp</c> (<c>VehicleInventoryBlock</c>).</remarks>
class MCVehicleInventoryBlock : public MCInventoryBlock
{
public:
    ~MCVehicleInventoryBlock() override;

    /// <summary>Shows <paramref name="logVehicle"/>; reads its weight-class and armor texts.</summary>
    void Init(std::shared_ptr<MCLogVehicle> logVehicle);

    /// <summary>Frees the picture and the texts.</summary>
    void Destroy() override;

    /// <summary>Empty (a bare <c>ret</c> the export has no function for).</summary>
    void Draw() override;

    /// <summary>Selection, drag to a drop slot or the store (a sale), and the info display.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// Makes the damage diagram when it is missing. Port: the row is drawn by <see cref="DrawRow"/> (the original
    /// painted it here).
    /// </summary>
    void DrawBackground();

    /// <summary>Port: the vehicle's name, tonnage/class line and diagram.</summary>
    void DrawRow(MCLogPort& port, int32_t top) override;

    /// <summary>Port: the diagram, tonnage, classes, speed and description.</summary>
    void DrawInfo(MCLogPort& port) override;

    /// <summary>
    /// The sale dialog's answer: a confirmed sale removes the vehicle; otherwise it goes back to the inventory.
    /// </summary>
    void OnSellConfirmed(int32_t result) const;

    /// <summary>The cached damage diagram (0x1c x 0x1e), for the sale dialog.</summary>
    std::shared_ptr<MCLogPort> PicturePort;
    /// <summary>The body locations the diagram shows.</summary>
    static constexpr int32_t DiagramLocations = 5;
    /// <summary>
    /// Port: the diagram's locations' damage states as <see cref="DrawBackground"/> made it (none: a location without
    /// armor, not drawn). The rows and the info box draw it in place from these (<see cref="PaintDiagram"/>; the
    /// original copied the picture), so with HD art on it shows from the pack.
    /// </summary>
    std::optional<std::array<std::optional<int32_t>, DiagramLocations>> DiagramStates;

    /// <summary>Draws the diagram into <paramref name="port"/> (its corner at 0, 0), as the original's picture held it.</summary>
    void PaintDiagram(MCLogPort& port) const;
    /// <summary>The vehicle (null once it is gone); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogVehicle> Vehicle() const { return _Vehicle.lock(); }
    /// <summary>The weight class (string table 0x4f..0x52 by tonnage).</summary>
    std::string WeightClassText;
    /// <summary>The armor rating (string table 0x4f/0x51/0x64..0x66 by armor tonnage), cut at 14 characters.</summary>
    std::string ArmorText;

private:
    /// <summary>The vehicle the block refers to (<see cref="Vehicle"/>).</summary>
    std::weak_ptr<MCLogVehicle> _Vehicle;

    /// <summary>Offers the vehicle for sale unless the mission requires it; true when the dialog opened.</summary>
    bool OfferSale();
};

#pragma once

#include "gui/MCListView.h"

class MCGuiEvent;
class MCPurVehicle;

/// <summary>
/// A row of the vehicle store (a row of the store's <see cref="MCListView"/>): picture, diagram, class texts,
/// weapons, stock and price.
/// </summary>
/// <remarks>Original source: <c>logistics\purchase.cpp</c> (<c>VehiclePurchaseBlock</c>).</remarks>
class MCVehiclePurchaseBlock : public MCListRow
{
public:
    /// <summary>Shows <paramref name="purVehicle"/>; reads its class texts.</summary>
    void Init(std::shared_ptr<MCPurVehicle> purVehicle);

    /// <summary>Frees the diagram and the texts.</summary>
    void OnDestroy() override;

    /// <summary>Buying (a drag to the inventory) and the help line.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// Remakes the diagram the purchase dialog shows (while in stock) and readies the description (the original's
    /// <c>drawBackground</c>, which also painted the row into the store's picture).
    /// </summary>
    void OnRefresh() override;

    /// <summary>
    /// Port: draws the row into <paramref name="port"/> (the store's content) with its top at <paramref name="top"/>:
    /// what the original's <c>drawBackground</c> painted into the store's picture.
    /// </summary>
    void DrawRow(MCLogPort& port, int32_t top) override;

    /// <summary>
    /// Port: draws the drag icon's picture into <paramref name="surface"/>: a 0x20 square of the row, over the store's
    /// background.
    /// </summary>
    void OnBeginDrag(MCLogPort& surface);

    /// <summary>
    /// The purchase dialog's answer for <paramref name="purVehicle"/>: a confirmed purchase of
    /// <paramref name="quantity"/> adds them to the inventory and takes them off the stock.
    /// </summary>
    static void OnBuyConfirmed(MCPurVehicle& purVehicle, int32_t result, int32_t quantity);

    /// <summary>The store vehicle (null once it is gone); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCPurVehicle> PurVehicle() const { return _PurVehicle.lock(); }
    /// <summary>The vehicle's diagram (0x1e x 0x1e) for the purchase dialog.</summary>
    std::shared_ptr<MCLogPort> PicturePort;
    /// <summary>The weight class by tonnage (cut at 14 characters).</summary>
    std::string WeightClassText;
    /// <summary>The armor rating by armor tonnage (cut at 14 characters).</summary>
    std::string ArmorText;

private:
    /// <summary>The store vehicle the block refers to (<see cref="PurVehicle"/>).</summary>
    std::weak_ptr<MCPurVehicle> _PurVehicle;
};

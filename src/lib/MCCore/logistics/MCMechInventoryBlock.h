#pragma once

#include "logistics/MCInventoryBlock.h"

class MCLogMech;

/// <summary>An owned mech in the inventory list: name, tonnage and a small damage diagram.</summary>
/// <remarks>Original source: <c>logistics\invblock.cpp</c> (<c>MechInventoryBlock</c>).</remarks>
class MCMechInventoryBlock : public MCInventoryBlock
{
public:
    ~MCMechInventoryBlock() override;

    /// <summary>Shows <paramref name="logMech"/>; takes its list position from the mech.</summary>
    void Init(std::shared_ptr<MCLogMech> logMech);

    /// <summary>Frees the diagram.</summary>
    void Destroy() override;

    /// <summary>Only draws the disabled state (the row itself is drawn by <see cref="DrawRow"/>).</summary>
    void Draw() override;

    /// <summary>
    /// Makes the damage diagram when it is missing. Port: the row is drawn by <see cref="DrawRow"/> (the original
    /// painted it here).
    /// </summary>
    void DrawBackground();

    /// <summary>Port: the mech's name and tonnage/class line, and the diagram.</summary>
    void DrawRow(MCLogPort& port, int32_t top) override;

    /// <summary>Port: the diagram, tonnage, classes, speed and description.</summary>
    void DrawInfo(MCLogPort& port) override;

    /// <summary>Frees the cached damage diagram so the next draw rebuilds it.</summary>
    void DeleteDiagram();

    /// <summary>Selection, drag to a drop slot or the store (a sale), and the info display.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// The sale dialog's answer: a confirmed sale moves the mech's undamaged weapons and equipment to the spare
    /// components and removes the mech; otherwise it goes back to the inventory.
    /// </summary>
    void OnSellConfirmed(int32_t result) const;

    /// <summary>The body locations the diagram shows.</summary>
    static constexpr int32_t DiagramLocations = 8;

    /// <summary>
    /// Port: the small damage diagram (0x1c x 0x1e) as <see cref="DrawBackground"/> made it: each location's damage
    /// state and the battle rating bar's length. The original kept a picture of it until
    /// <see cref="DeleteDiagram"/>; it is drawn in place from this (<see cref="PaintDiagram"/>), so with HD art on it
    /// shows from the pack.
    /// </summary>
    struct DiagramLook
    {
        std::array<int32_t, DiagramLocations> States{};
        int32_t Bar = 0;
    };

    std::optional<DiagramLook> Diagram;

    /// <summary>Draws the diagram into <paramref name="port"/> (its corner at 0, 0), as the original's picture held it.</summary>
    void PaintDiagram(MCLogPort& port) const;
    /// <summary>The mech (null once it is gone); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogMech> Mech() const { return _Mech.lock(); }

private:
    /// <summary>The mech the block refers to (<see cref="Mech"/>).</summary>
    std::weak_ptr<MCLogMech> _Mech;

    /// <summary>Offers the mech for sale (unless the mission requires it).</summary>
    void OfferSale();
};

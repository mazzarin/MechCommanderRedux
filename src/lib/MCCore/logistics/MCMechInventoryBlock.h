#pragma once

#include "logistics/MCInventoryBlock.h"

class MCLogMech;

/// <summary>
/// An owned mech's row in the inventory list (a row of <see cref="MCListView"/>): name, tonnage and a small damage
/// diagram.
/// </summary>
/// <remarks>Original source: <c>logistics\invblock.cpp</c> (<c>MechInventoryBlock</c>).</remarks>
class MCMechInventoryBlock : public MCInventoryBlock
{
public:
    /// <summary>Shows <paramref name="logMech"/>.</summary>
    void Init(std::shared_ptr<MCLogMech> logMech);

    /// <summary>Frees the diagram.</summary>
    void OnDestroy() override;

    /// <summary>Only draws the disabled state (the row itself is drawn by <see cref="DrawRow"/>).</summary>
    void Draw() override;

    /// <summary>
    /// Makes the damage diagram when it is missing (the original's <c>drawBackground</c>, which also painted the row).
    /// </summary>
    void OnRefresh() override;

    /// <summary>Port: the mech's name and tonnage/class line, and the diagram.</summary>
    void DrawRow(MCLogPort& port, int32_t top) override;

    /// <summary>Selection, drag to a drop slot or the store (a sale), and the info display.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// The sale dialog's answer for <paramref name="sold"/>: a confirmed sale moves the mech's undamaged weapons and
    /// equipment to the spare components and removes the mech; otherwise it goes back to the inventory.
    /// </summary>
    static void OnSellConfirmed(MCLogMech& sold, int32_t result);

    /// <summary>The body locations the diagram shows.</summary>
    static constexpr int32_t DiagramLocations = 8;

    /// <summary>
    /// Port: the small damage diagram (0x1c x 0x1e) as <see cref="OnRefresh"/> made it: each location's damage state
    /// and the battle rating bar's length. The original kept a picture of it; it is drawn in place from this
    /// (<see cref="PaintDiagram"/>), so with HD art on it shows from the pack.
    /// </summary>
    struct DiagramLook
    {
        std::array<int32_t, DiagramLocations> States{};
        int32_t Bar = 0;
    };

    std::optional<DiagramLook> Diagram;

    /// <summary>
    /// Draws <paramref name="mech"/>'s diagram as <paramref name="look"/> holds it into <paramref name="port"/> (its
    /// corner at 0, 0), as the original's picture held it.
    /// </summary>
    static void PaintDiagram(const MCLogMech& mech, const DiagramLook& look, MCLogPort& port);

    /// <summary>
    /// Port: <paramref name="mech"/>'s details in the info box: its <paramref name="diagram"/>, tonnage, classes,
    /// speed and description.
    /// </summary>
    static void DrawMechInfo(const MCLogMech& mech, MCOptionalRef<const DiagramLook> diagram, MCLogPort& port);
    /// <summary>The mech (null once it is gone); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogMech> Mech() const { return _Mech.lock(); }
    /// <summary>The mech's <see cref="MCLogPart::ListIndex"/>.</summary>
    int32_t ListIndex() const override;

private:
    /// <summary>The mech the block refers to (<see cref="Mech"/>).</summary>
    std::weak_ptr<MCLogMech> _Mech;

    /// <summary>
    /// Offers <paramref name="mech"/> for sale (unless the mission requires it), its <paramref name="diagram"/> in
    /// the dialog: both as the event that offers it took them, since its refresh can drop this row first.
    /// </summary>
    void OfferSale(const std::shared_ptr<MCLogMech>& mech, const std::optional<DiagramLook>& diagram);
};

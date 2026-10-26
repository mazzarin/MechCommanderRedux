#pragma once

#include "logistics/MCLogObject.h"

class MCGuiEvent;
class MCPurMech;

/// <summary>A row of the mech store (0x19a x 0x70): picture, diagram, class texts, the variant buttons and the price.</summary>
/// <remarks>Original source: <c>logistics\purchase.cpp</c> (<c>MechPurchaseBlock</c>).</remarks>
class MCMechPurchaseBlock : public MCLogObject
{
public:
    ~MCMechPurchaseBlock() override;

    /// <summary>Shows <paramref name="purMech"/> (variant <see cref="CurVariant"/>, set by the caller first).</summary>
    void Init(std::shared_ptr<MCPurMech> purMech);

    /// <summary>Frees the pictures and texts.</summary>
    void Destroy() override;

    /// <summary>The variant buttons, buying (a drag to the inventory) and the help line.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>Empty: the row is drawn by <see cref="DrawRow"/>.</summary>
    void Draw() override;

    /// <summary>
    /// Readies row <paramref name="row"/>: makes the picture and diagram when missing, the class texts and the
    /// description. Port: the row is drawn by <see cref="DrawRow"/> (the original painted it here).
    /// </summary>
    void DrawBackground(int32_t row);

    /// <summary>
    /// Port: draws the row into <paramref name="port"/> (the store's view) with its top at <paramref name="top"/>:
    /// what the original's <c>drawBackground</c> painted into the store's picture.
    /// </summary>
    void DrawRow(MCLogPort& port, int32_t top) const;

    /// <summary>
    /// Port: draws the drag icon's picture into <paramref name="surface"/>: a 0x20 square of the row, over the store's
    /// background.
    /// </summary>
    void OnBeginDrag(MCLogPort& surface);

    /// <summary>Empty.</summary>
    void SetBar();

    /// <summary>
    /// The purchase dialog's answer: a confirmed purchase of <paramref name="quantity"/> adds them to the inventory and
    /// takes them off the stock.
    /// </summary>
    void OnBuyConfirmed(int32_t result, int32_t quantity);

    /// <summary>The list position the row is drawn at.</summary>
    int32_t Row = 0;
    /// <summary>The store mech (null once it is gone); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCPurMech> PurMech() const { return _PurMech.lock(); }
    /// <summary>The shown variant's name index.</summary>
    int32_t NameIndex = 0;
    /// <summary>The variant shown (0..2), -1 before the shop list picks one.</summary>
    int32_t CurVariant = -1;
    /// <summary>
    /// Port: the name index of the mech the picture (0x4b x 100) and the diagram show, set by the first
    /// <see cref="DrawBackground"/> (the original made the pictures then, and kept them). The row draws them in place
    /// (<see cref="DrawRow"/>), so with HD art on they are drawn from the pack, sharp.
    /// </summary>
    std::optional<int32_t> PictureIndex;
    /// <summary>The small body diagram (0x1e x 0x1e) for the purchase dialog, made by the first <see cref="DrawBackground"/>.</summary>
    std::shared_ptr<MCLogPort> DiagramPort;
    /// <summary>The weight class by current tonnage.</summary>
    std::string WeightClassText;
    /// <summary>The armor rating by armor tonnage.</summary>
    std::string ArmorText;
    /// <summary>The internal structure rating by the total internal structure.</summary>
    std::string InternalText;

private:
    /// <summary>The store mech the block refers to (<see cref="PurMech"/>).</summary>
    std::weak_ptr<MCPurMech> _PurMech;
};

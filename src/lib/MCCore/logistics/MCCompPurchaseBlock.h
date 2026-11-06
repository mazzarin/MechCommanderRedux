#pragma once

#include "gui/MCListView.h"

class MCGuiEvent;
struct MCLogInventoryItem;

/// <summary>
/// A row of the component store (a row of the store's <see cref="MCListView"/>): picture, name, range, damage,
/// recycle time, stock and price.
/// </summary>
/// <remarks>Original source: <c>logistics\purchase.cpp</c> (<c>CompPurchaseBlock</c>).</remarks>
class MCCompPurchaseBlock : public MCListRow
{
public:
    /// <summary>Shows <paramref name="item"/>; builds its stat texts.</summary>
    void Init(std::shared_ptr<MCLogInventoryItem> item);

    /// <summary>Buying (a drag to the inventory) and the help line.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// Readies the description (the original's <c>drawBackground</c>, which also painted the row into the store's
    /// picture).
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
    /// The purchase dialog's answer for <paramref name="bought"/> (the store's item): a confirmed purchase moves
    /// <paramref name="quantity"/> from the store's stock to the spare components (a new one gets its inventory row)
    /// and pays for them.
    /// </summary>
    static void OnBuyConfirmed(MCLogInventoryItem& bought, int32_t result, int32_t quantity);

    /// <summary>The range: a short/medium/long word for weapons, "%.1f m" or "N/A" for equipment.</summary>
    std::string RangeText;
    /// <summary>The weight (string 0x27f, "%.1f tons").</summary>
    std::string WeightText;
    /// <summary>The recycle time with its rating word, or "N/A".</summary>
    std::string RecycleText;
    /// <summary>The damage with its rating word, or "N/A".</summary>
    std::string DamageText;
    /// <summary>The component (null once it is gone); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogInventoryItem> Item() const { return _Item.lock(); }

private:
    /// <summary>The component the block refers to (<see cref="Item"/>).</summary>
    std::weak_ptr<MCLogInventoryItem> _Item;
};

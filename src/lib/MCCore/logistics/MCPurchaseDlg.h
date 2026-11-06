#pragma once

#include "logistics/MCLogDialogBox.h"

/// <summary>
/// The buy/sell dialog: shows the unit cost, a quantity spinner and the resource points left, and calls
/// <see cref="PurchaseCallback"/> with the button and the quantity.
/// </summary>
/// <remarks>Original source: <c>logistics\logdlg.cpp</c> (<c>PurchaseDlg</c>).</remarks>
class MCPurchaseDlg : public MCLogDialogBox
{
public:
    /// <summary>The most a spinner goes to (a game rule; also the quantity when none is given).</summary>
    static constexpr int32_t MaxSpinnerQuantity = 199;
    /// <summary>The most a held arrow repeats up to.</summary>
    static constexpr int32_t MaxRepeatQuantity = 200;
    /// <summary>The spinner's repeat timer (its id, and every 200 ms).</summary>
    static constexpr int32_t RepeatTimer = 6;

    /// <summary>
    /// Sets up a purchase of <paramref name="purchaseType"/> (0/1 buy/sell mech, 2/3 pilot, 4/5 component, 6/7
    /// vehicle) at <paramref name="unitCost"/> each, at most <paramref name="maxQuantity"/> (negative =
    /// <see cref="MaxSpinnerQuantity"/>; 1 = no spinner), with two text lines and a copy of <paramref name="picture"/>.
    /// </summary>
    void Init(int32_t purchaseType, int32_t unitCost, int32_t maxQuantity, std::string_view title,
              std::string_view subtitle, MCOptionalRef<const MCGuiPort> picture);

    /// <summary>Frees the title and subtitle.</summary>
    void OnDestroy() override;

    /// <summary>The buttons, the spinner (with auto-repeat on its timer) and Enter/Escape.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>Port: draws the base box plus the texts, the costs, the quantity and the kind's picture.</summary>
    void Draw() override;

    /// <summary>Grabs the input, draws and shows the box.</summary>
    void Activate();

    /// <summary>Stops the spinner timer, hides the box and calls the callback with the result and the quantity.</summary>
    void Deactivate(int32_t result);

    /// <summary>The callback that also gets the quantity.</summary>
    void SetCallback(std::function<void(int32_t, int32_t)> newCallback) { PurchaseCallback = std::move(newCallback); }

    /// <summary>Whether one more can be bought: under the maximum, and the resource points pay for it.</summary>
    bool CanAddOne() const;

    /// <summary>The spinner's maximum.</summary>
    int32_t MaxQuantity = 0;
    /// <summary>What is bought or sold: 0/1 mech, 2/3 pilot, 4/5 component, 6/7 vehicle (odd = selling).</summary>
    int32_t PurchaseType = 0;
    /// <summary>The quantity chosen (starts at 1).</summary>
    int32_t Quantity = 0;
    /// <summary>The first text line.</summary>
    std::string Title;
    /// <summary>The second text line (red for a pilot sale).</summary>
    std::string Subtitle;
    /// <summary>Called by <see cref="Deactivate"/> with the result and <see cref="Quantity"/>.</summary>
    std::function<void(int32_t, int32_t)> PurchaseCallback;
    /// <summary>The cost of one (resource points).</summary>
    int32_t UnitCost = 0;

private:
    /// <summary>Which way the held spinner arrow counts (up, or down).</summary>
    bool _SpinUp = false;
};

#pragma once

#include "logistics/MCLogInvScreen.h"

class MCGuiEvent;

/// <summary>
/// The logistics purchase screen: the inventory on the left, the store (mechs, pilots, components, vehicles) on
/// the right; units are bought and sold by dragging between them.
/// </summary>
/// <remarks>Original source: <c>logistics\logpur.cpp</c> (<c>PurchaseScreen</c>).</remarks>
class MCPurchaseScreen : public MCLogInvScreen
{
public:
    /// <summary>Makes the full-screen object, the two panes and the store tab headers.</summary>
    void Init();

    /// <summary>Frees the tab pictures and the panes.</summary>
    void OnDestroy() override;

    /// <summary>Redraws the screen's background art (<c>lspbk00</c>).</summary>
    void DrawBackground();

    /// <summary>The tabs, the screen buttons and the help text for what the mouse is over.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>Shows or hides the screen and its panes.</summary>
    void ShowGuiWindow(bool show) override;

    /// <summary>
    /// How the store (the unit pane's list) lays out each tab, by inventory tab (mechs, pilots, components,
    /// vehicles); set by <c>MCLogInvScreen::CreatePurVehiclePane</c> and <c>RemovePilot</c>, where the original made
    /// each tab's picture.
    /// </summary>
    std::array<MCListLayout, 4> StoreLayouts{};
    /// <summary>The store's mech tab header (<c>lspbim00</c>).</summary>
    std::shared_ptr<MCLogPort> MechTabPort;
    /// <summary>The store's pilot tab header (<c>lspbip00</c>).</summary>
    std::shared_ptr<MCLogPort> PilotTabPort;
    /// <summary>The store's component tab header (<c>lspbic00</c>).</summary>
    std::shared_ptr<MCLogPort> CompTabPort;
    /// <summary>The store's vehicle tab header (<c>lspbiv00</c>).</summary>
    std::shared_ptr<MCLogPort> VehicleTabPort;

private:
    /// <summary>Puts the help line for (<paramref name="xPos"/>, <paramref name="yPos"/>) on the ticker.</summary>
    void ShowHelpFor(int32_t xPos, int32_t yPos);

    /// <summary>A left click at (<paramref name="xPos"/>, <paramref name="yPos"/>) of the screen: the buttons and tabs.</summary>
    void Click(int32_t xPos, int32_t yPos);

    /// <summary>The blink phase of the briefing button's highlight, flipped by each timer event.</summary>
    bool _BriefingBlink = false;
};

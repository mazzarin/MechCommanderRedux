#pragma once

#include "gui/MCListView.h"

class MCGuiEvent;
class MCPurPilotData;

/// <summary>
/// A row of the pilot hiring list (a row of the store's <see cref="MCListView"/>): portrait, name, rank, skills,
/// health and price.
/// </summary>
/// <remarks>Original source: <c>logistics\purchase.cpp</c> (<c>PilotPurchaseBlock</c>).</remarks>
class MCPilotPurchaseBlock : public MCListRow
{
public:
    void Init(std::shared_ptr<MCPurPilotData> pilot);

    /// <summary>Forgets the pilot shown.</summary>
    void OnDestroy() override;

    /// <summary>Hiring (a drag to the inventory) and the help line.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// Readies the description (the original's <c>drawBackground</c>, which also painted the row into the store's
    /// picture).
    /// </summary>
    void OnRefresh() override;

    /// <summary>
    /// Port: draws the row into <paramref name="port"/> (the store's content) with its top at <paramref name="top"/>:
    /// what the original's <c>drawBackground</c> painted into the store's picture. A hired pilot draws nothing.
    /// </summary>
    void DrawRow(MCLogPort& port, int32_t top) override;

    /// <summary>
    /// Port: draws the drag icon's picture into <paramref name="surface"/>: a 0x20 square of the row, over the pilot
    /// list's background.
    /// </summary>
    void OnBeginDrag(MCLogPort& surface);

    /// <summary>
    /// The hiring dialog's answer for <paramref name="pilot"/>: a confirmed hire adds the pilot to the inventory,
    /// takes it off the store and pays.
    /// </summary>
    static void OnHireConfirmed(MCPurPilotData& pilot, int32_t result);

    /// <summary>The pilot for hire (null once it is gone); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCPurPilotData> Pilot() const { return _Pilot.lock(); }

private:
    /// <summary>The pilot for hire the block refers to (<see cref="Pilot"/>).</summary>
    std::weak_ptr<MCPurPilotData> _Pilot;
};

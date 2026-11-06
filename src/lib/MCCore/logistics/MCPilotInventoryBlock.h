#pragma once

#include "logistics/MCInventoryBlock.h"

class MCLogMech;
class MCLogVehicle;
class MCLogWarrior;

/// <summary>An owned MechWarrior in the inventory list: portrait, callsign, rank and skills.</summary>
/// <remarks>Original source: <c>logistics\invblock.cpp</c> (<c>PilotInventoryBlock</c>).</remarks>
class MCPilotInventoryBlock : public MCInventoryBlock
{
public:
    /// <summary>Shows <paramref name="logWarrior"/> and loads the portrait.</summary>
    void Init(std::shared_ptr<MCLogWarrior> logWarrior);

    /// <summary>Frees the portrait.</summary>
    void OnDestroy() override;

    /// <summary>Only draws the disabled state.</summary>
    void Draw() override;

    /// <summary>
    /// Sets <see cref="GreyedOut"/> (the original's <c>drawBackground</c>, which also painted the row into the tab's
    /// picture and moved the block to its row; the list does those now).
    /// </summary>
    void OnRefresh() override;

    /// <summary>Port: the portrait, callsign and rank, darkened when <see cref="GreyedOut"/>.</summary>
    void DrawRow(MCLogPort& port, int32_t top) override;

    /// <summary>
    /// Port: <paramref name="warrior"/>'s skills, <paramref name="portrait"/>, rank, health and description in the info
    /// box (the box keeps the pilot and the portrait while it shows them, see <see cref="MCInvInfoBox::Details"/>).
    /// </summary>
    static void DrawPilotInfo(const MCLogWarrior& warrior, const MCLogPort& portrait, MCLogPort& port);

    /// <summary>Selection, drag onto a mech or the store (a sale), and the info display.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// The sale dialog's answer for <paramref name="warrior"/>: a confirmed sale lets the pilot go (marked sold back in
    /// the store); otherwise the pilot goes back to the inventory.
    /// </summary>
    static void OnSellConfirmed(MCLogWarrior& warrior, int32_t result);

    /// <summary>The pilot (null once it is gone); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogWarrior> Warrior() const { return _Warrior.lock(); }
    /// <summary>
    /// Set by <see cref="OnRefresh"/> on the repair screen when the selected mech can't take a pilot; blocks dragging.
    /// </summary>
    bool GreyedOut = false;
    /// <summary>The pilot's portrait (<c>logart\&lt;picture&gt;</c>).</summary>
    std::shared_ptr<MCLogPort> PortraitPort;
    /// <summary>The pilot's <see cref="MCLogWarrior::ListIndex"/>.</summary>
    int32_t ListIndex() const override;

private:
    /// <summary>The pilot the block refers to (<see cref="Warrior"/>).</summary>
    std::weak_ptr<MCLogWarrior> _Warrior;

    /// <summary>Opens the sale dialog for the pilot.</summary>
    void OfferSale();

    /// <summary>
    /// Gives <paramref name="warrior"/> (this row's pilot) to <paramref name="mech"/> (force row <paramref name="row"/>)
    /// and says so. Takes the pilot rather than reading this row's: the list it refills can take the row away.
    /// </summary>
    static void BoardMech(MCLogWarrior& warrior, MCLogMech& mech, int32_t row);

    /// <summary>
    /// Puts <paramref name="warrior"/> (this row's pilot) back in the inventory with the drop sound (fine over the
    /// inventory, refused elsewhere). Takes the pilot, as <see cref="BoardMech"/>.
    /// </summary>
    static void BackToInventory(MCLogWarrior& warrior, MCGuiEvent& event);
};

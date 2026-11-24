#pragma once

// Port-only (SPEC-menus.md's stage 8): PURCHASING's rules, the original's (logistics\logpur.cpp's purchase blocks and
// inventory blocks), in one place both the 1998 purchase screen's blocks and the remade PURCHASING
// (logistics/MCPurchasing) call: what a thing costs and pays back, how many may be bought, why one can't be, and the
// purchases and sales themselves (the force's lists, the store's stock and the resource points changed as the
// original's confirmed dialogs did). The sounds, the words and the screens' rows are the caller's.

class MCLogMech;
class MCLogVehicle;
class MCLogWarrior;
class MCPurMech;
class MCPurPilotData;
class MCPurVehicle;
struct MCLogInventoryItem;

namespace MCStore
{
    /// <summary>Why something can't be bought or sold.</summary>
    enum class MCRefusal : uint8_t
    {
        /// <summary>It can.</summary>
        None,
        /// <summary>The resource points don't pay for one (the original's message 0x4d).</summary>
        TooPoor,
        /// <summary>The player owns as many mechs and vehicles as the game allows (<c>CheckMaxUnits</c>).</summary>
        Full,
        /// <summary>The store has none left.</summary>
        SoldOut,
        /// <summary>The mission requires the unit: the original never offers its sale.</summary>
        Required,
        /// <summary>Port-only: the unit is in the force; only a spare sells (the original's inventory alone sold).</summary>
        InForce
    };

    /// <summary>
    /// What a sale pays for something worth <paramref name="value"/>: half in a campaign, all of it in a match or a solo
    /// mission, as the original's.
    /// </summary>
    int32_t SalePrice(int32_t value);

    /// <summary>
    /// How many of a mech or vehicle may be bought with <paramref name="available"/> in the store: the room under the
    /// unit limit, or the stock when that is less (<c>MaxPurchase</c>).
    /// </summary>
    int32_t MostUnits(int32_t available);

    /// <summary>Whether <paramref name="each"/> resource points pay for one more after <paramref name="quantity"/>.</summary>
    bool PaysFor(int32_t each, int32_t quantity);

    /// <summary>Why <paramref name="purMech"/>'s shown variant can't be bought, without the unit limit's warning.</summary>
    MCRefusal WhyNotBuy(const MCPurMech& purMech);

    /// <summary>Why <paramref name="pilot"/> can't be hired.</summary>
    MCRefusal WhyNotHire(const MCPurPilotData& pilot);

    /// <summary>Why the store's <paramref name="part"/> can't be bought.</summary>
    MCRefusal WhyNotBuy(const MCLogInventoryItem& part);

    /// <summary>Why <paramref name="purVehicle"/> can't be bought, without the unit limit's warning.</summary>
    MCRefusal WhyNotBuy(const MCPurVehicle& purVehicle);

    /// <summary>What <paramref name="mech"/> sells for (its value worked out again first, as the original's offer did).</summary>
    int32_t SalePrice(MCLogMech& mech);

    /// <summary>What <paramref name="warrior"/>'s dismissal pays back (their rank's cost).</summary>
    int32_t SalePrice(const MCLogWarrior& warrior);

    /// <summary>What a spare <paramref name="part"/> sells for.</summary>
    int32_t SalePrice(const MCLogInventoryItem& part);

    /// <summary>What <paramref name="vehicle"/> sells for.</summary>
    int32_t SalePrice(const MCLogVehicle& vehicle);

    /// <summary>
    /// Buys <paramref name="quantity"/> of <paramref name="purMech"/>'s shown variant at <paramref name="each"/>: they
    /// join the spare mechs, the store's stock goes down and the resource points pay.
    /// </summary>
    void BuyMechs(MCPurMech& purMech, int32_t quantity, int32_t each);

    /// <summary>
    /// Hires <paramref name="pilot"/> at <paramref name="cost"/>: they join the spare MechWarriors, leave the store
    /// (hired) and the resource points pay.
    /// </summary>
    void HirePilot(MCPurPilotData& pilot, int32_t cost);

    /// <summary>What a purchase of parts did to the inventory's rows.</summary>
    enum class MCPartRow : uint8_t
    {
        /// <summary>The spare part's row is new, or back from none: the inventory is listed again.</summary>
        Listed,
        /// <summary>The spare part's count went up: its rows take it.</summary>
        Counted
    };

    /// <summary>
    /// Buys <paramref name="quantity"/> of the store's <paramref name="part"/> at <paramref name="each"/>: the store's
    /// count goes down, the spare part's up (a new spare made for the first) and the resource points pay.
    /// </summary>
    /// <param name="spare">The spare part bought into.</param>
    MCPartRow BuyParts(MCLogInventoryItem& part, int32_t quantity, int32_t each,
                       std::shared_ptr<MCLogInventoryItem>& spare);

    /// <summary>
    /// Buys <paramref name="quantity"/> of <paramref name="purVehicle"/> at <paramref name="each"/>: they join the spare
    /// vehicles, the store's stock goes down and the resource points pay.
    /// </summary>
    void BuyVehicles(MCPurVehicle& purVehicle, int32_t quantity, int32_t each);

    /// <summary>
    /// Sells <paramref name="sold"/> for <paramref name="price"/>: its undamaged weapons and equipment go to the spare
    /// parts, the mech leaves the player's lists and the resource points take the price.
    /// </summary>
    void SellMech(MCLogMech& sold, int32_t price);

    /// <summary>
    /// Dismisses <paramref name="warrior"/> for <paramref name="price"/>: sold back to the store's list, out of the
    /// player's MechWarriors, the resource points taking the price. The caller says the pilot's line.
    /// </summary>
    void FirePilot(MCLogWarrior& warrior, int32_t price);

    /// <summary>Sells <paramref name="quantity"/> of the spare <paramref name="sold"/> at <paramref name="each"/>.</summary>
    /// <returns>Whether none are left (the inventory is listed again).</returns>
    bool SellParts(MCLogInventoryItem& sold, int32_t quantity, int32_t each);

    /// <summary>Sells <paramref name="sold"/> for <paramref name="price"/>: it leaves the player's lists.</summary>
    void SellVehicle(MCLogVehicle& sold, int32_t price);
}

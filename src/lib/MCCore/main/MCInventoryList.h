#pragma once

#include "gui/MCGuiRef.h"
#include "lib/MCOptionalRef.h"

// Original source: mcx\logistics.cpp (InventoryList and its records).

/// <summary>
/// One copy of a component in an inventory: where it sits and its state. The copies of one component type are the
/// <see cref="MCLogInventoryItem::Stats"/> of their item.
/// </summary>
struct MCLogInventoryStat
{
    /// <summary>A number unique within the inventory (<see cref="MCInventoryList::NextStatID"/>).</summary>
    uint8_t StatID = 0;
    /// <summary>Its damage (<see cref="MCInventoryList::HitItem"/>).</summary>
    uint8_t Hits = 0;
    /// <summary>Nonzero when the weapon faces forward (the mech file's FacesForward).</summary>
    uint8_t Facing = 0;
    /// <summary>The amount (ammunition count, or 1).</summary>
    int16_t Amount = 0;
    /// <summary>The body location it is mounted in (0xff = none; <see cref="MCInventoryList::SetStatLoc"/>).</summary>
    uint8_t Location = 0;
    /// <summary>The item number in the mech/vehicle file.</summary>
    int32_t ItemNum = 0;
};

/// <summary>
/// A component type in an inventory: its master component, how many copies there are (the <see cref="Stats"/>) and
/// where the inventory lists it. The purchase and inventory screens' lists make their own rows for it.
/// </summary>
struct MCLogInventoryItem : std::enable_shared_from_this<MCLogInventoryItem>
{
    MCLogInventoryItem();
    MCLogInventoryItem(const MCLogInventoryItem&) = delete;
    MCLogInventoryItem& operator=(const MCLogInventoryItem&) = delete;

    /// <summary>The index into <c>MasterComponentList</c>.</summary>
    uint8_t MasterID = 0;
    /// <summary>A copy of the master component's id.</summary>
    int32_t MasterValue = 0;
    /// <summary>The component's place in the logistics sort order (<see cref="MCLogistics::ComponentSort"/>).</summary>
    int32_t SortOrder = 0;
    /// <summary>The master component's name (at most 28 characters).</summary>
    std::string Name;
    /// <summary>
    /// How many copies there are. Usually the number of <see cref="Stats"/>, but ammunition counts as one, and
    /// <see cref="MCInventoryList::AddCountToItem"/> changes the count alone.
    /// </summary>
    int32_t Count = 0;
    /// <summary>The index of the component in <see cref="MCLogistics::RangeSortList"/>.</summary>
    int32_t RangeIndex = 0;
    /// <summary>The description text (loaded on demand by <see cref="MCInventoryList::LoadDescription"/>; empty until then).</summary>
    std::string Description;
    /// <summary>The copies.</summary>
    std::vector<std::shared_ptr<MCLogInventoryStat>> Stats;
    /// <summary>
    /// The item's index in the spare components (set when its inventory row is made); <see
    /// cref="MCLogistics::ReIndexInventory"/> gives out the rows in this order.
    /// </summary>
    int32_t InventoryIndex = 0;
    /// <summary>The row of the spare components list the item is shown at (-1 = none: no copies left).</summary>
    int32_t ListIndex = 0;
};

/// <summary>A list of components, from the highest master id down, each with its copies.</summary>
/// <remarks>Original source: <c>logistics.cpp</c> (<c>InventoryList</c>).</remarks>
class MCInventoryList
{
public:
    MCInventoryList() = default;
    ~MCInventoryList();
    MCInventoryList(const MCInventoryList&) = delete;
    MCInventoryList& operator=(const MCInventoryList&) = delete;

    /// <summary>
    /// Lists <paramref name="item"/> as a spare component: it starts on the inventory list's first row until the
    /// inventory is reindexed (as the original's new inventory block did; the lists make the rows now).
    /// </summary>
    static void ListAsSpare(MCLogInventoryItem& item);

    /// <summary>
    /// Loads the description of <paramref name="item"/>, or of the item at <paramref name="index"/> when it is null,
    /// from the object description file (once).
    /// </summary>
    void LoadDescription(int32_t index, MCOptionalRef<MCLogInventoryItem> item) const;

    /// <summary>Makes a copy record numbered with the next <see cref="NextStatID"/>.</summary>
    std::shared_ptr<MCLogInventoryStat> CreateStat(uint8_t itemNum, uint8_t hits, uint8_t facing, int16_t amount,
                                                   uint8_t location);

    /// <summary>The item at list position <paramref name="index"/>, or null.</summary>
    std::shared_ptr<MCLogInventoryItem> GetItemInfo(int32_t index) const;

    /// <summary>Adds <paramref name="count"/> to the count of the item with master id <paramref name="masterID"/> (not below 0).</summary>
    void AddCountToItem(int32_t count, int32_t masterID);

    /// <summary>Frees every item and its copies.</summary>
    void Clear();

    /// <summary>
    /// Adds copy <paramref name="stat"/> of component <paramref name="masterID"/>; a new item is listed (its inventory
    /// index and first row, where the original made its purchase and inventory widgets) when
    /// <paramref name="widgets"/> is set.
    /// </summary>
    /// <returns>The copy's stat id.</returns>
    int32_t AddItem(uint8_t masterID, std::shared_ptr<MCLogInventoryStat> stat, bool widgets);

    /// <summary>
    /// Removes copy <paramref name="statID"/> (the first for -1) of component <paramref name="masterID"/>; the last
    /// copy takes the item with it, whichever copy was asked for.
    /// </summary>
    /// <returns>0 when the item went; -1 otherwise, also when a copy was removed (the original's result).</returns>
    int32_t RemoveItem(uint8_t masterID, int32_t statID);

    /// <summary>The item holding the copy with stat id <paramref name="statID"/>, or null.</summary>
    std::shared_ptr<MCLogInventoryItem> GetItemStatIndex(int32_t statID) const;

    /// <summary>The stat id of copy <paramref name="copy"/> of component <paramref name="masterID"/>, or -1.</summary>
    int32_t GetItemStatID(uint8_t masterID, int32_t copy) const;

    /// <summary>The list position of component <paramref name="masterID"/>, or -1.</summary>
    int32_t GetIndexFromMasterID(uint8_t masterID) const;

    /// <summary>The master id of the item at list position <paramref name="index"/>, or 0xff.</summary>
    int32_t GetMasterIDFromIndex(int32_t index) const;

    /// <summary>The master id of the item holding copy <paramref name="statID"/>, or 0xff.</summary>
    uint8_t GetMasterID(uint8_t statID) const;

    /// <summary>The facing of copy <paramref name="statID"/>, or 0xff.</summary>
    uint8_t GetFacing(uint8_t statID) const;

    /// <summary>How many copies of component <paramref name="masterID"/> there are.</summary>
    int32_t GetItemCount(uint8_t masterID) const;

    /// <summary>Sets the damage of copy <paramref name="statID"/>.</summary>
    /// <returns>0, or -1 when there is no such copy.</returns>
    int32_t HitItem(uint8_t statID, uint8_t hits);

    /// <summary>Sets the location of copy <paramref name="statID"/>.</summary>
    /// <returns>0, or -1 when there is no such copy.</returns>
    int32_t SetStatLoc(uint8_t statID, int32_t location);

    /// <summary>The number of items.</summary>
    int32_t NumItems() const { return static_cast<int32_t>(Items.size()); }

    /// <summary>The items, from the highest master id down.</summary>
    std::vector<std::shared_ptr<MCLogInventoryItem>> Items;
    /// <summary>The stat id the next copy gets.</summary>
    uint8_t NextStatID = 0;

private:
    /// <summary>A copy found by <see cref="FindStat"/> and the item it is a copy of.</summary>
    struct FoundStat
    {
        std::shared_ptr<MCLogInventoryItem> Item;
        std::shared_ptr<MCLogInventoryStat> Stat;
    };

    /// <summary>
    /// The copy with stat id <paramref name="statID"/> and its item, walking the items and, of each, its first
    /// <c>Count</c> copies, as the stat lookups did (none when no copy has it).
    /// </summary>
    std::optional<FoundStat> FindStat(uint8_t statID) const;

    /// <summary>The item with master id <paramref name="masterID"/> (the walk stops at the first lower id), or null.</summary>
    std::shared_ptr<MCLogInventoryItem> FindItem(uint8_t masterID) const;
};

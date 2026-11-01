#pragma once

#include "gui/MCGuiOwned.h"
// Original source: mcx\logistics.cpp (LogMech).

#include "main/MCLogPart.h"

class MCMechBriefBlock;
class MCMechInventoryBlock;
class MCMechRepairBlock;
class MCLogWarrior;

/// <summary>A mech in logistics: armor, internals, the critical-slot layout, its pilot and its screen widgets.</summary>
/// <remarks>Original source: <c>logistics.cpp</c>, 600 (0x258) bytes.</remarks>
class MCLogMech : public MCLogPart
{
public:
    /// <summary>The armor locations: head, center/left/right torso, left/right arm, left/right leg, rear center/left/right torso.</summary>
    static constexpr size_t NumArmorLocations = 11;
    /// <summary>The body locations (the armor locations without the rear torsos).</summary>
    static constexpr size_t NumBodyLocations = 8;
    /// <summary>The most critical slots a body location has (<c>NumLocationCriticalSpaces</c>).</summary>
    static constexpr size_t MaxCriticalSlots = 12;
    /// <summary>The critical slot of an empty <see cref="ItemSlot"/>.</summary>
    static constexpr uint8_t EmptySlot = 0xff;
    /// <summary>
    /// The first weapon's master id: <see cref="PlaceItem"/> places the components below it by form. Desperate
    /// Measures' Rail Gun (98) and Light Gauss Rifle (99) come before the original game's first weapon (100).
    /// </summary>
    static constexpr uint8_t FirstWeaponID = 98;

    /// <summary>Armor points of a body location: the maximum and the current.</summary>
    struct ArmorPoints
    {
        uint8_t MaxArmor = 0;
        uint8_t CurArmor = 0;
    };

    /// <summary>
    /// A critical slot's occupant, as a profile's <c>Component:n</c> pair: <see cref="Row"/> is the copy's item number
    /// (0xff = empty), <see cref="Column"/> its damage, then the component.
    /// </summary>
    struct ItemSlot
    {
        uint8_t Row = 0;
        uint8_t Column = 0;
        uint8_t MasterID = 0;
    };

    MCLogMech();
    /// <summary>Frees the inventory and the widgets in the original's order.</summary>
    ~MCLogMech() override;

    /// <summary>The pilot modifier: the pilot's rank against the mech's weight class.</summary>
    int32_t CalcPilotModifier();

    /// <summary>
    /// Recomputes <see cref="MCLogPart::ResourcePoints"/> from the chassis, the components and the damage; weapons,
    /// ammunition and equipment count only when <paramref name="repaired"/>.
    /// </summary>
    void CalcMechCost(bool repaired);

    /// <summary>Recomputes the battle rating from the components.</summary>
    int32_t CalcBR();

    /// <summary>
    /// Puts copy <paramref name="itemNum"/> (damage <paramref name="hits"/>) of component <paramref name="masterID"/>
    /// in the first free critical slot of the location its form goes to.
    /// </summary>
    void PlaceItem(uint8_t masterID, int32_t itemNum, int32_t hits);

    /// <summary>Whether weapon <paramref name="masterID"/> takes a large slot.</summary>
    static bool GetWeaponLarge(uint8_t masterID);

    /// <summary>The large weapons in body location <paramref name="location"/>.</summary>
    int32_t GetLargeWeaponCount(int32_t location) const;

    /// <summary>The small weapons (and ammunition bins) in body location <paramref name="location"/>.</summary>
    int32_t GetSmallWeaponCount(int32_t location) const;

    /// <summary>Loads the description of <see cref="MCLogPart::DescIndex"/> (once; none for a negative <paramref name="descIndex"/>).</summary>
    void LoadDescription(int32_t descIndex);

    /// <summary>The mech's condition (0..1) from armor, internals and pilot.</summary>
    float CalcStatus();

    /// <summary>The internal structure's class name (string table).</summary>
    std::string ExtraName1;
    /// <summary>The jump jets' class name (string table).</summary>
    std::string ExtraName2;
    /// <summary>The name from the mech file.</summary>
    std::string MechName;
    /// <summary>The tonnage used by the chassis, engine and components.</summary>
    float UsedTonnage = 0;
    /// <summary>The tonnage left for components.</summary>
    float FreeTonnage = 0;
    /// <summary>The tonnage of the weapons and ammunition.</summary>
    float WeaponTonnage = 0;
    /// <summary>The pilot's index in the assigned warrior list (-1 = none).</summary>
    int32_t PilotIndex = 0;
    /// <summary>The name variant (0..2; picks the sort key and the multiplayer variant).</summary>
    int32_t NameVariant = 0;
    int32_t SellValue = 0;
    /// <summary>The key the mech list is sorted by (<c>MechSort[nameIndex] * 3 + variant</c>).</summary>
    int32_t SortKey = 0;
    uint8_t MaxRunSpeed = 0;
    /// <summary>The armor of the eleven armor locations.</summary>
    std::array<ArmorPoints, NumArmorLocations> Armor{};
    /// <summary>Nonzero where a body location has CASE.</summary>
    std::array<int32_t, NumBodyLocations> HasCase{};
    /// <summary>Internal structure of the eight body locations: the chassis maximum and the current.</summary>
    std::array<ArmorPoints, NumBodyLocations> Internals{};
    /// <summary>The critical slot grid of the eight body locations (0xff = empty).</summary>
    std::array<std::array<ItemSlot, MaxCriticalSlots>, NumBodyLocations> ItemSlots{};
    /// <summary>The hot spot of each body location on the damage diagram.</summary>
    std::array<uint8_t, NumBodyLocations> HotSpotNumber{};
    /// <summary>The chassis battle rating.</summary>
    int32_t ChassisBR = 0;
    int32_t PilotModifier = 0;
    /// <summary>The condition from <see cref="CalcStatus"/>.</summary>
    float StatusValue = 0;
    /// <summary>The row on the repair screen (null when read without widgets).</summary>
    MCGuiOwned<MCMechRepairBlock> RepairBlock;
    /// <summary>The row on the inventory screens (null when read without widgets).</summary>
    MCGuiOwned<MCMechInventoryBlock> InventoryBlock;
    /// <summary>The block in the briefing screen's deploy pane or a drop slot (<see cref="MCMechBriefBlock::Create"/>).</summary>
    MCGuiOwned<MCMechBriefBlock> BriefBlock;
    /// <summary>The pilot of a multiplayer mech received over the network (in <see cref="MCLogistics::MpWarriorList"/>).</summary>
    std::weak_ptr<MCLogWarrior> NetworkPilot;
};

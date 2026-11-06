#pragma once

class MCInventoryList;

/// <summary>The maximum and current armor of one body location of a mech for sale.</summary>
struct MCPurArmorLocation
{
    uint8_t MaxArmor = 0;
    uint8_t CurArmor = 0;
};

/// <summary>One critical slot of a body location of a mech for sale: the component and its damage.</summary>
struct MCPurCriticalSlot
{
    /// <summary>The copy's item number (the profile's Component:n first value), 0xff when empty.</summary>
    uint8_t MasterId = 0xff;
    /// <summary>Hits taken (passed to <c>MCInventoryList::HitItem</c>).</summary>
    uint8_t Damage = 0xff;
};

/// <summary>One variant of a mech offered for sale, read from its profile (<c>&lt;name&gt;.fit</c>).</summary>
/// <remarks>Original source: <c>logistics\purchase.cpp</c> (<c>PurMechData</c>).</remarks>
class MCPurMechData
{
public:
    /// <summary>Loads the description into <see cref="Description"/> once, when <paramref name="descIndex"/> is one.</summary>
    void LoadDescription(int32_t descIndex);

    /// <summary>Recomputes <see cref="BattleRating"/> from <see cref="ChassisBR"/> and the inventory.</summary>
    int32_t CalcBR();

    /// <summary>The profile's file name (without extension).</summary>
    std::string FileName;
    /// <summary>The display name (string table entry <c>DescIndex + 300</c>, cut at 39 characters).</summary>
    std::string Name;
    float CurTonnage = 0.0f;
    int32_t NameIndex = 0;
    /// <summary>The price in resource points: the file's ResourcePoints plus armor and internal structure.</summary>
    int32_t Cost = 0;
    uint8_t MaxRunSpeed = 0;
    /// <summary>The Armor block's Tonnage.</summary>
    float ArmorTonnage = 0.0f;
    /// <summary>Head, CenterTorso, LeftTorso, RightTorso, LeftArm, RightArm, LeftLeg, RightLeg, then the three rear torsos.</summary>
    std::array<MCPurArmorLocation, 11> Armor = {};
    uint8_t NumOther = 0;
    uint8_t NumWeapons = 0;
    uint8_t NumAmmo = 0;
    /// <summary>The current internal structure of the 8 body locations.</summary>
    std::array<uint8_t, 8> CurInternalStructure = {};
    /// <summary>
    /// The critical slots of the 8 body locations (the profile's layout: a location has up to 12, as
    /// <c>NumLocationCriticalSpaces</c> says).
    /// </summary>
    std::array<std::array<MCPurCriticalSlot, 12>, 8> CriticalSlots = {};
    /// <summary>How many of this variant the shop has.</summary>
    int32_t NumAvailable = 0;
    int32_t BattleRating = 0;
    /// <summary>The file's ChassisBR (100 when missing).</summary>
    int32_t ChassisBR = 0;
    /// <summary>The components the mech carries.</summary>
    std::unique_ptr<MCInventoryList> Inventory;
    /// <summary>The description text; empty for none.</summary>
    std::string Description;
    int32_t DescIndex = -1;
};

/// <summary>A mech type for sale: its three variants (A, W, J) and the one the store shows.</summary>
/// <remarks>Original source: <c>logistics\purchase.cpp</c> (<c>PurMech</c>).</remarks>
class MCPurMech
{
public:
    /// <summary>The variants of a mech type (a game rule: A, W and J).</summary>
    static constexpr size_t NumVariants = 3;

    std::array<std::unique_ptr<MCPurMechData>, NumVariants> Variants;
    /// <summary>
    /// The variant the store shows and sells (0..2; -1 before the list picks one): the first in stock when the
    /// list is made or the pane filled, or the one the player clicked.
    /// </summary>
    int32_t CurVariant = -1;
};

/// <summary>The mechs of the shop, newest first.</summary>
/// <remarks>Original source: <c>logistics\purchase.cpp</c> (<c>PurMechList</c>, a linked list).</remarks>
class MCPurMechList
{
public:
    /// <summary>Frees every mech and its variants.</summary>
    void Clear();

    /// <summary>
    /// Adds a mech type with its three variants (files and how many of each are for sale) and shows the first
    /// variant with stock.
    /// </summary>
    void AddMech(std::string_view fileA, int32_t countA, std::string_view fileJ, int32_t countJ, std::string_view fileW,
                 int32_t countW);

    /// <summary>
    /// Adds to the stock of the mech type whose first variant is <paramref name="fileName"/> (never below 0).
    /// </summary>
    /// <returns>0, or -1 when there is none.</returns>
    int32_t ModMech(std::string_view fileName, int32_t deltaA, int32_t deltaJ, int32_t deltaW);

    /// <summary>The <paramref name="index"/>th mech.</summary>
    /// <returns>0, or -1 past the end.</returns>
    int32_t GetMechInfo(int32_t index, std::shared_ptr<MCPurMech>& purMech);

    int32_t GetMechCount() const { return static_cast<int32_t>(Mechs.size()); }

    /// <summary>Reads profile <paramref name="fileName"/> into a variant.</summary>
    static std::unique_ptr<MCPurMechData> ReadVariant(std::string_view fileName);

    /// <summary>The mechs, newest first.</summary>
    std::vector<std::shared_ptr<MCPurMech>> Mechs;
};

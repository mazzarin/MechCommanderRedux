#pragma once

class MCInventoryList;

/// <summary>A vehicle type for sale, read from its profile.</summary>
/// <remarks>Original source: <c>logistics\purchase.cpp</c> (<c>PurVehicleData</c>).</remarks>
class MCPurVehicleData
{
public:
    /// <summary>Loads the description into <see cref="Description"/> once, when <paramref name="descIndex"/> is one.</summary>
    void LoadDescription(int32_t descIndex);

    /// <summary>The profile's file name.</summary>
    std::string FileName;
    /// <summary>The display name (string table entry <c>DescIndex + 700</c>).</summary>
    std::string Name;
    float CurTonnage = 0.0f;
    int32_t NameIndex = 0;
    /// <summary>The file's ResourcePoints (100 when missing).</summary>
    int32_t BaseCost = 0;
    /// <summary><see cref="BaseCost"/> plus the components (<see cref="MCPurVehicle::CalcVehicleCost"/>).</summary>
    int32_t Cost = 0;
    int32_t DescIndex = -1;
    /// <summary>The description text; empty for none.</summary>
    std::string Description;
    uint8_t MaxMoveSpeed = 0;
    /// <summary>The Armor block's Tonnage.</summary>
    float ArmorTonnage = 0.0f;
    uint8_t NumOther = 0;
    uint8_t NumWeapons = 0;
    uint8_t NumAmmo = 0;
    int32_t NumAvailable = 0;
    std::unique_ptr<MCInventoryList> Inventory;
};

/// <summary>A vehicle type for sale.</summary>
/// <remarks>Original source: <c>logistics\purchase.cpp</c> (<c>PurVehicle</c>).</remarks>
class MCPurVehicle
{
public:
    /// <summary>Sets <see cref="MCPurVehicleData::Cost"/> from the base cost and the components.</summary>
    void CalcVehicleCost() const;

    std::unique_ptr<MCPurVehicleData> Data;
};

/// <summary>The vehicles of the shop, sorted by tonnage.</summary>
/// <remarks>Original source: <c>logistics\purchase.cpp</c> (<c>PurVehicleList</c>, a linked list).</remarks>
class MCPurVehicleList
{
public:
    /// <summary>Frees every vehicle and its data.</summary>
    void Clear();

    /// <summary>Adds <paramref name="delta"/> to the stock of vehicle <paramref name="fileName"/> (never below 0).</summary>
    /// <returns>0, or -1 when there is none.</returns>
    int32_t ModVehicle(std::string_view fileName, int32_t delta);

    /// <summary>
    /// Reads vehicle profile <paramref name="fileName"/> with <paramref name="count"/> for sale and inserts it in tonnage order (before the first as heavy or heavier).
    /// </summary>
    /// <remarks>
    /// Port fix: a file without a General block is a raw record the original read over the 0xc-byte
    /// <c>PurVehicle</c> (0xd0 bytes per record) and then never added; the port just leaves it out.
    /// </remarks>
    void AddVehicle(std::string_view fileName, int32_t count);

    /// <returns>0, or -1 past the end.</returns>
    int32_t GetVehicleInfo(int32_t index, std::shared_ptr<MCPurVehicle>& vehicle);

    int32_t GetVehicleCount() const { return static_cast<int32_t>(Vehicles.size()); }

    /// <summary>The vehicles, lightest first.</summary>
    std::vector<std::shared_ptr<MCPurVehicle>> Vehicles;
};

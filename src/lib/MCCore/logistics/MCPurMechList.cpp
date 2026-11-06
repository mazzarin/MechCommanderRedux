#include "stdafx.h"
#include "logistics/MCPurMechList.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGamePaths.h"
#include "logistics/MCPurProfile.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "object/MCMasterComponent.h"
#include "object/MCMechGameSystem.h"

namespace
{
    /// <summary>The body location blocks of a mech profile, in location order.</summary>
    constexpr std::array<std::string_view, 8> BodyLocationNames = {"Head",    "CenterTorso", "LeftTorso", "RightTorso",
                                                                   "LeftArm", "RightArm",    "LeftLeg",   "RightLeg"};

    /// <summary>The armor locations of a mech profile's MaxArmorPoints and CurArmorPoints blocks.</summary>
    constexpr std::array<std::string_view, 11> ArmorLocationNames = {
        "Head",    "CenterTorso", "LeftTorso",       "RightTorso",    "LeftArm",       "RightArm",
        "LeftLeg", "RightLeg",    "RearCenterTorso", "RearLeftTorso", "RearRightTorso"};

    /// <summary>Each armor point adds 40 resource points to a mech's price.</summary>
    constexpr int32_t ArmorPointCost = 0x28;

    /// <summary>Each internal structure point adds 50 resource points.</summary>
    constexpr float InternalPointCost = 50.0f;
}

auto MCPurMechList::Clear() -> void
{
    Mechs.clear();
}

auto MCPurMechList::ReadVariant(std::string_view fileName) -> std::unique_ptr<MCPurMechData>
{
    MCFitIniFile file;
    OpenProfile(file, ProfilePath, fileName, true, " could not open mech file ");

    auto data = std::make_unique<MCPurMechData>();
    data->FileName = fileName;
    data->Inventory = std::make_unique<MCInventoryList>();

    int32_t result = file.SeekBlock("Header");
    Assert(result == 0, result, "Could not find header in mech file");
    const std::string fileType =
        ReadProfileString(file, "FileType", 0x14, "Could not find filetype string in mech file");
    Assert(fileType == "MechProfile", 0, "File is not a mech file");
    result = file.SeekBlock("General");
    Assert(result == 0, result, "Could not find general block in mech file");
    result = file.ReadIdFloat("CurTonnage", data->CurTonnage);
    Assert(result == 0, result, "Could not find curTonnage in mech file");
    // The type name is checked only (the display name comes from the string table below).
    ReadProfileString(file, "MechType", 0x28, "Could not read MechType in mech file");
    result = file.ReadIdLong("NameIndex", data->NameIndex);
    Assert(result == 0, result, " AddPurMech: could not find NameIndex ");

    if (file.ReadIdLong("ResourcePoints", data->Cost) != 0)
    {
        data->Cost = 100;
    }

    if (file.ReadIdLong("ChassisBR", data->ChassisBR) != 0)
    {
        data->ChassisBR = 100;
    }

    file.ReadIdLong("DescIndex", data->DescIndex);
    data->LoadDescription(data->DescIndex);
    // The display name replaces the MechType read above.
    data->Name = LoadGameString(static_cast<uint32_t>(data->DescIndex + 300), 0x28);

    result = file.SeekBlock("Engine");
    Assert(result == 0, result, "Could not find Engine block in mech file");
    result = file.ReadIdUChar("MaxRunSpeed", data->MaxRunSpeed);
    Assert(result == 0, result, "Could not read MaxRunSpeed in mech file");
    result = file.SeekBlock("Armor");
    Assert(result == 0, result, "Could not find Armor block in mech file");
    result = file.ReadIdFloat("Tonnage", data->ArmorTonnage);
    Assert(result == 0, result, "Could not read Tonnage in mech file");
    result = file.SeekBlock("MaxArmorPoints");
    Assert(result == 0, result, "Could not find MaxArmorPoints block in mech file");

    for (size_t location = 0; location < ArmorLocationNames.size(); ++location)
    {
        result = file.ReadIdUChar(ArmorLocationNames[location], data->Armor[location].MaxArmor);
        Assert(result == 0, result, "Could not read armor in maxArmor block in mech file");
    }

    result = file.SeekBlock("CurArmorPoints");
    Assert(result == 0, result, "Could not find CurArmorPoints block in mech file");
    int32_t armorPoints = 0;

    for (size_t location = 0; location < ArmorLocationNames.size(); ++location)
    {
        result = file.ReadIdUChar(ArmorLocationNames[location], data->Armor[location].CurArmor);
        Assert(result == 0, result, "Could not read armor in curArmorPoins block in mech file");
        armorPoints += data->Armor[location].CurArmor;
    }

    data->Cost += armorPoints * ArmorPointCost;

    result = file.SeekBlock("InventoryInfo");
    Assert(result == 0, result, "Could not find InventoryInfo block in vehicle file");
    result = file.ReadIdUChar("NumOther", data->NumOther);
    Assert(result == 0, result, "Could not read NumOther in mech file");
    result = file.ReadIdUChar("NumWeapons", data->NumWeapons);
    Assert(result == 0, result, "Could not read NumWeapons in mech file");
    result = file.ReadIdUChar("NumAmmo", data->NumAmmo);
    Assert(result == 0, result, "Could not read NumAmmo in mech file");
    data->Cost += ReadInventory(file, *data->Inventory, data->NumOther, data->NumWeapons, data->NumAmmo);

    // The body locations: internal structure and the critical slots (component copy, damage).
    float internalPoints = 0.0f;

    for (size_t location = 0; location < BodyLocationNames.size(); ++location)
    {
        result = file.SeekBlock(BodyLocationNames[location]);
        Assert(result == 0, result, "Could not find BodyLocation block in mech file");
        result = file.ReadIdUChar("CurInternalStructure", data->CurInternalStructure[location]);
        Assert(result == 0, result, "Could not read CurInternalStructure in mech file");
        internalPoints = static_cast<float>(data->CurInternalStructure[location]) + internalPoints;

        for (int32_t slot = 0; slot < NumLocationCriticalSpaces[location]; ++slot)
        {
            std::array<uint8_t, 2> component{};
            result = file.ReadIdUCharArray(std::format("Component:{}", slot), component);
            Assert(result == 0, result, "Could not read component in mech file");
            data->CriticalSlots[location][static_cast<size_t>(slot)] = {component[0], component[1]};

            if (component[0] != 0xff)
            {
                if (component[1] != 0)
                {
                    data->Inventory->HitItem(component[0], component[1]);
                }

                // The slot index is passed as the location (faithful).
                data->Inventory->SetStatLoc(component[0], slot);
            }
        }
    }

    data->Cost = static_cast<int32_t>(static_cast<double>(internalPoints) * InternalPointCost + data->Cost);
    data->CalcBR();
    file.Close();
    return data;
}

auto MCPurMechList::AddMech(std::string_view fileA, int32_t countA, std::string_view fileJ, int32_t countJ,
                            std::string_view fileW, int32_t countW) -> void
{
    auto purMech = std::make_shared<MCPurMech>();
    purMech->CurVariant = -1;
    purMech->Variants[0] = ReadVariant(fileA);
    purMech->Variants[0]->NumAvailable = countA;
    purMech->Variants[1] = ReadVariant(fileW);
    purMech->Variants[1]->NumAvailable = countW;
    purMech->Variants[2] = ReadVariant(fileJ);
    purMech->Variants[2]->NumAvailable = countJ;

    // Show the first variant in stock.
    if (countA == 0 && countW != 0)
    {
        purMech->CurVariant = 1;
    }
    else if (countA == 0 && countJ != 0)
    {
        purMech->CurVariant = 2;
    }
    else
    {
        purMech->CurVariant = 0;
    }

    Mechs.insert(Mechs.begin(), std::move(purMech));
}

auto MCPurMechList::ModMech(std::string_view fileName, int32_t deltaA, int32_t deltaJ, int32_t deltaW) -> int32_t
{
    for (const std::shared_ptr<MCPurMech>& purMech : Mechs)
    {
        if (purMech->Variants[0]->FileName != fileName)
        {
            continue;
        }

        for (const auto& [variant, delta] : {std::pair{0, deltaA}, std::pair{1, deltaW}, std::pair{2, deltaJ}})
        {
            int32_t& stock = purMech->Variants[static_cast<size_t>(variant)]->NumAvailable;
            stock = std::max(stock + delta, 0);
        }

        return 0;
    }

    return -1;
}

auto MCPurMechList::GetMechInfo(int32_t index, std::shared_ptr<MCPurMech>& purMech) -> int32_t
{
    if (index >= GetMechCount())
    {
        return -1;
    }

    // A negative index is the first mech (none in an empty list), as the original's walk gave.
    purMech = Mechs.empty() ? nullptr : Mechs[static_cast<size_t>(std::max(index, 0))];
    return 0;
}

auto MCPurMechData::LoadDescription(int32_t descIndex) -> void
{
    // The original loops three times over this same record; only the first pass can load.
    if (descIndex >= 0 && Description.empty())
    {
        Description = LoadDescriptionText(DescIndex);
    }
}

auto MCPurMechData::CalcBR() -> int32_t
{
    BattleRating = ChassisBR;

    for (const std::shared_ptr<MCLogInventoryItem>& item : Inventory->Items)
    {
        BattleRating = static_cast<int32_t>(
            static_cast<double>(MasterComponentList[item->MasterID].BattleRating) * item->Count + BattleRating);
    }

    return BattleRating;
}

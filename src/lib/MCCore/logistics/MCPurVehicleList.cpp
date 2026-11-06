#include "stdafx.h"
#include "logistics/MCPurVehicleList.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCPurProfile.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "object/MCMasterComponent.h"

auto MCPurVehicleList::Clear() -> void
{
    Vehicles.clear();
}

auto MCPurVehicleList::ModVehicle(std::string_view fileName, int32_t delta) -> int32_t
{
    for (const std::shared_ptr<MCPurVehicle>& vehicle : Vehicles)
    {
        if (vehicle->Data->FileName == fileName)
        {
            int32_t& stock = vehicle->Data->NumAvailable;
            stock = std::max(stock + delta, 0);
            return 0;
        }
    }

    return -1;
}

auto MCPurVehicleList::AddVehicle(std::string_view fileName, int32_t count) -> void
{
    MCFitIniFile file;
    OpenProfile(file, ProfilePath, fileName, false, " could not open vehicle file in scenario ");

    if (file.SeekBlock("General") != 0)
    {
        return;
    }

    auto vehicle = std::make_shared<MCPurVehicle>();
    vehicle->Data = std::make_unique<MCPurVehicleData>();
    MCPurVehicleData& data = *vehicle->Data;
    int32_t result = file.SeekBlock("Header");
    Assert(result == 0, result, "Could not find Header block in vehicle file");
    const std::string fileType = ReadProfileString(file, "FileType", 0x7f, "Could not read FileType in vehicle file");
    Assert(fileType == "GroundVehicleProfile", 0, "File is not a vehicle file");
    result = file.SeekBlock("General");
    Assert(result == 0, result, "Could not find General block in vehicle file");
    result = file.ReadIdLong("NameIndex", data.NameIndex);
    Assert(result == 0, result, "Could not read NameIndex in vehicle file");
    result = file.ReadIdFloat("CurTonnage", data.CurTonnage);
    Assert(result == 0, result, "Could not read CurTonnage in vehicle file");

    if (file.ReadIdLong("ResourcePoints", data.BaseCost) != 0)
    {
        data.BaseCost = 100;
    }

    file.ReadIdLong("DescIndex", data.DescIndex);
    data.LoadDescription(data.DescIndex);
    data.Name = LoadGameString(static_cast<uint32_t>(data.DescIndex + 700), 0x7f);

    result = file.SeekBlock("Engine");
    Assert(result == 0, result, "Could not find engine block in vehicle file");
    result = file.ReadIdUChar("MaxMoveSpeed", data.MaxMoveSpeed);
    Assert(result == 0, result, "Could not read MaxMoveSpeed in vehicle file");
    result = file.SeekBlock("Armor");
    Assert(result == 0, result, "Could not find armor block in vehicle file");
    result = file.ReadIdFloat("Tonnage", data.ArmorTonnage);
    Assert(result == 0, result, "Could not read Tonnage in vehicle file");
    result = file.SeekBlock("InventoryInfo");
    Assert(result == 0, result, "Could not find InventoryInfo block in vehicle file");
    result = file.ReadIdUChar("NumOther", data.NumOther);
    Assert(result == 0, result, "Could not read NumOther in vehicle file");
    result = file.ReadIdUChar("NumWeapons", data.NumWeapons);
    Assert(result == 0, result, "Could not read NumWeapons in vehicle file");
    result = file.ReadIdUChar("NumAmmo", data.NumAmmo);
    Assert(result == 0, result, "Could not read NumAmmo in vehicle file");
    data.Inventory = std::make_unique<MCInventoryList>();
    ReadInventory(file, *data.Inventory, data.NumOther, data.NumWeapons, data.NumAmmo);
    data.NumAvailable = count;
    data.FileName = fileName;

    vehicle->CalcVehicleCost();
    file.Close();

    // In tonnage order: before the first that is as heavy or heavier.
    const float tonnage = data.CurTonnage;
    auto place = std::ranges::find_if(Vehicles, [tonnage](const std::shared_ptr<MCPurVehicle>& other)
                                      { return !(other->Data->CurTonnage < tonnage); });
    Vehicles.insert(place, std::move(vehicle));
}

auto MCPurVehicleList::GetVehicleInfo(int32_t index, std::shared_ptr<MCPurVehicle>& vehicle) -> int32_t
{
    if (index >= GetVehicleCount())
    {
        return -1;
    }

    // A negative index is the first vehicle (none in an empty list), as the original's walk gave.
    vehicle = Vehicles.empty() ? nullptr : Vehicles[static_cast<size_t>(std::max(index, 0))];
    return 0;
}

auto MCPurVehicleData::LoadDescription(int32_t descIndex) -> void
{
    if (descIndex > -1 && Description.empty())
    {
        Description = LoadDescriptionText(DescIndex);
    }
}

auto MCPurVehicle::CalcVehicleCost() const -> void
{
    Data->Cost = Data->BaseCost;

    for (const std::shared_ptr<MCLogInventoryItem>& item : Data->Inventory->Items)
    {
        Data->Cost += MasterComponentList[item->MasterID].ResourcePoints * item->Count;
    }
}

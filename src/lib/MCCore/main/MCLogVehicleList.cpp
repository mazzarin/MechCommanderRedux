#include "stdafx.h"
#include "main/MCLogVehicleList.h"
#include "lib/MCPacketFile.h"
#include "logistics/MCMissionLogisticsBridge.h"
#include "logistics/MCLogRows.h"
#include "main/MCGameStrings.h"
#include "logistics/MCRepairScreen.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "main/MCLogisticsShared.h"
#include "object/MCObjectTypeManager.h"

MCLogVehicleList::~MCLogVehicleList()
{
    Clear();
}

auto MCLogVehicleList::Position(int32_t index) const -> size_t
{
    return index < 0 ? 0 : std::min(static_cast<size_t>(index), Vehicles.size());
}

auto MCLogVehicleList::Clear() -> void
{
    while (!Vehicles.empty())
    {
        RemoveVehicle(0);
    }
}

auto MCLogVehicleList::GetVehicleIndex(std::shared_ptr<const MCLogVehicle> vehicle) const -> int32_t
{
    const auto found =
        std::ranges::find_if(Vehicles, [&](const std::shared_ptr<MCLogVehicle>& entry) { return entry == vehicle; });
    return found != Vehicles.end() ? static_cast<int32_t>(found - Vehicles.begin()) : -1;
}

auto MCLogVehicleList::AddVehicle(std::string_view fileName, bool required, bool sorted, bool)
    -> std::shared_ptr<MCLogVehicle>
{
    MCFitIniFile file;
    const int32_t result = file.Open(GamePath(ProfilePath, fileName, ".fit"));
    Assert(result == 0, static_cast<uint32_t>(result), " could not open vehicle Profile file ");
    std::shared_ptr<MCLogVehicle> vehicle = AddVehicle(file, required, sorted, true);
    vehicle->ProfileName = std::string(fileName.substr(0, 9));
    file.Close();
    return vehicle;
}

auto MCLogVehicleList::AddVehicle(MCPacketFile& file, int32_t packet) -> std::shared_ptr<MCLogVehicle>
{
    MCFitIniFile profile;
    int32_t result = file.SeekPacket(packet);
    Assert(result == 0, 0, " Vehicle Packet Not Found ");
    result = profile.Open(file, static_cast<uint32_t>(file.GetPacketSize()));
    Assert(result == 0, 0, " Vehicle file could not open ");
    return AddVehicle(profile, false, false, true);
}

auto MCLogVehicleList::AddVehicle(MCFitIniFile& file, bool required, bool sorted, bool widgets)
    -> std::shared_ptr<MCLogVehicle>
{
    auto vehicle = std::make_shared<MCLogVehicle>();
    vehicle->LocalPart = true;

    // OB-089 (fixed): a profile without a [General] block was read as a saved vehicle list (raw record images) into a
    // vehicle that was never added; nothing in MCX.EXE writes such a file.
    if (file.SeekBlock("General") != 0)
    {
        Fatal(BLOCK_NOT_FOUND, " Vehicle profile has no General Block ", file.GetFilename());
    }

    static constexpr std::array<std::string_view, MCLogVehicle::NumLocations> locationBlocks = {
        "Front", "Left", "Right", "Rear", "Turret"};
    // The checks are numbered as the original's messages were.
    auto check = [](bool ok, std::string_view step) { Assert(ok, 0, std::format("Failed addVehicle - {}", step)); };

    check(file.SeekBlock("Header") == 0, "1");
    std::string fileType;
    check(ReadText(file, "FileType", 0x7f, fileType), "2");
    check(fileType == "GroundVehicleProfile", "2");
    check(file.SeekBlock("General") == 0, "3");
    vehicle->NameIndex = ReadRequired<int32_t>(file, "NameIndex", "Could not read NameIndex in vehicle profile");
    check(ReadEntry(file, "CurTonnage", vehicle->CurTonnage), "4");
    check(ReadEntry(file, "Status", vehicle->Status), "5");
    check(ReadEntry(file, "Chassis", vehicle->Chassis), "6");
    {
        // Original behaviour: the object packet file is opened and closed again, unused.
        MCPacketFile objects;
        check(objects.Open(GamePath(ObjectPath, ObjectPakName)) == 0, "7");
    }

    // Port fix (OB-093): the original read up to 255 characters into the 9-byte crew field.
    vehicle->Crew = ReadRequiredText(file, "Crew", 8, " Could not read crew in vehicle profile");
    vehicle->VehicleResourcePoints = file.Read<int32_t>("ResourcePoints").value_or(100);
    vehicle->BaseVehicleResourcePoints = vehicle->VehicleResourcePoints;
    std::string icon;
    check(ReadText(file, "icon", 0xff, icon), "7");
    vehicle->IconName = icon;
    vehicle->Assigned = file.Read<bool>("Assigned").value_or(false);
    vehicle->Deployed = file.Read<bool>("Deployed").value_or(false);
    vehicle->Required = file.Read<bool>("Required").value_or(false);
    vehicle->DescIndex = -1;
    ReadEntry(file, "DescIndex", vehicle->DescIndex);
    vehicle->LoadDescription(vehicle->DescIndex);
    vehicle->FileName = LoadLogString(static_cast<uint32_t>(vehicle->DescIndex + 700));

    check(file.SeekBlock("Engine") == 0, "9");
    check(ReadEntry(file, "Tonnage", vehicle->EngineTonnage), "10");
    check(ReadEntry(file, "Rating", vehicle->EngineRating), "11");
    check(ReadEntry(file, "MaxMoveSpeed", vehicle->MaxMoveSpeed), "12");
    check(file.SeekBlock("Armor") == 0, "13");
    check(ReadEntry(file, "Type", vehicle->ArmorType), "14");
    check(ReadEntry(file, "Tonnage", vehicle->ArmorTonnage), "15");
    check(file.SeekBlock("InventoryInfo") == 0, "16");
    check(ReadEntry(file, "NumOther", vehicle->NumOther), "17");
    check(ReadEntry(file, "NumWeapons", vehicle->NumWeapons), "18");
    check(ReadEntry(file, "NumAmmo", vehicle->NumAmmo), "19");
    vehicle->Inventory = std::make_unique<MCInventoryList>();
    MCInventoryList& inventory = *vehicle->Inventory;
    int32_t item = 0;
    const int32_t numOther = vehicle->NumOther;

    // Seeks Item:n and reads its MasterID.
    auto readItem = [&](std::string_view blockStep, std::string_view idStep)
    {
        check(file.SeekBlock(std::format("Item:{}", item)) == 0, blockStep);
        uint8_t masterID = 0;
        check(ReadEntry(file, "MasterID", masterID), idStep);
        return masterID;
    };

    auto add = [&](uint8_t masterID, uint8_t facing, int16_t amount)
    { inventory.AddItem(masterID, inventory.CreateStat(static_cast<uint8_t>(item), 0, facing, amount, 0xff), false); };

    for (; item < numOther; ++item)
    {
        add(readItem("19a", "19b"), 0, 1);
    }

    for (const int32_t weaponEnd = numOther + vehicle->NumWeapons; item < weaponEnd; ++item)
    {
        const uint8_t masterID = readItem("20", "21");
        uint8_t facesForward = 0;
        check(ReadEntry(file, "FacesForward", facesForward), "22");
        add(masterID, facesForward, 1);
    }

    for (const int32_t ammoEnd = numOther + vehicle->NumAmmo + vehicle->NumWeapons; item < ammoEnd; ++item)
    {
        const uint8_t masterID = readItem("23", "24");
        int32_t amount = 0;

        if (!ReadEntry(file, "Amount", amount))
        {
            uint8_t smallAmount = 0;
            check(ReadEntry(file, "Amount", smallAmount), "25");
            amount = smallAmount;
        }

        add(masterID, 0, static_cast<int16_t>(amount));
    }

    for (size_t location = 0; location < MCLogVehicle::NumLocations; ++location)
    {
        check(file.SeekBlock(locationBlocks[location]) == 0, "26");
        check(ReadEntry(file, "CurInternalStructure", vehicle->CurInternalStructure[location]), "27");
        check(ReadEntry(file, "MaxArmorPoints", vehicle->MaxArmorPoints[location]), "28");
        check(ReadEntry(file, "CurArmorPoints", vehicle->CurArmorPoints[location]), "29");
    }

    vehicle->NotMineYet = false;

    if (!vehicle->Required)
    {
        vehicle->Required = required;
    }

    vehicle->CalcVehicleCost();

    // The widgets are made before the vehicle is in the list, as the original did.
    if (widgets)
    {
        // The inventory row and texts (the inventory block's init took them; the list makes the rows now).
        vehicle->ListIndex = vehicle->NameIndex;
        vehicle->WeightClassName = LoadGameString(WeightClassString(vehicle->CurTonnage), 0xfe);
        vehicle->ArmorClassName = LoadGameString(ArmorClassString(vehicle->ArmorTonnage), 0xf);
        vehicle->ReadWithWidgets = true;
        // (The original also made the vehicle's briefing box here; the briefing screen makes the box it shows.)
    }

    // In by tonnage when sorted, else at the front.
    auto position = Vehicles.begin();

    if (sorted)
    {
        position = std::ranges::find_if(Vehicles, [&](const std::shared_ptr<MCLogVehicle>& other)
                                        { return vehicle->CurTonnage <= other->CurTonnage; });
    }

    return *Vehicles.insert(position, std::move(vehicle));
}

auto MCLogVehicleList::RemoveVehicle(int32_t index) -> int32_t
{
    const size_t position = Position(index);

    if (index >= GetVehicleCount() || position >= Vehicles.size())
    {
        return -1;
    }

    // Its repair row takes no more clicks (the original's block went with the vehicle).
    if (GlobalLog() != nullptr && GlobalLog()->RepairScreen != nullptr)
    {
        GlobalLog()->RepairScreen->HideUnitRow(*Vehicles[position]);
    }

    Vehicles.erase(Vehicles.begin() + static_cast<ptrdiff_t>(position));
    return 0;
}

auto MCLogVehicleList::RemoveVehicle(const MCLogVehicle& vehicle) -> int32_t
{
    const auto found = std::ranges::find_if(Vehicles, [&](const std::shared_ptr<MCLogVehicle>& entry)
                                            { return entry.get() == &vehicle; });
    const int32_t index = found != Vehicles.end() ? static_cast<int32_t>(found - Vehicles.begin()) : -1;
    return index >= 0 ? RemoveVehicle(index) : -1;
}

auto MCLogVehicleList::GetVehicleInfo(int32_t index, std::shared_ptr<MCLogVehicle>& vehicle) const -> int32_t
{
    const size_t position = Position(index);
    vehicle = index < GetVehicleCount() && position < Vehicles.size() ? Vehicles[position] : nullptr;
    return vehicle != nullptr ? 0 : -1;
}

auto MCLogVehicleList::SaveVehicleText(std::string_view fileName, int32_t index) const -> int32_t
{
    std::shared_ptr<MCLogVehicle> vehicle;

    if (GetVehicleInfo(index, vehicle) != 0)
    {
        return -1;
    }

    MCMissionLogisticsBridge::LogisticsVehicleProfileWriter(fileName, *vehicle, false);
    return 0;
}

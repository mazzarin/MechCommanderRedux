#include "stdafx.h"
#include "main/MCLogVehicle.h"
#include "logistics/MCPurProfile.h"
#include "object/MCMasterComponent.h"

MCLogVehicle::MCLogVehicle()
{
    PartType = 2;
}

MCLogVehicle::~MCLogVehicle()
{
    Inventory.reset();
}

auto MCLogVehicle::CalcVehicleCost() -> void
{
    VehicleResourcePoints = BaseVehicleResourcePoints;

    for (const std::shared_ptr<MCLogInventoryItem>& item : Inventory->Items)
    {
        VehicleResourcePoints += MasterComponentList[item->MasterID].ResourcePoints * item->Count;
    }
}

auto MCLogVehicle::LoadDescription(int32_t descIndex) -> void
{
    if (descIndex < 0 || !Description.empty())
    {
        return;
    }

    Description = LoadDescriptionText(DescIndex);
}

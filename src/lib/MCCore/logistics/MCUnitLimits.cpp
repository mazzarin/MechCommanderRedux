#include "stdafx.h"
#include "logistics/MCUnitLimits.h"
#include "logistics/MCLogRows.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "network/MCMultiPlayer.h"

int GMoreUnits = 0;
int32_t ResourcePoints = 0;

namespace
{
    /// <summary>Whether the raised limit applies: MoreUnits, outside a match (a match keeps the original's).</summary>
    bool MoreUnitsInForce()
    {
        return GMoreUnits != 0 && MultiPlayer() == nullptr;
    }
}

auto MaxUnits() -> int32_t
{
    return MoreUnitsInForce() ? RaisedMaxUnits : OriginalMaxUnits;
}

auto UnitWarning() -> int32_t
{
    return MoreUnitsInForce() ? RaisedUnitWarning : OriginalUnitWarning;
}

auto NumUnits() -> int32_t
{
    return GlobalLog()->ForceVehicleList->GetVehicleCount() + GlobalLog()->ForceMechList->GetMechCount() +
           GlobalLog()->VehicleList->GetVehicleCount() + GlobalLog()->MechList->GetMechCount();
}

auto MaxPurchase(int32_t available) -> int32_t
{
    int32_t room = MaxUnits() - NumUnits();

    if (available < room && available > -1)
    {
        room = available;
    }

    return room;
}

auto CheckMaxUnits() -> bool
{
    if (NumUnits() < MaxUnits())
    {
        return false;
    }

    PlayLogSound(0x33);
    ShowLogMessage(0x374, true);
    return true;
}

auto CheckNumUnits() -> void
{
    const int32_t units = NumUnits();

    if (units < UnitWarning())
    {
        return;
    }

    const int32_t most = MaxUnits();
    std::string text;

    if (units == most)
    {
        text = LoadGameString(0x374, 0xfe);
    }
    else
    {
        text = MCFormatPrintf(LoadGameString(0x372, 0xfe).c_str(), units, most);
    }

    PlayLogSound(0x33);
    ShowLogMessage(text, true);
}

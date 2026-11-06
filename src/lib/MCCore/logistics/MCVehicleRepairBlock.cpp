#include "stdafx.h"
#include "logistics/MCVehicleRepairBlock.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCScrollPane.h"
#include "lib/MCFatal.h"
#include "logistics/MCDragIcon.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCTicker.h"
#include "logistics/MCVehicleInventoryBlock.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "object/MCMasterComponent.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>What a vehicle row of the repair screen is doing with the mouse (one mouse: shared by every row).</summary>
    struct MCVehicleDrag
    {
        /// <summary>The vehicle is dragged with the left button held.</summary>
        bool LeftDrag = false;
        /// <summary>Set by a right-button press, cleared by the release.</summary>
        bool RightHeld = false;
        /// <summary>The vehicle is picked up.</summary>
        bool Vehicle = false;
        /// <summary>The drag icon's position.</summary>
        int32_t X = 0;
        int32_t Y = 0;
    };

    MCVehicleDrag Drag;

    MCRepairScreen& RepairScreen()
    {
        return *GlobalLog()->RepairScreen;
    }

    void DrawLine(const MCPane& pane, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color)
    {
        VfxLineDraw(pane, x0, y0, x1, y1, color);
    }
}

auto MCVehicleRepairBlock::Init(std::shared_ptr<MCLogVehicle> logVehicle) -> void
{
    _Vehicle = logVehicle;
    MCLogObject::InitWithoutPort(0, 0, 0x19a, 0x70);
}

auto MCVehicleRepairBlock::SlotIndex() const -> int32_t
{
    const std::shared_ptr<MCLogVehicle> vehicle = Vehicle();
    return vehicle != nullptr ? vehicle->SlotIndex : 0;
}

auto MCVehicleRepairBlock::LeaveForce() const -> void
{
    Assert(Vehicle() != nullptr, 0, "Vehicle is NULL");
    GlobalLog()->RemoveVehicleFromSlots(SlotIndex() - GlobalLog()->ForceMechList->GetMechCount());
    std::shared_ptr<MCLogVehicle> leaving = Vehicle();
    leaving->Deployed = false;
    leaving->Assigned = false;

    if (leaving == RepairScreen().SelectedVehicle())
    {
        RepairScreen().ClearSelectedVehicle();
    }

    GlobalLog()->ReorderVehicles();
    // The new listing destroys this row (the original took the block out of the pane).
    RepairScreen().RemoveVehicleFromList(*leaving);
    MCRepairScreen::CreateVhclInvBlock();
    RepairScreen().SetUpVhclInv(true, true);
}

auto MCVehicleRepairBlock::HandleEvent(MCGuiEvent& event) -> void
{
    if (GlobalLog()->CurrentScreen() == GlobalLog()->PurchaseScreen)
    {
        return;
    }

    if (!Parent.expired() && !Drag.LeftDrag && !Drag.RightHeld && (event.Type == 8 || event.Type == 9))
    {
        ParentObject()->HandleEvent(event);
        return;
    }

    const int32_t eventType = event.Type;

    switch (eventType)
    {
        case 1:
        {
            if (Drag.RightHeld)
            {
                return;
            }

            [[fallthrough]];
        }
        case 3:
        {
            if (Drag.LeftDrag)
            {
                break;
            }

            if (RepairScreen().SelectedVehicle() != Vehicle())
            {
                RepairScreen().SelectVehicle(Vehicle());
                return;
            }

            if (!OnRow(*this, event))
            {
                PlayLogSound(0x33);
                return;
            }

            // Pick up the vehicle.
            PlayLogSound(0x35);
            GuiSystem().SetCursorVisible(false);
            GuiSystem().Grab(weak_from_this());
            Drag.Vehicle = true;
            (eventType == 1 ? Drag.LeftDrag : Drag.RightHeld) = true;
            Drag.Y = event.Y - 0xf;
            Drag.X = event.X - 0xf;
            const std::shared_ptr<MCDragIcon> icon = MCDragIcon::Create(
                RepairScreen(), Drag.X, Drag.Y, 0x1e, 0x1e, [this](MCLogPort& surface) { OnBeginDrag(surface); });
            icon->ShowAt(Drag.X, Drag.Y);
            return;
        }

        case 4:
        {
            if (Drag.RightHeld)
            {
                return;
            }

            [[fallthrough]];
        }
        case 6:
        {
            if (Drag.LeftDrag && eventType == 6)
            {
                return;
            }

            Drag.RightHeld = false;

            if (GuiSystem().GrabbedObject() == nullptr)
            {
                return;
            }

            GuiSystem().SetCursorVisible(true);
            PlayLogSound(0x34);
            GuiSystem().Release();
            Drag.LeftDrag = false;
            MCDragIcon::Remove();
            const bool onInventory = eventType == 6 || OverPaneInside(*(RepairScreen().InventoryPane), event);

            if (Drag.Vehicle)
            {
                // Dropped on the inventory, the vehicle leaves the force.
                Drag.Vehicle = false;

                if (onInventory)
                {
                    // Leaving the force destroys this row: it lives until the drop is done.
                    const std::shared_ptr<MCGuiObject> self = shared_from_this();
                    LeaveForce();
                }

                return;
            }

            // Original behaviour: the pilot drop of the mech rows, though vehicles take no pilots (the original's pilot
            // was never set, so only this inventory branch can run, shifting from pilot -1).
            if (onInventory)
            {
                GlobalLog()->ReorderWarriors();
                RepairScreen().CreatePilotInvBlock();
                RepairScreen().SetUpPilotInv(true, true);
                GlobalLog()->ShiftPilots(-1, -1);
            }
            break;
        }

        case 7:
        {
            if (Drag.LeftDrag)
            {
                Drag.Y = event.Y - 0xf;
                Drag.X = event.X - 0xf;
                MCDragIcon::Current()->MoveTo(Drag.X, Drag.Y, false);
            }

            if (event.Key == 0)
            {
                GlobalLog()->Ticker->SetString(LoadGameString(0x34, 0xfe));
            }
            break;
        }

        default:
            break;
    }
}

auto MCVehicleRepairBlock::DrawDamageDiagram(MCLogPort& port) const -> void
{
    const std::shared_ptr<MCLogVehicle> vehicle = Vehicle();
    std::array<int32_t, 5> shade = {};

    for (size_t location = 0; location < shade.size(); ++location)
    {
        shade[location] =
            RepairShade(PercentLeft(vehicle->CurArmorPoints[location], vehicle->MaxArmorPoints[location]));
    }

    for (int32_t pass = 0; pass < 5; ++pass)
    {
        VfxShapeLookaside(ArmorShadeTable(pass));

        for (int32_t location = 0; location < 5; ++location)
        {
            if (shade[location] == pass)
            {
                VfxShapeTranslateDraw(port.Frame(), GlobalLog()->VehicleRepShapes[vehicle->NameIndex].Bytes(), location,
                                      0, 0);
            }
        }
    }
}

auto MCVehicleRepairBlock::DrawBackground(int32_t row, MCOptionalRef<MCLogPort> port) -> void
{
    if (row >= 0)
    {
        // The repair screen's rows are drawn each frame (DrawRow).
        return;
    }

    PaintRow(*port, 0, true, false);
}

auto MCVehicleRepairBlock::DrawRow(MCLogPort& port, int32_t top) -> void
{
    // Framed while selected (the original's frame stayed until the row was painted again).
    PaintRow(port, top, false, RepairScreen().SelectedVehicle() == Vehicle());
}

auto MCVehicleRepairBlock::OnBeginDrag(MCLogPort& surface) const -> void
{
    VfxPaneWipe(surface.Frame(), 0x10);

    for (int32_t location = 0; location < 5; ++location)
    {
        MCLogistics::DrawVehicleBodyLoc(*Vehicle(), location, surface, 2, 0);
    }
}

auto MCVehicleRepairBlock::PaintRow(MCLogPort& port, int32_t top, bool briefing, bool framed) -> void
{
    const std::shared_ptr<MCLogPort> rowArt = LogScreenArt(briefing ? "lsbbkv00.tga" : "lsrupv00.tga");

    if (rowArt == nullptr)
    {
        return;
    }

    MCLogBlockPort back(port.Frame(), 0, top, rowArt->Width(), rowArt->Height(), true);
    VfxPaneCopy(rowArt->Frame(), 0, 0, back.Frame(), 0, 0, -1);
    CopyArt(back, 5, 4, std::format("lscflv{:02}.tga", Vehicle()->NameIndex));
    const int32_t diagramX = briefing ? 0x124 : 0x11d;

    // The damage diagram, drawn over a copy of the vehicle's mask.
    if (const std::shared_ptr<MCLogPort> maskArt = LogScreenArt(std::format("vmask{:02}.tga", Vehicle()->NameIndex)))
    {
        MCLogBlockPort mask(back.Frame(), diagramX, 8, maskArt->Width(), maskArt->Height(), true);
        VfxPaneCopy(maskArt->Frame(), 0, 0, mask.Frame(), 0, 0, -1);
        DrawDamageDiagram(mask);
    }

    SetBar(back, diagramX);
    // The equipment and weapons, one "count name" line each.
    MCInventoryList& inventory = *Vehicle()->Inventory;
    int32_t line = 0;

    for (int32_t index = 0; index < inventory.NumItems(); ++index)
    {
        MCComponentForm form = MasterComponentList[inventory.GetMasterIDFromIndex(index)].Form;

        if (IsEquipment(form) || IsWeapon(form) || form == MCComponentForm::Weapon)
        {
            std::shared_ptr<MCLogInventoryItem> item = inventory.GetItemInfo(index);
            WriteText(*GreenFont, back, 0x8e, (GreenFont->Height() + 1) * line + 0x16,
                      std::format("{} {}", item->Count, item->Name));
            ++line;
        }
    }

    const std::string& weightClass = Vehicle()->WeightClassName;
    WriteText(*BlueFont, back, 6, 0x11,
              MCFormatPrintf(LoadGameString(0x53, 0xfe).c_str(), static_cast<double>(Vehicle()->CurTonnage),
                             weightClass.c_str()));
    WriteText(*YellowDropFont, back, 6, 0x29, std::format("{} m/s", Vehicle()->MaxMoveSpeed));
    WriteText(*YellowDropFont, back, 6, 0x3b, weightClass);

    if (briefing)
    {
        return;
    }

    if (framed)
    {
        // The selected vehicle's frame.
        DrawLine(back.Frame(), 1, 0, Width(), 0, 0xf2);
        DrawLine(back.Frame(), 1, 0, 1, Height() - 3, 0xf2);
        DrawLine(back.Frame(), 1, Height() - 3, Width(), Height() - 3, 0xf2);
        DrawLine(back.Frame(), Width(), 0, Width(), Height() - 3, 0xf2);
    }
    else
    {
        MCLogistics::Darken(0, LogisticFadetable(), back);
    }
}

auto MCVehicleRepairBlock::SetBar(MCLogPort& port, int32_t xPos) const -> void
{
    const std::shared_ptr<MCLogVehicle> vehicle = Vehicle();
    // The firepower left (1 without weapons), times each location's armor (at least 40% each).
    double working = 0.0;
    double total = 0.0;
    double firepower = 1.0;

    if (!vehicle->Inventory->Items.empty())
    {
        for (const std::shared_ptr<MCLogInventoryItem>& item : vehicle->Inventory->Items)
        {
            const MCMasterComponent& component = MasterComponentList[item->MasterID];

            if (!IsWeapon(component.Form) || item->Stats.empty())
            {
                continue;
            }

            int16_t value = WeaponWorth(component);

            for (const std::shared_ptr<MCLogInventoryStat>& stat : item->Stats)
            {
                if (stat->Hits == 0)
                {
                    working += value;
                }

                total += value;
            }
        }

        if (total != 0.0)
        {
            firepower = working / total;
        }
    }

    auto armorFactor = [&vehicle](int32_t location)
    { return static_cast<double>(vehicle->CurArmorPoints[location]) / vehicle->MaxArmorPoints[location] * 0.6 + 0.4; };

    double last = 1.0;

    if (static_cast<float>(vehicle->MaxArmorPoints[4]) != 0.0f)
    {
        last = armorFactor(4);
    }

    double status = last * armorFactor(3) * armorFactor(2) * armorFactor(1) * armorFactor(0) * firepower;
    uint8_t color;

    if (!(status >= 0.5))
    {
        color = status > 0.2 ? 0xf2 : 0xef;
    }
    else
    {
        color = 0xb;
    }

    auto end = static_cast<int32_t>(status * 74.0 + xPos);

    for (int32_t y = 2; y <= 5; ++y)
    {
        DrawLine(port.Frame(), xPos, y, end, y, color);
    }
}

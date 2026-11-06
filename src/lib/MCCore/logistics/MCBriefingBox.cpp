#include "stdafx.h"
#include "logistics/MCBriefingBox.h"
#include "gui/MCGuiEvent.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCMechRepairBlock.h"
#include "logistics/MCVehicleRepairBlock.h"
#include "main/MCLogistics.h"
#include "logistics/MCBriefingScreen.h"

auto MCBriefingBox::Init(const std::shared_ptr<MCLogPart>& part) -> void
{
    std::shared_ptr<MCLogMech> logMech;
    std::shared_ptr<MCLogVehicle> logVehicle;

    if (part->PartType == 1)
    {
        logMech = std::static_pointer_cast<MCLogMech>(part);
    }
    else
    {
        logVehicle = std::static_pointer_cast<MCLogVehicle>(part);
    }

    _Mech = logMech;
    _Vehicle = logVehicle;
    MCLogObject::InitWithoutPort(0xd3, 0x16f, 0x1ab, 0x6f);
    InventoryPane.reset();
    MechPanel.reset();
    VehiclePanel.reset();

    // The panel is a hidden child (the pane stays the first, which the events go to); the box paints through it.
    if (logMech == nullptr)
    {
        VehiclePanel = CreateChild<MCVehicleRepairBlock>(
            [&](MCVehicleRepairBlock& panel)
            {
                panel.Init(logVehicle);
                panel.ShowGuiWindow(false);
            });
        return;
    }

    InventoryPane =
        CreateChild<MCScrollPane>([](MCScrollPane& pane) { pane.Init(0x62, 0x58, 0x143, 0x11, std::string_view()); });
    MechPanel = CreateChild<MCMechRepairBlock>(
        [&](MCMechRepairBlock& panel)
        {
            panel.Init(logMech, true);
            panel.ShowGuiWindow(false);
        });
    MechPanel->SetInventory(*InventoryPane.Lock());
}

auto MCBriefingBox::Part() const -> std::shared_ptr<MCLogPart>
{
    if (const std::shared_ptr<MCLogMech> mech = Mech())
    {
        return mech;
    }

    return Vehicle();
}

auto MCBriefingBox::DrawBackground() -> void
{
    // Port: the box is drawn each frame (PaintBox) by the briefing screen.
    if (Mech() != nullptr)
    {
        MechPanel->SetInventory(*InventoryPane.Lock());
    }
}

auto MCBriefingBox::PaintBox(const MCPane& target, int32_t xPos, int32_t yPos) const -> void
{
    const std::shared_ptr<MCLogMech> mech = Mech();
    MCLogBlockPort work(target, xPos, yPos, 0x1ab, 0x6f, false);

    if (mech == nullptr)
    {
        VehiclePanel->DrawBackground(-1, work);
    }
    else
    {
        MechPanel->DrawBackground(-1, work);
        // The weapon list and its slider, then the tonnage bar.
        MCLogBlockPort list(work.Frame(), 0x143, 0x11, 0x62, 0x58, false);
        InventoryPane->DrawContentTo(list.Frame(), 0, 0);
        InventoryPane->DrawSliderColumn(work.Frame(), InventoryPane->Width() + 0x136, 0x11, false);
        int32_t fill = static_cast<int32_t>(static_cast<double>(mech->WeaponTonnage) / mech->FreeTonnage * 55.0);
        DrawTonnageBar(work.Frame(), 0x16c, fill);
    }

    MCLogistics::Darken(0, LogisticFadetable(), work);
}

auto MCBriefingBox::HandleEvent(MCGuiEvent& event) -> void
{
    const std::shared_ptr<MCScrollPane> pane = InventoryPane.Lock();

    if (!Parent.expired() && (event.Type == 8 || event.Type == 9))
    {
        ParentObject()->HandleEvent(event);
        return;
    }

    if (pane != nullptr)
    {
        pane->HandleEvent(event);
    }
}

auto MCBriefingBox::MouseWheel(int32_t steps, int32_t xPos, int32_t yPos) -> bool
{
    // The pane redraws the box (its parent) when it scrolls.
    // A vehicle's box has no list.
    const std::shared_ptr<MCScrollPane> pane = InventoryPane.Lock();
    return pane != nullptr && OverPaneArea(*pane, xPos, yPos) && pane->MouseWheel(steps, xPos, yPos);
}

auto MCBriefingBox::Draw() -> void
{
    // The original repainted the weapon list (scrolled) into the briefing screen's picture; the screen draws the whole
    // box each frame (PaintBox).
}

auto MCBriefingBox::Display() -> void
{
}

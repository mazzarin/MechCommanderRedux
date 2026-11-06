#include "stdafx.h"
#include "logistics/MCRepairScreen.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCListView.h"
#include "gui/MCScrollPane.h"
#include "lib/MCFatal.h"
#include "logistics/MCTicker.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCLogToolButton.h"
#include "logistics/MCLogChatInput.h"
#include "logistics/MCPlayerNameObject.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCMechRepairBlock.h"
#include "logistics/MCVehicleRepairBlock.h"
#include "logistics/MCBriefingBox.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCPilotPurchaseBlock.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "logistics/MCCompPurchaseBlock.h"
#include "logistics/MCUnitLimits.h"
#include "logistics/MCPurProfile.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCInput.h"
#include "vfx/MCVfxFunctions.h"
#include "logistics/MCBriefingScreen.h"

int32_t ResourceDisplayState = 0;

namespace
{
    /// <summary>The sample the screen's tabs and buttons play.</summary>
    constexpr uint32_t TabSample = 0x36;
    /// <summary>
    /// The key the Ctrl+Alt resource cheat compares with: VK_OEM_PLUS as a signed char. Original behaviour (OB-077):
    /// the key byte is zero-extended, so the cheat never fires.
    /// </summary>
    constexpr int32_t ResourceCheatKey = -0x45;
    /// <summary>The resource points the cheat would add.</summary>
    constexpr int32_t ResourceCheatPoints = 1000;

    /// <summary>Set while the left button is down on the screen; only written.</summary>
    bool MouseDown = false;

    /// <summary>The blink phase of the briefing button's highlight, flipped by each timer event.</summary>
    bool BriefingBlink = false;

    /// <summary>The depth the force's rows were put at in the unit pane.</summary>
    constexpr int32_t UnitRowDepth = 100;

    /// <summary>The colour under the force's rows (what the original wiped the rows' picture to).</summary>
    constexpr uint8_t UnitRowsBackground = 0xff;

    /// <summary>Puts string <paramref name="id"/> on the ticker.</summary>
    void ShowHelp(uint32_t id)
    {
        GlobalLog()->Ticker->SetString(LoadGameString(id, 0xfe));
    }
}

auto MCRepairScreen::Init() -> void
{
    ChatBlinking = false;
    _SelectedMech.reset();
    _SelectedVehicle.reset();
    const int32_t result = MCLogObject::Init(0, 0, 0x280, 0x1e0);
    Assert(result == 0, static_cast<uint32_t>(result), "Unable to init repair screen");
    // The original loaded the background (lsrbk00) as the screen's picture; the screen draws it each frame.
    InitLive("lsrbk00.tga");
    MakePanes(false);
    ShowGuiWindow(false);
}

auto MCRepairScreen::OnDestroy() -> void
{
    FreePanes();
    _ShownUnits.clear();
    MCLogObject::OnDestroy();
}

auto MCRepairScreen::SelectMech(std::shared_ptr<MCLogMech> mech) -> void
{
    std::shared_ptr<MCLogMech> oldMech = GlobalLog()->RepairScreen->SelectedMech();
    GlobalLog()->RepairScreen->_SelectedMech = mech;

    // A vehicle row is drawn each frame and has nothing else to work out (its DrawBackground does nothing for a row).
    _SelectedVehicle.reset();

    // The rows work out again what they show (the weapon lists, the battle rating, the repair buttons).
    for (const std::shared_ptr<MCLogMech>& changed : {mech, oldMech})
    {
        if (changed == nullptr)
        {
            continue;
        }

        if (const std::shared_ptr<MCMechRepairBlock> row = RowOf(*changed); row != nullptr)
        {
            row->DrawBackground(changed->SlotIndex, std::nullopt);
        }
    }

    if (GlobalLog()->CurrentInvTab == 1)
    {
        SetUpPilotInv(false, true);
        return;
    }

    if (GlobalLog()->CurrentInvTab == 2)
    {
        SetUpCompInv(false, true);
    }
}

auto MCRepairScreen::SelectVehicle(std::shared_ptr<MCLogVehicle> vehicle) -> void
{
    GlobalLog()->RepairScreen->_SelectedVehicle = std::move(vehicle);

    // The mech row deselected works out again what it shows (vehicle rows have nothing to work out).
    if (const std::shared_ptr<MCLogMech> mech = SelectedMech(); mech != nullptr)
    {
        _SelectedMech.reset();

        if (const std::shared_ptr<MCMechRepairBlock> row = RowOf(*mech); row != nullptr)
        {
            row->DrawBackground(mech->SlotIndex, std::nullopt);
        }
    }

    if (GlobalLog()->CurrentInvTab == 1)
    {
        SetUpPilotInv(false, true);
    }

    if (GlobalLog()->CurrentInvTab == 2)
    {
        SetUpCompInv(false, true);
    }
}

auto MCRepairScreen::AddMechToList([[maybe_unused]] MCLogMech& mech) -> void
{
    ShowForce(false);
}

auto MCRepairScreen::AddVehicleToList([[maybe_unused]] MCLogVehicle& vehicle) -> void
{
    // The original also painted the new vehicle's row (its DrawBackground, nothing for a row now).
    ShowForce(false);
}

auto MCRepairScreen::RemoveMechFromList([[maybe_unused]] MCLogMech& mech) -> void
{
    ShowForce(false);
}

auto MCRepairScreen::RemoveVehicleFromList([[maybe_unused]] MCLogVehicle& vehicle) -> void
{
    ShowForce(false);
}

auto MCRepairScreen::ShowForce(bool resetScroll) -> void
{
    // The original painted the rows into a new picture as tall as the force (at least the pane), over colour 0xff.
    _ShownUnits.clear();
    int32_t slot = 0;

    for (const std::shared_ptr<MCLogMech>& mech : GlobalLog()->ForceMechList->Mechs)
    {
        mech->SlotIndex = slot++;
        _ShownUnits.push_back(mech);
    }

    for (const std::shared_ptr<MCLogVehicle>& vehicle : GlobalLog()->ForceVehicleList->Vehicles)
    {
        vehicle->SlotIndex = slot++;
        _ShownUnits.push_back(vehicle);
    }

    MCListSource<MCLogPart> source;
    source.Items = []() -> const std::vector<std::shared_ptr<MCLogPart>>&
    { return GlobalLog()->RepairScreen->_ShownUnits; };
    source.MakeRow = [](MCListView& list, std::shared_ptr<MCLogPart> unit) -> MCGuiHandle<MCListRow>
    {
        if (std::shared_ptr<MCLogMech> mech = std::dynamic_pointer_cast<MCLogMech>(unit); mech != nullptr)
        {
            return list.CreateRow<MCMechRepairBlock>(
                [&](MCMechRepairBlock& row)
                {
                    row.Init(std::move(mech));
                    row.SetDepth(UnitRowDepth);
                });
        }

        return list.CreateRow<MCVehicleRepairBlock>(
            [&](MCVehicleRepairBlock& row)
            {
                row.Init(std::static_pointer_cast<MCLogVehicle>(unit));
                row.SetDepth(UnitRowDepth);
            });
    };

    const MCListLayout layout{UnitBlockHeight, UnitPane->Width() - 0xd, UnitPane->Height(), UnitRowsBackground};
    UnitPane->Show(source, layout, resetScroll);
}

auto MCRepairScreen::HideUnitRow(const MCLogPart& unit) -> void
{
    const std::shared_ptr<MCListView> pane = UnitPane.Lock();

    if (pane == nullptr)
    {
        return;
    }

    if (const std::shared_ptr<MCListRow> row = pane->RowOf<MCListRow>(unit); row != nullptr)
    {
        row->ShowGuiWindow(false);
    }
}

auto MCRepairScreen::RowOf(const MCLogMech& mech) const -> std::shared_ptr<MCMechRepairBlock>
{
    const std::shared_ptr<MCListView> pane = UnitPane.Lock();
    return pane != nullptr ? pane->RowOf<MCMechRepairBlock>(static_cast<const MCLogPart&>(mech)) : nullptr;
}

auto MCRepairScreen::RowOf(const MCLogVehicle& vehicle) const -> std::shared_ptr<MCVehicleRepairBlock>
{
    const std::shared_ptr<MCListView> pane = UnitPane.Lock();
    return pane != nullptr ? pane->RowOf<MCVehicleRepairBlock>(static_cast<const MCLogPart&>(vehicle)) : nullptr;
}

auto MCRepairScreen::DrawBackground() -> void
{
    // The background art was painted over everything the screen showed.
    ScreenChrome.Clear();
    Info.Clear();

    if (const std::shared_ptr<MCLogMech> mech = SelectedMech(); mech != nullptr)
    {
        if (const std::shared_ptr<MCMechRepairBlock> row = RowOf(*mech); row != nullptr)
        {
            row->DrawButtons(std::nullopt);
        }
    }
}

auto MCRepairScreen::HandleEvent(MCGuiEvent& event) -> void
{
    if (GlobalLog()->CurrentScreen().get() != this)
    {
        return;
    }

    const int32_t xPos = event.X;
    const int32_t yPos = event.Y;
    const uint8_t key = event.Key;

    if (key == 0 && event.Type != MCGuiEventType::Timer)
    {
        // The help line for whatever the mouse is over, and the highlighted screen button.
        GlobalLog()->DrawScreenButtons();
        DrawBlankInvInfoBlock(-1);
        const POINT point{xPos, yPos};
        auto inside = [&point](int32_t left, int32_t top, int32_t right, int32_t bottom)
        {
            const RECT area{left, top, right, bottom};
            return PtInRect(area, point) != 0;
        };

        if (inside(2, 2, 0xd1, 0xd))
        {
            ShowHelp(0x1d);
        }
        else if (inside(2, 0x10, 0xd1, 0x21))
        {
            ShowHelp(0x286);
            MCLogistics::HoverScreenButton(*this, 0);
        }
        else if (inside(2, 0x22, 0xd1, 0x33))
        {
            ShowHelp(0x1e);
            MCLogistics::HoverScreenButton(*this, 1);
        }
        else if (inside(2, 0x34, 0xd1, 0x45))
        {
            ShowHelp(0x41);
            MCLogistics::HoverScreenButton(*this, 2);
        }
        else if (inside(2, 0x46, 0xd1, 0x57))
        {
            ShowHelp(0x42);
        }
        else if (inside(0x20c, 2, 0x24d, 0xd) || inside(0x19b, 4, 0x24a, 0xf))
        {
            ShowHelp(0x1f);
        }
        else if (inside(0xc6, 0x66, 0xd1, 0x95))
        {
            ShowHelp(0x29);
        }
        else if (inside(0xc6, 0x97, 0xd1, 0xef))
        {
            ShowHelp(0x2a);
        }
        else if (inside(0xc6, 0xf1, 0xd1, 0x140))
        {
            ShowHelp(0x2b);
        }
        else if (inside(0xc6, 0x141, 0xcf, 0x17c))
        {
            ShowHelp(0x2c);
        }
        else if (inside(7, 0x6a, 0xb3, 0x178))
        {
            // The inventory tab's help (the original put its unset string buffer on the ticker for another tab).
            static constexpr std::array<uint32_t, 4> tabHelp = {0x31, 0x33, 0x32, 0x47};
            const int32_t tab = GlobalLog()->CurrentInvTab;

            if (tab >= 0 && tab < 4)
            {
                ShowHelp(tabHelp[static_cast<size_t>(tab)]);
            }
            else
            {
                GlobalLog()->Ticker->SetString({});
            }
        }
        else if (inside(0x270, 0x10, 0x27d, 0x1dd))
        {
            ShowHelp(0x2f);
        }
        else if (inside(0xd3, 0x10, 0x270, 0x1dd))
        {
            ShowHelp(0x43);
        }
        else
        {
            GlobalLog()->Ticker->SetString({});
        }
    }

    if (event.Type == MCGuiEventType::LeftButtonDown)
    {
        MouseDown = true;
        const POINT point{xPos - GlobalX(), yPos - GlobalY()};
        auto inside = [&point](int32_t left, int32_t top, int32_t right, int32_t bottom)
        {
            const RECT area{left, top, right, bottom};
            return PtInRect(area, point) != 0;
        };

        if (inside(2, 0x34, 0xd1, 0x45))
        {
            GlobalLog()->SetUpPurchaseScreen(true);
            return;
        }

        if (inside(2, 0x22, 0xd1, 0x32))
        {
            GlobalLog()->SetUpBriefingScreen(true);
            return;
        }

        if (inside(2, 0x10, 0xd1, 0x21))
        {
            if (MultiPlayer() != nullptr)
            {
                CheckExit();
                return;
            }

            PlayLogSound(TabSample);
            GlobalLog()->SetUpMainScreen(false);
            return;
        }

        // The inventory tabs.
        if (inside(0xc2, 0x67, 0xd1, 0x95))
        {
            PlayLogSound(TabSample);
            SetUpMechInv(true, true);
            SetUpMechPurchase();
        }

        if (inside(0xc2, 0x96, 0xd1, 0xf1))
        {
            PlayLogSound(TabSample);
            SetUpPilotInv(true, true);
            SetUpPilotPurchase();
        }

        if (inside(0xc2, 0xf2, 0xd1, 0x140))
        {
            PlayLogSound(TabSample);
            SetUpCompInv(true, true);
            SetUpCompPurchase();
        }

        if (inside(0xc2, 0x141, 0xd1, 0x17c))
        {
            PlayLogSound(TabSample);
            SetUpVhclInv(true, true);
            SetUpVehiclePurchase();
        }
    }

    if (event.Type == MCGuiEventType::LeftButtonUp)
    {
        MouseDown = false;
    }

    if (event.Type == MCGuiEventType::KeyDown)
    {
        const bool ctrlAlt = MCInput::GetAsyncKeyState(VK_CONTROL) != 0 && MCInput::GetAsyncKeyState(VK_MENU) != 0;

        if (MultiPlayer() == nullptr && static_cast<int32_t>(key) == ResourceCheatKey && ctrlAlt)
        {
            ResourcePoints += ResourceCheatPoints;
        }
    }

    if (event.Type == MCGuiEventType::KeyUp)
    {
        GlobalLog()->ProcessCheatCode(event.ScanCode);
        return;
    }

    if (event.Type == MCGuiEventType::Timer)
    {
        // Blink the briefing button.
        ScreenChrome.BlinkLit = BriefingBlink;
        BriefingBlink = !BriefingBlink;
    }
}

auto MCRepairScreen::ShowGuiWindow(bool show) -> void
{
    ShowWindow = show;
    InventoryPane->ShowGuiWindow(show);
    UnitPane->ShowGuiWindow(show);
}

auto MCRepairScreen::Display() -> void
{
    // The figure and the clock are drawn by the screens each frame (MCLogistics::DrawScreenChrome).
    MCLogObject::Display();
}

auto ResourceFigureText() -> std::string
{
    // States 1 and 2 showed the logistics heap's free memory and largest free block; the heap is gone, so they show
    // nothing (the original left the text unset for another state).
    return ResourceDisplayState == 0 ? std::format("{}", ResourcePoints) : std::string();
}

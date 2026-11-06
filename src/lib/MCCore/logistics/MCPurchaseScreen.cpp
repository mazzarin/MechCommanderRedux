#include "stdafx.h"
#include "logistics/MCPurchaseScreen.h"
#include "gui/MCScrollPane.h"
#include "lib/MCFatal.h"
#include "main/MCGamePaths.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCTicker.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCInput.h"
#include "logistics/MCBriefingScreen.h"

namespace
{
    /// <summary>Puts string <paramref name="id"/> on the ticker.</summary>
    void ShowHelp(uint32_t id)
    {
        GlobalLog()->Ticker->SetString(LoadGameString(id, 0xfe));
    }

    /// <summary>Whether (<paramref name="xPos"/>, <paramref name="yPos"/>) is in the box, right and bottom edges excluded.</summary>
    bool Inside(int32_t xPos, int32_t yPos, int32_t left, int32_t top, int32_t right, int32_t bottom)
    {
        return left <= xPos && xPos < right && top <= yPos && yPos < bottom;
    }
}

auto MCPurchaseScreen::Init() -> void
{
    ChatBlinking = false;
    StoreLayouts = {};
    const int32_t result = MCLogObject::Init(0, 0, 0x280, 0x1e0);
    Assert(result == 0, static_cast<uint32_t>(result), "Unable to init purchase screen");
    // The original loaded the background (lspbk00) as the screen's picture and pasted the mech inventory header into
    // it; the screen draws both each frame.
    InitLive("lspbk00.tga");
    Info.Header = 0;
    MakePanes(true);
    ShowGuiWindow(false);

    auto loadTab = [](std::string_view name)
    {
        auto port = std::make_shared<MCLogPort>();
        port->Load(std::format("{}logart\\{}", ArtPath, name));
        return port;
    };

    MechTabPort = loadTab("lspbim00.tga");
    PilotTabPort = loadTab("lspbip00.tga");
    CompTabPort = loadTab("lspbic00.tga");
    VehicleTabPort = loadTab("lspbiv00.tga");
}

auto MCPurchaseScreen::OnDestroy() -> void
{
    MechTabPort.reset();
    PilotTabPort.reset();
    CompTabPort.reset();
    VehicleTabPort.reset();
    FreePanes();
    MCLogObject::OnDestroy();
}

auto MCPurchaseScreen::DrawBackground() -> void
{
    // The background art was painted over everything the screen showed.
    ScreenChrome.Clear();
    Info.Clear();
}

auto MCPurchaseScreen::ShowHelpFor(int32_t xPos, int32_t yPos) -> void
{
    // The help line for whatever the mouse is over, and the highlighted screen button.
    GlobalLog()->DrawScreenButtons();
    DrawBlankInvInfoBlock(-1);

    auto inside = [xPos, yPos](int32_t left, int32_t top, int32_t right, int32_t bottom)
    { return Inside(xPos, yPos, left, top, right, bottom); };

    if (inside(2, 2, 0xd0, 0xd))
    {
        ShowHelp(0x1d);
    }
    else if (inside(2, 0x10, 0xd0, 0x21))
    {
        ShowHelp(0x286);
        MCLogistics::HoverScreenButton(*this, 0);
    }
    else if (inside(2, 0x22, 0xd0, 0x33))
    {
        ShowHelp(0x1e);
        MCLogistics::HoverScreenButton(*this, 1);
    }
    else if (inside(2, 0x34, 0xd0, 0x45))
    {
        ShowHelp(0x41);
    }
    else if (inside(2, 0x46, 0xd0, 0x57))
    {
        ShowHelp(0x42);
        MCLogistics::HoverScreenButton(*this, 3);
    }
    else if (inside(0x20c, 2, 0x24d, 0xd))
    {
        ShowHelp(0x1f);
    }
    else if (inside(0xc6, 0x66, 0xcf, 0x95))
    {
        ShowHelp(0x29);
    }
    else if (inside(0xc6, 0x97, 0xcf, 0xef))
    {
        ShowHelp(0x2a);
    }
    else if (inside(0xc6, 0xf1, 0xcf, 0x140))
    {
        ShowHelp(0x2b);
    }
    else if (inside(0xc6, 0x141, 0xcf, 0x17c))
    {
        ShowHelp(0x2c);
    }
    else if (inside(7, 0x6a, 0xb3, 0x178))
    {
        // The inventory list: the pilot tab has its own line. There are only the four tabs.
        ShowHelp(GlobalLog()->CurrentInvTab == 1 ? 0x2e : 0x2d);
    }
    else if (inside(0x270, 0x10, 0x27d, 0x1dd))
    {
        ShowHelp(0x2f);
    }
    else if (inside(0xd3, 0x10, 0x270, 0x1dd))
    {
        ShowHelp(0x30);
    }
    else
    {
        GlobalLog()->Ticker->SetString({});
    }
}

auto MCPurchaseScreen::Click(int32_t xPos, int32_t yPos) -> void
{
    auto inside = [xPos, yPos](int32_t top, int32_t bottom, int32_t left, int32_t right)
    { return Inside(xPos, yPos, left, top, right, bottom); };

    if (inside(0x46, 0x57, 2, 0xd1))
    {
        GlobalLog()->SetUpRepairScreen(true);
        return;
    }

    if (inside(0x22, 0x33, 2, 0xd1))
    {
        GlobalLog()->SetUpBriefingScreen(true);
        return;
    }

    if (inside(0x10, 0x21, 2, 0xd1))
    {
        if (MultiPlayer() != nullptr)
        {
            CheckExit();
            return;
        }

        PlayLogSound(0x36);
        GlobalLog()->SetUpMainScreen(false);
        return;
    }

    // The inventory tabs.
    if (inside(0x67, 0x95, 0xc2, 0xd1))
    {
        PlayLogSound(0x36);
        SetUpMechInv(true, true);
        SetUpMechPurchase();
    }

    if (inside(0x96, 0xf1, 0xc2, 0xd1))
    {
        PlayLogSound(0x36);
        SetUpPilotInv(true, true);
        SetUpPilotPurchase();
    }

    if (inside(0xf2, 0x140, 0xc2, 0xd1))
    {
        PlayLogSound(0x36);
        SetUpCompInv(true, true);
        SetUpCompPurchase();
    }

    if (inside(0x141, 0x17c, 0xc2, 0xd1))
    {
        PlayLogSound(0x36);
        SetUpVhclInv(true, true);
        SetUpVehiclePurchase();
    }
}

auto MCPurchaseScreen::HandleEvent(MCGuiEvent& event) -> void
{
    if (GlobalLog()->CurrentScreen().get() != this)
    {
        return;
    }

    const uint8_t key = event.Key;

    if (key == 0 && event.Type != 0x13)
    {
        ShowHelpFor(event.X, event.Y);
    }

    switch (event.Type)
    {
        case 1:
            Click(event.X - GlobalX(), event.Y - GlobalY());
            break;
        case 9:
        {
            bool ctrlAlt = MCInput::GetAsyncKeyState(VK_CONTROL) != 0 && MCInput::GetAsyncKeyState(VK_MENU) != 0;

            // Original behaviour (OB-077): the zero-extended key never equals -0x45, so the cheat never fires.
            if (MultiPlayer() == nullptr && static_cast<int32_t>(key) == -0x45 && ctrlAlt)
            {
                ResourcePoints += 1000;
            }
            break;
        }

        case 8:
            GlobalLog()->ProcessCheatCode(event.ScanCode);
            break;
        case 0x13:
        {
            // Blink the briefing button.
            ScreenChrome.BlinkLit = _BriefingBlink;
            _BriefingBlink = !_BriefingBlink;
            break;
        }

        default:
            break;
    }
}

auto MCPurchaseScreen::ShowGuiWindow(bool show) -> void
{
    ShowWindow = show;
    InventoryPane->ShowGuiWindow(show);
    UnitPane->ShowGuiWindow(show);
}

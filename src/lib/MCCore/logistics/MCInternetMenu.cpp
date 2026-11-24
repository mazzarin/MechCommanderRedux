#include "stdafx.h"
#include "logistics/MCInternetMenu.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCIdleCheck.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCUpdatePage.h"
#include "main/MCUpdater.h"
#include "network/MCLobbyUpdate.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCArtWords.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCGamesScreen.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogGroupBox.h"
#include "logistics/MCLogLabel.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogToolButton.h"
#include "logistics/MCLogPort.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "network/MCInternetGame.h"
#include "network/MCMultiPlayer.h"
#include "vfx/MCVfxFunctions.h"

#include <charconv>

namespace
{
    // ---- MULTIPLAYER ACCESS (mpscreen.fit) ----

    /// <summary>Its buttons: MODEM, SERIAL, LAN (NETWORK's place), INTERNET.</summary>
    constexpr int32_t ModemButton = 2;
    constexpr int32_t SerialButton = 3;
    constexpr int32_t LanButton = 4;
    constexpr int32_t InternetButton = 5;
    /// <summary>
    /// Where its fit puts the buttons. INTERNET takes the first place, its count the second's room, and LAN stays in
    /// NETWORK's place; the original's INTERNET is the fourth.
    /// </summary>
    constexpr int32_t ButtonsLeft = 390;
    constexpr int32_t FirstPlaceTop = 152;
    constexpr int32_t NetworkPlaceTop = 214;
    constexpr int32_t InternetPlaceTop = 245;
    constexpr int32_t BigButtonWidth = 175;
    constexpr int32_t BigButtonHeight = 28;
    /// <summary>The count's line, under INTERNET.</summary>
    constexpr int32_t CountGap = 4;
    /// <summary>The access screen's timer (the count's answer looked for).</summary>
    constexpr int16_t AccessTimer = 1;
    constexpr int32_t AccessTimerMs = 250;
    constexpr std::string_view OnlineFormat = "{} ONLINE";
    /// <summary>The original's NETWORK pictures, put back for the original's connection screen.</summary>
    constexpr std::string_view NetworkUp = "bh_lan.tga";
    constexpr std::string_view NetworkDown = "bg_lan.tga";
    constexpr std::string_view NetworkGray = "bn_lan.tga";

    /// <summary>MULTIPLAYER ACCESS's count's line, and the count being asked.</summary>
    MCGuiHandle<MCLogLabel> CountLine;
    std::unique_ptr<MCInternetCount> Count;

    /// <summary>MULTIPLAYER ACCESS's events: its timer, the count's answer looked for.</summary>
    void AccessScreenHandleEvent(MCGuiObject&, MCGuiEvent& event)
    {
        if (event.Type != MCGuiEventType::Timer || event.Data != AccessTimer || Count == nullptr || !Count->Done())
        {
            return;
        }

        // Nothing when the service can't be reached.
        if (const std::optional<int32_t> online = Count->Online())
        {
            CountLine->SetText(std::format(OnlineFormat, *online));
            CountLine->MoveTo(ButtonsLeft + (BigButtonWidth - CountLine->Width()) / 2, CountLine->Y());
            CountLine->ShowGuiWindow(true);
        }

        Count.reset();
        GuiSystem().RemoveTimer(*GlobalLog()->MultiplayerScreen, AccessTimer);
    }

    /// <summary>Lays the access screen's buttons: INTERNET first, its count, then LAN; or the original's four.</summary>
    void LayAccessButtons(bool lanAndInternet)
    {
        MCSplashScreen& screen = *GlobalLog()->MultiplayerScreen;
        screen.Element<MCGuiObject>(ModemButton)->ShowGuiWindow(!lanAndInternet);
        screen.Element<MCGuiObject>(SerialButton)->ShowGuiWindow(!lanAndInternet);
        auto lan = screen.Element<MCLogButton>(LanButton);
        auto internet = screen.Element<MCLogButton>(InternetButton);
        const int32_t internetTop = lanAndInternet ? FirstPlaceTop : InternetPlaceTop;
        lan->MoveTo(ButtonsLeft, NetworkPlaceTop);
        internet->MoveTo(ButtonsLeft, internetTop);
        CountLine->MoveTo(CountLine->X(), internetTop + BigButtonHeight + CountGap);

        // LAN's pictures are made of the art's letters (and their HD copies, made again as HD art may have come on).
        std::optional<MCButtonPictures> pictures = lanAndInternet ? MCLanButtonPictures() : std::nullopt;

        if (pictures.has_value())
        {
            lan->UpPicture = pictures->Up;
            lan->OverPicture = pictures->Up;
            lan->DownPicture = pictures->Down;
            lan->GrayPicture = pictures->Gray;
        }
        else
        {
            lan->SetUpPicture(NetworkUp);
            lan->SetOverPicture(NetworkUp);
            lan->SetDownPicture(NetworkDown);
            lan->SetGrayPicture(NetworkGray);
        }
    }
}

bool HasInternetGames()
{
    return MCInternetGame::Available();
}

void SetUpAccessScreen(MCSplashScreen& screen)
{
    screen.SetEventRoutine(AccessScreenHandleEvent);
    CountLine = screen.CreateChild<MCLogLabel>(
        [](MCLogLabel& label)
        {
            label.InitAt(ButtonsLeft, InternetPlaceTop + BigButtonHeight + CountGap);
            label.ShowGuiWindow(false);
        },
        std::string_view());
}

void ShowAccessScreen()
{
    MCSplashScreen& screen = *GlobalLog()->MultiplayerScreen;
    LayAccessButtons(true);
    screen.Element<MCLogButton>(LanButton)->Disabled = false;
    screen.Element<MCLogButton>(InternetButton)->Disabled = false;
    GlobalLog()->MainScreen->ShowGuiWindow(false);
    screen.ShowGuiWindow(true);
    GlobalLog()->SetCurrentScreen(screen);
    GlobalLog()->LogisticsState = 10;

    // The count, asked once as it opens: shown when it comes, nothing when the service can't be reached.
    CountLine->ShowGuiWindow(false);
    Count = MCInternetCount::Ask();

    if (Count != nullptr)
    {
        GuiSystem().AddTimer(screen.weak_from_this(), AccessTimer, AccessTimerMs, 0, 0, false);
    }
}

void ShowOriginalAccessButtons()
{
    LayAccessButtons(false);
    CountLine->ShowGuiWindow(false);
}

void AccessScreenHidden()
{
    if (GlobalLog() == nullptr || GlobalLog()->MultiplayerScreen == nullptr)
    {
        return;
    }

    Count.reset();
    GuiSystem().RemoveTimer(*GlobalLog()->MultiplayerScreen, AccessTimer);
}

void ReleaseInternetMenus()
{
    Count.reset();
    ReleaseGamesScreens();
}

std::string AccessCountShown()
{
    return CountLine != nullptr && CountLine->ShowWindow ? CountLine->Text() : std::string();
}

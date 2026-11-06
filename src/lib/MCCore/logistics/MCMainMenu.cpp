#include "stdafx.h"
#include "logistics/MCMainMenu.h"
#include "ai/MCMoveGeometry.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCReusableDialog.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCInput.h"
#include "sound/MCSoundSystem.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCSystemConfig.h"
#include "main/MCGameSession.h"
#include "linkup/MCSessionManager.h"

namespace
{
    /// <summary>
    /// Starts campaign <paramref name="campaign"/> from its start file <paramref name="startFile"/> on the briefing
    /// screen; <paramref name="firstPlanet"/> starts it on the first planet (the expansion's keeps the planet it had).
    /// </summary>
    void StartCampaign(std::string_view campaign, std::string_view startFile, bool firstPlanet)
    {
        EnsureRegistryVersion();
        SoundSystem()->StopDigitalMusic();
        SoundSystem()->PlayBettySample(0x19);
        MissionName = campaign;
        Mission()->ReloadCampaign(MissionName);

        if (firstPlanet)
        {
            CurPlanet = 0;
        }

        Solo = false;
        LastLogisticsMissionState = 0;
        GlobalLog()->LoadCampaign(startFile, ".pkk", false, false);
        GlobalLog()->BriefingScreen->HideBriefing();
        GlobalLog()->SetUpBriefingScreen(false);
        GlobalLog()->MainScreen->ShowGuiWindow(false);
    }
}

// Each menu action first made sure a campaign CD was in a drive (scanning C: to Z: for data\tiles\gtiles90.pak,
// pointing every drive-letter path at it, and asking for the disc when none had it; the dialog's OK retried the
// action). The port reads everything from the install folder, so the disc is always there (as checkForCDInDrive
// says); only the version check is kept.

void NewCampaign()
{
    StartCampaign("mechcmdr1", "start0", true);
}

void NewMcxCampaign()
{
    StartCampaign("xmechcmdr1", "xstart0", false);
}

void ShowMultiPlayer()
{
    GlobalLog()->MainScreen->ShowGuiWindow(false);
    GlobalLog()->MultiplayerScreen->ShowGuiWindow(true);
    Solo = false;
    GlobalLog()->SetCurrentScreen(GlobalLog()->MultiplayerScreen);
    GlobalLog()->LogisticsState = 10;
}

void ReplayCinema()
{
    EnsureRegistryVersion();
    Mission()->NextState = MCMissionState::PlayMovie;
    Mission()->State = MCMissionState::PlayMovie;
    Mission()->CurrentMovie = 0;
    GlobalLog()->MainScreen->ShowGuiWindow(false);
    SoundSystem()->StopDigitalMusic();
}

void ReturnToGame()
{
    if (GlobalLog()->CurrentMission == -1)
    {
        return;
    }

    if (GlobalLog()->PreviousState == 2)
    {
        GlobalLog()->SetUpPurchaseScreen(false);
    }
    else if (GlobalLog()->PreviousState == 4)
    {
        GlobalLog()->SetUpRepairScreen(false);
    }
    else
    {
        GlobalLog()->SetUpBriefingScreen(false);
    }

    GlobalLog()->MainScreen->ShowGuiWindow(false);
}

void GameOverMan()
{
    MCInput::PostMessage(WM_DESTROY, 0, 0);
    SoundSystem()->StopDigitalMusic();
}

void Cancel()
{
    if (GlobalLog()->CurrentScreen() == GlobalLog()->MainScreen)
    {
        return;
    }

    WhackTimer = true;
    GlobalLog()->SetUpMainScreen(true);
    LoadingSolo = false;
}

void DoExit()
{
    if (MultiPlayer() != nullptr)
    {
        if (LaunchedFromLobby != 0)
        {
            KillTheGame();
            return;
        }

        GlobalLog()->DestroyMultiplayer();
        MultiPlayer()->LeaveSession();
    }

    GlobalLog()->SetUpMainScreen(false);
}

void CheckExit()
{
    MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
    dialog.SetText(LoadGameString(0xaf, 0xfe));
    dialog.SetTwoButton(true);
    // The dialog's callback and the cancel button's action are left as they were.
    dialog.OkButton->SetUpPicture("bh_okay.tga");
    dialog.OkButton->SetDownPicture("bg_okay.tga");
    dialog.OkButton->Disabled = false;
    dialog.OkButton->Callback().SetExec(DoExit);
    dialog.CancelButton->SetUpPicture("bh_cancl.tga");
    dialog.CancelButton->SetDownPicture("bh_cancl.tga");
    dialog.CancelButton->Disabled = false;
    dialog.Activate();
}

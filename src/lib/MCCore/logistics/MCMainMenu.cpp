#include "stdafx.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCMainMenu.h"
#include "ai/MCMoveGeometry.h"
#include "gui/MCUpdateDisplay.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCMenuDialog.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCInput.h"
#include "platform/MCFrameLog.h"
#include "sound/MCSoundSystem.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCSystemConfig.h"
#include "main/MCGameSession.h"
#include "linkup/MCSessionManager.h"

namespace
{
    /// <summary>
    /// Campaign <paramref name="campaign"/>'s start (BeginCampaign's) as pieces, for a change's job: the campaign read
    /// again, then its load from start file <paramref name="startFile"/>, a step a piece; <paramref name="firstPlanet"/>
    /// starts it on the first planet (the expansion's keeps the planet it had).
    /// </summary>
    std::vector<MCScreenChange::MCPiece> CampaignStartPieces(std::string campaign, std::string_view startFile,
                                                             bool firstPlanet)
    {
        std::vector<MCScreenChange::MCPiece> pieces = {{.Part = "campaign.reload",
                                                        .Run = [campaign = std::move(campaign), firstPlanet]
                                                        {
                                                            MissionName = campaign;
                                                            Mission()->ReloadCampaign(MissionName);

                                                            if (firstPlanet)
                                                            {
                                                                CurPlanet = 0;
                                                            }

                                                            Solo = false;
                                                            LastLogisticsMissionState = 0;
                                                        }}};
        // A start file it refuses leaves the briefing shown over no campaign, as the original's.
        std::ranges::move(GlobalLog()->LoadCampaignPieces(startFile, ".pkk", false, false, std::make_shared<int32_t>()),
                          std::back_inserter(pieces));
        return pieces;
    }

    /// <summary>The briefing of the campaign started, in place of the main menu (BeginCampaign's end).</summary>
    void ShowCampaignStarted()
    {
        GlobalLog()->BriefingScreen->HideBriefing();
        GlobalLog()->SetUpBriefingScreen(false);
        GlobalLog()->MainScreen->ShowGuiWindow(false);
    }

    /// <summary>
    /// Starts campaign <paramref name="campaign"/> from its start file <paramref name="startFile"/> on the briefing
    /// screen; <paramref name="firstPlanet"/> starts it on the first planet (the expansion's keeps the planet it had).
    /// </summary>
    void StartCampaign(std::string_view campaign, std::string_view startFile, bool firstPlanet)
    {
        {
            MCFrameLog::Scope probe("probe.press registry");
            EnsureRegistryVersion();
        }
        {
            MCFrameLog::Scope probe("probe.press music stop");
            SoundSystem()->StopDigitalMusic();
        }
        {
            MCFrameLog::Scope probe("probe.press betty");
            SoundSystem()->PlayBettySample(0x19);
        }

        // Port-only: the campaign started and its briefing set up in the fade, where the main menu sat frozen for it
        // before its fade began (64-65 ms on the A10, 18-38 on the M4 Pro); port-only (SPEC-menus.md's stage 2): in a
        // change of screen through the flow, the main menu fading out live, the briefing fading in. Betty's line and the
        // music's stop at the click; from it, live, the remade briefing's document read and the campaign loaded, a step
        // a frame; the briefing set up once the screen is dark.
        std::vector<MCScreenChange::MCPiece> job;
        {
            MCFrameLog::Scope probe("probe.press ahead");
            job = MCCampaignMenu::Ahead();
        }
        {
            MCFrameLog::Scope probe("probe.press pieces");
            std::ranges::move(CampaignStartPieces(std::string(campaign), startFile, firstPlanet), std::back_inserter(job));
        }
        MCFrameLog::Scope probe("probe.press change");
        MCScreenFlow::Change("new campaign", ShowCampaignStarted, std::move(job));
    }
}

void BeginCampaign(std::string_view campaign, std::string_view startFile, bool firstPlanet)
{
    MCScreenChange::RunPieces(CampaignStartPieces(std::string(campaign), startFile, firstPlanet));
    ShowCampaignStarted();
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
    // Port-only (SPEC-menus.md's stage 7): 1998's MULTIPLAYER screen (mpscreen.fit) isn't made; its button opens the
    // remade one, in the main menu's panel.
    ConnectScreen();
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
        // Port-only (SPEC-menus.md's stage 8): the remade briefing on the tab it was left on.
        MCCampaignMenu::ComeBack();
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

        LeaveMatch();
    }

    GlobalLog()->SetUpMainScreen(false);
}

void LeaveMatch()
{
    GlobalLog()->DestroyMultiplayer();
    MultiPlayer()->LeaveSession();
}

void CheckExit()
{
    MCMenuDialog& dialog = *GlobalLog()->Message;
    dialog.SetText(LoadGameString(0xaf, 0xfe));
    dialog.SetTwoButton(true);
    // The dialog's callback and the cancel button's action are left as they were.
    dialog.Ok.SetPictures("bh_okay.tga", "bg_okay.tga");
    dialog.Ok.Disabled = false;
    dialog.Ok.Exec = DoExit;
    dialog.Cancel.SetPictures("bh_cancl.tga", "bh_cancl.tga");
    dialog.Cancel.Disabled = false;
    dialog.Activate();
}

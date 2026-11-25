#include "stdafx.h"
#include "logistics/MCMainMenuModel.h"
#include "ai/MCMoveGeometry.h"
#include "gui/MCScreenFlow.h"
#include "gui/MCUpdateDisplay.h"
#include "lib/MCFile.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCMainMenuScreen.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCUpdateModel.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCGameSession.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "main/MCSystemConfig.h"
#include "main/MCUpdater.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "platform/MCSocket.h"
#include "sound/MCSoundSystem.h"

namespace
{
    using MCCampaign = MCSaves::MCCampaign;

    constexpr std::string_view StandsFormat = "Operation {}, mission {}";
    constexpr std::string_view LineFormat = "{} · {}";
    constexpr std::string_view UpdateFormat = "Update to {}";
    /// <summary>UPDATE for the HD art a program installed alone came for, which names no release to update to.</summary>
    constexpr std::string_view BareUpdate = "Update";
    constexpr std::string_view SoloFormat = "Buy {} tons with {} RP";
    constexpr std::string_view OnlineFormat = "{} players online";
    constexpr std::string_view OneOnline = "1 player online";
    constexpr std::string_view SoloReturn = "Solo mission";
    constexpr std::string_view SoloExtension = ".sol";
    /// <summary>The campaigns, and their own names under CAMPAIGNS' rows.</summary>
    constexpr std::array<MCCampaign, 2> CampaignOrder = {MCCampaign::Original, MCCampaign::Expansion};
    constexpr std::array<std::string_view, 2> CampaignTitles = {"MechCommander", "Desperate Measures"};
    /// <summary>The logistics state 1998's MULTIPLAYER ACCESS had.</summary>
    constexpr int32_t MultiplayerState = 10;

    /// <summary>The tests' network (<see cref="MCMainMenuModel::SetNetworkForTests"/>).</summary>
    std::optional<bool> NetworkForTests;

    /// <summary>The campaign the logistics plays (CurPlanet: Port Arthur's, or Cermak's in the expansion).</summary>
    MCCampaign PlayedCampaign()
    {
        return CurPlanet == 0 ? MCCampaign::Original : MCCampaign::Expansion;
    }

    std::string Stands(int32_t operation, int32_t mission)
    {
        return std::format(StandsFormat, operation, mission);
    }

    /// <summary>Where a save opens, as CONTINUE names it: "Original campaign · Operation 2, mission 3".</summary>
    std::string OpensAt(const MCSaves::MCSave& save)
    {
        return std::format(LineFormat, MCSaves::CampaignName(save.Campaign),
                           Stands(save.OpensOperation, save.OpensMission));
    }
}

auto MCMainMenuModel::Choices() -> MCChoices
{
    MCSaves& saves = MCSaves::Game();
    saves.Refresh();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    MCChoices choices;

    // Between missions (a campaign or a single mission loaded): RETURN TO GAME leads, and SAVE GAME follows LOAD GAME
    // (not after a single mission's, as 1998's SAVE MISSION).
    choices.Returns = logistics != nullptr && logistics->CurrentMission != -1;
    choices.Saves = choices.Returns && !Solo;

    if (choices.Returns)
    {
        const MCSaves::MCSave playing = MCSaves::Unsaved(PlayedCampaign(), logistics->CurrentMission, 0);
        choices.ReturnLine = Solo           ? std::string(SoloReturn)
                             : playing.Read ? std::format(LineFormat, MCSaves::CampaignName(playing.Campaign),
                                                          Stands(playing.Operation, playing.Mission))
                                            : std::string(MCSaves::CampaignName(playing.Campaign));
    }
    else if (const MCOptionalRef<const MCSaves::MCSave> latest = saves.Latest(); latest.has_value())
    {
        choices.Latest = latest->get().Name;
        choices.ContinueLine = OpensAt(latest->get());
    }

    choices.Solo = !MCFileSystem::FindFileSources(GamePath(SavePath, "*", SoloExtension)).empty();
    return choices;
}

auto MCMainMenuModel::Campaigns() -> MCCampaignList
{
    MCSaves& saves = MCSaves::Game();
    saves.Refresh();
    MCCampaignList list;
    std::optional<size_t> newest;
    SDL_Time newestSaved = 0;

    for (size_t index = 0; index < CampaignOrder.size(); index++)
    {
        const MCCampaign campaign = CampaignOrder[index];
        const MCOptionalRef<const MCSaves::MCSave> latest = saves.Latest(campaign);
        const std::string stands = latest.has_value() ? Stands(latest->get().OpensOperation, latest->get().OpensMission)
                                                      : std::string(NotStarted);
        list.Rows.push_back({.Title = std::string(MCSaves::CampaignName(campaign)),
                             .Line = std::format(LineFormat, CampaignTitles[index], stands)});
        list.Campaigns.push_back(campaign);
        list.Latest.push_back(latest.has_value() ? latest->get().Name : std::string());

        if (latest.has_value() && (!newest.has_value() || latest->get().Saved > newestSaved))
        {
            newest = index;
            newestSaved = latest->get().Saved;
        }
    }

    list.Opens = static_cast<int>(newest.value_or(0));
    return list;
}

auto MCMainMenuModel::SoloMissions() -> MCSoloList
{
    MCSoloList list;

    for (const MCSaves::MCSolo& solo : MCSaves::SoloMissions())
    {
        list.Rows.push_back({.Title = solo.Read ? solo.MissionName : solo.Name,
                             .Line = solo.Read
                                         ? std::format(SoloFormat, solo.Tons, MCSaves::PointsText(solo.ResourcePoints))
                                         : std::string()});
        list.Files.push_back(solo.Name);
    }

    return list;
}

auto MCMainMenuModel::Foot() -> MCFoot
{
    MCFoot foot{.Version = VersionLineText(MCUpdater::RunningVersion())};

    if (!MCUpdater::Available() || !MCUpdater::Waiting())
    {
        return foot;
    }

    const MCUpdater::MCStatus& status = MCUpdater::Status();
    foot.Art = DueArtLineText();

    // The art alone: a bare UPDATE while it downloads (its page has the meter and STOP) and once it failed (its page says
    // why and tries again); once it's in, RESTART in UPDATE's place, the page's action being the same RESTART.
    if (MCUpdater::DueArtAlone())
    {
        if (!foot.Art.empty() && status.Stage != MCUpdater::MCStage::Downloading)
        {
            foot.Restart = UpdateRestartOffered();
        }
        else
        {
            foot.Update = std::string(BareUpdate);
        }

        return foot;
    }

    // A release due besides the art keeps its UPDATE beside the art's line; the foot has no room for RESTART too, so
    // UPDATE's page has it, putting the release and the art in together.
    foot.Update = std::format(UpdateFormat, ReleaseOf(status.Version));
    return foot;
}

auto MCMainMenuModel::RestartForArt() -> void
{
    if (UpdateRestartOffered())
    {
        RestartForUpdate();
    }
}

auto MCMainMenuModel::EnterMultiplayer() -> void
{
    EnsureRegistryVersion();
    Solo = false;

    if (MultiPlayer() == nullptr)
    {
        MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
        MultiPlayer()->Start();
    }

    if (const std::shared_ptr<MCLogistics> logistics = GlobalLog(); logistics != nullptr)
    {
        logistics->LogisticsState = MultiplayerState;
    }
}

auto MCMainMenuModel::LookForNetwork() -> MCNetwork
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const bool sockets = game != nullptr && game->SessionManager != nullptr && game->SessionManager->IsTcpAvailable();
    const MCNetwork network{.Network = NetworkForTests.value_or(sockets && MCSocketTransport::HasNetwork()),
                            .Internet = HasInternetGames()};

    if (!network.Network)
    {
        SDL_Log("Menus: MULTIPLAYER finds no network (%s)", sockets ? "no interface is up" : "sockets can't start");
    }

    return network;
}

auto MCMainMenuModel::OnlineText(int32_t online) -> std::string
{
    return online == 1 ? std::string(OneOnline) : std::format(OnlineFormat, online);
}

auto MCMainMenuModel::SetNetworkForTests(std::optional<bool> network) -> void
{
    NetworkForTests = network;
}

auto MCMainMenuModel::Play(const std::string& name, bool campaign) -> MCSavedGameLoad
{
    LoadingSolo = false;
    return MCSaves::LoadSavedGameAhead(name, campaign);
}

auto MCMainMenuModel::Played(const std::string& name, int32_t result) -> bool
{
    if (MCSaves::ShowSavedGame(result))
    {
        return true;
    }

    SDL_Log("Menus: the save %s couldn't be loaded", name.c_str());
    return false;
}

auto MCMainMenuModel::NewCampaign(MCSaves::MCCampaign campaign) -> void
{
    if (campaign == MCCampaign::Expansion)
    {
        NewMcxCampaign();
    }
    else
    {
        ::NewCampaign();
    }
}

auto MCMainMenuModel::ReturnToGame() -> void
{
    ::ReturnToGame();
}

auto MCMainMenuModel::Cinema() -> void
{
    ReplayCinema();
}

auto MCMainMenuModel::ToMainMenu() -> void
{
    WhackTimer = true;
    GlobalLog()->SetUpMainScreen(true);
    LoadingSolo = false;
    // Swapped in place, outside a change of screen: the palette 1998's main menu sets goes in at once. No 1998 screen
    // shows, and the next frame's palette fade would darken the remade menu to black (2.2 s on this Mac at 60 Hz).
    ActivatePaletteNow();
}

// ---- The 1998 main menu's callbacks (logistics\logmain.cpp) ----

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
        GlobalLog()->SetUpBriefingScreen(false);
    }

    /// <summary>
    /// Starts campaign <paramref name="campaign"/> from its start file <paramref name="startFile"/> on the briefing
    /// screen; <paramref name="firstPlanet"/> starts it on the first planet (the expansion's keeps the planet it had).
    /// </summary>
    void StartCampaign(std::string_view campaign, std::string_view startFile, bool firstPlanet)
    {
        EnsureRegistryVersion();
        SoundSystem()->StopDigitalMusic();
        SoundSystem()->PlayBettySample(0x19);

        // Port-only: the campaign started and its briefing set up in the fade, where the main menu sat frozen for it
        // before its fade began (64-65 ms on the A10, 18-38 on the M4 Pro); port-only (SPEC-menus.md's stage 2): in a
        // change of screen through the flow, the main menu fading out live, the briefing fading in. Betty's line and the
        // music's stop at the click; from it, live, the remade briefing's document read and the campaign loaded, a step
        // a frame; the briefing set up once the screen is dark.
        std::vector<MCScreenChange::MCPiece> job = MCCampaignMenu::Ahead();
        std::ranges::move(CampaignStartPieces(std::string(campaign), startFile, firstPlanet), std::back_inserter(job));
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
    // remade one, in the main menu's panel, which makes the multiplayer game as it shows (INTERNET and LAN, or "No
    // network connection."). Shown at once: the old code's way to it runs in a change's dark work.
    MCMainMenuScreens::ShowNow(MCMainPanel::Multiplayer);
}

void ReplayCinema()
{
    EnsureRegistryVersion();
    Mission()->NextState = MCMissionState::PlayMovie;
    Mission()->State = MCMissionState::PlayMovie;
    Mission()->CurrentMovie = 0;
    SoundSystem()->StopDigitalMusic();
}

void ReturnToGame()
{
    if (GlobalLog()->CurrentMission == -1)
    {
        return;
    }

    // Port-only (SPEC-menus.md's stage 8): the remade briefing on the tab it was left on (1998's PURCHASING and 'MECH
    // BAY, its own screens, came back as they were left: the remade campaign's pages are its tabs).
    MCCampaignMenu::ComeBack();
    GlobalLog()->SetUpBriefingScreen(false);
}

void GameOverMan()
{
    MCInput::PostMessage(WM_DESTROY, 0, 0);
    SoundSystem()->StopDigitalMusic();
}

void Cancel()
{
    if (MCMenuScreens::Top() == MCMenuScreens::Main())
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

#include "stdafx.h"
#include "logistics/MCMainMenuModel.h"
#include "ai/MCMoveGeometry.h"
#include "gui/MCUpdateDisplay.h"
#include "lib/MCFile.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCUpdatePage.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "main/MCUpdater.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCFileSystem.h"
#include "platform/MCSocket.h"

namespace
{
    using MCCampaign = MCSaves::MCCampaign;

    constexpr std::string_view StandsFormat = "Operation {}, mission {}";
    constexpr std::string_view LineFormat = "{} · {}";
    constexpr std::string_view UpdateFormat = "Update to {}";
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
    const bool waiting = MCUpdater::Available() && MCUpdater::Waiting();
    return {.Version = VersionLineText(MCUpdater::RunningVersion()),
            .Update = waiting ? std::format(UpdateFormat, ReleaseOf(MCUpdater::Status().Version)) : std::string()};
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

auto MCMainMenuModel::Play(const std::string& name, bool campaign) -> bool
{
    LoadingSolo = false;

    if (LoadSavedGame(name, campaign))
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

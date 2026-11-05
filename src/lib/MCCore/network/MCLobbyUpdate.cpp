#include "stdafx.h"
#include "network/MCLobbyUpdate.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCLogChatWindow.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "main/MCUpdater.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerMessages.h"
#include "network/MCRejoinNote.h"
#include "platform/MCFileSystem.h"

namespace
{
    using Clock = std::chrono::steady_clock;

    /// <summary>
    /// How long RESTART waits before the update installs, so its messages leave this game first (the game keeps
    /// sending and taking messages each frame meanwhile).
    /// </summary>
    constexpr std::chrono::milliseconds SendTime{500};
    /// <summary>How often a guest waiting for its host tries to join again.</summary>
    constexpr std::chrono::milliseconds TryEvery{500};
    /// <summary>How long the host lets a guest back in settle (numbered by the session) before showing it the room.</summary>
    constexpr std::chrono::seconds SettleTime{1};

    /// <summary>The logistics state of the room's waiting screen (the connect screen), as a lobby's start shows it.</summary>
    constexpr int32_t WaitingState = 0xe;
    /// <summary>The original's "Your connection to the game was unsuccesful..." (a lobby's join that failed).</summary>
    constexpr uint32_t JoinFailedString = 0x370;
    constexpr int32_t StringBuffer = 0xfe;

    /// <summary>What a rejoin that failed says: no room at the address, a full one, one whose match started.</summary>
    constexpr std::string_view RoomGoneText = "Couldn't join the room again: it has closed.";
    constexpr std::string_view RoomFullText = "Couldn't join the room again: it is full.";
    constexpr std::string_view RoomStartedText = "Couldn't join the room again: its match has started.";

    /// <summary>A guest restarting into an update, whose place the room keeps until it is back or the wait is over.</summary>
    struct MCComing
    {
        uint32_t Id = 0;
        std::string Name;
        Clock::time_point Until;
    };

    /// <summary>A guest's wait for its host, restarting into an update.</summary>
    struct MCHostWait
    {
        Clock::time_point Until;
        Clock::time_point NextTry;
    };

    /// <summary>RESTART was taken: when the update installs; this game then quits.</summary>
    std::optional<Clock::time_point> RestartAt;
    bool Restarting = false;
    /// <summary>The guests restarting into an update, by the order they said so.</summary>
    std::vector<MCComing> Coming;
    /// <summary>The host shows its room again once a guest back from its update has settled.</summary>
    std::optional<Clock::time_point> RefreshAt;
    std::optional<MCHostWait> HostWait;

    /// <summary><paramref name="text"/> in capitals, as the menus write names.</summary>
    std::string Upper(std::string_view text)
    {
        std::string upper(text);
        std::ranges::transform(upper, upper.begin(),
                               [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
        return upper;
    }

    /// <summary>What the room's other games say of player <paramref name="name"/> restarting into an update.</summary>
    std::string UpdatingText(std::string_view name)
    {
        return std::format("{} IS UPDATING.", Upper(name));
    }

    /// <summary>The players in the room other than this game's.</summary>
    std::vector<std::shared_ptr<MCFidpPlayer>> Others()
    {
        MCSessionManager& manager = *MultiPlayer()->SessionManager;
        std::vector<std::shared_ptr<MCFidpPlayer>> others;
        std::ranges::copy_if(manager.GetPlayers(nullptr), std::back_inserter(others),
                             [&](const std::shared_ptr<MCFidpPlayer>& player)
                             { return player != nullptr && player->Id != manager.MyPlayerID; });
        return others;
    }

    /// <summary>Whether <paramref name="player"/>'s build waits for its host restarting into an update.</summary>
    bool WaitsForHost(const MCFidpPlayer& player)
    {
        const auto said = MultiPlayer()->PlayerRules.find(player.Id);
        return said != MultiPlayer()->PlayerRules.end() && (said->second & MCMatchRule::RejoinsHost) != 0;
    }

    /// <summary>Sends this game's Updating to player <paramref name="to"/> (0: every player).</summary>
    void SendUpdating(uint32_t to)
    {
        MCMPLongMessage message{};
        message.Header = PlainHeader(MCMPMessageType::Updating);
        MultiPlayer()->SessionManager->SendPlainMessage(to, LinkupBytes(message));
    }

    /// <summary>The name this game takes in the room (as SetupLobbyConnection takes it).</summary>
    std::string PlayerName()
    {
        return !LobbyLaunchArgs.PlayerName.empty() ? LobbyLaunchArgs.PlayerName : MyGetUserName().value_or("Player");
    }

    /// <summary>The address of the room (as SetupLobbyConnection joins it).</summary>
    std::string RoomAddress()
    {
        constexpr std::string_view OwnMachine = "127.0.0.1";
        return !LobbyLaunchArgs.Address.empty() ? LobbyLaunchArgs.Address : std::string(OwnMachine);
    }

    /// <summary>The message dialog saying <paramref name="text"/>, with no button: the game waits.</summary>
    void ShowWaiting(std::string_view text)
    {
        MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
        dialog.SetText(text);
        dialog.OkButton->ShowGuiWindow(false);
        dialog.CancelButton->ShowGuiWindow(false);
        dialog.Callback = nullptr;
        dialog.Activate();
    }

    /// <summary>The room's waiting screen, as a lobby's start shows it (its button ends the game).</summary>
    void ShowWaitingScreen()
    {
        MCLogistics& logistics = *GlobalLog();
        logistics.CurrentScreen()->ShowGuiWindow(false);
        logistics.SetCurrentScreen(logistics.ConnectScreen);
        logistics.LogisticsState = WaitingState;
        logistics.ShowLogScreen(true, true);
    }

    /// <summary>
    /// The host (<paramref name="hostId"/>) restarts into an update: this guest leaves the room at once, says so, and
    /// joins the same address again (Frame) for up to a minute.
    /// </summary>
    void WaitForHost(uint32_t hostId)
    {
        MCMultiPlayer& game = *MultiPlayer();
        const std::shared_ptr<MCFidpPlayer> host = game.SessionManager->GetPlayer(hostId);
        const std::string name = host != nullptr ? host->Name : std::string("THE HOST");
        SDL_Log("Lobby updates: the host %s restarts into an update; joining %s again", name.c_str(),
                RoomAddress().c_str());
        GlobalLog()->DestroyMultiplayer();
        game.LeaveSession();
        game.PlayerRules.clear();
        Coming.clear();
        RefreshAt.reset();
        game.SessionManager->ConnectTcp(RoomAddress());
        ShowWaitingScreen();
        ShowWaiting(UpdatingText(name));
        const Clock::time_point now = Clock::now();
        HostWait = MCHostWait{.Until = now + MCLobbyUpdate::RejoinWait, .NextTry = now};
    }

    /// <summary>Says a join failed with <paramref name="text"/>; its OK ends the game, back to the lobby.</summary>
    void ShowJoinFailed(std::string_view text)
    {
        MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
        dialog.SetText(text);
        dialog.SetTwoButton(false);
        dialog.Callback = nullptr;
        dialog.OkButton->ShowGuiWindow(true);
        dialog.OkButton->Callback().SetExec(CancelToMPlayer);
        dialog.OkButton->SetUpPicture("bh_okay.tga");
        dialog.OkButton->SetDownPicture("bg_okay.tga");
        dialog.OkButton->Disabled = false;
        dialog.Activate();
    }

    /// <summary>The guest's next try at joining its host's room again, or the end of its wait.</summary>
    void TryHostAgain(Clock::time_point now)
    {
        MCSessionManager& manager = *MultiPlayer()->SessionManager;
        const int32_t tried = manager.TryLobbyJoin(PlayerName());

        if (tried == 0)
        {
            SDL_Log("Lobby updates: back in the room");
            HostWait.reset();
            MultiPlayer()->StartLobbyGame();
            DestroyConnectStatusWindow();
            return;
        }

        // A room that refused the join (the host's old one, closed, still answering) is asked again afresh.
        if (tried != 1)
        {
            manager.ConnectTcp(RoomAddress());
        }

        if (now < HostWait->Until)
        {
            HostWait->NextTry = now + TryEvery;
            return;
        }

        const MCRoomRefusal refusal = manager.LobbyRoomRefusal();
        SDL_Log("Lobby updates: the host's room didn't come back");
        HostWait.reset();
        ShowJoinFailed(MCLobbyUpdate::RejoinFailedText(refusal));
    }

    /// <summary>The host's places kept for updating guests: those past their wait go as the original's leaving does.</summary>
    void KeepPlaces(Clock::time_point now)
    {
        const bool host = MultiPlayer()->IsHost;

        for (auto coming = Coming.begin(); coming != Coming.end();)
        {
            if (now < coming->Until)
            {
                ++coming;
                continue;
            }

            SDL_Log("Lobby updates: %s didn't come back", coming->Name.c_str());
            const std::string name = coming->Name;
            coming = Coming.erase(coming);

            if (host && Coming.empty())
            {
                MultiPlayer()->SessionManager->AdmitNewPlayers(false);
            }

            if (GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen)
            {
                ShowPlayerLeft(name);
            }
        }
    }
}

auto MCLobbyUpdate::InRoom() -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    return LobbyLaunchArgs.Active && game != nullptr && game->SessionManager->CurrentSession() != nullptr;
}

auto MCLobbyUpdate::MatchUnderWay() -> bool
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    return game != nullptr &&
           (game->InLogistics || game->InMission || (logistics != nullptr && logistics->MultiplayerInitialized));
}

auto MCLobbyUpdate::ReadyText() -> std::string
{
    if (!InRoom())
    {
        return {};
    }

    if (MatchUnderWay())
    {
        return "DOWNLOADED: IT INSTALLS WHEN THE MATCH IS OVER.";
    }

    if (!MultiPlayer()->IsHost)
    {
        return "DOWNLOADED: ON RESTART, YOU JOIN THIS ROOM AGAIN.";
    }

    const std::vector<std::shared_ptr<MCFidpPlayer>> others = Others();

    if (others.empty())
    {
        return "DOWNLOADED: ON RESTART, THE ROOM OPENS AGAIN.";
    }

    const auto rejoining = std::ranges::count_if(others, [](const auto& player) { return WaitsForHost(*player); });
    return std::format("DOWNLOADED: ON RESTART, {} OF {} {} {} AGAIN.", rejoining, others.size(),
                       others.size() == 1 ? "PLAYER" : "PLAYERS", rejoining == 1 ? "JOINS" : "JOIN");
}

auto MCLobbyUpdate::Restart() -> bool
{
    if (!InRoom())
    {
        return false;
    }

    if (RestartAt.has_value() || Restarting)
    {
        return true;
    }

    if (MatchUnderWay())
    {
        SDL_Log("Lobby updates: RESTART waits for the match to end; the update installs as the game exits");
        return true;
    }

    const std::optional<uint64_t> build = MCUpdater::ExpectedBuild();
    const std::string commandLine = MCRejoinNote::LobbyCommandLine(LobbyLaunchArgs);

    if (!build.has_value())
    {
        SDL_Log("Lobby updates: the update's build isn't known: no rejoin note");
    }
    else if (const auto written = MCRejoinNote::Write(
                 MCFileSystem::UserRoot(), {.CommandLine = commandLine, .Build = *build, .Time = MCRejoinNote::Now()});
             !written)
    {
        SDL_Log("Lobby updates: no rejoin note: %s", written.error().c_str());
    }

    // The host tells the guests that wait for it; a guest, every player, so the room keeps its place.
    if (MultiPlayer()->IsHost)
    {
        for (const std::shared_ptr<MCFidpPlayer>& player : Others())
        {
            if (WaitsForHost(*player))
            {
                SendUpdating(player->Id);
            }
        }
    }
    else
    {
        SendUpdating(0);
    }

    SDL_Log("Lobby updates: restarting into the update, to start again as %s", commandLine.c_str());
    RestartAt = Clock::now() + SendTime;
    return true;
}

auto MCLobbyUpdate::Frame() -> void
{
    const Clock::time_point now = Clock::now();

    if (RestartAt.has_value() && now >= *RestartAt)
    {
        RestartAt.reset();
        MCUpdater::Answer(MCUpdater::MCAnswer::Install);
        // An update no longer ready (it failed meanwhile) leaves the room as it was.
        Restarting = MCUpdater::Status().Stage == MCUpdater::MCStage::Installing;
    }

    if (MultiPlayer() == nullptr || GlobalLog() == nullptr)
    {
        return;
    }

    if (HostWait.has_value() && now >= HostWait->NextTry)
    {
        TryHostAgain(now);
    }

    KeepPlaces(now);

    // A guest back from its update: the host's room shows it to everyone (and it to them) as the host's GO did.
    if (RefreshAt.has_value() && now >= *RefreshAt)
    {
        RefreshAt.reset();

        if (MultiPlayer()->IsHost && GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen &&
            MultiPlayer()->SessionManager->GetPlayers(nullptr).size() > 1)
        {
            GlobalLog()->SetUpSessionScreen();
        }
    }
}

auto MCLobbyUpdate::Updating(uint32_t fromId) -> void
{
    if (!InRoom() || GlobalLog() == nullptr || MatchUnderWay() || Restarting || RestartAt.has_value())
    {
        return;
    }

    MCMultiPlayer& game = *MultiPlayer();

    if (!game.IsHost && (fromId == game.ServerID || fromId == game.HostID))
    {
        WaitForHost(fromId);
        return;
    }

    const std::shared_ptr<MCFidpPlayer> player = game.SessionManager->GetPlayer(fromId);

    if (player == nullptr || std::ranges::contains(Coming, fromId, &MCComing::Id))
    {
        return;
    }

    SDL_Log("Lobby updates: %s restarts into an update; the room keeps its place", player->Name.c_str());
    Coming.push_back({.Id = fromId, .Name = player->Name, .Until = Clock::now() + RejoinWait});

    if (game.IsHost)
    {
        game.SessionManager->AdmitNewPlayers(true);
    }

    if (const std::shared_ptr<MCLogChatWindow> chat = GlobalLog()->ChatWindow; chat != nullptr)
    {
        chat->AddLine(UpdatingText(player->Name));
    }
}

auto MCLobbyUpdate::LeftToUpdate(uint32_t playerId) -> bool
{
    return Restarting || RestartAt.has_value() || std::ranges::contains(Coming, playerId, &MCComing::Id);
}

auto MCLobbyUpdate::PlayerJoined(uint32_t playerId) -> void
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const std::shared_ptr<MCFidpPlayer> player = game != nullptr ? game->SessionManager->GetPlayer(playerId) : nullptr;

    if (player == nullptr)
    {
        return;
    }

    const auto coming = std::ranges::find(Coming, player->Name, &MCComing::Name);

    if (coming == Coming.end())
    {
        return;
    }

    SDL_Log("Lobby updates: %s is back from its update", player->Name.c_str());
    Coming.erase(coming);

    if (game->IsHost)
    {
        if (Coming.empty())
        {
            game->SessionManager->AdmitNewPlayers(false);
        }

        RefreshAt = Clock::now() + SettleTime;
    }
}

auto MCLobbyUpdate::RejoinFailedText(MCRoomRefusal refusal) -> std::string
{
    switch (refusal)
    {
        case MCRoomRefusal::Full:
            return std::string(RoomFullText);
        case MCRoomRefusal::Started:
            return std::string(RoomStartedText);
        case MCRoomRefusal::Gone:
            break;
    }

    return std::string(RoomGoneText);
}

auto MCLobbyUpdate::JoinFailed() -> void
{
    ShowJoinFailed(LobbyLaunchArgs.Rejoin ? RejoinFailedText(MultiPlayer()->SessionManager->LastRoomRefusal)
                                          : LoadGameString(JoinFailedString, StringBuffer));
}

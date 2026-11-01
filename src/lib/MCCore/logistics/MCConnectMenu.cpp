#include "stdafx.h"
#include "logistics/MCConnectMenu.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSystem.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGameList.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogScrollTextObject.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "network/MCMPGameConfiguration.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "network/MCMultiPlayer.h"
#include "main/MCGameContext.h"
#include "lib/MCFatal.h"
#include "main/MCGameSession.h"

int32_t ReadyRoomTicks = 0;
bool WhackTimer = false;

namespace
{
    /// <summary>The fewest and the most players a hosted LAN session takes (a game rule).</summary>
    constexpr int32_t MinSessionPlayers = 2;
    constexpr int32_t MaxSessionPlayers = 6;

    /// <summary>DirectPlay's DPERR_CONNECTING: the modem is still dialling.</summary>
    constexpr int32_t DialStillConnecting = static_cast<int32_t>(0x8877015e);

    /// <summary>The listed session <paramref name="games"/> has selected, or null.</summary>
    std::shared_ptr<MCFidpSession> SelectedSession(MCGameList& games)
    {
        const std::optional<_GUID> game = games.GetSelectedGame();
        return game.has_value() ? MultiPlayer()->SessionManager->FindMatchingSession(*game) : nullptr;
    }

    /// <summary>The player's name for the multiplayer screens: the one remembered (or the user's), if any.</summary>
    std::optional<std::string> RememberedUserName()
    {
        return MyGetUserName();
    }

    /// <summary>The name for the player entry: the one remembered, else "Player".</summary>
    std::string PlayerNameOrDefault()
    {
        return RememberedUserName().value_or("Player");
    }

    /// <summary>The players a hosted LAN session takes: the number typed in the LAN screen's entry 11, within the rule.</summary>
    int32_t TypedMaxPlayers()
    {
        return std::clamp(ElementNumber(*GlobalLog()->LanScreen, 11), MinSessionPlayers, MaxSessionPlayers);
    }

    /// <summary>Opens the ready room (session screen) after a session was joined or created from <paramref name="from"/>.</summary>
    void EnterReadyRoom(MCLogObject& from, std::optional<bool> goDisabled)
    {
        from.ShowGuiWindow(false);
        GlobalLog()->ConnectScreen->ShowGuiWindow(true);
        GlobalLog()->SetCurrentScreen(GlobalLog()->ConnectScreen);
        GlobalLog()->LogisticsState = 0xe;

        if (goDisabled.has_value())
        {
            GlobalLog()->ConnectScreen->Element<MCLogButton>(2)->Disabled = *goDisabled;
        }
    }

    /// <summary>After a session was joined: how many are in it.</summary>
    void CountLanPlayers()
    {
        NumLanPlayers = static_cast<int32_t>(MultiPlayer()->SessionManager->GetPlayers(nullptr).size());
    }

    /// <summary>Shows the LAN session list (after connecting), the player entry focused.</summary>
    void OpenLanScreen()
    {
        GlobalLog()->MultiplayerScreen->ShowGuiWindow(false);
        GlobalLog()->LanScreen->ShowGuiWindow(true);
        GlobalLog()->LanScreen->ShowBlock(0);
        GlobalLog()->SetCurrentScreen(GlobalLog()->LanScreen);
        GlobalLog()->LogisticsState = 0xb;
        GuiSystem().SetText(GlobalLog()->LanScreen->Elements[4]);
    }

    /// <summary>
    /// The one-button message dialog with string <paramref name="stringId"/> whose button (showing cancel) runs
    /// <paramref name="cancelExec"/>, while <paramref name="screen"/> retries in a second (its timer
    /// <paramref name="timer"/>).
    /// </summary>
    void WaitWithCancel(MCGenericScreen& screen, int32_t timer, uint32_t stringId, std::function<void()> cancelExec,
                        bool enable)
    {
        GuiSystem().AddTimer(screen.weak_from_this(), timer, 1000, 0, 0, false);
        WhackTimer = false;
        MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
        dialog.SetText(LoadGameString(stringId, 0xfe));
        dialog.SetTwoButton(false);
        dialog.Callback = nullptr;
        dialog.OkButton->SetUpPicture("bh_cancl.tga");
        dialog.OkButton->SetDownPicture("bg_cancl.tga");

        if (enable)
        {
            dialog.OkButton->Disabled = false;
        }

        dialog.OkButton->Callback().SetExec(std::move(cancelExec));
        dialog.Activate();
    }
}

void ConnectScreen()
{
    EnsureRegistryVersion();
    MCGenericScreen& screen = *GlobalLog()->MultiplayerScreen;

    if (MultiPlayer() == nullptr)
    {
        // The multiplayer object is network/'s (P3-net gives it an owner).
        MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
        MultiPlayer()->Start();
    }

    GlobalLog()->MainScreen->ShowGuiWindow(false);
    GlobalLog()->MultiplayerScreen->ShowGuiWindow(true);
    GlobalLog()->SetCurrentScreen(GlobalLog()->MultiplayerScreen);
    GlobalLog()->LogisticsState = 10;

    // The connection buttons: modem, serial, LAN, internet; each is enabled when the machine has it.
    auto modemButton = screen.Element<MCLogButton>(2);
    auto serialButton = screen.Element<MCLogButton>(3);
    auto lanButton = screen.Element<MCLogButton>(4);
    auto internetButton = screen.Element<MCLogButton>(5);
    modemButton->Disabled = true;
    serialButton->Disabled = true;
    lanButton->Disabled = true;
    internetButton->Disabled = true;

    if (MultiPlayer()->SessionManager != nullptr)
    {
        MCSessionManager& manager = *MultiPlayer()->SessionManager;
        modemButton->Disabled = manager.IsModemAvailable() == 0;
        serialButton->Disabled = (manager.AvailableProtocols & 4) == 0;
        lanButton->Disabled = manager.IsIpxAvailable() == 0 && manager.IsTcpAvailable() == 0;
        internetButton->Disabled = (manager.AvailableProtocols & 0x10) == 0;
    }

    // Less than 32 MB: multiplayer may not run well.
    if (MCPort::TotalPhysicalMemory() < 32000000)
    {
        ShowMenuMessage(0x371);
    }
}

void CancelToConnect()
{
    WhackTimer = true;
    GlobalLog()->CurrentScreen()->ShowGuiWindow(false);
    GlobalLog()->MultiplayerScreen->ShowGuiWindow(true);
    GlobalLog()->SetCurrentScreen(GlobalLog()->MultiplayerScreen);
    GlobalLog()->LogisticsState = 10;
    GlobalLog()->LanScreen->ShowBlock(0);
    GlobalLog()->ModemScreen->ShowBlock(0);
    auto games = GlobalLog()->LanScreen->Element<MCGameList>(2);
    games->ClearSelection();
    games->Clear();
    GlobalLog()->LanScreen->Element<MCLogScrollTextObject>(3)->Clear();
    GlobalLog()->LanScreen->Element<MCLogButton>(6)->Disabled = true;
    // Original behaviour (OB-167): the same timer is removed twice (team 2's is left).
    GuiSystem().RemoveTimer(*GlobalLog()->SessionScreen->Team1RPText, 0);
    GuiSystem().RemoveTimer(*GlobalLog()->SessionScreen->Team1RPText, 0);
    MultiPlayer()->LeaveSession();
    GlobalLog()->PlayerLights.reset();
}

void CancelToMPlayer()
{
    KillTheGame();
}

void CancelToLan()
{
    GlobalLog()->CurrentScreen()->ShowGuiWindow(false);
    GlobalLog()->LanScreen->ShowGuiWindow(true);
    GlobalLog()->SetCurrentScreen(GlobalLog()->LanScreen);
    GlobalLog()->LogisticsState = 0xb;
    GlobalLog()->LanScreen->ShowBlock(0);
    GlobalLog()->LanScreen->Element<MCGameList>(2)->ClearSelection();
}

void CancelToSession()
{
    MCSplashScreen& loadScreen = *GlobalLog()->LoadScreen;
    loadScreen.CancelButton->Callback().SetExec(Cancel);
    loadScreen.LoadSaveButton->Callback().SetExec(LoadGame);
    loadScreen.FilePane->SetMultiplayer(false);
    GlobalLog()->CurrentScreen()->ShowGuiWindow(false);
    GlobalLog()->SetCurrentScreen(GlobalLog()->SessionScreen);
    GlobalLog()->LogisticsState = 8;
    GlobalLog()->ShowLogScreen(true, true);
}

void ShowModemScreen()
{
    MCSplashScreen& screen = *GlobalLog()->ModemScreen;

    if (MultiPlayer() != nullptr)
    {
        auto modems = screen.Element<MCLogScrollTextObject>(10);
        screen.Element<MCLogTextObject>(4)->SetStringBuffer(PlayerNameOrDefault());
        MultiPlayer()->SessionManager->FindModems();
        // The list is refilled with the modems found, keeping its selection.
        const int32_t selected = modems->HighlightLine[0];
        modems->Clear();
        modems->HighlightLine[0] = selected;

        for (int32_t i = 0;; i++)
        {
            const std::optional<std::string> modem = MultiPlayer()->SessionManager->GetModemName(i);

            if (!modem.has_value())
            {
                break;
            }

            modems->Print(*modem, 0x1f);
        }
    }

    GlobalLog()->MultiplayerScreen->ShowGuiWindow(false);
    GlobalLog()->SetCurrentScreen(screen);
    screen.ShowGuiWindow(true);
    GlobalLog()->LogisticsState = 0xc;
    screen.ShowBlock(0);
}

void ShowSerialScreen()
{
    MCGenericScreen& screen = *GlobalLog()->SerialScreen;
    screen.Element<MCLogTextObject>(4)->SetStringBuffer(PlayerNameOrDefault());
    GlobalLog()->MultiplayerScreen->ShowGuiWindow(false);
    GlobalLog()->SetCurrentScreen(screen);
    screen.ShowGuiWindow(true);
    GlobalLog()->LogisticsState = 0xd;
    GuiSystem().SetText(screen.Elements[4]);
}

void DoTheIpxThang()
{
    if (MultiPlayer() != nullptr && MultiPlayer()->SessionManager != nullptr)
    {
        MultiPlayer()->SessionManager->ConnectIpx();
    }

    OpenLanScreen();
}

void DoTheTcpThang()
{
    if (MultiPlayer() != nullptr && MultiPlayer()->SessionManager != nullptr)
    {
        MultiPlayer()->SessionManager->ConnectTcp(const_cast<char*>(""));
    }

    OpenLanScreen();
}

void TcpipxDialogCallback(int32_t result)
{
    if (result == 1)
    {
        DoTheIpxThang();
        return;
    }

    if (result == 2)
    {
        DoTheTcpThang();
    }
}

void ShowLanScreen()
{
    MCSplashScreen& screen = *GlobalLog()->LanScreen;
    auto gameEntry = screen.Element<MCLogTextObject>(10);

    if (const std::optional<std::string> name = RememberedUserName())
    {
        screen.Element<MCLogTextObject>(4)->SetStringBuffer(*name);
        gameEntry->InitBuffer(0x18, MCLogInputType::Text);
        gameEntry->SetStringBuffer(MCFormatPrintf(LoadGameString(0x377, 0xfe).c_str(), name->c_str()));
    }
    else
    {
        screen.Element<MCLogTextObject>(4)->SetStringBuffer("Player");
        gameEntry->SetStringBuffer("Game");
    }

    // Both protocols: ask which; else say which one is used.
    MCSessionManager& manager = *MultiPlayer()->SessionManager;

    if (manager.IsIpxAvailable() != 0 && manager.IsTcpAvailable() != 0)
    {
        MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
        dialog.SetText(LoadGameString(0xa6, 0xfe));
        dialog.SetTwoButton(true);
        dialog.Callback = TcpipxDialogCallback;
        dialog.OkButton->SetUpPicture("bh_ipx.tga");
        dialog.OkButton->SetDownPicture("bg_ipx.tga");
        dialog.OkButton->Disabled = false;
        dialog.OkButton->Result = 1;
        dialog.CancelButton->SetUpPicture("bh_tcp.tga");
        dialog.CancelButton->SetDownPicture("bg_tcp.tga");
        dialog.CancelButton->Disabled = false;
        dialog.CancelButton->Result = 2;
        dialog.Activate();
        return;
    }

    if (manager.IsIpxAvailable() != 0)
    {
        ShowMenuMessage(0x36f, [](int32_t) { DoTheIpxThang(); });
        return;
    }

    if (manager.IsTcpAvailable() != 0)
    {
        ShowMenuMessage(0x36e, [](int32_t) { DoTheTcpThang(); });
    }
}

void DoExitToZone1()
{
    // Port: the original started the Internet Gaming Zone's launcher (zonea502.exe, from the registry) or the web
    // browser on the Zone's MechCommander page; the Zone is gone, so nothing is started. It then quit, as here.
    KillTheGame();
}

void DoExitToZone()
{
    AskMenuQuestion(*GlobalLog()->QuestionDialog, 0x4e9, DoExitToZone1, nullptr, "bh_cancl.tga", true);
}

void DoExitToMplayer()
{
    // Port: the original started mplaynow.exe (the Mplayer.com lobby, long gone) and quit; the port takes the path
    // of a failed start: the error message.
    ShowMenuMessage(0x353);
}

void ShowInternet()
{
    AskMenuQuestion(*GlobalLog()->MessageDialog, 0x352, DoExitToMplayer, DoExitToZone, "bh_cancl.tga", true);
}

void HostGame()
{
    MCSplashScreen& screen = *GlobalLog()->LanScreen;
    const std::string playerName = ElementText(screen, 4);
    SaveUserName(playerName);
    screen.ShowBlock(1);
    GuiSystem().SetText(screen.Elements[11]);
    screen.Element<MCLogTextObject>(10)->SetStringBuffer(
        MCFormatPrintf(LoadGameString(0x377, 0xfe).c_str(), playerName.c_str()));
}

void ResetReadyRoom()
{
    MCGuiEvent event;
    event.Clear();
    event.Type = 0x13;
    event.Data = 0;
    ReadyRoomPlayerListHandleEvent(*GlobalLog()->ConnectScreen->Element<MCGuiObject>(3), event);
    event.Clear();
    event.Type = 0x1e;
    event.Data = 4;
    PlayerListHandleEvent(*GlobalLog()->LanScreen->Element<MCGuiObject>(3), event);
}

void JoinGame()
{
    if (MultiPlayer() == nullptr || MultiPlayer()->SessionManager == nullptr)
    {
        return;
    }

    MCSessionManager& manager = *MultiPlayer()->SessionManager;

    if (const std::optional<_GUID> game = GlobalLog()->LanScreen->Element<MCGameList>(2)->GetSelectedGame())
    {
        if (auto session = manager.FindMatchingSession(*game); session != nullptr)
        {
            std::string playerName = ElementText(*GlobalLog()->LanScreen, 4);
            SaveUserName(playerName);

            if (session->SessionDesc.dwCurrentPlayers < session->SessionDesc.dwMaxPlayers)
            {
                const int32_t result = manager.JoinSession(session->SessionDesc.guidInstance, playerName);
                CountLanPlayers();

                if (result == 0)
                {
                    EnterReadyRoom(*GlobalLog()->LanScreen, true);
                    ReadyRoomTicks = 0;
                    ResetReadyRoom();
                    return;
                }
            }
        }
    }

    // The game can't be joined.
    ShowMenuMessage(0xa7);
}

void CreateSession()
{
    if (MultiPlayer() != nullptr && MultiPlayer()->SessionManager != nullptr)
    {
        std::string sessionName = ElementText(*GlobalLog()->LanScreen, 10);
        const int32_t maxPlayers = TypedMaxPlayers();
        std::string playerName = ElementText(*GlobalLog()->LanScreen, 4);
        MultiPlayer()->CreateSession(sessionName.data(), playerName.data(), maxPlayers);
    }

    GlobalLog()->LanScreen->ShowGuiWindow(false);
    GlobalLog()->ConnectScreen->ShowGuiWindow(true);
    ReadyRoomTicks = 0;
    GlobalLog()->SetCurrentScreen(GlobalLog()->ConnectScreen);
    GlobalLog()->LogisticsState = 0xe;
    ResetReadyRoom();
}

void CreateSerialSession()
{
    if (MultiPlayer() == nullptr || MultiPlayer()->SessionManager == nullptr)
    {
        return;
    }

    const int32_t port = ElementNumber(*GlobalLog()->SerialScreen, 5);

    if (port < 1 || port > 4)
    {
        return;
    }

    MultiPlayer()->SessionManager->ConnectComPort(static_cast<uint32_t>(port), 0xe100, 0, 0, 4);
    std::string playerName = ElementText(*GlobalLog()->SerialScreen, 4);
    SaveUserName(playerName);

    if (MultiPlayer()->CreateSession(const_cast<char*>("SerialGame"), playerName.data(), 2) == 0)
    {
        EnterReadyRoom(*GlobalLog()->SerialScreen, std::nullopt);
    }
}

void SerialJoinButtonPressed()
{
    if (MultiPlayer() == nullptr)
    {
        return;
    }

    SaveUserName(ElementText(*GlobalLog()->SerialScreen, 4));
    WhackTimer = true;
    const int32_t port = ElementNumber(*GlobalLog()->SerialScreen, 5);

    if (port > 0 && port < 5 &&
        MultiPlayer()->SessionManager->ConnectComPort(static_cast<uint32_t>(port), 0xe100, 0, 0, 4) == 0)
    {
        JoinSerialSession();
    }
}

void JoinSerialSession()
{
    if (MultiPlayer() == nullptr)
    {
        return;
    }

    std::string playerName = ElementText(*GlobalLog()->SerialScreen, 4);

    if (MultiPlayer()->JoinSession("SerialGame", playerName) != 0)
    {
        // No game yet: try again in a second, with a way out.
        WaitWithCancel(*GlobalLog()->SerialScreen, 0, 0xb1, CancelToConnect, false);
        return;
    }

    CountLanPlayers();
    EnterReadyRoom(*GlobalLog()->SerialScreen, true);
    GlobalLog()->MessageDialog->Deactivate(0);
}

void JoinModemSession()
{
    std::string playerName = ElementText(*GlobalLog()->ModemScreen, 4);

    if (MultiPlayer()->JoinSession("MC Modem Game", playerName) != 0)
    {
        GuiSystem().AddTimer(GlobalLog()->ModemScreen, 1, 1000, 0, 0, false);
        WhackTimer = false;
        return;
    }

    CountLanPlayers();
    EnterReadyRoom(*GlobalLog()->ModemScreen, true);
    GlobalLog()->MessageDialog->Deactivate(0);
    WhackTimer = true;
}

int32_t DialModemSession()
{
    if (MultiPlayer() == nullptr)
    {
        return -1;
    }

    SaveUserName(ElementText(*GlobalLog()->ModemScreen, 4));
    const int32_t result = MultiPlayer()->SessionManager->Dial();

    if (result == 0)
    {
        JoinModemSession();
        return 0;
    }

    GuiSystem().AddTimer(GlobalLog()->ModemScreen, 0, 1000, 0, 0, false);
    WhackTimer = false;
    return result == DialStillConnecting ? 2 : 1;
}

void AllGoneCallback(int32_t)
{
    MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
    dialog.SetText(LoadGameString(0xbb, 0xfe));
    dialog.SetTwoButton(false);
    dialog.Callback = nullptr;
    dialog.OkButton->SetUpPicture("bh_okay.tga");
    dialog.OkButton->SetDownPicture("bg_okay.tga");
    dialog.OkButton->Callback().SetExec(nullptr);
    dialog.Activate();
}

void GOCallback()
{
    if (MultiPlayer() == nullptr)
    {
        return;
    }

    GlobalLog()->CurrentScreen()->ShowGuiWindow(false);
    GlobalLog()->SetCurrentScreen(GlobalLog()->SessionScreen);
    GlobalLog()->ShowLogScreen(true, true);
    GlobalLog()->LogisticsState = 8;
    GlobalLog()->SessionScreen->Activate(false);

    if (MultiPlayer() != nullptr)
    {
        MultiPlayer()->SessionManager->LockSession();
        return;
    }

    GlobalLog()->MessageDialog->Callback = AllGoneCallback;
}

void Go()
{
    if (MultiPlayer() == nullptr || MultiPlayer()->SessionManager == nullptr)
    {
        return;
    }

    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    const std::shared_ptr<MCFidpSession> session = manager.CurrentSession();

    if (session == nullptr)
    {
        return;
    }

    // A LAN session takes the number typed; a lobby (0x10) game six, a modem or serial game two.
    const MCNetProtocol connection = manager.CurrentConnection;
    int32_t maxPlayers;

    if (connection == MCNetProtocol::Ipx || connection == MCNetProtocol::TcpIp)
    {
        maxPlayers = TypedMaxPlayers();
    }
    else
    {
        maxPlayers = connection == MCNetProtocol::Lobby ? MaxSessionPlayers : 2;
    }

    const uint32_t players = session->SessionDesc.dwCurrentPlayers;
    Assert(static_cast<int32_t>(players) <= maxPlayers, players, " How'd we get too many players? ");
    GOCallback();
}

void Leave()
{
}

void WaitForCall()
{
    if (MultiPlayer() != nullptr && MultiPlayer()->SessionManager != nullptr)
    {
        auto modems = GlobalLog()->ModemScreen->Element<MCLogScrollTextObject>(10);

        if (std::optional<std::string> modem = modems->GetTextLine(modems->HighlightLine[0] + 1))
        {
            MultiPlayer()->SessionManager->ConnectModem(const_cast<char*>(""), modem->data());
            std::string playerName = ElementText(*GlobalLog()->ModemScreen, 4);
            SaveUserName(playerName);
            MultiPlayer()->CreateSession(const_cast<char*>("MC Modem Game"), playerName.data(), 2);
        }
    }

    GlobalLog()->LanScreen->Element<MCLogTextObject>(11)->InitBuffer(2, MCLogInputType::Digits);
    GlobalLog()->ModemScreen->ShowGuiWindow(false);
    GlobalLog()->ConnectScreen->Element<MCLogButton>(2)->Disabled = false;
    GlobalLog()->ConnectScreen->ShowGuiWindow(true);
    GlobalLog()->SetCurrentScreen(GlobalLog()->ConnectScreen);
    GlobalLog()->LogisticsState = 0xe;
    GuiSystem().SetText(GlobalLog()->ModemScreen->Elements[4]);
    GuiSystem().AddTimer(GlobalLog()->ModemScreen->Elements[4], 0, MCPort::CaretBlinkTime(), 0, 0, false);
}

void GetNumber()
{
    GuiSystem().SetText(GlobalLog()->ModemScreen->Elements[5]);
    GlobalLog()->ModemScreen->ShowBlock(1);
}

void CancelDial()
{
    WhackTimer = true;

    if (MultiPlayer() != nullptr)
    {
        MultiPlayer()->SessionManager->CancelDialing();
    }
}

void Dial()
{
    MCSplashScreen& screen = *GlobalLog()->ModemScreen;
    auto modems = screen.Element<MCLogScrollTextObject>(10);

    if (MultiPlayer() == nullptr || MultiPlayer()->SessionManager == nullptr)
    {
        return;
    }

    std::optional<std::string> modem = modems->GetTextLine(modems->HighlightLine[0] + 1);

    if (!modem.has_value())
    {
        return;
    }

    std::string number = ElementText(screen, 5);
    MultiPlayer()->SessionManager->ConnectModem(number.data(), modem->data());
    const int32_t result = DialModemSession();
    Assert(result != 1, 0, "Not currently connected to a modem");

    switch (result)
    {
        case 0:
        {
            EnterReadyRoom(screen, true);
            return;
        }
        case 1:
        case 3:
        {
            ShowMenuMessage(0xb3);
            return;
        }
        case 2:
        {
            // Still dialling: check again in a second; the button cancels.
            WaitWithCancel(screen, 0, 0xb2, CancelDial, true);
            return;
        }

        default:
            return;
    }
}

void ModemListHandleEvent(MCGuiObject& object, MCGuiEvent& event)
{
    if (event.Type != 1)
    {
        return;
    }

    // A click picks the modem on the line under the mouse.
    auto& list = static_cast<MCLogScrollTextObject&>(object);
    const int32_t lineHeight = Fonts[0][list.FontIndex]->Height();
    const int32_t line = (list.FirstPixel + event.Y - list.GlobalY()) / (lineHeight + 4);

    if (list.GetTextLine(line + 1).has_value())
    {
        list.HighlightLine[0] = line;
    }
}

void PlayerListHandleEvent(MCGuiObject& object, MCGuiEvent& event)
{
    if (event.Type != 0x1e)
    {
        return;
    }

    auto& list = static_cast<MCLogScrollTextObject&>(object);

    if (event.Data == 3)
    {
        // A game was picked: list its players.
        if (object.Parent.expired() || MultiPlayer() == nullptr || MultiPlayer()->SessionManager == nullptr)
        {
            return;
        }

        const auto games = std::static_pointer_cast<MCGenericScreen>(object.ParentObject())->Element<MCGameList>(2);
        list.Clear();

        if (games == nullptr || games->SelectedSession <= -1)
        {
            return;
        }

        if (const std::shared_ptr<MCFidpSession> session = SelectedSession(*games))
        {
            for (const auto& player : MultiPlayer()->SessionManager->GetPlayers(session))
            {
                list.Print(player->Name, 0x1f);
            }
        }
    }
    else if (event.Data == 4)
    {
        list.Clear();
    }
}

void ReadyRoomPlayerListHandleEvent(MCGuiObject& object, MCGuiEvent& event)
{
    if (event.Type != 0x13 || MultiPlayer() == nullptr)
    {
        return;
    }

    // The session manager, pinned through the game that owns it.
    const std::shared_ptr<MCMultiPlayer> multiPlayer = MultiPlayer();
    const std::shared_ptr<MCSessionManager> manager(multiPlayer, multiPlayer->SessionManager.get());

    if (manager == nullptr)
    {
        return;
    }

    auto& list = static_cast<MCLogScrollTextObject&>(object);
    const std::shared_ptr<MCFidpSession> session = manager->CurrentSession();
    ++ReadyRoomTicks;

    if (session == nullptr)
    {
        return;
    }

    // A command-line lobby launch (-host/-join) has no lobby measuring the pings, so it shows its own as a LAN game
    // does; only the original's DirectPlay lobby left them out.
    const bool showPing = LaunchedFromLobby == 0 || LobbyLaunchArgs.Active;

    if (manager->IsHost == 0 && showPing)
    {
        manager->SendPing();
    }

    list.Clear();
    const auto& players = manager->GetPlayers(session);

    // Each player, with the ping outside a DirectPlay lobby launch.
    for (const auto& player : players)
    {
        if (showPing)
        {
            list.Print(std::format("{} - {:04} ms", player->Name, player->LastLatency), 0x1f);
        }
        else
        {
            list.Print(player->Name, 0x1f);
        }
    }

    // The host can go once someone else is in.
    if (manager->IsHost != 0)
    {
        GlobalLog()->ConnectScreen->Element<MCLogButton>(2)->Disabled = players.size() < 2;

        // Port: a configured match (MPGameConfiguration) goes on as soon as every player it named has joined, straight
        // to the mech bay: no ready room, map screen or settings wait. A problem leaves the host here, with Go.
        if (GameConfiguration.Active)
        {
            MPPollConfiguredMatch();

            if (players.size() >= GameConfiguration.Roster.size() &&
                std::ranges::all_of(GameConfiguration.Roster,
                                    [&players](const MPGamePlayer& wanted)
                                    {
                                        return std::ranges::any_of(players, [&wanted](const auto& player)
                                                                   { return player->Name == wanted.Name; });
                                    }))
            {
                MPBeginConfiguredMatch();
            }
        }
    }
}

void LanScreenHandleEvent(MCGuiObject& object, MCGuiEvent& event)
{
    auto& screen = static_cast<MCGenericScreen&>(object);

    if (event.Type == 0x13)
    {
        screen.Element<MCGuiObject>(2)->HandleEvent(event);
        screen.Element<MCGuiObject>(3)->HandleEvent(event);
        return;
    }

    if (event.Type != 0x1e)
    {
        return;
    }

    auto joinButton = screen.Element<MCLogButton>(6);

    if (event.Data == 3)
    {
        // A game was picked: its players are listed, and it can be joined unless full.
        const std::shared_ptr<MCFidpSession> session = SelectedSession(*screen.Element<MCGameList>(2));
        screen.Element<MCGuiObject>(3)->HandleEvent(event);

        if (session != nullptr)
        {
            joinButton->Disabled = session->SessionDesc.dwMaxPlayers <= session->SessionDesc.dwCurrentPlayers;
        }

        return;
    }

    if (event.Data == 4)
    {
        screen.Element<MCGuiObject>(3)->HandleEvent(event);
        joinButton->Disabled = true;
    }
}

void ComPortTextHandleEvent(MCGuiObject& object, MCGuiEvent& event)
{
    if (event.Type != 10)
    {
        return;
    }

    // The port number typed is checked (1 to 4); a bad one is backspaced out. The screen hears whether it is good.
    auto& entry = static_cast<MCLogTextObject&>(object);
    bool valid = false;
    const std::string text(entry.Text());

    if (!text.empty() && event.LParam != 999)
    {
        const int32_t port = std::atoi(text.c_str());

        if (port < 5 && port != 0)
        {
            valid = true;
        }
        else
        {
            MCGuiEvent backspace;
            backspace.Type = 10;
            backspace.Key = 8;
            backspace.LParam = 999;
            object.HandleEvent(backspace);
        }
    }

    MCGuiEvent notify;
    notify.Type = 0x1e;
    notify.Data = 6;
    notify.LParam = valid ? 1 : 0;
    object.ParentObject()->HandleEvent(notify);
}

void SerialScreenHandleEvent(MCGuiObject& object, MCGuiEvent& event)
{
    if (event.Type == 0x13)
    {
        GuiSystem().RemoveTimer(object, 0);

        if (!WhackTimer)
        {
            JoinSerialSession();
        }

        return;
    }

    if (event.Type == 0x1e && event.Data == 6)
    {
        // The host and join buttons need a good port number.
        const bool disabled = event.LParam == 0;
        auto& screen = static_cast<MCGenericScreen&>(object);
        screen.Element<MCLogButton>(2)->Disabled = disabled;
        screen.Element<MCLogButton>(3)->Disabled = disabled;
    }
}

void ModemScreenHandleEvent(MCGuiObject& object, MCGuiEvent& event)
{
    if (event.Type != 0x13)
    {
        return;
    }

    if (event.Data == 0)
    {
        GuiSystem().RemoveTimer(object, 0);

        if (!WhackTimer)
        {
            DialModemSession();
        }
    }
    else if (event.Data == 1)
    {
        GuiSystem().RemoveTimer(object, 1);

        if (!WhackTimer)
        {
            JoinModemSession();
        }
    }
}

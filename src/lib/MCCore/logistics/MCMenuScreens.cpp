#include "stdafx.h"
#include "logistics/MCMenuScreens.h"
#include "gui/MCScreenChange.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGamesScreen.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCPrefsScreen.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCRoomScreen.h"
#include "logistics/MCSavesMenu.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCUpdateScreen.h"
#include "main/MCGameSession.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "network/MCMultiPlayer.h"
#include "network/MCRoomEnd.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlFlow.h"

namespace
{
    using MCScreenFlow::MCScreenId;
    using MCScreenFlow::MCScreenKind;

    /// <summary>The campaign screens' MAIN MENU sound (the original's).</summary>
    constexpr uint32_t MainMenuSample = 0x36;

    /// <summary>Whether the screen logistics shows now is <paramref name="screen"/>'s.</summary>
    template <typename T> bool Showing(const MCGuiHandle<T> MCLogistics::* screen)
    {
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();

        if (logistics == nullptr)
        {
            return false;
        }

        // Port-only (SPEC-menus.md's stage 3): not while a remade screen shows over it, hidden (ui/MCRmlFlow), though it
        // stays logistics' screen.
        const std::shared_ptr<MCLogObject> shown = logistics->CurrentScreen();
        return shown != nullptr && (shown->ShowWindow || !MCRmlFlow::Showing()) &&
               (logistics.get()->*screen).Is(*shown);
    }

    /// <summary>Whether the load screen shows in mode <paramref name="multiplayer"/>, <paramref name="solo"/>.</summary>
    bool ShowingLoad(bool multiplayer, bool solo)
    {
        if (!Showing(&MCLogistics::LoadScreen))
        {
            return false;
        }

        const MCSplashScreen& screen = *GlobalLog()->LoadScreen;
        const bool picksMission = screen.FilePane != nullptr && screen.FilePane->Multiplayer;
        return picksMission == multiplayer && (multiplayer || LoadingSolo == solo);
    }

    /// <summary>Hides the screen logistics shows (an old screen's Hide).</summary>
    void HideShown()
    {
        if (const std::shared_ptr<MCLogistics> logistics = GlobalLog(); logistics != nullptr)
        {
            if (const std::shared_ptr<MCLogObject> shown = logistics->CurrentScreen(); shown != nullptr)
            {
                shown->ShowGuiWindow(false);
            }
        }
    }

    /// <summary>The main menu shown, as CANCEL showed it from the screens over it (RETURN TO GAME's state kept).</summary>
    void ShowMainMenu()
    {
        WhackTimer = true;
        GlobalLog()->SetUpMainScreen(true);
        LoadingSolo = false;
    }

    /// <summary>A question with OK and CANCEL saying <paramref name="text"/>, whose OK runs <paramref name="yes"/>.</summary>
    void Ask(std::string_view text, std::function<void()> yes)
    {
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();

        if (logistics == nullptr || logistics->MessageDialog == nullptr)
        {
            return;
        }

        MCReusableDialog& dialog = *logistics->MessageDialog;
        dialog.SetText(text);
        dialog.SetTwoButton(true);
        dialog.Callback = nullptr;
        dialog.OkButton->SetUpPicture("bh_okay.tga");
        dialog.OkButton->SetDownPicture("bg_okay.tga");
        dialog.OkButton->Disabled = false;
        dialog.OkButton->Callback().SetExec(std::move(yes));
        dialog.CancelButton->SetUpPicture("bh_cancl.tga");
        dialog.CancelButton->SetDownPicture("bg_cancl.tga");
        dialog.CancelButton->Disabled = false;
        dialog.Activate();
    }

    /// <summary>An old screen's kind: its name, whether it shows, how it shows again, what leaving it does.</summary>
    MCScreenKind OldScreen(std::string name, std::function<bool()> shows, std::function<void()> show,
                           std::function<void()> leave = {}, bool givesWay = false)
    {
        return {.Name = std::move(name),
                .Show = std::move(show),
                .Hide = HideShown,
                .Leave = std::move(leave),
                .Shows = std::move(shows),
                .GivesWay = givesWay};
    }

    /// <summary>The old screen's way back <paramref name="work"/> as a change named <paramref name="name"/>.</summary>
    std::function<void()> Back(std::string name, std::function<void()> work)
    {
        return MCScreenFlow::Via(std::move(name), std::move(work));
    }

    /// <summary>The ids, registered once (<see cref="MCMenuScreens::Register"/>).</summary>
    struct MCIds
    {
        MCScreenId Main = MCScreenFlow::NoScreen;
        MCScreenId Multiplayer = MCScreenFlow::NoScreen;
        MCScreenId Lan = MCScreenFlow::NoScreen;
        MCScreenId Internet = MCScreenFlow::NoScreen;
        MCScreenId Room = MCScreenFlow::NoScreen;
        MCScreenId MissionPick = MCScreenFlow::NoScreen;
        MCScreenId Load = MCScreenFlow::NoScreen;
        MCScreenId Solo = MCScreenFlow::NoScreen;
        MCScreenId Save = MCScreenFlow::NoScreen;
        MCScreenId Preferences = MCScreenFlow::NoScreen;
        MCScreenId Update = MCScreenFlow::NoScreen;
        MCScreenId Campaign = MCScreenFlow::NoScreen;
        MCScreenId Connection = MCScreenFlow::NoScreen;
        MCScreenId ReadyRoom = MCScreenFlow::NoScreen;
    };

    MCIds Ids;
    bool Registered = false;
    /// <summary>The room readied at a network's word and a change to show it under way (<see cref="MCMenuScreens::OpenRoom"/>).</summary>
    bool RoomShowing = false;

    /// <summary>Whether a change to show the readied room is under way (running or waiting; stale once neither).</summary>
    bool RoomShowPending()
    {
        return RoomShowing && (MCScreenChange::Running() || MCScreenFlow::Waiting());
    }

    const MCIds& TheIds()
    {
        if (!Registered)
        {
            MCMenuScreens::Register();
        }

        return Ids;
    }
}

auto MCMenuScreens::Register() -> void
{
    Registered = true;
    Ids.Main = MCScreenFlow::Register(
        OldScreen("main", [] { return Showing(&MCLogistics::MainScreen); }, ShowMainMenu, AskQuit));
    Ids.Multiplayer = MCScreenFlow::Register(
        OldScreen("multiplayer", [] { return Showing(&MCLogistics::MultiplayerScreen); }, ConnectScreen));
    // Port-only (SPEC-menus.md's stage 3): LAN and INTERNET remade (logistics/MCGamesScreen).
    Ids.Lan = MCScreenFlow::Register(MCGamesKind(MCGamesWhere::Lan));
    Ids.Internet = MCScreenFlow::Register(MCGamesKind(MCGamesWhere::Internet));
    // Port-only (SPEC-menus.md's stage 4): the remade room (logistics/MCRoomScreen) shows in the 1998 room's place, which
    // stays logistics' screen, hidden.
    Ids.Room = MCScreenFlow::Register(OldScreen(
        "room",
        []
        {
            const std::shared_ptr<MCLogistics> logistics = GlobalLog();
            const std::shared_ptr<MCLogObject> shown = logistics != nullptr ? logistics->CurrentScreen() : nullptr;
            return (shown != nullptr && logistics->SessionScreen.Is(*shown) && RoomScreenShown()) ||
                   Showing(&MCLogistics::SessionScreen);
        },
        []
        {
            // Port-only (stage 4): back from a screen over the room (stage 6's UPDATE), the room as it stands: its
            // set-up would start it afresh.
            const std::shared_ptr<MCLogistics> logistics = GlobalLog();
            const std::shared_ptr<MCLogObject> shown = logistics->CurrentScreen();

            if (shown != nullptr && logistics->SessionScreen.Is(*shown))
            {
                logistics->SessionScreen->ShowGuiWindow(true);
                return;
            }

            logistics->SetUpSessionScreen();
        },
        []
        {
            // A launcher's game: the room is where it starts, and leaving it ends the game.
            if (LaunchedFromLobby)
            {
                AskLeaveGame();
                return;
            }

            LeaveRoom();
        }));
    Ids.MissionPick = MCScreenFlow::Register(OldScreen(
        "mission pick", [] { return ShowingLoad(true, false); }, [] { GlobalLog()->SetUpSessionScreen(); },
        Back("mission pick back", CancelToSession), true));
    // Stage 5: LOAD GAME and SAVE GAME remade (logistics/MCSavesMenu); SOLO MISSIONS on the old file screen until 7.
    Ids.Load = MCScreenFlow::Register(MCSavesMenu::Kind(MCSavesScreen::MCMode::Load));
    Ids.Solo = MCScreenFlow::Register(
        OldScreen("solo missions", [] { return ShowingLoad(false, true); }, SoloLoadScreen, {}, true));
    Ids.Save = MCScreenFlow::Register(MCSavesMenu::Kind(MCSavesScreen::MCMode::Save));
    // Port-only (SPEC-menus.md, stage 6): Preferences remade (logistics/MCPrefsScreen.h), made afresh at each press so it
    // reads the settings as they are then; ACCEPT shows the screen below, CANCEL and Escape go back (its Leave puts the
    // settings back).
    Ids.Preferences = MCScreenFlow::Register(MCRmlFlow::Kind(
        "preferences",
        [](MCRmlLayer& layer)
        {
            return MCGuiHandle<MCRmlScreen>(layer.CreateChild<MCPrefsScreen>(
                [](MCPrefsScreen& made)
                {
                    made.Accepted = [] { MCScreenFlow::ShowBelow(); };
                    made.Back = [] { MCScreenFlow::Back(); };
                }));
        },
        MCRmlFlow::MCKeep::Afresh));
    // Port-only (stage 6): UPDATE remade (logistics/MCUpdateScreen.h), from the main menu's UPDATE and INTERNET's or
    // LAN's newer game, made afresh at each press, named by where it opened; BACK shows that screen again.
    Ids.Update = MCScreenFlow::Register(MCRmlFlow::Kind(
        "update",
        [](MCRmlLayer& layer)
        {
            return MCGuiHandle<MCRmlScreen>(layer.CreateChild<MCUpdateScreen>(
                [](MCUpdateScreen& made) { made.Back = [] { MCScreenFlow::Back(); }; },
                MCUpdateScreen::PlaceOfShown()));
        },
        MCRmlFlow::MCKeep::Afresh));
    MCScreenKind campaign = OldScreen(
        "campaign",
        []
        {
            return Showing(&MCLogistics::BriefingScreen) || Showing(&MCLogistics::PurchaseScreen) ||
                   Showing(&MCLogistics::RepairScreen) || MCCampaignMenu::Shown();
        },
        ReturnToGame, LeaveCampaign);
    // Stage 8: BRIEFING and DEPLOY remade (logistics/MCCampaignMenu) over the 1998 briefing: hidden with it, and the
    // change waits for their first whole frame.
    campaign.Hide = []
    {
        MCCampaignMenu::Hide();
        HideShown();
    };

    campaign.Ready = [] { return MCCampaignMenu::Ready(); };
    Ids.Campaign = MCScreenFlow::Register(std::move(campaign));
    Ids.Connection = MCScreenFlow::Register(OldScreen(
        "connection", [] { return Showing(&MCLogistics::ModemScreen) || Showing(&MCLogistics::SerialScreen); },
        ConnectScreen, Back("connection back", CancelToConnect)));
    Ids.ReadyRoom = MCScreenFlow::Register(OldScreen(
        "ready room", [] { return LauncherScreenShown() || Showing(&MCLogistics::ConnectScreen); }, [] {},
        [] { KillTheGame(); }));
}

auto MCMenuScreens::Main() -> MCScreenId
{
    return TheIds().Main;
}

auto MCMenuScreens::Multiplayer() -> MCScreenId
{
    return TheIds().Multiplayer;
}

auto MCMenuScreens::Lan() -> MCScreenId
{
    return TheIds().Lan;
}

auto MCMenuScreens::Internet() -> MCScreenId
{
    return TheIds().Internet;
}

auto MCMenuScreens::Room() -> MCScreenId
{
    return TheIds().Room;
}

auto MCMenuScreens::MissionPick() -> MCScreenId
{
    return TheIds().MissionPick;
}

auto MCMenuScreens::Load() -> MCScreenId
{
    return TheIds().Load;
}

auto MCMenuScreens::Solo() -> MCScreenId
{
    return TheIds().Solo;
}

auto MCMenuScreens::Save() -> MCScreenId
{
    return TheIds().Save;
}

auto MCMenuScreens::Preferences() -> MCScreenId
{
    return TheIds().Preferences;
}

auto MCMenuScreens::Update() -> MCScreenId
{
    return TheIds().Update;
}

auto MCMenuScreens::Campaign() -> MCScreenId
{
    return TheIds().Campaign;
}

auto MCMenuScreens::Connection() -> MCScreenId
{
    return TheIds().Connection;
}

auto MCMenuScreens::ReadyRoom() -> MCScreenId
{
    return TheIds().ReadyRoom;
}

auto MCMenuScreens::GamesStack(bool internet, bool room) -> std::vector<MCScreenId>
{
    std::vector<MCScreenId> stack = {Main(), Multiplayer(), internet ? Internet() : Lan()};

    if (room)
    {
        stack.push_back(Room());
    }

    return stack;
}

auto MCMenuScreens::RoomStack() -> std::vector<MCScreenId>
{
    if (LaunchedFromLobby)
    {
        return {Room()};
    }

    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const bool internet =
        game != nullptr && game->SessionManager != nullptr && game->SessionManager->Internet != nullptr;
    return GamesStack(internet, true);
}

auto MCMenuScreens::AskQuit() -> void
{
    Ask(QuitQuestion, GameOverMan);
}

auto MCMenuScreens::AskLeaveGame() -> void
{
    // Port-only (SPEC-menus.md's stage 4): over the remade room, its name above "Leave the game?", CANCEL and LEAVE.
    constexpr std::string_view LeaveWord = "Leave";
    constexpr std::string_view CancelWord = "Cancel";
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();

    if (RoomScreenShown() && logistics != nullptr &&
        MCRmlDialogs::Show(MCRmlDialog::MCSay{.Eyebrow = logistics->Room.Name(),
                                              .Heading = std::string(LeaveGameQuestion),
                                              .Main = std::string(LeaveWord),
                                              .Other = std::string(CancelWord),
                                              .OnMain = [] { KillTheGame(); }}))
    {
        return;
    }

    Ask(LeaveGameQuestion, [] { KillTheGame(); });
}

auto MCMenuScreens::LeaveCampaign() -> void
{
    if (const std::shared_ptr<MCMultiPlayer> game = MultiPlayer(); game != nullptr)
    {
        // Port-only (SPEC-menus.md's stage 8): over the remade briefing, a remade dialog opening on CANCEL, the game's
        // name above it: the original's question (dialog_exit_mp), its EXIT landing on the games list the match came
        // from; a launcher's game, "Leave the game?", which ends it (the room's way back's end).
        if (MCCampaignMenu::Shown())
        {
            const bool launcher = LaunchedFromLobby != 0;
            MCRmlDialog::MCSay say{
                .Eyebrow = game->SessionName,
                .Heading = launcher
                               ? std::string(LeaveGameQuestion)
                               : MCRmlDialogs::SentenceCase(LoadGameString(ExitMultiplayerString, GameStringBuffer)),
                .Main = std::string(launcher ? LeaveWord : ExitWord),
                .Other = std::string(CancelWord),
                .OnMain = launcher ? std::function<void()>([] { KillTheGame(); }) : std::function<void()>(ExitToGames),
                .Leaves = true};

            if (MCRmlDialogs::Show(std::move(say)))
            {
                return;
            }
        }

        CheckExit();
        return;
    }

    // The original's at the press: the briefing's movie stopped, the sound.
    if (const std::shared_ptr<MCBriefingScreen> briefing = GlobalLog()->BriefingScreen.Lock(); briefing != nullptr)
    {
        briefing->StopSmackerMovies();
    }

    PlayLogSound(MainMenuSample);
    MCScreenFlow::Change("campaign back", [] { GlobalLog()->SetUpMainScreen(false); });
}

auto MCMenuScreens::ExitToGames() -> void
{
    const MCRoomEnd::MCGames games = MCRoomEnd::GamesOf();

    // A modem or serial game: the original's way, to the main menu.
    if (games == MCRoomEnd::MCGames::None)
    {
        DoExit();
        return;
    }

    LeaveMatch();
    MCRoomEnd::ShowGames(games, {});
}

auto MCMenuScreens::PrepareRoom() -> void
{
    GlobalLog()->ReadySessionScreen();
    RoomShowing = true;
}

auto MCMenuScreens::ShowPreparedRoom() -> void
{
    RoomShowing = false;
    GlobalLog()->ShowSessionScreen();
}

auto MCMenuScreens::OpenRoom(std::string name) -> void
{
    MCLogistics& logistics = *GlobalLog();

    // Shown already (the host's word to show it again), or dark already (a change's own work): set up at once.
    if (logistics.CurrentScreen() == logistics.SessionScreen || MCScreenChange::InPiece())
    {
        RoomShowing = false;
        logistics.SetUpSessionScreen();
        MCScreenFlow::Noted();
        return;
    }

    const bool pending = RoomShowPending();
    const std::shared_ptr<MCLogObject> before = logistics.CurrentScreen();
    PrepareRoom();

    // Its set-up gave the room up (too few players: the main menu, as the original's) or a change shows it already.
    if (logistics.CurrentScreen() != before || pending)
    {
        RoomShowing = pending;
        return;
    }

    MCScreenFlow::Change(std::move(name), ShowPreparedRoom);
}

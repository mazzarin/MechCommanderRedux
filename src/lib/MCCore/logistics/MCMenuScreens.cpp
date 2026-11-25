#include "stdafx.h"
#include "logistics/MCMenuScreens.h"
#include "gui/MCScreenChange.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCGamesScreen.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMainMenuScreen.h"
#include "logistics/MCPrefsScreen.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCRoomScreen.h"
#include "logistics/MCSavesMenu.h"
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
#include "main/MCLogisticsShared.h"

namespace
{
    using MCScreenFlow::MCScreenId;
    using MCScreenFlow::MCScreenKind;

    /// <summary>The campaign screens' MAIN MENU sound (the original's).</summary>
    constexpr uint32_t MainMenuSample = 0x36;

    /// <summary>
    /// Whether logistics is on <paramref name="id"/> with no remade screen showing yet: a remade screen waits for its
    /// first whole frame, so a change ends before it shows (1998's screen, hidden, was logistics' until stage 9).
    /// </summary>
    bool OnLogistics(MCScreenId id)
    {
        return MCMenuScreens::Top() == id && !MCRmlFlow::Showing();
    }

    /// <summary>The main menu shown, as CANCEL showed it from the screens over it.</summary>
    void ShowMainMenu()
    {
        GlobalLog()->SetUpMainScreen();
    }

    /// <summary>A question with OK and CANCEL saying <paramref name="text"/>, whose OK runs <paramref name="yes"/>.</summary>
    void Ask(std::string_view text, std::function<void()> yes)
    {
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();

        if (logistics == nullptr || logistics->Message == nullptr)
        {
            return;
        }

        MCMenuDialog& dialog = *logistics->Message;
        dialog.SetText(text);
        dialog.SetTwoButton(true);
        dialog.Callback = nullptr;
        dialog.Ok.SetPictures("bh_okay.tga", "bg_okay.tga");
        dialog.Ok.Disabled = false;
        dialog.Ok.Exec = std::move(yes);
        dialog.Cancel.SetPictures("bh_cancl.tga", "bg_cancl.tga");
        dialog.Cancel.Disabled = false;
        dialog.Activate();
    }

    /// <summary>
    /// A logistics screen's kind: its name, whether it shows, how it shows again, how it hides, what leaving it does.
    /// </summary>
    MCScreenKind LogisticsScreen(std::string name, std::function<bool()> shows, std::function<void()> show,
                                 std::function<void()> hide, std::function<void()> leave)
    {
        return {.Name = std::move(name),
                .Show = std::move(show),
                .Hide = std::move(hide),
                .Leave = std::move(leave),
                .Shows = std::move(shows)};
    }

    /// <summary>The ids, registered once (<see cref="MCMenuScreens::Register"/>).</summary>
    struct MCIds
    {
        MCScreenId Main = MCScreenFlow::NoScreen;
        MCScreenId Campaigns = MCScreenFlow::NoScreen;
        MCScreenId Multiplayer = MCScreenFlow::NoScreen;
        MCScreenId Lan = MCScreenFlow::NoScreen;
        MCScreenId Internet = MCScreenFlow::NoScreen;
        MCScreenId Room = MCScreenFlow::NoScreen;
        MCScreenId Load = MCScreenFlow::NoScreen;
        MCScreenId Solo = MCScreenFlow::NoScreen;
        MCScreenId Save = MCScreenFlow::NoScreen;
        MCScreenId Preferences = MCScreenFlow::NoScreen;
        MCScreenId Update = MCScreenFlow::NoScreen;
        MCScreenId Campaign = MCScreenFlow::NoScreen;
        MCScreenId ReadyRoom = MCScreenFlow::NoScreen;
    };

    MCIds Ids;
    bool Registered = false;
    /// <summary>The flow's top screen and the campaign's page on it (<see cref="MCMenuScreens::Top"/>).</summary>
    MCScreenId TopId = MCScreenFlow::NoScreen;
    MCMenuScreens::MCCampaignPage TopOn = MCMenuScreens::MCCampaignPage::Briefing;
    /// <summary>The main menu's logistics state (LogisticsState).</summary>
    constexpr int32_t MainMenuState = 1;
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
    // Port-only (SPEC-menus.md, stage 7): the main menu remade (logistics/MCMainMenuScreen.h), read live from the press,
    // logistics' screen (MCLogistics::ShowLogScreen): shown as 1998's CANCEL showed the main menu, the multiplayer game
    // let go.
    MCScreenKind main = MCMainMenuScreens::Kind(MCMainPanel::Main);
    main.Show = ShowMainMenu;
    main.Hide = [] { MCMainMenuScreens::HideAll(); };
    main.Leave = AskQuit;
    main.Shows = [] { return MCMainMenuScreens::Shown(MCMainPanel::Main) || OnLogistics(Main()); };
    Ids.Main = MCScreenFlow::Register(std::move(main));
    // Its submenus, swapped in its place from it with no fade; reached from another screen (INTERNET's BACK, a
    // landing), they fade as any change. MULTIPLAYER is 1998's MULTIPLAYER ACCESS, whose mpscreen.fit, modem and
    // serial screens aren't made.
    Ids.Campaigns = MCScreenFlow::Register(MCMainMenuScreens::Kind(MCMainPanel::Campaigns));
    Ids.Multiplayer = MCScreenFlow::Register(MCMainMenuScreens::Kind(MCMainPanel::Multiplayer));
    // Port-only (SPEC-menus.md's stage 3): LAN and INTERNET remade (logistics/MCGamesScreen).
    Ids.Lan = MCScreenFlow::Register(MCGamesKind(MCGamesWhere::Lan));
    Ids.Internet = MCScreenFlow::Register(MCGamesKind(MCGamesWhere::Internet));
    // Port-only (SPEC-menus.md's stage 4): the remade room (logistics/MCRoomScreen), logistics' screen.
    Ids.Room = MCScreenFlow::Register(LogisticsScreen(
        "room", [] { return (Top() == Room() && RoomScreenShown()) || OnLogistics(Room()); },
        []
        {
            // Port-only (stage 4): back from a screen over the room (stage 6's UPDATE), the room as it stands: its
            // set-up would start it afresh.
            if (Top() == Room())
            {
                ShowRoomScreen();
                return;
            }

            GlobalLog()->SetUpSessionScreen();
        },
        HideRoomScreen,
        []
        {
            // A launcher's game: the room is where it starts, and leaving it ends the game.
            if (LaunchedFromLobby)
            {
                AskLeaveGame();
                return;
            }

            MCRoomEnd::Leave();
        }));
    // Stage 5: LOAD GAME and SAVE GAME remade (logistics/MCSavesMenu); stage 7: SOLO MISSIONS, the main menu's submenu.
    Ids.Load = MCScreenFlow::Register(MCSavesMenu::Kind(MCSavesScreen::MCMode::Load));
    Ids.Solo = MCScreenFlow::Register(MCMainMenuScreens::Kind(MCMainPanel::Solo));
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
    // LAN's newer game, made afresh at each press; BACK shows the screen it opened from again.
    Ids.Update = MCScreenFlow::Register(MCRmlFlow::Kind(
        "update",
        [](MCRmlLayer& layer)
        {
            return MCGuiHandle<MCRmlScreen>(layer.CreateChild<MCUpdateScreen>(
                [](MCUpdateScreen& made) { made.Back = [] { MCScreenFlow::Back(); }; }));
        },
        MCRmlFlow::MCKeep::Afresh));
    // Stage 8: the campaign's screens remade (logistics/MCCampaignMenu); the change waits for their first whole frame.
    MCScreenKind campaign = LogisticsScreen(
        "campaign", [] { return MCCampaignMenu::Shown() || OnLogistics(Campaign()); }, ReturnToGame,
        [] { MCCampaignMenu::Hide(); }, LeaveCampaign);
    campaign.Ready = [] { return MCCampaignMenu::Ready(); };
    campaign.Prepare = [] { return MCCampaignMenu::Ahead(); };
    Ids.Campaign = MCScreenFlow::Register(std::move(campaign));
    // Port-only (stage 4): a launcher's join, on the painting (logistics/MCRoomScreen); the room opening hides it.
    Ids.ReadyRoom = MCScreenFlow::Register(LogisticsScreen(
        "ready room", [] { return LauncherScreenShown() || OnLogistics(ReadyRoom()); }, [] {}, {},
        [] { KillTheGame(); }));
}

auto MCMenuScreens::Main() -> MCScreenId
{
    return TheIds().Main;
}

auto MCMenuScreens::Campaigns() -> MCScreenId
{
    return TheIds().Campaigns;
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

auto MCMenuScreens::ReadyRoom() -> MCScreenId
{
    return TheIds().ReadyRoom;
}

auto MCMenuScreens::Top() -> MCScreenId
{
    return GlobalLog() != nullptr ? TopId : MCScreenFlow::NoScreen;
}

auto MCMenuScreens::TopPage() -> MCCampaignPage
{
    return TopOn;
}

auto MCMenuScreens::CampaignOn(MCCampaignPage page) -> bool
{
    return Top() == Campaign() && TopOn == page;
}

auto MCMenuScreens::NoteTop(MCScreenId id, MCCampaignPage page) -> void
{
    TopId = id;
    TopOn = page;
}

auto MCMenuScreens::ShowBack(MCScreenId id, MCBackWay way) -> void
{
    MCLogistics& logistics = *GlobalLog();

    if (id == Solo())
    {
        // Port-only (stage 7): the remade SOLO MISSIONS, in the main menu's panel.
        MCMainMenuScreens::ShowNow(MCMainPanel::Solo);
        return;
    }

    if (id == Campaign())
    {
        logistics.SetUpBriefingScreen();
        logistics.ShowLogScreen(true, false);
        return;
    }

    logistics.NoteTop(Main());
    logistics.LogisticsState = MainMenuState;
    const bool show = way != MCBackWay::Hidden;
    logistics.ShowLogScreen(show, show);
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
    if (MCMainMenuScreens::AskQuit())
    {
        return;
    }

    Ask(QuitQuestion, GameOverMan);
}

auto MCMenuScreens::AskLeaveGame() -> void
{
    // Port-only (SPEC-menus.md's stage 4): over the remade room, its name above "Leave the game?", CANCEL and LEAVE.
    constexpr std::string_view LeaveWord = "Leave";
    constexpr std::string_view CancelWord = "Cancel";
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();

    if (RoomScreenShown() && logistics != nullptr)
    {
        MCRmlDialogs::Show(MCRmlDialog::MCSay{.Eyebrow = logistics->Room.Name(),
                                              .Heading = std::string(LeaveGameQuestion),
                                              .Main = std::string(LeaveWord),
                                              .Other = std::string(CancelWord),
                                              .OnMain = [] { MCRoomEnd::LeaveLauncherGame(); }});
        return;
    }

    Ask(LeaveGameQuestion, [] { MCRoomEnd::LeaveLauncherGame(); });
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

            MCRmlDialogs::Show(std::move(say));
            return;
        }

        CheckExit();
        return;
    }

    // The original's at the press: the sound (the operation's video goes as the briefing hides).
    PlayLogSound(MainMenuSample);
    MCScreenFlow::Change("campaign back", [] { GlobalLog()->SetUpMainScreen(); });
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
    if (Top() == Room() || MCScreenChange::InPiece())
    {
        RoomShowing = false;
        logistics.SetUpSessionScreen();
        MCScreenFlow::Noted();
        return;
    }

    const bool pending = RoomShowPending();
    const MCScreenId before = Top();
    const MCCampaignPage beforePage = TopPage();
    PrepareRoom();

    // Its set-up gave the room up (too few players: the main menu, as the original's) or a change shows it already.
    if (Top() != before || TopPage() != beforePage || pending)
    {
        RoomShowing = pending;
        return;
    }

    MCScreenFlow::Change(std::move(name), ShowPreparedRoom);
}

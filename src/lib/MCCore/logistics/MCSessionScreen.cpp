#include "stdafx.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCSessionScreen.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "lib/MCFatal.h"
#include "linkup/MCFidpGroup.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogChatWindow.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogScrollTextObject.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCLogToolButton.h"
#include "logistics/MCPlayerNameObject.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCTicker.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCRoomScreen.h"
#include "logistics/MCUpdatePage.h"
#include "main/MCUpdater.h"
#include "main/MCGamePaths.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCMainMenu.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "main/MCGameStrings.h"
#include "network/MCLobbyUpdate.h"
#include "network/MCMultiPlayer.h"
#include "network/MCRoomEnd.h"
#include "sound/MCSoundSystem.h"
#include "vfx/MCVfxFunctions.h"
#include "logistics/MCBriefingScreen.h"

namespace
{
    /// <summary>The top of the unassigned list and the height of its rows.</summary>
    constexpr int32_t UnassignedTop = 0x162;
    constexpr int32_t UnassignedRow = 0x14;
    /// <summary>The sample a tab plays as it comes in front (the briefing's), and the chat tab's blink's half period.</summary>
    constexpr uint32_t TabSound = 0x36;
    constexpr uint32_t ChatBlinkMs = 500;
    /// <summary>The screen's art (its palette is the screen's).</summary>
    constexpr std::string_view BackgroundArt = "ses_bk00.tga";
    /// <summary>
    /// Port-only: the update's line, left of the start button: its top and height, from the left edge of team 1's box
    /// to a gap before the start button's recess.
    /// </summary>
    constexpr int32_t UpdateLineLeft = 0xed;
    constexpr int32_t UpdateLineTop = 0x1b4;
    constexpr int32_t UpdateLineWidth = 0xd8;
    constexpr int32_t UpdateLineHeight = 0x13;
    /// <summary>
    /// Port-only (the personal build): the line saying whom the start waits for, in the update's line's place: its
    /// field and outline, the palette's grey ramp's black and middle grey.
    /// </summary>
    constexpr uint8_t WaitsLineField = 0x10;
    constexpr uint8_t WaitsLineOutline = 0x18;
    /// <summary>The name slots' places: the unassigned list's left, and each team's slots' left, top and spacing.</summary>
    constexpr int32_t UnassignedLeft = 0xb;
    constexpr int32_t Team1NameLeft = 0xf8;
    constexpr int32_t Team2NameLeft = 0x1ba;
    constexpr int32_t TeamNameTop = 0x133;
    constexpr int32_t TeamNameRow = 0x16;
    /// <summary>The description's colour (the palette's).</summary>
    constexpr int32_t DescriptionColour = 0x1f;
    /// <summary>The text boxes' timer (OB-167's, removed as the room is left).</summary>
    constexpr int16_t BoxTimer = 0;

    MCSessionScreen& Screen()
    {
        return *GlobalLog()->SessionScreen;
    }

    /// <summary>The load-mission button: opens the load screen for a multiplayer mission.</summary>
    void OpenMissionLoad()
    {
        MCSplashScreen& loadScreen = *GlobalLog()->LoadScreen;
        loadScreen.LoadSaveButton->Callback().SetExec(LoadMPGame);
        loadScreen.CancelButton->Callback().SetExec([] { MCScreenFlow::Back(); });
        loadScreen.FilePane->SetMultiplayer(true);
        GlobalLog()->CurrentScreen()->ShowGuiWindow(false);
        GlobalLog()->SetCurrentScreen(loadScreen);
        GlobalLog()->LogisticsState = 5;
        GlobalLog()->ShowLogScreen(true, false);
    }

    /// <summary>A tech base toggle: sets its team's tech base and tells the others.</summary>
    void TechTabRoutine(MCGuiObject& object, MCGuiEvent& event)
    {
        if (event.Type != 1)
        {
            return;
        }

        auto& button = static_cast<MCLogToolButton&>(object);
        MCSessionScreen::Room().PickTechBase(static_cast<int8_t>(button.Group), static_cast<int8_t>(button.Value));
    }

    /// <summary>A team's resource points up 1000.</summary>
    void IncrementTeam1RP()
    {
        MCSessionScreen::Room().SetTeam1RP(MCSessionScreen::Room().Team1Points + 1000);
    }

    void IncrementTeam2RP()
    {
        MCSessionScreen::Room().SetTeam2RP(MCSessionScreen::Room().Team2Points + 1000);
    }

    /// <summary>A team's resource points down 1000 (not below 0).</summary>
    void DecrementTeam1RP()
    {
        MCSessionScreen::Room().SetTeam1RP(std::max(MCSessionScreen::Room().Team1Points - 1000, 0));
    }

    void DecrementTeam2RP()
    {
        MCSessionScreen::Room().SetTeam2RP(std::max(MCSessionScreen::Room().Team2Points - 1000, 0));
    }

    /// <summary>The screen's paint routine: the unassigned list's background and a bar per unassigned player.</summary>
    void PaintUnassigned(MCGuiObject& object)
    {
        auto& screen = static_cast<MCLogObject&>(object);
        screen.FillBox(0x1f, 0x162, 0xc6, 0x1d3, 0x10);
        int16_t top = 0x163;

        for (int32_t row = 0; row < MCSessionScreen::Room().NumUnassigned; ++row)
        {
            screen.FillBox(0x1f, top, 0xc6, static_cast<int16_t>(top + 0xd), 0x12);
            top += UnassignedRow;
        }
    }
}

auto MCSessionScreen::Room() -> MCRoomModel&
{
    return GlobalLog()->Room;
}

auto MCSessionScreen::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view) -> int32_t
{
    int32_t result = MCLogObject::Init(xPos, yPos, width, height);
    SetPaintRoutine(PaintUnassigned);
    Assert(result == 0, static_cast<uint32_t>(result), " Error initing session screen ");

    // The screen tabs.
    auto makeTab = [this](int32_t tabY, std::string_view art)
    {
        return CreateChild<MCLogToolButton>(
            [&](MCLogToolButton& tab)
            {
                const int32_t tabResult = tab.Init(2, tabY, 0xcf, 0x12, {});
                Assert(tabResult == 0, static_cast<uint32_t>(tabResult),
                       " Error initing session button on session screen ");
                tab.SetUpPicture(std::format("bn_{}.tga", art));
                tab.SetDownPicture(std::format("bg_{}.tga", art));
                tab.SetOverPicture(std::format("bh_{}.tga", art));
                tab.SetEventRoutine(LScreenSwitchEventHandler);
            });
    };

    SessionButton = makeTab(0x10, "session");
    SessionButton->Toggled = true;
    ExitButton = makeTab(0x22, "exit");
    ExitButton->Callback().SetExec(LaunchedFromLobby == 0 ? Cancel : MCRoomEnd::LeaveForLauncher);

    // The host's load and start buttons.
    auto makeButton = [this](int32_t buttonX, int32_t buttonY, std::string_view art, std::function<void()> exec)
    {
        return CreateChild<MCLogButton>(
            [&](MCLogButton& button)
            {
                const int32_t buttonResult = button.Init(buttonX, buttonY, 0x5e, 0x13, {});
                Assert(buttonResult == 0, static_cast<uint32_t>(buttonResult),
                       " Error initing load button on session screen ");
                button.SetUpPicture(std::format("ses_bh_{}.tga", art));
                button.SetOverPicture(std::format("ses_bh_{}.tga", art));
                button.SetDownPicture(std::format("ses_bg_{}.tga", art));
                button.Callback().SetExec(std::move(exec));
                button.SetGrayPicture(std::format("ses_bn_{}.tga", art));
                button.SetTransparent(true);
            });
    };

    LoadMissionButton = makeButton(0x107, 0xd1, "map", MCScreenFlow::Via("mission pick", OpenMissionLoad));
    StartButton = makeButton(0x1de, 0x1b4, "begin", [] { Room().Begin(); });
    StartButton->Disabled = true;

    // The mission description.
    MissionText = CreateChild<MCLogScrollTextObject>(
        [](MCLogScrollTextObject& text)
        {
            const int32_t textResult = text.Init(0x17f, 0x35, 0xdc, 0x82, {});
            Assert(textResult == 0, static_cast<uint32_t>(textResult),
                   " Error initing mission description on session screen ");
            text.ScrollTab->ShowGuiWindow(false);
            text.Scrolling = false;
        });

    // Each team's resource points, a number the host can type.
    auto makeRPText = [this](int32_t textX)
    {
        return CreateChild<MCLogTextObject>(
            [&](MCLogTextObject& text)
            {
                text.MCLogObject::Init(textX, 0x184, 0x42, 0xe);
                text.Font = LgBlackFont;
                text.BackgroundColor = 0x1f;
                text.CursorPos = 0;
                text.CursorPixel = 0;
                VfxPaneWipe(text.Lport()->Frame(), 0x1f);
                text.InitBuffer(8, MCLogInputType::None);
                text.SetStringBuffer("0");
                text.SetBackColor(0x10);
                text.Font = LgWhiteFont;
            });
    };

    Team1RPText = makeRPText(0x10f);
    Team2RPText = makeRPText(0x1d1);

    auto makeSpinner = [this](int32_t spinX, int32_t spinY, std::string_view art)
    {
        return CreateChild<MCLogSpinnerButton>(
            [&](MCLogSpinnerButton& spinner)
            {
                spinner.Init(spinX, spinY, 9, 7, {});
                spinner.SetUpPicture(std::format("Ses_bh_{}.tga", art));
                spinner.SetDownPicture(std::format("Ses_bg_{}.tga", art));
                spinner.SetGrayPicture(std::format("Ses_bn_{}.tga", art));
                spinner.SetEventRoutine(nullptr);
                spinner.PressSound = 0xf;
            });
    };

    Team1RPUp = makeSpinner(0x102, 0x183, "rpup");
    Team1RPDown = makeSpinner(0x102, 0x18b, "rpdn");
    Team2RPUp = makeSpinner(0x1c4, 0x183, "rpup");
    Team2RPDown = makeSpinner(0x1c4, 0x18b, "rpdn");

    // The tech base toggles (team 1: Inner Sphere, team 2: Clan).
    auto makeTechButton = [this](int32_t techX, int32_t team, int32_t techBase, bool toggledOn)
    {
        return CreateChild<MCLogToolButton>(
            [&](MCLogToolButton& techButton)
            {
                techButton.Init(techX, 0x10c, 0x27, 0xe, {});
                const std::string_view art = techBase == -1 ? "clan" : "is";
                techButton.SetUpPicture(std::format("ses_bh_{}.tga", art));
                techButton.SetOverPicture(std::format("ses_bh_{}.tga", art));
                techButton.SetDownPicture(std::format("ses_bg_{}.tga", art));
                techButton.Group = team;
                techButton.Value = techBase;

                if (toggledOn)
                {
                    techButton.Toggled = true;
                }

                techButton.SetTransparent(true);
                techButton.PressSound = 0xf;
            });
    };

    Room().Team1TechBase = 1;
    Team1ISButton = makeTechButton(0x153, 1, 1, true);
    Team1ClanButton = makeTechButton(0x179, 1, -1, false);
    Room().Team2TechBase = -1;
    Team2ISButton = makeTechButton(0x215, 2, 1, false);
    Team2ClanButton = makeTechButton(0x23b, 2, -1, true);

    // Where names can be dropped: team 1's three slots, then team 2's.
    DropTargets = {{0xf7, 0x132, 0x199, 0x144},  {0xf7, 0x148, 0x199, 0x15a},  {0xf7, 0x15d, 0x199, 0x16f},
                   {0x1b9, 0x132, 0x25b, 0x144}, {0x1b9, 0x148, 0x25b, 0x15a}, {0x1b9, 0x15d, 0x25a, 0x16f}};

    // The six name slots, each with its player number picture, each showing its seat of the room model (empty).
    Room().Seats.fill({});

    for (size_t slot = 0; slot < PlayerNames.size(); ++slot)
    {
        PlayerNames[slot] = CreateChild<MCPlayerNameObject>([](MCPlayerNameObject& made)
                                                            { made.Init(0xb, UnassignedTop, 0xa0, 0x10, {}); });
        MCPlayerNameObject& nameObject = *PlayerNames[slot];
        nameObject.SeatIndex = slot;
        nameObject.MoveTo(0xb, UnassignedTop + static_cast<int32_t>(slot) * UnassignedRow, false);
        nameObject.ShowGuiWindow(false);
        nameObject.SetTransparent(true);
        // The original wiped the name's picture to the screen's background colour and pasted the number there; the
        // name draws them each frame.
        nameObject.NumberBack = BackgroundColor;
        nameObject.NumberArt = LogArt(std::format("ses_p{}.tga", slot + 1));
    }

    // Port-only: MATCH OPTIONS, the second tab, its list in the chat window's place.
    MatchOptionsList = AddMatchOptionsList(*this);
    MatchTabs = {MakeMatchTabs(MCSessionTab::Chat), MakeMatchTabs(MCSessionTab::Options)};
    FrontTab = MCSessionTab::Chat;

    // Port-only (the personal build): a lobby's game is offered the update here; port (Redux HD): so is an internet
    // game's room the menus opened, which comes back after a restart too (network/MCLobbyUpdate.h).
    if (MCUpdater::Available())
    {
        UpdateLine = AddUpdateLine(*this, MCGenericScreen::PaletteFromArt(BackgroundArt), UpdateLineLeft, UpdateLineTop,
                                   UpdateLineWidth, UpdateLineHeight, [] { return MCLobbyUpdate::InRoom(); });
    }

    SetBackground(BackgroundArt);
    ShowGuiWindow(false);
    Room().NumUnassigned = 0;
    // Port-only (SPEC-menus.md's stage 9): the room's logic is its model's, this screen a view on it.
    Room().SetView(WeakAs<MCSessionScreen>());
    return 0;
}

auto MCSessionScreen::OnDestroy() -> void
{
    MatchTabs = {};
    DropTargets.clear();
    // Port-only (SPEC-menus.md's stage 4): the mission and the map go with the room's model, which logistics owns (the
    // screen closes as logistics goes, which may be gone already). The remade room viewing that model goes too.
    HideRoomScreen();
    ClearMap();

    if (GlobalLog() != nullptr)
    {
        GlobalLog()->Room.SetView({});
    }

    MCLogObject::OnDestroy();
}

auto MCSessionScreen::Draw() -> void
{
    // Port-only (SPEC-menus.md's stage 4): the UPDATE page closed, the remade room again in the 1998 room's place.
    if (UpdateOver && !UpdatePageShown() && ShowWindow)
    {
        UpdateOver = false;
        ShowGuiWindow(true);
        return;
    }

    if (MultiPlayer() == nullptr || MultiPlayer()->SessionManager->CurrentSession() == nullptr)
    {
        return;
    }

    MCLogObject::Draw();
    MCPane& frame = _Port->Frame();
    DrawMap(frame);

    // Port-only: the tabs, MULTIPLAYER CHAT blinking behind MATCH OPTIONS while new chat waits; the list shows the
    // match's options as they are now, the host's sent again when a player's rules change what the match plays.
    if (const std::shared_ptr<MCLogPort>& tabs = MatchTabs[static_cast<size_t>(FrontTab)]; tabs != nullptr)
    {
        const std::shared_ptr<MCLogChat> chat = GlobalLog()->Chat;
        const bool newChat = FrontTab == MCSessionTab::Options && chat != nullptr && chat->LinesAdded > ChatLinesSeen;
        DrawMatchTabs(frame, *tabs, FrontTab, newChat && (MCPort::Milliseconds() / ChatBlinkMs) % 2 == 0);
    }

    Room().Tend();
    ShowMatchOptions(*MatchOptionsList);
    MedWhiteFont->WriteString(frame, 0x180, 199, Room().MissionName);
    MedWhiteFont->WriteString(frame, 0x180, 0xe3, Room().MapName);
    MedWhiteFont->WriteString(frame, 0x20e, 199,
                              Room().MissionTime.empty() ? LoadGameString(0x37f, 0xfe) : Room().MissionTime);
    DrawStartWaits(frame);

    // Each team's resource points per player.
    if (MultiPlayer()->ClanGroupID == 0)
    {
        GlobalLog()->DrawScreenChrome(*this, frame);
        return;
    }

    int32_t players = 1;
    auto group = MultiPlayer()->SessionManager->GetGroup(MultiPlayer()->InnerSphereGroupID);
    int32_t divisor = players;

    if (group != nullptr)
    {
        divisor = static_cast<int32_t>(group->Players.size());

        if (divisor == 0)
        {
            divisor = players = 1;
        }
    }

    LgWhiteFont->WriteString(frame, 0x157, 0x185, std::format("{}", Room().Team1Points / divisor));
    group = MultiPlayer()->SessionManager->GetGroup(MultiPlayer()->ClanGroupID);

    if (group != nullptr)
    {
        divisor = static_cast<int32_t>(group->Players.size());
        players = divisor;
    }

    if (divisor == 0)
    {
        divisor = players = 1;
    }

    LgWhiteFont->WriteString(frame, 0x219, 0x185, std::format("{}", Room().Team2Points / divisor));
    GlobalLog()->DrawScreenChrome(*this, frame);
}

auto MCSessionScreen::DrawStartWaits(const MCPane& frame) -> void
{
    if (Room().StartWaits.empty())
    {
        return;
    }

    constexpr auto Left = static_cast<int16_t>(UpdateLineLeft);
    constexpr auto Top = static_cast<int16_t>(UpdateLineTop);
    constexpr auto Right = static_cast<int16_t>(UpdateLineLeft + UpdateLineWidth - 1);
    constexpr auto Bottom = static_cast<int16_t>(UpdateLineTop + UpdateLineHeight - 1);
    FillBox(Left, Top, Right, Bottom, WaitsLineOutline);
    FillBox(static_cast<int16_t>(Left + 1), static_cast<int16_t>(Top + 1), static_cast<int16_t>(Right - 1),
            static_cast<int16_t>(Bottom - 1), WaitsLineField);
    // A line too long for the field (long names) is cut at its end.
    MCGuiFont& font = *WhiteFont;
    font.WriteString(frame, UpdateLineLeft + std::max((UpdateLineWidth - font.Width(Room().StartWaits)) / 2, 1),
                     UpdateLineTop + (UpdateLineHeight - font.Height()) / 2, Room().StartWaits, UpdateLineWidth - 2);
}

auto MCSessionScreen::HandleEvent(MCGuiEvent& event) -> void
{
    // Port-only (SPEC-menus.md's stage 2): Escape goes back one screen, as EXIT does (the original's did nothing).
    if (event.Type == MCGuiEventType::KeyDown && event.Key == 0x1b)
    {
        MCScreenFlow::Back();
        return;
    }

    if (event.Type == MCGuiEventType::LeftButtonDown)
    {
        // Port-only: a click on a tab brings it in front.
        if (const std::optional<MCSessionTab> tab = MatchTabAt(event.X - GlobalX(), event.Y - GlobalY());
            tab.has_value() && MatchTabs[static_cast<size_t>(*tab)] != nullptr)
        {
            ShowTab(*tab);
        }
    }
    else if (event.Type == 0x1d)
    {
        // A name was dropped: onto a team slot, or anywhere else (no team).
        const auto nameObject = std::static_pointer_cast<MCPlayerNameObject>(event.TargetObject());
        const uint32_t id = nameObject->PlayerId();
        RECT area;
        area.left = nameObject->GlobalX();
        area.top = nameObject->GlobalY();
        area.right = area.left + 0x14;
        area.bottom = nameObject->Height() + area.top;
        RECT overlap;
        bool placed = false;

        for (size_t target = 0; target < DropTargets.size(); ++target)
        {
            if (IntersectRect(overlap, area, DropTargets[target]) != 0)
            {
                const auto team = static_cast<int8_t>(target / TeamSlots + 1);
                Room().AssignPlayer(id, team, static_cast<int8_t>(target % TeamSlots), false);
                placed = true;
                break;
            }
        }

        if (!placed)
        {
            Room().AssignPlayer(id, MCRoomModel::NoTeam, 0, false);
        }
    }

    MCGuiObject::HandleEvent(event);
}

auto MCSessionScreen::FindObject(int32_t xPos, int32_t yPos) -> std::shared_ptr<MCGuiObject>
{
    if (std::shared_ptr<MCGuiObject> onPage = UpdatePageFind(*this, xPos, yPos); onPage != nullptr)
    {
        return onPage;
    }

    return MCLogObject::FindObject(xPos, yPos);
}

auto MCSessionScreen::ShowTab(MCSessionTab tab) -> void
{
    if (FrontTab == tab)
    {
        return;
    }

    FrontTab = tab;
    SoundSystem()->PlayInterfaceSample(TabSound);
    const bool options = tab == MCSessionTab::Options;
    MatchOptionsList->ShowGuiWindow(options);

    if (const std::shared_ptr<MCLogChatWindow> chatWindow = GlobalLog()->ChatWindowOn(*this); chatWindow != nullptr)
    {
        chatWindow->ShowGuiWindow(!options);
    }

    if (const std::shared_ptr<MCLogChat> chat = GlobalLog()->Chat; chat != nullptr)
    {
        ChatLinesSeen = chat->LinesAdded;
    }
}

auto MCSessionScreen::ShowGuiWindow(bool show) -> void
{
    // Port-only (SPEC-menus.md's stage 4): the remade room in its place.
    if (!show)
    {
        UpdateOver = false;
        HideRoomScreen();
    }
    else if (!UpdateOver && ShowRoomScreen())
    {
        MCLogObject::ShowGuiWindow(false);
        return;
    }

    if (!show && FrontTab == MCSessionTab::Options)
    {
        FrontTab = MCSessionTab::Chat;
        MatchOptionsList->ShowGuiWindow(false);

        if (const std::shared_ptr<MCLogChatWindow> chatWindow = GlobalLog()->ChatWindowOn(*this); chatWindow != nullptr)
        {
            chatWindow->ShowGuiWindow(true);
        }
    }

    MCLogObject::ShowGuiWindow(show);
}

auto MCSessionScreen::ShowUpdateOver() -> void
{
    UpdateOver = true;
    HideRoomScreen();
    MCLogObject::ShowGuiWindow(true);
    ShowUpdatePageOver(*this);
}

auto MCSessionScreen::Opening() -> void
{
    SessionButton->Disabled = false;
    ExitButton->Toggled = false;
    // Port-only (SPEC-menus.md's stage 2): a room that waits for its guests to leave (a REMATCH's) stays with nothing to
    // press when it's shown again (a guest's late join).
    ExitButton->Disabled = MCRoomEnd::WaitsForGuests();
    StartButton->Disabled = true;
    // Port-only (SPEC-menus.md's stage 2): EXIT goes back one screen, as Escape does (the room's way back: a launcher's
    // asks to leave the game, which ends it).
    ExitButton->Callback().SetExec([] { MCScreenFlow::Back(); });
    int32_t slotY = UnassignedTop;

    for (auto& nameObject : PlayerNames)
    {
        nameObject->MoveTo(UnassignedLeft, slotY, false);
        nameObject->ShowGuiWindow(false);
        slotY += UnassignedRow;
    }

    // Port-only: the screen opens on the chat.
    FrontTab = MCSessionTab::Chat;
    MatchOptionsList->ShowGuiWindow(false);
    MatchOptionsList->Reset();
}

auto MCSessionScreen::Opened() -> void
{
    GlobalLog()->ShowLightsOn(*this, std::nullopt);

    if (const std::shared_ptr<MCLogChatWindow> chatWindow = GlobalLog()->ChatWindowOn(*this); chatWindow != nullptr)
    {
        chatWindow->MoveTo(2, 0x42, false);
        chatWindow->Resize(0x10a);
        GlobalLog()->ShowChatOn(*this);
        GlobalLog()->ShowChat(true);
    }

    // The ticker shows on this screen now.
    GlobalLog()->ShowTickerOn(*this);
}

auto MCSessionScreen::NameToRow(size_t seat, int32_t row) -> void
{
    PlayerNames.at(seat)->MoveTo(UnassignedLeft, row * UnassignedRow + UnassignedTop, false);
}

auto MCSessionScreen::NameToTeam(size_t seat, int8_t team, int8_t slot) -> void
{
    PlayerNames.at(seat)->MoveTo(team == MCRoomModel::Team1 ? Team1NameLeft : Team2NameLeft,
                                 slot * TeamNameRow + TeamNameTop, false);
}

auto MCSessionScreen::ShowName(size_t seat, bool shown) -> void
{
    PlayerNames.at(seat)->ShowGuiWindow(shown);
}

auto MCSessionScreen::ShowPoints(int8_t team) -> void
{
    (team == MCRoomModel::Team1 ? Team1RPText : Team2RPText)->SetStringBuffer(std::format("{}", Room().Points(team)));
}

auto MCSessionScreen::TakeTypedPoints() -> void
{
    Room().PointsText = {std::string(Team1RPText->Text()), std::string(Team2RPText->Text())};
}

auto MCSessionScreen::ShowMissionText(int32_t font, std::span<const std::string> lines) -> void
{
    MissionText->Clear();
    MissionText->FontIndex = font;

    for (const std::string& line : lines)
    {
        MissionText->Print(line, DescriptionColour);
    }
}

auto MCSessionScreen::ShowMap() -> void
{
    ClearMap();
    const std::string& fileName = Room().MapFile;

    if (fileName.empty())
    {
        // No mission: clear the map box (the original wiped it in the background picture).
        MapBoxWiped = true;
        return;
    }

    // The original stretched the map picture over the box in the background picture; the screen keeps the picture
    // and draws it so each frame. A map picture that can't be loaded is fatal.
    MapPicture = std::make_shared<MCLogPort>();
    MapPicture->Load(GamePath(TerrainPath, fileName, ".log.tga"));
}

auto MCSessionScreen::ShowControls() -> void
{
    SetTeamControls(Room().ForcesLive);

    for (auto& nameObject : PlayerNames)
    {
        nameObject->Draggable = Room().HostControls;
    }

    LoadMissionButton->Disabled = !Room().HostControls;
}

auto MCSessionScreen::ShowTechBases() -> void
{
    Team1ISButton->Toggled = Room().Team1TechBase == MCRoomModel::InnerSphere;
    Team1ClanButton->Toggled = Room().Team1TechBase == MCRoomModel::Clan;
    Team2ISButton->Toggled = Room().Team2TechBase == MCRoomModel::InnerSphere;
    Team2ClanButton->Toggled = Room().Team2TechBase == MCRoomModel::Clan;
}

auto MCSessionScreen::ShowStart() -> void
{
    StartButton->Disabled = !Room().CanStart;

    if (UpdateLine != nullptr)
    {
        UpdateLine->ShowGuiWindow(Room().StartWaits.empty());
    }
}

auto MCSessionScreen::LeftMissionPick() -> void
{
    MCSplashScreen& loadScreen = *GlobalLog()->LoadScreen;
    // Port-only (SPEC-menus.md's stage 2): CANCEL goes back one screen, as Escape does.
    loadScreen.CancelButton->Callback().SetExec([] { MCScreenFlow::Back(); });
    loadScreen.LoadSaveButton->Callback().SetExec(LoadGame);
    loadScreen.FilePane->SetMultiplayer(false);

    // Port-only (the personal build): the room shown again loads its mission on the session screen itself.
    // Port-only (SPEC-menus.md's stage 2): through the flow, with the one fade.
    if (GlobalLog()->CurrentScreen() != GlobalLog()->SessionScreen)
    {
        MCScreenFlow::Change("mission picked",
                             []
                             {
                                 GlobalLog()->CurrentScreen()->ShowGuiWindow(false);
                                 GlobalLog()->SetCurrentScreen(Screen());
                                 GlobalLog()->LogisticsState = 8;
                                 GlobalLog()->ShowLogScreen(true, true);
                             });
    }
}

auto MCSessionScreen::DrawNow() -> void
{
    Draw();
}

auto MCSessionScreen::StopBoxTimers() -> void
{
    // Original behaviour (OB-167): the same timer is removed twice (team 2's is left).
    GuiSystem().RemoveTimer(*Team1RPText, BoxTimer);
    GuiSystem().RemoveTimer(*Team1RPText, BoxTimer);
}

auto MCSessionScreen::DrawMap(const MCPane& target) const -> void
{
    if (MapBoxWiped)
    {
        MCPane box = target;
        box.X0 = 0xf4;
        box.Y0 = 0x33;
        box.X1 = 0x177;
        box.Y1 = 0xb6;
        VfxPaneWipe(box, 0x10);
    }

    if (MapPicture == nullptr)
    {
        return;
    }

    // Stretch the map picture over the box.
    const int32_t maxU = MapPicture->Bitmap().XMax;
    const int32_t maxV = MapPicture->Bitmap().YMax;
    std::array<MCScreenVertex, 4> corners = {{
        {0xf4, 0x33, 0, 0, 0, 0},
        {0x177, 0x33, 0, maxU << 16, 0, 0},
        {0x177, 0xb6, 0, maxU << 16, maxV << 16, 0},
        {0xf4, 0xb6, 0, 0, maxV << 16, 0},
    }};

    VfxMapPolygon(target, corners, MapPicture->Bitmap(), VfxMapTransparent);
}

auto MCSessionScreen::ClearMap() -> void
{
    MapPicture.reset();
}

auto MCSessionScreen::SetTeamControls(bool live) const -> void
{
    const MCLogInputType input = live ? MCLogInputType::Digits : MCLogInputType::None;
    Team1RPText->AllowedInput = input;
    Team2RPText->AllowedInput = input;
    Team1RPUp->Callback().SetExec(live ? IncrementTeam1RP : nullptr);
    Team1RPDown->Callback().SetExec(live ? DecrementTeam1RP : nullptr);
    Team2RPUp->Callback().SetExec(live ? IncrementTeam2RP : nullptr);
    Team2RPDown->Callback().SetExec(live ? DecrementTeam2RP : nullptr);

    for (const MCGuiHandle<MCLogSpinnerButton>& spinner : {Team1RPUp, Team1RPDown, Team2RPUp, Team2RPDown})
    {
        spinner->Disabled = !live;
    }

    for (const MCGuiHandle<MCLogToolButton>& techButton :
         {Team1ISButton, Team1ClanButton, Team2ISButton, Team2ClanButton})
    {
        techButton->SetEventRoutine(live ? std::function<void(MCGuiObject&, MCGuiEvent&)>(TechTabRoutine) : nullptr);
    }
}

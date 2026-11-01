#include "stdafx.h"
#include "logistics/MCSessionScreen.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpGroup.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogChatWindow.h"
#include "logistics/MCLogScrollTextObject.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCLogToolButton.h"
#include "logistics/MCPlayerNameObject.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCTicker.h"
#include "main/MCGamePaths.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCMainMenu.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "network/MCMultiPlayer.h"
#include "sound/MCSoundSystem.h"
#include "vfx/MCVfxFunctions.h"
#include "logistics/MCBriefingScreen.h"

namespace
{
    /// <summary>The top of the unassigned list and the height of its rows.</summary>
    constexpr int32_t UnassignedTop = 0x162;
    constexpr int32_t UnassignedRow = 0x14;
    /// <summary>The screen's timer (pings and the resource point updates).</summary>
    constexpr int32_t ScreenTimer = 0;

    MCSessionScreen& Screen()
    {
        return *GlobalLog()->SessionScreen;
    }

    /// <summary>The slot of <paramref name="playerId"/> on <paramref name="team"/>, or -1.</summary>
    int32_t FindSlot(std::span<const uint32_t> team, uint32_t playerId)
    {
        const auto found = std::ranges::find(team, playerId);
        return found == team.end() ? -1 : static_cast<int32_t>(found - team.begin());
    }

    /// <summary>The number in a resource point text, as the original's <c>atol</c> read it.</summary>
    int32_t ReadPoints(const MCLogTextObject& text)
    {
        return std::atol(std::string(text.Text()).c_str());
    }

    /// <summary>
    /// Sends a guaranteed message carrying a file name (<paramref name="type"/> load mission or start) to every
    /// player.
    /// </summary>
    void SendFileName(uint16_t type, std::string_view fileName)
    {
        // The header, then the name and its NUL.
        constexpr size_t headerSize = sizeof(MCMPFileNameMessage);
        MCMPFileNameMessage header{};
        header.Tagger.Clear();
        header.Header = type;
        std::vector<uint8_t> buffer(headerSize + fileName.size() + 1, 0);
        std::memcpy(buffer.data(), &header, headerSize);
        std::ranges::copy(fileName, buffer.begin() + headerSize);
        MultiPlayer()->SessionManager->SendMessageToGroup(0, buffer);
    }

    /// <summary>Sends a two-long guaranteed message to every player.</summary>
    void SendTwoLongs(uint16_t type, int32_t value1, int32_t value2)
    {
        MCMPTwoLongMessage message{};
        message.Header = type;
        message.Value1 = value1;
        message.Value2 = value2;
        MultiPlayer()->SessionManager->SendMessageToGroup(0, LinkupBytes(message));
    }

    /// <summary>The file name part of <paramref name="path"/> without folder or extension (<c>_splitpath</c>'s fname).</summary>
    std::string SplitFileName(std::string_view path)
    {
        const size_t slash = path.find_last_of("\\/:");
        std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
        const size_t dot = name.rfind('.');
        return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
    }

    /// <summary>The dialog's Cancel: drops the loaded mission.</summary>
    void MPCancel()
    {
        GlobalLog()->MessageDialog->Callback = nullptr;
        Screen().CancelMission();
    }

    /// <summary>After a multiplayer save: returns to the session screen and tells the others which file to load.</summary>
    void MPLoadWorked(int32_t)
    {
        MCSplashScreen& loadScreen = *GlobalLog()->LoadScreen;
        loadScreen.CancelButton->Callback().SetExec(Cancel);
        loadScreen.LoadSaveButton->Callback().SetExec(LoadGame);
        loadScreen.FilePane->SetMultiplayer(false);
        GlobalLog()->CurrentScreen()->ShowGuiWindow(false);
        GlobalLog()->SetCurrentScreen(Screen());
        GlobalLog()->LogisticsState = 8;
        GlobalLog()->ShowLogScreen(true, true);
        SendFileName(GuaranteedHeader(MCMPMessageType::LoadMission), Screen().MissionFile);
    }

    /// <summary>The load-mission button: opens the load screen for a multiplayer mission.</summary>
    void OpenMissionLoad()
    {
        MCSplashScreen& loadScreen = *GlobalLog()->LoadScreen;
        loadScreen.LoadSaveButton->Callback().SetExec(LoadMPGame);
        loadScreen.CancelButton->Callback().SetExec(CancelToSession);
        loadScreen.FilePane->SetMultiplayer(true);
        GlobalLog()->CurrentScreen()->ShowGuiWindow(false);
        GlobalLog()->SetCurrentScreen(loadScreen);
        GlobalLog()->LogisticsState = 5;
        GlobalLog()->ShowLogScreen(true, false);
    }

    /// <summary>The start button: tells every player to start the loaded mission.</summary>
    void StartMission()
    {
        std::string missionName = SplitFileName(Screen().MissionFile);
        std::string extension = ".MPK";
        SendFileName(GuaranteedHeader(MCMPMessageType::Start), missionName);
        GuiSystem().RemoveTimer(Screen(), ScreenTimer);
        MultiPlayer()->SessionManager->SendLatencyInfo();
        SoundSystem()->PlayBettySample(0x19);
        Screen().PublishMatchState();
        GlobalLog()->InitializeMultiplayer();
        GlobalLog()->LoadCampaign(missionName, extension, false, false);
        GlobalLog()->SetUpBriefingScreen(false);
    }

    /// <summary>A tech base toggle: sets its team's tech base and tells the others.</summary>
    void TechTabRoutine(MCGuiObject& object, MCGuiEvent& event)
    {
        if (event.Type != 1)
        {
            return;
        }

        auto& button = static_cast<MCLogToolButton&>(object);
        const int32_t techBase = button.Value;
        const int32_t team = button.Group;
        Screen().SetTeamTechBase(static_cast<int8_t>(team), static_cast<int8_t>(techBase));

        if (MultiPlayer()->IsHost != 0)
        {
            SendTwoLongs(GuaranteedHeader(MCMPMessageType::TechbaseChange), team, techBase);
        }
    }

    /// <summary>A team's resource points up 1000.</summary>
    void IncrementTeam1RP()
    {
        Screen().SetTeam1RP(ReadPoints(*Screen().Team1RPText) + 1000);
    }

    void IncrementTeam2RP()
    {
        Screen().SetTeam2RP(ReadPoints(*Screen().Team2RPText) + 1000);
    }

    /// <summary>A team's resource points down 1000 (not below 0).</summary>
    void DecrementTeam1RP()
    {
        Screen().SetTeam1RP(std::max(ReadPoints(*Screen().Team1RPText) - 1000, 0));
    }

    void DecrementTeam2RP()
    {
        Screen().SetTeam2RP(std::max(ReadPoints(*Screen().Team2RPText) - 1000, 0));
    }

    /// <summary>The screen's paint routine: the unassigned list's background and a bar per unassigned player.</summary>
    void PaintUnassigned(MCGuiObject& object)
    {
        auto& screen = static_cast<MCLogObject&>(object);
        screen.FillBox(0x1f, 0x162, 0xc6, 0x1d3, 0x10);
        int16_t top = 0x163;

        for (int32_t row = 0; row < Screen().NumUnassigned; ++row)
        {
            screen.FillBox(0x1f, top, 0xc6, static_cast<int16_t>(top + 0xd), 0x12);
            top += UnassignedRow;
        }
    }
}

MCSessionScreen::~MCSessionScreen()
{
    MCSessionScreen::Destroy();
}

auto MCSessionScreen::SortedPlayerIds(std::span<const uint32_t> ids) -> std::array<uint32_t, MaxPlayers>
{
    std::array<uint32_t, MaxPlayers> sorted;
    sorted.fill(NoPlayer);
    std::ranges::copy(ids.first(std::min(ids.size(), MaxPlayers)), sorted.begin());
    // Empty slots are the largest id, so they end up last.
    std::ranges::sort(sorted);
    return sorted;
}

auto MCSessionScreen::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view) -> int32_t
{
    int32_t result = MCLogObject::Init(xPos, yPos, width, height);
    SetPaintRoutine(PaintUnassigned);
    Assert(result == 0, static_cast<uint32_t>(result), " Error initing session screen ");

    // The screen tabs.
    auto makeTab = [this](int32_t tabY, std::string_view art)
    {
        auto tab = MCMakeGui<MCLogToolButton>();
        const int32_t tabResult = tab->Init(2, tabY, 0xcf, 0x12, {});
        Assert(tabResult == 0, static_cast<uint32_t>(tabResult), " Error initing session button on session screen ");
        tab->SetUpPicture(std::format("bn_{}.tga", art));
        tab->SetDownPicture(std::format("bg_{}.tga", art));
        tab->SetOverPicture(std::format("bh_{}.tga", art));
        tab->SetEventRoutine(LScreenSwitchEventHandler);
        AddChild(tab);
        return tab;
    };

    SessionButton = makeTab(0x10, "session");
    SessionButton->Toggled = true;
    ExitButton = makeTab(0x22, "exit");
    ExitButton->Callback().SetExec(LaunchedFromLobby == 0 ? Cancel : CancelToMPlayer);

    // The host's load and start buttons.
    auto makeButton = [this](int32_t buttonX, int32_t buttonY, std::string_view art, std::function<void()> exec)
    {
        auto button = MCMakeGui<MCLogButton>();
        const int32_t buttonResult = button->Init(buttonX, buttonY, 0x5e, 0x13, {});
        Assert(buttonResult == 0, static_cast<uint32_t>(buttonResult), " Error initing load button on session screen ");
        button->SetUpPicture(std::format("ses_bh_{}.tga", art));
        button->SetOverPicture(std::format("ses_bh_{}.tga", art));
        button->SetDownPicture(std::format("ses_bg_{}.tga", art));
        button->Callback().SetExec(std::move(exec));
        button->SetGrayPicture(std::format("ses_bn_{}.tga", art));
        button->SetTransparent(true);
        AddChild(button);
        return button;
    };

    LoadMissionButton = makeButton(0x107, 0xd1, "map", OpenMissionLoad);
    StartButton = makeButton(0x1de, 0x1b4, "begin", StartMission);
    StartButton->Disabled = true;

    // The mission description.
    MissionText = MCMakeGui<MCLogScrollTextObject>();
    result = MissionText->Init(0x17f, 0x35, 0xdc, 0x82, {});
    Assert(result == 0, static_cast<uint32_t>(result), " Error initing mission description on session screen ");
    MissionText->ScrollTab->ShowGuiWindow(false);
    MissionText->Scrolling = false;
    AddChild(MissionText);

    // Each team's resource points, a number the host can type.
    auto makeRPText = [this](int32_t textX)
    {
        auto text = MCMakeGui<MCLogTextObject>();
        text->MCLogObject::Init(textX, 0x184, 0x42, 0xe);
        text->Font = LgBlackFont;
        text->BackgroundColor = 0x1f;
        text->CursorPos = 0;
        text->CursorPixel = 0;
        VfxPaneWipe(text->Lport()->Frame(), 0x1f);
        text->InitBuffer(8, MCLogInputType::None);
        text->SetStringBuffer("0");
        text->SetBackColor(0x10);
        text->Font = LgWhiteFont;
        AddChild(text);
        return text;
    };

    Team1RPText = makeRPText(0x10f);
    Team2RPText = makeRPText(0x1d1);

    auto makeSpinner = [this](int32_t spinX, int32_t spinY, std::string_view art)
    {
        auto spinner = MCMakeGui<MCLogSpinnerButton>();
        spinner->Init(spinX, spinY, 9, 7, {});
        spinner->SetUpPicture(std::format("Ses_bh_{}.tga", art));
        spinner->SetDownPicture(std::format("Ses_bg_{}.tga", art));
        spinner->SetGrayPicture(std::format("Ses_bn_{}.tga", art));
        spinner->SetEventRoutine(nullptr);
        spinner->PressSound = 0xf;
        AddChild(spinner);
        return spinner;
    };

    Team1RPUp = makeSpinner(0x102, 0x183, "rpup");
    Team1RPDown = makeSpinner(0x102, 0x18b, "rpdn");
    Team2RPUp = makeSpinner(0x1c4, 0x183, "rpup");
    Team2RPDown = makeSpinner(0x1c4, 0x18b, "rpdn");

    // The tech base toggles (team 1: Inner Sphere, team 2: Clan).
    auto makeTechButton = [this](int32_t techX, int32_t team, int32_t techBase, bool toggledOn)
    {
        auto techButton = MCMakeGui<MCLogToolButton>();
        techButton->Init(techX, 0x10c, 0x27, 0xe, {});
        const std::string_view art = techBase == -1 ? "clan" : "is";
        techButton->SetUpPicture(std::format("ses_bh_{}.tga", art));
        techButton->SetOverPicture(std::format("ses_bh_{}.tga", art));
        techButton->SetDownPicture(std::format("ses_bg_{}.tga", art));
        techButton->Group = team;
        techButton->Value = techBase;

        if (toggledOn)
        {
            techButton->Toggled = true;
        }

        techButton->SetTransparent(true);
        techButton->PressSound = 0xf;
        AddChild(techButton);
        return techButton;
    };

    Team1TechBase = 1;
    Team1ISButton = makeTechButton(0x153, 1, 1, true);
    Team1ClanButton = makeTechButton(0x179, 1, -1, false);
    Team2TechBase = -1;
    Team2ISButton = makeTechButton(0x215, 2, 1, false);
    Team2ClanButton = makeTechButton(0x23b, 2, -1, true);

    // Where names can be dropped: team 1's three slots, then team 2's.
    DropTargets = {{0xf7, 0x132, 0x199, 0x144},  {0xf7, 0x148, 0x199, 0x15a},  {0xf7, 0x15d, 0x199, 0x16f},
                   {0x1b9, 0x132, 0x25b, 0x144}, {0x1b9, 0x148, 0x25b, 0x15a}, {0x1b9, 0x15d, 0x25a, 0x16f}};

    // The six name slots, each with its player number picture.
    for (size_t slot = 0; slot < PlayerNames.size(); ++slot)
    {
        auto& nameObject = PlayerNames[slot];
        nameObject = MCMakeGui<MCPlayerNameObject>();
        nameObject->Init(0xb, UnassignedTop, 0xa0, 0x10, {});
        AddChild(nameObject);
        nameObject->MoveTo(0xb, UnassignedTop + static_cast<int32_t>(slot) * UnassignedRow, false);
        nameObject->ShowGuiWindow(false);
        nameObject->SetTransparent(true);
        // The original wiped the name's picture to the screen's background colour and pasted the number there; the
        // name draws them each frame.
        nameObject->NumberBack = BackgroundColor;
        nameObject->NumberArt = LogArt(std::format("ses_p{}.tga", slot + 1));
    }

    SetBackground("ses_bk00.tga");
    ScreenWindow()->AddChild(weak_from_this());
    ShowGuiWindow(false);
    NumUnassigned = 0;
    return 0;
}

auto MCSessionScreen::Destroy() -> void
{
    SessionButton.reset();
    ExitButton.reset();
    LoadMissionButton.reset();
    StartButton.reset();
    MissionText.reset();
    Team1RPText.reset();
    Team2RPText.reset();
    Team1RPUp.reset();
    Team1RPDown.reset();
    Team2RPUp.reset();
    Team2RPDown.reset();
    Team1ISButton.reset();
    Team1ClanButton.reset();
    Team2ISButton.reset();
    Team2ClanButton.reset();

    for (auto& nameObject : PlayerNames)
    {
        nameObject.reset();
    }

    DropTargets.clear();
    MissionFile.clear();
    MapName.clear();
    ClearMap();
    MCLogObject::Destroy();
}

auto MCSessionScreen::Draw() -> void
{
    if (MultiPlayer() == nullptr || MultiPlayer()->SessionManager->CurrentSession() == nullptr)
    {
        return;
    }

    MCLogObject::Draw();
    MCPane& frame = _Port->Frame();
    DrawMap(frame);
    MedWhiteFont->WriteString(frame, 0x180, 199, MissionName);
    MedWhiteFont->WriteString(frame, 0x180, 0xe3, MapName);
    MedWhiteFont->WriteString(frame, 0x20e, 199, MissionLabel.empty() ? LoadGameString(0x37f, 0xfe) : MissionLabel);

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

    LgWhiteFont->WriteString(frame, 0x157, 0x185, std::format("{}", ReadPoints(*Team1RPText) / divisor));
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

    LgWhiteFont->WriteString(frame, 0x219, 0x185, std::format("{}", ReadPoints(*Team2RPText) / divisor));
    GlobalLog()->DrawScreenChrome(*this, frame);
}

auto MCSessionScreen::HandleEvent(MCGuiEvent& event) -> void
{
    if (event.Type == 0x13)
    {
        if (event.Data == ScreenTimer)
        {
            if (MultiPlayer() == nullptr)
            {
                GuiSystem().RemoveTimer(*this, ScreenTimer);
            }
            else
            {
                // Ping for the first three seconds so the latencies are known.
                if (_Pinging)
                {
                    if (MCPort::Milliseconds() < _PingUntil)
                    {
                        if (MultiPlayer()->SessionManager->IsHost == 0)
                        {
                            MultiPlayer()->SessionManager->SendPing();
                        }
                    }
                    else
                    {
                        _Pinging = false;
                    }
                }

                // Tell the others when the host changed a team's points.
                if (Team1RP != ReadPoints(*Team1RPText))
                {
                    Team1RP = ReadPoints(*Team1RPText);

                    if (MultiPlayer()->IsHost != 0)
                    {
                        SendTwoLongs(GuaranteedHeader(MCMPMessageType::RPUpdate), Team1RP, 1);
                    }
                }

                if (Team2RP != ReadPoints(*Team2RPText))
                {
                    Team2RP = ReadPoints(*Team2RPText);

                    if (MultiPlayer()->IsHost != 0)
                    {
                        SendTwoLongs(GuaranteedHeader(MCMPMessageType::RPUpdate), Team2RP, 2);
                    }
                }
            }
        }
    }
    else if (event.Type == 0x1d)
    {
        // A name was dropped: onto a team slot, or anywhere else (no team).
        const auto nameObject = std::static_pointer_cast<MCPlayerNameObject>(event.TargetObject());
        const uint32_t id = nameObject->PlayerId;
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
                AssignPlayer(id, team, static_cast<int8_t>(target % TeamSlots), false);
                placed = true;
                break;
            }
        }

        if (!placed)
        {
            AssignPlayer(id, 0, 0, false);
        }
    }

    MCGuiObject::HandleEvent(event);
}

auto MCSessionScreen::Activate(bool refresh) -> void
{
    const bool isHost = MultiPlayer()->IsHost != 0;

    if (MultiPlayer()->SessionManager->GetPlayers(nullptr).size() < 2)
    {
        Cancel();
        return;
    }

    _PingUntil = MCPort::Milliseconds() + 3000;
    _Pinging = true;
    SessionButton->Disabled = false;
    ExitButton->Toggled = false;
    ExitButton->Disabled = false;
    StartButton->Disabled = true;

    if (LaunchedFromLobby != 0)
    {
        ExitButton->Callback().SetExec(CancelToMPlayer);
    }

    CancelMission();
    Team2RP = 0;
    Team1RP = 0;
    int32_t slotY = UnassignedTop;

    for (auto& nameObject : PlayerNames)
    {
        nameObject->MoveTo(0xb, slotY, false);
        nameObject->ShowGuiWindow(false);
        slotY += UnassignedRow;
    }

    Team1Players.fill(NoPlayer);
    Team2Players.fill(NoPlayer);
    NumUnassigned = 0;
    NumPlayers = 0;

    if (isHost && !refresh)
    {
        SendTwoLongs(GuaranteedHeader(MCMPMessageType::SwitchScreen), 1, 0);
    }

    Team1ISButton->Toggled = true;
    Team1ClanButton->Toggled = false;
    Team2ISButton->Toggled = false;
    Team1TechBase = 1;
    Team2ClanButton->Toggled = true;
    Team2TechBase = -1;

    // The players' ids in ascending order (the empty slots last).
    std::vector<uint32_t> sessionIds;
    const auto& players = MultiPlayer()->SessionManager->GetPlayers(nullptr);
    NumPlayers = static_cast<int32_t>(players.size());

    for (const auto& player : players)
    {
        sessionIds.push_back(player->Id);
    }

    const std::array<uint32_t, MaxPlayers> ids = SortedPlayerIds(sessionIds);

    if (GlobalLog()->PlayerLights.get() == nullptr)
    {
        GlobalLog()->PlayerLights = MCMakeGui<MCMPPlayerLights>();
        GlobalLog()->PlayerLights->Init();
    }

    MCMPPlayerLights& lights = *GlobalLog()->PlayerLights;

    if (!lights.Parent.expired())
    {
        lights.ParentObject()->RemoveChild(lights);
    }

    AddChild(lights.weak_from_this());
    lights.SetNumPlayers(NumPlayers);
    size_t index = 0;

    do
    {
        MCPlayerNameObject& nameObject = *PlayerNames[index];
        nameObject.SetPlayerId(ids[index]);
        nameObject.ShowGuiWindow(true);
        nameObject.Draggable = false;
        AssignPlayer(ids[index], 0, 0, true);
        lights.SetPlayerID(static_cast<int32_t>(index), ids[index]);
        lights.SetPlayerStatus(ids[index], refresh ? 2 : 0);
        ++index;
    } while (index < MaxPlayers && ids[index] != NoPlayer);

    if (!isHost || refresh)
    {
        ControlsOff();

        if (refresh)
        {
            // Back from a mission: wait for everyone to check in again.
            MissionText->Clear();
            MissionText->FontIndex = 2;
            MissionText->Print(LoadGameString(0xb8, 0xfe), 0x1f);
            MCFIGuaranteedMessageHeader checkIn{};
            checkIn.Header = GuaranteedHeader(MCMPMessageType::SessionCheckIn);
            MultiPlayer()->SessionManager->SendMessageToGroup(0, LinkupBytes(checkIn));
            MultiPlayer()->PlayerSessionCheckIn[MultiPlayer()->SessionManager->MyPlayer()->PlayerNumber] = -1;
            SomeoneCheckedIn();
        }
    }
    else
    {
        ControlsOn();
    }

    MissionName.clear();
    MapName.clear();
    MissionLabel.clear();

    if (const std::shared_ptr<MCLogChatWindow> chatWindow = GlobalLog()->ChatWindow; chatWindow != nullptr)
    {
        chatWindow->MoveTo(2, 0x42, false);
        chatWindow->Resize(0x10a);

        if (!chatWindow->Parent.expired())
        {
            chatWindow->ParentObject()->RemoveChild(*chatWindow);
        }

        AddChild(chatWindow->weak_from_this());
        chatWindow->ShowGuiWindow(true);
    }

    if (const std::shared_ptr<MCTicker> ticker = GlobalLog()->Ticker; ticker != nullptr)
    {
        if (!ticker->Parent.expired())
        {
            ticker->ParentObject()->RemoveChild(*ticker);
        }

        AddChild(ticker->weak_from_this());
        // The ticker paints into this screen now (what it shows is kept in the screen's chrome).
        ticker->SetScreen(weak_from_this());
        ticker->SetPos(3, 3);
    }

    GuiSystem().AddTimer(weak_from_this(), ScreenTimer, 500, 0, 0, false);
    MultiPlayer()->ChatCallback = LogisticsChatCallback;
}

auto MCSessionScreen::NameOf(uint32_t playerId) -> std::shared_ptr<MCPlayerNameObject>
{
    for (auto& candidate : PlayerNames)
    {
        if (candidate->PlayerId == playerId)
        {
            return candidate;
        }
    }

    return nullptr;
}

auto MCSessionScreen::LayOutUnassigned(uint32_t skipId, size_t names, bool skipEmpty) -> void
{
    NumUnassigned = 0;

    for (size_t index = 0; index < names; ++index)
    {
        MCPlayerNameObject& nameObject = *PlayerNames[index];
        const uint32_t id = nameObject.PlayerId;

        if (skipEmpty ? id == NoPlayer : id == skipId)
        {
            continue;
        }

        if (FindSlot(Team1Players, id) >= 0 || FindSlot(Team2Players, id) >= 0)
        {
            continue;
        }

        nameObject.MoveTo(0xb, NumUnassigned * UnassignedRow + UnassignedTop, false);
        NumUnassigned++;
    }
}

auto MCSessionScreen::AssignPlayer(uint32_t playerId, int8_t team, int8_t slot, bool remote) -> void
{
    std::shared_ptr<MCPlayerNameObject> nameObject = NameOf(playerId);
    const bool onTeam = team == 1 || team == 2;

    // Issue #25: the host's join-team message names the player and slot; a player the screen doesn't list, or a slot
    // past the team's, is ignored (the original moved a null name and wrote past the team).
    if ((onTeam && (slot < 0 || static_cast<size_t>(slot) >= TeamSlots)) ||
        (nameObject == nullptr && (onTeam || (team == 0 && !remote))))
    {
        return;
    }

    if (team == 0)
    {
        if (remote)
        {
            // Filling the screen: the player starts unassigned.
            NumUnassigned++;
            CheckGoodToGo();
            return;
        }

        int32_t index = FindSlot(Team1Players, playerId);
        uint32_t groupID = MultiPlayer()->InnerSphereGroupID;
        std::span<uint32_t> players = Team1Players;

        if (index < 0)
        {
            index = FindSlot(Team2Players, playerId);
            groupID = MultiPlayer()->ClanGroupID;
            players = Team2Players;
        }

        if (index >= 0)
        {
            nameObject->MoveTo(0xb, NumUnassigned * UnassignedRow + UnassignedTop, false);
            players[static_cast<size_t>(index)] = NoPlayer;
            MultiPlayer()->SessionManager->RemovePlayerFromGroup(groupID, playerId);
            NumUnassigned++;
        }
        else
        {
            LayOutUnassigned(NoPlayer, MaxPlayers, true);
        }

        GlobalLog()->PlayerLights->SetPlayerStatus(playerId, 0);
    }
    else if (team == 1 || team == 2)
    {
        std::array<uint32_t, TeamSlots>& teamPlayers = team == 1 ? Team1Players : Team2Players;
        const auto slotIndex = static_cast<size_t>(slot);

        if (teamPlayers[slotIndex] != NoPlayer)
        {
            AssignPlayer(teamPlayers[slotIndex], 0, 0, false);
        }

        if (const int32_t index = FindSlot(Team1Players, playerId); index >= 0)
        {
            Team1Players[static_cast<size_t>(index)] = NoPlayer;
            MultiPlayer()->SessionManager->RemovePlayerFromGroup(MultiPlayer()->InnerSphereGroupID, playerId);
        }
        else if (const int32_t team2Index = FindSlot(Team2Players, playerId); team2Index >= 0)
        {
            Team2Players[static_cast<size_t>(team2Index)] = NoPlayer;
            MultiPlayer()->SessionManager->RemovePlayerFromGroup(MultiPlayer()->ClanGroupID, playerId);
        }
        else
        {
            LayOutUnassigned(playerId, static_cast<size_t>(NumPlayers), false);
        }

        teamPlayers[slotIndex] = playerId;
        const uint32_t groupID = team == 1 ? MultiPlayer()->InnerSphereGroupID : MultiPlayer()->ClanGroupID;
        MultiPlayer()->SessionManager->AddPlayerToGroup(groupID, playerId);

        if (MultiPlayer()->SessionManager->MyPlayer()->Id == playerId)
        {
            MultiPlayer()->HomeTeam = team == 1 ? 0 : 1;
            MultiPlayer()->HomeTeamGroupID = groupID;
            MultiPlayer()->EnemyTeamGroupID =
                team == 1 ? MultiPlayer()->ClanGroupID : MultiPlayer()->InnerSphereGroupID;
        }

        nameObject->MoveTo(team == 1 ? 0xf8 : 0x1ba, slot * 0x16 + 0x133, false);
        GlobalLog()->PlayerLights->SetPlayerStatus(playerId, 1);

        if (remote)
        {
            CheckGoodToGo();
            return;
        }
    }
    else if (remote)
    {
        CheckGoodToGo();
        return;
    }

    if (MultiPlayer()->IsHost != 0)
    {
        MCMPJoinTeamMessage message{};
        message.Header = GuaranteedHeader(MCMPMessageType::JoinTeam);
        message.PlayerID = playerId;
        message.Team = team;
        message.Slot = slot;
        MultiPlayer()->SessionManager->SendMessageToGroup(0, LinkupBytes(message));
    }

    CheckGoodToGo();
}

auto MCSessionScreen::SetTeam1RP(int32_t resourcePoints) const -> void
{
    Team1RPText->SetStringBuffer(std::format("{}", resourcePoints));
}

auto MCSessionScreen::SetTeam2RP(int32_t resourcePoints) const -> void
{
    Team2RPText->SetStringBuffer(std::format("{}", resourcePoints));
}

auto MCSessionScreen::SetMap(std::string_view fileName) -> void
{
    ClearMap();

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

auto MCSessionScreen::SetMissionName(std::string_view name) -> void
{
    MissionName = name;
}

auto MCSessionScreen::SetMapName(std::string_view name) -> void
{
    MapName = name;
}

auto MCSessionScreen::SomeoneCheckedIn() -> void
{
    bool allIn = true;

    for (size_t index = 0; index < PlayerNames.size(); ++index)
    {
        const uint32_t id = PlayerNames[index]->PlayerId;

        if (id == NoPlayer)
        {
            continue;
        }

        // Original behaviour: the check-in flags are read by name slot, not by player number.
        if (MultiPlayer()->PlayerSessionCheckIn[index] == 0)
        {
            allIn = false;
        }
        else
        {
            GlobalLog()->PlayerLights->SetPlayerStatus(id, 0);
        }
    }

    if (allIn)
    {
        MissionText->Clear();
        MissionText->FontIndex = 0;

        if (MultiPlayer()->IsHost != 0)
        {
            ControlsOn();
        }
    }
}

auto MCSessionScreen::ShowDialog(std::string_view text, std::function<void()> onOk, std::string_view upArt,
                                 std::string_view downArt, std::function<void(int32_t)> onResult) -> void
{
    MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
    dialog.SetText(text);
    dialog.SetTwoButton(false);
    dialog.OkButton->Callback().SetExec(std::move(onOk));
    dialog.OkButton->SetUpPicture(upArt);
    dialog.OkButton->SetDownPicture(downArt);
    dialog.OkButton->Disabled = false;
    dialog.Callback = std::move(onResult);
    dialog.Activate();
}

auto MCSessionScreen::FileReport(uint32_t playerId, bool haveFile) -> void
{
    using Status = MCPlayerNameObject::MCFileStatus;

    // Nothing to do unless an inquiry is out.
    if (std::ranges::all_of(PlayerNames, [](const auto& name) { return name->FileStatus == Status::NotAsked; }))
    {
        return;
    }

    if (std::shared_ptr<MCPlayerNameObject> reporter = NameOf(playerId); reporter != nullptr)
    {
        reporter->FileStatus = haveFile ? Status::Present : Status::Missing;
    }

    bool allHave = true;

    for (const auto& nameObject : PlayerNames)
    {
        if (nameObject->FileStatus == Status::Waiting)
        {
            return;
        }

        if (nameObject->FileStatus == Status::Missing)
        {
            allHave = false;
        }
    }

    if (allHave)
    {
        // Everyone has it: the waiting dialog closes itself.
        MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
        GuiSystem().AddTimer(dialog.weak_from_this(), 0, 1000, 0, 0, false);
        dialog.OkButton->Callback().SetExec(nullptr);
        CheckGoodToGo();
        return;
    }

    // Name the players missing the file.
    std::string text = LoadGameString(0xab, 0xfe);

    for (const auto& nameObject : PlayerNames)
    {
        if (nameObject->FileStatus == Status::Missing)
        {
            text += nameObject->PlayerName;
        }

        nameObject->FileStatus = Status::NotAsked;
    }

    ShowDialog(text, MPCancel, "bh_okay.tga", "bg_okay.tga", nullptr);
}

auto MCSessionScreen::LoadMission(std::string_view fileName) -> void
{
    MCPacketFile packetFile;
    MCFitIniFile iniFile;
    MCFile textFile;
    const std::string path =
        MultiPlayer()->IsHost == 0 ? GamePath(fileName, "", "") : GamePath(SavePath, fileName, ".mpk");

    // Reads st name (as the original's 0xfe-character buffer took it).
    auto readText = [&iniFile](std::string_view name, std::string& text) -> int32_t
    {
        MCFitResult<std::string> value = iniFile.Read<std::string>(name);

        if (!value.has_value())
        {
            return std::to_underlying(value.error());
        }

        if (value->size() >= 0xfe)
        {
            return BUFFER_TOO_SMALL;
        }

        text = std::move(*value);
        return 0;
    };

    std::string text;
    uint32_t value = 0;
    int32_t result = packetFile.Open(path);
    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't open .MPK file ");

    if (result == 0)
    {
        result = packetFile.SeekPacket(0);
    }

    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't find packet 0 in .MPK file ");

    if (result == 0)
    {
        result = iniFile.Open(packetFile, packetFile.GetPacketSize());
        Assert(result == 0, static_cast<uint32_t>(result), " Couldn't open packet 0 in .MPK file ");

        if (result == 0)
        {
            result = iniFile.SeekBlock("Multiplayer");
        }
    }

    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't find Multiplayer block in file ");

    if (result == 0)
    {
        result = readText("LongMissionName", text);
    }

    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't read mission name in Multiplayer file ");

    if (result == 0)
    {
        SetMissionName(text);
        result = readText("LongMapName", text);
    }

    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't read map name in Multiplayer file ");

    if (result == 0)
    {
        SetMapName(text);
        result = readText("MapFileName", text);
    }

    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't read map file name in Multiplayer file ");

    if (result == 0)
    {
        SetMap(text);

        if (iniFile.ReadIdULong("MissionTime", value) == 0)
        {
            MissionLabel = std::format("{}:{:02}", value / 60, value % 60);
        }

        result = iniFile.SeekBlock("ResourcePoints");
    }

    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't find ResourcePoints block in MPK file ");

    if (result == 0)
    {
        const bool team1Missing = iniFile.ReadIdULong("Team1Points", value) != 0;

        if (!team1Missing)
        {
            SetTeam1RP(static_cast<int32_t>(value));
        }

        const bool team2Missing = iniFile.ReadIdULong("Team2Points", value) != 0;

        if (!team2Missing)
        {
            SetTeam2RP(static_cast<int32_t>(value));
        }

        if (team1Missing || team2Missing)
        {
            // An older file: one figure for both teams.
            if (iniFile.ReadIdULong("numPoints", value) != 0)
            {
                value = 0;
            }

            if (team1Missing)
            {
                SetTeam1RP(static_cast<int32_t>(value));
            }

            if (team2Missing)
            {
                SetTeam2RP(static_cast<int32_t>(value));
            }
        }
    }

    if (MultiPlayer()->IsHost != 0)
    {
        LockControls(iniFile.SeekBlock("Lock") == 0);
    }

    iniFile.Close();

    // Packet 1: the mission description.
    result = packetFile.SeekPacket(1);
    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't find packet 1 in .MPK file ");

    if (result == 0)
    {
        result = textFile.Open(packetFile, packetFile.GetPacketSize());
        Assert(result == 0, static_cast<uint32_t>(result), " Couldn't open packet 1 in .MPK file ");

        if (result == 0)
        {
            MissionText->Clear();
            MissionText->FontIndex = 0;

            while (!textFile.Eof())
            {
                MissionText->Print(textFile.ReadLine(0xfe), 0x1f);
            }

            Assert(textFile.Eof(), 0, "Error reading MP mission description");

            if (MultiPlayer()->IsHost != 0)
            {
                // Ask everyone whether they have the file, and wait for the answers.
                MissionFile = path;
                MultiPlayer()->SendFileInquiry(MissionFile.data());
                // The server has it already.
                const uint32_t serverId = MultiPlayer()->SessionManager->ServerID;

                for (auto& nameObject : PlayerNames)
                {
                    if (nameObject->PlayerId != NoPlayer)
                    {
                        nameObject->FileStatus = MCPlayerNameObject::MCFileStatus::Waiting;
                    }

                    if (nameObject->PlayerId == serverId)
                    {
                        nameObject->FileStatus = MCPlayerNameObject::MCFileStatus::Present;
                    }
                }

                ShowDialog(LoadGameString(0xac, 0xfe), MPCancel, "bh_cancl.tga", "bg_cancl.tga", MPLoadWorked);
            }

            return;
        }
    }

    // The file couldn't be read.
    SetMissionName({});
    SetMapName({});
    SetMap({});
    MissionLabel.clear();
    SetTeam1RP(0);
    SetTeam2RP(0);
    MissionText->Clear();
    ShowDialog(MCFormatPrintf(LoadGameString(0xad, 0xfe).c_str(), std::string(fileName).c_str()), MPCancel,
               "bh_okay.tga", "bg_okay.tga", nullptr);
}

auto MCSessionScreen::CancelMission() -> void
{
    SetMissionName({});
    SetMapName({});
    SetMap({});
    MissionFile.clear();
    MissionLabel.clear();
    SetTeam1RP(0);
    SetTeam2RP(0);
    MissionText->Clear();

    for (auto& nameObject : PlayerNames)
    {
        nameObject->FileStatus = MCPlayerNameObject::MCFileStatus::NotAsked;
    }
}

auto MCSessionScreen::PublishMatchState() const -> void
{
    MPMatchState& match = MultiPlayer()->Match;
    match.Team1Players = Team1Players;
    match.Team2Players = Team2Players;
    match.Team1TechBase = Team1TechBase;
    match.Team2TechBase = Team2TechBase;
    // As the mech bay read the typed points before: the text's buffer.
    match.Team1Points = std::atoi(Team1RPText->Buffer.c_str());
    match.Team2Points = std::atoi(Team2RPText->Buffer.c_str());
}

auto MCSessionScreen::CheckGoodToGo() const -> void
{
    auto filled = [](const std::array<uint32_t, TeamSlots>& team)
    { return std::ranges::any_of(team, [](uint32_t id) { return id != NoPlayer; }); };

    const bool goodToGo = NumUnassigned == 0 && !MissionFile.empty() && !MultiPlayer()->PlayersOnHomeTeam()->empty() &&
                          !MultiPlayer()->PlayersOnEnemyTeam()->empty() && filled(Team1Players) && filled(Team2Players);
    StartButton->Disabled = !goodToGo;
}

auto MCSessionScreen::SetTeamTechBase(int8_t team, int8_t techBase) -> void
{
    if (techBase != 1 && techBase != -1)
    {
        return;
    }

    std::shared_ptr<MCLogToolButton> isButton;
    std::shared_ptr<MCLogToolButton> clanButton;

    if (team == 1)
    {
        Team1TechBase = techBase;
        isButton = Team1ISButton;
        clanButton = Team1ClanButton;
    }
    else if (team == 2)
    {
        Team2TechBase = techBase;
        isButton = Team2ISButton;
        clanButton = Team2ClanButton;
    }
    else
    {
        return;
    }

    // The chosen toggle on, the other off.
    isButton->Toggled = techBase == 1;
    clanButton->Toggled = techBase == -1;
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

    for (const std::shared_ptr<MCLogSpinnerButton>& spinner : {Team1RPUp, Team1RPDown, Team2RPUp, Team2RPDown})
    {
        spinner->Disabled = !live;
    }

    for (const std::shared_ptr<MCLogToolButton>& techButton :
         {Team1ISButton, Team1ClanButton, Team2ISButton, Team2ClanButton})
    {
        techButton->SetEventRoutine(live ? std::function<void(MCGuiObject&, MCGuiEvent&)>(TechTabRoutine) : nullptr);
    }
}

auto MCSessionScreen::ControlsOn() -> void
{
    SetTeamControls(true);

    for (auto& nameObject : PlayerNames)
    {
        nameObject->Draggable = true;
    }

    LoadMissionButton->Disabled = false;
}

auto MCSessionScreen::ControlsOff() -> void
{
    SetTeamControls(false);

    for (auto& nameObject : PlayerNames)
    {
        nameObject->Draggable = false;
    }

    LoadMissionButton->Disabled = true;
}

auto MCSessionScreen::LockControls(bool lock) -> void
{
    SetTeamControls(!lock);

    if (lock)
    {
        // A locked mission: fixed points and tech bases (team 1 Inner Sphere, team 2 Clan).
        SetTeamTechBase(1, 1);
        SetTeamTechBase(2, -1);
        SendTwoLongs(GuaranteedHeader(MCMPMessageType::TechbaseChange), 1, 1);
        SendTwoLongs(GuaranteedHeader(MCMPMessageType::TechbaseChange), 2, -1);
    }
}

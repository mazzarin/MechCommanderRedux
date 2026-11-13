#include "stdafx.h"
#include "main/MCLogistics.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCScrollPane.h"
#include "gui/MCUpdateDisplay.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "ai/MCMoveGeometry.h"
#include "logistics/MCBriefingBox.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGameList.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogChatWindow.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogScrollTextObject.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCLogChatInput.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCPilotPurchaseBlock.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCPurchaseDlg.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCTicker.h"
#include "logistics/MCUpdatePage.h"
#include "logistics/MCTickerLine.h"
#include "main/MCMPPlayerLightsView.h"
#include "logistics/MCUnitLimits.h"
#include "linkup/MCSessionManager.h"
#include "main/MCLogisticsShared.h"
#include "main/MCLogMech.h"
#include "main/MCLogMechList.h"
#include "main/MCLogVehicle.h"
#include "main/MCLogVehicleList.h"
#include "main/MCGameSession.h"
#include "mission/MCScenario.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCObjectTypeManager.h"
#include "main/MCGamePaths.h"
#include "main/MCMissionGlobals.h"
#include "main/MCGameStrings.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"
#include "main/MCGameContext.h"
#include "main/MCTelemetry.h"
#include "main/MCUpdater.h"
#include "platform/MCDisplay.h"
#include "platform/MCFrameLog.h"
#include "platform/MCInput.h"
#include "platform/MCRegistry.h"
#include "platform/MCRenderer.h"
#include "platform/MCSharpPictures.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCHdArt.h"
#include "vfx/MCVfxFunctions.h"

std::type_identity_t<std::array<bool, 7>> LogCheatActive{};
std::array<int32_t, 6> MultiPlayerColors{};
std::type_identity_t<int32_t> LogCurCheatChar{};
std::type_identity_t<int> InDemo{};
bool Solo = false;
int32_t LastLogisticsMissionState = 0;
std::string HoldString;

namespace
{
    /// <summary>The logistics screens while they exist (<see cref="GlobalLog"/>).</summary>
    std::weak_ptr<MCLogistics> GlobalLogistics;

    /// <summary>Port-only: the frame log's note of the logistics screen <paramref name="name"/> set up.</summary>
    void NoteScreen(std::string_view name)
    {
        MCFrameLog::Note(std::format("logistics: the {} screen", name));
    }

    /// <summary>Port (Redux HD): the room's chat line telling an internet game's host the code its players join by.</summary>
    constexpr std::string_view InternetCodeFormat = "FRIENDS CAN JOIN WITH CODE {}.";
}

auto GlobalLog() -> std::shared_ptr<MCLogistics>
{
    return GlobalLogistics.lock();
}

auto SetGlobalLog(std::weak_ptr<MCLogistics> logistics) -> void
{
    GlobalLogistics = std::move(logistics);
}

MCLogistics::MCLogistics()
    : WarriorList(std::make_unique<MCLogWarriorList>())
    , AssignedWarriorList(std::make_unique<MCLogWarriorList>())
    , MechList(std::make_unique<MCLogMechList>())
    , VehicleList(std::make_unique<MCLogVehicleList>())
    , ForceMechList(std::make_unique<MCLogMechList>())
    , ForceVehicleList(std::make_unique<MCLogVehicleList>())
    , PurMechList(std::make_unique<MCPurMechList>())
    , PurVehicleList(std::make_unique<MCPurVehicleList>())
    , PurPilotList(std::make_unique<MCPurPilotList>())
    , ComponentInventory(std::make_unique<MCInventoryList>())
    , PurchaseComponents(std::make_unique<MCInventoryList>())
{
    // Port fix: the shop's lists were made only by a campaign's load, so a screen shown without one (the main menu's
    // RETURN TO GAME after a refused save) read a null list.
}

namespace
{
    /// <summary>Screen element <paramref name="index"/> of <paramref name="screen"/> as a <typeparamref name="T"/>.</summary>
    template <typename T> std::shared_ptr<T> ScreenElement(MCGenericScreen& screen, int32_t index)
    {
        return screen.Element<T>(index);
    }

    /// <summary>A new <paramref name="width"/> x <paramref name="height"/> port with its own bitmap.</summary>
    std::shared_ptr<MCLogPort> NewPort(int32_t width, int32_t height)
    {
        auto port = std::make_shared<MCLogPort>();
        port->Init(width, height);
        return port;
    }

    /// <summary>A new port loaded from the art file <paramref name="name"/> under <c>ArtPath</c>.</summary>
    std::shared_ptr<MCLogPort> NewArtPort(std::string_view name)
    {
        auto port = std::make_shared<MCLogPort>();
        port->Load(ArtPath + std::string(name));
        return port;
    }

    /// <summary>Loads the whole of an opened shape file into a block registered as shapes (the repair and icon shapes).</summary>
    MCRegisteredBlock ReadShapeFile(MCFile& file, std::string_view sizeError)
    {
        const uint32_t length = file.GetLength();
        MCRegisteredBlock shapes(length, MCDataKind::Shapes);
        const int32_t read = file.Read(shapes.Bytes());
        Assert(static_cast<uint32_t>(read) == length, 0, sizeError);
        file.Close();
        return shapes;
    }

    /// <summary>Opens shape file <paramref name="fileName"/> (asserting it exists) and loads it (<see cref="ReadShapeFile"/>).</summary>
    MCRegisteredBlock LoadShapeFile(MCFile& file, std::string_view fileName, std::string_view openError,
                                    std::string_view sizeError)
    {
        const int32_t result = file.Open(fileName);
        Assert(result == 0, static_cast<uint32_t>(result), openError);
        return ReadShapeFile(file, sizeError);
    }

    /// <summary>
    /// Makes <paramref name="screen"/> on the screen window, with its elements from screen ini
    /// <paramref name="name"/><c>.fit</c> under <c>ArtPath</c>; <paramref name="then"/> runs after, the file still
    /// open.
    /// </summary>
    void MakeScreen(MCGuiHandle<MCSplashScreen>& screen, std::string_view name, std::string_view missingError,
                    std::string_view startError, const std::function<void(MCFitIniFile& file)>& then = nullptr)
    {
        MCFitIniFile file;
        const int32_t opened = file.Open(GamePath(ArtPath, name, ".fit"));
        Assert(opened == 0, static_cast<uint32_t>(opened), missingError);
        screen = ScreenWindow()->CreateChild<MCSplashScreen>(
            [&](MCSplashScreen& made)
            {
                screen = made.WeakAs<MCSplashScreen>();
                const int32_t result = made.Init(file);
                Assert(result == 0, static_cast<uint32_t>(result), startError);
            });

        if (then)
        {
            then(file);
        }
    }

    /// <summary>Sets up a multiplayer screen's text field: the white font and the background.</summary>
    std::shared_ptr<MCLogTextObject> SetUpTextField(MCGenericScreen& screen, int32_t index)
    {
        auto field = ScreenElement<MCLogTextObject>(screen, index);
        field->Font = MedWhiteFont;
        field->SetBackColor(0x10);
        return field;
    }
}

auto MCLogistics::Start() -> void
{
    if (EmptyFile.empty())
    {
        EmptyFile = LoadGameString(0x381, 0xfe);
    }

    _Started = true;
    SetGlobalLog(weak_from_this());
    AutoPlayMovie = false;
    SetCurrentScreen({});
    DragIcon = nullptr;
    CurrentInvTab = 0;
    CurrentMission = -1;
    NextWarriorID = 1;
    ResourcePoints = -9999;
    PlayerLights = nullptr;
    HammerDown = false;
    WindowTitle = AppName + " -- Logistics";

    // Port: SetWindowTextA -> the SDL window's title.
    if (const std::shared_ptr<MCDisplay> display = MCInput::Display())
    {
        display->SetTitle(WindowTitle.c_str());
    }

    LogisticsState = 0;

    for (size_t lance = 0; lance < NumLances; ++lance)
    {
        for (size_t slot = 0; slot < LanceSlots; ++slot)
        {
            DeploySlots[lance][slot] = {-1, -1};
            DeploySlotPlacements[lance][slot] = {};
        }
    }

    LocalDropSlot.fill(false);
    PlayerColors = {1, 3, 4, 2, 6, 5};
    MultiplayerInitialized = false;
    DefaultPlanningTime = 0xf0;
    PlanningTime = 0xf0;
    InventoryIconPorts[0] = NewArtPort("logart\\lsciim.tga");
    InventoryIconPorts[1] = NewArtPort("logart\\lsciip.tga");
    InventoryIconPorts[2] = NewArtPort("logart\\lsciic.tga");
    InventoryIconPorts[3] = NewArtPort("logart\\lsciiv.tga");

    if (InDemo == 0)
    {
        // The chat; its windows are made with the screens that show it.
        Chat = std::make_shared<MCLogChat>(100000);
        ChatShown = false;
    }

    // The screens are the screen window's children for good, hidden until shown; each is made where the original
    // initialised it (and so added it to the screen), so they keep the original's order there.
    // (Each handle is stored before the screen's Init, as the original stored the pointer first.)
    ScreenWindow()->CreateChild<MCRepairScreen>(
        [this](MCRepairScreen& screen)
        {
            RepairScreen = screen.WeakAs<MCRepairScreen>();
            screen.Init();
        });
    ScreenWindow()->CreateChild<MCBriefingScreen>(
        [this](MCBriefingScreen& screen)
        {
            BriefingScreen = screen.WeakAs<MCBriefingScreen>();
            screen.Init();
        });
    ScreenWindow()->CreateChild<MCPurchaseScreen>(
        [this](MCPurchaseScreen& screen)
        {
            PurchaseScreen = screen.WeakAs<MCPurchaseScreen>();
            screen.Init();
        });
    LogisticsState = 1;
    MakeScreen(MainScreen, "mainScreen", " No Splash Screen FIT File ", " Unable to start splash screen ");
    SetCurrentScreen(MainScreen);

    // Port (personal): a build that updates itself has UPDATE in ORIGINAL CINEMA's place, and looks for a newer
    // version once a run (main/MCUpdater.h).
    if (MCUpdater::Available())
    {
        AddUpdatePage(*MainScreen);
    }

    // Port (Redux HD): the version running, under the menu's window.
    AddVersionLine(*MainScreen);
    MCUpdater::CheckAtStart();
    // Port (personal): the reports start once a run (main/MCTelemetry.h); the main menu shows their notice or
    // question when due (MCMission::Run).
    MCTelemetry::Start();

    if (InDemo == 0)
    {
        const std::optional<std::string> userName = MyGetUserName();
        MakeScreen(MultiplayerScreen, "mpscreen", " No Connection Screen FIT File ",
                   " Unable to start connect screen ");
        // Port (Redux HD): MULTIPLAYER ACCESS's LAN and INTERNET, and the INTERNET screen in the LAN screen's layout.
        SetUpAccessScreen(*MultiplayerScreen);
        MakeScreen(InternetScreen, "lanscreen", " No LAN Screen FIT File ", " Unable to start internet screen ");
        SetUpInternetScreen(*InternetScreen);

        {
            MakeScreen(LanScreen, "lanscreen", " No LAN Screen FIT File ", " Unable to start lanScreen screen ");
            MCSplashScreen& screen = *LanScreen;
            screen.SetEventRoutine(LanScreenHandleEvent);
            auto players = ScreenElement<MCLogScrollTextObject>(screen, 3);
            players->SetEventRoutine(PlayerListHandleEvent);
            players->FontIndex = 1;
            ScreenElement<MCGameList>(screen, 2)->FontIndex = 1;
            const std::shared_ptr<MCLogTextObject> nameField = SetUpTextField(screen, 4);
            const std::shared_ptr<MCLogTextObject> gameField = SetUpTextField(screen, 10);
            const std::shared_ptr<MCLogTextObject> playersField = SetUpTextField(screen, 11);
            nameField->InitBuffer(0x10, MCLogInputType::Text);
            gameField->InitBuffer(0x18, MCLogInputType::Text);
            nameField->SetStringBuffer(userName.value_or("Player"));

            // The game's name: "<player>'s game" from the string table, or "Game" without a user name.
            if (userName.has_value())
            {
                gameField->InitBuffer(0x18, MCLogInputType::Text);
                gameField->SetStringBuffer(MCFormatPrintf(LoadGameString(0x377, 0xfe).c_str(), userName->c_str()));
            }
            else
            {
                gameField->SetStringBuffer("Game");
            }

            playersField->InitBuffer(2, MCLogInputType::Port);
            playersField->SetStringBuffer("6");
            ScreenElement<MCLogButton>(screen, 6)->Disabled = true;
            screen.ShowBlock(0);
        }

        {
            MakeScreen(ModemScreen, "modem", " No modem Screen FIT File ", " Unable to start modemScreen screen ");
            MCSplashScreen& screen = *ModemScreen;
            screen.SetEventRoutine(ModemScreenHandleEvent);
            const std::shared_ptr<MCLogTextObject> nameField = SetUpTextField(screen, 4);
            const std::shared_ptr<MCLogTextObject> phoneField = SetUpTextField(screen, 5);
            nameField->InitBuffer(0x10, MCLogInputType::Text);
            phoneField->InitBuffer(0x18, MCLogInputType::Text);
            nameField->SetStringBuffer(userName.value_or("Player"));
            auto modems = ScreenElement<MCLogScrollTextObject>(screen, 10);
            modems->FontIndex = 1;
            modems->HighlightColor[0] = 0x14;
            modems->HighlightLine[0] = 0;
            modems->SetEventRoutine(ModemListHandleEvent);
            screen.ShowBlock(0);
        }

        {
            // The serial screen reuses the modem screen's messages.
            MakeScreen(SerialScreen, "serial", " No modem Screen FIT File ", " Unable to start modemScreen screen ");
            MCSplashScreen& screen = *SerialScreen;
            const std::shared_ptr<MCLogTextObject> nameField = SetUpTextField(screen, 4);
            nameField->InitBuffer(0x10, MCLogInputType::Text);
            nameField->SetStringBuffer(userName.value_or("Player"));
            const std::shared_ptr<MCLogTextObject> portField = SetUpTextField(screen, 5);
            portField->InitBuffer(2, MCLogInputType::Digits);
            portField->SetEventRoutine(ComPortTextHandleEvent);
            portField->SetStringBuffer("1");
            screen.SetEventRoutine(SerialScreenHandleEvent);
        }

        {
            MakeScreen(ConnectScreen, "readyroom", " No Ready Room Screen FIT File ",
                       " Unable to start readyRoomScreen screen ");
            MCSplashScreen& screen = *ConnectScreen;
            auto players = ScreenElement<MCLogScrollTextObject>(screen, 3);
            players->SetEventRoutine(ReadyRoomPlayerListHandleEvent);
            players->FontIndex = 1;
            ScreenElement<MCLogButton>(screen, 2)->Disabled = true;
        }
    }

    MakeScreen(LoadScreen, "loadScreen", " No Load Screen FIT File ", " Unable to start load screen ");
    LoadScreen->SetEventRoutine(LoadSaveScreenHandleEvent);
    MakeScreen(SaveScreen, "saveScreen", " No Save Screen FIT File ", " Unable to start save screen ");
    SaveScreen->SetEventRoutine(LoadSaveScreenHandleEvent);

    if (InDemo == 0)
    {
        // The preferences screen reuses the save screen's messages.
        MakeScreen(PrefScreen, "prefScreen", " No Save Screen FIT File ", " Unable to start save screen ",
                   [this](MCFitIniFile& file)
                   {
                       PrefScreen->SetEventRoutine(PrefScreenHandleEvent);
                       // Port: the difficulty and renderer choices as drop-downs.
                       AddPreferencesPanel(*PrefScreen, file);
                   });
        ScreenWindow()->CreateChild<MCSessionScreen>(
            [this](MCSessionScreen& screen)
            {
                SessionScreen = screen.WeakAs<MCSessionScreen>();
                screen.Init(0, 0, 0x280, 0x1e0, {});
            });
    }

    ShowLogScreen(false, false);

    // Under the process ID, as aSystem::init sets it: copies of the game on one machine share the user folder.
    SaveTempPath = std::format("{}temp\\{}\\", SavePath, MCPort::ProcessId());
    // Port fix (OB-094): the original allocated a File here, and a FitIniFile after the sort tables, and never used or
    // freed either.

    char line[256];
    MCFile file;
    int32_t result = file.Open(ArtPath + "logart\\comp.rsp");
    Assert(result == 0, 0, " could not open componant name file ");
    // The table has one entry more than the file has lines; every entry is read from a line (the last from the end
    // of the file).
    int32_t numRangeSorted = 1;

    while (true)
    {
        file.ReadLine(line, 0x28);

        if (file.Eof())
        {
            break;
        }

        ++numRangeSorted;
    }

    RangeSortList.assign(static_cast<size_t>(numRangeSorted), 0);
    file.Seek(0, 0);

    for (uint32_t& entry : RangeSortList)
    {
        file.ReadLine(line, 0x28);
        entry = static_cast<uint32_t>(std::atol(line));
    }

    file.Close();
    result = file.Open(GamePath(ObjectPath, "objsort.rsp"));
    Assert(result == 0, 0, " could not open object sort file ");

    for (int32_t& sortOrder : ComponentSort)
    {
        file.ReadLine(line, 0xfe);
        sortOrder = static_cast<int32_t>(std::atol(line));
    }

    file.Close();
    InvBlockPort = NewArtPort("logart\\invblock.tga");

    // The ticker, and a line for it on each screen that shows it; it starts on the screen shown now.
    Ticker = std::make_shared<MCTicker>(0xcd, MedWhiteFont);
    {
        const std::shared_ptr<MCLogPort> tickerBack = NewPort(0xcd, MedWhiteFont->Height());
        VfxPaneWipe(tickerBack->Frame(), 0xed);
        TickerLines.clear();

        const std::array<std::shared_ptr<MCLogObject>, 5> screens = {
            CurrentScreen(), PurchaseScreen.Lock(), BriefingScreen.Lock(), RepairScreen.Lock(), SessionScreen.Lock()};

        for (const std::shared_ptr<MCLogObject>& screen : screens)
        {
            if (screen == nullptr)
            {
                continue;
            }

            TickerLines.push_back(screen->CreateChild<MCTickerLine>(
                [&](MCTickerLine& line)
                {
                    line.Init(3, 3, 0xcd, 1, Ticker);
                    line.SetBackPane(*tickerBack);
                }));
        }
    }

    // The first line is the screen's shown now; it runs the scroll timer from the start, as the original's ticker did.
    TickerLines.front()->StartScrolling();
    ShowTickerOn(*CurrentScreen(), true);

    // The chat's windows, hidden until the chat is put on their screen.
    ChatWindows.clear();
    ChatWindow.reset();

    if (Chat != nullptr)
    {
        const std::array<std::shared_ptr<MCLogObject>, 2> chatScreens = {SessionScreen.Lock(), BriefingScreen.Lock()};

        for (const std::shared_ptr<MCLogObject>& screen : chatScreens)
        {
            ChatWindows.push_back(screen->CreateChild<MCLogChatWindow>(
                [&](MCLogChatWindow& window)
                {
                    window.Init(7, 0x44, 0xbf, 0x101, Chat);
                    window.ShowGuiWindow(false);
                }));
        }
    }

    ScreenWindow()->CreateChild<MCPurchaseDlg>(
        [this](MCPurchaseDlg& dialog)
        {
            PurchaseDialog = dialog.WeakAs<MCPurchaseDlg>();
            dialog.MCLogDialogBox::Init(0xe5, 0xa2, 0xb5, 0x9c);
        });
    const auto makeDialog = [](auto& handle)
    {
        using Dialog = std::remove_cvref_t<decltype(*handle.Lock())>;
        ScreenWindow()->CreateChild<Dialog>(
            [&handle](Dialog& dialog)
            {
                handle = dialog.template WeakAs<Dialog>();
                dialog.Init(0, 0, 4, 4, {});
            });
    };

    makeDialog(MessageDialog);
    makeDialog(QuestionDialog);
    makeDialog(RefitDialog);

    ResourceBackPort = NewPort(0x3d, 0xc);
    VfxPaneWipe(ResourceBackPort->Frame(), 0x10);
    ClockBackPort = NewPort(0x32, 0xc);
    VfxPaneWipe(ClockBackPort->Frame(), 0x10);
    RepairBackPort = NewArtPort("logart\\lsrupm00.tga");

    for (size_t i = 0; i < MechRepShapes.size(); ++i)
    {
        MechRepShapes[i] = LoadShapeFile(file, std::format("{}mechrep{:02}.shp", ArtPath, i),
                                         "could not open mechrep shape file", "unexpected mechrep size");
        MechIconShapes[i] = LoadShapeFile(file, std::format("{}mi{:02}.shp", ArtPath, i),
                                          "could not open mechicon shape file", "unexpected mechicon size");
    }

    // A vehicle without a repair picture has no icon either.
    for (size_t i = 0; i < VehicleRepShapes.size(); ++i)
    {
        if (file.Open(std::format("{}vr1{:02}.shp", ArtPath, i)) != 0)
        {
            continue;
        }

        VehicleRepShapes[i] = ReadShapeFile(file, "unexpected vhclrep size");

        if (file.Open(std::format("{}vi1{:02}.shp", ArtPath, i)) == 0)
        {
            VehicleIconShapes[i] = ReadShapeFile(file, "unexpected vhclrep size");
        }
    }

    // Ten remap tables: each maps every colour to 0xff (transparent) except one, which it recolours.
    static constexpr std::array<std::pair<uint8_t, uint8_t>, 10> lookasideColors = {{{0xe8, 0xe8},
                                                                                     {0xe8, 0xf2},
                                                                                     {0xe8, 0xeb},
                                                                                     {0xe8, 0xef},
                                                                                     {0xe8, 0x13},
                                                                                     {0xe6, 0xe6},
                                                                                     {0xe6, 0xf1},
                                                                                     {0xe6, 0xf4},
                                                                                     {0xe6, 0xed},
                                                                                     {0xe6, 0x13}}};

    for (size_t i = 0; i < ShapeLookaside.size(); ++i)
    {
        ShapeLookaside[i].fill(0xff);
        ShapeLookaside[i][lookasideColors[i].first] = lookasideColors[i].second;
    }

    MCRenderer::RegisterData(std::span(ShapeLookaside[0].data(), sizeof(ShapeLookaside)), MCDataKind::Tables);
    AttachHdArt();

    RepairPorts[0] = NewArtPort("logart\\lsrupm03.tga");
    RepairPorts[1] = NewArtPort("logart\\lsrupm01.tga");
    RepairPorts[2] = NewArtPort("logart\\lsrupm04.tga");
    RepairPorts[3] = NewArtPort("logart\\lsrupm02.tga");
    RepairPorts[4] = NewArtPort("logart\\lsrupm06.tga");
    RepairPorts[5] = NewArtPort("logart\\lsrupm07.tga");
    PurchasePorts[0] = NewArtPort("logart\\lspcb05.tga");
    PurchasePorts[1] = NewArtPort("logart\\lspcb07.tga");
    PurchasePorts[2] = NewArtPort("logart\\lspcb06.tga");
    PurchasePorts[3] = NewArtPort("logart\\lspcb09.tga");
    static constexpr std::array<std::string_view, 5> buttonNames = {
        "logart\\lscb{}00.tga", "b{}_exit.tga", "logart\\lscb{}01.tga", "logart\\lscb{}02.tga", "logart\\lscb{}03.tga"};
    static constexpr std::array<char, 3> buttonStates = {'n', 'h', 'g'};

    for (size_t button = 0; button < ScreenButtonPorts.size(); ++button)
    {
        for (size_t state = 0; state < buttonStates.size(); ++state)
        {
            ScreenButtonPorts[button][state] =
                NewArtPort(std::vformat(buttonNames[button], std::make_format_args(buttonStates[state])));
        }
    }

    {
        MCFitIniFile gameSystemFile;
        result = gameSystemFile.Open(GamePath(MissionPath, "gamesys.fit"));
        Assert(result == 0, static_cast<uint32_t>(result), " Couldn't open gamesys.fit");
        result = gameSystemFile.SeekBlock("Warrior");
        Assert(result == 0, static_cast<uint32_t>(result), " Couldn't find Warrior block ");
        MaxPilotSkill = ReadRequired<float>(gameSystemFile, "SkillMax",
                                            " Couldn't find SkillMax variable in Warrior block of gamesys.fit ");
        MinPilotSkill = ReadRequired<float>(gameSystemFile, "SkillMin",
                                            " Couldn't find SkillMin variable in Warrior block of gamesys.fit ");
        static_cast<void>(gameSystemFile.ReadArray("SkillWeightings", std::span<float>(SkillWeightings)));
        Assert(gameSystemFile.ReadArray("WarriorRankScale", std::span<float>(WarriorRankScale)).has_value(), 0,
               " Couldn't find WarriorRankScale variable in Warrior block of gamesys.fit ");
        result = gameSystemFile.SeekBlock("MultiPlayerColors");
        Assert(result == 0, static_cast<uint32_t>(result), " Could not find Multiplayer Colors block ");
        Assert(gameSystemFile.ReadArray("mPlayerColors", std::span<int32_t>(MultiPlayerColors)).has_value(), 0,
               " Could not find multiplayer colors data ");
    }

    ReadyRoomTicks = 0;

    if (LaunchedFromLobby == 0 || MultiPlayer() == nullptr || Turn != 0)
    {
        LogisticsState = 1;
        SetCurrentScreen(MainScreen);
    }
    else
    {
        // Started from a lobby: straight to the ready room, whose cancel button quits the game. Port-only (the personal
        // build): it shows while the lobby's join goes on; joined (or hosting), the session screen opens (MCMission).
        SetCurrentScreen(ConnectScreen);
        LogisticsState = 0xe;
        ScreenElement<MCLogButton>(*ConnectScreen, 1)->Callback().SetExec([] { KillTheGame(); });
    }

    ShowLogScreen(false, false);
    MakeAhead();
}

namespace
{
    /// <summary>The Atlas's repair shapes (mechrep00.shp) and its head's armor frame, 21 pixels wide.</summary>
    constexpr size_t AtlasMech = 0;
    constexpr int32_t AtlasHead = MCLogistics::MechArmorShapes;
    constexpr float AtlasHeadWidth = 21.0f;

    /// <summary>
    /// Port-only: the Atlas's head is a skull, but its eyes are boxes of pixels (each with a tick at its outer corner)
    /// that line art draws as tilted boxes: drawn by hand as sockets under the brow, tall at the outside and narrowing
    /// toward the nose, the right one the left one mirrored.
    /// </summary>
    std::vector<MCSharpLines::MCDrawnLoop> AtlasEyes()
    {
        const std::vector<std::array<float, 2>> left = {{4.3f, 5.4f}, {6.4f, 5.6f}, {8.3f, 6.3f}, {8.6f, 7.2f},
                                                        {7.9f, 8.2f}, {6.0f, 8.6f}, {4.5f, 8.2f}, {4.0f, 6.8f}};
        std::vector<std::array<float, 2>> right;

        for (const auto& [x, y] : left)
        {
            right.push_back({AtlasHeadWidth - x, y});
        }

        return {{3, 5, 8, 8, left}, {12, 5, 17, 8, right}};
    }
}

auto MCLogistics::AttachHdArt() -> void
{
    // A new owner: ties made before go with the old one.
    HdArtOwner = std::make_shared<const int>(0);
    std::vector<MCHdArt::LineArtFrame> lineArt;

    // Each unit's icons tied to the pack's (named as their files are), and its repair shapes' diagram (runs of frames
    // from each first) and sold-out outline as line art.
    const auto attach = [&](std::span<const MCRegisteredBlock> repairs, std::span<const MCRegisteredBlock> icons,
                            std::string_view prefix, std::initializer_list<int32_t> diagram, int32_t locations,
                            int32_t soldOut)
    {
        for (size_t i = 0; i < repairs.size(); ++i)
        {
            if (!icons[i].Empty())
            {
                MCHdArt::Attach(icons[i].Bytes(), HdArtOwner, MCHdArt::IconShapeKey{std::format("{}{:02}", prefix, i)});
            }

            if (repairs[i].Empty())
            {
                continue;
            }

            const MCAddressKey table = MCAddressKey::OfData(repairs[i].Bytes());

            for (const int32_t first : diagram)
            {
                for (int32_t location = 0; location < locations; ++location)
                {
                    lineArt.push_back({table, first + location});
                }
            }

            lineArt.push_back({table, soldOut});
        }
    };

    attach(MechRepShapes, MechIconShapes, "mi", {MechArmorShapes, MechInternalShapes}, MechDiagramLocations,
           MechSoldOutShape);
    attach(VehicleRepShapes, VehicleIconShapes, "vi1", {VehicleArmorShapes}, VehicleDiagramLocations,
           VehicleSoldOutShape);

    // The Atlas's skull: its eyes drawn by hand.
    if (AtlasMech < MechRepShapes.size() && !MechRepShapes[AtlasMech].Empty())
    {
        const MCAddressKey atlas = MCAddressKey::OfData(MechRepShapes[AtlasMech].Bytes());

        for (MCHdArt::LineArtFrame& frame : lineArt)
        {
            if (frame.Table == atlas && frame.Frame == AtlasHead)
            {
                frame.Drawn = AtlasEyes();
            }
        }
    }

    MCHdArt::SetLineArtFrames(lineArt);
}

namespace
{
    /// <summary>Port-only: whether the screen is drawn sharp (HD art, the GPU drawing): only then are its pictures.</summary>
    bool DrawsSharp()
    {
        const std::shared_ptr<MCDisplay> display = MCInput::Display();
        return display != nullptr && display->SharpScreen();
    }
}

auto MCLogistics::MakeAhead() const -> void
{
    if (!DrawsSharp())
    {
        return;
    }

    // Each font once (the drop fonts serve two sizes).
    std::set<const MCGuiFont*> fonts;

    for (const std::array<std::shared_ptr<MCGuiFont>, 3>& sizes : Fonts)
    {
        for (const std::shared_ptr<MCGuiFont>& font : sizes)
        {
            if (font != nullptr && !font->FontData.Empty() && fonts.insert(font.get()).second)
            {
                MCSharpPictures::MakeFont(font->FontData.Bytes());
            }
        }
    }

    // The HD copies of the pictures the screens are made of (their art, loaded by Start, the screens' backgrounds
    // with it: MCLogInvScreen::InitLive), read ahead by the pack's reading thread.
    std::vector<MCHdArt::PictureAhead> pictures;

    for (const std::shared_ptr<MCTexture>& texture : MCRenderer::Textures())
    {
        if (texture != nullptr && !texture->CpuStale && !texture->Pixels.empty())
        {
            pictures.push_back({texture->Pixels, texture->Width, texture->Height});
        }
    }

    MCHdArt::ReadPicturesAhead(pictures);
}

auto MCLogistics::MakeLineArtAhead() const -> void
{
    if (!DrawsSharp())
    {
        return;
    }

    // The units' repair shapes, the shop's first (the purchase screen's mechs show first), each once.
    std::vector<std::span<const uint8_t>> tables;
    const auto add = [&tables](std::span<const MCRegisteredBlock> shapes, int32_t index)
    {
        if (index >= 0 && static_cast<size_t>(index) < shapes.size() && !shapes[static_cast<size_t>(index)].Empty())
        {
            const std::span<const uint8_t> table = shapes[static_cast<size_t>(index)].Bytes();

            if (std::ranges::none_of(tables,
                                     [&](std::span<const uint8_t> kept) { return kept.data() == table.data(); }))
            {
                tables.push_back(table);
            }
        }
    };

    if (PurMechList != nullptr)
    {
        for (const std::shared_ptr<MCPurMech>& mech : PurMechList->Mechs)
        {
            for (const std::unique_ptr<MCPurMechData>& variant : mech->Variants)
            {
                if (variant != nullptr)
                {
                    add(MechRepShapes, variant->NameIndex);
                }
            }
        }
    }

    if (MechList != nullptr)
    {
        for (const std::shared_ptr<MCLogMech>& mech : MechList->Mechs)
        {
            add(MechRepShapes, mech->NameIndex);
        }
    }

    if (PurVehicleList != nullptr)
    {
        for (const std::shared_ptr<MCPurVehicle>& vehicle : PurVehicleList->Vehicles)
        {
            if (vehicle->Data != nullptr)
            {
                add(VehicleRepShapes, vehicle->Data->NameIndex);
            }
        }
    }

    if (VehicleList != nullptr)
    {
        for (const std::shared_ptr<MCLogVehicle>& vehicle : VehicleList->Vehicles)
        {
            add(VehicleRepShapes, vehicle->NameIndex);
        }
    }

    // Each one's frames marked as line art (AttachHdArt).
    for (const std::span<const uint8_t> table : tables)
    {
        for (int32_t frame = 0; frame < VfxShapeCount(table); ++frame)
        {
            if (MCHdArt::LineArt(table, frame))
            {
                MCSharpPictures::MakeLineArt(table, frame, MCHdArt::LineArtDrawn(table, frame));
            }
        }
    }
}

MCLogistics::~MCLogistics()
{
    if (!_Started)
    {
        return;
    }

    // Port-only: the line art made ahead for this logistics' lists goes with them.
    MCSharpPictures::ForgetLineArt();
    MCHdArt::SetLineArtFrames({});
    HdArtOwner.reset();

    MCRenderer::UnregisterData(std::span(ShapeLookaside[0].data(), sizeof(ShapeLookaside)));
    ResetPlayerLights();
    Ticker.reset();
    ChatWindow.reset();
    ChatWindows.clear();
    Chat.reset();

    if (MultiplayerInitialized)
    {
        DestroyMultiplayer();
    }

    CampaignBriefingName.clear();
    OperationCinema.clear();
    RepairBackPort.reset();

    for (std::shared_ptr<MCLogPort>& port : RepairPorts)
    {
        port.reset();
    }

    for (std::shared_ptr<MCLogPort>& port : PurchasePorts)
    {
        port.reset();
    }

    for (auto& ports : ScreenButtonPorts)
    {
        for (std::shared_ptr<MCLogPort>& port : ports)
        {
            port.reset();
        }
    }

    for (std::shared_ptr<MCLogPort>& port : InventoryIconPorts)
    {
        port.reset();
    }

    for (size_t i = 0; i < MechRepShapes.size(); ++i)
    {
        MechRepShapes[i] = {};
        MechIconShapes[i] = {};
    }

    for (size_t i = 0; i < VehicleRepShapes.size(); ++i)
    {
        VehicleRepShapes[i] = {};
        VehicleIconShapes[i] = {};
    }

    ResourceBackPort.reset();
    ClockBackPort.reset();

    CloseGuiObject(PurchaseDialog);
    PurchaseDialog.reset();
    CloseGuiObject(MessageDialog);
    MessageDialog.reset();
    CloseGuiObject(QuestionDialog);
    QuestionDialog.reset();
    CloseGuiObject(RefitDialog);
    RefitDialog.reset();
    InvBlockPort.reset();
    SetCurrentScreen({});
    VehicleList.reset();
    // The force lists last until the object goes (the original left them for the mission in multiplayer).
    ForceVehicleList->Clear();
    PurMechList.reset();
    PurVehicleList.reset();
    PurchaseComponents.reset();
    PurPilotList.reset();
    ComponentInventory.reset();
    RangeSortList.clear();
    WarriorList.reset();
    MechList.reset();
    AssignedWarriorList.reset();
    ForceMechList->Clear();
    CloseGuiObject(BriefingScreen);
    BriefingScreen.reset();
    CloseGuiObject(PurchaseScreen);
    PurchaseScreen.reset();
    CloseGuiObject(RepairScreen);
    RepairScreen.reset();
    CloseGuiObject(MainScreen);
    MainScreen.reset();
    CloseGuiObject(MultiplayerScreen);
    MultiplayerScreen.reset();
    CloseGuiObject(LanScreen);
    LanScreen.reset();
    CloseGuiObject(ModemScreen);
    ModemScreen.reset();
    CloseGuiObject(SerialScreen);
    SerialScreen.reset();
    CloseGuiObject(ConnectScreen);
    ConnectScreen.reset();
    CloseGuiObject(SessionScreen);
    SessionScreen.reset();
    CloseGuiObject(PrefScreen);
    PrefScreen.reset();
    CloseGuiObject(LoadScreen);
    LoadScreen.reset();
    CloseGuiObject(SaveScreen);
    SaveScreen.reset();
    GuiSystem().SetCurrentObject({});
    ClearLogArt();
    EmptyFile.clear();
}

auto MCLogistics::ShowLogScreen(bool show, bool redraw) const -> void
{
    if (redraw)
    {
        GuiSystem().ActivatePaletteFromTga(ArtPath + "logart\\lsrupm05.tga");
    }

    CurrentScreen()->ShowGuiWindow(show);
}

auto MCLogistics::SetUpMainScreen(bool fromMenu) -> int32_t
{
    NoteScreen("main");

    if (CurrentScreen() != nullptr)
    {
        ShowLogScreen(false, false);
    }

    SetCurrentScreen(MainScreen);

    if (!fromMenu)
    {
        PreviousState = LogisticsState;
    }

    LogisticsState = 1;
    ShowLogScreen(true, true);

    if (MultiPlayer() != nullptr)
    {
        if (MultiplayerInitialized)
        {
            DestroyMultiplayer();
        }

        MCGameContext::Current().SetMultiPlayer(nullptr);
        MCBriefingScreen& briefing = *BriefingScreen;
        briefing.ChatBlinking = false;

        if (briefing.ChatTimerOn != 0)
        {
            GuiSystem().RemoveTimer(briefing, 5);
        }

        if (PurchaseScreen->ChatBlinking != 0)
        {
            GuiSystem().RemoveTimer(*PurchaseScreen, 7);
        }

        if (RepairScreen->ChatBlinking != 0)
        {
            GuiSystem().RemoveTimer(*RepairScreen, 8);
        }

        // (The original forgot the box shown here, which still showed; the box stays until the screen shows another.)

        if (PurchaseDialog != nullptr)
        {
            PurchaseDialog->ShowGuiWindow(false);
        }
    }

    ClearChat();
    return 0;
}

auto MCLogistics::ClearChat() -> void
{
    if (Chat == nullptr)
    {
        return;
    }

    Chat->Clear();

    for (const MCGuiHandle<MCLogChatWindow>& handle : ChatWindows)
    {
        if (const std::shared_ptr<MCLogChatWindow> window = handle.Lock(); window != nullptr)
        {
            window->ResetHistoryPane();
        }
    }
}

namespace
{
    /// <summary>The colour the original wiped its work pictures to before it drew a pane into one.</summary>
    constexpr int32_t SlideBack = 0x10;
    /// <summary>How far in from a slide picture's right edge a pane's slider column goes.</summary>
    constexpr int32_t SlideColumnInset = 0xe;
    /// <summary>How long a slide takes: it crosses its width in this time.</summary>
    constexpr double SlideSeconds = 0.25;

    /// <summary>
    /// The slide's picture of what <paramref name="pane"/> shows (the original drew it into a work picture wiped to
    /// 0x10): its background copy at (<paramref name="backX"/>, 1), its scrolled contents at (0, 1) keyed (through a
    /// scratch picture wiped to the key) and its slider column at the right edge.
    /// </summary>
    MCLogistics::MCSlidePicture PaneSlide(MCScrollPane& pane, int32_t backX)
    {
        return [&pane, backX](MCLogPort& picture)
        {
            VfxPaneWipe(picture.Frame(), SlideBack);

            if (pane.BackgroundCopy != nullptr)
            {
                pane.BackgroundCopy->CopyTo(picture.Frame(), backX, 1, true);
            }

            {
                MCLogBlockPort content(picture.Frame(), 0, 1, pane.Width(), pane.Height(), true);
                pane.DrawContentTo(content.Frame(), 0, 0);
            }

            pane.DrawSliderColumn(picture.Frame(), picture.Width() - SlideColumnInset, 1, true);
        };
    }

    /// <summary>
    /// The slide's picture of the briefing screen: the part of its look (PaintLook and the screen's chrome, over a new
    /// zeroed picture) under the slide.
    /// </summary>
    MCLogistics::MCSlidePicture BriefingSlide(MCBriefingScreen& briefing)
    {
        return [&briefing](MCLogPort& picture)
        {
            VfxPaneWipe(picture.Frame(), 0);
            MCLogBlockPort look(picture.Frame(), -MCLogistics::SlideX, -MCLogistics::SlideY, briefing.Width(),
                                briefing.Height(), false);
            briefing.PaintLook(look.Frame());
            GlobalLog()->DrawScreenChrome(briefing, look.Frame());
        };
    }
}

auto MCLogistics::SetUpPurchaseScreen(bool animate) -> int32_t
{
    NoteScreen("purchase");
    MakeLineArtAhead();
    MCBriefingScreen& briefing = *BriefingScreen;
    briefing.StopSmackerMovies();
    GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(0));

    if (MultiPlayer() != nullptr)
    {
        if (briefing.ChatBlinking == 0)
        {
            if (PurchaseScreen->ChatBlinking != 0)
            {
                GuiSystem().RemoveTimer(*PurchaseScreen, 7);
                // Original behaviour (OB-096): clears the repair screen's flag instead of the purchase screen's, so
                // the purchase screen's chat button does not blink again until the flag is cleared elsewhere.
                RepairScreen->ChatBlinking = false;
            }
        }
        else if (PurchaseScreen->ChatBlinking == 0)
        {
            GuiSystem().AddTimer(PurchaseScreen, 7, 0xfa, 0, 0, false);
            PurchaseScreen->ChatBlinking = true;
        }
    }

    const std::shared_ptr<MCLogObject> previous = CurrentScreen();

    if (previous != nullptr)
    {
        ShowLogScreen(false, false);
    }

    MCPurchaseScreen& screen = *PurchaseScreen;
    ShowTickerOn(screen);
    SetCurrentScreen(PurchaseScreen);
    LogisticsState = 2;
    screen.DrawBackground();
    DrawScreenButtons();

    switch (CurrentInvTab)
    {
        case 0:
        {
            screen.SetUpMechInv(true, true);
            screen.SetUpMechPurchase();
            break;
        }
        case 1:
        {
            screen.SetUpPilotInv(true, true);
            screen.SetUpPilotPurchase();
            break;
        }
        case 2:
        {
            screen.SetUpCompInv(true, true);
            screen.SetUpCompPurchase();
            break;
        }
        case 3:
        {
            screen.SetUpVhclInv(true, true);
            screen.SetUpVehiclePurchase();
            break;
        }
    }

    if (MultiPlayer() != nullptr)
    {
        ShowLightsOn(screen, 100);
    }

    ShowLogScreen(true, previous != RepairScreen && previous != BriefingScreen);

    if (animate)
    {
        std::shared_ptr<MCScrollPane> pane = screen.UnitPane.Lock();
        const std::shared_ptr<MCScrollPane> previousPane = RepairScreen->UnitPane.Lock();
        const bool fromRepair = previous == RepairScreen;
        pane->ShowGuiWindow(false);
        Transition(fromRepair ? PaneSlide(*previousPane, 0) : BriefingSlide(*BriefingScreen), PaneSlide(*pane, 0),
                   fromRepair ? 1 : 0);
        pane->ShowGuiWindow(true);
    }

    return 0;
}

auto MCLogistics::DrawScreenButtons() const -> void
{
    const std::shared_ptr<MCLogObject> screen = CurrentScreen();

    if (screen != BriefingScreen && screen != PurchaseScreen && screen != RepairScreen)
    {
        return;
    }

    // (The original also made and freed an unused lPort here.)
    if (const MCOptionalRef<MCLogScreenChrome> chrome = screen->Chrome())
    {
        chrome->get().HoveredButton = -1;
    }
}

auto MCLogistics::HoverScreenButton(MCLogObject& screen, int32_t button) -> void
{
    if (const MCOptionalRef<MCLogScreenChrome> chrome = screen.Chrome())
    {
        chrome->get().HoveredButton = button;
    }
}

auto MCLogistics::DrawScreenChrome(MCLogObject& screen, const MCPane& target) -> void
{
    const MCOptionalRef<MCLogScreenChrome> chrome = screen.Chrome();

    // The ready lights' backing, under the screen's lights (the original's lights painted it into their parent).
    if (const std::shared_ptr<MCMPPlayerLightsView> lights =
            PlayerLights != nullptr ? PlayerLights->ShownView() : nullptr;
        lights != nullptr && lights->ParentObject().get() == &screen)
    {
        if (const std::shared_ptr<MCLogPort> back = LogScreenArt("lsc_p0.tga"))
        {
            back->CopyTo(target, 0xd3, 0, false);
        }
    }

    if (BriefingScreen.Is(screen) || PurchaseScreen.Is(screen) || RepairScreen.Is(screen))
    {
        // Button 0 is the main menu in single player, exit in multiplayer; the screen's own button is grayed, the one
        // under the mouse lit, and the briefing button blinks while the chat is unread.
        const std::array<size_t, 4> ports = {MultiPlayer() == nullptr ? size_t{0} : size_t{1}, 2, 3, 4};
        const int32_t own = BriefingScreen.Is(screen) ? 1 : (PurchaseScreen.Is(screen) ? 2 : 3);

        for (int32_t button = 0; button < 4; button++)
        {
            const int32_t top = 0x10 + button * 0x12;
            const std::array<std::shared_ptr<MCLogPort>, 3>& faces =
                ScreenButtonPorts[ports[static_cast<size_t>(button)]];

            if (const std::shared_ptr<MCLogPort> face = faces[button == own ? 2 : 0]; face != nullptr)
            {
                face->CopyTo(target, 2, top, false);
            }

            const bool blinking =
                button == 1 && chrome.has_value() && chrome->get().BlinkLit && BriefingScreen->ChatBlinking != 0;

            if (button != own && ((chrome.has_value() && chrome->get().HoveredButton == button) || blinking))
            {
                if (const std::shared_ptr<MCLogPort> lit = faces[1]; lit != nullptr)
                {
                    lit->CopyTo(target, 2, top, true);
                }
            }
        }
    }

    // The ticker, when this screen shows it.
    for (const MCGuiHandle<MCTickerLine>& handle : TickerLines)
    {
        const std::shared_ptr<MCTickerLine> line = handle.Lock();

        if (line != nullptr && line->IsShowing() && line->ParentObject().get() == &screen)
        {
            line->DrawLine(target);
        }
    }

    // The resource points (not on the session screen) and the clock.
    if (!SessionScreen.Is(screen))
    {
        const std::string text = ResourceFigureText();
        VfxPaneCopy(ResourceBackPort->Frame(), 0, 0, target, 0x209, 2, -1);
        MedWhiteFont->WriteString(target, 0x244 - MedWhiteFont->Width(text), 4, text);
    }

    const std::string time = MCPort::StrTime();
    VfxPaneCopy(ClockBackPort->Frame(), 0, 0, target, 0x24c, 2, -1);
    MedWhiteFont->WriteString(target, 0x254, 4, time, -1);
}

auto MCLogistics::MakePlayerLights() -> void
{
    PlayerLights = std::make_shared<MCMPPlayerLights>();
    const std::array<std::shared_ptr<MCLogObject>, 4> screens = {SessionScreen.Lock(), PurchaseScreen.Lock(),
                                                                 BriefingScreen.Lock(), RepairScreen.Lock()};

    for (const std::shared_ptr<MCLogObject>& screen : screens)
    {
        if (screen != nullptr)
        {
            PlayerLights->Views.push_back(screen->CreateChild<MCMPPlayerLightsView>([&](MCMPPlayerLightsView& view)
                                                                                    { view.Init(PlayerLights); }));
        }
    }
}

auto MCLogistics::ShowLightsOn(MCLogObject& screen, std::optional<int32_t> depth) -> void
{
    if (PlayerLights == nullptr)
    {
        return;
    }

    for (const bool showing : {false, true})
    {
        for (const MCGuiHandle<MCMPPlayerLightsView>& handle : PlayerLights->Views)
        {
            const std::shared_ptr<MCMPPlayerLightsView> view = handle.Lock();

            if (view != nullptr && (view->ParentObject().get() == &screen) == showing)
            {
                view->Show(showing, depth);
            }
        }
    }
}

auto MCLogistics::ResetPlayerLights() -> void
{
    if (PlayerLights == nullptr)
    {
        return;
    }

    for (const MCGuiHandle<MCMPPlayerLightsView>& view : PlayerLights->Views)
    {
        CloseGuiObject(view);
    }

    PlayerLights.reset();
}

auto MCLogistics::ChatWindowOn(const MCLogObject& screen) const -> std::shared_ptr<MCLogChatWindow>
{
    for (const MCGuiHandle<MCLogChatWindow>& handle : ChatWindows)
    {
        if (const std::shared_ptr<MCLogChatWindow> window = handle.Lock();
            window != nullptr && window->ParentObject().get() == &screen)
        {
            return window;
        }
    }

    return nullptr;
}

auto MCLogistics::ShowChatOn(MCLogObject& screen) -> void
{
    const std::shared_ptr<MCLogChatWindow> next = ChatWindowOn(screen);

    if (next == nullptr)
    {
        return;
    }

    const std::shared_ptr<MCLogChatWindow> shown = ChatWindow.Lock();
    bool show = ChatShown;

    if (shown != nullptr && shown != next)
    {
        // The window showing hands its input line's caret blink and text focus on as they are (the original's one
        // window kept them as it moved).
        show = shown->IsShowing();
        shown->ShowGuiWindow(false);
        GuiSystem().HandOver(*shown->ChatInput, next->ChatInput.Weak());
        next->ChatInput->TeamButton->Toggled = Chat->TeamOnly;
    }

    if (shown != next)
    {
        next->ShowGuiWindow(show);
        ChatWindow = next;
    }

    // In front, as the original's window was when added to the screen.
    next->BringToFront(true);
}

auto MCLogistics::ShowChat(bool show) -> void
{
    if (const std::shared_ptr<MCLogChatWindow> window = ChatWindow.Lock(); window != nullptr)
    {
        window->ShowGuiWindow(show);
        return;
    }

    ChatShown = show;
}

auto MCLogistics::ShowTickerOn(MCLogObject& screen, bool shuffle) -> void
{
    std::shared_ptr<MCTickerLine> shown;
    std::shared_ptr<MCTickerLine> next;

    for (const MCGuiHandle<MCTickerLine>& handle : TickerLines)
    {
        if (const std::shared_ptr<MCTickerLine> line = handle.Lock(); line != nullptr)
        {
            if (line->IsShowing())
            {
                shown = line;
            }

            if (line->ParentObject().get() == &screen)
            {
                next = line;
            }
        }
    }

    if (next == nullptr)
    {
        return;
    }

    // The line showing hands its scroll timer on as it runs (the original's one ticker kept it as it moved).
    if (shown != nullptr && shown != next)
    {
        shown->Show(false, false);
        GuiSystem().HandOver(*shown, next->weak_from_this());
    }

    next->Show(true, shuffle);
}

auto MCLogistics::SetUpBriefingScreen(bool animate) -> int32_t
{
    NoteScreen("briefing");
    MakeLineArtAhead();
    GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(0));
    const std::shared_ptr<MCLogObject> previous = CurrentScreen();

    if (previous != nullptr)
    {
        ShowLogScreen(false, false);
    }

    MCBriefingScreen& briefing = *BriefingScreen;
    ShowTickerOn(briefing);
    briefing.SetUpDeploy();

    // The original blanked the local player's empty drop slots here (the screen draws them each frame), then the box
    // area under them (the box is shown again below).
    SetCurrentScreen(BriefingScreen);
    LogisticsState = 3;
    ShowLogScreen(true, previous != RepairScreen && previous != PurchaseScreen);
    briefing.MovieStarted = false;
    briefing.SetUpMission();
    briefing.MissionPane->SetScrollPos(0.0f);
    briefing.DeployPane->SetScrollPos(0.0f);
    briefing.CalcTonnages();
    DrawScreenButtons();

    if (MultiPlayer() == nullptr)
    {
        ShowChat(false);
    }
    else
    {
        const std::shared_ptr<MCLogChatWindow> chat = ChatWindowOn(briefing);

        if (chat != ChatWindow.Lock())
        {
            chat->Resize(0xe7);
            ShowChatOn(briefing);
            chat->MoveTo(2, 0x65, false);
        }

        ShowLightsOn(briefing, 100);

        if (briefing.ChatBlinking != 0 && briefing.ChatTimerOn == 0)
        {
            GuiSystem().AddTimer(briefing.weak_from_this(), 5, 500, 0, 0, false);
            briefing.ChatTimerOn = true;
        }

        briefing.SetUpOperation();
    }

    // Show the briefing box of the first unit in the deploy pane (none: the box area is blank).
    if (!briefing.DeployUnits.empty())
    {
        briefing.ShowBriefing(briefing.DeployUnits.front());
    }
    else
    {
        briefing.HideBriefing();
    }

    if (animate)
    {
        const std::shared_ptr<MCScrollPane> pane =
            (previous == RepairScreen ? RepairScreen->UnitPane : PurchaseScreen->UnitPane).Lock();
        Transition(PaneSlide(*pane, 1), BriefingSlide(briefing), 1);
    }

    return 0;
}

auto MCLogistics::SetUpSessionScreen() -> int32_t
{
    NoteScreen("session");

    if (MultiplayerInitialized)
    {
        DestroyMultiplayer();
    }

    CurrentScreen()->ShowGuiWindow(false);
    SetCurrentScreen(SessionScreen);
    ShowLogScreen(true, true);
    LogisticsState = 8;
    SessionScreen->Activate(false);

    // Port (Redux HD): an internet game's host passes its code on to its players.
    if (MultiPlayer() != nullptr && MultiPlayer()->SessionManager != nullptr)
    {
        if (const std::optional<std::string> code = MultiPlayer()->SessionManager->HostedInternetCode())
        {
            Chat->AddLine(std::format(InternetCodeFormat, *code));
        }
    }

    return 0;
}

auto MCLogistics::SetUpRepairScreen(bool animate) -> int32_t
{
    NoteScreen("repair");
    MakeLineArtAhead();
    MCBriefingScreen& briefing = *BriefingScreen;
    briefing.StopSmackerMovies();
    GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(0));
    MCRepairScreen& repair = *RepairScreen;

    if (MultiPlayer() != nullptr)
    {
        if (briefing.ChatBlinking == 0)
        {
            if (repair.ChatBlinking != 0)
            {
                GuiSystem().RemoveTimer(repair, 8);
                repair.ChatBlinking = false;
            }
        }
        else if (repair.ChatBlinking == 0)
        {
            GuiSystem().AddTimer(repair.weak_from_this(), 8, 0xfa, 0, 0, false);
            repair.ChatBlinking = true;
        }
    }

    const std::shared_ptr<MCLogObject> previous = CurrentScreen();

    if (previous != nullptr)
    {
        ShowLogScreen(false, false);
    }

    ShowTickerOn(repair);
    SetCurrentScreen(RepairScreen);
    LogisticsState = 4;
    repair.DrawBackground();
    DrawScreenButtons();

    switch (CurrentInvTab)
    {
        case 0:
            repair.SetUpMechInv(true, true);
            break;
        case 1:
            repair.SetUpPilotInv(true, true);
            break;
        case 2:
            repair.SetUpCompInv(true, true);
            break;
        case 3:
            repair.SetUpVhclInv(true, true);
            break;
    }

    // Nothing selected yet: the force's first mech (a null one when the force has none).
    if (repair.SelectedMech() == nullptr && repair.SelectedVehicle() == nullptr)
    {
        repair.SelectMech(ForceMechList->Mechs.empty() ? nullptr : ForceMechList->Mechs.front());
    }

    if (MultiPlayer() != nullptr)
    {
        ShowLightsOn(repair, 100);
    }

    ShowLogScreen(true, previous != PurchaseScreen && previous != BriefingScreen);

    if (animate)
    {
        const std::shared_ptr<MCScrollPane> previousPane = PurchaseScreen->UnitPane.Lock();
        repair.UnitPane->ShowGuiWindow(false);
        Transition(previous == PurchaseScreen ? PaneSlide(*previousPane, 0) : BriefingSlide(*BriefingScreen),
                   PaneSlide(*repair.UnitPane, 1), 0);
        repair.UnitPane->ShowGuiWindow(true);
    }

    return 0;
}

namespace
{
    /// <summary>
    /// Port: the pane a screen change slides over the screen's right part (<see cref="MCLogistics::Transition"/>). The
    /// original wrote both pictures into its own picture each frame; it draws them from the slide's state instead.
    /// </summary>
    class MCTransitionWipe : public MCLogObject
    {
    public:
        /// <summary>Draws the two pictures as the slide stands (the original's loop body).</summary>
        void Draw() override
        {
            if (!Lport()->ViewOpen())
            {
                return;
            }

            if (From == nullptr || To == nullptr)
            {
                return;
            }

            MCPane& target = Lport()->Frame();

            // Put together once (HD art off): copied as the original copied its work pictures.
            if (FromPicture != nullptr && ToPicture != nullptr)
            {
                if (Direction == 0)
                {
                    FromPicture->CopyTo(target, 0, 0, true);
                    VfxPaneCopy(ToPicture->Frame(), Width() - Offset, 0, target, 0, 0, VfxNoColor);
                }
                else
                {
                    ToPicture->CopyTo(target, 0, 0, true);
                    VfxPaneCopy(FromPicture->Frame(), Offset, 0, target, 0, 0, VfxNoColor);
                }

                return;
            }

            // Painted each frame: the picture under the other keyed; the other's part shown opaque over it, cut to its
            // place: direction 0 shows the new picture's right part at the left, else the old one's from the slide's
            // offset.
            if (Direction == 0)
            {
                Paint(From, 0, Width(), true);
                Paint(To, Offset - Width(), Offset, false);
            }
            else
            {
                Paint(To, 0, Width(), true);
                Paint(From, -Offset, Width() - Offset, false);
            }
        }

        /// <summary>The wipe draws itself each frame (its port is a view).</summary>
        bool DrawsLive() override { return true; }

        /// <summary>The screen shown before the change, and the one after.</summary>
        MCLogistics::MCSlidePicture From;
        MCLogistics::MCSlidePicture To;
        /// <summary>
        /// Without a sharp screen, the two put together once in pictures, as the original's work pictures were (the
        /// GPU then draws the slide as it always did); with one, none: they are painted each frame, sharp.
        /// </summary>
        std::shared_ptr<MCLogPort> FromPicture;
        std::shared_ptr<MCLogPort> ToPicture;
        /// <summary>0: the new picture slides in from the right; otherwise the old one slides out to the left.</summary>
        int Direction = 0;
        /// <summary>How far the slide has gone, in pixels.</summary>
        int32_t Offset = 0;

    private:
        /// <summary>
        /// Paints <paramref name="picture"/> with its corner at x <paramref name="xPos"/> of the slide, cut to its first
        /// <paramref name="shown"/> columns, keyed or not (as the original copied the picture).
        /// </summary>
        void Paint(const MCLogistics::MCSlidePicture& picture, int32_t xPos, int32_t shown, bool keyed)
        {
            if (shown <= 0)
            {
                return;
            }

            MCLogBlockPort part(Lport()->Frame(), 0, 0, std::min(shown, Width()), Height(), keyed);
            MCLogBlockPort placed(part.Frame(), xPos, 0, Width(), Height(), keyed);
            picture(placed);
        }
    };
}

auto MCLogistics::Transition(const MCSlidePicture& from, const MCSlidePicture& to, int direction) const -> void
{
    // A pane over the screen's right part, redrawn each frame for a quarter of a second: direction 0 slides the new
    // picture in from the right over the old one, any other slides the old one out to the left off the new one.
    const MCGuiHandle<MCTransitionWipe> wipe = CurrentScreen()->CreateChild<MCTransitionWipe>(
        [&](MCTransitionWipe& made)
        {
            made.Init(SlideX, SlideY, SlideWidth, SlideHeight);
            made.From = from;
            made.To = to;

            if (!MCInput::Display()->SharpScreen())
            {
                const auto picture = [](const MCSlidePicture& paint)
                {
                    std::shared_ptr<MCLogPort> painted = NewPort(SlideWidth, SlideHeight);
                    paint(*painted);
                    return painted;
                };

                made.FromPicture = picture(from);
                made.ToPicture = picture(to);
            }

            made.Direction = direction;
        });
    wipe->ShowGuiWindow(true);
    wipe->SetDepth(100);
    SoundSystem()->PlayInterfaceSample(0x36);
    const int64_t frequency = MCPort::PerformanceFrequency();
    float elapsed = 0.0f;

    do
    {
        const int64_t start = MCPort::PerformanceCounter();
        wipe->Offset = static_cast<int32_t>(static_cast<double>(elapsed) / SlideSeconds * SlideWidth);
        UpdateDisplay(false, false, 0, false, 0);
        const int64_t end = MCPort::PerformanceCounter();
        // The original divided the low 32 bits of the tick difference by the low 32 bits of the frequency.
        const auto ticks = static_cast<uint32_t>(end - start);
        elapsed = static_cast<float>(static_cast<double>(ticks) / static_cast<int32_t>(frequency) + elapsed);
    } while (elapsed < SlideSeconds);

    CloseGuiObject(wipe);
}

auto MCLogistics::Darken(int32_t amount, MCColorTable fadeTable, MCLogPort& port) -> void
{
    // Darkens row block amount (of the port's height) through the fade table. (With no port the original darkened
    // the repair screen's unit pane in 0x19d x 0x70 blocks; every caller passes one.)
    const int32_t height = port.Height();
    DarkenRect(port, 0, height * amount, port.Width(), height, fadeTable);
}

void MCLogistics::DarkenRect(MCLogPort& port, int32_t xPos, int32_t yPos, int32_t width, int32_t height,
                             MCColorTable fadeTable)
{
    std::array<MCScreenVertex, 4> corners{};
    corners[0].X = xPos;
    corners[0].Y = yPos;
    corners[1].X = xPos + width - 1;
    corners[1].Y = yPos;
    corners[2].X = xPos + width - 1;
    corners[2].Y = yPos + height - 1;
    corners[3].X = xPos;
    corners[3].Y = yPos + height - 1;
    VfxTranslatePolygon(port.Frame(), corners, fadeTable);
}

auto MCLogistics::ReIndexInventory() const -> int32_t
{
    // Give each component with copies the next inventory row, in the order of the widgets' inventory indexes;
    // components with none get -1.
    const int32_t numItems = ComponentInventory->NumItems();
    int32_t row = 0;

    for (int32_t index = 0; index < numItems; index++)
    {
        const auto found =
            std::ranges::find_if(ComponentInventory->Items, [&](const std::shared_ptr<MCLogInventoryItem>& item)
                                 { return item->InventoryIndex == index; });
        Assert(found != ComponentInventory->Items.end(), 0,
               "Could not reindex player inventory. Probably an old savegame");

        if (found == ComponentInventory->Items.end())
        {
            continue;
        }

        MCLogInventoryItem& item = **found;
        item.ListIndex = item.Count == 0 ? -1 : row++;
    }

    return row;
}

auto MyGetUserName() -> std::optional<std::string>
{
    // The name buffers on the multiplayer screens hold 63 characters.
    constexpr uint32_t bufferSize = 0x3f;

    // The name last entered on the multiplayer screens (HKLM\Software\FASA Interactive\MechCommander Expansion,
    // "Player Name"; the port's registry file), else the logged-in user's name.
    if (const std::optional<std::string> saved =
            MCRegistry::Read("Software\\FASA Interactive\\MechCommander Expansion", "Player Name");
        saved.has_value() && saved->size() + 1 <= bufferSize)
    {
        return saved;
    }

    // Port fix: RegQueryValueExA set the size to the one it needed before failing, and GetUserNameA was then given
    // that larger size for the same buffer. The port keeps the buffer's size.
    // GetUserNameA failed when the name and its terminator didn't fit.
    std::optional<std::string> name = MCPort::GetUserName();

    if (!name.has_value() || name->size() + 1 > bufferSize)
    {
        return std::nullopt;
    }

    return name;
}

auto CancelBool(int32_t) -> void
{
    if (LaunchedFromLobby != 0 && MultiPlayer() != nullptr)
    {
        KillTheGame();
    }

    Cancel();
}

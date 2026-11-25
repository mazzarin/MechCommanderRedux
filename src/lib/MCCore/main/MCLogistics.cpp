#include "stdafx.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCMenuScreens.h"
#include "main/MCLogistics.h"
#include "lib/MCReleaseData.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCScrollPane.h"
#include "gui/MCUpdateDisplay.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCFilesAhead.h"
#include "lib/MCFitIniFile.h"
#include "ai/MCMoveGeometry.h"
#include "logistics/MCBriefingBox.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCDeploy.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGameList.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCChatView.h"
#include "logistics/MCLogChatWindow.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogPort.h"
#include "logistics/MCLogScrollTextObject.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMainMenuScreen.h"
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
#include "logistics/MCRoomScreen.h"
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
#include "sprite/MCShapeFile.h"
#include "ui/MCRmlFlow.h"
#include "vfx/MCVfxFunctions.h"

std::type_identity_t<std::array<bool, 7>> LogCheatActive{};
std::array<int32_t, 6> MultiPlayerColors{};
std::type_identity_t<int32_t> LogCurCheatChar{};
std::type_identity_t<int> InDemo{};
bool Solo = false;
int32_t LastLogisticsMissionState = 0;
std::string HoldString;
int32_t ResourceDisplayState = 0;

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

    /// <summary>The chat's history, in pixels (its lines' wrapped heights times the width), as the original's.</summary>
    constexpr int32_t ChatHistorySize = 100000;
    /// <summary>
    /// The chat's history's width: the 1998 chat windows' history pane's content (0xb8 wide, less its slider), which set
    /// the history's area (MCLogChatWindow::Init).
    /// </summary>
    constexpr int32_t ChatHistoryWidth = 0xb8 - MCScrollPane::SliderWidth;
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
    /// <summary>A new <paramref name="width"/> x <paramref name="height"/> port with its own bitmap.</summary>
    std::shared_ptr<MCLogPort> NewPort(int32_t width, int32_t height)
    {
        auto port = std::make_shared<MCLogPort>();
        port->Init(width, height);
        return port;
    }

    /// <summary>
    /// Loads the whole of an opened shape file into a block registered as shapes (the repair and icon shapes): with HD
    /// art's numbered folders drawn, the pack's copy (MCShapeFile).
    /// </summary>
    MCRegisteredBlock ReadShapeFile(MCShapeFile& file, std::string_view sizeError)
    {
        const uint32_t length = file.GetLength();
        MCRegisteredBlock shapes(length, MCDataKind::Shapes);
        const int32_t read = file.Read(shapes.Bytes());
        Assert(static_cast<uint32_t>(read) == length, 0, sizeError);
        file.Close();
        return shapes;
    }

    /// <summary>Opens shape file <paramref name="fileName"/> (asserting it exists) and loads it (<see cref="ReadShapeFile"/>).</summary>
    MCRegisteredBlock LoadShapeFile(MCShapeFile& file, std::string_view fileName, std::string_view openError,
                                    std::string_view sizeError)
    {
        const int32_t result = file.Open(fileName);
        Assert(result == 0, static_cast<uint32_t>(result), openError);
        return ReadShapeFile(file, sizeError);
    }
}

auto MCLogistics::ReadSortTables() -> void
{
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
}

auto MCLogistics::ReadGameSystemValues() -> void
{
    MCFitIniFile gameSystemFile;
    int32_t result = gameSystemFile.Open(GamePath(MissionPath, "gamesys.fit"));
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

auto MCLogistics::ReadKeptTablesIfSwitched() -> void
{
    // Port-only (Redux HD): read again from the files a match plays, or single player's, since what's read changed.
    if (KeptTablesSwitches != MCReleaseData::Switches())
    {
        KeptTablesSwitches = MCReleaseData::Switches();
        ReadSortTables();
        ReadGameSystemValues();
    }
}

auto MCLogistics::Start() -> void
{
    if (EmptyFile.empty())
    {
        EmptyFile = LoadGameString(0x381, 0xfe);
    }

    _Started = true;
    _Starting = true;
    SetGlobalLog(weak_from_this());
    // Port-only (SPEC-menus.md's stage 2): the old screens as the flow's kinds.
    MCMenuScreens::Register();
    // Port-only (SPEC-menus.md's stage 2): the art a logistics before this one loaded, kept across the mission, when it
    // was loaded from the files this one would load.
    _ArtKey = {
        .Switches = MCReleaseData::Switches(), .Numbered = MCHdArt::NumberedFolders(), .Pack = MCHdArt::PackFolder()};
    const bool artReused = TakeBackArt();
    // Port-only: what the screens look for in the archives is noted while they're up, to be read ahead when they're
    // made again (MCMission::CloseResultsScreen).
    MCFilesAhead::StartNoting();
    AutoPlayMovie = false;
    NoteTop(MCScreenFlow::NoScreen);
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

    if (InDemo == 0)
    {
        // The chat (the remade screens' chat views show it), its history the 1998 chat windows' (which kept the lines
        // that fit it, wrapped to its width).
        Chat = std::make_shared<MCLogChat>(ChatHistorySize);
        Chat->SetHistoryArea(ChatHistoryWidth, ChatHistorySize / ChatHistoryWidth);
        ChatShown = false;
    }

    // Port-only: the frame log's parts of the menus' start (the screens, then the art), as the way back times them.
    std::optional<MCFrameLog::Scope> part(std::in_place, "menus.screens");
    // Port-only (SPEC-menus.md's stage 9): none of the 1998 screens, windows or their ticker is made: the remade screens
    // (the flow's kinds) show every menu, over the models logistics keeps. The chat's unseen mark starts clear, and
    // DEPLOY's drop at no tons and open, as the 1998 briefing, made here, set them.
    MCChatView::SetUnread(false);
    CurDeployTonnage = 0;
    MCDeploy::SetLocked(false);
    LogisticsState = 1;
    NoteTop(MCMenuScreens::Main());
    MCUpdater::CheckAtStart();
    // Port (personal): the reports start once a run (main/MCTelemetry.h); the main menu shows their notice or
    // question when due (MCMission::Run).
    MCTelemetry::Start();

    if (InDemo == 0)
    {
        // The room's logic runs in any logistics with menus; it starts as the 1998 room's set-up, made here, started
        // it: team 1 Inner Sphere, team 2 Clan, no one seated or waiting.
        Room.Team1TechBase = MCRoomModel::InnerSphere;
        Room.Team2TechBase = MCRoomModel::Clan;
        Room.Seats.fill({});
        Room.NumUnassigned = 0;
        Room.SetMade(true);
    }

    // Under the process ID, as aSystem::init sets it: copies of the game on one machine share the user folder.
    SaveTempPath = std::format("{}temp\\{}\\", SavePath, MCPort::ProcessId());
    // Port fix (OB-094): the original allocated a File here, and a FitIniFile after the sort tables, and never used or
    // freed either.

    KeptTablesSwitches = MCReleaseData::Switches();
    ReadSortTables();

    // Port-only (SPEC-menus.md's stage 9, step 2): each dialog a model, said by the remade dialog.
    Message = std::make_shared<MCMenuDialog>();
    Question = std::make_shared<MCMenuDialog>();
    Refit = std::make_shared<MCMenuDialog>();

    part.reset();
    part.emplace("menus.art");

    if (!artReused)
    {
        LoadArtFiles();
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

    ReadGameSystemValues();

    ReadyRoomTicks = 0;

    if (LaunchedFromLobby == 0 || MultiPlayer() == nullptr || Turn != 0)
    {
        LogisticsState = 1;
        NoteTop(MCMenuScreens::Main());
    }
    else
    {
        // Started from a lobby: straight to the ready room, whose CANCEL ends the game (the remade launcher's join,
        // logistics/MCRoomScreen). Port-only (the personal build): it shows while the lobby's join goes on; joined (or
        // hosting), the room opens (MCMission).
        NoteTop(MCMenuScreens::ReadyRoom());
        LogisticsState = 0xe;
    }

    MakeAhead();
    _Starting = false;

    // Port-only (SPEC-menus.md's stage 2): the menus' first screen at the stack's bottom, at the game's start (the way
    // back lands where it goes, MCScreenFlow::Land).
    if (MCScreenFlow::Stack().empty() && MCScreenFlow::Landing().empty())
    {
        MCScreenFlow::Noted();
    }
}

auto MCLogistics::LoadArtFiles() -> void
{
    // Port-only (SPEC-menus.md's stage 9): the units' icon shapes alone, which the remade screens draw their outlines
    // from (ui/MCRmlArt's unit: pictures); the 1998 screens' repair shapes, frames, tabs, buttons and backs aren't read.
    MCShapeFile shapeFile;

    for (size_t i = 0; i < MechIconShapes.size(); ++i)
    {
        MechIconShapes[i] = LoadShapeFile(shapeFile, std::format("{}mi{:02}.shp", ArtPath, i),
                                          "could not open mechicon shape file", "unexpected mechicon size");
    }

    // A vehicle without a repair picture has no icon either.
    for (size_t i = 0; i < VehicleIconShapes.size(); ++i)
    {
        if (shapeFile.Open(std::format("{}vr1{:02}.shp", ArtPath, i)) != 0)
        {
            continue;
        }

        shapeFile.Close();

        if (shapeFile.Open(std::format("{}vi1{:02}.shp", ArtPath, i)) == 0)
        {
            VehicleIconShapes[i] = ReadShapeFile(shapeFile, "unexpected vhclrep size");
        }
    }
}

auto MCLogistics::TakeBackArt() -> bool
{
    const std::shared_ptr<MCLogisticsArt> art = std::exchange(_ReusedArt, nullptr);

    if (art == nullptr || art->Key != _ArtKey)
    {
        return false;
    }

    static_cast<MCLogisticsArtFiles&>(*this) = std::move(art->Files);
    ReuseLogArt(std::move(art->Loaded));
    return true;
}

auto MCLogistics::TakeArt() -> std::shared_ptr<MCLogisticsArt>
{
    if (!_Started)
    {
        return nullptr;
    }

    auto art = std::make_shared<MCLogisticsArt>();
    art->Key = _ArtKey;
    art->Files = std::move(static_cast<MCLogisticsArtFiles&>(*this));
    art->Loaded = TakeLogArt();
    return art;
}

auto MCLogistics::ReuseArt(std::shared_ptr<MCLogisticsArt> art) -> void
{
    _ReusedArt = std::move(art);
}

auto MCLogistics::SetCurrentScreen(std::weak_ptr<MCLogObject> screen) -> void
{
    _CurrentScreen = std::move(screen);
}

auto MCLogistics::ShowInPlace(std::weak_ptr<MCLogObject> screen) -> std::shared_ptr<MCLogObject>
{
    std::shared_ptr<MCLogObject> previous = CurrentScreen();

    if (previous != nullptr)
    {
        previous->ShowGuiWindow(false);
    }

    SetCurrentScreen(std::move(screen));
    return previous;
}

auto MCLogistics::NoteTop(MCScreenFlow::MCScreenId id, MCMenuScreens::MCCampaignPage page) -> void
{
    const MCScreenFlow::MCScreenId before = std::exchange(_Top, id);

    if (GlobalLog().get() == this)
    {
        MCMenuScreens::NoteTop(id, page);
    }

    if (id == MCScreenFlow::NoScreen || id == before)
    {
        return;
    }

    // Another screen of logistics' comes: the remade ones shown hide (it shows its own), as a 1998 screen coming up hid
    // them.
    MCRmlFlow::HideScreens();

    if (_Starting || before == MCScreenFlow::NoScreen || MCScreenChange::InPiece() || MCScreenChange::Dark() ||
        !HasGuiSystem() || !GuiSystem().CanChangeScreen())
    {
        return;
    }

    MCScreenFlow::NoteUnrouted(std::format("logistics state {}", LogisticsState));
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

auto MCLogistics::RepairShapesDrawn(size_t mech, int32_t frame) -> std::vector<MCSharpLines::MCDrawnLoop>
{
    return mech == AtlasMech && frame == AtlasHead ? AtlasEyes() : std::vector<MCSharpLines::MCDrawnLoop>{};
}

auto MCLogistics::AttachHdArt() -> void
{
    // A new owner: ties made before go with the old one.
    HdArtOwner = std::make_shared<const int>(0);

    // Each unit's icons tied to the pack's (named as their files are). Port-only (SPEC-menus.md's stage 9): the repair
    // shapes, the 1998 screens' diagrams and sold-out outlines, aren't loaded.
    const auto attach = [this](std::span<const MCRegisteredBlock> icons, std::string_view prefix)
    {
        for (size_t i = 0; i < icons.size(); ++i)
        {
            if (!icons[i].Empty())
            {
                MCHdArt::Attach(icons[i].Bytes(), HdArtOwner, MCHdArt::IconShapeKey{std::format("{}{:02}", prefix, i)});
            }
        }
    };

    attach(MechIconShapes, "mi");
    attach(VehicleIconShapes, "vi1");
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

    // The fonts' glyphs (made since the display opened, with HD art on then: only those not made are made).
    GuiSystem().MakeFontsAhead();

    // The HD copies of the pictures the screens are made of (their art, loaded by Start, the screens' backgrounds
    // with it: MCLogInvScreen::InitLive), read ahead by the pack's reading thread; then (those first, the soonest
    // needed) the rest of the screens' set, as the display's opening asked.
    ReadTexturesAhead();
    GuiSystem().ReadMenuSetAhead();
}

auto MCLogistics::ReadTexturesAhead(MCHdArt::ReadAs as) -> void
{
    if (!DrawsSharp())
    {
        return;
    }

    std::vector<MCHdArt::PictureAhead> pictures;

    for (const std::shared_ptr<MCTexture>& texture : MCRenderer::Textures())
    {
        if (texture != nullptr && !texture->CpuStale && !texture->Pixels.empty())
        {
            pictures.push_back({texture->Pixels, texture->Width, texture->Height});
        }
    }

    MCHdArt::ReadPicturesAhead(pictures, as);
}

auto MCLogistics::BriefingMapFile(std::string_view scenario) -> std::optional<std::string>
{
    // The longest map name GetCurrentMission reads.
    constexpr size_t MapFileChars = 0xff;
    MCFitIniFile missionFile;
    std::string map;

    if (missionFile.Open(GamePath(MissionPath, scenario, ".fit")) != 0 || missionFile.SeekBlock("Campaign") != 0 ||
        !ReadText(missionFile, "MapFile", MapFileChars, map))
    {
        return std::nullopt;
    }

    return GamePath(TerrainPath, map, ".log.tga");
}

auto MCLogistics::BriefingMapOf(std::string_view scenario) -> std::optional<MCPicturePixels>
{
    const std::optional<std::string> path = BriefingMapFile(scenario);
    return path.has_value() ? MapPictureAt(*path) : std::nullopt;
}

auto MCLogistics::MapPictureAt(const std::string& path) -> std::optional<MCPicturePixels>
{
    MCFile file;

    if (file.Open(path) != 0 || file.FileSize() == 0)
    {
        return std::nullopt;
    }

    std::vector<uint8_t> data(file.FileSize());
    file.Read(data);
    file.Close();
    const MCLogTga tga = ReadLogTga(data);

    if (tga.Pixels.empty())
    {
        return std::nullopt;
    }

    return MCPicturePixels{{tga.Pixels.begin(), tga.Pixels.end()}, tga.Width, tga.Height};
}

auto MCLogistics::ReadBriefingMapAhead(std::string_view scenario) -> void
{
    if (!DrawsSharp())
    {
        return;
    }

    if (const std::optional<MCPicturePixels> map = BriefingMapOf(scenario); map.has_value())
    {
        const std::array pictures = {MCHdArt::PictureAhead{map->Pixels, map->Width, map->Height}};
        MCHdArt::ReadPicturesAhead(pictures);
    }
}

MCLogistics::~MCLogistics()
{
    if (!_Started)
    {
        return;
    }

    MCFilesAhead::StopNoting();
    // Port-only: the line art made ahead for this logistics' lists goes with them.
    MCSharpPictures::ForgetLineArt();
    MCHdArt::SetLineArtFrames({}, MCHdArt::LineArtSet::Logistics);
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

    for (MCRegisteredBlock& shapes : MechIconShapes)
    {
        shapes = {};
    }

    for (MCRegisteredBlock& shapes : VehicleIconShapes)
    {
        shapes = {};
    }

    Message.reset();
    Question.reset();
    Refit.reset();
    NoteTop(MCScreenFlow::NoScreen);
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
    Room.SetMade(false);
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

    // Port-only (SPEC-menus.md's stage 7): the main menu remade; port-only (stage 4): the room remade, and a launcher's
    // join on the painting (logistics/MCRoomScreen). Port-only (stage 9): no 1998 screen is made; the campaign's
    // screens show through their own set-up (MCCampaignMenu).
    if (!show)
    {
        return;
    }

    if (_Top == MCMenuScreens::Main())
    {
        MCMainMenuScreens::ShowMain();
    }
    else if (_Top == MCMenuScreens::Room())
    {
        ShowRoomScreen();
    }
    else if (_Top == MCMenuScreens::ReadyRoom())
    {
        ShowLauncherScreen();
    }
}

auto MCLogistics::SetUpMainScreen(bool fromMenu) -> int32_t
{
    NoteScreen("main");
    NoteTop(MCMenuScreens::Main());

    if (!fromMenu)
    {
        PreviousState = LogisticsState;
    }

    LogisticsState = 1;
    ShowLogScreen(true, true);
    LeaveMatchAtMainMenu();
    ClearChat();
    return 0;
}

auto MCLogistics::LeaveMatchAtMainMenu() -> void
{
    if (MultiPlayer() != nullptr)
    {
        if (MultiplayerInitialized)
        {
            DestroyMultiplayer();
        }

        MCGameContext::Current().SetMultiPlayer(nullptr);
        MCChatView::SetUnread(false);
    }
}

auto MCLogistics::ClearChat() -> void
{
    if (Chat == nullptr)
    {
        return;
    }

    Chat->Clear();
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

namespace
{
    /// <summary>
    /// The purchase screen's inventory list and store set up for inventory tab <paramref name="tab"/>, the store
    /// filled though the screen is hidden when <paramref name="ahead"/>.
    /// </summary>
    void SetUpPurchaseLists(MCPurchaseScreen& screen, int32_t tab, bool ahead)
    {
        switch (tab)
        {
            case 0:
            {
                screen.SetUpMechInv(true, true);
                screen.SetUpMechPurchase(ahead);
                break;
            }
            case 1:
            {
                screen.SetUpPilotInv(true, true);
                screen.SetUpPilotPurchase(ahead);
                break;
            }
            case 2:
            {
                screen.SetUpCompInv(true, true);
                screen.SetUpCompPurchase(ahead);
                break;
            }
            case 3:
            {
                screen.SetUpVhclInv(true, true);
                screen.SetUpVehiclePurchase(ahead);
                break;
            }
        }
    }

    /// <summary>The repair screen's inventory list set up for inventory tab <paramref name="tab"/>.</summary>
    void SetUpRepairLists(MCRepairScreen& repair, int32_t tab)
    {
        switch (tab)
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
    }
}

auto MCLogistics::SetUpPurchaseScreen(bool animate) -> int32_t
{
    NoteScreen("purchase");
    MCBriefingScreen& briefing = *BriefingScreen;
    briefing.StopSmackerMovies();
    GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(0));

    if (MultiPlayer() != nullptr)
    {
        if (!MCChatView::Unread())
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

    const std::shared_ptr<MCLogObject> previous = ShowInPlace(PurchaseScreen);
    MCPurchaseScreen& screen = *PurchaseScreen;
    ShowTickerOn(screen);
    LogisticsState = 2;
    screen.DrawBackground();
    DrawScreenButtons();

    SetUpPurchaseLists(screen, CurrentInvTab, false);

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

            const bool blinking = button == 1 && chrome.has_value() && chrome->get().BlinkLit && MCChatView::Unread();

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
    // Port-only (SPEC-menus.md's stage 9): the model alone; the remade room, campaign and loading screens view it.
    PlayerLights = std::make_shared<MCMPPlayerLights>();
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
    // Port-only (SPEC-menus.md's stage 8): the remade BRIEFING and DEPLOY (logistics/MCCampaignMenu) over the 1998
    // briefing, which is set up as ever and hidden under them. The 1998 PURCHASING's and 'MECH BAY's BRIEFING button
    // comes back to the tab left on, through a change of screen in place of the slide.
    if (animate)
    {
        MCCampaignMenu::ComeBack();

        if (!MCScreenChange::InPiece() && !MCScreenChange::Dark())
        {
            MCScreenFlow::Change("briefing", [] { GlobalLog()->SetUpBriefingScreen(false); });
            return 0;
        }
    }

    NoteScreen("briefing");
    GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(0));
    NoteTop(MCMenuScreens::Campaign());
    LogisticsState = 3;
    ShowLogScreen(true, true);
    MCDeploy::CountTons();

    // A match's briefing opens on its chat, as 1998's opened on its operation tab, the chat: the chat's unseen mark
    // cleared (MCBriefingScreen::SetUpOperation's).
    if (MultiPlayer() != nullptr)
    {
        MCChatView::SetUnread(false);
    }

    // Port-only: in a change's piece, the remade screen set up, laid out and shown in pieces of their own, after this
    // one.
    if (MCScreenChange::InPiece() && !animate)
    {
        MCScreenChange::RunNext(MCCampaignMenu::ShowPieces());
        return 0;
    }

    MCCampaignMenu::Show();
    return 0;
}

auto MCLogistics::SetUpSessionScreen() -> int32_t
{
    ShowSessionScreen();
    ReadySessionScreen();
    return 0;
}

auto MCLogistics::ReadySessionScreen() -> void
{
    // Before the room's set-up, as the original went: the last session's lists go, and the chat's handler is set again
    // by the set-up.
    if (MultiplayerInitialized)
    {
        DestroyMultiplayer();
    }

    Room.Activate(false);

    // Port (Redux HD): an internet game's host passes its code on to its players.
    if (MultiPlayer() != nullptr && MultiPlayer()->SessionManager != nullptr)
    {
        if (const std::optional<std::string> code = MultiPlayer()->SessionManager->HostedInternetCode())
        {
            Chat->AddLine(std::format(InternetCodeFormat, *code));
        }
    }
}

auto MCLogistics::ShowSessionScreen() -> void
{
    NoteScreen("session");
    NoteTop(MCMenuScreens::Room());
    ShowLogScreen(true, true);
    LogisticsState = 8;
}

auto MCLogistics::SetUpRepairScreen(bool animate) -> int32_t
{
    NoteScreen("repair");
    MCBriefingScreen& briefing = *BriefingScreen;
    briefing.StopSmackerMovies();
    GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(0));
    MCRepairScreen& repair = *RepairScreen;

    if (MultiPlayer() != nullptr)
    {
        if (!MCChatView::Unread())
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

    const std::shared_ptr<MCLogObject> previous = ShowInPlace(RepairScreen);
    ShowTickerOn(repair);
    LogisticsState = 4;
    repair.DrawBackground();
    DrawScreenButtons();

    SetUpRepairLists(repair, CurrentInvTab);

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

auto ResourceFigureText() -> std::string
{
    // States 1 and 2 showed the logistics heap's free memory and largest free block; the heap is gone, so they show
    // nothing (the original left the text unset for another state).
    return ResourceDisplayState == 0 ? std::format("{}", ResourcePoints) : std::string();
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

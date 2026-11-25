#include "stdafx.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCMenuScreens.h"
#include "main/MCLogistics.h"
#include "lib/MCReleaseData.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCUpdateDisplay.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCFilesAhead.h"
#include "lib/MCFitIniFile.h"
#include "ai/MCMoveGeometry.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCDeploy.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCChatView.h"
#include "logistics/MCLogChat.h"
#include "logistics/MCLogPort.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMainMenuScreen.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCRoomScreen.h"
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
    /// The chat's history's width: the 1998 chat windows' history pane's content (0xb8 wide, less its 13-pixel slider),
    /// which set the history's area.
    /// </summary>
    constexpr int32_t ChatHistoryWidth = 0xb8 - 13;
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

namespace
{
    /// <summary>
    /// Campaign mission <paramref name="scenario"/>'s mission file's Campaign entry <paramref name="name"/>, as
    /// GetCurrentMission reads it (at most <paramref name="chars"/> characters); none when the file or the entry can't be
    /// read.
    /// </summary>
    std::optional<std::string> CampaignEntry(std::string_view scenario, std::string_view name, size_t chars)
    {
        MCFitIniFile missionFile;
        std::string text;

        if (missionFile.Open(GamePath(MissionPath, scenario, ".fit")) != 0 || missionFile.SeekBlock("Campaign") != 0 ||
            !ReadText(missionFile, name, chars, text))
        {
            return std::nullopt;
        }

        return text;
    }
}

auto MCLogistics::BriefingMapFile(std::string_view scenario) -> std::optional<std::string>
{
    // The longest map name GetCurrentMission reads.
    constexpr size_t MapFileChars = 0xff;
    const std::optional<std::string> map = CampaignEntry(scenario, "MapFile", MapFileChars);
    return map.has_value() ? std::optional(GamePath(TerrainPath, *map, ".log.tga")) : std::nullopt;
}

auto MCLogistics::BriefingTextFile(std::string_view scenario) -> std::optional<std::string>
{
    // The longest briefing name GetCurrentMission reads.
    constexpr size_t BriefingFileChars = 0x7f;
    const std::optional<std::string> briefing = CampaignEntry(scenario, "BriefingFile", BriefingFileChars);
    return briefing.has_value() ? std::optional(GamePath(MissionPath, *briefing)) : std::nullopt;
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

auto MCLogistics::SetUpMainScreen() -> int32_t
{
    NoteScreen("main");
    NoteTop(MCMenuScreens::Main());
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

auto MCLogistics::MakePlayerLights() -> void
{
    // Port-only (SPEC-menus.md's stage 9): the model alone; the remade room, campaign and loading screens view it.
    PlayerLights = std::make_shared<MCMPPlayerLights>();
}

auto MCLogistics::ResetPlayerLights() -> void
{
    PlayerLights.reset();
}

auto MCLogistics::SetUpBriefingScreen() -> int32_t
{
    // Port-only (SPEC-menus.md's stage 8): the remade BRIEFING and DEPLOY (logistics/MCCampaignMenu), on the tab left on.
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
    if (MCScreenChange::InPiece())
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

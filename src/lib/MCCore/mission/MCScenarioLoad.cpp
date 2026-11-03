#include "stdafx.h"
#include "mission/MCScenario.h"
#include "gui/MCGuiChatWindow.h"
#include "object/MCBuildingMarines.h"
#include "object/MCCameraDrone.h"
#include "platform/MCInput.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "abl/MCAblRuntime.h"
#include "ai/MCMoveSystem.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "camera/MCCameraList.h"
#include "engine/MCCraterManager.h"
#include "engine/MCElementBuffer.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCUpdateDisplay.h"
#include "lib/MCFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "logistics/MCPreferencesMenu.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCDifficultySettings.h"
#include "mission/MCMission.h"
#include "mission/MCScenarioReading.h"
#include "network/MCMultiPlayer.h"
#include "object/MCBattleMech.h"
#include "object/MCCollisionSystem.h"
#include "object/MCContactSystem.h"
#include "object/MCEffectSystem.h"
#include "object/MCElemental.h"
#include "object/MCElementalGameSystem.h"
#include "object/MCForces.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCMasterComponent.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMechWarrior.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCMoverGroup.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectTypeManager.h"
#include "object/MCTrain.h"
#include "object/MCTrainCar.h"
#include "object/MCTrainManager.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCElementalActor.h"
#include "sprite/MCGVAppearance.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCParticles.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCSpriteManager.h"
#include "sprite/MCWeather.h"
#include "terrain/MCGroundMesh.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>Degrees to radians, as MCX.EXE stores it (a hair under pi / 180).</summary>
    constexpr double DegreesToRadians = 0x1.1df46a2526c7ap-6;

    /// <summary>
    /// The object of part <paramref name="partNumber"/> of <paramref name="scenario"/>, which the mission file names as
    /// <paramref name="what"/>: a part that exists and holds a <typeparamref name="T"/> that <paramref name="fits"/>.
    /// </summary>
    /// <remarks>
    /// Issue #25: the original indexed the parts with the file's number unchecked and cast whatever it found (a mech to
    /// a train car). A mission that names another part can't be played: it ends with the reason.
    /// </remarks>
    template <class T, class Fits>
    std::shared_ptr<T> ReferencedPart(const MCScenario& scenario, int64_t partNumber, std::string_view what,
                                      Fits&& fits)
    {
        if (partNumber >= 1 && partNumber <= static_cast<int64_t>(scenario.NumParts()))
        {
            if (std::shared_ptr<T> object =
                    std::dynamic_pointer_cast<T>(LockObject(scenario.Parts[static_cast<size_t>(partNumber)].Object));
                object != nullptr && fits(*object))
            {
                return object;
            }
        }

        Fatal(static_cast<int32_t>(partNumber),
              std::format(" The mission's {} names part {}, which isn't one ", what, partNumber));
    }

    /// <summary>
    /// Installs a terrain in the game context and loads <paramref name="fileName"/> into it (what it builds reaches for
    /// it there); a failure is Fatal.
    /// </summary>
    void InstallTerrain(std::string_view fileName)
    {
        MCGameContext::Current().SetTerrain(std::make_unique<MCTerrain>());

        if (std::expected<void, std::string> loaded = Terrain()->Load(fileName); !loaded)
        {
            Fatal(0, std::format(" could not start Terrain System: {} ", loaded.error()));
        }
    }

    /// <summary>Turns a frame about its k axis (MC2's inline frame_of_ref::rotate_about_k).</summary>
    void RotateAboutK(MCFrameOfRef& frame, float s, float c)
    {
        const MCVector3D oldI = frame.I;
        frame.I = frame.I * c + frame.J * s;
        frame.J = frame.J * c - oldI * s;
    }

    /// <summary>Reads shape file <paramref name="name"/> (the connect and waypoint shapes).</summary>
    /// <returns>The shapes, or no block when the file can't be opened.</returns>
    MCRegisteredBlock LoadShapeFile(std::string_view name)
    {
        MCFile shapeFile;

        if (shapeFile.Open(GamePath(ShapesPath, name, ".shp")) != 0)
        {
            return {};
        }

        MCRegisteredBlock shapes(shapeFile.FileSize(), MCDataKind::Shapes);
        shapeFile.Read(shapes.Bytes());
        return shapes;
    }

    /// <summary>
    /// Opens <paramref name="name"/><c>.fit</c> in <paramref name="path"/> into <paramref name="file"/>, falling back
    /// on the temporary save folder (a saved game's copy).
    /// </summary>
    /// <returns>The result of the last open.</returns>
    int32_t OpenWithSaveFallback(MCFitIniFile& file, std::string_view path, std::string_view name)
    {
        const int32_t result = file.Open(GamePath(path, name, ".fit"));
        return result == 0 ? 0 : file.Open(GamePath(SaveTempPath, name, ".fit"));
    }

    /// <summary>Shows the connect shape on the loading screen while the scenario loads.</summary>
    class MCConnectShapeScope
    {
    public:
        MCConnectShapeScope() { ConnectShape() = LoadShapeFile("connect"); }
        ~MCConnectShapeScope() { ConnectShape() = {}; }
        MCConnectShapeScope(const MCConnectShapeScope&) = delete;
        MCConnectShapeScope& operator=(const MCConnectShapeScope&) = delete;
    };

    /// <summary>A string entry of the scenario FIT that sizes a heap the port no longer has: required, not used.</summary>
    void RequireUnused(MCFitIniFile& file, std::string_view name, std::string_view message)
    {
        RequireFit<uint32_t>(file, name, message);
    }
}

auto MCScenario::Load(std::string_view scenarioName, std::string_view terrainName) -> int32_t
{
    NumCameraDrones = 0;
    // Port fix: the original freed the connect shapes with free() while the renderer still had them registered.
    const MCConnectShapeScope connectShape;
    _WaypointMarkers = LoadShapeFile("waypoints");

    // The original passed SYSTEM.CFG's ABL heap, stack, code block, module and static sizes (all gone) and ran
    // without debug info, debugger or profile log. Its debugger print callback did nothing.
    AblInit({.DebuggerPrint = [](std::string_view) {}});
    Turn = 0;

    // The objects placed now but brought into play later by the script.
    ScenarioObjectList = std::make_unique<MCObjectQueue>();
    CreatedParts.clear();

    UpdateDisplay(false, true, 100, true, 0);
    SoundSystem()->PlayStaticNoise();
    const auto [maxFiresBurning, maxFireBurnTime] = LoadGameSystem();

    // The scenario file (from the missions folder, or a saved game's copy).
    MCFitIniFile file;

    if (const int32_t result = OpenWithSaveFallback(file, MissionPath, scenarioName); result != 0)
    {
        Fatal(result, " could not open scenario file ");
    }

    if (file.SeekBlock("Planet") == 0)
    {
        if (const MCFitResult<int32_t> setting = file.Read<int32_t>("Setting"); setting)
        {
            CurPlanet = *setting;
        }
        else if (setting.error() == MCFitError::VariableNotFound)
        {
            CurPlanet = 0;
        }
    }

    if (CurPlanet == 1)
    {
        // On this planet overlay types 1-15 cost nothing to cross, at every move level.
        for (int32_t level = 0; level < NumMoveLevels; level++)
        {
            const auto first = OverlayWeightTable.begin() + level * OverlayWeightLevelSize + MapCellDim * MapCellDim;
            std::fill(first, first + 15 * MapCellDim * MapCellDim, 0);
        }
    }

    // Port-only (HD lighting): the time of day the map maker set the mission at; by day without one.
    MCSceneLights::SetMissionTime(MCSceneLights::ReadTimeOfDay(file));
    // Port-only (HD weather): the weather the map maker set it in; clear without one.
    MCWeather::SetMissionWeather(MCWeather::Read(file));
    LoadPalette(file);
    LoadForces(file);
    LoadSettings(file);
    LoadSensorContactShapes(file);

    if (const int32_t result = LoadSystems(file, maxFiresBurning, maxFireBurnTime); result != 0)
    {
        return result;
    }

    LoadTerrain(file, terrainName);
    LoadScript(file);
    LoadWarriors(file);
    LoadParts(file);
    LoadCarriers(file);
    LoadObjectives(file);
    LoadGroups(file);

    ClanTeam()->BuildRoster(*this);
    InnerSphereTeam()->BuildRoster(*this);

    if (AlliedTeam() != nullptr)
    {
        AlliedTeam()->BuildRoster(*this);
    }

    if (MultiPlayer() == nullptr)
    {
        HomeCommander()->SetNetPlayerId(0);
    }

    HomeCommander()->AddToGui(1);

    if (MultiPlayer() != nullptr)
    {
        for (int32_t i = 0; i < NumCommanders(); i++)
        {
            if (CommanderById(i) != HomeCommander())
            {
                CommanderById(i)->AddToGui(0);
            }
        }
    }

    ScenarioTime = 0.0f;
    // Port-only (HD particles, HD weather): none from the last mission, and their dice from the start.
    MCParticles::Clear();
    MCWeather::Clear();
    MissionStartTime = 0;
    RunningTime = 0.0f;
    ActualTime = 0.0f;
    // Port-only: the HD art pack's ground is read here, under the loading bar, rather than in the mission's first frame.
    MCTerrainReadHdTiles();
    UpdateDisplay(false, true, 30, true, 100);

    CameraList()->SetEye(CameraList()->ActivateAllReady());

    if (MultiPlayer() != nullptr)
    {
        Eye()->ChangeTarget(LockObject(MultiPlayer()->LocalMovers[0]), true);
    }

    // Port-only: the ground goes up to the GPU here too, under the loading bar, once the cameras have their terrain
    // windows, rather than in the mission's first frame.
    if (const std::expected<void, std::string> uploaded = MCTerrainUploadGround(); !uploaded)
    {
        Fatal(-1, std::format("The ground can't go up to the GPU: {}", uploaded.error()));
    }

    // The 'Mechs start with the damage their loadouts carried over.
    for (const std::shared_ptr<MCObjectList>& list : {InnerSphereMechList(), ClanMechList()})
    {
        for (std::shared_ptr<MCBaseObject> object : *list)
        {
            if (object->ObjectClass == MCObjectClass::BattleMech)
            {
                static_cast<MCBattleMech&>(*object).DamageLoadedComponents();
            }
        }
    }

    HasOutputBlock = file.SeekBlock("Output") == 0;
    file.Close();

    ScenarioEndTurn = -1;
    StartUpTurns = DefaultStartUpTurns;
    MusicPending = true;

    if (MultiPlayer() != nullptr)
    {
        MultiPlayer()->ChatCallback = ScenarioChatCallback;

        if (MultiPlayer()->IsServer != 0)
        {
            for (int32_t& checkedIn : MultiPlayer()->PlayerCheckedIn)
            {
                checkedIn = 0;
            }
        }

        MultiPlayer()->SendPlayerCheckIn();
    }

    StartingUp = true;
    StartUpCountdown = 100;
    _Loaded = true;
    return 0;
}

auto MCScenario::LoadGameSystem() -> std::pair<int32_t, float>
{
    MCFitIniFile file;

    if (const int32_t result = file.Open(GamePath(MissionPath, "gamesys", ".fit")); result != 0)
    {
        Fatal(result, " Could not open GameSys.Fit file ");
    }

    RequireFitBlock(file, "General", " Could not find General Block in GameSys ");
    MaxVisualRange = RequireFit<float>(file, "MaxVisualRange", " Could not find MaxVisualRange in GameSys ");
    MaxVisualRadius = MaxVisualRange * 1.4142f;
    FireVisualRange = RequireFit<float>(file, "FireVisualRange", " Could not find FireVisualRange in GameSys ");
    MaxWeaponRange = RequireFit<float>(file, "MaxWeaponRange", " Could not find MaxWeaponRange in GameSys ");

    if (const MCFitResult<uint32_t> ranges = file.ReadArray<float>("WeaponRange", std::span(WeaponRange, 3)); !ranges)
    {
        Fatal(std::to_underlying(ranges.error()), " Could not find WeaponRange in GameSys ");
    }

    DefaultAttackRange = OptionalFit<float>(file, "DefaultAttackRange", 75.0f);
    BaseSensorRange = RequireFit<float>(file, "BaseSensorRange", " Could not find BaseSensorRange in GameSys ");

    if (const MCFitResult<uint32_t> table = file.ReadArray<int32_t>("VisualRangeTable", VisualRangeTable); !table)
    {
        Fatal(std::to_underlying(table.error()), " Could not find Visual Range Table ");
    }

    UpdateDisplay(false, true, 30, true, 2);

    if (MasterComponentList.empty())
    {
        std::string componentName = GamePath(ObjectPath, "compbas", ".csv");
        const int32_t loaded =
            InitMasterComponentListExcel(componentName.data(), 0xff, MaxVisualRange / MaxWeaponRange, BaseSensorRange);
        Assert(loaded == 0, static_cast<uint32_t>(loaded), " Could not load compBas.csv ");
    }

    AlwaysRevealed = RequireFit<uint8_t>(file, "AlwaysRevealed", " Could not find AlwaysRevealed in GameSys ");
    GodMode = OptionalFit<uint8_t>(file, "GodMode", 0);
    ForceAlways = OptionalFit<uint8_t>(file, "AlwaysDraw", 0);
    DrawRevealedTacMap = OptionalFit<uint8_t>(file, "RevealTacMap", 0) != 0;
    FootPrints = OptionalFit<uint8_t>(file, "FootPrints", 1);
    BonusTonnageDivisor = RequireFit<int32_t>(file, "BonusTonnageDivisor", " No Tonnage divisor in GameSys ");
    BonusPointsPerTon = RequireFit<int32_t>(file, "BonusPointsPerTon", " No Bonus points per Ton in GameSys ");

    DifficultySettings.Load(file);
    Assert(LoadMoverGameSystem(file) == 0, 0, " could not load Mover System in GameSys ");
    Assert(LoadMultiplayerGameSystem(file) == 0, 0, " could not load Multiplayer System in GameSys ");
    Assert(LoadMechGameSystem(file) == 0, 0, " could not load Mech System in GameSys ");
    MechSalvageChance = DifficultySettings.Salvage(MechSalvageChance, GameDifficulty);
    Assert(LoadGroundVehicleGameSystem(file) == 0, 0, " could not load Ground Vehicle System in GameSys ");
    Assert(LoadElementalGameSystem(file) == 0, 0, " could not load Elemental System in GameSys ");

    RequireFitBlock(file, "Mine", " Could not find Mine Block in GameSys ");
    MineBaseDamage = RequireFit<float>(file, "BaseDamage", " Could not find Damage variable in Mine Block in GameSys ");
    MineSplashDamage =
        RequireFit<float>(file, "SplashDamage", " Could not find Splash Damage variable in Mine Block in GameSys ");
    MineSplashRange =
        RequireFit<float>(file, "SplashRange", " Could not find Splash Range variable in Mine Block in GameSys ");
    MineExplosion =
        RequireFit<int32_t>(file, "Explosion", " Could not find Explosion variable in Mine Block in GameSys ");
    MineLayThrottle = OptionalFit<int32_t>(file, "MineLayThrottle", 50);
    MineSweepThrottle = OptionalFit<int32_t>(file, "MineSweepThrottle", 50);
    MineWaitTime = RequireFit<float>(file, "MineWaitTime", " Could not find mine Wait time in Mine Block ");

    // The smoke sphere and shape budgets are required and not used: smokes take what they need (OB-152).
    RequireFitBlock(file, "Smoke", " Could not find Smoke Block in GameSys ");
    RequireFit<int32_t>(file, "MaxSmokeSpheres", " Could not find total Smoke Count in GameSys ");
    RequireFit<int32_t>(file, "TotalSmokeShapeSize", " Could not find total Smoke Shape Size in GameSys ");

    RequireFitBlock(file, "Fire", " Could not find Fire Block in GameSys ");
    const auto maxFiresBurning =
        RequireFit<int32_t>(file, "MaxFiresBurning", " COuld not find max fires burning in gameSys ");
    const auto maxFireBurnTime =
        RequireFit<float>(file, "MaxFireBurnTime", " COuld not find max fire burn time in gameSys ");
    UpdateDisplay(false, true, 30, true, 5);
    return {maxFiresBurning, maxFireBurnTime};
}

auto MCScenario::LoadPalette(MCFitIniFile& file) -> void
{
    RequireFitBlock(file, "PaletteSystem", " could not find PaletteSystem Block ");
    const auto paletteName =
        RequireFit<std::string>(file, "PaletteSystem", " could not find PaletteSystem in PaletteSystem Block ");

    // The mission's palette is shown until the scenario goes; the interface's comes back then.
    std::expected<std::unique_ptr<MCPalette>, std::string> palette = MCPalette::Create(paletteName);

    if (!palette)
    {
        Fatal(0, std::format(" could not start gamePalette: {} ", palette.error()));
    }

    OldPalette = MCGameContext::Current().SetPalette(std::move(*palette));
    InitAlphaLookup(GamePalette().Colors());
    GuiSystem().ActivatePalette(GamePalette().RgbData, 10, 0xf6);
    UpdateDisplay(false, true, 20, true, 7);
}

auto MCScenario::LoadForces(MCFitIniFile& file) -> void
{
    RequireFitBlock(file, "Teams", "Could not find Teams Block");
    const bool haveAlliedTeam = RequireFit<bool>(file, "AlliedTeam", " Could not find AlliedTeam in Teams Block ");

    // The clan, allied (when the scenario has one) and Inner Sphere teams.
    MCGameContext::Current().SetForces(std::make_unique<MCForces>(haveAlliedTeam));
    UpdateDisplay(false, true, 30, true, 10);

    RequireFitBlock(file, "Artillery", " could not find Artillery block in Scenario File ");
    const int32_t largeStrikes = OptionalFit<int32_t>(file, "NumLargeStrikes", 0);
    const int32_t smallStrikes = OptionalFit<int32_t>(file, "NumSmallStrikes", 0);
    const int32_t sensorStrikes = OptionalFit<int32_t>(file, "NumSensorStrikes", 0);
    const int32_t cameraStrikes = OptionalFit<int32_t>(file, "NumCameraStrikes", 0);
    MCForces& forces = Forces();

    if (MultiPlayer() == nullptr)
    {
        forces.PlayerTeam = InnerSphereTeam();
        forces.MakeCommanders(AlliedTeam() == nullptr ? 2 : 3);
        forces.PlayerCommander = CommanderById(0);
        CommanderById(0)->SetNumSmallStrikes(smallStrikes);
        HomeCommander()->SetNumLargeStrikes(largeStrikes);
        HomeCommander()->SetNumSensorStrikes(sensorStrikes);
        HomeCommander()->SetNumCameraDrones(cameraStrikes);
        CommanderById(1)->SetNumSmallStrikes(999);
        CommanderById(1)->SetNumLargeStrikes(999);
        CommanderById(1)->SetNumSensorStrikes(999);
        CommanderById(1)->SetNumCameraDrones(999);
    }
    else
    {
        forces.MakeCommanders(MCForces::MaxCommanders);

        if (MultiPlayer()->HomeTeam == 0)
        {
            forces.PlayerTeam = InnerSphereTeam();
        }
        else if (MultiPlayer()->HomeTeam == 1)
        {
            forces.PlayerTeam = ClanTeam();
        }
        else
        {
            Fatal(0, " Must Be Clan or InnerSphere in Multiplayer! ");
        }

        forces.PlayerCommander = CommanderById(MultiPlayer()->CheckInId);
    }

    UpdateDisplay(false, true, 30, true, 13);
}

auto MCScenario::LoadSettings(MCFitIniFile& file) -> void
{
    RequireFitBlock(file, "Music", " could not find Music block in Scenario File ");
    ScenarioTuneNum = RequireFit<uint8_t>(file, "scenarioTuneNum",
                                          " could not find ScenarioTuneNum in Music block in Scenario File ");

    RequireFitBlock(file, "GameScale", " could not find GameScale block in Scenario File ");
    WorldUnitsPerMeter = RequireFit<float>(file, "WorldUnitsPerMeter",
                                           " could not find worldUnitsperMeter in GameScale block in Scenario File ");
    MetersPerWorldUnit = RequireFit<float>(file, "MetersPerWorldUnit",
                                           " could not find MetersperWorldUnit in GameScale block in Scenario File ");
    // Required, not used.
    RequireUnused(file, "Duration", " could not find Duration in GameScale block in Scenario File ");
    CycleLength =
        RequireFit<float>(file, "CycleLength", " could not find CycleLength in GameScale block in Scenario File ");
    SingleStepMode = static_cast<int>(OptionalFit<uint32_t>(file, "SingleStep", 0));

    // A scenario may replace the game system file's table. OB-155 (fixed): a shorter table keeps the rest of the game
    // system's (the original copied whatever its stack held past the entries read).
    std::array<int32_t, VisualRangeTableSize> visualRanges = VisualRangeTable;

    if (file.ReadArray<int32_t>("VisualRangeTable", visualRanges).has_value())
    {
        VisualRangeTable = visualRanges;
    }

    // The frame's draw list. The ElementSystem block's ElementHeapSize, MaxElements and MaxGroups sized the original's;
    // the port's grows.
    MCGameContext::Current().SetElementList(std::make_unique<MCElementBuffer>());
    UpdateDisplay(false, true, 30, true, 15);
}

auto MCScenario::LoadSensorContactShapes(MCFitIniFile& file) -> void
{
    RequireFitBlock(file, "SensorContactShape", " could not find SensorContactShape block in Scenario File ");
    const auto shapeName = RequireFit<std::string>(
        file, "shapeName", " could not find ShapeName in SensorContactShape block in Scenario File ");
    MCPacketFile shapeFile;

    if (shapeFile.Open(GamePath(SpritePath, shapeName, ".pak")) != 0)
    {
        if (const int32_t result = shapeFile.Open(GamePath(CDspritePath, shapeName, ".pak")); result != 0)
        {
            Fatal(result, " could not open sensor shape file ");
        }
    }

    for (int32_t i = 0; i < SensorContactShapeCount; i++)
    {
        shapeFile.SeekPacket(i);
        const int32_t size = shapeFile.GetPacketSize();
        // An empty packet still fails, as it did when systemHeap's malloc(0) returned null.
        Assert(size > 0, 0, " no RAM for Large Sensor Shape ");
        MCRegisteredBlock& shape = _SensorContactShapes[static_cast<size_t>(i)];
        shape = MCRegisteredBlock(static_cast<size_t>(size), MCDataKind::Shapes);
        shapeFile.ReadPacket(i, shape.Bytes());
    }

    shapeFile.Close();
}

auto MCScenario::LoadSystems(MCFitIniFile& file, int32_t maxFiresBurning, float maxFireBurnTime) -> int32_t
{
    // The block's CraterShapeSize sized the original's shape heap.
    RequireFitBlock(file, "CraterSystem", " could not find CraterSystem Block in Scenario File ");
    const auto numCraters =
        RequireFit<int32_t>(file, "NumCraters", " could not find NumCraters in CraterSystem Block in Scenario File ");
    const auto craterFileName = RequireFit<std::string>(
        file, "CraterFile", " could not find CraterFile in CraterSystem Block in Scenario File ");

    {
        std::expected<std::unique_ptr<MCCraterManager>, std::string> craters =
            MCCraterManager::Create(numCraters, craterFileName);

        if (!craters)
        {
            Fatal(0, std::format(" could not Start CraterManager: {} ", craters.error()));
        }

        MCGameContext::Current().SetCraterManager(std::move(*craters));
    }

    RequireFitBlock(file, "CameraSystem", " could not Find CameraSystem Block ");
    RequireUnused(file, "CameraHeapSize", " could not Find CameraHeapSize in CameraSystem Block ");
    const auto cameraFileName =
        RequireFit<std::string>(file, "CameraFileName", " could not Find CameraFileName in CameraSystem Block ");
    MCGameContext::Current().SetCameraList(std::make_unique<MCCameraList>());

    if (std::expected<void, std::string> cameras = CameraList()->Load(cameraFileName); !cameras)
    {
        Fatal(0, std::format(" could start CameraSystem: {} ", cameras.error()));
    }

    UpdateDisplay(false, true, 30, true, 20);

    // The type and object heap sizes and NumObjects (the watchers' count, which the original ignored too) are
    // required, then ignored.
    RequireFitBlock(file, "ObjectSystem", " could not Find ObjectSystem Block ");
    RequireUnused(file, "ObjectHeapSize", " could not Find objectHeapSize in ObjectSystem Block ");
    RequireUnused(file, "ObjectTypeHeapSize", " could not Find ObjectTypeHeapSzize in ObjectSystem Block ");
    RequireUnused(file, "NumObjects", " could not Find NumObjects in ObjectSystem Block ");
    const auto objectFileName =
        RequireFit<std::string>(file, "ObjectFileName", " could not Find ObjectFileName in ObjectSystem Block ");
    MCObjectSystem::Start(objectFileName);

    // The original sized the sprite manager's heaps from the sizes here and in the SpriteManager block (still
    // required, then ignored).
    RequireFitBlock(file, "SpriteSystem", " could not Find SpriteSystem Block ");
    RequireUnused(file, "SpriteHeapSize", " could not Find SpriteHeapSize in SpriteSystem Block ");
    RequireUnused(file, "SpriteManagerHeapSize", " could not Find SpriteManagerHeapSize in SpriteSystem Block ");
    RequireUnused(file, "SpriteDataHeapSize", " could not Find SpriteDataHeapSize in SpriteSystem Block ");
    const auto spriteFileName =
        RequireFit<std::string>(file, "SpriteFileName", " could not Find SpriteFileName in SpriteSystem Block ");
    const auto shapeFileName =
        RequireFit<std::string>(file, "ShapeFileName", " could not Find ShapeFileName in SpriteSystem Block ");

    RequireFitBlock(file, "SpriteManager", " could not Find SpriteManager Block ");
    RequireUnused(file, "LegHeapSize", " could not Find LegHeapSize in SpriteManager Block ");
    RequireUnused(file, "TorsoHeapSize", " could not Find TorsoHeapSize in SpriteManager Block ");
    RequireUnused(file, "RightArmHeapSize", " could not Find RightArmHeapSize in SpriteManager Block ");
    RequireUnused(file, "LeftArmHeapSize", " could not Find LeftArmHeapSize in SpriteManager Block ");
    RequireUnused(file, "TotalMechs", " could not Find TotalMechs in SpriteManager Block ");

    std::expected<std::unique_ptr<MCSpriteManager>, std::string> spriteManager =
        MCSpriteManager::Create(shapeFileName, Use90PixelSprite != 0);

    if (!spriteManager)
    {
        Fatal(0, std::format(" could not Start SpriteManager: {} ", spriteManager.error()));
    }

    MCGameContext::Current().SetSpriteManager(std::move(*spriteManager));

    // The original went on without the sprite PAK (it tested the list, not the result); the port stops.
    std::expected<std::unique_ptr<MCAppearanceTypeList>, std::string> typeList =
        MCAppearanceTypeList::Create(spriteFileName);

    if (!typeList)
    {
        Fatal(0, std::format(" could not start AppearanceList: {} ", typeList.error()));
    }

    MCGameContext::Current().SetAppearanceTypeList(std::move(*typeList));

    std::expected<std::unique_ptr<MCContactSystem>, std::string> contacts = MCContactSystem::Create(file);

    if (!contacts)
    {
        Fatal(0, std::format(" could not start PotentialContactManager: {} ", contacts.error()));
    }

    MCGameContext::Current().SetContactSystem(std::move(*contacts));
    UpdateDisplay(false, true, 20, true, 25);

    std::expected<std::unique_ptr<MCEffectSystem>, MCFitError> effects =
        MCEffectSystem::Create(file, maxFiresBurning, maxFireBurnTime);

    if (!effects)
    {
        return std::to_underlying(effects.error());
    }

    MCGameContext::Current().SetEffectSystem(std::move(*effects));
    UpdateDisplay(false, true, 30, true, 35);

    std::expected<std::unique_ptr<MCCollisionSystem>, std::string> collisions = MCCollisionSystem::Create(file);

    if (!collisions)
    {
        Fatal(0, std::format(" could not start Collision System: {} ", collisions.error()));
    }

    MCGameContext::Current().SetCollisionSystem(std::move(*collisions));
    UpdateDisplay(false, true, 30, true, 37);
    return 0;
}

auto MCScenario::LoadTerrain(MCFitIniFile& file, std::string_view terrainName) -> void
{
    if (terrainName.empty())
    {
        RequireFitBlock(file, "TerrainSystem", " could not find TerrainSystem block ");
        const auto terrainFileName =
            RequireFit<std::string>(file, "TerrainFileName", " could not find TerrainFileName in TerrainSystem block ");
        InstallTerrain(terrainFileName);
        UpdateDisplay(false, true, 30, true, 50);

        // The movement maps: the terrain's .dat (the scenario map) and .gmm (the global map).
        MCFile mapFile;

        if (const int32_t result = mapFile.Open(GamePath(TerrainPath, terrainFileName, ".dat")); result != 0)
        {
            Fatal(result, " could not start Game Map ");
        }

        MCFile globalMapFile;

        if (const int32_t result = globalMapFile.Open(GamePath(TerrainPath, terrainFileName, ".gmm")); result != 0)
        {
            Fatal(result, " Could not open global Map ");
        }

        std::expected<std::unique_ptr<MCMoveSystem>, std::string> moveSystem =
            MCMoveSystem::Load(mapFile, globalMapFile, SimpleMovePathRange * 2 + 1);

        // A mission whose maps can't be read can't be played.
        if (!moveSystem)
        {
            Fatal(0, " could not start Game Map: ", moveSystem.error());
        }

        MCGameContext::Current().SetMoveSystem(std::move(*moveSystem));
        UpdateDisplay(false, true, 30, true, 60);
        UpdateDisplay(false, true, 30, true, 65);
        Terrain()->UpdateAllObjects();
        UpdateDisplay(false, true, 30, true, 70);
    }
    else
    {
        InstallTerrain(MCFileSystem::ToUtf8(MCFileSystem::FromUtf8(terrainName).stem()));
    }

    // Read from whichever block is current (the TerrainSystem block's, on the game's path).
    if (const MCFitResult<std::string> tacMapGifName = file.Read<std::string>("TacMapGifName"); tacMapGifName)
    {
        TacticalMap()->SetRevealedBitmap(tacMapGifName->c_str());
    }
}

auto MCScenario::LoadScript(MCFitIniFile& file) -> void
{
    if (file.SeekBlock("ABLibraries") == 0)
    {
        for (int32_t libraryNumber = 0;; libraryNumber++)
        {
            const MCFitResult<std::string> libraryName =
                file.Read<std::string>(std::format("Library{}", libraryNumber));

            if (!libraryName)
            {
                break;
            }

            if (AblLoadLibrary(GamePath(MissionPath, *libraryName, ".abx")) != 0)
            {
                Fatal(0, std::format(" Cannot load ABL Library {} ", *libraryName));
            }
        }
    }

    UpdateDisplay(false, true, 30, true, 73);

    RequireFitBlock(file, "Script", " could not find Script Block ");
    ScenarioScript = RequireFit<std::string>(file, "ScenarioScript", " could not find ScenarioScript in Script Block ");
    const std::string windowTitle = std::format("{} - {}", std::string_view(AppName), ScenarioScript);

    // Port: SetWindowTextA -> the SDL window's title.
    if (const std::shared_ptr<MCDisplay> display = MCInput::Display())
    {
        display->SetTitle(windowTitle.c_str());
    }

    WindowTitle = windowTitle;
    UpdateDisplay(false, true, 30, true, 75);

    ScenarioScriptHandle = AblPreProcess(GamePath(MissionPath, ScenarioScript, ".abl"));
    Assert(-1 < ScenarioScriptHandle, static_cast<uint32_t>(ScenarioScriptHandle), " Bad Scenario Script ");
    ScenarioBrain = MCAblModule::Create(ScenarioScriptHandle);
    ScenarioBrain->SetName("Scenario");
    ScenarioBrain->Step = true;
    ScenarioBrainParams = MCAblParam{};
    ScenarioBrainHandleMessage = ScenarioBrain->FindFunction("handlemessage", true);
}

auto MCScenario::LoadWarriors(MCFitIniFile& file) -> void
{
    const auto readCaptureChance = [this, &file]
    {
        const MCFitResult<uint8_t> chance = file.Read<uint8_t>("CaptureChance");
        CaptureChance = chance && *chance <= 4 ? *chance : 2;
    };

    RequireFitBlock(file, "Warriors", " Could not find Warriors Block ");
    readCaptureChance();
    const auto numWarriors =
        RequireFit<uint32_t>(file, "NumWarriors", " Could not find NumWarriors in Warriors Block ");
    const MCFitResult<std::string> brainParameterFile = file.Read<std::string>("BrainParameterFile");
    // Read twice in the original.
    readCaptureChance();
    UpdateDisplay(false, true, 30, true, 77);

    NumMarines = 0;
    _Warriors.clear();

    if (numWarriors != 0)
    {
        _Warriors.resize(numWarriors + 1);
    }

    for (uint32_t i = 1; i <= numWarriors; i++)
    {
        RequireFitBlock(file, std::format("Warrior{}", i), " Could not find Warrior Number Block ");
        const auto profileName =
            RequireFit<std::string>(file, "Profile", " Could not find Warrior Profile in Warrior Number Block ");
        auto& warrior = _Warriors[i];
        warrior = std::make_unique<MCMechWarrior>();

        // A saved game keeps its warriors' profiles in the temporary save folder.
        MCFitIniFile profileFile;
        int32_t result = OpenWithSaveFallback(profileFile, WarriorPath, profileName);
        Assert(result == 0, static_cast<uint32_t>(result), " Could not open Warrior Profile File ");
        result = warrior->Load(profileFile);
        Assert(result == 0, static_cast<uint32_t>(result), " Could not load Warrior Profile ");

        warrior->Index = static_cast<int32_t>(i);
        const auto brainName =
            RequireFit<std::string>(file, "Brain", " Could not find Warrior Brain in Warrior Number Block ");
        warrior->SetBrainName(brainName.c_str());
        const int32_t brainHandle = AblPreProcess(GamePath(WarriorPath, brainName, ".abl"));
        Assert(-1 < brainHandle, static_cast<uint32_t>(brainHandle), " Could not start Warrior Brain ");
        result = warrior->SetBrain(brainHandle);
        Assert(result == 0, static_cast<uint32_t>(result), " Could Not Set Brain ");
        warrior->NotMineYet = OptionalFit<bool>(file, "NotMineYet", false) ? 1 : 0;
    }

    if (brainParameterFile)
    {
        MCFitIniFile parameterFile;
        const int32_t result = parameterFile.Open(GamePath(WarriorPath, *brainParameterFile, ".fit"));
        Assert(result == 0, static_cast<uint32_t>(result), " Could not open Brain Parameter File ");

        for (uint32_t i = 1; i <= numWarriors; i++)
        {
            _Warriors[i]->LoadBrainParameters(parameterFile, static_cast<int32_t>(i));
        }
    }

    UpdateDisplay(false, true, 30, true, 80);
}

auto MCScenario::LoadParts(MCFitIniFile& file) -> void
{
    RequireFitBlock(file, "Parts", " Could not find Parts Block ");
    const auto numParts = RequireFit<uint32_t>(file, "NumParts", " Could not find NumParts in Parts Block ");
    MoverRoster.fill({});
    Parts.clear();

    if (numParts != 0)
    {
        Parts.resize(numParts + 1);
    }

    for (int32_t i = 1; i <= static_cast<int32_t>(numParts); i++)
    {
        UpdateDisplay(false, true, 30, true,
                      static_cast<int32_t>(static_cast<double>(i) / static_cast<int32_t>(numParts) * 10.0 + 80.0));
        Parts[static_cast<size_t>(i)] = ReadScenarioPart(file, i);
        CreatePartObject(i);
    }

    UpdateDisplay(false, true, 20, true, 90);
}

auto MCScenario::LoadCarriers(MCFitIniFile& file) -> void
{
    MCGameContext::Current().SetTrainManager(nullptr);

    if (file.SeekBlock("Trains") == 0)
    {
        MCGameContext::Current().SetTrainManager(std::make_unique<MCTrainManager>());
        const auto numTrains = RequireFit<int32_t>(file, "NumTrains", " Could not find number of trains");

        for (int32_t trainNumber = 0; trainNumber < numTrains; trainNumber++)
        {
            RequireFitBlock(file, std::format("Train{}", trainNumber), " Could not find train block");
            const auto numCars = RequireFit<int32_t>(file, "NumCars", " Could not find number of cars in train block");
            Assert(0 < numCars, 0, " Need at least one car in train...");
            const std::shared_ptr<MCTrain> train = TrainManager().CreateTrain();

            for (int32_t carNumber = 0; carNumber < numCars; carNumber++)
            {
                const auto carPart =
                    RequireFit<int32_t>(file, std::format("Car{}", carNumber), " Could not find a car in train block");
                const auto car = ReferencedPart<MCTrainCar>(*this, carPart, "train car", [](const MCTrainCar& found)
                                                            { return found.ObjectClass == MCObjectClass::TrainCar; });
                const MCPart& part = Parts[static_cast<size_t>(carPart)];
                train->AddCar(*car);
                car->SetPartId(trainNumber, carNumber);

                if (carNumber == 0)
                {
                    // The lead car's part sets the train's speed (clamped to its top speed) and direction.
                    if (std::fabs(part.Velocity) <= train->MaxSpeed)
                    {
                        train->DesiredSpeed = part.Velocity;
                    }
                    else
                    {
                        train->DesiredSpeed = part.Velocity <= 0.0f ? -train->MaxSpeed : train->MaxSpeed;
                    }

                    if (part.Rotation != 45.0f && part.Rotation != -45.0f && part.Rotation != 135.0f &&
                        part.Rotation != -135.0f)
                    {
                        Fatal(static_cast<int32_t>(part.Rotation),
                              " Train Rotation Invalid.  (must be 45,-45,135,-135) ");
                    }

                    train->TrackDirection = static_cast<int32_t>(part.Rotation);
                }
            }
        }
    }

    UpdateDisplay(false, true, 20, true, 92);

    if (file.SeekBlock("Elemental Carriers") == 0)
    {
        const auto numCarriers = RequireFit<int32_t>(file, "Carriers", " Could not find number of carriers");

        for (int32_t carrierNumber = 0; carrierNumber < numCarriers; carrierNumber++)
        {
            RequireFitBlock(file, std::format("ECarrier{}", carrierNumber), " Could not find carrier block");
            const auto partNumber = RequireFit<int32_t>(file, "Carrier", " Could not read carrier in carrier block");
            const auto carrier = ReferencedPart<MCGroundVehicle>(
                *this, partNumber, "elemental carrier", [](const MCGroundVehicle& found)
                { return found.ObjectClass == MCObjectClass::GroundVehicle && found.ElementalCarrier != 0; });

            for (int32_t i = 0; i < MCGroundVehicle::MaxElementals; i++)
            {
                const MCFitResult<int32_t> elementalPart = file.Read<int32_t>(std::format("Elemental{}", i));

                if (!elementalPart)
                {
                    break;
                }

                const auto elemental =
                    ReferencedPart<MCElemental>(*this, *elementalPart, "carried elemental", [](const MCElemental& found)
                                                { return found.ObjectClass == MCObjectClass::Elemental; });
                carrier->Elementals[i] = elemental;
                elemental->Transport = carrier;
            }
        }
    }

    UpdateDisplay(false, true, 20, true, 93);

    if (file.SeekBlock("BusBlock") == 0)
    {
        const auto numBuses = RequireFit<int32_t>(file, "Buses", " Could not find number of buses");

        for (int32_t busNumber = 0; busNumber < numBuses; busNumber++)
        {
            RequireFitBlock(file, std::format("Bus{}", busNumber), " Could not find bus block");
            const auto busPart = RequireFit<int32_t>(file, "Bus", " Could not read carrier in carrier block");
            const auto bus =
                ReferencedPart<MCGroundVehicle>(*this, busPart, "bus", [](const MCGroundVehicle& found)
                                                { return found.ObjectClass == MCObjectClass::GroundVehicle; });

            for (int32_t seat = 0; seat < MaxGroundVehicleSeats && seat < static_cast<int32_t>(bus->Seats); seat++)
            {
                const MCFitResult<int32_t> passenger = file.Read<int32_t>(std::format("Passenger{}", seat));

                if (!passenger)
                {
                    break;
                }

                Assert(*passenger <= static_cast<int32_t>(NumWarriors()), 0, "Illegal passenger");
                bus->Passengers[seat] = Warrior(static_cast<uint32_t>(*passenger));
            }
        }
    }
}

auto MCScenario::LoadObjectives(MCFitIniFile& file) -> void
{
    RequireFitBlock(file, "Objectives", " Could not find Objective Block ");
    TimeLimit = OptionalFit<int32_t>(file, "TimeLeft", -1);
    TwoMinuteWarningPlayed = false;
    ThirtySecondWarningPlayed = false;
    const auto numObjectives =
        RequireFit<uint32_t>(file, "NumObjectives", " Could not find numObjectives in Objective Block ");
    Assert(numObjectives < MCObjectiveList::MaxObjectives + 1, numObjectives, " Too Many Objectives ");

    if (MultiPlayer() == nullptr)
    {
        InnerSphereTeam()->FirstObjective = 0;
        InnerSphereTeam()->NumObjectives = numObjectives;
    }
    else
    {
        const auto innerSphere = RequireFit<uint32_t>(file, "NumInnerSphereObjectives",
                                                      " Could not find NumInnerSphereObjectives in Objective Block ");
        const auto clan =
            RequireFit<uint32_t>(file, "NumClanObjectives", " Could not find NumClanObjectives in Objective Block ");
        Assert(innerSphere + clan == numObjectives, 0, " Incorrect # of objectives ");
        InnerSphereTeam()->FirstObjective = 0;
        InnerSphereTeam()->NumObjectives = innerSphere;
        ClanTeam()->FirstObjective = static_cast<int32_t>(innerSphere);
        ClanTeam()->NumObjectives = clan;
    }

    for (uint32_t i = 0; i < numObjectives; i++)
    {
        UpdateDisplay(false, true, 20, true,
                      static_cast<int32_t>(static_cast<double>(i) / static_cast<int32_t>(numObjectives) * 5.0 + 93.0));
    }

    // A mission whose objectives can't be read can't be played.
    if (const std::expected<void, std::string> loaded = Objectives.Load(file, numObjectives); !loaded)
    {
        Fatal(0, " Bad mission objectives: ", loaded.error());
    }
}

auto MCScenario::LoadGroups(MCFitIniFile& file) -> void
{
    // Each commander's support strikes and groups ("Commander%dGroup:%d": the part numbers of its mates).
    for (int32_t commanderId = 0; commanderId < NumCommanders(); commanderId++)
    {
        std::shared_ptr<MCCommander> commander = CommanderById(commanderId);
        UpdateDisplay(false, true, 30, true, 98);

        if (file.SeekBlock(std::format("Commander:{}", commanderId)) == 0)
        {
            commander->SetNumSmallStrikes(OptionalFit<int32_t>(file, "NumSmallStrikes", 0));
            commander->SetNumLargeStrikes(OptionalFit<int32_t>(file, "NumLargeStrikes", 0));
            commander->SetNumSensorStrikes(OptionalFit<int32_t>(file, "NumSensorStrikes", 0));
            commander->SetNumCameraDrones(OptionalFit<int32_t>(file, "NumCameraDrones", 0));
        }

        // A commander has 32 groups; a mission's further ones are refused (issue #25: the original went past them).
        for (int32_t groupId = 0; file.SeekBlock(std::format("Commander{}Group:{}", commanderId, groupId)) == 0;
             groupId++)
        {
            if (groupId >= MCCommander::MaxGroups)
            {
                Fatal(groupId, std::format(" The mission gives commander {} more than {} groups ", commanderId,
                                           MCCommander::MaxGroups));
            }

            bool pointChosen = false;
            std::array<int32_t, MCMoverGroup::MaxMovers> mates{};

            if (const MCFitResult<uint32_t> result = file.ReadArray<int32_t>("Mates", mates); !result)
            {
                Fatal(std::to_underlying(result.error()), " could not find Mates in Group in Scenario File ");
            }

            for (int32_t i = 0; i < MCMoverGroup::MaxMovers; i++)
            {
                if (mates[i] <= 0)
                {
                    continue;
                }

                const auto mateMover = ReferencedPart<MCMover>(*this, mates[i], "group mate", [](const MCMover& found)
                                                               { return IsMoverClass(found.ObjectClass); });
                MCPart& mate = Parts[static_cast<size_t>(mates[i])];
                const int32_t partId = 0x200 + commanderId * 0x180 + groupId * MCMoverGroup::MaxMovers + i;
                mateMover->SetPartId(partId);

                if (mate.Exists == 0)
                {
                    CreatedParts.push_back({partId, false});
                }

                commander->GetGroup(groupId)->Add(mateMover);

                if (!pointChosen)
                {
                    commander->GetGroup(groupId)->SelectPoint(1);
                    pointChosen = true;
                    commander->SetTeam(commander->GetGroup(groupId)->GetPoint()->GetTeam());
                }
            }

            if (MultiPlayer() == nullptr && commanderId == 1)
            {
                CommanderById(1)->GetGroup(groupId)->SetDisbandOnNoPoint(0);
            }
        }
    }
}

auto MCScenario::CreatePartObject(int32_t partNumber) -> void
{
    MCPart& part = Parts[static_cast<size_t>(partNumber)];

    if (part.Destroyed || part.Created)
    {
        return;
    }

    const std::shared_ptr<MCGameObject> object = CreateObject(static_cast<int32_t>(part.ObjNumber));
    part.Object = object;
    part.Created = object != nullptr;

    if (object == nullptr)
    {
        Fatal(static_cast<int32_t>(part.ObjNumber),
              std::format(" Couldnt create object number {}  which is part Number {}", part.ObjNumber, partNumber));
    }

    object->SetAwake(part.Active);

    if (part.ProfileName != "NONE")
    {
        MCFitIniFile profileFile;

        if (const int32_t result = OpenWithSaveFallback(profileFile, ProfilePath, part.ProfileName); result != 0)
        {
            Fatal(result);
        }

        if (object->LoadProfile(profileFile) != 0)
        {
            Fatal(static_cast<int32_t>(0xfaaf0007), " Bad Profile File ");
        }
    }

    std::shared_ptr<MCTeam> team;

    if (part.TeamId == 0)
    {
        team = InnerSphereTeam();
    }
    else if (part.TeamId == 1)
    {
        team = ClanTeam();
    }
    else if (part.TeamId == 2)
    {
        team = AlliedTeam();
    }

    const std::shared_ptr<MCMechWarrior> pilot = Warrior(part.Pilot);
    const MCObjectClass objectClass = object->ObjectClass;

    if (objectClass == MCObjectClass::BattleMech)
    {
        const auto mech = std::static_pointer_cast<MCBattleMech>(object);
        mech->SetPilot(pilot);
        mech->SetTeam(team);
        mech->CalcWeaponEffectiveness(1);
        mech->CalcWeaponEffectiveness(0);
        mech->CalcWeaponRangeRatings();
        mech->Captureable = part.Captureable;
        const int32_t paintScheme = (part.PaintScheme == -1) ? pilot->PaintScheme : part.PaintScheme;
        static_cast<MCMechActor*>(mech->Appearance.get())->FadeTableIndex = paintScheme;
    }
    else if (objectClass == MCObjectClass::GroundVehicle || objectClass == MCObjectClass::Elemental)
    {
        const auto mover = std::static_pointer_cast<MCMover>(object);
        mover->SetPilot(pilot);
        mover->SetTeam(team);
        mover->CalcWeaponRangeRatings();

        // The original tests +0x8b8 of both: a vehicle's gvAppearance flag, and an elemental's field there, which
        // MCX.EXE only ever sets to 0.
        if (objectClass == MCObjectClass::GroundVehicle && static_cast<MCGroundVehicle&>(*mover).GvAppearance != 0)
        {
            static_cast<MCGVAppearance*>(mover->Appearance.get())->FadeTableIndex =
                (part.PaintScheme == -1) ? pilot->PaintScheme : part.PaintScheme;
        }
    }

    object->SetControl(part.ControlType, part.ControlDataType, -1);
    MCVector3D position(part.Position[0], part.Position[1], part.Position[2]);
    object->SetPosition(position);

    if (objectClass == MCObjectClass::BattleMech || objectClass == MCObjectClass::GroundVehicle ||
        objectClass == MCObjectClass::Elemental || objectClass == MCObjectClass::Mover)
    {
        static_cast<MCMover&>(*object).SetLastValidPosition(position);
    }

    MCFrameOfRef frame;
    frame.ResetToWorldFrame();
    const double radians = part.Rotation * DegreesToRadians;
    RotateAboutK(frame, static_cast<float>(std::sin(radians)), static_cast<float>(std::cos(radians)));
    object->SetFrame(frame);

    if (objectClass == MCObjectClass::BattleMech)
    {
        auto actor = std::static_pointer_cast<MCMechActor>(object->GetAppearance());

        if (actor != nullptr)
        {
            actor->SetGesture(part.GestureId);
        }

        if (part.Alignment == HomeTeam()->Alignment)
        {
            actor->PreloadGestures();
        }
    }
    else if (objectClass == MCObjectClass::Elemental)
    {
        auto actor = std::static_pointer_cast<MCElementalActor>(object->GetAppearance());

        if (actor != nullptr)
        {
            actor->SetGesture(part.GestureId);
        }
    }

    // The part number is kept in the object's id.
    object->IdNumber = static_cast<uint32_t>(partNumber);

    if (MultiPlayer() != nullptr)
    {
        MultiPlayer()->AddToMoverRoster(std::static_pointer_cast<MCMover>(object));
        MultiPlayer()->AddToPlayerMoverRoster(part.CommanderId, std::static_pointer_cast<MCMover>(object));

        if (part.CommanderId == MultiPlayer()->CheckInId)
        {
            MultiPlayer()->AddToLocalMovers(std::static_pointer_cast<MCMover>(object));
        }
    }

    if (part.Exists == 0)
    {
        // Not in play yet: the script brings it in with CreateScenarioObject.
        object->SetCommanderId(part.CommanderId);
        object->SetAlignment(part.Alignment);
        ScenarioObjectList->DefaultList().Add(object);
        return;
    }

    if (objectClass < MCObjectClass::BattleMech || MCObjectClass::Elemental < objectClass)
    {
        object->SetExists(1);
        ObjectList()->DefaultList().Add(object);
    }
    else
    {
        object->SetCommanderId(part.CommanderId);
        object->SetAlignment(part.Alignment);
        std::shared_ptr<MCObjectList> list = (part.Alignment == -1) ? ClanMechList() : InnerSphereMechList();

        if (list != nullptr)
        {
            list->Add(object);
        }

        object->SetPotentialContact(objectClass == MCObjectClass::Elemental ? 2 : 1);
        object->SetExists(1);
    }

    GameObjectMap().AddObject(*object);
}

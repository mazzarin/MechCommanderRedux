#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fakes/MCManualClock.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiSmackerWindow.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCUpdateDisplay.h"
#include "lib/MCFastFileSet.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGameList.h"
#include "logistics/MCLogComboBox.h"
#include "logistics/MCLogInvScreen.h"
#include "logistics/MCCompPurchaseBlock.h"
#include "logistics/MCLogSlider.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearanceTypeList.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "object/MCBattleMech.h"
#include "sprite/MCSpriteTree.h"
#include "main/MCSystemConfig.h"
#include "main/MCLogMech.h"
#include "main/MCLogMechList.h"
#include "main/MCLogistics.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCMechWarrior.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"

namespace
{
    /// <summary>
    /// A failed SDL_assert prints what failed and ends the process (exit 42), so the test fails. SDL's own handler
    /// would open a prompt that nobody answers in a headless run, and its abort shows the Debug CRT's abort() box.
    /// </summary>
    SDL_AssertState SDLCALL FailOnAssert(const SDL_AssertData* data, void*)
    {
        std::cout << std::format("  {}({}): assertion failed in {}: {}\n", data->filename, data->linenum,
                                 data->function, data->condition)
                  << std::flush;
        std::_Exit(42);
    }

    /// <summary>Installs <see cref="FailOnAssert"/> before any test runs.</summary>
    const bool AssertHandlerInstalled = []
    {
        SDL_SetAssertionHandler(FailOnAssert, nullptr);
        return true;
    }();
}

namespace MCTestGame
{
    std::function<void()> OnPresent;
    MCBootOptions BootOptions;

    void BootInLargeWindow(int uiResolution, int interfaceSize)
    {
        BootOptions.WindowWidth = LargeWindowWidth;
        BootOptions.WindowHeight = LargeWindowHeight;
        BootOptions.FixedZoomHeight = 0.0f;
        BootOptions.Started = [uiResolution, interfaceSize]
        {
            GUiResolution = uiResolution;
            GInterfaceSize = interfaceSize;
        };
    }

    namespace
    {
        /// <summary>The folder of the user folder's data folder that PinnedSaves makes the save folder.</summary>
        constexpr std::string_view PinnedSaveFolder = "mc_tests_saves";

        /// <summary>The extension of a campaign's saves.</summary>
        constexpr std::string_view SaveExtension = ".sav";

        /// <summary>The pinned save folder on disk.</summary>
        std::filesystem::path PinnedSaveFolderPath()
        {
            return UserFolder() / "data" / PinnedSaveFolder;
        }

        /// <summary>
        /// Makes <paramref name="folder"/> the save folder: SavePath, and the folder the file panes of logistics' screens
        /// list (each took SavePath as the boot made it).
        /// </summary>
        void SetSaveFolder(const std::string& folder)
        {
            SavePath = folder;
            const std::shared_ptr<MCLogistics> logistics = GlobalLog();

            if (logistics == nullptr)
            {
                return;
            }

            for (const std::shared_ptr<MCSplashScreen>& screen :
                 {logistics->LoadScreen.Lock(), logistics->SaveScreen.Lock(), logistics->MultiplayerScreen.Lock()})
            {
                if (screen != nullptr && screen->FilePane != nullptr)
                {
                    screen->FilePane->StartDirectory = folder;
                }
            }
        }
    }

    std::filesystem::path UserFolder()
    {
        return std::filesystem::temp_directory_path() / "mc_tests_user";
    }

    PinnedSaves::PinnedSaves(std::span<const std::string_view> names) : _Kept(SavePath)
    {
        const std::filesystem::path folder = PinnedSaveFolderPath();
        std::filesystem::remove_all(folder);
        std::filesystem::create_directories(folder);
        const std::string pinned = std::format("data\\{}\\", PinnedSaveFolder);

        // The save folder also holds what the game ships there (campaign starts, solo missions, multiplayer maps):
        // copies of all of it but the saves.
        for (const std::string& file : MCFileSystem::FindFiles(GamePath(_Kept, "*")))
        {
            if (MCPort::StrICmp(std::filesystem::path(file).extension().string(), SaveExtension) != 0)
            {
                MCTest::Scope scope(std::format("pinning the saves: copying {}", file));
                REQUIRE(MCFileSystem::CopyFile(_Kept + file, pinned + file));
            }
        }

        for (const std::string_view name : names)
        {
            std::ofstream(folder / std::format("{}{}", name, SaveExtension));
        }

        SetSaveFolder(pinned);
    }

    PinnedSaves::~PinnedSaves()
    {
        SetSaveFolder(_Kept);
        std::error_code error;
        std::filesystem::remove_all(PinnedSaveFolderPath(), error);

        if (error)
        {
            std::cout << "  can't remove the pinned saves' folder: " << error.message() << "\n";
        }
    }

    SaveGameCleanup::SaveGameCleanup() : _Folder(UserFolder() / "data" / "savegame")
    {
        for (const std::filesystem::path& file : Files())
        {
            _Before.insert(file);
        }
    }

    SaveGameCleanup::~SaveGameCleanup()
    {
        for (const std::filesystem::path& file : Files())
        {
            std::error_code error;

            if (!_Before.contains(file) && !std::filesystem::remove(file, error) && error)
            {
                std::cout << "  can't remove the save " << file.string() << ": " << error.message() << "\n";
            }
        }
    }

    std::vector<std::filesystem::path> SaveGameCleanup::Files() const
    {
        std::vector<std::filesystem::path> files;
        // No save folder yet (before the game first saves) is no files.
        std::error_code error;

        for (const auto& entry : std::filesystem::directory_iterator(_Folder, error))
        {
            if (entry.is_regular_file())
            {
                files.push_back(entry.path());
            }
        }

        return files;
    }

    namespace
    {
        /// <summary>
        /// The log file named by option <paramref name="option"/>, opened for appending with a <c>== &lt;test&gt;</c>
        /// line first (each isolated test runs in a process of its own and adds its section), or a closed stream
        /// without the option.
        /// </summary>
        std::ofstream OpenLog(std::string_view option)
        {
            const std::optional<std::string_view> name = MCTest::Option(option);

            if (!name)
            {
                return {};
            }

            std::ofstream log(MCFileSystem::FromUtf8(*name), std::ios::app);

            if (log.is_open())
            {
                log << "== " << MCTest::CurrentTestName() << "\n";
            }

            return log;
        }

        /// <summary>Every present: the --present-log line, the --present-shot screenshot, then <see cref="OnPresent"/>.</summary>
        void Presented()
        {
            static std::ofstream presentLog = OpenLog("present-log");
            static const std::string shotsWanted = []
            {
                const std::optional<std::string_view> shotList = MCTest::Option("present-shot");
                return shotList ? std::format(",{},", *shotList) : std::string();
            }();
            static int32_t presentIndex = 0;

            if (presentLog.is_open())
            {
                presentLog << std::format("{} 0x{:08x}\n", presentIndex, MCScreenInput::ScreenHash()) << std::flush;
            }

            const std::optional<std::string_view> shots = MCTest::Option("shots");

            if (shots && shotsWanted.contains(std::format(",{},", presentIndex)))
            {
                static_cast<void>(MCInput::Display()->SaveScreenshot(MCFileSystem::FromUtf8(*shots) /
                                                                     std::format("present{}.bmp", presentIndex)));
            }

            presentIndex++;

            if (OnPresent)
            {
                OnPresent();
            }
        }

        /// <summary>Folds <paramref name="value"/>'s bytes into the FNV-1a hash <paramref name="hash"/>.</summary>
        template <typename T> void Fold(uint32_t& hash, const T& value)
        {
            const auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(value);

            for (const uint8_t byte : bytes)
            {
                hash = (hash ^ byte) * 0x01000193;
            }
        }

        /// <summary>Folds a vector's three floats into <paramref name="hash"/>.</summary>
        void FoldVector(uint32_t& hash, const MCVector3D& vector)
        {
            Fold(hash, vector.X);
            Fold(hash, vector.Y);
            Fold(hash, vector.Z);
        }
    }

    uint32_t StateHash()
    {
        uint32_t hash = 0x811c9dc5;
        Fold(hash, Turn);
        Fold(hash, ScenarioTime);
        Fold(hash, MCPort::RandState());

        if (Scenario() == nullptr)
        {
            return hash;
        }

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; partId++)
        {
            std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover == nullptr)
            {
                continue;
            }

            Fold(hash, partId);
            FoldVector(hash, mover->Position);
            FoldVector(hash, mover->Frame.I);
            FoldVector(hash, mover->Frame.J);
            FoldVector(hash, mover->Frame.K);
            Fold(hash, mover->Status);

            for (int32_t i = 0; i < mover->NumBodyLocations(); i++)
            {
                Fold(hash, mover->Body[i].CurInternalStructure);
                Fold(hash, mover->Body[i].DamageState);
            }

            for (int32_t i = 0; i < mover->NumArmorLocations(); i++)
            {
                Fold(hash, mover->Armor[i].CurArmor);
            }

            if (std::shared_ptr<MCMechWarrior> pilot = mover->GetPilot(); pilot != nullptr)
            {
                Fold(hash, pilot->Status);
                Fold(hash, pilot->OrderState);
                Fold(hash, pilot->CurTacOrder.Id);
                Fold(hash, pilot->CurTacOrder.Code);
            }
        }

        for (int32_t i = 0; i < Scenario()->Objectives.Count(); i++)
        {
            Fold(hash, Scenario()->Objectives[i].Status);
        }

        return hash;
    }

    bool Available()
    {
        static const bool available = []
        {
            // --game <folder>, else the current folder when it holds the game (as the game looks for SYSTEM.CFG).
            const std::optional<std::string_view> option = MCTest::Option("game");
            const std::filesystem::path root =
                option ? MCFileSystem::FromUtf8(*option) : std::filesystem::current_path();

            if (!std::filesystem::is_regular_file(root / "SYSTEM.CFG"))
            {
                std::cout << "  (skipped: pass --game <the MechCommander Gold install>)\n";
                return false;
            }

            MCFileSystem::SetGameRoot(root);
            return true;
        }();
        return available;
    }

    void OpenFastFiles()
    {
        static bool opened = false;

        if (opened)
        {
            return;
        }

        opened = true;
        MCFastFileSet& fastFiles = MCGameContext::Current().FastFiles();

        for (const std::string_view name : {"art.fst", "mission.fst", "misc.fst", "terrain.fst", "shapes.fst"})
        {
            static_cast<void>(fastFiles.Open(name));
        }
    }

    namespace
    {
        /// <summary>What the process booted: 0 nothing yet, -1 logistics, else a mission segment.</summary>
        int32_t booted = 0;

        /// <summary>Set by StartLogistics: the boot's clock moves on with the presents.</summary>
        bool clockFollowsPresents = false;

        /// <summary>RunGame up to MCGuiSystem::Run, with <paramref name="commandLine"/>, in a hidden window.</summary>
        bool Boot(std::string commandLine);

        /// <summary>
        /// The boot's clock, a manual one the boot installs in the context current then (a test that installs a clock of
        /// its own over it fails with std::bad_cast).
        /// </summary>
        MCManualClock& BootClock()
        {
            return dynamic_cast<MCManualClock&>(MCGameContext::Current().Clock());
        }
    }

    std::shared_ptr<MCMover> OrderMissionOneAttack()
    {
        constexpr int32_t UllerPart = 896;
        constexpr int32_t FirstPlayerPart = 0x200;
        constexpr int32_t PlayerMechs = 3;
        std::shared_ptr<MCMover> uller = GetMoverFromPartId(UllerPart);

        for (int32_t partId = FirstPlayerPart; uller != nullptr && partId < FirstPlayerPart + PlayerMechs; partId++)
        {
            if (const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId); mover != nullptr)
            {
                MCTacticalOrder order;
                order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::AttackObject, 0);
                order.Target = uller;
                order.AttackParams.Type = 1;
                order.AttackParams.Method = 0;
                order.AttackParams.Range = -1;
                order.AttackParams.Pursue = -1;
                mover->HandleTacticalOrder(order, 1, 0);
            }
        }

        return uller;
    }

    bool StartMission(int32_t segment)
    {
        if (booted != 0)
        {
            return booted == segment && Scenario() != nullptr;
        }

        booted = segment;

        if (!Boot("-mission " + std::to_string(segment)))
        {
            return false;
        }

        // The hidden window has no mouse, so the cursor stays at (0, 0), where it scrolls the camera off the
        // player's units into the map's corner. Park it in the middle of the screen, as a player's would be.
        if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
        {
            SDL_Event motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.windowID = SDL_GetWindowID(&display->Window());
            display->LogicalToWindow(320.5f, 240.5f, motion.motion.x, motion.motion.y);
            MCInput::HandleEvent(motion);
        }

        // The scenario loads and then counts down its start-up; run until units are moving.
        for (int32_t frame = 0; frame < 3000; frame++)
        {
            RunFrame(1.0f / 15.0f);

            if (Scenario() != nullptr && Mission()->State == MCMissionState::Scenario && Turn > 30)
            {
                return true;
            }
        }

        std::cout << "  the scenario never started (mission state " << std::to_underlying(Mission()->State) << ")\n";
        return false;
    }

    bool StartLogistics(const std::function<void()>& afterBoot)
    {
        if (booted != 0)
        {
            return booted == -1 && GlobalLog() != nullptr;
        }

        booted = -1;
        // The screen wipes draw frames until a quarter of a second has passed.
        clockFollowsPresents = true;

        if (!Boot(""))
        {
            return false;
        }

        if (afterBoot)
        {
            afterBoot();
        }

        // The intro movie (if any) and the main menu's entrance.
        for (int32_t frame = 0; frame < 3000; frame++)
        {
            RunFrame(1.0f / 15.0f);

            if (Mission()->State == MCMissionState::Logistics && GlobalLog() != nullptr &&
                GlobalLog()->CurrentScreen() == GlobalLog()->MainScreen && GuiSystem().SmackerWindow == nullptr)
            {
                return true;
            }
        }

        std::cout << "  logistics never came up (mission state " << std::to_underlying(Mission()->State) << ")\n";
        return false;
    }

    bool StartLobby(std::string_view commandLine)
    {
        if (booted != 0)
        {
            return false;
        }

        booted = -1;
        clockFollowsPresents = true;

        if (!Boot(std::string(commandLine)))
        {
            return false;
        }

        for (int32_t frame = 0; frame < 3000; frame++)
        {
            RunFrame(1.0f / 15.0f);

            // The room (the session screen) once the lobby's join is done, or a dialog saying it failed.
            if (Mission()->State == MCMissionState::Logistics && GlobalLog() != nullptr &&
                (GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen ||
                 GlobalLog()->MessageDialog->IsShowing()) &&
                GuiSystem().SmackerWindow == nullptr)
            {
                return true;
            }
        }

        std::cout << "  the lobby's logistics never came up (mission state " << std::to_underlying(Mission()->State)
                  << ")\n";
        return false;
    }

    bool StartCampaignMission()
    {
        // Two seconds settle a screen change's wipe.
        const auto settle = []
        {
            for (int32_t frame = 0; frame < 30; frame++)
            {
                RunFrame(1.0f / 15.0f);
            }
        };

        settle();
        NewCampaign();
        settle();
        {
            // Pinned only for the set-up: starting the scenario takes logistics down.
            const std::shared_ptr<MCLogistics> logistics = GlobalLog();
            REQUIRE_EQ(logistics->CurrentMission, 0);
            REQUIRE_EQ(Mission()->CurrentScenario, 0);
            std::shared_ptr<MCLogMech> mech;
            REQUIRE_EQ(logistics->ForceMechList->GetMechInfo(0, mech), 0);
            logistics->DeploySlots[0][0].Unit = 0;
            mech->Deployed = true;
        }

        Mission()->StartScenario(Mission()->Scenarios[0]);

        for (int32_t frame = 0; frame < 3000; frame++)
        {
            RunFrame(1.0f / 15.0f);

            if (Mission()->State == MCMissionState::Scenario && Scenario() != nullptr && !Scenario()->StartingUp)
            {
                return true;
            }
        }

        std::cout << "  the campaign's scenario never started (mission state " << std::to_underlying(Mission()->State)
                  << ")\n";
        return false;
    }

    namespace
    {
        bool Boot(std::string commandLine)
        {
            // The game writes its saves and settings under the user folder; keep them out of the real one.
            const std::filesystem::path userRoot = UserFolder();
            std::filesystem::create_directories(userRoot);
            MCFileSystem::SetUserRoot(userRoot);

            // The sound system still runs (as in play), into SDL's silent driver.
            SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

            if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
            {
                std::cout << "  SDL_Init failed: " << SDL_GetError() << "\n";
                return false;
            }

            // RunGame, up to MCGuiSystem::Run. SystemInit reads the paths from SYSTEM.CFG; the FastFiles it opens are
            // every *.fst, so none may be open yet.
            GHiddenWindow = 1;
            GNoSound = !BootOptions.Sound;
            // Deterministic runs: game time only moves with RunFrame, and the dice start the same way (RunGame seeds
            // them from the time of day). --seed <n> picks other dice. With the default, mission 1's Uller has its
            // pilot knocked out (4 wounds) in the fight the mission tests stage.
            MCManualClock& clock = MCGameContext::Current().SetClock(std::make_unique<MCManualClock>());

            if (clockFollowsPresents)
            {
                clock.AdvanceOnPresent();
            }

            const std::optional<std::string_view> seed = MCTest::Option("seed");
            MCPort::SeedRand(seed ? static_cast<uint32_t>(std::stoul(std::string(*seed))) : 10u);
            // The world view shows 480 lines (one world pixel per screen pixel in the 640x480 window) at any zoom
            // request, so what is on screen, and so updated, is the same every run.
            MCFixedZoomHeight = BootOptions.FixedZoomHeight;
            PaletteName = "palette.gif";
            MCGameContext::Current().SetGuiSystem(std::make_unique<MCGuiSystem>());

            if (GuiSystem().Start(commandLine, 640, 480) != 0)
            {
                std::cout << "  aSystem::start failed\n";
                return false;
            }

            // The original's pace and interface size, not AUTO's size for whatever window a test opens (a test, or
            // BootOptions.Started, sets another).
            GScrollSpeed = AutoScrollSpeed;
            GInterfaceSize = OriginalInterfaceSize;

            if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
            {
                display->OnPresent = Presented;

                if (BootOptions.Started)
                {
                    BootOptions.Started();
                }

                // Another window size: the screen follows it, as it does for a player's window in the menus.
                if (BootOptions.WindowWidth > 0 && BootOptions.WindowHeight > 0)
                {
                    SDL_SetWindowSize(&display->Window(), BootOptions.WindowWidth, BootOptions.WindowHeight);
                    SDL_SyncWindow(&display->Window());
                    MCFollowWindowSize();
                }
            }

            for (int32_t i = 0; i < ScreenWindow()->NumberOfChildren(); i++)
            {
                ScreenWindow()->Child(i)->Draw();
            }

            return true;
        }
    }

    void RunFrame(float seconds)
    {
        BootClock().Advance(static_cast<uint64_t>(static_cast<double>(seconds) * 1e9));
        FrameLength = seconds;
        FrameRate = 1.0f / seconds;
        MCInput::PumpMessages();

        if (GuiSystem().SmackerWindow == nullptr)
        {
            GuiSystem().RunFrameCallbacks(true);
        }

        int32_t staticNoise = 0;
        int32_t noiseChance = 0;

        if (Scenario() != nullptr)
        {
            staticNoise = Scenario()->StartingUp;
            noiseChance = Scenario()->StartUpCountdown;
        }

        UpdateDisplay(0, staticNoise, noiseChance, 0, 0);

        // --frames <file>: each frame's movers, to find where two runs part.
        static std::ofstream frameLog = []
        {
            const std::optional<std::string_view> name = MCTest::Option("frames");
            return name ? std::ofstream(MCFileSystem::FromUtf8(*name)) : std::ofstream();
        }();

        if (frameLog.is_open())
        {
            static int32_t frame = 0;
            frameLog << std::format("frame {} turn {} time {:.3f} clock {}", frame++, Turn,
                                    static_cast<double>(ScenarioTime), MCPort::Milliseconds());

            for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId && Scenario() != nullptr; partId++)
            {
                if (std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId); mover != nullptr)
                {
                    const MCVector3D position = mover->GetPosition();
                    frameLog << std::format(" {}:{:.2f},{:.2f}", partId, static_cast<double>(position.X),
                                            static_cast<double>(position.Y));
                }
            }

            frameLog << "\n" << std::flush;
        }

        // --state-log <file>: each frame's state hash, to find where two builds part (tools/ci/baseline.py).
        static std::ofstream stateLog = OpenLog("state-log");

        if (stateLog.is_open() && Scenario() != nullptr)
        {
            static int32_t stateFrame = 0;
            stateLog << std::format("{} 0x{:08x}\n", stateFrame++, StateHash()) << std::flush;
        }
    }
}

std::vector<std::pair<std::string, int32_t>> MCTestGame::MechTrees()
{
    std::vector<std::pair<std::string, int32_t>> trees;
    MCPacketFile pak;

    if (pak.Open(GamePath(SpritePath, "sprites", ".pak")) != 0)
    {
        return trees;
    }

    for (int32_t packet = 0; packet < pak.GetNumPackets(); ++packet)
    {
        MCFitIniFile fit;

        if (pak.SeekPacket(packet) != 0 || pak.GetPacketSize() == 0 ||
            fit.Open(pak, static_cast<uint32_t>(pak.GetPacketSize())) != 0 || fit.SeekBlock("Parts") != 0 ||
            fit.SeekBlock("Main Info") != 0)
        {
            continue;
        }

        if (const MCFitResult<std::string> treeName = fit.Read<std::string>("Name"); treeName.has_value())
        {
            trees.emplace_back(*treeName, packet);
        }
    }

    return trees;
}

std::shared_ptr<MCSpriteTree> MCTestGame::FindMechTree(std::string_view name)
{
    for (const auto& [treeName, packet] : MechTrees())
    {
        if (treeName == name)
        {
            return std::static_pointer_cast<MCSpriteTree>(
                AppearanceTypeList().GetAppearance(0x01000000u | static_cast<uint32_t>(packet)));
        }
    }

    return nullptr;
}

void MCTestGame::FaceMech(MCBattleMech& mech, float degrees)
{
    const double radians = static_cast<double>(degrees) * std::numbers::pi / 180.0;
    const auto c = static_cast<float>(std::cos(radians));
    const auto s = static_cast<float>(std::sin(radians));
    MCFrameOfRef frame(MCVector3D(c, s, 0.0f), MCVector3D(-s, c, 0.0f), UnitZ);
    mech.SetFrame(frame);
}

void MCTestGame::StockStore(int32_t count)
{
    for (const auto& mech : GlobalLog()->PurMechList->Mechs)
    {
        for (const auto& variant : mech->Variants)
        {
            if (variant != nullptr)
            {
                variant->NumAvailable = count;
            }
        }
    }

    for (const std::shared_ptr<MCLogInventoryItem>& item : GlobalLog()->PurchaseComponents->Items)
    {
        item->Count = count;
    }

    for (const auto& vehicle : GlobalLog()->PurVehicleList->Vehicles)
    {
        vehicle->Data->NumAvailable = count;
    }

    MCLogInvScreen::RefreshStoreRows();
}

#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fakes/MCManualClock.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCUpdateDisplay.h"
#include "lib/MCFastFileSet.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGameList.h"
#include "logistics/MCLogComboBox.h"
#include "logistics/MCLogSlider.h"
#include "logistics/MCSplashScreen.h"
#include "appear/MCAppearanceTypeList.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "object/MCBattleMech.h"
#include "sprite/MCSpriteTree.h"
#include "main/MCSystemConfig.h"
#include "main/MCLogistics.h"
#include "main/MCGameContext.h"
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

            std::ofstream log(std::filesystem::path(*name), std::ios::app);

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
                static_cast<void>(MCInput::Display()->SaveScreenshot(std::filesystem::path(*shots) /
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
                option ? std::filesystem::path(*option) : std::filesystem::current_path();

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

    bool StartLogistics()
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

    namespace
    {
        bool Boot(std::string commandLine)
        {
            // The game writes its saves and settings under the user folder; keep them out of the real one.
            const std::filesystem::path userRoot = std::filesystem::temp_directory_path() / "mc_tests_user";
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
            GNoSound = true;
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
            MCFixedZoomHeight = 480.0f;
            PaletteName = "palette.gif";
            MCGameContext::Current().SetGuiSystem(std::make_unique<MCGuiSystem>());

            if (GuiSystem().Start(commandLine, 640, 480) != 0)
            {
                std::cout << "  aSystem::start failed\n";
                return false;
            }

            if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
            {
                display->OnPresent = Presented;
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
            return name ? std::ofstream(std::filesystem::path(*name)) : std::ofstream();
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

std::shared_ptr<MCSpriteTree> MCTestGame::FindMechTree(std::string_view name)
{
    MCPacketFile pak;

    if (pak.Open(GamePath(SpritePath, "sprites", ".pak")) != 0)
    {
        return nullptr;
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

        const MCFitResult<std::string> treeName = fit.Read<std::string>("Name");

        if (treeName.has_value() && *treeName == name)
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

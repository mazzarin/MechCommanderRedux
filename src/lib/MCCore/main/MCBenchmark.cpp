#include "stdafx.h"
#include "main/MCBenchmark.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiCallback.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiInput.h"
#include "gui/MCGuiSmackerWindow.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCUpdateDisplay.h"
#include "lib/MCVector3D.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCDeploy.h"
#include "logistics/MCLogisticsTabs.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMenuScreens.h"
#include "main/MCGameSession.h"
#include "main/MCHdSettings.h"
#include "main/MCLogistics.h"
#include "main/MCMissionGlobals.h"
#include "main/MCSystemConfig.h"
#include "main/MCTelemetry.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "mission/MCStagedBattle.h"
#include "mission/MCTrailerMission.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCFrameLog.h"
#include "platform/MCInput.h"
#include "platform/MCUtf8Path.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCWeather.h"
#include "sprite/MCWeatherPace.h"
#include "terrain/MCTerrain.h"

namespace
{
    /// <summary>The battle the benchmark plays: the trailer's canyon (MCBattleSpec::BuiltIn).</summary>
    constexpr std::string_view BattleName = "canyon";
    /// <summary>The campaign whose game segments the battle's mission is one of (SYSTEM.CFG's own, as installed).</summary>
    constexpr std::string_view Campaign = "MechCmdr1";
    /// <summary>
    /// The scenario's time before the battle starts: the recorder's start (MCTestGame::StartMission's 30 turns at
    /// 15 a second), the units moving and the drop's sounds over.
    /// </summary>
    constexpr float StartSeconds = 2.0f;
    /// <summary>The pan after the battle: once round the map, at this share of the way from its middle to its corners.</summary>
    constexpr float PanSeconds = 30.0f;
    constexpr float PanRingShare = 0.55f;
    constexpr float FullTurn = 2.0f * std::numbers::pi_v<float>;
    constexpr double MsPerSecond = 1000.0;
    constexpr float Percent = 100.0f;
    constexpr std::string_view SummariesFile = "benchmarks.txt";
    constexpr std::string_view UnknownBuild = "unknown build";

    constexpr std::array<std::string_view, 6> Weathers{"clear", "rain", "downpour", "lightsnow", "snow", "blizzard"};
    constexpr std::array<std::string_view, 3> Times{"day", "dusk", "night"};
    constexpr std::string_view HdWord = "hd";
    constexpr std::string_view OriginalWord = "original";

    /// <summary>Where a run is.</summary>
    enum class MCPhase : uint8_t
    {
        /// <summary>The mission loading, its scenario starting up.</summary>
        Waiting,
        Battle,
        Pan,
        Done,
    };

    struct MCRun
    {
        MCBenchmark::MCVariant Variant;
        MCBattleSpec Spec;
        std::shared_ptr<MCGuiCallback> Callback;
        MCPhase Phase = MCPhase::Waiting;
        std::unique_ptr<MCStagedBattle> Battle;
        /// <summary>The scenario's time as the battle and the pan started.</summary>
        float BattleStart = 0.0f;
        float PanStart = 0.0f;
        /// <summary>The pan's first angle round the map's middle: the battle's last view's.</summary>
        float PanFrom = 0.0f;
        /// <summary>The battle's last frame staged (-1: none yet).</summary>
        int32_t Staged = -1;
        /// <summary>Whether the pan's frames are being tallied (from the frame after the one that revealed the map).</summary>
        bool PanTallied = false;
        /// <summary>Whether the player's mouse and keys are held off the game yet (MCInput::HoldInput), and whether Escape stopped it.</summary>
        bool InputHeld = false;
        bool Stopped = false;
        MCFrameLog::MCTally BattleTally;
    };

    std::optional<MCRun>& current()
    {
        static std::optional<MCRun> instance;
        return instance;
    }

    /// <summary>Where a logistics tour is (MCBenchmark::ArmBriefing).</summary>
    enum class MCBriefingPhase : uint8_t
    {
        /// <summary>The game starting, until the main menu shows.</summary>
        Menu,
        /// <summary>A new campaign asked for, until its first briefing shows.</summary>
        Opening,
        /// <summary>The tour's steps, each up for StepSeconds.</summary>
        Steps,
        /// <summary>Launch pressed, until the mission's first frame.</summary>
        Launching,
        Done,
    };

    /// <summary>A step's frames, what the hold held at its end and what it found, for the summary.</summary>
    struct MCStepResult
    {
        std::string_view Name;
        MCFrameLog::MCTally Tally;
        double HeldMb = 0.0;
        /// <summary>Port-only (SPEC-memory.md's "The GPU from the briefing"): the fill on the GPU at the step's end.</summary>
        MCHdArt::GpuFill Fill;
        std::string Note;
    };

    /// <summary>
    /// A logistics tour (MCBenchmark::ArmBriefing): a new campaign's first briefing on the campaign's screen, its drop,
    /// the 'MECH BAY, PURCHASING, back to BRIEFING and Launch.
    /// </summary>
    struct MCBriefingRun
    {
        int32_t StepSeconds = 0;
        std::shared_ptr<MCGuiCallback> Callback;
        MCBriefingPhase Phase = MCBriefingPhase::Menu;
        /// <summary>The step now, an index into the tour's steps.</summary>
        size_t Step = 0;
        std::chrono::steady_clock::time_point StepStart;
        /// <summary>The mech the tour puts in the lance (and takes off it again).</summary>
        std::weak_ptr<MCLogMech> Added;
        std::vector<MCStepResult> Results;
        /// <summary>From Launch's press to the mission's first frame (seconds).</summary>
        double LaunchSeconds = 0.0;
        bool InputHeld = false;
        bool Stopped = false;
    };

    std::optional<MCBriefingRun>& briefing()
    {
        static std::optional<MCBriefingRun> instance;
        return instance;
    }

    std::string& build()
    {
        static std::string instance(UnknownBuild);
        return instance;
    }

    std::string lower(std::string_view text)
    {
        std::string lowered(text);
        std::ranges::transform(lowered, lowered.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
        return lowered;
    }

    /// <summary>Adds <paramref name="line"/> to the user folder's summaries.</summary>
    void AddSummary(std::string_view line)
    {
        const std::filesystem::path path = MCFileSystem::UserRoot() / SummariesFile;
        std::ofstream file(path, std::ios::app);
        file << line << "\n";

        if (!file)
        {
            SDL_Log("benchmark: can't add to %s", MCFileSystem::ToUtf8(path).c_str());
        }
    }

    /// <summary>Ends the game for <paramref name="why"/>, saying so in the log, benchmarks.txt and the frame log.</summary>
    [[noreturn]] void Fail(std::string_view why)
    {
        MCInput::HoldInput(false);
        const std::string line = std::format("MCRedux benchmark, {}: {}", build(), why);
        SDL_Log("%s", line.c_str());
        AddSummary(line);
        MCFrameLog::Line(line);
        MCFrameLog::Close();
        MCHdArt::CloseTrace();

        // Done, its callback kept to its end (this may be it).
        if (current().has_value())
        {
            current()->Phase = MCPhase::Done;
        }

        if (briefing().has_value())
        {
            briefing()->Phase = MCBriefingPhase::Done;
        }

        KillTheGame(1);
    }

    /// <summary>The computer, as the reports read it; "not read" before they start or without a backend.</summary>
    std::string MachineText()
    {
        const std::optional<MCTelemetry::MCMachine> machine = MCTelemetry::KnownMachine();

        if (!machine.has_value())
        {
            return "computer not read";
        }

        std::string cores = std::format("{}P+{}E cores", machine->PerformanceCores, machine->EfficiencyCores);

        if (machine->GpuCores > 0)
        {
            cores += std::format(", {} GPU cores", machine->GpuCores);
        }

        return std::format("{} ({}), {} GB, {}, {} {}", machine->Cpu, cores, machine->MemoryGb, machine->Model,
                           machine->Platform, machine->System);
    }

    /// <summary>
    /// A tally's hitches and frames, for the summary: "p99 12.10 ms, worst 45.20 ms, ...": the screen's frames where the
    /// presenter said (MCFrameLog::Presented), else the game's loop's.
    /// </summary>
    std::string FramesText(const MCFrameLog::MCTally& tally)
    {
        const MCFrameLog::MCFrameTimes& frames = tally.Judged();
        const double median = frames.MedianMs();
        return std::format("p99 {:.2f} ms, worst {:.2f} ms, {} frames late by a refresh, {} over 1.5 refreshes; "
                           "median {:.2f} ms ({:.1f} fps); {} frames in {:.1f} s",
                           frames.P99Ms(), frames.WorstMs(), frames.Slow, frames.Long, median,
                           median > 0.0 ? MsPerSecond / median : 0.0, frames.Times.size(), frames.Seconds());
    }

    /// <summary>Which frames the summary judges: "frames as shown" (handed to the screen) or the game's loop's.</summary>
    std::string_view TimingText(const MCFrameLog::MCTally& tally)
    {
        return tally.TimedShown() ? "frames as shown" : "frames as the game's loop ran (none shown)";
    }

    /// <summary>
    /// The HD frames first used in a tally, the time their reading took and the readers' reads of the archives
    /// (sprite/MCHdArt.cpp's names).
    /// </summary>
    std::string HdText(const MCFrameLog::MCTally& tally)
    {
        return std::format("{} HD frames first used, read in {:.1f} ms, {} reads by the readers",
                           tally.Count("HD frames first used"), tally.PartMs("hd.read"),
                           tally.Count("HD reads by the readers"));
    }

    /// <summary>The camera put at <paramref name="angle"/> round the pan's ring, on the ground.</summary>
    void PanTo(float angle)
    {
        const float halfMap = static_cast<float>(MCTerrain::VerticesBlockSide * MCTerrain::BlocksMapSide) *
                              MCTerrain::MetersPerVertex * 0.5f;
        const float radius = halfMap * PanRingShare;
        MCVector3D position(radius * std::cos(angle), radius * std::sin(angle), 0.0f);
        position.Z = MCTerrain::GetTerrainElevation(position);
        const std::shared_ptr<MCCamera> eye = Eye();

        if (eye != nullptr)
        {
            eye->ChangeTarget(nullptr, true);
            eye->SetPosition(position);
        }
    }

    /// <summary>The mouse put in the middle of the window, where it scrolls nothing (as the recorder parks it).</summary>
    void ParkMouse()
    {
        if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
        {
            const SDL_Rect view = display->View();
            MCInput::SetCursorPos(view.x + view.w / 2, view.y + view.h / 2);
        }
    }

    /// <summary>
    /// A run's frame, from the first on (the display up): ends the game if Escape stopped it (<paramref name="stopped"/>);
    /// once, the player's mouse and keys held off the game, so every run plays the same whoever's at the computer (the
    /// mouse parked in the middle, where it scrolls nothing, and Escape calling <paramref name="stop"/>); and the frame
    /// log, -framelog's or the run's own, benchmark-<paramref name="name"/>.txt in the user folder.
    /// </summary>
    void HoldTheGame(bool& inputHeld, bool stopped, std::function<void()> stop, std::string_view name)
    {
        if (stopped)
        {
            Fail("stopped by Escape before its end");
        }

        if (!std::exchange(inputHeld, true))
        {
            MCInput::HoldInput(true, std::move(stop));
            ParkMouse();
        }

        if (!MCFrameLog::Enabled())
        {
            const std::filesystem::path path = MCFileSystem::UserRoot() / std::format("benchmark-{}.txt", name);

            if (!MCFrameLog::Open(path))
            {
                Fail(std::format("can't write the frame log {}", MCFileSystem::ToUtf8(path)));
            }
        }
    }

    /// <summary>The battle staged up to the frame due at the scenario's time now, or the camera aimed between frames.</summary>
    void PlayBattle(MCRun& run)
    {
        const int32_t last = run.Spec.Frames - 1;
        const auto due = std::min(last, static_cast<int32_t>((ScenarioTime - run.BattleStart) *
                                                             static_cast<float>(MCBattleSpec::FramesPerSecond)));

        if (due <= run.Staged)
        {
            run.Battle->Aim(run.Staged);
            return;
        }

        // Each frame staged once the logic has run the one before (two or more at once when the frames are slower than
        // the battle's).
        while (run.Staged < due)
        {
            if (run.Staged >= 0)
            {
                run.Battle->Staged(run.Staged);
            }

            run.Battle->Stage(++run.Staged);
        }
    }

    void StartPan(MCRun& run)
    {
        run.Battle->Staged(run.Staged);
        run.BattleTally = MCFrameLog::TakeTally();
        // The whole map shown for the pan (the original's reveal cheat), as to a player who has been everywhere: under
        // the fog of war it drew next to nothing. The battle is over, so its play is as ever; the frame that reveals it
        // is in neither part's tally.
        RevealAll();
        MCFrameLog::Note("benchmark: the pan starts, the map revealed");
        MCHdArt::TraceNote("benchmark: the pan starts");

        if (const std::shared_ptr<MCCamera> eye = Eye(); eye != nullptr)
        {
            run.PanFrom = std::atan2(eye->Position.Y, eye->Position.X);
        }

        run.PanStart = ScenarioTime;
        run.Phase = MCPhase::Pan;
    }

    /// <summary>
    /// The HD art readers' settings where the command line changed them from the defaults (-hdreaders, -hdsetreads,
    /// -hdreaderpriority; SPEC-first-use.md, each default measured): "HD readers 8, set reads 2, level"; else nothing.
    /// </summary>
    std::string ReadersText()
    {
        const MCHdArt::ReaderSettings settings = MCHdArt::GetReaderSettings();

        if (settings == MCHdArt::ReaderSettings{})
        {
            return {};
        }

        return std::format("HD readers {}, set reads {}, {}", settings.Readers,
                           settings.SetReads == 0 ? std::string("all") : std::to_string(settings.SetReads),
                           settings.Below ? "below" : "level");
    }

    /// <summary>
    /// Port-only (<c>-hdguard</c>, MCHdArt::GuardOriginalReads): says each kind of original art read the guard noted in
    /// the log, the frame log and benchmarks.txt, or that it couldn't guard (no version 2 pack drawn, as when the app
    /// didn't find its pack). Whether the run passes: the guard off, or on with a version 2 pack drawn and no read noted
    /// (a run that drew no version 2 pack fails: it guarded nothing).
    /// </summary>
    bool ReportGuardedReads()
    {
        const std::optional<std::vector<MCHdArt::GuardedRead>> reads = MCHdArt::GuardedReads();

        if (!reads.has_value())
        {
            return true;
        }

        std::vector<std::string> lines;
        size_t total = 0;
        const bool guarding = MCHdArt::GuardingOriginalReads();

        if (!guarding)
        {
            lines.emplace_back("HD guard: no version 2 HD pack drawn, nothing guarded: the run fails");
        }

        for (const MCHdArt::GuardedRead& read : *reads)
        {
            std::string examples;

            for (const std::string& example : read.Examples)
            {
                examples += (examples.empty() ? "" : "; ") + example;
            }

            lines.push_back(std::format("HD guard: {} x {} (e.g. {})", read.Count, read.Kind, examples));
            total += read.Count;
        }

        lines.push_back(std::format("HD guard: {} reads of the original's art in all", total));

        for (const std::string& line : lines)
        {
            SDL_Log("%s", line.c_str());
            MCFrameLog::Line(line);
            AddSummary(line);
        }

        return guarding && total == 0;
    }

    [[noreturn]] void Finish(MCRun& run)
    {
        const MCFrameLog::MCTally pan = MCFrameLog::TakeTally();
        // The weather's share of its pieces thrown (the snow thins itself where the frames can't keep up with it).
        std::string notes = run.Variant.Weather == Weathers.front()
                                ? std::string()
                                : std::format("weather pieces {:.0f}% thrown", MCWeatherPacing::Share() * Percent);
        const std::string readers = ReadersText();
        notes += notes.empty() || readers.empty() ? readers : ", " + readers;
        const std::string variant =
            notes.empty() ? run.Variant.Name() : std::format("{} ({})", run.Variant.Name(), notes);
        const MCBenchmark::MCRunInfo info{
            .Build = build(), .Variant = variant, .Display = DisplayDescription(), .Machine = MachineText()};
        const std::string line = MCBenchmark::Summary(info, run.BattleTally, pan);
        run.Phase = MCPhase::Done;
        MCInput::HoldInput(false);
        MCFrameLog::Line(line);
        AddSummary(line);
        SDL_Log("%s", line.c_str());
        // A run under -hdguard fails on any original art read that the HD pack is drawn in place of, or with none drawn.
        const bool passed = ReportGuardedReads();
        MCFrameLog::Close();
        MCHdArt::CloseTrace();
        KillTheGame(passed ? 0 : 1);
    }

    /// <summary>The benchmark's frame, before the scenario's logic runs (its callback was added first).</summary>
    void Frame()
    {
        if (!current().has_value() || current()->Phase == MCPhase::Done)
        {
            return;
        }

        // The benchmark's own work in the frame log, apart from the game's (its staging of the battle, the camera's pan).
        MCFrameLog::Scope part("logic.benchmark");
        MCRun& run = *current();

        HoldTheGame(
            run.InputHeld, run.Stopped,
            []
            {
                if (current().has_value())
                {
                    current()->Stopped = true;
                }
            },
            run.Variant.Name());

        switch (run.Phase)
        {
            case MCPhase::Waiting:
            {
                if (Mission() == nullptr || Mission()->State != MCMissionState::Scenario || Scenario() == nullptr ||
                    Scenario()->StartingUp || ScenarioTime < StartSeconds)
                {
                    return;
                }

                MCFrameLog::Line(
                    std::format("benchmark: {} {}, {}, {}", BattleName, run.Variant.Name(), build(), MachineText()));
                run.Battle = std::make_unique<MCStagedBattle>(run.Spec,
                                                              [](MCBattleLine kind, std::string_view line)
                                                              {
                                                                  // The staged events beside the frames they happen in; each second's state stays the recorder's.
                                                                  if (kind == MCBattleLine::Event)
                                                                  {
                                                                      MCFrameLog::Note(std::format("battle: {}", line));
                                                                  }
                                                              });

                if (const std::expected<void, std::string> begun = run.Battle->Begin(); !begun.has_value())
                {
                    Fail(begun.error());
                }

                ParkMouse();
                MCFrameLog::Note("benchmark: the battle starts");
                MCHdArt::TraceNote("benchmark: the battle starts");
                MCFrameLog::StartTally();
                run.BattleStart = ScenarioTime;
                run.Phase = MCPhase::Battle;
                PlayBattle(run);
                break;
            }
            case MCPhase::Battle:
            {
                if (ScenarioTime - run.BattleStart >=
                    static_cast<float>(run.Spec.Frames) / static_cast<float>(MCBattleSpec::FramesPerSecond))
                {
                    StartPan(run);
                    PanTo(run.PanFrom);
                }
                else
                {
                    PlayBattle(run);
                }

                break;
            }
            case MCPhase::Pan:
            {
                if (!std::exchange(run.PanTallied, true))
                {
                    MCFrameLog::StartTally();
                }

                const float panned = ScenarioTime - run.PanStart;

                if (panned >= PanSeconds)
                {
                    Finish(run);
                }

                PanTo(run.PanFrom + FullTurn * panned / PanSeconds);
                break;
            }
            case MCPhase::Done:
                break;
        }
    }

    /// <summary>The tour's name: its frame log's (benchmark-logistics.txt).</summary>
    constexpr std::string_view TourName = "logistics";
    constexpr double BytesPerMb = 1024.0 * 1024.0;
    /// <summary>A part's type when it's a mech (MCLogPart::PartType).</summary>
    constexpr int32_t MechPartType = 1;
    /// <summary>The longest Launch may take to the mission's first frame before the run fails.</summary>
    constexpr double MostLaunchSeconds = 120.0;

    double HeldMb()
    {
        return static_cast<double>(MCHdArt::HeldBytes()) / BytesPerMb;
    }

    double SecondsSince(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    /// <summary>
    /// The first mech of READY AREA that fits the drop, sent to its first open slot (the right button's send, as a player
    /// puts one in: its briefing shows, its art read from then, MCHdArt::HoldMechAhead); DEPLOY shown first.
    /// </summary>
    std::string AddMech(MCBriefingRun& run, MCCampaignScreen& screen)
    {
        screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
        // READY AREA's cards are these units, in this order (MCCampaignScreen::FillCards; nothing is carried).
        const std::vector<std::shared_ptr<MCLogPart>> ready = MCDeploy::ReadyUnits();

        for (size_t card = 0; card < ready.size(); ++card)
        {
            const std::shared_ptr<MCLogPart>& unit = ready[card];

            if (unit == nullptr || unit->PartType != MechPartType || !MCDeploy::Fits(*unit))
            {
                continue;
            }

            run.Added = std::static_pointer_cast<MCLogMech>(unit);
            screen.SendReady(card, std::nullopt);
            return MCDeploy::SlotOf(*unit).has_value() ? std::string() : "the mech that fits stayed in READY AREA";
        }

        return "no mech in READY AREA fits the drop";
    }

    /// <summary>The mech AddMech put in, sent back out of its slot to READY AREA.</summary>
    std::string RemoveMech(MCBriefingRun& run, MCCampaignScreen& screen)
    {
        const std::shared_ptr<MCLogMech> mech = run.Added.lock();
        const std::optional<size_t> slot = mech != nullptr ? MCDeploy::SlotOf(*mech) : std::nullopt;

        if (!slot.has_value())
        {
            return "the mech put in isn't in the drop";
        }

        screen.SendBack(*slot);
        return MCDeploy::SlotOf(*mech).has_value() ? "the mech put in stayed in the drop" : std::string();
    }

    /// <summary>In the 'MECH BAY, another 'Mech of the force than the one in the lab picked: its first show there.</summary>
    std::string ShowAnotherMech(MCBriefingRun&, MCCampaignScreen& screen)
    {
        MCLogisticsTabs& tabs = screen.Logistics();
        const std::shared_ptr<MCLogPart> shown = tabs.LabUnit();

        for (size_t card = 0; card < tabs.ForceCards().size(); ++card)
        {
            const std::shared_ptr<MCLogPart> unit = tabs.UnitAt(MCLogisticsTabs::MCPlace::Force, card);

            if (unit != nullptr && unit->PartType == MechPartType && unit != shown)
            {
                tabs.Pick(MCLogisticsTabs::MCPlace::Force, card);
                return {};
            }
        }

        return "the MECH BAY lists no other mech";
    }

    /// <summary>The campaign screen's tab <typeparamref name="Tab"/> chosen, as its button chooses it.</summary>
    template <MCCampaignScreen::MCTab Tab> std::string ShowTab(MCBriefingRun&, MCCampaignScreen& screen)
    {
        screen.ChooseTab(Tab);
        return {};
    }

    /// <summary>PURCHASING's kind <typeparamref name="Kind"/> chosen (what the store lists).</summary>
    template <MCLogisticsTabs::MCKind Kind> std::string ShowKind(MCBriefingRun&, MCCampaignScreen& screen)
    {
        screen.Logistics().ChooseKind(Kind);
        return {};
    }

    /// <summary>The 'MECH BAY's INVENTORY on its MechWarriors.</summary>
    std::string ShowBayPilots(MCBriefingRun&, MCCampaignScreen& screen)
    {
        screen.Logistics().ChooseStock(MCLogisticsTabs::MCStock::MechWarriors);
        return {};
    }

    /// <summary>A step of the tour: its name, and what it does as it starts (what stopped it, or nothing).</summary>
    struct MCTourStep
    {
        std::string_view Name;
        std::string (*Start)(MCBriefingRun& run, MCCampaignScreen& screen);
    };

    /// <summary>
    /// The tour, as a player goes through mission 1's logistics on the campaign's screen (MCCampaignMenu): BRIEFING
    /// (shown by the new campaign), DEPLOY, a mech put in the drop and taken out, the 'MECH BAY and another mech in its
    /// lab, its MechWarriors, PURCHASING and its kinds, BRIEFING again, a mech put in for Launch. Each is the screen's
    /// own action, as its button or card calls it.
    /// </summary>
    constexpr std::array<MCTourStep, 13> TourSteps{{
        {"briefing", [](MCBriefingRun&, MCCampaignScreen&) { return std::string(); }},
        {"deploy", ShowTab<MCCampaignScreen::MCTab::Deploy>},
        {"a mech put in", AddMech},
        {"the mech taken out", RemoveMech},
        {"mech bay", ShowTab<MCCampaignScreen::MCTab::MechBay>},
        {"another mech shown", ShowAnotherMech},
        {"mech bay's MechWarriors", ShowBayPilots},
        {"purchasing", ShowTab<MCCampaignScreen::MCTab::Purchasing>},
        {"purchasing's vehicles", ShowKind<MCLogisticsTabs::MCKind::Vehicles>},
        {"purchasing's MechWarriors", ShowKind<MCLogisticsTabs::MCKind::MechWarriors>},
        {"purchasing's mechs", ShowKind<MCLogisticsTabs::MCKind::Mechs>},
        {"briefing again", ShowTab<MCCampaignScreen::MCTab::Briefing>},
        {"a mech put in for Launch", AddMech},
    }};

    /// <summary>
    /// A step's line: "mech bay: p99 9.1 ms, worst 31.0 ms, 2 late, held 1630 MB, on the GPU 812 of 1490 MB (fits)".
    /// </summary>
    std::string StepText(const MCStepResult& step)
    {
        const MCFrameLog::MCFrameTimes& frames = step.Tally.Judged();
        const MCHdArt::GpuFill& fill = step.Fill;
        const std::string gpu =
            fill.MissionBytes == 0
                ? std::string()
                : std::format(", on the GPU {:.0f} of {:.0f} MB ({})", static_cast<double>(fill.UpBytes) / BytesPerMb,
                              static_cast<double>(fill.MissionBytes) / BytesPerMb, fill.Fits ? "fits" : "doesn't fit");
        return std::format("{}: p99 {:.1f} ms, worst {:.1f} ms, {} late, held {:.0f} MB{}{}", step.Name, frames.P99Ms(),
                           frames.WorstMs(), frames.Slow, step.HeldMb, gpu, step.Note.empty() ? "" : ", " + step.Note);
    }

    /// <summary>The step now ended: its frames and what the hold held, and what the lance's steps found.</summary>
    void EndStep(MCBriefingRun& run)
    {
        MCStepResult result{.Name = TourSteps[run.Step].Name,
                            .Tally = MCFrameLog::TakeTally(),
                            .HeldMb = HeldMb(),
                            .Fill = MCHdArt::GpuFillNow()};

        const bool lanceStep = TourSteps[run.Step].Start == AddMech || TourSteps[run.Step].Start == RemoveMech;

        if (const std::shared_ptr<MCLogMech> mech = run.Added.lock(); lanceStep && mech != nullptr)
        {
            result.Note =
                std::format("chassis {} {}", mech->Chassis, mech->Deployed != 0 ? "in the lance" : "not in the lance");
        }

        MCFrameLog::Line(std::format("benchmark: {}", StepText(result)));
        run.Results.push_back(std::move(result));
    }

    /// <summary>Step <paramref name="step"/> begun: what it does, its frames tallied from here.</summary>
    void StartStep(MCBriefingRun& run, size_t step)
    {
        run.Step = step;
        const MCOptionalRef<MCCampaignScreen> screen = MCCampaignMenu::Screen();

        if (!screen.has_value())
        {
            Fail(std::format("the logistics tour's {}: the campaign's screen isn't up", TourSteps[step].Name));
        }

        if (const std::string stopped = TourSteps[step].Start(run, *screen); !stopped.empty())
        {
            Fail(std::format("the logistics tour's {}: {}", TourSteps[step].Name, stopped));
        }

        MCFrameLog::Note(std::format("benchmark: {}", TourSteps[step].Name));
        MCHdArt::TraceNote(std::format("benchmark: {}", TourSteps[step].Name));
        MCFrameLog::StartTally();
        run.StepStart = std::chrono::steady_clock::now();
    }

    /// <summary>The tour's end, at the mission's first frame: its summary in the log, the frame log and benchmarks.txt.</summary>
    [[noreturn]] void FinishTour(MCBriefingRun& run)
    {
        const MCFrameLog::MCTally launch = MCFrameLog::TakeTally();
        MCFrameLog::MCTally whole;
        std::string steps;

        for (const MCStepResult& step : run.Results)
        {
            whole.Add(step.Tally);
            steps += (steps.empty() ? "" : "; ") + StepText(step);
        }

        const std::string readers = ReadersText();
        const std::string line = std::format(
            "MCRedux logistics benchmark {}, mission 1's logistics, {} s a step, the briefing's hold {}, its fill on "
            "the "
            "GPU {}{}, {}, {}: {}; "
            "{}; {}; Launch to the mission's first frame {:.2f} s (its longest frame {:.1f} ms); held {:.1f} MB in {} "
            "reads. Steps: {}",
            build(), run.StepSeconds, MCHdArt::BriefingHoldWanted() ? "on" : "off",
            MCHdArt::BriefingFillWanted() ? "on" : "off", readers.empty() ? "" : ", " + readers, DisplayDescription(),
            MachineText(), TimingText(whole), FramesText(whole), HdText(whole), run.LaunchSeconds,
            launch.Loop.WorstMs(), HeldMb(), MCHdArt::HeldReads(), steps);
        run.Phase = MCBriefingPhase::Done;
        MCInput::HoldInput(false);
        MCFrameLog::Line(line);
        AddSummary(line);
        SDL_Log("%s", line.c_str());
        MCFrameLog::Close();
        MCHdArt::CloseTrace();
        KillTheGame(0);
    }

    /// <summary>
    /// The tour's frame: once the main menu shows, a new campaign; from its first briefing on the campaign's screen, each
    /// step in turn for StepSeconds, then Launch (BEGIN MISSION), until the mission's first frame.
    /// </summary>
    void BriefingFrame()
    {
        if (!briefing().has_value() || briefing()->Phase == MCBriefingPhase::Done)
        {
            return;
        }

        MCFrameLog::Scope part("logic.benchmark");
        MCBriefingRun& run = *briefing();
        HoldTheGame(
            run.InputHeld, run.Stopped,
            []
            {
                if (briefing().has_value())
                {
                    briefing()->Stopped = true;
                }
            },
            TourName);

        if (run.Phase == MCBriefingPhase::Launching)
        {
            if (Mission() != nullptr && Mission()->State == MCMissionState::Scenario && Scenario() != nullptr)
            {
                run.LaunchSeconds = SecondsSince(run.StepStart);
                FinishTour(run);
            }

            if (SecondsSince(run.StepStart) > MostLaunchSeconds)
            {
                Fail("Launch didn't start the mission");
            }

            return;
        }

        const std::shared_ptr<MCLogistics> logistics = GlobalLog();

        if (Mission() == nullptr || Mission()->State != MCMissionState::Logistics || logistics == nullptr ||
            GuiSystem().SmackerWindow != nullptr)
        {
            return;
        }

        switch (run.Phase)
        {
            case MCBriefingPhase::Menu:
            {
                if (MCMenuScreens::Top() == MCMenuScreens::Main())
                {
                    MCFrameLog::Note("benchmark: a new campaign");
                    // The tour times the tabs: the first mission's video doesn't play by itself over them.
                    MCCampaignMenu::AutoPlaysVideo = false;
                    NewCampaign();
                    run.Phase = MCBriefingPhase::Opening;
                }

                break;
            }
            case MCBriefingPhase::Opening:
            {
                if (MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing) && MCCampaignMenu::Ready())
                {
                    run.Phase = MCBriefingPhase::Steps;
                    StartStep(run, 0);
                }

                break;
            }
            case MCBriefingPhase::Steps:
            {
                if (SecondsSince(run.StepStart) < run.StepSeconds)
                {
                    break;
                }

                EndStep(run);

                if (run.Step + 1 < TourSteps.size())
                {
                    StartStep(run, run.Step + 1);
                    break;
                }

                // Launch: BEGIN MISSION pressed (it asks for the mission, which loads at a frame's end), the wait timed
                // from the press to the mission's first frame.
                const MCOptionalRef<MCCampaignScreen> screen = MCCampaignMenu::Screen();

                if (!screen.has_value())
                {
                    Fail("the campaign's screen went before Launch");
                }

                MCFrameLog::Note("benchmark: Launch");
                MCHdArt::TraceNote("benchmark: Launch");
                MCFrameLog::StartTally();
                run.StepStart = std::chrono::steady_clock::now();
                run.Phase = MCBriefingPhase::Launching;
                screen->get().PressBegin();
                break;
            }
            case MCBriefingPhase::Launching:
            case MCBriefingPhase::Done:
                break;
        }
    }
}

namespace MCBenchmark
{
    std::string MCVariant::Name() const
    {
        return std::format("{}-{}-{}", Weather, Time, Hd ? HdWord : OriginalWord);
    }

    std::expected<MCVariant, std::string> ReadVariant(std::string_view words)
    {
        MCVariant variant;
        bool weatherGiven = false;
        bool timeGiven = false;
        bool lookGiven = false;

        for (const auto part : std::views::split(words, ','))
        {
            const std::string word = lower(std::string_view(part.begin(), part.end()));

            if (word.empty())
            {
                continue;
            }

            bool* given = nullptr;

            if (std::ranges::find(Weathers, word) != Weathers.end())
            {
                given = &weatherGiven;
                variant.Weather = word;
            }
            else if (std::ranges::find(Times, word) != Times.end())
            {
                given = &timeGiven;
                variant.Time = word;
            }
            else if (word == HdWord || word == OriginalWord)
            {
                given = &lookGiven;
                variant.Hd = word == HdWord;
            }
            else
            {
                return std::unexpected(std::format(
                    "-benchmark: \"{}\" isn't a weather (clear, rain, downpour, lightsnow, snow, blizzard), a time of "
                    "day (day, dusk, night) or a look (hd, original)",
                    word));
            }

            if (std::exchange(*given, true))
            {
                return std::unexpected(std::format("-benchmark: \"{}\" names a second {}", word,
                                                   given == &weatherGiven ? "weather"
                                                   : given == &timeGiven  ? "time of day"
                                                                          : "look"));
            }
        }

        if (!variant.Hd && (variant.Weather != Weathers.front() || variant.Time != Times.front()))
        {
            return std::unexpected("-benchmark: the original's look has no weather, dusk or night (they're HD's)");
        }

        return variant;
    }

    std::string Summary(const MCRunInfo& run, const MCFrameLog::MCTally& battle, const MCFrameLog::MCTally& pan)
    {
        MCFrameLog::MCTally whole = battle;
        whole.Add(pan);
        return std::format("MCRedux benchmark {}, {} {}, {}, {}, {}: {}; {}. Battle: {}; {}. Pan: {}; {}", run.Build,
                           BattleName, run.Variant, run.Display, run.Machine, TimingText(whole), FramesText(whole),
                           HdText(whole), FramesText(battle), HdText(battle), FramesText(pan), HdText(pan));
    }

    void SetBuild(std::string text)
    {
        build() = std::move(text);
    }

    void Arm(std::string_view words)
    {
        const std::expected<MCVariant, std::string> variant = ReadVariant(words);

        if (!variant.has_value())
        {
            Fail(variant.error());
        }

        const std::expected<MCBattleSpec, std::string> spec = MCBattleSpec::Parse(*MCBattleSpec::BuiltIn(BattleName));

        if (!spec.has_value())
        {
            Fail(std::format("the {} battle's spec: {}", BattleName, spec.error()));
        }

        // The battle's mission, a game segment of the campaign as installed (-mission's), with the trailer's force.
        GlobalGameSegment = spec->Mission;
        MissionName = Campaign;

        if (!spec->Force.empty())
        {
            if (const std::expected<void, std::string> put = MCTrailerMission::Put(spec->Force); !put.has_value())
            {
                Fail(put.error());
            }
        }

        // The looks: HD's defaults or the original's, BRAKING at its default in both (it changes play), then the
        // variant's weather and time of day, as HD OPTIONS' WEATHER and HD LIGHTING show every mission.
        const MCHdSettings::MCSetting& braking = MCHdSettings::Find("MechBraking");

        if (variant->Hd)
        {
            UseHdByDefault();
            MCHdSettings::UseDefaults();
        }
        else
        {
            MCHdSettings::UseOriginal();
        }

        braking.Value.get() = MCHdSettings::Default(braking);
        GHdWeather = MCWeather::SettingOf(*MCWeather::Named(variant->Weather));

        if (const MCTimeOfDay time = *MCSceneLights::TimeOfDayNamed(variant->Time); time != MCTimeOfDay::Day)
        {
            GHdLighting = time == MCTimeOfDay::Dusk ? HdLightingDusk : HdLightingNight;
        }

        current().emplace(MCRun{.Variant = *variant, .Spec = *spec, .Callback = std::make_shared<MCGuiCallback>()});
        current()->Callback->SetExec(Frame);
        GuiSystem().AddCallback(current()->Callback);
    }

    void ArmBriefing(std::string_view word)
    {
        const int32_t seconds = std::atoi(std::string(word).c_str());

        if (seconds < 1 || seconds > MostStepSeconds)
        {
            Fail(std::format("-briefingbench: \"{}\" isn't a number of seconds from 1 to {}", word, MostStepSeconds));
        }

        // HD's looks at their defaults, as the canyon's HD variant has them.
        UseHdByDefault();
        MCHdSettings::UseDefaults();
        briefing().emplace(MCBriefingRun{.StepSeconds = seconds, .Callback = std::make_shared<MCGuiCallback>()});
        briefing()->Callback->SetExec(BriefingFrame);
        GuiSystem().AddCallback(briefing()->Callback);
    }

    bool Running()
    {
        return (current().has_value() && current()->Phase != MCPhase::Done) ||
               (briefing().has_value() && briefing()->Phase != MCBriefingPhase::Done);
    }

    void Release()
    {
        // Left to the statics' end at the program's exit, the battle's last units were destroyed after their game
        // (a crash at every quit).
        MCInput::HoldInput(false);
        current().reset();
        briefing().reset();
    }
}

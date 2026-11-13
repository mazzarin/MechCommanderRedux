#include "stdafx.h"
#include "main/MCBenchmark.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiCallback.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCVector3D.h"
#include "main/MCGameSession.h"
#include "main/MCHdSettings.h"
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

        // Done, its callback kept to its end (this may be it).
        if (current().has_value())
        {
            current()->Phase = MCPhase::Done;
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

    /// <summary>A tally's hitches and frames, for the summary: "p99 12.10 ms, worst 45.20 ms, ...".</summary>
    std::string FramesText(const MCFrameLog::MCTally& tally)
    {
        const double median = tally.MedianMs();
        return std::format("p99 {:.2f} ms, worst {:.2f} ms, {} frames late by a refresh, {} over 1.5 refreshes; "
                           "median {:.2f} ms ({:.1f} fps); {} frames in {:.1f} s",
                           tally.P99Ms(), tally.WorstMs(), tally.Slow, tally.Long, median,
                           median > 0.0 ? MsPerSecond / median : 0.0, tally.Times.size(), tally.Seconds());
    }

    /// <summary>The HD frames first used in a tally and the time their reading took (sprite/MCHdArt.cpp's names).</summary>
    std::string HdText(const MCFrameLog::MCTally& tally)
    {
        return std::format("{} HD frames first used, read in {:.1f} ms", tally.Count("HD frames first used"),
                           tally.PartMs("hd.read"));
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

        if (const std::shared_ptr<MCCamera> eye = Eye(); eye != nullptr)
        {
            run.PanFrom = std::atan2(eye->Position.Y, eye->Position.X);
        }

        run.PanStart = ScenarioTime;
        run.Phase = MCPhase::Pan;
    }

    [[noreturn]] void Finish(MCRun& run)
    {
        const MCFrameLog::MCTally pan = MCFrameLog::TakeTally();
        // The weather's share of its pieces thrown (the snow thins itself where the frames can't keep up with it).
        const std::string variant = run.Variant.Weather == Weathers.front()
                                        ? run.Variant.Name()
                                        : std::format("{} (weather pieces {:.0f}% thrown)", run.Variant.Name(),
                                                      MCWeatherPacing::Share() * Percent);
        const MCBenchmark::MCRunInfo info{
            .Build = build(), .Variant = variant, .Display = DisplayDescription(), .Machine = MachineText()};
        const std::string line = MCBenchmark::Summary(info, run.BattleTally, pan);
        run.Phase = MCPhase::Done;
        MCInput::HoldInput(false);
        MCFrameLog::Line(line);
        MCFrameLog::Close();
        AddSummary(line);
        SDL_Log("%s", line.c_str());
        KillTheGame(0);
    }

    /// <summary>The benchmark's frame, before the scenario's logic runs (its callback was added first).</summary>
    void Frame()
    {
        if (!current().has_value() || current()->Phase == MCPhase::Done)
        {
            return;
        }

        MCRun& run = *current();

        if (run.Stopped)
        {
            Fail("stopped by Escape before its end");
        }

        // From the first frame on (the display up), the player's mouse and keys held off the game, so every run plays
        // the same whoever's at the computer: the mouse parked in the middle, where it scrolls nothing, and Escape
        // stopping the run.
        if (!std::exchange(run.InputHeld, true))
        {
            MCInput::HoldInput(true,
                               []
                               {
                                   if (current().has_value())
                                   {
                                       current()->Stopped = true;
                                   }
                               });
            ParkMouse();
        }

        // The frame log, -framelog's or the run's own, from the first frame on.
        if (!MCFrameLog::Enabled())
        {
            const std::filesystem::path path =
                MCFileSystem::UserRoot() / std::format("benchmark-{}.txt", run.Variant.Name());

            if (!MCFrameLog::Open(path))
            {
                Fail(std::format("can't write the frame log {}", MCFileSystem::ToUtf8(path)));
            }
        }

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
        return std::format("MCRedux benchmark {}, {} {}, {}, {}: {}; {}. Battle: {}; {}. Pan: {}; {}", run.Build,
                           BattleName, run.Variant, run.Display, run.Machine, FramesText(whole), HdText(whole),
                           FramesText(battle), HdText(battle), FramesText(pan), HdText(pan));
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

    bool Running()
    {
        return current().has_value() && current()->Phase != MCPhase::Done;
    }
}

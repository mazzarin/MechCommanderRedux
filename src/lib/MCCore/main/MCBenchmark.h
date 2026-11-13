#pragma once

// Port-only (Redux HD): the benchmark, -benchmark [words]. The game plays the trailer's staged canyon battle
// (mission/MCStagedBattle.h, its force made from the install's own mission by mission/MCTrailerMission.h) in its own
// window and frame loop at real time, then pans the camera round the map (revealed, as the original's cheat does), and
// quits, so every build and computer is measured on the same play. The frame log (platform/MCFrameLog.h) records it, -framelog's file or else
// benchmark-<variant>.txt in the user folder, and its summary (the frames' p99 and worst, those late by a refresh and
// those over 1.5 refreshes, the median, the HD frames first used and their read time) ends that log and is added to
// the user folder's benchmarks.txt. The window's size, full screen and vsync are the player's own settings (and
// -novsync); the words choose the rest. While it runs the player's mouse and keys are held off the game, so a touch
// changes nothing (MCInput::HoldInput); Escape stops it, and quitting or closing the window still work.

#include <expected>
#include <string>
#include <string_view>

namespace MCFrameLog
{
    struct MCTally;
}

namespace MCBenchmark
{
    /// <summary>What a run plays in: the weather, the time of day and the look.</summary>
    struct MCVariant
    {
        /// <summary>The weather, as MCWeather::Named takes it (clear: the mission's own, a clear sky).</summary>
        std::string Weather = "clear";
        /// <summary>The time of day, as MCSceneLights::TimeOfDayNamed takes it.</summary>
        std::string Time = "day";
        /// <summary>HD's look (every HD setting at its default) or the original's (every HD setting off).</summary>
        bool Hd = true;

        /// <summary>Its name, as the files take it: weather-time-look (clear-day-hd).</summary>
        std::string Name() const;
    };

    /// <summary>
    /// The variant <paramref name="words"/> names: words split by commas, a weather (clear, rain, downpour, lightsnow,
    /// snow, blizzard), a time of day (day, dusk, night) and hd or original, each at most once, any case; the rest as
    /// <see cref="MCVariant"/> has them. The original's look has neither weather nor dusk or night.
    /// </summary>
    std::expected<MCVariant, std::string> ReadVariant(std::string_view words);

    /// <summary>What a run's summary says besides its frames.</summary>
    struct MCRunInfo
    {
        /// <summary>The build: "0.6.8 (976), commit 8c9bf531".</summary>
        std::string Build;
        std::string Variant;
        /// <summary>The window and how it's drawn: "3024x1890 pixels at 120.00 Hz, full screen, vsync on, ...".</summary>
        std::string Display;
        /// <summary>The computer: "Apple M4 Pro (10P+4E cores, 20 GPU cores), 48 GB, Mac16,8, mac 26.5".</summary>
        std::string Machine;
    };

    /// <summary>
    /// The run's summary, one line: the whole run's frames (the battle's and the pan's together) headed by its hitches,
    /// then each part's own.
    /// </summary>
    std::string Summary(const MCRunInfo& run, const MCFrameLog::MCTally& battle, const MCFrameLog::MCTally& pan);

    /// <summary>The build the summaries name (the app's version and commit; none: "unknown build").</summary>
    void SetBuild(std::string build);

    /// <summary>
    /// Arms the benchmark (ParseCommandLine, after PREFS is read): the variant <paramref name="words"/> names
    /// (<see cref="ReadVariant"/>), the canyon's mission and force, the looks, and the frame callback that plays it.
    /// A variant or force it can't play ends the game (exit code 1), saying why in the log and benchmarks.txt.
    /// </summary>
    void Arm(std::string_view words);

    /// <summary>
    /// Whether a benchmark is armed and not done: the mission plays on past its end, the updater waits, and the
    /// settings it changed aren't saved.
    /// </summary>
    bool Running();
}

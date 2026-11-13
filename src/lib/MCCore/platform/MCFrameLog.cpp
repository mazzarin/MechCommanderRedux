#include "stdafx.h"
#include "platform/MCFrameLog.h"

namespace
{
    /// <summary>Frames seen before any is called slow: what is usual needs a few.</summary>
    constexpr size_t MinRecentFrames = 30;
    /// <summary>The last frames what is usual is the median of.</summary>
    constexpr size_t RecentFrames = 120;
    /// <summary>A slow frame is this many times the usual one...</summary>
    constexpr double SlowFactor = 1.5;
    /// <summary>...and this much longer (ms), or less where the screen refreshes more often (RefreshMarginShare).</summary>
    constexpr double SlowMarginMs = 6.0;
    /// <summary>
    /// The share of a refresh a frame later than the usual one is slow by: later by most of a refresh, it missed one
    /// (on a 240 Hz screen 3.75 ms, where SlowMarginMs alone let a frame shown twice as long go by).
    /// </summary>
    constexpr double RefreshMarginShare = 0.9;
    /// <summary>A frame longer than this many refreshes was on the screen for two or more.</summary>
    constexpr double LongFrameRefreshes = 1.5;
    constexpr double MsPerSecond = 1000.0;
    constexpr size_t Percentile99 = 99;
    constexpr size_t Percent = 100;

    /// <summary>Named amounts in the order first added (parts' ticks, counts).</summary>
    template <typename T> using MCNamed = std::vector<std::pair<std::string, T>>;

    template <typename T> void AddNamed(MCNamed<T>& named, std::string_view name, T amount)
    {
        for (auto& [known, total] : named)
        {
            if (known == name)
            {
                total += amount;
                return;
            }
        }

        named.emplace_back(std::string(name), amount);
    }

    /// <summary>The frames of the second running, for its line.</summary>
    struct MCSecond
    {
        uint64_t Start = 0;
        std::vector<double> Times;
        MCNamed<uint64_t> Parts;
        MCNamed<int64_t> Counts;
        size_t LongFrames = 0;
        double RefreshHz = 0.0;
    };

    struct MCFrameLogState
    {
        /// <summary>
        /// Guards the frame's parts, counts and notes, which come from any thread (MCFrameLog.h's Threads), and the
        /// log's generation. The rest is the game thread's alone (Open, Close, NextFrame), so the file is written
        /// without it: a render thread's part never waits for a write.
        /// </summary>
        std::mutex Lock;
        std::atomic<bool> Enabled = false;
        /// <summary>Which log is open (each Open and Close starts another), so a part begun in one adds to no other.</summary>
        std::atomic<uint64_t> Generation = 0;
        MCNamed<uint64_t> Parts;
        MCNamed<int64_t> Counts;
        std::vector<std::string> Notes;
        std::ofstream File;
        uint64_t Opened = 0;
        /// <summary>When the frame running started (none before the first).</summary>
        std::optional<uint64_t> FrameStart;
        uint64_t Frame = 0;
        /// <summary>The last frames' times (ms), for what is usual.</summary>
        std::deque<double> Recent;
        MCSecond Second;
        /// <summary>The stretch being tallied whole (MCFrameLog::StartTally), if one is.</summary>
        std::optional<MCFrameLog::MCTally> Tally;
    };

    /// <summary>A frame's parts, counts and notes, taken from the state to be written.</summary>
    struct MCFrameRecord
    {
        MCNamed<uint64_t> Parts;
        MCNamed<int64_t> Counts;
        std::vector<std::string> Notes;
    };

    MCFrameLogState& state()
    {
        static MCFrameLogState instance;
        return instance;
    }

    /// <summary>The tests' clock (MCFrameLog::SetClockForTests), else none.</summary>
    MCFrameLog::MCClock& testClock()
    {
        static MCFrameLog::MCClock clock;
        return clock;
    }

    uint64_t now()
    {
        const MCFrameLog::MCClock& clock = testClock();
        return clock ? clock() : SDL_GetPerformanceCounter();
    }

    double toMs(uint64_t ticks)
    {
        return static_cast<double>(ticks) * MsPerSecond / static_cast<double>(SDL_GetPerformanceFrequency());
    }

    double median(std::vector<double> times)
    {
        if (times.empty())
        {
            return 0.0;
        }

        std::ranges::nth_element(times, times.begin() + static_cast<ptrdiff_t>(times.size() / 2));
        return times[times.size() / 2];
    }

    /// <summary>The <paramref name="percent"/>th percentile of <paramref name="times"/>, as the second's line takes it.</summary>
    double percentile(std::vector<double> times, size_t percent)
    {
        if (times.empty())
        {
            return 0.0;
        }

        std::ranges::sort(times);
        return times[std::min(times.size() * percent / Percent, times.size() - 1)];
    }

    /// <summary>Whether a frame of <paramref name="ms"/> is slow against the usual one, on a screen of <paramref name="refreshHz"/>.</summary>
    bool isSlow(double ms, double usual, double refreshHz)
    {
        const double margin =
            refreshHz > 0.0 ? std::min(SlowMarginMs, MsPerSecond / refreshHz * RefreshMarginShare) : SlowMarginMs;
        return ms > usual * SlowFactor && ms > usual + margin;
    }

    void writeFrame(MCFrameLogState& log, const MCFrameRecord& frame, double ms, double usual, bool slow)
    {
        log.File << std::format("frame {} {:.2f} ms (usual {:.2f}){}\n", log.Frame, ms, usual, slow ? " SLOW" : "");

        for (const auto& [name, ticks] : frame.Parts)
        {
            log.File << std::format("    {:<22} {:8.2f}\n", name, toMs(ticks));
        }

        for (const auto& [name, amount] : frame.Counts)
        {
            log.File << std::format("    + {}: {}\n", name, amount);
        }

        for (const std::string& note : frame.Notes)
        {
            log.File << "    * " << note << "\n";
        }
    }

    /// <summary>The second's line: its frames' times, the long ones, each part's time and each count a frame.</summary>
    void writeSecond(MCFrameLogState& log, uint64_t end)
    {
        MCSecond& second = log.Second;
        std::vector<double> sorted = second.Times;
        std::ranges::sort(sorted);
        const double frames = static_cast<double>(sorted.size());
        log.File << std::format("-- {:.1f} s: {} frames, median {:.2f} ms, p99 {:.2f}, max {:.2f}",
                                toMs(end - log.Opened) / MsPerSecond, sorted.size(), sorted[sorted.size() / 2],
                                sorted[sorted.size() * Percentile99 / Percent], sorted.back());

        if (second.RefreshHz > 0.0)
        {
            log.File << std::format("; {} over {} refreshes at {:.0f} Hz", second.LongFrames, LongFrameRefreshes,
                                    second.RefreshHz);
        }

        log.File << "\n";

        if (!second.Parts.empty())
        {
            log.File << "   a frame:";

            for (size_t i = 0; i < second.Parts.size(); i++)
            {
                const auto& [name, ticks] = second.Parts[i];
                log.File << std::format("{} {} {:.2f}", i == 0 ? "" : ",", name, toMs(ticks) / frames);
            }

            log.File << "\n";
        }

        if (!second.Counts.empty())
        {
            log.File << "   counts:";

            for (size_t i = 0; i < second.Counts.size(); i++)
            {
                const auto& [name, amount] = second.Counts[i];
                log.File << std::format("{} {} {}", i == 0 ? "" : ",", name, amount);
            }

            log.File << "\n";
        }

        second = MCSecond{.Start = end};
    }
}

namespace MCFrameLog
{
    bool Open(const std::filesystem::path& path)
    {
        Close();
        MCFrameLogState& log = state();
        const std::scoped_lock lock(log.Lock);
        ++log.Generation;
        log.File.clear();
        log.File.open(path, std::ios::out | std::ios::trunc);
        log.Enabled = log.File.is_open();

        if (log.Enabled)
        {
            log.Opened = now();
            log.Second = MCSecond{.Start = log.Opened};
            log.File << "MCRedux frame log: frames much slower than the usual one or late by a refresh of the screen\n"
                        "(SLOW), frames with notes (*), and a line a second (--).\n"
                        "Times in ms. A part is listed when it ends, so after the parts inside it; render.* parts are\n"
                        "the render thread's, counted in the frame running as they end. + lines count what the frame\n"
                        "did. The GPU's own time isn't measured: present.acquire and present.wait wait for it and for\n"
                        "the screen.\n";
            log.File.flush();
        }

        return log.Enabled;
    }

    void Close()
    {
        MCFrameLogState& log = state();
        const std::scoped_lock lock(log.Lock);
        ++log.Generation;

        if (log.File.is_open())
        {
            log.File.close();
        }

        log.Enabled = false;
        log.FrameStart.reset();
        log.Frame = 0;
        log.Parts.clear();
        log.Counts.clear();
        log.Notes.clear();
        log.Recent.clear();
        log.Second = {};
        log.Tally.reset();
    }

    bool Enabled()
    {
        return state().Enabled;
    }

    void NextFrame(double refreshHz)
    {
        MCFrameLogState& log = state();

        if (!log.Enabled)
        {
            return;
        }

        const uint64_t frameEnd = now();
        MCFrameRecord frame;
        const std::optional<uint64_t> frameStart = std::exchange(log.FrameStart, frameEnd);

        if (const std::scoped_lock lock(log.Lock); frameStart.has_value())
        {
            frame = {std::exchange(log.Parts, {}), std::exchange(log.Counts, {}), std::exchange(log.Notes, {})};
        }
        else
        {
            // Before the first frame: what was timed is no frame's, but what was noted is the first's.
            log.Parts.clear();
            log.Counts.clear();
        }

        if (frameStart.has_value())
        {
            const double ms = toMs(frameEnd - *frameStart);
            const double usual = median(std::vector<double>(log.Recent.begin(), log.Recent.end()));
            const bool slow = log.Recent.size() >= MinRecentFrames && isSlow(ms, usual, refreshHz);

            if (slow || !frame.Notes.empty())
            {
                writeFrame(log, frame, ms, usual, slow);
                log.File.flush();
            }

            log.Recent.push_back(ms);

            if (log.Recent.size() > RecentFrames)
            {
                log.Recent.pop_front();
            }

            MCSecond& second = log.Second;
            second.Times.push_back(ms);
            second.RefreshHz = refreshHz;
            const bool longFrame = refreshHz > 0.0 && ms > MsPerSecond / refreshHz * LongFrameRefreshes;
            second.LongFrames += longFrame ? 1 : 0;

            if (log.Tally.has_value())
            {
                MCFrameLog::MCTally& tally = *log.Tally;
                tally.Times.push_back(ms);
                tally.Slow += slow ? 1 : 0;
                tally.Long += longFrame ? 1 : 0;
                tally.RefreshHz = refreshHz;

                for (const auto& [name, ticks] : frame.Parts)
                {
                    AddNamed(tally.PartsMs, name, toMs(ticks));
                }

                for (const auto& [name, amount] : frame.Counts)
                {
                    AddNamed(tally.Counts, name, amount);
                }
            }

            for (const auto& [name, ticks] : frame.Parts)
            {
                AddNamed(second.Parts, name, ticks);
            }

            for (const auto& [name, amount] : frame.Counts)
            {
                AddNamed(second.Counts, name, amount);
            }

            if (toMs(frameEnd - second.Start) >= MsPerSecond)
            {
                writeSecond(log, frameEnd);
                log.File.flush();
            }
        }

        ++log.Frame;
    }

    void Note(std::string text)
    {
        MCFrameLogState& log = state();

        if (log.Enabled)
        {
            const std::scoped_lock lock(log.Lock);
            log.Notes.push_back(std::move(text));
        }
    }

    void Count(std::string_view name, int64_t amount)
    {
        MCFrameLogState& log = state();

        if (log.Enabled)
        {
            const std::scoped_lock lock(log.Lock);
            AddNamed(log.Counts, name, amount);
        }
    }

    void SetClockForTests(MCClock clock)
    {
        testClock() = std::move(clock);
    }

    double MCTally::Seconds() const
    {
        double ms = 0.0;

        for (const double time : Times)
        {
            ms += time;
        }

        return ms / MsPerSecond;
    }

    double MCTally::MedianMs() const
    {
        return median(Times);
    }

    double MCTally::P99Ms() const
    {
        return percentile(Times, Percentile99);
    }

    double MCTally::WorstMs() const
    {
        return Times.empty() ? 0.0 : std::ranges::max(Times);
    }

    double MCTally::PartMs(std::string_view name) const
    {
        const auto found = std::ranges::find(PartsMs, name, &std::pair<std::string, double>::first);
        return found != PartsMs.end() ? found->second : 0.0;
    }

    int64_t MCTally::Count(std::string_view name) const
    {
        const auto found = std::ranges::find(Counts, name, &std::pair<std::string, int64_t>::first);
        return found != Counts.end() ? found->second : 0;
    }

    void StartTally()
    {
        MCFrameLogState& log = state();

        if (log.Enabled)
        {
            log.Tally.emplace();
        }
    }

    MCTally TakeTally()
    {
        MCFrameLogState& log = state();
        MCTally tally = log.Tally.value_or(MCTally{});
        log.Tally.reset();
        return tally;
    }

    Scope::Scope(std::string_view name) : _On(state().Enabled)
    {
        if (_On)
        {
            _Generation = state().Generation;
            _Name = name;
            _Start = now();
        }
    }

    Scope::~Scope()
    {
        if (!_On)
        {
            return;
        }

        const uint64_t ticks = now() - _Start;
        MCFrameLogState& log = state();
        const std::scoped_lock lock(log.Lock);

        // A part begun in a log since closed or opened again is no frame's of this one.
        if (log.Enabled && log.Generation == _Generation)
        {
            AddNamed(log.Parts, _Name, ticks);
        }
    }
}

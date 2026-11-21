#include "stdafx.h"
#include "platform/MCFrameLog.h"
#include "platform/MCMemoryRoom.h"

#include <condition_variable>
#include <stop_token>
#include <thread>

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
    /// <summary>
    /// With vsync, the most refreshes after its hand-over a frame is taken as shown (MCFrameLog::Presented): the queue
    /// for the screen holds a few frames, and a frame shown later than that was one the screen never waited for.
    /// </summary>
    constexpr double MostQueuedRefreshes = 3.0;
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
        /// <summary>The time between the frames handed to the screen in it (MCFrameLog::Presented), and the long ones.</summary>
        std::vector<double> Shown;
        size_t ShownLong = 0;
    };

    /// <summary>
    /// Port-only: the log's file, written on a thread of its own. The game's thread hands it the text written where it
    /// once wrote and flushed the file itself (hand), so a write the disk holds up never lengthens a frame: a frame's time
    /// starts as the last one ends, before that one's text is written, and under a backup's load such writes took
    /// 136-353 ms (21 Nov), counted in the next frame as time none of its parts took. The text handed and not yet written
    /// and whether some is being written are under Lock; the file is the thread's while it runs.
    /// </summary>
    struct MCFrameLogWriter
    {
        std::mutex Lock;
        std::condition_variable_any Changed;
        std::string Waiting;
        bool Writing = false;
        std::ofstream File;
        std::jthread Thread;
    };

    struct MCFrameLogState
    {
        /// <summary>
        /// Guards the frame's parts, counts and notes, which come from any thread (MCFrameLog.h's Threads), and the
        /// log's generation. The rest is the game thread's alone (Open, Close, NextFrame), so the text is written
        /// without it: a render thread's part never waits for a write.
        /// </summary>
        std::mutex Lock;
        std::atomic<bool> Enabled = false;
        /// <summary>Which log is open (each Open and Close starts another), so a part begun in one adds to no other.</summary>
        std::atomic<uint64_t> Generation = 0;
        MCNamed<uint64_t> Parts;
        MCNamed<int64_t> Counts;
        std::vector<std::string> Notes;
        /// <summary>The text written since it was last handed to the writer (hand).</summary>
        std::string Text;
        MCFrameLogWriter Writer;
        uint64_t Opened = 0;
        /// <summary>When the frame running started (none before the first).</summary>
        std::optional<uint64_t> FrameStart;
        uint64_t Frame = 0;
        /// <summary>The last frames' times (ms), for what is usual.</summary>
        std::deque<double> Recent;
        /// <summary>When the screen last showed a frame, as the log takes it (MCFrameLog::Presented); none before the first.</summary>
        std::optional<uint64_t> LastShown;
        /// <summary>When the frames were handed to the screen since the last NextFrame, for it to take as shown.</summary>
        std::vector<uint64_t> PendingPresents;
        /// <summary>The last of those times (ms), for what is usual on the screen.</summary>
        std::deque<double> RecentShown;
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

    /// <summary>The writer's thread: writes the text handed it as it comes, until stopped with none left.</summary>
    void writeHanded(const std::stop_token& stop, MCFrameLogWriter& writer)
    {
        std::unique_lock lock(writer.Lock);

        while (true)
        {
            writer.Changed.wait(lock, stop, [&writer] { return !writer.Waiting.empty(); });

            if (writer.Waiting.empty())
            {
                return;
            }

            const std::string text = std::exchange(writer.Waiting, {});
            writer.Writing = true;
            lock.unlock();
            writer.File << text;
            writer.File.flush();
            lock.lock();
            writer.Writing = false;
            writer.Changed.notify_all();
        }
    }

    /// <summary>Hands the text written so far to the writer's thread.</summary>
    void hand(MCFrameLogState& log)
    {
        if (log.Text.empty())
        {
            return;
        }

        {
            const std::scoped_lock lock(log.Writer.Lock);
            log.Writer.Waiting += log.Text;
        }

        log.Text.clear();
        log.Writer.Changed.notify_all();
    }

    double toMs(uint64_t ticks)
    {
        return static_cast<double>(ticks) * MsPerSecond / static_cast<double>(SDL_GetPerformanceFrequency());
    }

    uint64_t toTicks(double ms)
    {
        return static_cast<uint64_t>(
            std::llround(ms * static_cast<double>(SDL_GetPerformanceFrequency()) / MsPerSecond));
    }

    /// <summary>
    /// The time each frame handed over at <paramref name="presents"/> was on the screen after the one before it (ms),
    /// as MCFrameLog::Presented takes them: with <paramref name="vsync"/> and a known refresh, shown at the later of its
    /// hand-over and a refresh after the last (at most MostQueuedRefreshes after its hand-over); else as handed over.
    /// </summary>
    std::vector<double> takeShown(std::optional<uint64_t>& lastShown, std::span<const uint64_t> presents,
                                  double refreshHz, bool vsync)
    {
        std::vector<double> shown;
        const uint64_t refresh = vsync && refreshHz > 0.0 ? toTicks(MsPerSecond / refreshHz) : 0;

        for (const uint64_t at : presents)
        {
            uint64_t shownAt = at;

            if (refresh > 0 && lastShown.has_value())
            {
                shownAt = std::min(std::max(at, *lastShown + refresh),
                                   at + static_cast<uint64_t>(static_cast<double>(refresh) * MostQueuedRefreshes));
            }

            if (lastShown.has_value())
            {
                shown.push_back(toMs(shownAt - std::min(shownAt, *lastShown)));
            }

            lastShown = shownAt;
        }

        return shown;
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

    /// <summary>Whether a frame of <paramref name="ms"/> was on a screen of <paramref name="refreshHz"/> for two refreshes or more.</summary>
    bool isLong(double ms, double refreshHz)
    {
        return refreshHz > 0.0 && ms > MsPerSecond / refreshHz * LongFrameRefreshes;
    }

    /// <summary>The usual frame among <paramref name="recent"/> ones: their median.</summary>
    double usualOf(const std::deque<double>& recent)
    {
        return median(std::vector<double>(recent.begin(), recent.end()));
    }

    /// <summary>
    /// Whether a frame of <paramref name="ms"/> is slow against <paramref name="recent"/> ones (none is until there are
    /// enough to know the usual), which it then joins, the oldest let go.
    /// </summary>
    bool judgeAmong(std::deque<double>& recent, double ms, double usual, double refreshHz)
    {
        const bool slow = recent.size() >= MinRecentFrames && isSlow(ms, usual, refreshHz);
        recent.push_back(ms);

        if (recent.size() > RecentFrames)
        {
            recent.pop_front();
        }

        return slow;
    }

    /// <summary>A frame of <paramref name="ms"/> added to a tally's <paramref name="times"/>.</summary>
    void tallyFrame(MCFrameLog::MCFrameTimes& times, double ms, bool slow, bool longFrame)
    {
        times.Times.push_back(ms);
        times.Slow += slow ? 1 : 0;
        times.Long += longFrame ? 1 : 0;
    }

    /// <summary>The median, 99th percentile and longest of <paramref name="times"/> (some), as the second's line puts them.</summary>
    std::string timesText(std::vector<double> times)
    {
        std::ranges::sort(times);
        return std::format("median {:.2f} ms, p99 {:.2f}, max {:.2f}", times[times.size() / 2],
                           times[times.size() * Percentile99 / Percent], times.back());
    }

    void writeFrame(MCFrameLogState& log, const MCFrameRecord& frame, double ms, double usual, bool slow)
    {
        log.Text += std::format("frame {} {:.2f} ms (usual {:.2f}){}\n", log.Frame, ms, usual, slow ? " SLOW" : "");

        for (const auto& [name, ticks] : frame.Parts)
        {
            log.Text += std::format("    {:<22} {:8.2f}\n", name, toMs(ticks));
        }

        for (const auto& [name, amount] : frame.Counts)
        {
            log.Text += std::format("    + {}: {}\n", name, amount);
        }

        for (const std::string& note : frame.Notes)
        {
            log.Text += std::format("    * {}\n", note);
        }
    }

    /// <summary>The second's line: its frames' times, the long ones, each part's time and each count a frame.</summary>
    void writeSecond(MCFrameLogState& log, uint64_t end)
    {
        MCSecond& second = log.Second;
        const double frames = static_cast<double>(second.Times.size());
        log.Text += std::format("-- {:.1f} s: {} frames, {}", toMs(end - log.Opened) / MsPerSecond, second.Times.size(),
                                timesText(second.Times));

        if (second.RefreshHz > 0.0)
        {
            log.Text += std::format("; {} over {} refreshes at {:.0f} Hz", second.LongFrames, LongFrameRefreshes,
                                    second.RefreshHz);
        }

        if (!second.Shown.empty())
        {
            log.Text += std::format("; shown: {} frames, {}", second.Shown.size(), timesText(second.Shown));

            if (second.RefreshHz > 0.0)
            {
                log.Text += std::format(", {} over {} refreshes", second.ShownLong, LongFrameRefreshes);
            }
        }

        log.Text += '\n';

        if (!second.Parts.empty())
        {
            log.Text += "   a frame:";

            for (size_t i = 0; i < second.Parts.size(); i++)
            {
                const auto& [name, ticks] = second.Parts[i];
                log.Text += std::format("{} {} {:.2f}", i == 0 ? "" : ",", name, toMs(ticks) / frames);
            }

            log.Text += '\n';
        }

        if (!second.Counts.empty())
        {
            log.Text += "   counts:";

            for (size_t i = 0; i < second.Counts.size(); i++)
            {
                const auto& [name, amount] = second.Counts[i];
                log.Text += std::format("{} {} {}", i == 0 ? "" : ",", name, amount);
            }

            log.Text += '\n';
        }

        log.Text += MCMemoryRoom::LedgerLine();
        log.Text += '\n';
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
        log.Writer.File.clear();
        log.Writer.File.open(path, std::ios::out | std::ios::trunc);
        log.Enabled = log.Writer.File.is_open();

        if (log.Enabled)
        {
            log.Writer.Thread = std::jthread(writeHanded, std::ref(log.Writer));
            log.Opened = now();
            log.Second = MCSecond{.Start = log.Opened};
            log.Text +=
                "MCRedux frame log: frames much slower than the usual one or late by a refresh of the screen\n"
                "(SLOW), frames with notes (*), and a line a second (--): the game's loop's frames, then the\n"
                "frames as the screen showed them (shown: each as it was handed over, with vsync no sooner\n"
                "than a refresh after the last, as the queue of frames for the screen shows them).\n"
                "Times in ms. A part is listed when it ends, so after the parts inside it; render.* parts are\n"
                "the render thread's, counted in the frame running as they end. + lines count what the frame\n"
                "did. The GPU's own time isn't measured: present.acquire and present.wait wait for it and for\n"
                "the screen. memory: each holder's bytes (payload/allocated/retired, MB), each side's sum, what\n"
                "the game holds against half the machine's memory (and a card's against 90% of its room), and\n"
                "the system's figure: macOS's physical footprint (Metal's memory in it), Windows' private bytes\n"
                "and DXGI's local and non-local use, each with what no holder names (unnamed).\n";
            hand(log);
        }

        return log.Enabled;
    }

    void Close()
    {
        MCFrameLogState& log = state();
        const std::scoped_lock lock(log.Lock);
        ++log.Generation;

        // What's left written before the file closes.
        if (log.Writer.File.is_open())
        {
            hand(log);
            log.Writer.Thread.request_stop();
            log.Writer.Thread.join();
            log.Writer.File.close();
        }

        log.Text.clear();
        log.Enabled = false;
        log.FrameStart.reset();
        log.Frame = 0;
        log.Parts.clear();
        log.Counts.clear();
        log.Notes.clear();
        log.Recent.clear();
        log.LastShown.reset();
        log.PendingPresents.clear();
        log.RecentShown.clear();
        log.Second = {};
        log.Tally.reset();
    }

    bool Enabled()
    {
        return state().Enabled;
    }

    void NextFrame(double refreshHz, bool vsync)
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

        // The frames handed to the screen while this one ran, as it showed them (before the first, none's).
        const std::vector<uint64_t> presents = std::exchange(log.PendingPresents, {});
        const std::vector<double> shown = takeShown(log.LastShown, presents, refreshHz, vsync);

        if (frameStart.has_value())
        {
            const double ms = toMs(frameEnd - *frameStart);
            const double usual = usualOf(log.Recent);
            const bool slow = judgeAmong(log.Recent, ms, usual, refreshHz);

            if (slow || !frame.Notes.empty())
            {
                writeFrame(log, frame, ms, usual, slow);
                hand(log);
            }

            MCSecond& second = log.Second;
            second.Times.push_back(ms);
            second.RefreshHz = refreshHz;
            const bool longFrame = isLong(ms, refreshHz);
            second.LongFrames += longFrame ? 1 : 0;

            for (const double shownMs : shown)
            {
                const bool shownSlow = judgeAmong(log.RecentShown, shownMs, usualOf(log.RecentShown), refreshHz);
                const bool shownLong = isLong(shownMs, refreshHz);
                second.Shown.push_back(shownMs);
                second.ShownLong += shownLong ? 1 : 0;

                if (log.Tally.has_value())
                {
                    tallyFrame(log.Tally->Shown, shownMs, shownSlow, shownLong);
                }
            }

            if (log.Tally.has_value())
            {
                MCFrameLog::MCTally& tally = *log.Tally;
                tallyFrame(tally.Loop, ms, slow, longFrame);
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
                hand(log);
            }
        }

        ++log.Frame;
    }

    void Presented()
    {
        MCFrameLogState& log = state();

        if (!log.Enabled)
        {
            return;
        }

        log.PendingPresents.push_back(now());
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

    void Line(std::string_view text)
    {
        MCFrameLogState& log = state();

        if (log.Enabled)
        {
            log.Text += text;
            log.Text += '\n';
            hand(log);
        }
    }

    void WaitForWrites()
    {
        MCFrameLogWriter& writer = state().Writer;
        std::unique_lock lock(writer.Lock);
        writer.Changed.wait(lock, [&writer] { return writer.Waiting.empty() && !writer.Writing; });
    }

    void SetClockForTests(MCClock clock)
    {
        testClock() = std::move(clock);
    }

    double MCFrameTimes::Seconds() const
    {
        double ms = 0.0;

        for (const double time : Times)
        {
            ms += time;
        }

        return ms / MsPerSecond;
    }

    double MCFrameTimes::MedianMs() const
    {
        return median(Times);
    }

    double MCFrameTimes::P99Ms() const
    {
        return percentile(Times, Percentile99);
    }

    double MCFrameTimes::WorstMs() const
    {
        return Times.empty() ? 0.0 : std::ranges::max(Times);
    }

    void MCFrameTimes::Add(const MCFrameTimes& later)
    {
        Times.insert(Times.end(), later.Times.begin(), later.Times.end());
        Slow += later.Slow;
        Long += later.Long;
    }

    bool MCTally::TimedShown() const
    {
        return !Shown.Times.empty();
    }

    const MCFrameTimes& MCTally::Judged() const
    {
        return TimedShown() ? Shown : Loop;
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

    void MCTally::Add(const MCTally& later)
    {
        Loop.Add(later.Loop);
        Shown.Add(later.Shown);
        RefreshHz = later.Loop.Times.empty() ? RefreshHz : later.RefreshHz;

        for (const auto& [name, ms] : later.PartsMs)
        {
            AddNamed(PartsMs, name, ms);
        }

        for (const auto& [name, amount] : later.Counts)
        {
            AddNamed(Counts, name, amount);
        }
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

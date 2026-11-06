#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

// Port-only: a piece of an update's work on a thread of its own (the updater's check, staging and arming, the first
// start's download, a repair). Its progress and end are read from the main thread; asked to stop, its transfer to the
// release's host is cancelled at once (MCContentStage::MCSource::Cancel), so the join that follows is quick and no
// thread is ever left running as the game ends. Standard headers only: main/MCUpdater.h includes it, and the Mac's
// Sparkle backend includes that without the precompiled header.

namespace MCContentStage
{
    class MCSource;
}

namespace MCUpdateWork
{
    /// <summary>A transfer to the release's host, made for each piece of work on its thread; none when it can't be.</summary>
    using MCSourceMaker = std::function<std::unique_ptr<MCContentStage::MCSource>()>;

    /// <summary>What a work fails with when its maker makes no transfer.</summary>
    constexpr std::string_view NoSourceError = "the release's host has no usable address in this build";

    /// <summary>
    /// How long a join waits before it says, loudly, that the work hasn't stopped: a stop cancels the transfer at once,
    /// so past this something holds the work up. The join waits on all the same.
    /// </summary>
    constexpr std::chrono::seconds JoinWarning{10};

    /// <summary>
    /// What the content's steps tell as they go (MCContentStage::Stage and the like): the bytes in hand and expected (0:
    /// not known yet), and whether they're checking what an earlier attempt left (the bytes already downloaded, which
    /// count as in hand) rather than downloading.
    /// </summary>
    struct MCProgressReport
    {
        uint64_t Received = 0;
        uint64_t Expected = 0;
        bool Checking = false;

        bool operator==(const MCProgressReport&) const = default;
    };

    /// <summary>A step's progress callback: told each report, it returns false to stop.</summary>
    using MCProgressCallback = std::function<bool(const MCProgressReport&)>;

    /// <summary>
    /// The work's progress (its last report) and the bytes that have come over the network for it so far, which only
    /// grow. From any thread.
    /// </summary>
    class MCProgress
    {
    public:
        void Set(uint64_t received, uint64_t expected);
        void Set(const MCProgressReport& report);
        std::pair<uint64_t, uint64_t> Get() const;
        MCProgressReport Report() const;

        void AddNetwork(uint64_t bytes) { _Network += bytes; }
        uint64_t Network() const { return _Network; }

    private:
        mutable std::mutex _Mutex;
        MCProgressReport _Report;
        std::atomic<uint64_t> _Network = 0;
    };

    /// <summary>What kind of failure ended a piece of work, as the player is told it (the detail goes to the log).</summary>
    enum class MCFailureKind : uint8_t
    {
        /// <summary>Anything not below.</summary>
        Other,
        /// <summary>The release's host couldn't be reached, or the connection to it was lost, for as long as it was tried.</summary>
        Unreachable,
        /// <summary>The game's disk hasn't the room the work needs (<see cref="MCFailure::BytesShort"/>).</summary>
        DiskFull,
        /// <summary>The release's content needs a newer program than this one (the newest installer brings it).</summary>
        NewerProgram,
    };

    /// <summary>Why a piece of work failed: its kind, and the detail for the log.</summary>
    struct MCFailure
    {
        MCFailureKind Kind = MCFailureKind::Other;
        std::string Reason;
        /// <summary>DiskFull: how many more bytes the game's disk must have free.</summary>
        uint64_t BytesShort = 0;
    };

    /// <summary>
    /// A size as the game's screens show it (the UPDATE page, HD OPTIONS, the first start's window), in decimal units as
    /// the setup gives them: GB to a tenth from 1 GB ("5.5 GB"), else MB ("350.2 MB").
    /// </summary>
    std::string SizeText(uint64_t bytes);

    /// <summary>Bytes in of a whole, both in the whole's unit: "1.2 OF 5.5 GB".</summary>
    std::string ProgressText(uint64_t received, uint64_t whole);

    /// <summary>A download's speed, in decimal units: MB a second to a tenth from 1 MB ("10.4 MB/S"), else KB ("850 KB/S").</summary>
    std::string SpeedText(double bytesPerSecond);

    /// <summary>
    /// The time a download has left, roughly: "LESS THAN A MINUTE LEFT", "ABOUT 8 MIN LEFT", "ABOUT 1 HR 20 MIN LEFT"
    /// (to the nearest minute).
    /// </summary>
    std::string TimeLeftText(std::chrono::seconds left);

    /// <summary>
    /// A download's speed from the network bytes it has had (<see cref="MCProgress::Network"/>), sampled as the screen is
    /// drawn and smoothed over the last <see cref="SpeedWindow"/>.
    /// </summary>
    class MCRate
    {
    public:
        using MCClock = std::chrono::steady_clock;

        /// <summary>How far back the speed looks.</summary>
        static constexpr std::chrono::seconds SpeedWindow{10};
        /// <summary>How long the samples must span before the speed is told, and before a time left is.</summary>
        static constexpr std::chrono::seconds SpeedAfter{2};
        static constexpr std::chrono::seconds TimeLeftAfter{5};

        /// <summary>The network bytes had in all by <paramref name="at"/>.</summary>
        void Add(MCClock::time_point at, uint64_t networkBytes);

        /// <summary>Forgets every sample: the bytes between now and the next one aren't a download's (a check).</summary>
        void Reset() { _Samples.clear(); }

        /// <summary>The bytes a second over the last <see cref="SpeedWindow"/>; none until the samples span <see cref="SpeedAfter"/>.</summary>
        std::optional<double> BytesPerSecond() const;

        /// <summary>
        /// The time <paramref name="left"/> bytes take at that speed; none until the samples span
        /// <see cref="TimeLeftAfter"/>, or while nothing comes.
        /// </summary>
        std::optional<std::chrono::seconds> TimeLeft(uint64_t left) const;

        /// <summary>The line the first start's window shows under its bar: "10.4 MB/S · ABOUT 8 MIN LEFT" (empty: nothing yet).</summary>
        std::string Text(uint64_t left) const;

    private:
        /// <summary>How long the samples span.</summary>
        MCClock::duration Span() const;

        std::deque<std::pair<MCClock::time_point, uint64_t>> _Samples;
    };

    /// <summary>
    /// The progress callback the content's steps take (MCContentUpdater::Stage and the like): it reports to
    /// <paramref name="progress"/> and goes on until <paramref name="stop"/> is asked.
    /// </summary>
    MCProgressCallback Reporter(MCProgress& progress, std::stop_token stop);

    /// <summary>The goOn a check's planning takes (MCContentUpdater::Check and the like): true until <paramref name="stop"/> is asked.</summary>
    std::function<bool()> GoOn(std::stop_token stop);

    /// <summary>
    /// The transfers <paramref name="maker"/> makes, each byte they fetch or read (theirs and those of the transfers made
    /// from them) added to <paramref name="progress"/>'s network bytes, from whichever thread it comes on.
    /// </summary>
    MCSourceMaker Counted(MCSourceMaker maker, MCProgress& progress);

    /// <summary>
    /// The transfer for a piece of work, made by <c>maker</c> on the work's thread, and cancelled the moment the work is
    /// asked to stop, so a read stalled on the network ends then rather than at its timeout.
    /// </summary>
    class MCWorkSource
    {
    public:
        MCWorkSource(const MCSourceMaker& maker, std::stop_token stop);
        ~MCWorkSource();
        MCWorkSource(const MCWorkSource&) = delete;
        MCWorkSource& operator=(const MCWorkSource&) = delete;

        /// <summary>The transfer; none when the maker made none.</summary>
        MCContentStage::MCSource* Get() const { return _Source.get(); }

    private:
        std::unique_ptr<MCContentStage::MCSource> _Source;
        /// <summary>After the transfer: it goes first, and waits for a cancel under way.</summary>
        std::optional<std::stop_callback<std::function<void()>>> _Cancel;
    };

    /// <summary>
    /// Runs <paramref name="job"/> on the transfer <paramref name="maker"/> makes, cancelled when <paramref name="stop"/>
    /// is asked; with none, fails with <see cref="NoSourceError"/>.
    /// </summary>
    template <typename T, typename Job>
    std::expected<T, std::string> WithSource(const MCSourceMaker& maker, std::stop_token stop, Job&& job)
    {
        const MCWorkSource source(maker, std::move(stop));

        if (source.Get() == nullptr)
        {
            return std::unexpected(std::string(NoSourceError));
        }

        return std::forward<Job>(job)(*source.Get());
    }

    /// <summary>What a work is handed: its progress, and its stop.</summary>
    struct MCContext
    {
        MCProgress& Progress;
        std::stop_token Stop;
    };

    /// <summary>
    /// A thread that runs a body once, then marks it finished; its join waits for it (loudly past
    /// <see cref="JoinWarning"/>). <see cref="MCWork"/> holds its result.
    /// </summary>
    class MCWorkThread
    {
    public:
        /// <summary>Starts <paramref name="body"/>; <paramref name="name"/> says which work it is in the logs.</summary>
        MCWorkThread(std::string name, std::function<void(MCContext&)> body);
        ~MCWorkThread();
        MCWorkThread(const MCWorkThread&) = delete;
        MCWorkThread& operator=(const MCWorkThread&) = delete;

        /// <summary>Whether the body has ended (the join then doesn't wait).</summary>
        bool Finished() const;

        MCProgress& Progress() { return _Progress; }

        /// <summary>Asks the body to stop: its transfer is cancelled and its progress callback says to stop.</summary>
        void Stop();

        /// <summary>Waits for the body's end and its thread (once; again, it returns at once).</summary>
        void Join();

    private:
        std::string _Name;
        MCProgress _Progress;
        mutable std::mutex _Mutex;
        std::condition_variable _Ended;
        bool _Finished = false;
        /// <summary>Last: it starts once the rest is made.</summary>
        std::jthread _Thread;
    };

    /// <summary>
    /// A piece of work giving a <typeparamref name="T"/> or what failed (a <typeparamref name="E"/>), on a thread of its
    /// own from the moment it's made. Gone, it stops and is joined: nothing outlives it.
    /// </summary>
    template <typename T, typename E = std::string> class MCWork
    {
    public:
        using MCResult = std::expected<T, E>;

        MCWork(std::string name, std::function<MCResult(MCContext&)> job)
            : _Thread(std::move(name), [this, job = std::move(job)](MCContext& context) { _Result = job(context); })
        {
        }

        MCWork(const MCWork&) = delete;
        MCWork& operator=(const MCWork&) = delete;

        bool Finished() const { return _Thread.Finished(); }

        MCProgress& Progress() { return _Thread.Progress(); }

        void Stop() { _Thread.Stop(); }

        /// <summary>Waits for the work's end and gives its result (once).</summary>
        MCResult Join()
        {
            _Thread.Join();
            return std::move(*_Result);
        }

    private:
        /// <summary>Written on the work's thread before it ends; read only once it's joined.</summary>
        std::optional<MCResult> _Result;
        /// <summary>Last: it starts once the result's place is made, and is joined before it goes.</summary>
        MCWorkThread _Thread;
    };
}

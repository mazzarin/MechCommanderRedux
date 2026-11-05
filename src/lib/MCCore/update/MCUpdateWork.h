#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
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

    /// <summary>The work's progress: the bytes in so far, and of the whole (0: not known yet). From any thread.</summary>
    class MCProgress
    {
    public:
        void Set(uint64_t received, uint64_t expected);
        std::pair<uint64_t, uint64_t> Get() const;

    private:
        mutable std::mutex _Mutex;
        uint64_t _Received = 0;
        uint64_t _Expected = 0;
    };

    /// <summary>
    /// A size as the game's screens show it (the UPDATE page, HD OPTIONS, the first start's window), in decimal units as
    /// the setup gives them: GB to a tenth from 1 GB ("5.5 GB"), else MB ("350.2 MB").
    /// </summary>
    std::string SizeText(uint64_t bytes);

    /// <summary>Bytes in of a whole, both in the whole's unit: "1.2 OF 5.5 GB".</summary>
    std::string ProgressText(uint64_t received, uint64_t whole);

    /// <summary>
    /// The progress callback the content's steps take (MCContentUpdater::Stage and the like): it reports to
    /// <paramref name="progress"/> and goes on until <paramref name="stop"/> is asked.
    /// </summary>
    std::function<bool(uint64_t, uint64_t)> Reporter(MCProgress& progress, std::stop_token stop);

    /// <summary>The goOn a check's planning takes (MCContentUpdater::Check and the like): true until <paramref name="stop"/> is asked.</summary>
    std::function<bool()> GoOn(std::stop_token stop);

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
    /// A piece of work giving a <typeparamref name="T"/> or what failed, on a thread of its own from the moment it's
    /// made. Gone, it stops and is joined: nothing outlives it.
    /// </summary>
    template <typename T> class MCWork
    {
    public:
        using MCResult = std::expected<T, std::string>;

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

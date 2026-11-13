#pragma once

/// <summary>
/// Port-only: a log of slow frames, for finding hitches in play (<c>-framelog &lt;file&gt;</c>). Each frame's time is
/// split into named parts (<see cref="Scope"/>), counts what it did (<see cref="Count"/>) and carries notes of what
/// happened in it (<see cref="Note"/>); a frame much slower than the frames around it, or a skipped refresh of the
/// screen, is written out with them, and every second a line of its frames: their times, how many took longer than
/// a refresh and 1.5, and each part's time and each count a frame, so a slowdown that lasts shows with its parts too.
/// Costs a flag test per part while off.
/// </summary>
/// <remarks>
/// Threads: <see cref="Open"/> and <see cref="NextFrame"/> on the game's thread; parts, counts and notes on any (the
/// render thread's parts are named <c>render.*</c>), counted in the frame running as they end.
/// </remarks>
namespace MCFrameLog
{
    /// <summary>The clock the log reads, in ticks of SDL_GetPerformanceFrequency (the tests set their own).</summary>
    using MCClock = std::function<uint64_t()>;

    /// <summary>Starts logging into <paramref name="path"/> (replaced), ending a log already open.</summary>
    bool Open(const std::filesystem::path& path);

    /// <summary>Ends the log, if one is open.</summary>
    void Close();

    /// <summary>Whether a log is open.</summary>
    bool Enabled();

    /// <summary>
    /// Ends the frame running since the last call (written if slow) and starts the next; <paramref name="refreshHz"/> is
    /// the window's screen's refresh rate (0: not known), by which a frame that skipped a refresh is slow.
    /// </summary>
    void NextFrame(double refreshHz = 0.0);

    /// <summary>Adds <paramref name="text"/> to this frame's notes (a frame with notes is always written).</summary>
    void Note(std::string text);

    /// <summary>Adds <paramref name="amount"/> to this frame's count <paramref name="name"/> (a phrase: "HD frames read").</summary>
    void Count(std::string_view name, int64_t amount = 1);

    /// <summary>The tests' clock instead of the system's (none: the system's again), set while no frame runs.</summary>
    void SetClockForTests(MCClock clock);

    /// <summary>Adds the time from its construction to its end to the frame's part <c>Name</c>.</summary>
    class Scope
    {
    public:
        /// <summary>Starts timing part <paramref name="name"/> (copied only while a log is open).</summary>
        explicit Scope(std::string_view name);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        /// <summary>Whether a log was open as it started: only then is it timed.</summary>
        bool _On = false;
        /// <summary>The log open as it started (it adds to that one alone).</summary>
        uint64_t _Generation = 0;
        /// <summary>The part's name (empty while no log is open).</summary>
        std::string _Name;
        uint64_t _Start = 0;
    };
}

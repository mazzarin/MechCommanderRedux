#pragma once

#include "platform/MCServices.h"

/// <summary>
/// A clock that only the test moves (<see cref="Advance"/>), so a run doesn't depend on how fast the machine is. The
/// date and time of day are 2000-01-01 12:00:00 plus the clock, in UTC and local time alike.
/// </summary>
/// <remarks>
/// Reads don't move it, so it reads the same however often the game looks. A busy wait would then never end (the
/// palette fade, the 50 ms waits), so past 100,000 reads on the thread that made the clock (the game's) without an
/// advance, each read moves it on a microsecond.
/// </remarks>
class MCManualClock final : public MCClock
{
public:
    /// <summary>A clock at 0 owned by the calling thread (the game's).</summary>
    MCManualClock();

    /// <summary>Moves the clock on by <paramref name="nanoseconds"/>.</summary>
    void Advance(uint64_t nanoseconds);

    /// <summary>
    /// From now on (until <paramref name="on"/> is false) each present after the first since the clock last advanced
    /// moves it on by a 60 Hz refresh, as waiting for the display would. Loops that draw frames until some time has
    /// passed (the logistics screen wipes) then end after the frames they would take on a real display.
    /// </summary>
    void AdvanceOnPresent(bool on = true);

    /// <summary>
    /// Until given none: each read on the clock's thread first moves it on to <paramref name="target"/>'s time where
    /// that is ahead (the recorder keeping it with the real one through a frame, so budgets on the game's clock run out
    /// in real time, as in play).
    /// </summary>
    void Follow(std::function<uint64_t()> target);

    /// <summary>The clock in nanoseconds, without counting as a read.</summary>
    uint64_t Nanoseconds() const { return _Nanoseconds.load(); }

    uint32_t Milliseconds() override;
    int64_t PerformanceCounter() override;
    int64_t PerformanceFrequency() override;
    uint64_t UtcFileTime() override;
    uint64_t LocalFileTime() override;
    void Presented() override;

private:
    /// <summary>The clock, read (see the remarks).</summary>
    uint64_t Read();

    std::atomic<uint64_t> _Nanoseconds = 0;
    std::thread::id _Owner;
    /// <summary>The owner's reads since the clock last advanced.</summary>
    uint32_t _Reads = 0;
    bool _AdvanceOnPresent = false;
    /// <summary>The presents since the clock last advanced.</summary>
    uint32_t _Presents = 0;
    /// <summary>The time each read catches up with (<see cref="Follow"/>); none.</summary>
    std::function<uint64_t()> _Follow;
};

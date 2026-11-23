#include "stdafx.h"
#include "MCManualClock.h"

namespace
{
    /// <summary>2000-01-01 12:00:00 as FILETIME ticks (100 ns since 1601-01-01).</summary>
    uint64_t StartFileTime()
    {
        using namespace std::chrono;
        constexpr int64_t FileTimeEpochDays = 134774;
        constexpr uint64_t TicksPerDay = 864000000000ull;
        constexpr sys_days Start = year{2000} / January / 1;
        const auto startDays = static_cast<uint64_t>(Start.time_since_epoch().count() + FileTimeEpochDays);
        return startDays * TicksPerDay + 12 * 36000000000ull;
    }
}

MCManualClock::MCManualClock() : _Owner(std::this_thread::get_id())
{
}

void MCManualClock::Advance(uint64_t nanoseconds)
{
    _Nanoseconds += nanoseconds;
    _Reads = 0;
    _Presents = 0;
}

void MCManualClock::AdvanceOnPresent(bool on)
{
    _AdvanceOnPresent = on;
}

uint64_t MCManualClock::Read()
{
    if (std::this_thread::get_id() == _Owner && ++_Reads > 100000)
    {
        return _Nanoseconds += 1000;
    }

    return _Nanoseconds.load();
}

uint32_t MCManualClock::Milliseconds()
{
    return static_cast<uint32_t>(Read() / 1000000);
}

int64_t MCManualClock::PerformanceCounter()
{
    return static_cast<int64_t>(Read());
}

int64_t MCManualClock::PerformanceFrequency()
{
    return 1000000000;
}

uint64_t MCManualClock::UtcFileTime()
{
    return StartFileTime() + _Nanoseconds.load() / 100;
}

uint64_t MCManualClock::LocalFileTime()
{
    return UtcFileTime();
}

void MCManualClock::Presented()
{
    if (_AdvanceOnPresent && std::this_thread::get_id() == _Owner && ++_Presents > 1)
    {
        _Nanoseconds += 1000000000 / 60;
    }
}

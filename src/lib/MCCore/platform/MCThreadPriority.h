#pragma once

#include <cstdint>

/// <summary>
/// Port-only: a thread's scheduling priority as its system keeps it (macOS's quality-of-service class, Windows' thread
/// priority), taken from one thread to set on another.
/// </summary>
struct MCThreadPriority
{
    int32_t Value = 0;

    bool operator==(const MCThreadPriority&) const = default;
};

/// <summary>Port-only: the calling thread's priority.</summary>
MCThreadPriority MCCurrentThreadPriority();

/// <summary>
/// Port-only: gives the calling thread <paramref name="like"/>'s priority, or (<paramref name="below"/>) the one below it
/// that leaves its disk reads and cores as they are: on macOS the default class where <paramref name="like"/>'s is above
/// it (a thread the game starts has it; never utility or background, whose disk reads the system throttles and which
/// keep to the efficiency cores), on Windows the next priority down (never its background mode, which lowers the disk's
/// priority too). Elsewhere it does nothing.
/// </summary>
/// <returns>Whether the system took it.</returns>
bool MCSetCurrentThreadPriority(MCThreadPriority like, bool below);

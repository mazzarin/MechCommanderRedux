#pragma once

// Port-only: how long to wait before trying again after a failure, the one rule for the game's network clients: the
// INTERNET screen's list and the internet games' sockets coming back to the games service (network/MCInternetLobby,
// platform/MCServiceSignaling), and the updates' downloads trying their hosts again (update/MCHttpSource).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <random>

/// <summary>
/// The waits before each try again after failures: doubling from <see cref="First"/> up to <see cref="Most"/>, each
/// drawn at random from its last <see cref="Jitter"/> share, so the copies a service's deploy or outage drops at once
/// don't all come back at once; and, where <see cref="FirstSpread"/> says, the first anywhere up to that long.
/// </summary>
struct MCRetryDelay
{
    /// <summary>A source of random fractions, each in [0, 1): a test's own, or <see cref="ThreadRandom"/>.</summary>
    using MCRandom = std::function<double()>;

    /// <summary>Past this many doublings every wait is the most (and the doubling never overflows).</summary>
    static constexpr int32_t MostDoublings = 30;
    /// <summary>The share of each wait drawn at random by default: a wait is from half its longest to all of it.</summary>
    static constexpr double DefaultJitter = 0.5;

    /// <summary>The first wait's longest; each try's after it is twice the last's.</summary>
    std::chrono::milliseconds First{0};
    /// <summary>The longest any wait is.</summary>
    std::chrono::milliseconds Most{0};
    /// <summary>The share of each wait drawn at random (0 to 1): a wait is from (1 - Jitter) of its longest to all of it.</summary>
    double Jitter = DefaultJitter;
    /// <summary>The first wait anywhere from none to this long instead (0: as the others, from <see cref="First"/>).</summary>
    std::chrono::milliseconds FirstSpread{0};

    /// <summary>
    /// The longest the wait before try <paramref name="tries"/> (0: the first after a failure) is: <see cref="First"/>
    /// doubled for each try before it, at most <see cref="Most"/>.
    /// </summary>
    std::chrono::milliseconds Longest(int32_t tries) const
    {
        const std::chrono::milliseconds most = std::max(Most, std::chrono::milliseconds{0});
        std::chrono::milliseconds wait = std::max(First, std::chrono::milliseconds{0});

        for (int32_t doubling = 0; doubling < std::clamp(tries, 0, MostDoublings) && wait < most; doubling++)
        {
            wait *= 2;
        }

        return std::min(wait, most);
    }

    /// <summary>The wait before try <paramref name="tries"/> (0: the first after a failure), drawn from <paramref name="random"/>.</summary>
    std::chrono::milliseconds Wait(int32_t tries, const MCRandom& random) const
    {
        const double drawn = std::clamp(random(), 0.0, 1.0);

        if (tries <= 0 && FirstSpread > std::chrono::milliseconds{0})
        {
            return Part(FirstSpread, drawn);
        }

        // From all of the longest (drawn 0) down to its (1 - Jitter) share.
        return Part(Longest(tries), 1.0 - std::clamp(Jitter, 0.0, 1.0) * drawn);
    }

    /// <summary>The wait before try <paramref name="tries"/>, drawn from this thread's own random source.</summary>
    std::chrono::milliseconds Wait(int32_t tries) const { return Wait(tries, ThreadRandom); }

    /// <summary>A random fraction in [0, 1) from this thread's own generator, seeded from the system's random source.</summary>
    static double ThreadRandom()
    {
        thread_local std::mt19937_64 generator{std::random_device{}()};
        return std::uniform_real_distribution<double>(0.0, 1.0)(generator);
    }

private:
    /// <summary><paramref name="share"/> of <paramref name="wait"/>, to the nearest millisecond.</summary>
    static std::chrono::milliseconds Part(std::chrono::milliseconds wait, double share)
    {
        return std::chrono::round<std::chrono::milliseconds>(std::chrono::duration<double, std::milli>(wait) * share);
    }
};

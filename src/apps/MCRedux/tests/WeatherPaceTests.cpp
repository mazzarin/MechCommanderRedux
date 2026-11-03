#include "stdafx.h"
#include "MCTest.h"
#include "sprite/MCWeatherPace.h"

#include <algorithm>
#include <array>
#include <cmath>

// MCWeatherPace on modelled machines: a frame's work is the machine's own plus the weather's for the pieces in the air,
// which follow the share thrown over about the time they take to fall and rest. A display that waits for its next
// refresh (vsync) shows a late frame a whole refresh later; one that doesn't (a variable refresh) shows it when done.

namespace
{
    /// <summary>A 60 Hz display's refresh, seconds.</summary>
    constexpr double Refresh60 = 1.0 / 60.0;
    /// <summary>A 120 Hz display's.</summary>
    constexpr double Refresh120 = 1.0 / 120.0;
    /// <summary>How long the pieces in the air take to follow the share thrown (a flake's fall and rest).</summary>
    constexpr double PieceLife = 2.5;
    /// <summary>A frame within this much of the display's refresh is on time.</summary>
    constexpr double OnTime = 1.05;

    /// <summary>A machine and its display.</summary>
    struct MCPacedMachine
    {
        /// <summary>A frame's own work, seconds.</summary>
        double Base = 0.0;
        /// <summary>The weather's work with every piece in the air, seconds.</summary>
        double Weather = 0.0;
        double Refresh = Refresh60;
        /// <summary>Whether a late frame waits for the next refresh.</summary>
        bool VSync = true;

        /// <summary>The most of the pieces with which its frames are on time.</summary>
        double Affords() const { return (Refresh - Base) / Weather; }
    };

    /// <summary>What a run of a machine's frames did.</summary>
    struct MCPacedRun
    {
        /// <summary>Seconds of frames shown later than on time.</summary>
        double LateSeconds = 0.0;
        /// <summary>The least and the last share.</summary>
        float Least = 1.0f;
        float Last = 1.0f;
        /// <summary>The share of the pieces in the air at the end.</summary>
        double InAir = 1.0;
    };

    /// <summary>
    /// Runs <paramref name="machine"/>'s frames for <paramref name="seconds"/> under <paramref name="pace"/>, the
    /// pieces in the air starting at <paramref name="inAir"/>.
    /// </summary>
    MCPacedRun Run(MCWeatherPace& pace, const MCPacedMachine& machine, double seconds, double inAir = 1.0)
    {
        MCPacedRun run{.InAir = inAir};

        for (double time = 0.0; time < seconds;)
        {
            const double work = machine.Base + machine.Weather * run.InAir;
            const double frame = machine.VSync ? std::ceil(work / machine.Refresh - 1e-9) * machine.Refresh
                                               : std::max(work, machine.Refresh);
            pace.Frame(frame, machine.Refresh, true);
            run.InAir += (pace.Share() - run.InAir) * std::min(1.0, frame / PieceLife);
            run.LateSeconds += frame > machine.Refresh * OnTime ? frame : 0.0;
            run.Least = std::min(run.Least, pace.Share());
            time += frame;
        }

        run.Last = pace.Share();
        return run;
    }
}

TEST_CASE("hd weather: a machine whose frames keep up throws every piece")
{
    for (const bool vsync : {true, false})
    {
        MCWeatherPace pace;
        const MCPacedRun run = Run(pace, {.Base = 0.005, .Weather = 0.006, .VSync = vsync}, 120.0);
        CHECK_EQ(run.Least, 1.0f);
        CHECK_EQ(run.LateSeconds, 0.0);
    }
}

/// <summary>
/// A machine that can't keep up with the whole weather (an M1 Air's blizzard, a slower one's, a 120 Hz display's)
/// throws fewer pieces within half a minute, then keeps the frames on time but for the odd step up that doesn't hold,
/// and never far below what it affords.
/// </summary>
TEST_CASE("hd weather: a slow machine throws fewer pieces until its frames keep up, and holds there")
{
    const std::array machines{MCPacedMachine{.Base = 0.012, .Weather = 0.012},
                              MCPacedMachine{.Base = 0.010, .Weather = 0.014},
                              MCPacedMachine{.Base = 0.0047, .Weather = 0.007, .Refresh = Refresh120}};

    for (const MCPacedMachine& machine : machines)
    {
        MCWeatherPace pace;
        const MCPacedRun first = Run(pace, machine, 30.0);
        CHECK(first.Last <= machine.Affords());
        constexpr double heldSeconds = 600.0;
        const MCPacedRun held = Run(pace, machine, heldSeconds, first.InAir);
        CHECK(held.LateSeconds < heldSeconds * 0.04);
        CHECK(held.Least > machine.Affords() * 0.5);
        CHECK(held.Last <= machine.Affords());
    }
}

/// <summary>Without vsync (a variable refresh), the frames end within a tenth of the display's.</summary>
TEST_CASE("hd weather: without vsync a slow machine's frames end near the display's rate")
{
    MCWeatherPace pace;
    const MCPacedMachine machine{.Base = 0.010, .Weather = 0.014, .VSync = false};
    const MCPacedRun run = Run(pace, machine, 120.0);
    const double work = machine.Base + machine.Weather * run.InAir;
    CHECK(work <= machine.Refresh * 1.1);
    CHECK(run.Last >= machine.Affords() * 0.8);
}

TEST_CASE("hd weather: a machine too slow even without the weather keeps the least of its pieces")
{
    MCWeatherPace pace;
    const MCPacedRun run = Run(pace, {.Base = 0.025, .Weather = 0.010}, 60.0);
    CHECK_EQ(run.Last, MCWeatherPace::LeastShare);
}

/// <summary>
/// Once the weather costs less (the view zoomed in, the snow easing), every piece comes back within a few minutes,
/// however often the steps up failed before.
/// </summary>
TEST_CASE("hd weather: the pieces come back once the frames have time for them")
{
    MCWeatherPace pace;
    const MCPacedRun slow = Run(pace, {.Base = 0.010, .Weather = 0.014}, 300.0);
    REQUIRE(slow.Last < 0.6f);
    const MCPacedRun eased = Run(pace, {.Base = 0.010, .Weather = 0.003}, 240.0, slow.InAir);
    CHECK_EQ(eased.Last, 1.0f);
    CHECK_EQ(eased.LateSeconds, 0.0);
}

/// <summary>Frames without the weather's pieces, and loads' and hitches' long frames, change nothing.</summary>
TEST_CASE("hd weather: frames without the pieces, loads and hitches leave the pieces as they were")
{
    MCWeatherPace pace;

    for (int frame = 0; frame < 3600; frame++)
    {
        pace.Frame(Refresh60 * 3.0, Refresh60, false);
    }

    for (int frame = 0; frame < 200; frame++)
    {
        pace.Frame(MCWeatherPace::MostFrameSeconds * 1.5, Refresh60, true);
    }

    CHECK_EQ(pace.Share(), 1.0f);
    CHECK_EQ(MCWeatherPacing::Share(), 1.0f);
}

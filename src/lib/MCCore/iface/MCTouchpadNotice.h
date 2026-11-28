#pragma once

// Port-only (Redux HD): the one-time notice that the touchpad may be dropping orders. Some laptops' touchpad drivers
// ignore the pad's clicks while a key other than Shift, Ctrl or Alt is held, so Space + click (run) or L + click (long
// range) never reaches Windows, and nothing above the driver can bring the click back: the HP EliteBook's Synaptics
// driver (19.0) drops them while such a key is held, PalmCheck at its lowest or not. The notice suggests both. The
// driver leaves an empty packet in the mouse's raw input where each press and release was (the app's
// MCTouchpadWatchWin.cpp watches a built-in pointing device's for them, never a plugged-in mouse's); two within
// PairWithin with a key held are a click swallowed. The first in a battle shows the notice over the battlefield, single
// player waiting under it; it never shows again (PREFS "TouchpadNoticeSeen"). Standard headers only: the Windows
// watcher, built without the precompiled header, includes this.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

namespace MCTouchpadNotice
{
    using Clock = std::chrono::steady_clock;

    /// <summary>The longest from a swallowed press's packet to its release's (the HP's: 110 to 160 ms).</summary>
    inline constexpr std::chrono::milliseconds PairWithin{1000};
    /// <summary>How long a swallowed click waits for the battle to be able to say so (the pause shown, a dialog).</summary>
    inline constexpr std::chrono::seconds SayWithin{2};

    /// <summary>The notice's words.</summary>
    inline constexpr std::string_view Eyebrow = "Touchpad";
    inline constexpr std::string_view Text =
        "Your touchpad may be dropping clicks made with a key held, like Space + click to run. You can try turning "
        "down palm rejection in your touchpad settings, or get a separate mouse.";

    /// <summary>
    /// The empty packets' pairing: one packet with a key held waits for another within <see cref="PairWithin"/>, and
    /// the pair is a click swallowed. One thread feeds it (the watcher's), any thread takes what it found.
    /// </summary>
    class MCSwallowedClicks
    {
    public:
        /// <summary>An empty packet with a key held, at <paramref name="at"/>.</summary>
        void EmptyPacket(Clock::time_point at);

        /// <summary>When the last click was swallowed, if one was since the last take; forgets it.</summary>
        std::optional<Clock::time_point> Take();

        /// <summary>Whether a swallowed click waits to be taken.</summary>
        bool Waiting() const { return _SwallowedAt.load() != None; }

    private:
        static constexpr int64_t None = 0;
        /// <summary>The unpaired packet's time (the clock's ticks), or <see cref="None"/>.</summary>
        int64_t _UnpairedAt = None;
        std::atomic<int64_t> _SwallowedAt = None;
    };

    /// <summary>The game's: the watcher feeds it (<see cref="EmptyPacket"/>), the battle takes from it.</summary>
    MCSwallowedClicks& Clicks();

    /// <summary>An empty packet with a key held (the watcher's thread): fed to <see cref="Clicks"/>.</summary>
    void EmptyPacket(Clock::time_point at);

    /// <summary>
    /// Each frame of a battle: a click swallowed in the last <see cref="SayWithin"/> and the notice never seen shows it
    /// over the battlefield (once the battle has started and while neither the pause nor a dialog shows), single player
    /// waiting under it until OK; it's seen from then (PREFS saved). One older is forgotten.
    /// </summary>
    void Tend();

    /// <summary>Whether the notice shows now.</summary>
    bool Shows();
}

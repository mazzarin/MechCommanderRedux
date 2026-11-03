#include "stdafx.h"
#include "sprite/MCWeatherPace.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "platform/MCDisplay.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

namespace
{
    /// <summary>A window whose mean frame is this much longer than the display's is behind.</summary>
    constexpr double BehindRatio = 1.10;
    /// <summary>A window whose mean frame is no more than this much longer than the display's keeps up.</summary>
    constexpr double KeepUpRatio = 1.03;
    /// <summary>Windows in a row behind before a cut (a second's: a moment's burst of fire cuts nothing).</summary>
    constexpr int BehindWindowsToCut = 2;
    /// <summary>A cut takes the share down by as much as the frames are behind, no less than to this...</summary>
    constexpr double DeepestCut = 0.6;
    /// <summary>...and no more than to this.</summary>
    constexpr double ShallowestCut = 0.85;
    /// <summary>A step back up.</summary>
    constexpr float RaiseStep = 0.1f;
    /// <summary>The display's refresh when the system doesn't say.</summary>
    constexpr double FallbackRefreshHz = 60.0;
}

void MCWeatherPace::Frame(double seconds, double refreshSeconds, bool drewPieces)
{
    if (!drewPieces || seconds <= 0.0 || seconds > MostFrameSeconds || refreshSeconds <= 0.0)
    {
        return;
    }

    _SinceChange += seconds;
    _Window += seconds;
    _WindowFrames++;

    if (_Window < WindowSeconds)
    {
        return;
    }

    const double lag = _Window / _WindowFrames / refreshSeconds;
    const double window = std::exchange(_Window, 0.0);
    _WindowFrames = 0;

    // A step up that the frames can't take goes back at once (the pieces it added are still coming), and the next
    // waits longer.
    if (_Raised && lag > BehindRatio)
    {
        _Share = std::max(LeastShare, _Share - RaiseStep);
        _RaiseWait = std::min(_RaiseWait * 2.0, LongestRaiseWait);
        _BehindWindows = 0;
        _KeptUp = 0.0;
        _SinceChange = 0.0;
        _Raised = false;
        return;
    }

    // The pieces thrown before the last change are still falling: what the frames show isn't the new share's yet.
    if (_SinceChange < SettleSeconds)
    {
        _BehindWindows = 0;
        _KeptUp = 0.0;
        return;
    }

    if (lag > BehindRatio)
    {
        _KeptUp = 0.0;

        if (++_BehindWindows < BehindWindowsToCut || _Share <= LeastShare)
        {
            return;
        }

        _Share = std::max(LeastShare, static_cast<float>(_Share * std::clamp(1.0 / lag, DeepestCut, ShallowestCut)));
        _BehindWindows = 0;
        _SinceChange = 0.0;
        _Raised = false;
        return;
    }

    _BehindWindows = 0;

    if (lag > KeepUpRatio)
    {
        _KeptUp = 0.0;
        return;
    }

    _KeptUp += window;

    if (_KeptUp < _RaiseWait || _Share >= 1.0f)
    {
        return;
    }

    // The last step up held: the next comes sooner.
    if (_Raised)
    {
        _RaiseWait = std::max(_RaiseWait / 2.0, FirstRaiseWait);
    }

    _Share = std::min(1.0f, _Share + RaiseStep);
    _KeptUp = 0.0;
    _SinceChange = 0.0;
    _Raised = true;
}

namespace MCWeatherPacing
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        /// <summary>The app's pace while followed.</summary>
        std::optional<MCWeatherPace> Pace;
        /// <summary>When the last frame ended.</summary>
        std::optional<Clock::time_point> LastFrame;

        /// <summary>Seconds between the frames the window's display shows.</summary>
        double RefreshSeconds()
        {
            const std::shared_ptr<MCDisplay> display = HasGuiSystem() ? GuiSystem().Display() : nullptr;
            const SDL_DisplayMode* mode =
                display != nullptr ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(&display->Window())) : nullptr;
            return 1.0 / (mode != nullptr && mode->refresh_rate > 0.0f ? mode->refresh_rate : FallbackRefreshHz);
        }
    }

    void Follow(bool on)
    {
        Pace = on ? std::optional<MCWeatherPace>(MCWeatherPace()) : std::nullopt;
        LastFrame.reset();
    }

    void Frame(bool drewPieces)
    {
        if (!Pace.has_value())
        {
            return;
        }

        const Clock::time_point now = Clock::now();
        const std::optional<Clock::time_point> last = std::exchange(LastFrame, now);

        if (last.has_value())
        {
            Pace->Frame(std::chrono::duration<double>(now - *last).count(), RefreshSeconds(),
                        drewPieces && !LockFrameRate);
        }
    }

    float Share()
    {
        return Pace.has_value() ? Pace->Share() : 1.0f;
    }
}

#include "stdafx.h"
#include "gui/MCScreenChange.h"
#include "platform/MCDisplay.h"
#include "platform/MCFrameLog.h"
#include "platform/MCInput.h"

namespace
{
    using MCScreenChange::MCPhase;

    /// <summary>Milliseconds in a second, for the frame log's times.</summary>
    constexpr double MsPerSecond = 1000.0;
    /// <summary>A frame on the screen this many refreshes or longer was late (MCFrameLog's own rule).</summary>
    constexpr double LateRefreshes = 1.5;

    /// <summary>The change running, and what the frame log is told of it.</summary>
    struct MCChange
    {
        /// <summary>Its name (the frame log's).</summary>
        std::string Name;
        MCPhase Phase = MCPhase::None;
        /// <summary>The game's clock (ms) at the phase's start; none until the phase's first frame is drawn.</summary>
        std::optional<uint32_t> PhaseStart;
        /// <summary>The real clock (SDL's counter) at the press, the fade-out shown black, the work done.</summary>
        uint64_t Pressed = 0;
        uint64_t Darkened = 0;
        uint64_t WorkDone = 0;
        /// <summary>The real clock at the fade-in's first frame shown (0 before it).</summary>
        uint64_t FirstFrame = 0;
        /// <summary>The real clock at the last frame shown since the press.</summary>
        uint64_t LastShown = 0;
        /// <summary>Each frame's time since the press (ms): from the press, or the frame before, to its present.</summary>
        std::vector<double> Frames;
        /// <summary>The frames shown dark (the work's).</summary>
        size_t DarkFrames = 0;
    };

    MCChange Change;
    size_t ChangesFinished = 0;
    /// <summary>The last change to end: its name and its press (for a change carrying on from it).</summary>
    std::string LastName;
    uint64_t LastPressed = 0;
    /// <summary>The running change's press carried from the one it carries on from (0: none).</summary>
    uint64_t CarriedPress = 0;
    std::string CarriedFrom;

    /// <summary>Milliseconds between two of SDL's counter's readings.</summary>
    double MsBetween(uint64_t from, uint64_t to)
    {
        return static_cast<double>(to - from) * MsPerSecond / static_cast<double>(SDL_GetPerformanceFrequency());
    }

    /// <summary>The refresh rate of the window's screen (Hz); 0 when not known.</summary>
    double RefreshHz()
    {
        const std::shared_ptr<MCDisplay> display = MCInput::Display();

        if (display == nullptr)
        {
            return 0.0;
        }

        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(&display->Window()));
        return mode != nullptr ? static_cast<double>(mode->refresh_rate) : 0.0;
    }

    /// <summary>The frame log's line for the change as it ends: the press to its first whole frame, and its frames.</summary>
    void LogChange()
    {
        if (!MCFrameLog::Enabled())
        {
            return;
        }

        const double refresh = RefreshHz();
        const double refreshMs = refresh > 0.0 ? MsPerSecond / refresh : 0.0;
        const double longest = Change.Frames.empty() ? 0.0 : std::ranges::max(Change.Frames);
        const auto over = refreshMs > 0.0 ? std::ranges::count_if(Change.Frames, [refreshMs](double ms)
                                                                  { return ms > refreshMs * LateRefreshes; })
                                          : 0;
        MCFrameLog::Line(std::format(
            "change {}: the press to the first whole frame {:.1f} ms (out {:.1f}, dark {:.1f}, the work {:.1f}); "
            "{} frames to the fade-in's end ({} dark), the longest {:.1f} ms, {} over {} refreshes of {:.2f} ms",
            Change.Name, MsBetween(Change.Pressed, Change.FirstFrame), MsBetween(Change.Pressed, Change.Darkened),
            MsBetween(Change.Darkened, Change.FirstFrame), MsBetween(Change.Darkened, Change.WorkDone),
            Change.Frames.size(), Change.DarkFrames, longest, over, LateRefreshes, refreshMs));

        if (CarriedPress != 0)
        {
            MCFrameLog::Line(std::format("change {}: from {}'s press to the first whole frame {:.1f} ms", Change.Name,
                                         CarriedFrom, MsBetween(CarriedPress, Change.FirstFrame)));
        }
    }
}

auto MCScreenChange::Start(std::string_view name, std::string_view after) -> bool
{
    if (Change.Phase != MCPhase::None || MCInput::Display() == nullptr)
    {
        return false;
    }

    const bool carries = !after.empty() && after == LastName;
    CarriedPress = carries ? LastPressed : 0;
    CarriedFrom = carries ? std::string(after) : std::string();
    Change = MCChange{.Name = std::string(name), .Phase = MCPhase::Out};
    Change.Pressed = SDL_GetPerformanceCounter();
    Change.LastShown = Change.Pressed;
    MCFrameLog::Note(std::format("change {}: pressed", Change.Name));
    return true;
}

auto MCScreenChange::Phase() -> MCPhase
{
    return Change.Phase;
}

auto MCScreenChange::Name() -> std::string_view
{
    return Change.Name;
}

auto MCScreenChange::Dark() -> bool
{
    return Change.Phase == MCPhase::Dark;
}

auto MCScreenChange::FadeIn() -> void
{
    if (Change.Phase != MCPhase::Dark)
    {
        return;
    }

    Change.Phase = MCPhase::In;
    Change.PhaseStart.reset();
    Change.WorkDone = SDL_GetPerformanceCounter();
}

auto MCScreenChange::Running() -> bool
{
    return Change.Phase != MCPhase::None;
}

auto MCScreenChange::HoldsInput() -> bool
{
    return Running();
}

auto MCScreenChange::FadeNow() -> int
{
    switch (Change.Phase)
    {
        case MCPhase::None:
            return 0;
        case MCPhase::Dark:
            return Black;
        case MCPhase::Out:
        case MCPhase::In:
            break;
    }

    // The phase's clock starts with its first frame, so a frame drawn long after the press (or the work) still
    // starts the fade from its end.
    const uint32_t now = MCPort::Milliseconds();

    if (!Change.PhaseStart.has_value())
    {
        Change.PhaseStart = now;
    }

    const uint32_t elapsed = std::min(now - *Change.PhaseStart, FadeMs);
    const int level = static_cast<int>(static_cast<uint64_t>(elapsed) * Black / FadeMs);
    return Change.Phase == MCPhase::Out ? level : Black - level;
}

auto MCScreenChange::Presented(int fade) -> void
{
    if (Change.Phase == MCPhase::None)
    {
        return;
    }

    const uint64_t now = SDL_GetPerformanceCounter();
    Change.Frames.push_back(MsBetween(Change.LastShown, now));
    Change.LastShown = now;

    switch (Change.Phase)
    {
        case MCPhase::Out:
        {
            if (fade >= Black)
            {
                Change.Phase = MCPhase::Dark;
                Change.PhaseStart.reset();
                Change.Darkened = now;
                MCFrameLog::Note(std::format("change {}: dark", Change.Name));

                // The black frame shown before the work holds the game's thread (a frame presented ahead is shown only
                // as the game's thread checks for it): else the last of the fade-out would sit on screen through it.
                if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
                {
                    display->ShowPresented();
                }
            }
            break;
        }
        case MCPhase::Dark:
            Change.DarkFrames++;
            break;
        case MCPhase::In:
        {
            if (Change.FirstFrame == 0)
            {
                Change.FirstFrame = now;
                MCFrameLog::Note(std::format("change {}: the first whole frame", Change.Name));
            }

            if (fade <= 0)
            {
                LogChange();
                LastName = Change.Name;
                LastPressed = CarriedPress != 0 ? CarriedPress : Change.Pressed;
                CarriedPress = 0;
                Change = MCChange{};
                ChangesFinished++;
            }
            break;
        }
        case MCPhase::None:
            break;
    }
}

auto MCScreenChange::Cancel() -> void
{
    if (Change.Phase != MCPhase::None)
    {
        MCFrameLog::Note(std::format("change {}: called off", Change.Name));

        if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
        {
            display->SetFade(0);
        }
    }

    Change = MCChange{};
    CarriedPress = 0;
}

auto MCScreenChange::Finished() -> size_t
{
    return ChangesFinished;
}

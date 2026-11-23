#include "stdafx.h"
#include "gui/MCScreenChange.h"
#include "platform/MCDisplay.h"
#include "platform/MCFrameLog.h"
#include "platform/MCInput.h"

namespace
{
    using MCScreenChange::MCChange;
    using MCScreenChange::MCFadeOut;
    using MCScreenChange::MCPhase;
    using MCScreenChange::MCPiece;

    /// <summary>Milliseconds in a second, for the frame log's times.</summary>
    constexpr double MsPerSecond = 1000.0;
    /// <summary>A frame on the screen this many refreshes or longer was late (MCFrameLog's own rule).</summary>
    constexpr double LateRefreshes = 1.5;

    /// <summary>The change running: its description, where it is, and what the frame log is told of it.</summary>
    struct MCRunning
    {
        MCChange Change;
        MCPhase Phase = MCPhase::None;
        /// <summary>The next of the job's pieces, and of the dark work's.</summary>
        size_t NextJob = 0;
        size_t NextDark = 0;
        /// <summary>The game's clock (ms) at the phase's start; none until the phase's first frame is drawn.</summary>
        std::optional<uint32_t> PhaseStart;
        /// <summary>
        /// The real clock (SDL's counter) at the press, the job done, the fade-out's start, the fade-out shown black, the
        /// work done.
        /// </summary>
        uint64_t Pressed = 0;
        uint64_t JobDone = 0;
        uint64_t OutStarted = 0;
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

    MCRunning Current;
    size_t ChangesFinished = 0;
    /// <summary>Counts the changes started and called off, so a piece that ended its own change is seen to have.</summary>
    uint64_t Generation = 0;
    /// <summary>Whether <see cref="MCScreenChange::Tend"/> runs a piece (one that draws frames of its own runs no other).</summary>
    bool Tending = false;
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

    /// <summary>
    /// Milliseconds on the game's clock since its counter read <paramref name="from"/>: the frame's budget is the game's
    /// time, so a test's manual clock gives every run the same frames.
    /// </summary>
    double GameMsSince(int64_t from)
    {
        return static_cast<double>(MCPort::PerformanceCounter() - from) * MsPerSecond /
               static_cast<double>(MCPort::PerformanceFrequency());
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

    /// <summary>Runs <paramref name="run"/>, its time in the frame log's part <paramref name="part"/> (none when empty).</summary>
    void RunInPart(std::string_view part, const std::function<void()>& run)
    {
        if (run == nullptr)
        {
            return;
        }

        if (part.empty())
        {
            run();
            return;
        }

        const MCFrameLog::Scope scope(part);
        run();
    }

    /// <summary>Whether the job has pieces left.</summary>
    bool JobLeft()
    {
        return Current.NextJob < Current.Change.Job.size();
    }

    /// <summary>Whether the change has pieces left that run in its phase now: the job's, then once dark the dark work's.</summary>
    bool PiecesLeft()
    {
        switch (Current.Phase)
        {
            case MCPhase::Working:
                return JobLeft();
            case MCPhase::Out:
                return Current.Change.FadeOut == MCFadeOut::AtPress && JobLeft();
            case MCPhase::Dark:
                return JobLeft() || Current.NextDark < Current.Change.Dark.size();
            case MCPhase::None:
            case MCPhase::In:
                break;
        }

        return false;
    }

    /// <summary>What running a piece came to.</summary>
    enum class MCRan
    {
        /// <summary>Done: the next piece may run.</summary>
        Done,
        /// <summary>A wait not over: the pieces after it wait for the next frame.</summary>
        Waiting,
        /// <summary>The piece ended its own change (the game stopping, the player calling it off).</summary>
        Ended
    };

    /// <summary>
    /// Runs the change's next piece in its phase (<see cref="PiecesLeft"/>): its run once, then its wait, if any, asked
    /// (the piece stays next while the wait isn't over).
    /// </summary>
    MCRan RunNextPiece()
    {
        const bool job = JobLeft();
        MCPiece& piece = job ? Current.Change.Job[Current.NextJob] : Current.Change.Dark[Current.NextDark];
        // Moved out first: the piece's work may end the change, and its description with it.
        const std::function<void()> run = std::move(piece.Run);
        piece.Run = nullptr;
        const std::string_view part = piece.Part;
        const uint64_t generation = Generation;
        Tending = true;
        RunInPart(part, run);
        Tending = false;

        if (generation != Generation)
        {
            return MCRan::Ended;
        }

        MCPiece& waiting = job ? Current.Change.Job[Current.NextJob] : Current.Change.Dark[Current.NextDark];

        if (waiting.Until != nullptr)
        {
            // The wait copied: it may end the change too.
            const std::function<bool()> until = waiting.Until;
            Tending = true;
            const bool over = until();
            Tending = false;

            if (generation != Generation)
            {
                return MCRan::Ended;
            }

            if (!over)
            {
                return MCRan::Waiting;
            }
        }

        (job ? Current.NextJob : Current.NextDark)++;

        if (Current.JobDone == 0 && !JobLeft())
        {
            Current.JobDone = SDL_GetPerformanceCounter();
            MCFrameLog::Note(std::format("change {}: the job done", Current.Change.Name));
        }

        return MCRan::Done;
    }

    /// <summary>The screen shown starts to fade out (from its next frame).</summary>
    void StartFadeOut(uint64_t at)
    {
        Current.Phase = MCPhase::Out;
        Current.PhaseStart.reset();
        Current.OutStarted = at;
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
        const std::vector<double>& frames = Current.Frames;
        const double longest = frames.empty() ? 0.0 : std::ranges::max(frames);
        const auto over = refreshMs > 0.0 ? std::ranges::count_if(frames, [refreshMs](double ms)
                                                                  { return ms > refreshMs * LateRefreshes; })
                                          : 0;
        MCFrameLog::Line(
            std::format("change {}: the press to the first whole frame {:.1f} ms (the job {:.1f}, out {:.1f}, dark "
                        "{:.1f}, the work "
                        "{:.1f}); {} frames to the fade-in's end ({} dark), the longest {:.1f} ms, {} over {} "
                        "refreshes of {:.2f} ms",
                        Current.Change.Name, MsBetween(Current.Pressed, Current.FirstFrame),
                        MsBetween(Current.Pressed, Current.JobDone), MsBetween(Current.OutStarted, Current.Darkened),
                        MsBetween(Current.Darkened, Current.FirstFrame), MsBetween(Current.Darkened, Current.WorkDone),
                        frames.size(), Current.DarkFrames, longest, over, LateRefreshes, refreshMs));

        if (CarriedPress != 0)
        {
            MCFrameLog::Line(std::format("change {}: from {}'s press to the first whole frame {:.1f} ms",
                                         Current.Change.Name, CarriedFrom,
                                         MsBetween(CarriedPress, Current.FirstFrame)));
        }
    }
}

auto MCScreenChange::Start(MCChange change) -> bool
{
    if (Current.Phase != MCPhase::None || MCInput::Display() == nullptr)
    {
        return false;
    }

    const bool carries = !change.After.empty() && change.After == LastName;
    CarriedPress = carries ? LastPressed : 0;
    CarriedFrom = carries ? change.After : std::string();
    const uint64_t now = SDL_GetPerformanceCounter();
    Generation++;
    Current = MCRunning{.Change = std::move(change), .Pressed = now, .LastShown = now};
    MCFrameLog::Note(std::format("change {}: pressed", Current.Change.Name));

    if (Current.Change.Job.empty())
    {
        Current.JobDone = now;
    }

    // A job to do first, live; black already, dark from the press; else the fade-out from the press.
    if (Current.Change.FadeOut == MCFadeOut::AfterJob && !Current.Change.Job.empty())
    {
        Current.Phase = MCPhase::Working;
    }
    else if (Current.Change.FadeOut == MCFadeOut::FromBlack)
    {
        Current.Phase = MCPhase::Dark;
        Current.OutStarted = now;
        Current.Darkened = now;
    }
    else
    {
        StartFadeOut(now);
    }

    return true;
}

auto MCScreenChange::Tend() -> void
{
    const MCPhase phase = Current.Phase;

    if (Tending || (phase != MCPhase::Working && phase != MCPhase::Out && phase != MCPhase::Dark))
    {
        return;
    }

    if (PiecesLeft())
    {
        const int64_t started = MCPort::PerformanceCounter();
        MCRan ran = MCRan::Done;

        // At least one piece a frame: a slow one gets a frame of its own, so the screen is never held long between them.
        // A wait not over leaves the rest for the next frame.
        do
        {
            ran = RunNextPiece();
        } while (ran == MCRan::Done && PiecesLeft() && GameMsSince(started) < FrameBudgetMs);

        if (ran != MCRan::Done || PiecesLeft())
        {
            return;
        }
    }

    const uint64_t now = SDL_GetPerformanceCounter();

    switch (phase)
    {
        case MCPhase::Working:
        {
            StartFadeOut(now);
            break;
        }
        case MCPhase::Dark:
        {
            // The work done: the next frame drawn is the next screen's first, and it fades in from there.
            Current.Phase = MCPhase::In;
            Current.PhaseStart.reset();
            Current.WorkDone = now;
            break;
        }
        case MCPhase::None:
        case MCPhase::Out:
        case MCPhase::In:
            break;
    }
}

auto MCScreenChange::RunPieces(std::span<const MCPiece> pieces) -> void
{
    const bool tending = Tending;
    Tending = true;

    for (const MCPiece& piece : pieces)
    {
        RunInPart(piece.Part, piece.Run);

        // A wait done without a change: asked until it's over (nothing else to draw meanwhile).
        while (piece.Until != nullptr && !piece.Until())
        {
            SDL_Delay(1);
        }
    }

    Tending = tending;
}

auto MCScreenChange::Phase() -> MCPhase
{
    return Current.Phase;
}

auto MCScreenChange::Name() -> std::string_view
{
    return Current.Change.Name;
}

auto MCScreenChange::Dark() -> bool
{
    return Current.Phase == MCPhase::Dark;
}

auto MCScreenChange::JobDone() -> bool
{
    return Current.Phase != MCPhase::None && Current.JobDone != 0;
}

auto MCScreenChange::InPiece() -> bool
{
    return Tending;
}

auto MCScreenChange::Running() -> bool
{
    return Current.Phase != MCPhase::None;
}

auto MCScreenChange::Fading() -> bool
{
    return Current.Phase == MCPhase::Out || Current.Phase == MCPhase::Dark || Current.Phase == MCPhase::In;
}

auto MCScreenChange::HoldsInput() -> bool
{
    return Fading();
}

auto MCScreenChange::FadeNow() -> int
{
    switch (Current.Phase)
    {
        case MCPhase::None:
        case MCPhase::Working:
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

    if (!Current.PhaseStart.has_value())
    {
        Current.PhaseStart = now;
    }

    const uint32_t elapsed = std::min(now - *Current.PhaseStart, FadeMs);
    const int level = static_cast<int>(static_cast<uint64_t>(elapsed) * Black / FadeMs);
    return Current.Phase == MCPhase::Out ? level : Black - level;
}

auto MCScreenChange::Presented(int fade) -> void
{
    if (Current.Phase == MCPhase::None)
    {
        return;
    }

    const uint64_t now = SDL_GetPerformanceCounter();
    Current.Frames.push_back(MsBetween(Current.LastShown, now));
    Current.LastShown = now;

    switch (Current.Phase)
    {
        case MCPhase::Out:
        {
            if (fade >= Black)
            {
                Current.Phase = MCPhase::Dark;
                Current.PhaseStart.reset();
                Current.Darkened = now;
                MCFrameLog::Note(std::format("change {}: dark", Current.Change.Name));

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
            Current.DarkFrames++;
            break;
        case MCPhase::In:
        {
            if (Current.FirstFrame == 0)
            {
                Current.FirstFrame = now;
                MCFrameLog::Note(std::format("change {}: the first whole frame", Current.Change.Name));
            }

            if (fade <= 0)
            {
                LogChange();
                LastName = Current.Change.Name;
                LastPressed = CarriedPress != 0 ? CarriedPress : Current.Pressed;
                CarriedPress = 0;
                const std::function<void()> then = std::move(Current.Change.Then);
                Current = MCRunning{};
                ChangesFinished++;

                if (then != nullptr)
                {
                    then();
                }
            }
            break;
        }
        case MCPhase::None:
        case MCPhase::Working:
            break;
    }
}

auto MCScreenChange::Cancel() -> void
{
    if (Current.Phase == MCPhase::None)
    {
        return;
    }

    MCFrameLog::Note(std::format("change {}: called off", Current.Change.Name));

    if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
    {
        display->SetFade(0);
    }

    const bool beforeDark = Current.Phase == MCPhase::Working || Current.Phase == MCPhase::Out;
    std::function<void()> calledOff;

    if (beforeDark)
    {
        calledOff = std::move(Current.Change.CalledOff);
    }

    Current = MCRunning{};
    CarriedPress = 0;
    Generation++;

    if (calledOff != nullptr)
    {
        calledOff();
    }
}

auto MCScreenChange::Finished() -> size_t
{
    return ChangesFinished;
}

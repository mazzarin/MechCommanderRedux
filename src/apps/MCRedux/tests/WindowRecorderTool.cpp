#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fakes/MCManualClock.h"
#include "gui/MCGuiCallback.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "gui/MCUpdateDisplay.h"
#include "main/MCGameContext.h"
#include "main/MCHdSettings.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCLogMech.h"
#include "main/MCLogMechList.h"
#include "main/MCLogistics.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "platform/MCDisplay.h"
#include "platform/MCFrameLog.h"
#include "platform/MCInput.h"
#include "platform/MCPresenter.h"
#include "platform/MCRenderer.h"
#include "sprite/MCHdArt.h"

// Port-only, a tool (SPEC-menus.md's stage 1): --record-window <folder> records the whole window as presented, frame
// by frame, through the two changes of screen stage 1 made: BEGIN MISSION to the battlefield's first seconds, and the
// results' DONE to the next screen. Each frame is read back from the GPU as the window shows it (the presenter's
// RecordWindow), with when it was presented, and checked: no frame held past a refresh and a half (frozen), no picture
// changing size between two frames that aren't black, no step between two frames bigger than a fade's (a black cut),
// nothing but the surround round the view. The game's clock follows the real one, the frames paced as the display's
// vsync would pace them. Each part's folder gets video.mp4 (a frame a sixtieth of a second, by ffmpeg), frames.csv and
// summary.txt; the summary is printed too.
//
// Options: --record-window-size <w>x<h> (the window in pixels; 1920x1080), --record-window-ui <lines> (UI RESOLUTION;
// 1080), --record-window-pack <folder> (HD art on, from that pack, as a player's app has it), --record-window-battle
// <seconds> (the battlefield recorded after its fade-in; 3), --record-window-hz <n> (the frames' pace; the display's
// refresh), --record-window-video 0 (no frames read back, for the frame log's times as in play; any renderer),
// --record-window-framelog <file> (the frame log, as the app's -framelog writes it: each change's line, the parts'
// times). Each frame recorded waits for the GPU's copy, so frames take a few milliseconds longer than in play: time
// the changes with --record-window-video 0.
//
// Port-only (SPEC-menus.md's stage 2): --record-window-landings 1 goes on through each landing out of a mission: a
// loss's REPLAY MISSION, a quick-start mission's win to SOLO MISSIONS, Escape back to the main menu, and the
// campaign's last mission won to the main menu (its end movies skipped with Escape); each part's summary ends with the
// flow's stack.

namespace
{
    using MCScreenChange::MCPhase;

    constexpr int DefaultWidth = 1920;
    constexpr int DefaultHeight = 1080;
    constexpr int DefaultUiLines = 1080;
    constexpr double DefaultBattleSeconds = 3.0;
    /// <summary>The pace of the tool's frames where the display's refresh isn't known, and the video's frame rate.</summary>
    constexpr double DefaultPaceHz = 60.0;
    constexpr int VideoRate = 60;

    /// <summary>
    /// The pace of the tool's frames, as vsync on the window's display would pace them (--record-window-hz, else the
    /// display's refresh), so the frame log's frames late by a refresh and a half are the display's.
    /// </summary>
    double PaceHz = DefaultPaceHz;
    /// <summary>A frame on the screen longer than this many refreshes was held (MCFrameLog's rule).</summary>
    constexpr double LateRefreshes = 1.5;
    /// <summary>The shortest and longest frame the game is told it took (the clock steps by the real time between).</summary>
    constexpr double ShortestFrame = 1.0 / 240.0;
    constexpr double LongestFrame = 0.25;
    /// <summary>The briefing and the results recorded live before the press, and the next screen after its fade-in.</summary>
    constexpr double SecondsBefore = 1.0;
    constexpr double SecondsAfter = 1.0;
    /// <summary>The most a change may take before the tool gives up on it.</summary>
    constexpr double MostChangeSeconds = 60.0;
    /// <summary>Frames read back and waiting for the writer, at most (each a window's pixels).</summary>
    constexpr size_t MostQueued = 8;
    /// <summary>A frame whose mean brightness (0-255) is below this is black.</summary>
    constexpr double DarkLuma = 1.0;
    /// <summary>
    /// A step between two frames by more than this share of the brightest picture around them (<see cref="CutReach"/>
    /// frames each way) is a cut: a 150 ms fade's steps at 60 Hz are about a ninth of it each...
    /// </summary>
    constexpr double CutShare = 0.35;
    constexpr size_t CutReach = 10;
    /// <summary>...when that picture is at least this bright.</summary>
    constexpr double CutFloor = 16.0;
    /// <summary>
    /// A fade-in's frame darker than the one before by more than this share of it, or a fade-out's brighter, runs the
    /// wrong way: something the fade doesn't cover came or went (a step of the fade itself is the other way).
    /// </summary>
    constexpr double WrongWayShare = 0.15;
    /// <summary>A pixel round the view this far from the surround's colour (in any component) isn't the surround.</summary>
    constexpr int SurroundTolerance = 2;
    /// <summary>The viewport's edges may move this many pixels between frames (rounding) without counting.</summary>
    constexpr int SizeTolerance = 1;
    /// <summary>The frames listed for each kind of finding, at most.</summary>
    constexpr size_t MostListed = 20;
    /// <summary>Rec. 601's luma weights.</summary>
    constexpr double LumaRed = 0.299;
    constexpr double LumaGreen = 0.587;
    constexpr double LumaBlue = 0.114;
    constexpr double MsPerSecond = 1000.0;
    /// <summary>The first mission's result that wins it (above 3).</summary>
    constexpr uint32_t Won = 5;
    /// <summary>A mission's result that loses it (below 3).</summary>
    constexpr uint32_t Lost = 0;
    /// <summary>How often the campaign's end movies are skipped (Escape), in seconds.</summary>
    constexpr double SkipEvery = 0.5;
    /// <summary>The most frames the scenario's end takes to bring the results up (twenty seconds).</summary>
    constexpr int32_t MostFramesToResults = 300;

    using Clock = std::chrono::steady_clock;

    /// <summary>What the game was doing as a frame was presented (taken at its present).</summary>
    struct MCFrameNote
    {
        double PresentMs = 0.0;
        std::string Change;
        MCPhase Phase = MCPhase::None;
        int Fade = 0;
        MCMissionState State = MCMissionState::Logistics;
        bool Loading = false;
        /// <summary>
        /// The battle's start-up static drawn (the original's, MCScenario::StartingUp): it clears by itself over its
        /// first turns, under a fade or not.
        /// </summary>
        bool StartingUp = false;
        /// <summary>The colour round the view as shown (faded).</summary>
        SDL_Color Surround{};
        /// <summary>Where the view lands in the window, in pixels.</summary>
        SDL_Rect Shown{};
    };

    /// <summary>A frame recorded: its note, and what the writer found in its pixels.</summary>
    struct MCFrameStats
    {
        uint64_t Number = 0;
        MCFrameNote Note;
        /// <summary>Mean brightness over the window (0-255).</summary>
        double Luma = 0.0;
        /// <summary>Pixels round the view that aren't the surround.</summary>
        uint64_t Stray = 0;
        /// <summary>Whether it is the frame before's picture, pixel for pixel.</summary>
        bool Same = false;
    };

    /// <summary>How many phases a change has (MCScreenChange::MCPhase's, None to In).</summary>
    constexpr size_t PhaseCount = static_cast<size_t>(MCPhase::In) + 1;

    std::string_view PhaseName(MCPhase phase)
    {
        switch (phase)
        {
            case MCPhase::None:
                return "none";
            case MCPhase::Working:
                return "working";
            case MCPhase::Out:
                return "out";
            case MCPhase::Dark:
                return "dark";
            case MCPhase::In:
                return "in";
        }

        return "?";
    }

    std::string_view StateName(MCMissionState state)
    {
        switch (state)
        {
            case MCMissionState::Logistics:
                return "logistics";
            case MCMissionState::StartScenario:
                return "loading";
            case MCMissionState::Scenario:
                return "battle";
            case MCMissionState::Results:
                return "results";
            default:
                return "other";
        }
    }

    double Luma(SDL_Color color)
    {
        return LumaRed * color.r + LumaGreen * color.g + LumaBlue * color.b;
    }

    bool NearColor(SDL_Color a, SDL_Color b)
    {
        return std::abs(a.r - b.r) <= SurroundTolerance && std::abs(a.g - b.g) <= SurroundTolerance &&
               std::abs(a.b - b.b) <= SurroundTolerance;
    }

    /// <summary>Whether two viewports differ by more than rounding.</summary>
    bool DifferentSize(const SDL_Rect& a, const SDL_Rect& b)
    {
        return std::abs(a.x - b.x) > SizeTolerance || std::abs(a.y - b.y) > SizeTolerance ||
               std::abs(a.w - b.w) > SizeTolerance || std::abs(a.h - b.h) > SizeTolerance;
    }

    /// <summary>ffmpeg reading raw RGBA frames on its input, writing an H.264 video (as the showcase's).</summary>
    class MCVideoPipe
    {
    public:
        MCVideoPipe(const std::filesystem::path& path, int width, int height)
        {
            const std::string command = std::format(
                "ffmpeg -v error -y -f rawvideo -pix_fmt rgba -s {}x{} -r {} -i - -c:v libx264 -preset veryfast "
                "-crf 12 -pix_fmt yuv420p \"{}\"",
                width, height, VideoRate, path.string());
#ifdef _WIN32
            _Pipe = _popen(command.c_str(), "wb");
#else
            _Pipe = popen(command.c_str(), "w");
#endif
        }

        ~MCVideoPipe()
        {
            if (_Pipe != nullptr)
            {
#ifdef _WIN32
                _pclose(_Pipe);
#else
                pclose(_Pipe);
#endif
            }
        }

        MCVideoPipe(const MCVideoPipe&) = delete;
        MCVideoPipe& operator=(const MCVideoPipe&) = delete;

        bool Open() const { return _Pipe != nullptr; }

        bool Write(std::span<const SDL_Color> pixels)
        {
            return std::fwrite(pixels.data(), sizeof(SDL_Color), pixels.size(), _Pipe) == pixels.size();
        }

    private:
        FILE* _Pipe = nullptr;
    };

    /// <summary>
    /// One part's recording, from its making to its end: each frame presented meanwhile noted (MCTestGame::OnPresent),
    /// read back (the display's RecordWindow), checked and written to the video on a thread of its own; its end
    /// writes frames.csv and summary.txt into <paramref name="folder"/>.
    /// </summary>
    class MCWindowRecording
    {
    public:
        MCWindowRecording(std::filesystem::path folder, int width, int height)
            : _Folder(std::move(folder)), _Video(_Folder / "video.mp4", width, height), _Width(width), _Height(height)
        {
            _Started = Clock::now();
            MCTestGame::OnPresent = [this] { Note(); };
            _Recording = MCInput::Display()->RecordWindow([this](MCWindowFrame&& frame) { Take(std::move(frame)); });
            _Writer = std::thread([this] { Write(); });
        }

        ~MCWindowRecording()
        {
            MCTestGame::OnPresent = {};
            MCInput::Display()->RecordWindow({});
            {
                const std::scoped_lock lock(_Lock);
                _Ending = true;
            }

            _Changed.notify_all();
            _Writer.join();
        }

        MCWindowRecording(const MCWindowRecording&) = delete;
        MCWindowRecording& operator=(const MCWindowRecording&) = delete;

        /// <summary>Whether the presenter reads its frames back and ffmpeg takes them.</summary>
        bool Recording() const { return _Recording && _Video.Open(); }

        /// <summary>Frames the video took fewer of than were recorded (ffmpeg stopped).</summary>
        size_t Unwritten() const { return _Unwritten; }

    private:
        void Note()
        {
            const MCDisplay& display = *MCInput::Display();
            const MCViewport viewport = display.ShownViewport();
            MCFrameNote note{.PresentMs = std::chrono::duration<double, std::milli>(Clock::now() - _Started).count(),
                             .Change = std::string(MCScreenChange::Name()),
                             .Phase = MCScreenChange::Phase(),
                             .Fade = display.Fade(),
                             .State = Mission()->State,
                             .Loading = LoadingScreenUp,
                             .StartingUp = Scenario() != nullptr && Scenario()->StartingUp,
                             .Surround = MCFadedColor(display.Surround(), display.Fade(), display.FadeScales()),
                             .Shown = SDL_Rect{
                                 static_cast<int>(std::lround(viewport.X)), static_cast<int>(std::lround(viewport.Y)),
                                 static_cast<int>(std::lround(viewport.W)), static_cast<int>(std::lround(viewport.H))}};
            _Notes[display.Presents()] = std::move(note);
        }

        /// <summary>On the game's thread, as each frame is shown: queued for the writer with its note.</summary>
        void Take(MCWindowFrame&& frame)
        {
            const auto found = _Notes.find(frame.Number);

            // Presented before the recording began (a frame run ahead, shown after).
            if (found == _Notes.end())
            {
                return;
            }

            MCFrameNote note = std::move(found->second);
            _Notes.erase(found);
            std::unique_lock lock(_Lock);
            _Changed.wait(lock, [this] { return _Queued.size() < MostQueued; });
            _Queued.emplace_back(std::move(frame), std::move(note));
            _Changed.notify_all();
        }

        /// <summary>The writer's thread: each frame checked and sent to the video, then the files written.</summary>
        void Write()
        {
            std::vector<SDL_Color> before;

            for (;;)
            {
                std::pair<MCWindowFrame, MCFrameNote> next;
                {
                    std::unique_lock lock(_Lock);
                    _Changed.wait(lock, [this] { return !_Queued.empty() || _Ending; });

                    if (_Queued.empty())
                    {
                        break;
                    }

                    next = std::move(_Queued.front());
                    _Queued.pop_front();
                }

                _Changed.notify_all();
                MCFrameStats stats = Check(next.first, std::move(next.second), before);

                if (next.first.Width == _Width && next.first.Height == _Height && _Video.Open())
                {
                    if (!_Video.Write(next.first.Pixels))
                    {
                        _Unwritten++;
                    }
                }
                else
                {
                    _Unwritten++;
                }

                before = std::move(next.first.Pixels);
                const std::scoped_lock lock(_Lock);
                _Stats.push_back(std::move(stats));
            }

            WriteFiles();
        }

        static MCFrameStats Check(const MCWindowFrame& frame, MCFrameNote note, const std::vector<SDL_Color>& before)
        {
            MCFrameStats stats{.Number = frame.Number, .Note = std::move(note)};
            const SDL_Rect& shown = stats.Note.Shown;
            double luma = 0.0;

            for (int y = 0; y < frame.Height; y++)
            {
                const bool rowInView = y >= shown.y && y < shown.y + shown.h;

                for (int x = 0; x < frame.Width; x++)
                {
                    const SDL_Color color = frame.Pixels[static_cast<size_t>(y) * frame.Width + x];
                    luma += Luma(color);

                    if ((!rowInView || x < shown.x || x >= shown.x + shown.w) && !NearColor(color, stats.Note.Surround))
                    {
                        stats.Stray++;
                    }
                }
            }

            stats.Luma = frame.Pixels.empty() ? 0.0 : luma / static_cast<double>(frame.Pixels.size());
            stats.Same = before.size() == frame.Pixels.size() &&
                         std::memcmp(before.data(), frame.Pixels.data(), before.size() * sizeof(SDL_Color)) == 0;
            return stats;
        }

        void WriteFiles()
        {
            std::ofstream csv(_Folder / "frames.csv");
            csv << "frame,present_ms,gap_ms,change,phase,fade,state,loading,shown_x,shown_y,shown_w,shown_h,luma,stray,"
                   "same\n";
            double last = 0.0;

            for (const MCFrameStats& frame : _Stats)
            {
                const MCFrameNote& note = frame.Note;
                csv << std::format("{},{:.2f},{:.2f},{},{},{},{},{},{},{},{},{},{:.2f},{},{}\n", frame.Number,
                                   note.PresentMs, note.PresentMs - last, note.Change, PhaseName(note.Phase), note.Fade,
                                   StateName(note.State), note.Loading ? 1 : 0, note.Shown.x, note.Shown.y,
                                   note.Shown.w, note.Shown.h, frame.Luma, frame.Stray, frame.Same ? 1 : 0);
                last = note.PresentMs;
            }

            std::ofstream summary(_Folder / "summary.txt");
            summary << Summarise(_Stats);
        }

    public:
        /// <summary>The findings over <paramref name="frames"/>: each kind with the frames that show it.</summary>
        static std::string Summarise(std::span<const MCFrameStats> frames)
        {
            const double refreshMs = MsPerSecond / PaceHz;
            std::string out;
            const auto line = [&out](std::string text)
            {
                out += text;
                out += '\n';
            };

            const auto where = [](const MCFrameStats& frame)
            {
                const MCFrameNote& note = frame.Note;
                return std::format("frame {} at {:.1f} ms ({}{}{}, fade {}, {}{})", frame.Number, note.PresentMs,
                                   note.Change.empty() ? "no change" : note.Change, note.Change.empty() ? "" : " ",
                                   note.Change.empty() ? "" : PhaseName(note.Phase), note.Fade, StateName(note.State),
                                   note.Loading ? ", loading screen" : "");
            };

            if (frames.empty())
            {
                line("no frames recorded");
                return out;
            }

            line(std::format("{} frames over {:.0f} ms, paced at {:.0f} Hz (the video at {})", frames.size(),
                             frames.back().Note.PresentMs - frames.front().Note.PresentMs, PaceHz, VideoRate));
            std::vector<std::string> held;
            std::vector<std::string> stuck;
            std::vector<std::string> resized;
            std::vector<std::string> cuts;
            std::vector<std::string> wrongWay;
            std::vector<std::string> stray;
            double longest = 0.0;

            for (size_t i = 1; i < frames.size(); i++)
            {
                const MCFrameStats& before = frames[i - 1];
                const MCFrameStats& frame = frames[i];
                const double gap = frame.Note.PresentMs - before.Note.PresentMs;
                longest = std::max(longest, gap);

                if (gap > refreshMs * LateRefreshes)
                {
                    held.push_back(std::format("{:.1f} ms before {}", gap, where(frame)));
                }

                const bool fading = frame.Note.Phase == MCPhase::Out || frame.Note.Phase == MCPhase::In;

                if (frame.Same && fading && frame.Note.Fade != before.Note.Fade)
                {
                    stuck.push_back(where(frame));
                }

                if (before.Luma >= DarkLuma && frame.Luma >= DarkLuma &&
                    DifferentSize(before.Note.Shown, frame.Note.Shown))
                {
                    resized.push_back(std::format("{} ({}x{} to {}x{})", where(frame), before.Note.Shown.w,
                                                  before.Note.Shown.h, frame.Note.Shown.w, frame.Note.Shown.h));
                }

                double brightest = 0.0;

                for (size_t k = i > CutReach ? i - CutReach : 0; k < std::min(frames.size(), i + CutReach + 1); k++)
                {
                    brightest = std::max(brightest, frames[k].Luma);
                }

                if (brightest >= CutFloor && std::abs(frame.Luma - before.Luma) > brightest * CutShare)
                {
                    cuts.push_back(
                        std::format("{} (brightness {:.1f} to {:.1f})", where(frame), before.Luma, frame.Luma));
                }

                const bool sameFade = before.Note.Phase == frame.Note.Phase && fading && !before.Note.StartingUp &&
                                      !frame.Note.StartingUp;
                const bool lifting = sameFade && frame.Note.Phase == MCPhase::In && frame.Note.Fade < before.Note.Fade;
                const bool falling = sameFade && frame.Note.Phase == MCPhase::Out && frame.Note.Fade > before.Note.Fade;

                if (before.Luma >= DarkLuma && ((lifting && frame.Luma < before.Luma * (1.0 - WrongWayShare)) ||
                                                (falling && frame.Luma > before.Luma * (1.0 + WrongWayShare))))
                {
                    wrongWay.push_back(
                        std::format("{} (brightness {:.1f} to {:.1f})", where(frame), before.Luma, frame.Luma));
                }
            }

            for (const MCFrameStats& frame : frames)
            {
                if (frame.Stray > 0)
                {
                    stray.push_back(std::format("{}: {} pixels", where(frame), frame.Stray));
                }
            }

            line(std::format("the longest time between two frames: {:.1f} ms (a refresh {:.2f} ms)", longest,
                             refreshMs));
            const auto list = [&line](std::string_view title, const std::vector<std::string>& found)
            {
                line(std::format("{}: {}", title, found.size()));

                for (size_t i = 0; i < std::min(found.size(), MostListed); i++)
                {
                    line(std::format("  {}", found[i]));
                }
            };

            list("frames held past a refresh and a half (frozen)", held);
            list("frames whose fade moved and picture didn't", stuck);
            list("pictures changing size between frames that aren't black", resized);
            list("steps bigger than a fade's (black cuts)", cuts);
            list("fades running the wrong way (something they don't cover came or went)", wrongWay);
            list("frames with something round the view but the surround", stray);

            // Each change: its frames by phase, and from its fade-out's first frame (the one after the press, or after
            // its job) to the next screen's first and its fade-in's end.
            for (size_t first = 0; first < frames.size();)
            {
                const std::string& name = frames[first].Note.Change;

                if (name.empty())
                {
                    first++;
                    continue;
                }

                size_t end = first;
                std::array<size_t, PhaseCount> phases{};
                std::optional<double> firstOut;
                std::optional<double> firstIn;

                while (end < frames.size() && frames[end].Note.Change == name)
                {
                    const MCFrameNote& note = frames[end].Note;
                    phases[static_cast<size_t>(note.Phase)]++;

                    if (note.Phase == MCPhase::Out && !firstOut.has_value())
                    {
                        firstOut = note.PresentMs;
                    }

                    if (note.Phase == MCPhase::In && !firstIn.has_value())
                    {
                        firstIn = note.PresentMs;
                    }

                    end++;
                }

                const double start = firstOut.value_or(frames[first].Note.PresentMs);
                const auto count = [&phases](MCPhase phase) { return phases[static_cast<size_t>(phase)]; };
                line(std::format("change {}: {} frames working, {} out, {} dark, {} in; its fade-out's first frame to "
                                 "the next screen's first {:.1f} ms, to the fade-in's end {:.1f} ms",
                                 name, count(MCPhase::Working), count(MCPhase::Out), count(MCPhase::Dark),
                                 count(MCPhase::In), firstIn.value_or(start) - start,
                                 frames[end - 1].Note.PresentMs - start));
                first = end;
            }

            return out;
        }

    private:
        std::filesystem::path _Folder;
        MCVideoPipe _Video;
        int _Width = 0;
        int _Height = 0;
        bool _Recording = false;
        Clock::time_point _Started;
        /// <summary>The notes of frames presented and not yet read back, by present (the game's thread only).</summary>
        std::map<uint64_t, MCFrameNote> _Notes;
        std::mutex _Lock;
        std::condition_variable _Changed;
        std::deque<std::pair<MCWindowFrame, MCFrameNote>> _Queued;
        bool _Ending = false;
        std::vector<MCFrameStats> _Stats;
        std::atomic<size_t> _Unwritten = 0;
        std::thread _Writer;
    };

    /// <summary>
    /// While it lives, the game's clock (the boot's manual one) keeps up with the real one: at each frame's start (its
    /// length, <see cref="Behind"/>) and again after the frame's callbacks, before it is drawn, so a frame drawn after
    /// long work (a mission's load) shows the time it was drawn at, as in play, not the time the frame began.
    /// </summary>
    class MCRealTime
    {
    public:
        MCRealTime() : _Clock(dynamic_cast<MCManualClock&>(MCGameContext::Current().Clock()))
        {
            // The real time alone moves it (a present moving it too would put it ahead through a load's frames).
            _Clock.AdvanceOnPresent(false);
            _RealStart = Clock::now();
            _GameStart = _Clock.Nanoseconds();
            _CatchUp->SetExec([this] { CatchUp(); });
            GuiSystem().AddCallback(_CatchUp);
        }

        ~MCRealTime()
        {
            GuiSystem().RemoveCallback(*_CatchUp);
            _Clock.AdvanceOnPresent(true);
        }

        MCRealTime(const MCRealTime&) = delete;
        MCRealTime& operator=(const MCRealTime&) = delete;

        /// <summary>How far (seconds) the game's clock is behind the real one; 0 when ahead.</summary>
        double Behind() const
        {
            const uint64_t target = Target();
            const uint64_t now = _Clock.Nanoseconds();
            return target > now ? static_cast<double>(target - now) / NanosecondsPerSecond : 0.0;
        }

    private:
        static constexpr double NanosecondsPerSecond = 1e9;

        uint64_t Target() const
        {
            const auto real = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - _RealStart).count();
            return _GameStart + static_cast<uint64_t>(std::max<int64_t>(real, 0));
        }

        void CatchUp()
        {
            const uint64_t target = Target();

            if (const uint64_t now = _Clock.Nanoseconds(); target > now)
            {
                _Clock.Advance(target - now);
            }
        }

        MCManualClock& _Clock;
        Clock::time_point _RealStart;
        uint64_t _GameStart = 0;
        /// <summary>Added after the game's own callbacks (the mission's), so it runs last, just before the draw.</summary>
        std::shared_ptr<MCGuiCallback> _CatchUp = std::make_shared<MCGuiCallback>();
    };

    /// <summary>
    /// Runs the game's frames at a 60 Hz display's pace, each as long as the real time since the game's clock last
    /// caught up (<paramref name="time"/>), until <paramref name="done"/> (or <paramref name="mostSeconds"/>); whether
    /// <paramref name="done"/> came.
    /// </summary>
    bool RunPaced(const MCRealTime& time, const std::function<bool()>& done, double mostSeconds)
    {
        const auto refresh = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / PaceHz));
        const Clock::time_point end =
            Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(mostSeconds));

        while (!done())
        {
            const Clock::time_point now = Clock::now();

            if (now >= end)
            {
                return false;
            }

            // As the game's frame loop starts each (MCGuiSystem::Run).
            if (MCFrameLog::Enabled())
            {
                MCFrameLog::NextFrame(PaceHz, true);
            }

            MCTestGame::RunFrame(static_cast<float>(std::clamp(time.Behind(), ShortestFrame, LongestFrame)));
            std::this_thread::sleep_until(now + refresh);
        }

        return true;
    }

    /// <summary>Runs paced frames for <paramref name="seconds"/>.</summary>
    void RunPacedFor(const MCRealTime& time, double seconds)
    {
        RunPaced(time, [] { return false; }, seconds);
    }

    /// <summary>Whether no change of screen runs.</summary>
    bool Settled()
    {
        return !MCScreenChange::Running();
    }

    /// <summary>An option's number, else <paramref name="fallback"/>.</summary>
    double NumberOption(std::string_view name, double fallback)
    {
        if (const std::optional<std::string_view> given = MCTest::Option(name); given.has_value())
        {
            return std::stod(std::string(*given));
        }

        return fallback;
    }

    /// <summary>The briefing's first mech deployed, as MCTestGame::StartCampaignMission deploys it.</summary>
    void DeployFirstMech()
    {
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();
        REQUIRE(logistics->CurrentScreen() == logistics->BriefingScreen);
        std::shared_ptr<MCLogMech> mech;
        REQUIRE_EQ(logistics->ForceMechList->GetMechInfo(0, mech), 0);
        logistics->DeploySlots[0][0].Unit = 0;
        mech->Deployed = true;
    }

    /// <summary>The mission's results brought up at once (not recorded), ended as <paramref name="result"/>.</summary>
    void EndToResults(uint32_t result)
    {
        ScenarioResult = result;
        Mission()->EndScenarioRequested = -1;

        for (int32_t frame = 0; frame < MostFramesToResults && Mission()->State != MCMissionState::Results; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        if (Mission()->State != MCMissionState::Results)
        {
            std::cout << std::format(
                "  the mission's end never brought its results up (state {}, next {}, solo {}, turn {}, starting {}, "
                "paused {})\n",
                std::to_underlying(Mission()->State), std::to_underlying(Mission()->NextState), Solo, Turn,
                Scenario() != nullptr && Scenario()->StartingUp, GamePaused);
        }

        REQUIRE(Mission()->State == MCMissionState::Results);
    }

    /// <summary>Records a part into <paramref name="folder"/>: <paramref name="run"/> played while it records.</summary>
    std::string RecordPart(const std::filesystem::path& folder, int width, int height, const std::function<void()>& run)
    {
        // Timed only (--record-window-video 0): the frames as in play, without the readback's cost.
        if (const std::optional<std::string_view> video = MCTest::Option("record-window-video"); video == "0")
        {
            run();
            return "not recorded (--record-window-video 0): the frame log has the times\n";
        }

        std::filesystem::create_directories(folder);
        size_t unwritten = 0;
        {
            // Its end writes the summary, once every frame is checked.
            MCWindowRecording recording(folder, width, height);
            REQUIRE(recording.Recording());
            run();
            unwritten = recording.Unwritten();
        }

        std::ifstream summary(folder / "summary.txt");
        std::string text((std::istreambuf_iterator<char>(summary)), {});

        if (unwritten > 0)
        {
            text +=
                std::format("{} frames not in the video (ffmpeg stopped, or the window's size changed)\n", unwritten);
        }

        return text;
    }
}

/// <summary>Tool: <c>--record-window &lt;folder&gt;</c> (see the top of this file).</summary>
TEST_CASE_ISOLATED("tool: --record-window records the window through BEGIN MISSION and the results' DONE")
{
    const std::optional<std::string_view> folderOption = MCTest::Option("record-window");

    if (!folderOption.has_value() || !MCTestGame::Available())
    {
        return;
    }

    const std::filesystem::path folder = std::filesystem::absolute(std::filesystem::path(*folderOption));
    int width = DefaultWidth;
    int height = DefaultHeight;

    if (const std::optional<std::string_view> size = MCTest::Option("record-window-size"); size.has_value())
    {
        const size_t by = size->find('x');
        REQUIRE(by != std::string_view::npos);
        width = std::stoi(std::string(size->substr(0, by)));
        height = std::stoi(std::string(size->substr(by + 1)));
    }

    const int uiLines = static_cast<int>(NumberOption("record-window-ui", DefaultUiLines));
    const double battleSeconds = NumberOption("record-window-battle", DefaultBattleSeconds);
    MCTestGame::BootOptions.WindowWidth = width;
    MCTestGame::BootOptions.WindowHeight = height;
    MCTestGame::BootOptions.FixedZoomHeight = 0.0f;
    const std::optional<std::string_view> packOption = MCTest::Option("record-window-pack");
    const bool hd = packOption.has_value();

    if (hd)
    {
        UseHdByDefault();
        MCHdArt::SetPackFolder(std::filesystem::absolute(std::filesystem::path(*packOption)));
        MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
    }

    MCTestGame::BootOptions.Started = [uiLines, hd]
    {
        GUiResolution = uiLines;
        GInterfaceSize = OriginalInterfaceSize;

        if (hd)
        {
            GHdArt = 1;
        }
    };

    const MCTestGame::SaveGameCleanup saves;
    REQUIRE(MCTestGame::StartLogistics());
    // The mouse in the window's middle, where a player's rests: left in the corner, the battle's edge scrolling runs
    // the camera off the ground the units see.
    MCScreenInput::RealMove(GuiSystem().Width() / 2, GuiSystem().Height() / 2);
    const MCRealTime time;
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(&MCInput::Display()->Window()));
    PaceHz = NumberOption("record-window-hz",
                          mode != nullptr && mode->refresh_rate > 0.0f ? mode->refresh_rate : DefaultPaceHz);

    if (const std::optional<std::string_view> log = MCTest::Option("record-window-framelog"); log.has_value())
    {
        REQUIRE(MCFrameLog::Open(std::filesystem::absolute(std::filesystem::path(*log))));
    }

    // The log ends (written whole) when the tool does.
    const struct MCCloseLog
    {
        MCCloseLog() = default;
        MCCloseLog(const MCCloseLog&) = delete;
        MCCloseLog& operator=(const MCCloseLog&) = delete;
        ~MCCloseLog() { MCFrameLog::Close(); }
    } closeLog;
    RunPaced(time, Settled, MostChangeSeconds);
    NewCampaign();
    RunPaced(time, Settled, MostChangeSeconds);
    RunPacedFor(time, SecondsBefore);
    DeployFirstMech();

    // BEGIN MISSION (MCBriefingScreen::Launch) to the battlefield's first seconds.
    const std::string into = RecordPart(
        folder / "into-mission", width, height,
        [&time, battleSeconds]
        {
            RunPacedFor(time, SecondsBefore);
            Mission()->RequestScenario(Mission()->Scenarios[0]);
            CHECK(RunPaced(
                time, [] { return Mission()->State == MCMissionState::Scenario && Settled(); }, MostChangeSeconds));
            RunPacedFor(time, battleSeconds);
        });
    std::cout << "into the mission:\n" << into;

    // The mission won, its results up (not recorded: their own fade-in is the original's).
    EndToResults(Won);
    RunPacedFor(time, SecondsBefore);

    // DONE to the next screen.
    const std::string out = RecordPart(folder / "out-of-mission", width, height,
                                       [&time]
                                       {
                                           RunPacedFor(time, SecondsBefore);
                                           Mission()->LeaveResults();
                                           CHECK(RunPaced(time, Settled, MostChangeSeconds));
                                           RunPacedFor(time, SecondsAfter);
                                       });
    std::cout << "out of the mission:\n" << out;

    if (!MCTest::Option("record-window-landings").has_value())
    {
        return;
    }

    // Each landing out of a mission (not recorded: BEGIN MISSION, recorded above, and the results' fade-in).
    const auto playToResults = [&time](uint32_t result, bool solo, bool last)
    {
        DeployFirstMech();
        Mission()->RequestScenario(Mission()->Scenarios[static_cast<size_t>(Mission()->CurrentScenario)]);
        // Its start-up over first (an end asked for while it lasts never comes: its turns stop).
        REQUIRE(RunPaced(
            time, [] { return Mission()->State == MCMissionState::Scenario && Settled() && !Scenario()->StartingUp; },
            MostChangeSeconds));
        // As a quick-start mission's end (BriefingTests' PlayToResultsAndBack), or the campaign's last.
        Solo = solo;

        if (last)
        {
            Mission()->CurrentScenario = static_cast<int32_t>(Mission()->LastScenario);
        }

        EndToResults(result);
        RunPacedFor(time, SecondsBefore);
    };

    const auto recordLanding =
        [&](std::string_view name, const std::function<void()>& press, const std::function<bool()>& landed)
    {
        const std::string report =
            RecordPart(folder / name, width, height,
                       [&]
                       {
                           RunPacedFor(time, SecondsBefore);
                           press();
                           CHECK(RunPaced(time, [&] { return Settled() && landed(); }, MostChangeSeconds));
                           RunPacedFor(time, SecondsAfter);
                       });
        std::cout << std::format("{}:\n{}  the flow's stack: {}\n", name, report, MCScreenFlow::StackText());
    };

    const auto shows = [](auto MCLogistics::* screen)
    {
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();
        return logistics != nullptr && logistics->CurrentScreen() == logistics.get()->*screen;
    };

    const auto leaveResults = [] { Mission()->LeaveResults(); };

    // A loss: REPLAY MISSION's briefing, the main menu under it.
    playToResults(Lost, false, false);
    recordLanding("replay-mission", leaveResults, [&] { return shows(&MCLogistics::BriefingScreen); });

    // A quick-start mission won: SOLO MISSIONS, the main menu under it; Escape goes back to the main menu.
    playToResults(Won, true, false);
    recordLanding("solo-missions", leaveResults, [&] { return shows(&MCLogistics::LoadScreen) && LoadingSolo; });
    Solo = false;
    recordLanding(
        "back-to-main", [] { MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE); },
        [&] { return shows(&MCLogistics::MainScreen); });

    // The campaign's last mission won (the first, marked last): its end movies (skipped), then the main menu.
    NewCampaign();
    REQUIRE(RunPaced(time, Settled, MostChangeSeconds));
    playToResults(Won, false, true);
    Clock::time_point skipped = Clock::now();
    recordLanding("campaign-end", leaveResults,
                  [&]
                  {
                      const auto every =
                          std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(SkipEvery));

                      if (GuiSystem().SmackerWindow != nullptr && Clock::now() - skipped >= every)
                      {
                          skipped = Clock::now();
                          MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
                      }

                      return Mission()->State == MCMissionState::Logistics && GuiSystem().SmackerWindow == nullptr &&
                             shows(&MCLogistics::MainScreen);
                  });
}

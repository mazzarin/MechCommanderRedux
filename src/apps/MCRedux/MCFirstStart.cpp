#include "stdafx.h"
#include "MCFirstStart.h"
#include "platform/MCFileSystem.h"

namespace
{
    /// <summary>
    /// The window's size in points (36 letters a line); it's drawn at twice that scale, the built-in text's letters 16
    /// points tall.
    /// </summary>
    constexpr int WindowWidth = 640;
    constexpr int WindowHeight = 176;
    constexpr float DrawingScale = 2.0f;

    /// <summary>Where the lines and the bar go, in the drawing's units (the window's points halved).</summary>
    constexpr float Margin = 14.0f;
    constexpr float HeadingTop = 14.0f;
    constexpr float AmountTop = 30.0f;
    constexpr float BarTop = 48.0f;
    constexpr float BarHeight = 10.0f;
    /// <summary>The speed and time left, under the bar.</summary>
    constexpr float RateTop = 66.0f;

    /// <summary>What the line under the heading says before the bytes are known, while what's here is checked, and once closed.</summary>
    constexpr std::string_view LookingText = "LOOKING AT WHAT'S NEEDED.";
    constexpr std::string_view CheckingText = "CHECKING WHAT'S ALREADY DOWNLOADED.";
    constexpr std::string_view StoppingText = "STOPPING.";

    /// <summary>What a failed download's window says, by the failure's kind (MCFirstStart::ProblemText).</summary>
    constexpr std::string_view UnreachableText =
        "CAN'T REACH THE DOWNLOAD SERVER. CHECK YOUR INTERNET CONNECTION, THEN TRY AGAIN.";
    constexpr std::string_view DiskFullText =
        "NOT ENOUGH DISK SPACE: THE DOWNLOAD NEEDS {} MORE. FREE SOME UP, THEN TRY "
        "AGAIN.";
    constexpr std::string_view NewerProgramText =
        "THIS VERSION OF MECHCOMMANDER REDUX HD IS TOO OLD FOR THE GAME'S FILES. DOWNLOAD THE NEWEST INSTALLER.";
    constexpr std::string_view OtherText = "SOMETHING WENT WRONG. TRY AGAIN, OR QUIT AND START THE GAME LATER.";

    /// <summary>The question's lines and buttons (<see cref="MCFirstStart::Ask"/>), in the drawing's units.</summary>
    constexpr float TextTop = AmountTop;
    constexpr float LineHeight = 12.0f;
    constexpr float ButtonsGap = 10.0f;
    constexpr float ButtonPadding = 6.0f;
    constexpr float ButtonHeight = 16.0f;
    constexpr float ButtonSpacing = 8.0f;
    constexpr float LetterWidth = static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
    /// <summary>The most lines a question shows (<see cref="MCFirstStart::Wrapped"/>).</summary>
    constexpr size_t MostLines = 12;
    constexpr std::string_view CutShort = "...";

    /// <summary>How often the window is drawn while the work goes on.</summary>
    constexpr int32_t FrameMilliseconds = 50;

    /// <summary>
    /// How long a closed window draws itself STOPPING while the work stops (its transfer cancelled, it ends at once);
    /// past this it just waits for it (the next start resumes the download).
    /// </summary>
    constexpr std::chrono::seconds StopWait{1};

    struct MCColor
    {
        uint8_t Red = 0;
        uint8_t Green = 0;
        uint8_t Blue = 0;
    };

    /// <summary>The menus' look: amber on near black.</summary>
    constexpr MCColor Background{16, 16, 16};
    constexpr MCColor Amber{232, 168, 48};
    constexpr MCColor BarEdge{120, 88, 32};

    /// <summary>The window and its renderer, closed as it goes.</summary>
    class MCOpenWindow
    {
    public:
        /// <summary>A window of the width every one has, <paramref name="height"/> points tall; or why it can't open.</summary>
        static std::expected<std::unique_ptr<MCOpenWindow>, std::string> Open(int height, bool hidden)
        {
            auto open = std::unique_ptr<MCOpenWindow>(new MCOpenWindow());
            const SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | (hidden ? SDL_WINDOW_HIDDEN : 0);

            if (!SDL_CreateWindowAndRenderer(std::string(MCFirstStart::WindowTitle).c_str(), WindowWidth, height, flags,
                                             &open->Window, &open->Renderer))
            {
                return std::unexpected(std::format("its window can't open ({})", SDL_GetError()));
            }

            const float scale = DrawingScale * SDL_GetWindowPixelDensity(open->Window);
            SDL_SetRenderScale(open->Renderer, scale, scale);
            return open;
        }

        MCOpenWindow(const MCOpenWindow&) = delete;
        MCOpenWindow& operator=(const MCOpenWindow&) = delete;

        /// <summary>Makes the window <paramref name="height"/> points tall (a question's, or the progress's).</summary>
        void SetHeight(int height)
        {
            if (!SDL_SetWindowSize(Window, WindowWidth, height))
            {
                SDL_Log("First start: the window can't be made %d points tall: %s", height, SDL_GetError());
            }
        }

        ~MCOpenWindow()
        {
            if (Renderer != nullptr)
            {
                SDL_DestroyRenderer(Renderer);
            }

            if (Window != nullptr)
            {
                SDL_DestroyWindow(Window);
            }
        }

        SDL_Window* Window = nullptr;
        SDL_Renderer* Renderer = nullptr;

    private:
        MCOpenWindow() = default;
    };

    void SetColor(SDL_Renderer* renderer, MCColor color)
    {
        SDL_SetRenderDrawColor(renderer, color.Red, color.Green, color.Blue, SDL_ALPHA_OPAQUE);
    }

    /// <summary>Shows the frame drawn, saved to <paramref name="shot"/> first when given.</summary>
    void Present(SDL_Renderer* renderer, const std::filesystem::path& shot)
    {
        if (!shot.empty())
        {
            // Read before the frame is shown, as SDL asks.
            if (SDL_Surface* frame = SDL_RenderReadPixels(renderer, nullptr); frame != nullptr)
            {
                if (!SDL_SaveBMP(frame, MCFileSystem::ToUtf8(shot).c_str()))
                {
                    SDL_Log("First start: the window's frame isn't saved: %s", SDL_GetError());
                }

                SDL_DestroySurface(frame);
            }
        }

        SDL_RenderPresent(renderer);
    }

    /// <summary>The letters a line of the drawing holds between its margins.</summary>
    size_t LineLetters()
    {
        return static_cast<size_t>((static_cast<float>(WindowWidth) / DrawingScale - 2 * Margin) / LetterWidth);
    }

    /// <summary>A line cut short to <paramref name="width"/> letters, ending "...".</summary>
    void CutLine(std::string& line, size_t width)
    {
        line.resize(std::min(line.size(), width - CutShort.size()));
        line += CutShort;
    }

    /// <summary>A button of the question: its place, in the drawing's units, and its word.</summary>
    struct MCButton
    {
        SDL_FRect Area{};
        std::string_view Label;
    };

    /// <summary>The buttons in a row from the left margin, at <paramref name="top"/>.</summary>
    std::vector<MCButton> LaidOut(std::span<const std::string_view> labels, float top)
    {
        std::vector<MCButton> buttons;
        float left = Margin;

        for (const std::string_view label : labels)
        {
            const float width = static_cast<float>(label.size()) * LetterWidth + 2 * ButtonPadding;
            buttons.push_back({{left, top, width, ButtonHeight}, label});
            left += width + ButtonSpacing;
        }

        return buttons;
    }

    /// <summary>A point of the window, in its points as the mouse gives them, in the drawing's units.</summary>
    SDL_FPoint DrawingPoint(float x, float y)
    {
        // The drawing's scale is DrawingScale times the window's pixel density, so it's DrawingScale of its points.
        return {x / DrawingScale, y / DrawingScale};
    }

    /// <summary>The button at the window's point (in its points), if any.</summary>
    std::optional<size_t> ButtonAt(const std::vector<MCButton>& buttons, float x, float y)
    {
        const SDL_FPoint point = DrawingPoint(x, y);

        for (size_t i = 0; i < buttons.size(); ++i)
        {
            if (SDL_PointInRectFloat(&point, &buttons[i].Area))
            {
                return i;
            }
        }

        return std::nullopt;
    }

    /// <summary>Draws the question: the heading, its lines, and the buttons, <paramref name="lit"/> filled.</summary>
    void DrawQuestion(SDL_Renderer* renderer, std::string_view heading, const std::vector<std::string>& lines,
                      const std::vector<MCButton>& buttons, size_t lit, const std::filesystem::path& shot)
    {
        SetColor(renderer, Background);
        SDL_RenderClear(renderer);
        SetColor(renderer, Amber);
        SDL_RenderDebugText(renderer, Margin, HeadingTop, std::string(heading).c_str());

        for (size_t i = 0; i < lines.size(); ++i)
        {
            SDL_RenderDebugText(renderer, Margin, TextTop + static_cast<float>(i) * LineHeight, lines[i].c_str());
        }

        for (size_t i = 0; i < buttons.size(); ++i)
        {
            const SDL_FRect& area = buttons[i].Area;
            SetColor(renderer, Amber);

            if (i == lit)
            {
                SDL_RenderFillRect(renderer, &area);
                SetColor(renderer, Background);
            }
            else
            {
                SDL_RenderRect(renderer, &area);
            }

            SDL_RenderDebugText(renderer, area.x + ButtonPadding, area.y + (ButtonHeight - LetterWidth) / 2,
                                std::string(buttons[i].Label).c_str());
        }

        Present(renderer, shot);
    }

    /// <summary>The progress's report and the speed measured, as the window draws them.</summary>
    struct MCShown
    {
        MCUpdateWork::MCProgressReport Report;
        const MCUpdateWork::MCRate& Rate;
        bool Stopping = false;
    };

    /// <summary>
    /// Draws the window: the heading, the amount, the bar, and under it the speed and time left while bytes are to come
    /// (not while checking or stopping); saved to <paramref name="shot"/> when given.
    /// </summary>
    void Draw(SDL_Renderer* renderer, std::string_view heading, const MCShown& shown, const std::filesystem::path& shot)
    {
        const auto& [received, expected, checking] = shown.Report;
        SetColor(renderer, Background);
        SDL_RenderClear(renderer);
        SetColor(renderer, Amber);
        SDL_RenderDebugText(renderer, Margin, HeadingTop, std::string(heading).c_str());
        SDL_RenderDebugText(renderer, Margin, AmountTop,
                            MCFirstStart::AmountLine(shown.Report, shown.Stopping).c_str());

        // Once all is in (the files flushed, the update armed), there's no speed to tell.
        if (!shown.Stopping && !checking && received < expected)
        {
            const std::string rate = shown.Rate.Text(expected - received);
            SDL_RenderDebugText(renderer, Margin, RateTop, rate.c_str());
        }

        const float barWidth = static_cast<float>(WindowWidth) / DrawingScale - 2 * Margin;
        const double share =
            expected > 0 ? std::min(1.0, static_cast<double>(received) / static_cast<double>(expected)) : 0.0;
        const SDL_FRect filled{Margin, BarTop, barWidth * static_cast<float>(share), BarHeight};
        SDL_RenderFillRect(renderer, &filled);
        SetColor(renderer, BarEdge);
        const SDL_FRect edge{Margin, BarTop, barWidth, BarHeight};
        SDL_RenderRect(renderer, &edge);
        Present(renderer, shot);
    }

    /// <summary>
    /// The speed's next sample: only bytes downloaded count, so the samples start again while the work looks at what's
    /// needed or checks what's here.
    /// </summary>
    void Sample(MCUpdateWork::MCRate& rate, const MCUpdateWork::MCProgressReport& report,
                const MCFirstStart::MCProgress& progress)
    {
        if (report.Checking || report.Expected == 0)
        {
            rate.Reset();
            return;
        }

        rate.Add(std::chrono::steady_clock::now(), progress.Network());
    }

    /// <summary><see cref="MCFirstStart::Run"/> in a window open already.</summary>
    std::expected<bool, MCFirstStart::MCFailure> RunIn(MCOpenWindow& window, std::string_view heading,
                                                       MCFirstStart::MCWork work,
                                                       const MCFirstStart::MCWindowOptions& options)
    {
        SDL_Renderer* const renderer = window.Renderer;
        MCUpdateWork::MCWork<void, MCFirstStart::MCFailure> running(
            std::string(heading), [work = std::move(work)](MCUpdateWork::MCContext& context)
            { return work(context.Progress, context.Stop); });
        bool closed = false;
        std::chrono::steady_clock::time_point stopBy;
        MCUpdateWork::MCRate rate;

        while (!running.Finished())
        {
            SDL_Event event;

            while (SDL_PollEvent(&event))
            {
                if ((event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) && !closed)
                {
                    SDL_Log("First start: the window was closed; the download stops");
                    closed = true;
                    stopBy = std::chrono::steady_clock::now() + StopWait;
                    running.Stop();
                }
            }

            if (closed && std::chrono::steady_clock::now() >= stopBy)
            {
                break;
            }

            const MCUpdateWork::MCProgressReport report = running.Progress().Report();
            Sample(rate, report, running.Progress());
            Draw(renderer, heading, {.Report = report, .Rate = rate, .Stopping = closed}, {});
            SDL_WaitEventTimeout(nullptr, FrameMilliseconds);
        }

        Draw(renderer, heading,
             {.Report = running.Progress().Report(), .Rate = rate, .Stopping = closed && !running.Finished()},
             options.Shot);

        if (closed && !running.Finished())
        {
            SDL_Log("First start: the download is still stopping; the game waits for it (the next start resumes it)");
        }

        if (std::expected<void, MCFirstStart::MCFailure> result = running.Join(); !result)
        {
            if (closed)
            {
                SDL_Log("First start: the work stopped: %s", result.error().Reason.c_str());
                return false;
            }

            return std::unexpected(std::move(result.error()));
        }

        return true;
    }
}

std::string MCFirstStart::AmountLine(const MCUpdateWork::MCProgressReport& report, bool stopping)
{
    if (stopping)
    {
        return std::string(StoppingText);
    }

    if (report.Expected == 0)
    {
        return std::string(LookingText);
    }

    if (report.Checking)
    {
        return std::string(CheckingText);
    }

    return std::format("{} OF {}", MCUpdateWork::SizeText(report.Received), MCUpdateWork::SizeText(report.Expected));
}

std::string MCFirstStart::ProblemText(const MCFailure& failure)
{
    using MCUpdateWork::MCFailureKind;

    switch (failure.Kind)
    {
        case MCFailureKind::Unreachable:
            return std::string(UnreachableText);
        case MCFailureKind::DiskFull:
            return std::format(DiskFullText, MCUpdateWork::SizeText(failure.BytesShort));
        case MCFailureKind::NewerProgram:
            return std::string(NewerProgramText);
        case MCFailureKind::Other:
            break;
    }

    return std::string(OtherText);
}

std::expected<bool, MCFirstStart::MCFailure> MCFirstStart::Run(std::string_view heading, MCWork work,
                                                               const MCWindowOptions& options)
{
    const auto open = MCOpenWindow::Open(WindowHeight, options.Hidden);

    if (!open)
    {
        return std::unexpected(MCFailure{.Reason = open.error()});
    }

    return RunIn(**open, heading, std::move(work), options);
}

std::vector<std::string> MCFirstStart::Wrapped(std::string_view text, size_t width)
{
    std::vector<std::string> lines;
    // Where the last paragraph's lines begin.
    size_t lastStart = 0;

    for (const auto paragraph : std::views::split(text, '\n'))
    {
        lastStart = lines.size();
        std::string line;

        for (const auto part : std::views::split(std::string_view(paragraph), ' '))
        {
            std::string_view word(part);

            if (word.empty())
            {
                continue;
            }

            if (!line.empty() && line.size() + 1 + word.size() > width)
            {
                lines.push_back(std::move(line));
                line.clear();
            }

            while (word.size() > width)
            {
                if (!line.empty())
                {
                    lines.push_back(std::move(line));
                    line.clear();
                }

                lines.emplace_back(word.substr(0, width));
                word.remove_prefix(width);
            }

            if (!line.empty())
            {
                line += ' ';
            }

            line += word;
        }

        lines.push_back(std::move(line));
    }

    if (lines.size() <= MostLines)
    {
        return lines;
    }

    // The last paragraph (the step to take) shows whole, after a blank line when it has one; the text before it is cut.
    const size_t last = lines.size() - lastStart;
    const size_t blank = lastStart > 0 && lines[lastStart - 1].empty() ? 1 : 0;

    if (last + blank < MostLines)
    {
        const size_t before = MostLines - last - blank;
        std::vector<std::string> shown(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(before));
        CutLine(shown.back(), width);
        shown.insert(shown.end(), lines.begin() + static_cast<std::ptrdiff_t>(lastStart - blank), lines.end());
        return shown;
    }

    lines.resize(MostLines);
    CutLine(lines.back(), width);
    return lines;
}

namespace
{
    /// <summary>A question laid out: its lines, its buttons, and the window's height for them, in points.</summary>
    struct MCQuestion
    {
        std::vector<std::string> Lines;
        std::vector<MCButton> Buttons;
        int Height = 0;
    };

    MCQuestion LaidOutQuestion(std::string_view text, std::span<const std::string_view> buttons)
    {
        MCQuestion question{.Lines = MCFirstStart::Wrapped(text, LineLetters())};
        const float buttonsTop = TextTop + static_cast<float>(question.Lines.size()) * LineHeight + ButtonsGap;
        question.Buttons = LaidOut(buttons, buttonsTop);
        question.Height = static_cast<int>((buttonsTop + ButtonHeight + Margin) * DrawingScale);
        return question;
    }

    /// <summary><see cref="MCFirstStart::Ask"/> in a window open already, made the question's height.</summary>
    std::optional<size_t> AskIn(MCOpenWindow& window, std::string_view heading, std::string_view text,
                                std::span<const std::string_view> buttons, const MCFirstStart::MCWindowOptions& options)
    {
        const MCQuestion question = LaidOutQuestion(text, buttons);
        const std::vector<std::string>& lines = question.Lines;
        const std::vector<MCButton>& laidOut = question.Buttons;
        window.SetHeight(question.Height);
        SDL_Renderer* const renderer = window.Renderer;
        SDL_RaiseWindow(window.Window);
        size_t lit = 0;
        // The button the mouse went down on: one pressed is one let go on where it went down.
        std::optional<size_t> pressedOn;

        if (options.Answer)
        {
            DrawQuestion(renderer, heading, lines, laidOut, *options.Answer, options.Shot);
            return *options.Answer < buttons.size() ? options.Answer : std::nullopt;
        }

        for (;;)
        {
            DrawQuestion(renderer, heading, lines, laidOut, lit, {});
            SDL_Event event;

            if (!SDL_WaitEvent(&event))
            {
                SDL_Log("First start: the question's window gets no events: %s", SDL_GetError());
                return std::nullopt;
            }

            do
            {
                switch (event.type)
                {
                    case SDL_EVENT_QUIT:
                    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                        return std::nullopt;
                    case SDL_EVENT_MOUSE_MOTION:
                    {
                        lit = ButtonAt(laidOut, event.motion.x, event.motion.y).value_or(lit);
                        break;
                    }
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    {
                        if (event.button.button == SDL_BUTTON_LEFT)
                        {
                            pressedOn = ButtonAt(laidOut, event.button.x, event.button.y);
                        }

                        break;
                    }
                    case SDL_EVENT_MOUSE_BUTTON_UP:
                    {
                        if (event.button.button != SDL_BUTTON_LEFT)
                        {
                            break;
                        }

                        const std::optional<size_t> released = ButtonAt(laidOut, event.button.x, event.button.y);

                        if (released && released == pressedOn)
                        {
                            return released;
                        }

                        pressedOn.reset();
                        break;
                    }
                    case SDL_EVENT_KEY_DOWN:
                    {
                        // A key held from before the window opened (Enter, starting the game) repeats: only a press counts.
                        if (event.key.repeat)
                        {
                            break;
                        }

                        switch (event.key.key)
                        {
                            case SDLK_LEFT:
                                lit = (lit + buttons.size() - 1) % buttons.size();
                                break;
                            case SDLK_RIGHT:
                            case SDLK_TAB:
                                lit = (lit + 1) % buttons.size();
                                break;
                            case SDLK_RETURN:
                            case SDLK_KP_ENTER:
                            case SDLK_SPACE:
                                return lit;
                            case SDLK_ESCAPE:
                                return buttons.size() - 1;
                            default:
                                break;
                        }

                        break;
                    }
                    default:
                        break;
                }
            } while (SDL_PollEvent(&event));
        }
    }
}

std::optional<size_t> MCFirstStart::Ask(std::string_view heading, std::string_view text,
                                        std::span<const std::string_view> buttons, const MCWindowOptions& options)
{
    if (buttons.empty())
    {
        return std::nullopt;
    }

    const auto open = MCOpenWindow::Open(LaidOutQuestion(text, buttons).Height, options.Hidden);

    if (!open)
    {
        SDL_Log("First start: the question \"%.*s\" can't be asked: %s", static_cast<int>(heading.size()),
                heading.data(), open.error().c_str());
        return std::nullopt;
    }

    return AskIn(**open, heading, text, buttons, options);
}

std::expected<bool, std::string> MCFirstStart::RunAsking(std::string_view heading, const MCWork& work,
                                                         const MCWindowOptions& options)
{
    const auto open = MCOpenWindow::Open(WindowHeight, options.Hidden);

    if (!open)
    {
        return std::unexpected(open.error());
    }

    constexpr std::array<std::string_view, 2> Buttons = {TryAgainButton, QuitButton};

    for (;;)
    {
        const std::expected<bool, MCFailure> done = RunIn(**open, heading, work, options);

        if (done)
        {
            return *done;
        }

        SDL_Log("First start: %s failed: %s; the window asks whether to try again", std::string(heading).c_str(),
                done.error().Reason.c_str());
        const std::optional<size_t> answer = AskIn(**open, ProblemHeading, ProblemText(done.error()), Buttons, options);

        if (!answer || Buttons[*answer] == QuitButton)
        {
            SDL_Log("First start: the player quit; the next start carries on");
            return false;
        }

        SDL_Log("First start: the player tries again");
        (*open)->SetHeight(WindowHeight);
    }
}

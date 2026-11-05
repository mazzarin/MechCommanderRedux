#include "stdafx.h"
#include "MCFirstStart.h"
#include "platform/MCFileSystem.h"

#include <atomic>
#include <thread>

namespace
{
    /// <summary>The window's size in points; it's drawn at twice that scale, the built-in text's letters 16 points tall.</summary>
    constexpr int WindowWidth = 560;
    constexpr int WindowHeight = 140;
    constexpr float DrawingScale = 2.0f;

    /// <summary>Where the lines and the bar go, in the drawing's units (the window's points halved).</summary>
    constexpr float Margin = 14.0f;
    constexpr float HeadingTop = 14.0f;
    constexpr float AmountTop = 30.0f;
    constexpr float BarTop = 48.0f;
    constexpr float BarHeight = 10.0f;

    /// <summary>How often the window is drawn while the work goes on.</summary>
    constexpr int32_t FrameMilliseconds = 50;

    /// <summary>
    /// How long a closed window waits for the work to stop: a download stalled on the network stops only at its
    /// timeout, so past this the game ends without it (the next start resumes it).
    /// </summary>
    constexpr std::chrono::seconds StopWait{1};

    constexpr double BytesPerGigabyte = 1e9;

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

    /// <summary>The work's end, shared with its thread (which a closed window may leave behind).</summary>
    struct MCShared
    {
        MCFirstStart::MCProgress Progress;
        std::atomic<bool> Done = false;
        std::expected<void, std::string> Result;
    };

    void SetColor(SDL_Renderer* renderer, MCColor color)
    {
        SDL_SetRenderDrawColor(renderer, color.Red, color.Green, color.Blue, SDL_ALPHA_OPAQUE);
    }

    double Gigabytes(uint64_t bytes)
    {
        return static_cast<double>(bytes) / BytesPerGigabyte;
    }

    /// <summary>The second line: how much is in, or what's happening.</summary>
    std::string AmountLine(uint64_t received, uint64_t expected, bool stopping)
    {
        if (stopping)
        {
            return "STOPPING.";
        }

        if (expected == 0)
        {
            return "LOOKING AT WHAT'S NEEDED.";
        }

        return std::format("{:.1f} OF {:.1f} GB", Gigabytes(received), Gigabytes(expected));
    }

    /// <summary>Draws the window: the heading, the amount and the bar; saved to <paramref name="shot"/> when given.</summary>
    void Draw(SDL_Renderer* renderer, std::string_view heading, const MCFirstStart::MCProgress& progress, bool stopping,
              const std::filesystem::path& shot)
    {
        const auto [received, expected] = progress.Get();
        SetColor(renderer, Background);
        SDL_RenderClear(renderer);
        SetColor(renderer, Amber);
        SDL_RenderDebugText(renderer, Margin, HeadingTop, std::string(heading).c_str());
        SDL_RenderDebugText(renderer, Margin, AmountTop, AmountLine(received, expected, stopping).c_str());

        const float barWidth = static_cast<float>(WindowWidth) / DrawingScale - 2 * Margin;
        const double share =
            expected > 0 ? std::min(1.0, static_cast<double>(received) / static_cast<double>(expected)) : 0.0;
        const SDL_FRect filled{Margin, BarTop, barWidth * static_cast<float>(share), BarHeight};
        SDL_RenderFillRect(renderer, &filled);
        SetColor(renderer, BarEdge);
        const SDL_FRect edge{Margin, BarTop, barWidth, BarHeight};
        SDL_RenderRect(renderer, &edge);

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
}

void MCFirstStart::MCProgress::Set(uint64_t received, uint64_t expected)
{
    const std::lock_guard lock(_Mutex);
    _Received = received;
    _Expected = expected;
}

std::pair<uint64_t, uint64_t> MCFirstStart::MCProgress::Get() const
{
    const std::lock_guard lock(_Mutex);
    return {_Received, _Expected};
}

std::expected<bool, std::string> MCFirstStart::Run(std::string_view heading, MCWork work,
                                                   const MCWindowOptions& options)
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    const SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | (options.Hidden ? SDL_WINDOW_HIDDEN : 0);

    if (!SDL_CreateWindowAndRenderer("MechCommander Redux", WindowWidth, WindowHeight, flags, &window, &renderer))
    {
        return std::unexpected(std::format("its progress window can't open ({})", SDL_GetError()));
    }

    SDL_SetRenderScale(renderer, DrawingScale * SDL_GetWindowPixelDensity(window),
                       DrawingScale * SDL_GetWindowPixelDensity(window));
    const auto shared = std::make_shared<MCShared>();
    std::jthread worker(
        [shared, work = std::move(work)](std::stop_token stop)
        {
            shared->Result = work(shared->Progress, stop);
            shared->Done = true;
        });

    bool closed = false;
    std::chrono::steady_clock::time_point stopBy;

    while (!shared->Done)
    {
        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
            if ((event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) && !closed)
            {
                SDL_Log("First start: the window was closed; the download stops");
                closed = true;
                stopBy = std::chrono::steady_clock::now() + StopWait;
                worker.request_stop();
            }
        }

        if (closed && std::chrono::steady_clock::now() >= stopBy)
        {
            break;
        }

        Draw(renderer, heading, shared->Progress, closed, {});
        SDL_WaitEventTimeout(nullptr, FrameMilliseconds);
    }

    Draw(renderer, heading, shared->Progress, closed && !shared->Done, options.Shot);

    if (shared->Done)
    {
        worker.join();
    }
    else
    {
        // It holds only what it shares; it ends with the process.
        SDL_Log("First start: the download is still stopping as the game quits; the next start resumes it");
        worker.detach();
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    if (!shared->Done)
    {
        return false;
    }

    if (!shared->Result)
    {
        return closed ? std::expected<bool, std::string>(false) : std::unexpected(shared->Result.error());
    }

    return true;
}

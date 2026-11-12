#include "stdafx.h"
#include "platform/MCFrameLog.h"

namespace
{
    struct MCFrameLogState
    {
        /// <summary>Guards the rest: parts and notes come from any thread (MCFrameLog.h's Threads).</summary>
        std::mutex Lock;
        std::ofstream File;
        std::atomic<bool> Enabled = false;
        uint64_t FrameStart = 0;
        uint64_t Frame = 0;
        /// <summary>This frame's parts and their ticks.</summary>
        std::vector<std::pair<std::string, uint64_t>> Parts;
        std::vector<std::string> Notes;
        /// <summary>The last frames' times (ms), for what is usual.</summary>
        std::deque<double> Recent;
        std::vector<double> SinceSummary;
    };

    MCFrameLogState& state()
    {
        static MCFrameLogState instance;
        return instance;
    }

    double toMs(uint64_t ticks)
    {
        return static_cast<double>(ticks) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    }

    double median(std::vector<double> times)
    {
        if (times.empty())
        {
            return 0.0;
        }

        std::ranges::nth_element(times, times.begin() + static_cast<ptrdiff_t>(times.size() / 2));
        return times[times.size() / 2];
    }
}

namespace MCFrameLog
{
    bool Open(const std::filesystem::path& path)
    {
        MCFrameLogState& log = state();
        const std::scoped_lock lock(log.Lock);
        log.File.open(path, std::ios::out | std::ios::trunc);
        log.Enabled = log.File.is_open();

        if (log.Enabled)
        {
            log.File << "MCRedux frame log: frames much slower than the usual one (SLOW), and frames with notes.\n"
                        "Times in ms; a part is listed when it ends, so after the parts inside it.\n";
            log.File.flush();
        }

        return log.Enabled;
    }

    bool Enabled()
    {
        return state().Enabled;
    }

    void NextFrame()
    {
        MCFrameLogState& log = state();

        if (!log.Enabled)
        {
            return;
        }

        const std::scoped_lock lock(log.Lock);
        const uint64_t now = SDL_GetPerformanceCounter();

        if (log.FrameStart != 0)
        {
            const double ms = toMs(now - log.FrameStart);
            const double usual = median(std::vector<double>(log.Recent.begin(), log.Recent.end()));
            const bool slow = log.Recent.size() >= 30 && ms > usual * 1.5 && ms > usual + 6.0;

            if (slow || !log.Notes.empty())
            {
                log.File << std::format("frame {} {:.2f} ms (usual {:.2f}){}\n", log.Frame, ms, usual,
                                        slow ? " SLOW" : "");

                for (const auto& [name, ticks] : log.Parts)
                {
                    log.File << std::format("    {:<22} {:8.2f}\n", name, toMs(ticks));
                }

                for (const std::string& note : log.Notes)
                {
                    log.File << "    * " << note << "\n";
                }

                log.File.flush();
            }

            log.Recent.push_back(ms);

            if (log.Recent.size() > 120)
            {
                log.Recent.pop_front();
            }

            log.SinceSummary.push_back(ms);

            if (log.SinceSummary.size() >= 600)
            {
                std::vector<double> sorted = log.SinceSummary;
                std::ranges::sort(sorted);
                log.File << std::format("-- frames {}..{}: median {:.2f} ms, p99 {:.2f}, max {:.2f}\n",
                                        log.Frame + 1 - sorted.size(), log.Frame, sorted[sorted.size() / 2],
                                        sorted[sorted.size() * 99 / 100], sorted.back());
                log.File.flush();
                log.SinceSummary.clear();
            }
        }

        ++log.Frame;
        log.FrameStart = now;
        log.Parts.clear();
        log.Notes.clear();
    }

    void Note(std::string text)
    {
        MCFrameLogState& log = state();

        if (log.Enabled)
        {
            const std::scoped_lock lock(log.Lock);
            log.Notes.push_back(std::move(text));
        }
    }

    Scope::Scope(std::string_view name) : _Start(state().Enabled ? SDL_GetPerformanceCounter() : 0)
    {
        if (_Start != 0)
        {
            _Name = name;
        }
    }

    Scope::~Scope()
    {
        if (_Start == 0)
        {
            return;
        }

        const uint64_t ticks = SDL_GetPerformanceCounter() - _Start;
        MCFrameLogState& log = state();
        const std::scoped_lock lock(log.Lock);
        std::vector<std::pair<std::string, uint64_t>>& parts = log.Parts;

        for (auto& [name, total] : parts)
        {
            if (name == _Name)
            {
                total += ticks;
                return;
            }
        }

        parts.emplace_back(_Name, ticks);
    }
}

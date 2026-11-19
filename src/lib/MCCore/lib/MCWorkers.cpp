#include "stdafx.h"
#include "lib/MCWorkers.h"

MCWorkers::MCWorkers(size_t helpers, Priority priority, MCThreadPriority like)
    : _HelperCount(helpers), _Priority(priority), _Like(like)
{
}

MCWorkers::~MCWorkers() = default;

auto MCWorkers::HelpersFor(size_t most) -> size_t
{
    const size_t threads = std::thread::hardware_concurrency();
    return threads / 2 > 1 ? std::min(threads / 2 - 1, most) : 0;
}

auto MCWorkers::Run(size_t parts, const std::function<void(size_t)>& work) -> void
{
    if (parts == 0)
    {
        return;
    }

    // One part, or no helpers: this thread runs them all, in order.
    if (parts == 1 || _HelperCount == 0)
    {
        for (size_t part = 0; part < parts; ++part)
        {
            work(part);
        }

        return;
    }

    {
        std::scoped_lock lock(_Lock);

        while (_Threads.size() < _HelperCount)
        {
            try
            {
                _Threads.emplace_back([this](std::stop_token stop) { Help(std::move(stop)); });
            }
            catch (const std::system_error& error)
            {
                // The system wouldn't start another: those started help (or none, this thread doing it all).
                SDL_Log("MCWorkers: could not start helper %zu of %zu (%s); going on with %zu", _Threads.size() + 1,
                        _HelperCount, error.what(), _Threads.size());
                _HelperCount = _Threads.size();
            }
        }

        _Work = std::cref(work);
        _Parts = parts;
        _Error = nullptr;
        _Next.store(0);
        _Left.store(parts);
        ++_Batch;
    }

    _Handed.notify_all();
    TakeParts(work, parts);
    std::exception_ptr error;

    {
        std::unique_lock lock(_Lock);
        // Every part done, and no helper still holding the work (one that came late takes no part, but reads it).
        _Finished.wait(lock, [this] { return _Left.load() == 0 && _Inside == 0; });
        _Work.reset();
        error = std::exchange(_Error, nullptr);
    }

    if (error != nullptr)
    {
        std::rethrow_exception(error);
    }
}

auto MCWorkers::LowerThisThread(MCThreadPriority like) -> void
{
    if (constexpr bool Below = true; !MCSetCurrentThreadPriority(like, Below))
    {
        SDL_Log("MCWorkers: could not lower a thread's priority (as %d) a step; it runs as it was", like.Value);
    }
}

auto MCWorkers::Help(std::stop_token stop) -> void
{
    if (_Priority == Priority::Low)
    {
        LowerThisThread(_Like);
    }

    uint64_t seen = 0;
    std::unique_lock lock(_Lock);

    while (_Handed.wait(lock, stop, [this, &seen] { return _Batch != seen; }))
    {
        seen = _Batch;

        // Woken after its batch was done: nothing to take.
        if (!_Work.has_value())
        {
            continue;
        }

        const std::function<void(size_t)>& work = _Work->get();
        const size_t parts = _Parts;
        ++_Inside;
        lock.unlock();
        TakeParts(work, parts);
        lock.lock();
        --_Inside;
        _Finished.notify_all();
    }
}

auto MCWorkers::TakeParts(const std::function<void(size_t)>& work, size_t parts) -> void
{
    for (size_t part = _Next.fetch_add(1); part < parts; part = _Next.fetch_add(1))
    {
        try
        {
            work(part);
        }
        catch (...)
        {
            // Kept for Run to throw on its thread once every part is done.
            std::scoped_lock lock(_Lock);

            if (_Error == nullptr)
            {
                _Error = std::current_exception();
            }
        }

        if (_Left.fetch_sub(1) == 1)
        {
            std::scoped_lock lock(_Lock);
            _Finished.notify_all();
        }
    }
}

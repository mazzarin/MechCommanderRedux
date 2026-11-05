#include "stdafx.h"
#include "update/MCUpdateWork.h"
#include "update/MCContentStage.h"

namespace
{
    /// <summary>Decimal units, as the setup and the release give sizes.</summary>
    constexpr double BytesPerGigabyte = 1e9;
    constexpr double BytesPerMegabyte = 1e6;

    /// <summary>From here a size shows in GB: 999.95 MB would be written 1000.0 MB, so it's 1.0 GB.</summary>
    constexpr double ShownInGigabytes = 999.95e6;

    /// <summary>A size's unit, as the screens write it, and its bytes.</summary>
    struct MCUnit
    {
        std::string_view Name;
        double Bytes = 1;
    };

    MCUnit UnitFor(uint64_t bytes)
    {
        if (static_cast<double>(bytes) >= ShownInGigabytes)
        {
            return {.Name = "GB", .Bytes = BytesPerGigabyte};
        }

        return {.Name = "MB", .Bytes = BytesPerMegabyte};
    }

    double InUnit(uint64_t bytes, const MCUnit& unit)
    {
        return static_cast<double>(bytes) / unit.Bytes;
    }
}

std::string MCUpdateWork::SizeText(uint64_t bytes)
{
    const MCUnit unit = UnitFor(bytes);
    return std::format("{:.1f} {}", InUnit(bytes, unit), unit.Name);
}

std::string MCUpdateWork::ProgressText(uint64_t received, uint64_t whole)
{
    const MCUnit unit = UnitFor(whole);
    return std::format("{:.1f} OF {:.1f} {}", InUnit(received, unit), InUnit(whole, unit), unit.Name);
}

void MCUpdateWork::MCProgress::Set(uint64_t received, uint64_t expected)
{
    const std::lock_guard lock(_Mutex);
    _Received = received;
    _Expected = expected;
}

std::pair<uint64_t, uint64_t> MCUpdateWork::MCProgress::Get() const
{
    const std::lock_guard lock(_Mutex);
    return {_Received, _Expected};
}

std::function<bool(uint64_t, uint64_t)> MCUpdateWork::Reporter(MCProgress& progress, std::stop_token stop)
{
    return [&progress, stop = std::move(stop)](uint64_t received, uint64_t expected)
    {
        progress.Set(received, expected);
        return !stop.stop_requested();
    };
}

std::function<bool()> MCUpdateWork::GoOn(std::stop_token stop)
{
    return [stop = std::move(stop)] { return !stop.stop_requested(); };
}

MCUpdateWork::MCWorkSource::MCWorkSource(const MCSourceMaker& maker, std::stop_token stop)
    : _Source(maker ? maker() : nullptr)
{
    if (_Source == nullptr)
    {
        return;
    }

    // Asked to stop already, it's cancelled here and now.
    _Cancel.emplace(std::move(stop), std::function<void()>([source = _Source.get()] { source->Cancel(); }));
}

MCUpdateWork::MCWorkSource::~MCWorkSource()
{
    // The cancel goes before the transfer it calls.
    _Cancel.reset();
}

MCUpdateWork::MCWorkThread::MCWorkThread(std::string name, std::function<void(MCContext&)> body)
    : _Name(std::move(name))
    , _Thread(
          [this, body = std::move(body)](std::stop_token stop)
          {
              MCContext context{.Progress = _Progress, .Stop = std::move(stop)};
              body(context);

              {
                  const std::lock_guard lock(_Mutex);
                  _Finished = true;
              }

              _Ended.notify_all();
          })
{
}

MCUpdateWork::MCWorkThread::~MCWorkThread()
{
    Stop();
    Join();
}

bool MCUpdateWork::MCWorkThread::Finished() const
{
    const std::lock_guard lock(_Mutex);
    return _Finished;
}

void MCUpdateWork::MCWorkThread::Stop()
{
    _Thread.request_stop();
}

void MCUpdateWork::MCWorkThread::Join()
{
    if (!_Thread.joinable())
    {
        return;
    }

    {
        std::unique_lock lock(_Mutex);

        if (!_Ended.wait_for(lock, JoinWarning, [this] { return _Finished; }))
        {
            SDL_Log("Updates: %s hasn't ended %lld s after it was waited for; the game waits on for it", _Name.c_str(),
                    static_cast<long long>(JoinWarning.count()));
            _Ended.wait(lock, [this] { return _Finished; });
        }
    }

    _Thread.join();
}

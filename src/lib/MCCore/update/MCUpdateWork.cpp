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

    /// <summary>Speeds in decimal units, as sizes are: from here a speed shows in MB a second (999.95 KB/S is 1.0 MB/S).</summary>
    constexpr double BytesPerKilobyte = 1e3;
    constexpr double SpeedInMegabytes = 999.95e3;

    /// <summary>What parts the speed from the time left on the window's line under its bar: a middle dot.</summary>
    constexpr std::string_view LineSeparator = " \xC2\xB7 ";

    constexpr int64_t MinutesPerHour = 60;

    /// <summary>
    /// A source whose bytes are counted as network bytes (<see cref="MCUpdateWork::Counted"/>): everything else passes to
    /// the source it wraps, and the sources made from it are counted too.
    /// </summary>
    class MCCountedSource final : public MCContentStage::MCSource
    {
    public:
        MCCountedSource(std::unique_ptr<MCContentStage::MCSource> inner, MCUpdateWork::MCProgress& progress)
            : _Inner(std::move(inner)), _Progress(progress)
        {
        }

        std::expected<void, std::string> Fetch(std::string_view name, const std::filesystem::path& to, uint64_t size,
                                               const std::function<bool(std::span<const char>)>& received) override
        {
            return _Inner->Fetch(name, to, size,
                                 [&](std::span<const char> bytes)
                                 {
                                     _Progress.AddNetwork(bytes.size());
                                     return received(bytes);
                                 });
        }

        std::expected<std::string, std::string> Read(std::string_view name, size_t most) override
        {
            auto read = _Inner->Read(name, most);

            if (read)
            {
                _Progress.AddNetwork(read->size());
            }

            return read;
        }

        void Cancel() override { _Inner->Cancel(); }

        std::unique_ptr<MCContentStage::MCSource> Another() override
        {
            std::unique_ptr<MCContentStage::MCSource> another = _Inner->Another();
            return another != nullptr ? std::make_unique<MCCountedSource>(std::move(another), _Progress) : nullptr;
        }

        bool HostUnreachable() const override { return _Inner->HostUnreachable(); }

    private:
        std::unique_ptr<MCContentStage::MCSource> _Inner;
        MCUpdateWork::MCProgress& _Progress;
    };
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

std::string MCUpdateWork::SpeedText(double bytesPerSecond)
{
    if (bytesPerSecond >= SpeedInMegabytes)
    {
        return std::format("{:.1f} MB/S", bytesPerSecond / BytesPerMegabyte);
    }

    return std::format("{:.0f} KB/S", std::max(0.0, bytesPerSecond) / BytesPerKilobyte);
}

std::string MCUpdateWork::TimeLeftText(std::chrono::seconds left)
{
    if (left < std::chrono::minutes(1))
    {
        return "LESS THAN A MINUTE LEFT";
    }

    const int64_t minutes = std::chrono::round<std::chrono::minutes>(left).count();

    if (minutes < MinutesPerHour)
    {
        return std::format("ABOUT {} MIN LEFT", minutes);
    }

    const int64_t hours = minutes / MinutesPerHour;
    const int64_t rest = minutes % MinutesPerHour;
    return rest == 0 ? std::format("ABOUT {} HR LEFT", hours) : std::format("ABOUT {} HR {} MIN LEFT", hours, rest);
}

void MCUpdateWork::MCRate::MCAverage::Add(double value, double seconds, std::chrono::seconds smoothing)
{
    const double kept = std::exp(-seconds / std::chrono::duration<double>(smoothing).count());
    Sum = Sum * kept + value * (1 - kept);
    Weight = Weight * kept + (1 - kept);
}

void MCUpdateWork::MCRate::Add(MCClock::time_point at, uint64_t networkBytes, uint64_t left)
{
    if (!_Start)
    {
        _Start = at;
        _Last = at;
        _LastBytesAt = at;
        _Bytes = networkBytes;
        return;
    }

    if (at - _Last < SampleEvery)
    {
        return;
    }

    const double seconds = std::chrono::duration<double>(at - _Last).count();
    const uint64_t got = networkBytes - std::min(networkBytes, _Bytes);
    const double perSecond = static_cast<double>(got) / seconds;
    _Speed.Add(perSecond, seconds, SpeedSmoothing);
    _SteadySpeed.Add(perSecond, seconds, TimeLeftSmoothing);
    _Last = at;
    _Bytes = networkBytes;

    if (got > 0)
    {
        _LastBytesAt = at;
    }

    if (Stalled() || _SteadySpeed.Value() <= 0)
    {
        _Countdown.reset();
        return;
    }

    // The countdown goes on a second a second; a new estimate replaces it only once it strays past the tolerance.
    const double estimate = static_cast<double>(left) / _SteadySpeed.Value();
    const double tolerance =
        std::max(TimeLeftTolerance * estimate, std::chrono::duration<double>(TimeLeftToleranceFloor).count());

    if (_Countdown)
    {
        *_Countdown = std::max(0.0, *_Countdown - seconds);
    }

    if (!_Countdown || std::abs(estimate - *_Countdown) > tolerance)
    {
        _Countdown = estimate;
    }
}

MCUpdateWork::MCRate::MCClock::duration MCUpdateWork::MCRate::Span() const
{
    return _Start ? _Last - *_Start : MCClock::duration::zero();
}

bool MCUpdateWork::MCRate::Stalled() const
{
    return _Last - _LastBytesAt >= StalledAfter;
}

std::optional<double> MCUpdateWork::MCRate::BytesPerSecond() const
{
    if (Span() < SpeedAfter)
    {
        return std::nullopt;
    }

    return Stalled() ? 0.0 : _Speed.Value();
}

std::optional<std::chrono::seconds> MCUpdateWork::MCRate::TimeLeft() const
{
    if (Span() < TimeLeftAfter || !_Countdown)
    {
        return std::nullopt;
    }

    return std::chrono::seconds(static_cast<int64_t>(std::ceil(*_Countdown)));
}

std::string MCUpdateWork::MCRate::Text() const
{
    const std::optional<double> speed = BytesPerSecond();

    if (!speed)
    {
        return {};
    }

    const std::optional<std::chrono::seconds> time = TimeLeft();
    return time ? std::format("{}{}{}", SpeedText(*speed), LineSeparator, TimeLeftText(*time)) : SpeedText(*speed);
}

void MCUpdateWork::MCProgress::Set(uint64_t received, uint64_t expected)
{
    Set({.Received = received, .Expected = expected});
}

void MCUpdateWork::MCProgress::Set(const MCProgressReport& report)
{
    const std::lock_guard lock(_Mutex);
    _Report = report;
}

std::pair<uint64_t, uint64_t> MCUpdateWork::MCProgress::Get() const
{
    const std::lock_guard lock(_Mutex);
    return {_Report.Received, _Report.Expected};
}

MCUpdateWork::MCProgressReport MCUpdateWork::MCProgress::Report() const
{
    const std::lock_guard lock(_Mutex);
    return _Report;
}

MCUpdateWork::MCProgressCallback MCUpdateWork::Reporter(MCProgress& progress, std::stop_token stop)
{
    return [&progress, stop = std::move(stop)](const MCProgressReport& report)
    {
        progress.Set(report);
        return !stop.stop_requested();
    };
}

MCUpdateWork::MCSourceMaker MCUpdateWork::Counted(MCSourceMaker maker, MCProgress& progress)
{
    return [maker = std::move(maker), &progress]() -> std::unique_ptr<MCContentStage::MCSource>
    {
        std::unique_ptr<MCContentStage::MCSource> source = maker ? maker() : nullptr;
        return source != nullptr ? std::make_unique<MCCountedSource>(std::move(source), progress) : nullptr;
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

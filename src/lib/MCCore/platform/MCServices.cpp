#include "stdafx.h"
#include "platform/MCServices.h"
#include "main/MCGameContext.h"

namespace
{
    /// <summary>Days from 1601-01-01 (the FILETIME epoch) to 1970-01-01.</summary>
    constexpr int64_t FileTimeEpochDays = 134774;
    /// <summary>FILETIME ticks (100 ns) in a day.</summary>
    constexpr int64_t TicksPerDay = 864000000000ll;

    /// <summary>A time since the Unix epoch as FILETIME ticks.</summary>
    template <typename Duration> uint64_t ToFileTime(Duration sinceUnix)
    {
        using namespace std::chrono;
        const auto ticks = duration_cast<duration<int64_t, std::ratio<1, 10000000>>>(sinceUnix);
        return static_cast<uint64_t>(ticks.count() + FileTimeEpochDays * TicksPerDay);
    }

    /// <summary>The CRT generator's state; per thread and starting at 1, as the CRT's.</summary>
    thread_local uint32_t RandSeed = 1;
}

uint32_t MCSystemClock::Milliseconds()
{
    return static_cast<uint32_t>(SDL_GetTicks());
}

int64_t MCSystemClock::PerformanceCounter()
{
    return static_cast<int64_t>(SDL_GetPerformanceCounter());
}

int64_t MCSystemClock::PerformanceFrequency()
{
    return static_cast<int64_t>(SDL_GetPerformanceFrequency());
}

uint64_t MCSystemClock::UtcFileTime()
{
    return ToFileTime(std::chrono::system_clock::now().time_since_epoch());
}

uint64_t MCSystemClock::LocalFileTime()
{
#if defined(__APPLE__)
    // libc++ has no std::chrono::current_zone(); use the C library's UTC offset for "now".
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&t, &local);
    return ToFileTime(now.time_since_epoch() + std::chrono::seconds(local.tm_gmtoff));
#else
    const auto local = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
    return ToFileTime(local.time_since_epoch());
#endif
}

int32_t MCCrtRandom::Next()
{
    RandSeed = Step(RandSeed);
    return Value(RandSeed);
}

void MCCrtRandom::Seed(uint32_t seed)
{
    RandSeed = seed;
}

uint32_t MCCrtRandom::State() const
{
    return RandSeed;
}

namespace MCSocket
{
    namespace
    {
        MCNetTransport& Net()
        {
            return MCGameContext::Current().Net();
        }
    }

    bool Startup()
    {
        return Net().Startup();
    }

    std::optional<uint32_t> Resolve(std::string_view host)
    {
        return Net().Resolve(host);
    }

    Handle ListenTcp(uint16_t port)
    {
        return Net().ListenTcp(port);
    }

    Handle AcceptTcp(Handle listener, Address& from)
    {
        return Net().AcceptTcp(listener, from);
    }

    Handle ConnectTcp(const Address& to, int timeoutMs)
    {
        return Net().ConnectTcp(to, timeoutMs);
    }

    void KeepAlive(Handle socket)
    {
        Net().KeepAlive(socket);
    }

    int Send(Handle socket, std::span<const uint8_t> data)
    {
        return Net().Send(socket, data);
    }

    int Receive(Handle socket, std::span<uint8_t> data)
    {
        return Net().Receive(socket, data);
    }

    Handle OpenUdp(uint16_t port)
    {
        return Net().OpenUdp(port);
    }

    bool SendTo(Handle socket, const Address& to, std::span<const uint8_t> data)
    {
        return Net().SendTo(socket, to, data);
    }

    int ReceiveFrom(Handle socket, std::span<uint8_t> data, Address& from)
    {
        return Net().ReceiveFrom(socket, data, from);
    }

    uint16_t LocalPort(Handle socket)
    {
        return Net().LocalPort(socket);
    }

    void Close(Handle socket)
    {
        Net().Close(socket);
    }

    bool EndSending(Handle socket)
    {
        return Net().EndSending(socket);
    }
}

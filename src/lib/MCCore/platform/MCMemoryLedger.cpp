#include "stdafx.h"
#include "platform/MCMemoryLedger.h"

namespace
{
    using MCMemoryLedger::MCHolder;
    using MCMemoryLedger::MCSide;

    /// <summary>A holder's name and side.</summary>
    struct MCHolderFacts
    {
        std::string_view Name;
        MCSide Side = MCSide::Cpu;
    };

    constexpr std::array<MCHolderFacts, MCMemoryLedger::HolderCount> Holders{{
        {"hd frames", MCSide::Cpu},
        {"hd read-ahead", MCSide::Cpu},
        {"hd hold", MCSide::Cpu},
        {"hd kept streams", MCSide::Cpu},
        {"archive tables", MCSide::Cpu},
        {"hd sounds", MCSide::Cpu},
        {"hd pictures", MCSide::Gpu},
        {"shapes", MCSide::Gpu},
        {"pictures", MCSide::Gpu},
        {"hd ground", MCSide::Gpu},
        {"ground", MCSide::Gpu},
        {"targets", MCSide::Gpu},
        {"movies", MCSide::Gpu},
        {"transfers", MCSide::Gpu},
        {"other", MCSide::Gpu},
    }};

    /// <summary>A holder's figures, as atomics any thread adds to.</summary>
    struct MCCounted
    {
        std::atomic<int64_t> Payload = 0;
        std::atomic<int64_t> Allocated = 0;
        std::atomic<int64_t> Retired = 0;
    };

    std::array<MCCounted, MCMemoryLedger::HolderCount>& Counted()
    {
        static std::array<MCCounted, MCMemoryLedger::HolderCount> counted;
        return counted;
    }

    constexpr double BytesPerMb = 1024.0 * 1024.0;
    constexpr uint64_t CeilingShare = 2;
    constexpr uint64_t CardPercent = 90;
    constexpr uint64_t Hundred = 100;

    double Mb(int64_t bytes)
    {
        return static_cast<double>(std::max<int64_t>(bytes, 0)) / BytesPerMb;
    }

    double Mb(uint64_t bytes)
    {
        return static_cast<double>(bytes) / BytesPerMb;
    }

    /// <summary>A difference's MB, negative where the ledger counts more than the system.</summary>
    double SignedMb(int64_t bytes)
    {
        return static_cast<double>(bytes) / BytesPerMb;
    }

    MCMemoryLedger::MCFigures Difference(const MCMemoryLedger::MCFigures& to, const MCMemoryLedger::MCFigures& from)
    {
        return {.Payload = to.Payload - from.Payload,
                .Allocated = to.Allocated - from.Allocated,
                .Retired = to.Retired - from.Retired};
    }

    /// <summary>A side's holders with any bytes, as the line puts them, and their sum.</summary>
    std::string SideText(MCSide side, MCMemoryLedger::MCFigures& sum)
    {
        std::string text;

        for (size_t at = 0; at < MCMemoryLedger::HolderCount; at++)
        {
            const auto holder = static_cast<MCHolder>(at);

            if (MCMemoryLedger::SideOf(holder) != side)
            {
                continue;
            }

            const MCMemoryLedger::MCFigures figures = MCMemoryLedger::Of(holder);

            if (figures == MCMemoryLedger::MCFigures{})
            {
                continue;
            }

            sum.Payload += figures.Payload;
            sum.Allocated += figures.Allocated;
            sum.Retired += figures.Retired;
            text += std::format("{}{} {:.0f}/{:.0f}/{:.0f}", text.empty() ? "" : ", ", MCMemoryLedger::NameOf(holder),
                                Mb(figures.Payload), Mb(figures.Allocated), Mb(figures.Retired));
        }

        return text.empty() ? "none" : text;
    }

    /// <summary>A side's bytes against the plan: what it holds, allocated or retired, both still the system's to free.</summary>
    int64_t Holding(const MCMemoryLedger::MCFigures& sum)
    {
        return std::max<int64_t>(sum.Allocated + sum.Retired, 0);
    }
}

std::string_view MCMemoryLedger::NameOf(MCHolder holder)
{
    return Holders.at(static_cast<size_t>(holder)).Name;
}

MCMemoryLedger::MCSide MCMemoryLedger::SideOf(MCHolder holder)
{
    return Holders.at(static_cast<size_t>(holder)).Side;
}

bool MCMemoryLedger::PayloadApart(MCHolder holder)
{
    return holder == MCHolder::GpuHdPictures;
}

void MCMemoryLedger::Add(MCHolder holder, const MCFigures& change)
{
    MCCounted& counted = Counted().at(static_cast<size_t>(holder));
    counted.Payload.fetch_add(change.Payload, std::memory_order_relaxed);
    counted.Allocated.fetch_add(change.Allocated, std::memory_order_relaxed);
    counted.Retired.fetch_add(change.Retired, std::memory_order_relaxed);
}

MCMemoryLedger::MCFigures MCMemoryLedger::Of(MCHolder holder)
{
    const MCCounted& counted = Counted().at(static_cast<size_t>(holder));
    return {.Payload = counted.Payload.load(std::memory_order_relaxed),
            .Allocated = counted.Allocated.load(std::memory_order_relaxed),
            .Retired = counted.Retired.load(std::memory_order_relaxed)};
}

MCMemoryLedger::MCShare::~MCShare()
{
    Set({});
}

MCMemoryLedger::MCShare::MCShare(MCShare&& other) noexcept
    : _Holder(std::exchange(other._Holder, std::nullopt)), _Figures(std::exchange(other._Figures, {}))
{
}

MCMemoryLedger::MCShare& MCMemoryLedger::MCShare::operator=(MCShare&& other) noexcept
{
    if (this != &other)
    {
        Set({});
        _Holder = std::exchange(other._Holder, std::nullopt);
        _Figures = std::exchange(other._Figures, {});
    }

    return *this;
}

void MCMemoryLedger::MCShare::Set(const MCFigures& figures)
{
    if (!_Holder.has_value() || figures == _Figures)
    {
        return;
    }

    Add(*_Holder, Difference(figures, _Figures));
    _Figures = figures;
}

void MCMemoryLedger::MCShare::SetBytes(size_t bytes)
{
    const auto counted = static_cast<int64_t>(bytes);
    Set({.Payload = counted, .Allocated = counted});
}

uint64_t MCMemoryLedger::Ceiling(const MCRoom& room)
{
    return room.MachineBytes / CeilingShare;
}

uint64_t MCMemoryLedger::CardCeiling(uint64_t cardBytes)
{
    return cardBytes * CardPercent / Hundred;
}

std::string MCMemoryLedger::Line(const MCRoom& room, const MCProcessFigures& process)
{
    MCFigures cpu;
    MCFigures gpu;
    const std::string cpuText = SideText(MCSide::Cpu, cpu);
    const std::string gpuText = SideText(MCSide::Gpu, gpu);
    std::string line =
        std::format("   memory (MB, payload/allocated/retired): cpu: {} = {:.0f}/{:.0f}/{:.0f}; gpu: {} = "
                    "{:.0f}/{:.0f}/{:.0f}",
                    cpuText, Mb(cpu.Payload), Mb(cpu.Allocated), Mb(cpu.Retired), gpuText, Mb(gpu.Payload),
                    Mb(gpu.Allocated), Mb(gpu.Retired));
    const uint64_t ceiling = Ceiling(room);

    if (room.Shared || !room.CardBytes.has_value())
    {
        line += std::format("; holding {:.0f} of {:.0f} (half of {:.0f}, shared)", Mb(Holding(cpu) + Holding(gpu)),
                            Mb(ceiling), Mb(room.MachineBytes));
    }
    else
    {
        line += std::format("; holding {:.0f} of {:.0f} (half of {:.0f}), the card {:.0f} of {:.0f} (90% of {:.0f})",
                            Mb(Holding(cpu)), Mb(ceiling), Mb(room.MachineBytes), Mb(Holding(gpu)),
                            Mb(CardCeiling(*room.CardBytes)), Mb(*room.CardBytes));
    }

    // What the system counts that no holder names (the game's baseline, frameworks, drivers, the allocator's rounding):
    // on macOS the footprint takes in the GPU's memory too; on Windows the GPU's is DXGI's apart from private bytes,
    // which hold the driver's mappings of it as well (GpuMapped), taken out.
    const bool gpuApart = process.GpuLocal != 0 || process.GpuNonLocal != 0;

    if (process.Footprint != 0)
    {
        const int64_t named = Holding(cpu) + (gpuApart ? 0 : Holding(gpu));
        const int64_t own = static_cast<int64_t>(process.Footprint) - static_cast<int64_t>(process.GpuMapped);
        line += std::format("; system: {:.0f}", Mb(process.Footprint));

        if (process.GpuMapped != 0)
        {
            line += std::format(" ({:.0f} the gpu's, mapped)", Mb(process.GpuMapped));
        }

        line += std::format(", unnamed {:.0f}", SignedMb(own - named));
    }

    if (gpuApart)
    {
        line += std::format(", gpu local {:.0f}, non-local {:.0f}, unnamed {:.0f}", Mb(process.GpuLocal),
                            Mb(process.GpuNonLocal),
                            SignedMb(static_cast<int64_t>(process.GpuLocal + process.GpuNonLocal) - Holding(gpu)));
    }

    return line;
}

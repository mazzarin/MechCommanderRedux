#pragma once

#include <cstdint>

/// <summary>Port-only: FNV-1a over 64 bits, a value at a time (the GPU renderer's keys and the sharp pictures').</summary>
namespace MCFnvHash
{
    /// <summary>The offset basis (a hash of nothing) and the prime.</summary>
    inline constexpr uint64_t Basis = 0xcbf29ce484222325ull;
    inline constexpr uint64_t Prime = 0x100000001b3ull;

    /// <summary>Mixes <paramref name="value"/> into <paramref name="hash"/>.</summary>
    constexpr void Mix(uint64_t& hash, uint64_t value)
    {
        hash ^= value;
        hash *= Prime;
    }
}

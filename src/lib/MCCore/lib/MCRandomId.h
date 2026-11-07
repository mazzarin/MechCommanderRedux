#pragma once

// Port: a random ID of 128 bits as 32 lowercase hex digits, from the system's random source. A copy's IDs (the reports'
// in main/MCTelemetry, the internet games' in network/MCInternetCopy) are each made apart, so neither tells of the other.

#include <algorithm>
#include <cstddef>
#include <format>
#include <random>
#include <string>
#include <string_view>

inline constexpr size_t MCRandomIdDigits = 32;

/// <summary>A new random ID.</summary>
inline std::string MCRandomId()
{
    // Each of the random source's words is 32 bits: 8 hex digits.
    constexpr size_t wordDigits = 8;
    std::random_device random;
    std::string id;

    for (size_t digits = 0; digits < MCRandomIdDigits; digits += wordDigits)
    {
        id += std::format("{:0{}x}", random(), wordDigits);
    }

    return id;
}

/// <summary>Whether <paramref name="id"/> is an ID as <see cref="MCRandomId"/> makes them.</summary>
inline bool MCIsRandomId(std::string_view id)
{
    return id.size() == MCRandomIdDigits &&
           std::ranges::all_of(id, [](char digit)
                               { return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'); });
}

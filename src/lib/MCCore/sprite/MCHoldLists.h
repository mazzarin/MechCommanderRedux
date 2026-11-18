#pragma once

// Port-only (SPEC-memory.md's "From the briefing"): the HD pack's folders each mission's load hands the hold,
// which its briefing reads ahead (MCHdArt::BriefingFolders), and each mech chassis's, read as it's placed in the
// lance (MCHdArt::ChassisFolders): worked out by loading each mission with mc_tests --hold-list and written by
// mc_tests --hold-lists-table (tests/HdPackTests.cpp). Don't edit it.

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

// clang-format off
namespace MCHoldLists
{
/// <summary>A mission (its file's name, lower case) and its folders, as indices into Folders.</summary>
struct Mission
{
    std::string_view Name;
    std::span<const uint16_t> Folders;
};

/// <summary>A mech chassis (its packet in the object file) and its tree's folders, as indices into Folders.</summary>
struct Chassis
{
    uint32_t Number;
    std::span<const uint16_t> Folders;
};

inline constexpr std::array<std::string_view, 0> Folders{{
}};


inline constexpr std::array<Mission, 0> Missions{{
}};

inline constexpr std::array<Chassis, 0> Mechs{{
}};
}

// clang-format on

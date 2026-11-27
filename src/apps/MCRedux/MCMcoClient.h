#pragma once

#include "network/MCMcoApi.h"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

// Port-only (Redux HD; SPEC-mco-rooms.md's "Who it says it is"): who this copy is to MechCommander Online, for the game
// and the MCO probe (MCMcoProbe.h) alike: its own device (a seed kept in the save folder's mco.cfg, made there the first
// time), its user agent and its system as MCO's launcher names Windows'.

/// <summary>This copy as MCO's client, its services at <paramref name="site"/> (MCO's, or a stand-in's), or why not.</summary>
std::expected<MCMcoApi::MCClient, std::string> MCMcoThisClient(const std::filesystem::path& userFolder,
                                                               std::string_view site = MCMcoApi::Site);

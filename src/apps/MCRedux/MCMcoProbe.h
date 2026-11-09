#pragma once

#include <span>
#include <string_view>

// Port-only (Redux HD): the MechCommander Online probe (SPEC-mco-rooms.md's Size: the probe first), run from the game's
// own program. It signs in to MCO as Redux HD names itself (its own device, user agent and system), registers in MCO's
// lobby, lists the rooms there (the Redux rooms' build too), then leaves the lobby and signs out: whether MCO takes a
// client that isn't its own. The player types the email and password at its prompt; neither, nor any token, is shown
// or saved. Only the device's seed is saved, in the save folder's mco.cfg, so MCO sees one device each run. On Windows
// the game's program doesn't hold the terminal it's started from, so the probe is started with "start /wait".

namespace MCMcoProbe
{
    /// <summary>The command line's word that starts the probe (the first argument).</summary>
    inline constexpr std::string_view Option = "--mco-probe";

    /// <summary>
    /// Runs the probe: <c>guest</c> (a guest's profile, made new), <c>sign-in</c> (an account) or <c>create</c> (an
    /// account made, from a guest's profile), then <c>--name NAME</c> (a guest's or new account's player name, else
    /// asked), <c>--seconds N</c> (the rooms polled for N seconds, each change shown), <c>--user FOLDER</c> (the save
    /// folder, where mco.cfg is kept; else the game's) and <c>--service ADDRESS/</c> (a stand-in's address for MCO's
    /// site, its two services' pages under it).
    /// </summary>
    /// <param name="arguments">The words after <see cref="Option"/>.</param>
    /// <returns>The exit code: 0 once the lobby was listed.</returns>
    int Run(std::span<const std::string_view> arguments);
}

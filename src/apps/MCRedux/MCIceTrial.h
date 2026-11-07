#pragma once

#include <span>
#include <string_view>

// Port-only (the personal build): the internet games' trial (PLAN.md, step 2), run from the game's own program so each
// system's firewall sees the game. Two machines, one hosting and one joining by the code it prints, open a link
// through the trial service (tools/cloudflare/trial in the workspace) and the game's transport (platform/MCIceTransport),
// and the guest sends a game's worth of small messages for its seconds, timing each one's round trip. Each end prints
// its link's report as it ends (MCTelemetry::LinkJson: its path, setup, traffic, never an address), to the terminal
// and to --out's file. Nothing goes to the reports.

namespace MCIceTrial
{
    /// <summary>The command line's word that starts the trial (the first argument).</summary>
    inline constexpr std::string_view Option = "--ice-trial";

    /// <summary>
    /// Runs the trial: <c>host</c> or <c>join CODE</c>, then <c>--relay-only</c> (a public game's links),
    /// <c>--seconds N</c>, <c>--service wss://...</c> and <c>--out FILE</c>.
    /// </summary>
    /// <param name="arguments">The words after <see cref="Option"/>.</param>
    /// <returns>The exit code: 0 when the link opened and carried every message.</returns>
    int Run(std::span<const std::string_view> arguments);
}

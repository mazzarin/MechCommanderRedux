#pragma once

// Port-only (SPEC-menus.md's stage 8): a mission's briefing text (its mission file's BriefingFile, in the original's
// formatting codes: MCSmuti) read into the parts the remade BRIEFING and the loading screen show apart: the objectives
// numbered, the time limits, the fire support, the orders by paragraph and the tactical warnings. The 1998 briefing drew
// the text whole in its pane; the parts are found by the lines the campaigns' briefings all write ("1-Destroy ...",
// "MISSION TIME LIMIT- 15:00", "FIRE SUPPORT:", "**TACTICAL WARNING**", the rules of dashes between them). The title and
// the operation are MCMissionNames'.

namespace MCBriefingText
{
    /// <summary>What a mission's FIRE SUPPORT lines give: its artillery strikes, sensor probes and camera drones.</summary>
    struct MCFireSupport
    {
        int32_t SmallArtillery = 0;
        int32_t LargeArtillery = 0;
        int32_t SensorProbes = 0;
        int32_t CameraDrones = 0;

        bool operator==(const MCFireSupport&) const = default;
    };

    /// <summary>An objective: its words, on one line, and its own time limit ("6:00"; empty with none).</summary>
    struct MCObjective
    {
        std::string Text;
        std::string TimeLimit;

        bool operator==(const MCObjective&) const = default;
    };

    /// <summary>A briefing's parts, each in the case the briefing writes it, its formatting codes left out.</summary>
    struct MCRead
    {
        /// <summary>The objectives in their order (their numbers are the map's pins').</summary>
        std::vector<MCObjective> Objectives;
        /// <summary>
        /// The mission's time limit ("15:00"), however the briefing writes it ("MISSION TIME LIMIT- 15:00" under the
        /// objectives, "TIME LIMIT: 25 MINUTES" or "***25 MINUTE TIME LIMIT***" after the fire support); empty with none.
        /// </summary>
        std::string TimeLimit;
        /// <summary>The fire support, when the briefing has its lines (a match's have none).</summary>
        std::optional<MCFireSupport> FireSupport;
        /// <summary>The orders, a paragraph each.</summary>
        std::vector<std::string> Orders;
        /// <summary>The warnings' head as written ("TACTICAL WARNING", "TACTICAL NOTICE"); empty with none.</summary>
        std::string WarningsHead;
        /// <summary>The warnings, one each (their stars left out).</summary>
        std::vector<std::string> Warnings;
    };

    /// <summary>The parts of <paramref name="text"/>, a briefing's text as its file holds it.</summary>
    MCRead Read(std::string_view text);

    /// <summary><paramref name="text"/> (a description, in MCSmuti's codes) as plain words, its lines run together.</summary>
    std::string Plain(std::string_view text);

    /// <summary>
    /// The fire support a line each: what is given, in the order the briefing lists it ("4 small artillery", "1 sensor
    /// probe"); "None" alone when it gives nothing.
    /// </summary>
    std::vector<std::string> FireSupportLines(const MCFireSupport& support);
}

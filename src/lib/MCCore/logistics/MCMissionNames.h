#pragma once

// Port-only (SPEC-menus.md, stage 5): the campaigns' operations and missions by the names the remade menus show them.
// A briefing's text names its mission in capitals on starred lines under its FEDCOM line ("FEDCOM:SKYHOOK/02PRDN03",
// "***DESTROY ENEMY CONVOY***"; STARCOM in two single missions); the app's data file (ui/missions.json) gives each the case the approved frames show
// ("Skyhook", "Destroy enemy convoy"), keyed by the briefing's file and the operation's word. One not in it shows as the
// briefing writes it. LOAD GAME and SAVE GAME name saves by them; the briefing and the loading screen will (stage 8).
// The same file names the match maps (CHOOSE MAP's, the room's): 1998's own room packets name several by their files'
// abbreviations ("CaptIsld", "HvrDmmt"), so the file gives each 1998 map a name to show by, keyed by its room packet's
// file. Only what the menus show changes: the packet's own name (LongMissionName) is what the room sends and the games
// service lists, and its file the match's identity. A map not in it (a player's, a mod's) shows by its own name.

namespace MCMissionNames
{
    /// <summary>What a briefing's text calls its operation and mission, as it writes them; empty for what it lacks.</summary>
    struct MCWritten
    {
        /// <summary>The operation's word on the FEDCOM line ("SKYHOOK").</summary>
        std::string Operation;
        /// <summary>The mission's starred title lines after it, joined by spaces ("DESTROY CLAN HEADQUARTERS BUILDING").</summary>
        std::string Mission;
    };

    /// <summary>
    /// The names <paramref name="text"/>, a briefing's text (its formatting codes and all), gives: its title the starred
    /// lines under its dispatch mark, or, with none (a match's side briefing), those it opens with.
    /// </summary>
    MCWritten FromBriefing(std::string_view text);

    /// <summary>
    /// The names <paramref name="text"/> gives, its title read under its dispatch mark (<paramref name="marked"/>) or
    /// from its start.
    /// </summary>
    MCWritten FromBriefing(std::string_view text, bool marked);

    /// <summary>
    /// The data file's names read from <paramref name="file"/> (a JSON object with "operations", "missions" and
    /// "maps"), in place of any read before; why not when it can't be read (no names then: every name as written).
    /// </summary>
    std::expected<void, std::string> Load(const std::filesystem::path& file);

    /// <summary>The app's data file (ui/missions.json beside the program, or in the Mac app's resources).</summary>
    std::filesystem::path AppFile();

    /// <summary>
    /// The mission whose briefing is <paramref name="briefingFile"/> (its name in the mission's FIT, case ignored), by
    /// the menus' name for it; else as written, <paramref name="written"/>. The app's file is read on first use.
    /// </summary>
    std::string Mission(std::string_view briefingFile, std::string_view written);

    /// <summary>The operation the briefings call <paramref name="written"/>, by the menus' name for it; else as written.</summary>
    std::string Operation(std::string_view written);

    /// <summary>
    /// The match map whose room packet is <paramref name="file"/> (its name, with or without its <c>.mpk</c>, case
    /// ignored), by the menus' name for it; else by its own name, <paramref name="written"/> (its LongMissionName).
    /// </summary>
    std::string Map(std::string_view file, std::string_view written);

    /// <summary><paramref name="text"/> in lower case (ASCII), as files' names are matched, case ignored.</summary>
    std::string Lowered(std::string_view text);
}

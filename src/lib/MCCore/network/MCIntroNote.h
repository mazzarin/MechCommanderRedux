#pragma once

// Port-only (SPEC-menus.md's "The intro movies"): the intro note, restart.cfg in the user folder. The intro movies play
// at a start, but not when the game starts itself again: UPDATE's RESTART (Sparkle's relaunch on the Mac, the new copy
// on Windows) and the content's RESTART leave the note as they quit, with the time; a start that finds one under two
// minutes old takes it (the note goes) and opens on the main menu. A rejoin after an update starts in its room, with no
// movies, as a lobby's start does; a note too old goes unread.

namespace MCIntroNote
{
    /// <summary>The note's file, in the user folder.</summary>
    inline constexpr std::string_view FileName = "restart.cfg";

    /// <summary>How long a note stays good: the restart starts the game again well within it.</summary>
    inline constexpr std::chrono::seconds Life{120};

    /// <summary>Writes the note in <paramref name="userFolder"/> at <paramref name="now"/> (seconds since 1970), over any before.</summary>
    /// <returns>Nothing, or why it couldn't be written.</returns>
    std::expected<void, std::string> Write(const std::filesystem::path& userFolder, int64_t now);

    /// <summary>
    /// A start: whether <paramref name="userFolder"/> holds a note written within <see cref="Life"/> of
    /// <paramref name="now"/> (seconds since 1970). Any note found goes.
    /// </summary>
    bool TakeAtStart(const std::filesystem::path& userFolder, int64_t now);

    /// <summary>Set at the start when a note was taken: the intro movies are skipped once (MCMission).</summary>
    extern bool SkipIntro;
}

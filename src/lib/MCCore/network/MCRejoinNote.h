#pragma once

struct MCLobbyLaunchArgs;

// Port-only (the personal build): the rejoin note, rejoin.cfg in the user folder (SPEC-install-and-updates.md's
// "Players who come from a multiplayer lobby"). A game a lobby started (-host or -join) that restarts into an update
// writes it as it quits: the lobby's words it was started with, when, and the build it expects to start. Sparkle starts
// the new version with no command line, so a start with none that finds a note under two minutes old naming its own
// build takes it (the note goes) and joins or hosts the room again as it says; a note too old goes unread, and one
// naming another build is left for that build.

namespace MCRejoinNote
{
    /// <summary>The note's file, in the user folder.</summary>
    inline constexpr std::string_view FileName = "rejoin.cfg";

    /// <summary>How long a note stays good: the update installs and the new version starts well within it.</summary>
    inline constexpr std::chrono::seconds Life{120};

    /// <summary>The switch a note's command line carries: the start is a rejoin, which says why it fails.</summary>
    inline constexpr std::string_view RejoinSwitch = "-rejoin";

    /// <summary>What a note holds.</summary>
    struct MCNote
    {
        /// <summary>The words to start with (<see cref="LobbyCommandLine"/>).</summary>
        std::string CommandLine;
        /// <summary>The build the note is for: the one the update brings.</summary>
        uint64_t Build = 0;
        /// <summary>When it was written, in seconds since 1970.</summary>
        int64_t Time = 0;
    };

    /// <summary>
    /// The command line the lobby started the game with (<paramref name="args"/>'), whole, so the restart starts as the
    /// lobby started it (its match's options, slots and teams too), with <see cref="RejoinSwitch"/> once.
    /// </summary>
    std::string LobbyCommandLine(const MCLobbyLaunchArgs& args);

    /// <summary>Writes <paramref name="note"/> in <paramref name="userFolder"/>, over any before.</summary>
    /// <returns>Nothing, or why it couldn't be written.</returns>
    std::expected<void, std::string> Write(const std::filesystem::path& userFolder, const MCNote& note);

    /// <summary>
    /// A start with no command line: the note's command line when <paramref name="userFolder"/> holds one written
    /// within <see cref="Life"/> of <paramref name="now"/> (seconds since 1970) for build <paramref name="build"/>,
    /// which is deleted; a note too old (or unreadable) is deleted and one for another build left, both unread.
    /// </summary>
    std::optional<std::string> TakeAtStart(const std::filesystem::path& userFolder, uint64_t build, int64_t now);

    /// <summary>
    /// A lobby's start with its own words (an update with no program restarts the game as it was started): any note in
    /// <paramref name="userFolder"/> was for it, and goes.
    /// </summary>
    void Discard(const std::filesystem::path& userFolder);

    /// <summary>RESTART is off (network/MCLobbyUpdate.h): the note in <paramref name="userFolder"/> goes, unread.</summary>
    void Withdraw(const std::filesystem::path& userFolder);

    /// <summary>Now, in seconds since 1970 (the system's clock, as the note keeps it).</summary>
    int64_t Now();
}

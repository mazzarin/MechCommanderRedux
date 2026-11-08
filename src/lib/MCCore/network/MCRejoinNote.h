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

    /// <summary>
    /// Port (Redux HD): an internet game's switch for a public game: a host's listed, its links relayed; a guest's links
    /// relayed (gui/MCGuiStartup's -public).
    /// </summary>
    inline constexpr std::string_view PublicSwitch = "-public";

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
    /// The lobby's words the game was started with (<paramref name="args"/>'), so the restart starts as the lobby started
    /// it (its match's options, slots and teams too), with <see cref="RejoinSwitch"/> once; no other word
    /// (gui/MCGuiStartup.h's LobbyWords).
    /// </summary>
    std::string LobbyCommandLine(const MCLobbyLaunchArgs& args);

    /// <summary>Port (Redux HD): an internet game's room the menus opened, as a restart into an update comes back to it.</summary>
    struct MCInternetRoom
    {
        bool Hosting = false;
        /// <summary>The game's code; a host's place's token too.</summary>
        std::string Code;
        std::string Token;
        /// <summary>A host's game is public (listed, its links relayed); a guest's is, as it knew it (relays only).</summary>
        bool Public = false;
        std::string PlayerName;
        std::string SessionName;
        int32_t MaxPlayers = 0;
        /// <summary>The start comes back to a room it was in (a join that fails says the room has gone).</summary>
        bool Rejoin = true;
    };

    /// <summary>
    /// Port (Redux HD): the words a restart starts with to come back to <paramref name="room"/>: a host's game back in
    /// its place (-place), a guest joining it by its code, either the menus' again once in (-menus), with
    /// <see cref="RejoinSwitch"/>; a public game's with <see cref="PublicSwitch"/>, a guest's too (relays only).
    /// </summary>
    std::string InternetRoomCommandLine(const MCInternetRoom& room);

    /// <summary>Writes <paramref name="note"/> in <paramref name="userFolder"/>, over any before.</summary>
    /// <returns>Nothing, or why it couldn't be written.</returns>
    std::expected<void, std::string> Write(const std::filesystem::path& userFolder, const MCNote& note);

    /// <summary>
    /// A start with no command line: the lobby's words in the note's command line (LobbyWords: any other is left out)
    /// when <paramref name="userFolder"/> holds one written within <see cref="Life"/> of <paramref name="now"/> (seconds
    /// since 1970) for build <paramref name="build"/>, which is deleted; a note too old (or unreadable, or without a
    /// lobby's switch) is deleted and one for another build left, both unread.
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

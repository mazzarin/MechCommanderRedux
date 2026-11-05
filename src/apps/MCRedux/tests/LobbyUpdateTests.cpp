#include "stdafx.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "gui/MCGuiStartup.h"
#include "linkup/MCSessionManager.h"
#include "network/MCRejoinNote.h"

// Port-only (the personal build): updates for players a multiplayer lobby starts (SPEC-install-and-updates.md's
// "Players who come from a multiplayer lobby"): the rejoin note a lobby's game leaves as it restarts into an update.

namespace
{
    constexpr std::string_view ScratchGroup = "mc_tests_lobby_updates";
    constexpr uint64_t NoteBuild = 600;
    constexpr int64_t WrittenAt = 1'800'000'000;

    /// <summary>The note's file in <paramref name="folder"/>.</summary>
    std::filesystem::path NotePath(const MCScratchFolder& folder)
    {
        return folder.Path() / MCRejoinNote::FileName;
    }

    /// <summary>Writes a note for <paramref name="build"/> at <paramref name="time"/> holding <paramref name="line"/>.</summary>
    void WriteNote(const MCScratchFolder& folder, uint64_t build, int64_t time, std::string_view line)
    {
        REQUIRE(MCRejoinNote::Write(folder.Path(), {.CommandLine = std::string(line), .Build = build, .Time = time}));
    }
}

TEST_CASE("lobby updates: the rejoin note holds the words the lobby started the game with")
{
    MCLobbyLaunchArgs host{.Active = true, .IsHost = true, .Address = ":28800", .PlayerName = "Big Bob"};
    CHECK_EQ(MCRejoinNote::LobbyCommandLine(host),
             std::string("-host -port 28800 -player \"Big Bob\" -maxplayers 6 -rejoin"));

    MCLobbyLaunchArgs guest{.Active = true,
                            .IsHost = false,
                            .Address = "192.168.1.5:28801",
                            .PlayerName = "Ann \"Ace\" Lee",
                            .SessionName = "Ann's room",
                            .MaxPlayers = 4};
    const std::string line = MCRejoinNote::LobbyCommandLine(guest);
    CHECK_EQ(
        line,
        std::string("-join 192.168.1.5:28801 -player \"Ann Ace Lee\" -session \"Ann's room\" -maxplayers 4 -rejoin"));
    CHECK(StartsInLobby(line));

    // A join with no address (the game's own default), and a host on the default port.
    CHECK_EQ(MCRejoinNote::LobbyCommandLine(MCLobbyLaunchArgs{.Active = true}),
             std::string("-join -maxplayers 6 -rejoin"));
    CHECK_EQ(MCRejoinNote::LobbyCommandLine(MCLobbyLaunchArgs{.Active = true, .IsHost = true}),
             std::string("-host -maxplayers 6 -rejoin"));
}

TEST_CASE("lobby updates: a start with no command line takes a fresh rejoin note for its build, once")
{
    const MCScratchFolder folder(ScratchGroup, "fresh");
    constexpr std::string_view Line = "-join 10.0.0.2 -player ANN -maxplayers 6 -rejoin";
    WriteNote(folder, NoteBuild, WrittenAt, Line);

    // Within two minutes, either way of the clock.
    const std::optional<std::string> taken = MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt + 119);
    REQUIRE(taken.has_value());
    CHECK_EQ(*taken, std::string(Line));
    CHECK(!std::filesystem::exists(NotePath(folder)));
    CHECK(!MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt + 1).has_value());

    WriteNote(folder, NoteBuild, WrittenAt, Line);
    CHECK(MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt - 30).has_value());
}

TEST_CASE("lobby updates: a rejoin note too old, or for another build, is ignored")
{
    const MCScratchFolder folder(ScratchGroup, "ignored");
    constexpr std::string_view Line = "-host -port 28800 -player BOB -maxplayers 6 -rejoin";

    // Two minutes old: ignored, and gone.
    WriteNote(folder, NoteBuild, WrittenAt, Line);
    CHECK(!MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt + 120).has_value());
    CHECK(!std::filesystem::exists(NotePath(folder)));
    WriteNote(folder, NoteBuild, WrittenAt, Line);
    CHECK(!MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt - 121).has_value());
    CHECK(!std::filesystem::exists(NotePath(folder)));

    // For another build (the update's, while this one starts): ignored, and left for that build.
    WriteNote(folder, NoteBuild + 1, WrittenAt, Line);
    CHECK(!MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt + 5).has_value());
    CHECK(std::filesystem::exists(NotePath(folder)));
    CHECK(MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild + 1, WrittenAt + 6).has_value());

    // Not a note, or not a lobby's words: gone, unread.
    {
        std::ofstream file(NotePath(folder), std::ios::binary);
        file << "{\"commandLine\": 5}";
    }

    CHECK(!MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt).has_value());
    CHECK(!std::filesystem::exists(NotePath(folder)));
    WriteNote(folder, NoteBuild, WrittenAt, "-mission 3");
    CHECK(!MCRejoinNote::TakeAtStart(folder.Path(), NoteBuild, WrittenAt).has_value());
    CHECK(!std::filesystem::exists(NotePath(folder)));
}

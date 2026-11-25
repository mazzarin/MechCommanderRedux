#pragma once

// Port-only (SPEC-menus.md's stage 4, CHOOSE MAP): the maps a room can play, the game's room packets
// (data/savegame/*.mpk, and the user folder's): each one's name, its file, the points each team starts with, its time
// limit and its description, read as the room reads the one it loads (MCRoomModel::LoadMission).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// <summary>A room packet, as CHOOSE MAP lists it.</summary>
struct MCMatchMap
{
    /// <summary>Its file's name without the extension, as the room loads it ("CPTRBASE").</summary>
    std::string File;
    /// <summary>Its name (LongMissionName: "Capture Base").</summary>
    std::string Name;
    /// <summary>Its map's file (MapFileName), whose briefing picture is its picture.</summary>
    std::string MapFile;
    int32_t Team1Points = 0;
    int32_t Team2Points = 0;
    /// <summary>Its time limit in seconds (MissionTime); none for none.</summary>
    std::optional<uint32_t> Seconds;
    /// <summary>Its description's lines (packet 1), an empty line between paragraphs.</summary>
    std::vector<std::string> Description;
    /// <summary>
    /// Whether it is one of 1998's NA maps, its plain map with the artillery crates' scripting taken out, as its
    /// description says (<see cref="NoArtilleryLine"/>): the match options' ARTILLERY OFF and PROBES OFF stand in.
    /// </summary>
    bool NoArtillery = false;
};

/// <summary>The line an NA map's description has (CPTBASNA's, FORTNA's and the six others').</summary>
inline constexpr std::string_view NoArtilleryLine = "**NO FUNCTIONING ARTILLERY CRATES**";

/// <summary>Room packet <paramref name="file"/> (its name without the extension) read; none when it can't be.</summary>
std::optional<MCMatchMap> MCReadMatchMap(std::string_view file);

/// <summary>Every room packet the game finds that can be read (the NA maps too), by name, then file.</summary>
std::vector<MCMatchMap> MCMatchMaps();

/// <summary>Points as the menus write them, grouped by thousands: "300,000".</summary>
std::string MCGroupedPoints(int32_t points);

/// <summary>
/// How a row of a description shows in the room and CHOOSE MAP (SPEC-menus.md, the room): its words wrapped, or one of
/// 1998's asterisk banners as the menus' own. A banner's row starts or ends with an asterisk.
/// </summary>
enum class MCDescriptionKind
{
    /// <summary>Words wrapped to the box (a paragraph).</summary>
    Text,
    /// <summary>A row of words between asterisks ("*** LOCKED MISSION ***"): its words, a label between two thin rules.</summary>
    Label,
    /// <summary>A row of asterisks alone: a thin rule.</summary>
    Rule,
    /// <summary>Any other banner's row ("**NOTE** read this"): one line, cut at the box's edge.</summary>
    Cut,
};

/// <summary>A row of a description as it shows: its kind and its words (a label's without its asterisks; a rule none).</summary>
struct MCDescriptionRow
{
    MCDescriptionKind Kind = MCDescriptionKind::Text;
    std::string Text;

    bool operator==(const MCDescriptionRow&) const = default;
};

/// <summary>The row <paramref name="line"/> of a description makes alone: a banner's, or its words.</summary>
MCDescriptionRow MCDescriptionRowOf(std::string_view line);

/// <summary>
/// A description's lines (<see cref="MCMatchMap::Description"/>) as they show: its paragraphs (an empty line between
/// two, a paragraph's lines joined by a space), each banner a row of its own between them; with
/// <paramref name="joined"/>, the paragraphs between two banners one (the room's).
/// </summary>
std::vector<MCDescriptionRow> MCDescriptionRows(const std::vector<std::string>& lines, bool joined);

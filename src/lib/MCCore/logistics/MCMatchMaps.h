#pragma once

// Port-only (SPEC-menus.md's stage 4, CHOOSE MAP): the maps a room can play, the game's room packets
// (data/savegame/*.mpk, and the user folder's): each one's name, its file, the points each team starts with, its time
// limit and its description, read as the room reads the one it loads (MCSessionScreen::LoadMission).

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
};

/// <summary>Room packet <paramref name="file"/> (its name without the extension) read; none when it can't be.</summary>
std::optional<MCMatchMap> MCReadMatchMap(std::string_view file);

/// <summary>Every room packet the game finds that can be read, by name, then file.</summary>
std::vector<MCMatchMap> MCMatchMaps();

/// <summary>Points as the menus write them, grouped by thousands: "300,000".</summary>
std::string MCGroupedPoints(int32_t points);

/// <summary>A description's lines (<see cref="MCMatchMap::Description"/>) put back into paragraphs.</summary>
std::vector<std::string> MCParagraphs(const std::vector<std::string>& lines);

#include "stdafx.h"
#include "logistics/MCMatchMaps.h"
#include "MCPort.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "platform/MCFileSystem.h"
#include "platform/MCServices.h"

namespace
{
    constexpr std::string_view MapExtension = ".mpk";
    /// <summary>The longest line the room reads of a description (the original's buffer).</summary>
    constexpr int32_t LongestLine = 0xfe;
    /// <summary>The packets: the room's facts, then its description.</summary>
    constexpr int32_t FactsPacket = 0;
    constexpr int32_t DescriptionPacket = 1;
    /// <summary>Digits in a group of points' thousands.</summary>
    constexpr size_t DigitsGrouped = 3;
    /// <summary>What a description's line may have round its words.</summary>
    constexpr std::string_view Blanks = " \t";

    /// <summary>Whether <paramref name="line"/> is <see cref="NoArtilleryLine"/>, whatever its capitals and blanks.</summary>
    bool SaysNoArtillery(std::string_view line)
    {
        const size_t first = line.find_first_not_of(Blanks);

        if (first == std::string_view::npos)
        {
            return false;
        }

        const std::string_view words = line.substr(first, line.find_last_not_of(Blanks) - first + 1);
        return MCPort::StrICmp(words, NoArtilleryLine) == 0;
    }
}

std::string MCGroupedPoints(int32_t points)
{
    std::string digits = std::to_string(std::max(points, 0));

    for (size_t at = digits.size(); at > DigitsGrouped; at -= DigitsGrouped)
    {
        digits.insert(at - DigitsGrouped, ",");
    }

    return digits;
}

std::optional<MCMatchMap> MCReadMatchMap(std::string_view file)
{
    const std::string path = GamePath(SavePath, file, MapExtension);
    MCPacketFile packets;
    MCFitIniFile facts;
    MCMatchMap map{.File = std::string(file)};

    if (packets.Open(path) != 0 || packets.SeekPacket(FactsPacket) != 0 ||
        facts.Open(packets, packets.GetPacketSize()) != 0 || facts.SeekBlock("Multiplayer") != 0)
    {
        SDL_Log("CHOOSE MAP: %s can't be read", path.c_str());
        return std::nullopt;
    }

    const MCFitResult<std::string> name = facts.Read<std::string>("LongMissionName");
    const MCFitResult<std::string> mapFile = facts.Read<std::string>("MapFileName");

    if (!name.has_value() || !mapFile.has_value())
    {
        SDL_Log("CHOOSE MAP: %s has no name or map", path.c_str());
        return std::nullopt;
    }

    map.Name = *name;
    map.MapFile = *mapFile;

    if (uint32_t seconds = 0; facts.ReadIdULong("MissionTime", seconds) == 0)
    {
        map.Seconds = seconds;
    }

    // Each team's points; an older file has one figure for both (as the room reads it).
    if (facts.SeekBlock("ResourcePoints") == 0)
    {
        uint32_t points = 0;
        uint32_t both = 0;
        const bool hasBoth = facts.ReadIdULong("numPoints", both) == 0;
        map.Team1Points = static_cast<int32_t>(facts.ReadIdULong("Team1Points", points) == 0 ? points
                                               : hasBoth                                     ? both
                                                                                             : 0);
        map.Team2Points = static_cast<int32_t>(facts.ReadIdULong("Team2Points", points) == 0 ? points
                                               : hasBoth                                     ? both
                                                                                             : 0);
    }

    facts.Close();
    MCFile description;

    if (packets.SeekPacket(DescriptionPacket) == 0 && description.Open(packets, packets.GetPacketSize()) == 0)
    {
        while (!description.Eof())
        {
            map.Description.push_back(description.ReadLine(LongestLine));
        }
    }

    map.NoArtillery = std::ranges::any_of(map.Description, SaysNoArtillery);
    return map;
}

std::vector<MCMatchMap> MCMatchMaps()
{
    std::vector<MCMatchMap> maps;

    for (const MCFoundFile& found : MCFileSystem::FindFileSources(GamePath(SavePath, "*", MapExtension)))
    {
        const std::string file = MCFileSystem::ToUtf8(MCFileSystem::FromUtf8(found.Name).stem());

        if (std::optional<MCMatchMap> map = MCReadMatchMap(file); map.has_value())
        {
            maps.push_back(std::move(*map));
        }
    }

    // By name as a player reads them, whatever their capitals ("Cache & Kill", "caldera", "CaptIsld").
    const auto folded = [](std::string text)
    {
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };

    std::ranges::sort(maps, {}, [&folded](const MCMatchMap& map) { return std::pair(folded(map.Name), map.File); });
    return maps;
}

std::vector<std::string> MCParagraphs(const std::vector<std::string>& lines)
{
    // An empty line between two paragraphs; a paragraph's lines joined by a space.
    std::vector<std::string> paragraphs;
    std::string paragraph;

    for (const std::string& line : lines)
    {
        if (line.empty())
        {
            if (!paragraph.empty())
            {
                paragraphs.push_back(std::exchange(paragraph, {}));
            }

            continue;
        }

        paragraph += paragraph.empty() ? line : " " + line;
    }

    if (!paragraph.empty())
    {
        paragraphs.push_back(std::move(paragraph));
    }

    return paragraphs;
}

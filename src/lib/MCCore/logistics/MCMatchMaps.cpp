#include "stdafx.h"
#include "logistics/MCMatchMaps.h"
#include "logistics/MCMissionNames.h"
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
    /// <summary>1998's banners' mark, and what lies round a banner's words (the marks and blanks).</summary>
    constexpr char Asterisk = '*';
    constexpr std::string_view BannerMarks = " \t*";

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

    map.Written = *name;
    map.Name = MCMissionNames::Map(file, map.Written);
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

    // By the name they show by, whatever its capitals ("Cache & Kill", "Capture Island", a mod's "deathmatch").
    const auto folded = [](std::string text)
    {
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };

    std::ranges::sort(maps, {}, [&folded](const MCMatchMap& map) { return std::pair(folded(map.Name), map.File); });
    return maps;
}

std::string MCShownMapName(std::string_view written, std::span<const MCMatchMap> maps)
{
    std::optional<std::string_view> shown;

    for (const MCMatchMap& map : maps)
    {
        if (map.Written != written)
        {
            continue;
        }

        if (shown.has_value() && *shown != map.Name)
        {
            return std::string(written);
        }

        shown = map.Name;
    }

    return std::string(shown.value_or(written));
}

MCDescriptionRow MCDescriptionRowOf(std::string_view line)
{
    const size_t first = line.find_first_not_of(Blanks);

    if (first == std::string_view::npos)
    {
        return MCDescriptionRow{.Text = std::string(line)};
    }

    const std::string_view row = line.substr(first, line.find_last_not_of(Blanks) - first + 1);

    if (row.front() != Asterisk && row.back() != Asterisk)
    {
        return MCDescriptionRow{.Text = std::string(line)};
    }

    const size_t from = row.find_first_not_of(BannerMarks);

    if (from == std::string_view::npos)
    {
        return MCDescriptionRow{.Kind = MCDescriptionKind::Rule};
    }

    // Words between asterisks, none among them: the label; anything else a banner's row whole.
    const std::string_view words = row.substr(from, row.find_last_not_of(BannerMarks) - from + 1);

    if (row.front() == Asterisk && row.back() == Asterisk && words.find(Asterisk) == std::string_view::npos)
    {
        return MCDescriptionRow{.Kind = MCDescriptionKind::Label, .Text = std::string(words)};
    }

    return MCDescriptionRow{.Kind = MCDescriptionKind::Cut, .Text = std::string(row)};
}

std::vector<MCDescriptionRow> MCDescriptionRows(const std::vector<std::string>& lines, bool joined)
{
    std::vector<MCDescriptionRow> rows;
    std::string paragraph;

    // A paragraph ends at an empty line and at a banner; joined, it carries on the words since the last banner.
    const auto endParagraph = [&rows, &paragraph, joined]
    {
        if (paragraph.empty())
        {
            return;
        }

        if (joined && !rows.empty() && rows.back().Kind == MCDescriptionKind::Text)
        {
            rows.back().Text += " " + std::exchange(paragraph, {});
            return;
        }

        rows.push_back(MCDescriptionRow{.Text = std::exchange(paragraph, {})});
    };

    for (const std::string& line : lines)
    {
        if (line.empty())
        {
            endParagraph();
            continue;
        }

        MCDescriptionRow row = MCDescriptionRowOf(line);

        if (row.Kind == MCDescriptionKind::Text)
        {
            paragraph += paragraph.empty() ? line : " " + line;
            continue;
        }

        endParagraph();
        rows.push_back(std::move(row));
    }

    endParagraph();
    return rows;
}

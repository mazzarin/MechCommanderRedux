#include "stdafx.h"
#include "MultiMap.h"
#include "DaoDatabase.h"
#include "EditorObject.h"
#include "MissionRecordSet.h"
#include "Objective.h"
#include "TileLibrary.h"
#include "resource.h"
#include "EditorFiles.h"

#include <fstream>
#include <io.h>

namespace
{
    /// <summary>The multiplayer mission's most turrets, drop zones and players.</summary>
    inline constexpr int32_t MULTI_MAX_TURRETS = 0x40;
    inline constexpr int32_t MULTI_MAX_DROP_ZONES = 6;
    inline constexpr int32_t MULTI_MAX_PLAYERS = 6;
    /// <summary>The drop zones each team needs.</summary>
    inline constexpr int32_t TEAM_DROP_ZONES = 3;
    /// <summary>The side of team 1's objectives and drop zones; anything else is team 2's.</summary>
    inline constexpr int32_t TEAM1_SIDE = 2;
    /// <summary>The side of the second objective a new map gets.</summary>
    inline constexpr int32_t TEAM2_SIDE = 1;
    /// <summary>The tile set string offset of the GTiles set (whose missions are on Garotte).</summary>
    inline constexpr int32_t GTILES_OFFSET = 0x32;
    /// <summary>The map side (in tiles, less one) from which a mission counts as large.</summary>
    inline constexpr int32_t LARGE_MAP_ROWS = 0x78;
    /// <summary>The longest line read from the save.</summary>
    inline constexpr std::streamsize SAVE_LINE_SIZE = 0x400;
    /// <summary>The longest description read by its packet's offsets; a longer one is read to the end of the file.</summary>
    inline constexpr long DESCRIPTION_LIMIT = 1000;

    /// <summary>
    /// The save's packet file header: a signature (replaced by the checksum) and the offsets of its four packets
    /// (an empty first one, the FIT text, the description and an empty last one).
    /// </summary>
    struct SaveHeader
    {
        uint32_t signature = 0xfeedface;
        uint32_t offsets[4] = {sizeof(SaveHeader), sizeof(SaveHeader), 0, 0};
    };

    static_assert(sizeof(SaveHeader) == 0x14);

    /// <summary>Where the description packet's offset is in the header.</summary>
    inline constexpr LONG DESCRIPTION_OFFSET = offsetof(SaveHeader, offsets) + 2 * sizeof(uint32_t);

    /// <summary>The game folder the editor works in (the original's Paths\DataPath).</summary>
    CString dataPath()
    {
        return EditorDataPath();
    }

    /// <summary>The number at the end of <paramref name="line"/>, after its last character that is neither a digit nor a space.</summary>
    long trailingNumber(const char* line)
    {
        auto i = static_cast<ptrdiff_t>(std::strlen(line)) - 1;

        while (i >= 0 && (std::isdigit(static_cast<unsigned char>(line[i])) != 0 ||
                          std::isspace(static_cast<unsigned char>(line[i])) != 0))
        {
            i--;
        }

        return std::atol(line + i + 1);
    }

    /// <summary>The two dispatch paths of mission <paramref name="name"/>: team 1's ("is") and team 2's ("c").</summary>
    std::pair<CString, CString> dispatchPaths(const CString& name)
    {
        const CString folder = dataPath() + "\\Data\\Missions\\";
        return {folder + "is" + (name + ".txt"), folder + "c" + (name + ".txt")};
    }
}

MultiMap::MultiMap(int32_t maxRow, int32_t maxCol) : EditorMap(maxRow, maxCol)
{
    auto* objective = new Objective(OBJECTIVE_DESTROY_ALL);
    objective->side = TEAM2_SIDE;
    addObjective(objective);
}

MissionSettings MultiMap::team1Settings() const
{
    return teams[0];
}

MissionSettings MultiMap::team2Settings() const
{
    return teams[1];
}

void MultiMap::setTeam1Settings(const MissionSettings& settings)
{
    teams[0] = settings;
}

void MultiMap::setTeam2Settings(const MissionSettings& settings)
{
    teams[1] = settings;
}

int32_t MultiMap::maxTurrets()
{
    return MULTI_MAX_TURRETS;
}

int32_t MultiMap::maxDropZones()
{
    return MULTI_MAX_DROP_ZONES;
}

int32_t MultiMap::maxPlayers()
{
    return MULTI_MAX_PLAYERS;
}

bool MultiMap::checkMission([[maybe_unused]] DaoDatabase& database, [[maybe_unused]] const CString& name)
{
    const auto refuse = [](UINT text, long team1, long team2)
    {
        CString message;
        message.Format(text, team1, team2);
        CString caption;
        caption.LoadString(IDS_MCEDITOR);
        MessageBoxA(nullptr, message, caption, MB_OK);
        return false;
    };

    long team1Objectives = 0;
    long team2Objectives = 0;

    for (const Objective* objective : objectives)
    {
        (objective->side == TEAM1_SIDE ? team1Objectives : team2Objectives)++;
    }

    if (team1Objectives == 0 || team2Objectives == 0)
    {
        return refuse(IDS_A_MULTIPLAYER_MISSION_REQUIRES, team1Objectives, team2Objectives);
    }

    long team1Zones = 0;
    long team2Zones = 0;

    for (const EditorObject* zone : dropZones)
    {
        (zone->side == TEAM1_SIDE ? team1Zones : team2Zones)++;
    }

    if (team1Zones < TEAM_DROP_ZONES || team2Zones < TEAM_DROP_ZONES)
    {
        return refuse(IDS_YOU_MUST_ASSIGN_THREE, team1Zones, team2Zones);
    }

    return checkMissionSize();
}

bool MultiMap::saveMission(DaoDatabase& database, CString& name)
{
    const auto [team1Path, team2Path] = dispatchPaths(name);
    const MissionSettings& team1 = teams[0];
    const MissionSettings& team2 = teams[1];
    missionText.write(team1Path, team1.artillerySmall, team1.artilleryLarge, team1.artillerySensor,
                      team1.artilleryCamera);
    clanText.write(team2Path, team2.artillerySmall, team2.artilleryLarge, team2.artillerySensor, team2.artilleryCamera);
    MissionRecordSet mission(&database);
    mission.Open(dbOpenDynaset, nullptr, 0);
    CString filter;
    filter.Format("[MissionName] = '%s'", static_cast<LPCTSTR>(name));

    if (!mission.FindFirst(filter))
    {
        mission.AddNew();
    }
    else
    {
        mission.Edit();
    }

    // Original behaviour: unlike the single-player save, the script file name isn't set, so a new mission's units
    // write their orders to Data\Missions\Warriors\.fit. Team 2's time and force limits aren't kept.
    mission.m_MissionFileName = name;
    mission.m_MissionName = name;
    mission.m_BriefingFileName = name + ".txt";
    mission.m_MapSize = maxRow >= LARGE_MAP_ROWS ? 1 : 0;
    mission.m_MissionDateTime = COleDateTime::GetCurrentTime();
    mission.m_Multiplayer = TRUE;
    mission.m_TerrainFileName = name;
    mission.m_ArtilleryCamera = team1.artilleryCamera;
    mission.m_ArtilleryLarge = team1.artilleryLarge;
    mission.m_ArtillerySensor = team1.artillerySensor;
    mission.m_ArtillerySmall = team1.artillerySmall;
    mission.m_PlayerForceLimit = team1.playerForceLimit;
    mission.m_ClanArtilleryCamera = team2.artilleryCamera;
    mission.m_TimeLimit = team1.timeLimit;
    mission.m_ClanArtillerySmall = team2.artillerySmall;
    mission.m_ClanArtilleryLarge = team2.artilleryLarge;
    mission.m_ClanArtillerySensor = team2.artillerySensor;
    mission.m_Planet = tileSetStringOffset == GTILES_OFFSET ? "Garotte" : "PortArthur";
    mission.m_TimeOfDay = static_cast<long>(timeOfDay);
    mission.m_Weather = weather;
    mission.Update();
    mission.FindLast(filter);
    const long missionId = mission.m_MissionID;
    // Original behaviour: whether the rows were written isn't looked at.
    saveUnits(database, missionId);
    writeDropZones(database, missionId);
    mission.Close();
    return writeSave(name, nullptr);
}

bool MultiMap::writeSave(const CString& name, const char* saveName)
{
    const CString path = dataPath();
    const std::optional<std::vector<char>> save = readTemplate(path + "\\Teditor\\MPK.txt");

    if (!save.has_value())
    {
        return false;
    }

    const CString gameName = saveName != nullptr ? CString(saveName) : name;
    const CString savePath = path + "\\Data\\Savegame\\" + gameName + ".mpk";
    FILE* file = std::fopen(savePath, "w+b");

    if (file == nullptr)
    {
        return false;
    }

    SaveHeader header;
    std::fwrite(&header, 1, sizeof(header), file);
    // EB-130: the planet's Setting is whether a function's address is set, not its result, so always 1 (Garotte).
    constexpr long PLANET_SETTING = 1;
    const MissionSettings& team1 = teams[0];
    std::fprintf(file, save->data(), static_cast<LPCTSTR>(name), static_cast<LPCTSTR>(gameName),
                 static_cast<LPCTSTR>(name), static_cast<LPCTSTR>(name), static_cast<LPCTSTR>(gameName),
                 team1.timeLimit, team1.resourcePoints, team1.resourcePoints, teams[1].resourcePoints, PLANET_SETTING);
    std::fflush(file);

    // The description follows the text as the third packet.
    header.offsets[2] = static_cast<uint32_t>(_filelength(_fileno(file)));
    header.offsets[3] = header.offsets[2] + static_cast<uint32_t>(description.GetLength());
    {
        CStdioFile packets(file);
        packets.Seek(0, CFile::end);
        writeDescription(packets, description);
    }

    std::fseek(file, 0, SEEK_SET);
    std::fwrite(&header, 1, sizeof(header), file);
    std::fflush(file);
    std::fclose(file);

    // The signature becomes the sum of every byte after it.
    // EB-131 fixed: the original reads the file into a buffer twice the template's size, unchecked, and doesn't check
    // the file opened; the port reads it into a buffer of its size.
    CFile packetFile;

    if (!packetFile.Open(savePath, CFile::modeReadWrite | CFile::modeNoTruncate))
    {
        return true;
    }

    packetFile.Seek(sizeof(header.signature), CFile::begin);
    const auto length = static_cast<size_t>(packetFile.GetLength());
    std::vector<uint8_t> bytes(length);
    packetFile.Read(bytes.data(), static_cast<UINT>(length));
    uint32_t checksum = 0;

    for (size_t i = 0; i + sizeof(header.signature) < length; i++)
    {
        checksum += bytes[i];
    }

    packetFile.Seek(0, CFile::begin);
    packetFile.Write(&checksum, sizeof(checksum));
    return true;
}

bool MultiMap::loadMission(DaoDatabase& database, const CString& name)
{
    MissionRecordSet mission(&database);
    mission.Open(dbOpenDynaset, nullptr, 0);
    CString filter;
    filter.Format("[MissionName] = '%s'", static_cast<LPCTSTR>(name));

    if (!mission.FindFirst(filter))
    {
        return false;
    }

    const auto [team1Path, team2Path] = dispatchPaths(name);
    missionText.read(team1Path);
    clanText.read(team2Path);
    MissionSettings& team1 = teams[0];
    MissionSettings& team2 = teams[1];
    team1.artilleryCamera = mission.m_ArtilleryCamera;
    team1.artilleryLarge = mission.m_ArtilleryLarge;
    team1.artillerySensor = mission.m_ArtillerySensor;
    team1.artillerySmall = mission.m_ArtillerySmall;
    team1.playerForceLimit = mission.m_PlayerForceLimit;
    team1.timeLimit = mission.m_TimeLimit;
    team2.artilleryCamera = mission.m_ClanArtilleryCamera;
    team2.artilleryLarge = mission.m_ClanArtilleryLarge;
    team2.artillerySensor = mission.m_ClanArtillerySensor;
    team2.artillerySmall = mission.m_ClanArtillerySmall;
    timeOfDay = MissionTimeOfDayOf(mission.m_TimeOfDay);
    weather = MissionWeatherOf(mission.m_Weather).value;
    const long missionId = mission.m_MissionID;
    mission.Close();
    const CString savePath = dataPath() + "\\Data\\Savegame\\" + name + ".mpk";

    // The save's text, read as text lines from the start of the file (the header with them) up to FITend.
    {
        std::ifstream save(static_cast<LPCTSTR>(savePath));
        char line[SAVE_LINE_SIZE];

        while (!save.fail() && !save.bad())
        {
            save.getline(line, SAVE_LINE_SIZE);

            if (std::strstr(line, "Team1Points") != nullptr)
            {
                team1.resourcePoints = trailingNumber(line);
            }

            if (std::strstr(line, "Team2Points") != nullptr)
            {
                team2.resourcePoints = trailingNumber(line);
            }

            if (std::strstr(line, "MissionTime") != nullptr)
            {
                team1.timeLimit = trailingNumber(line);
            }

            if (std::strstr(line, "FITend") != nullptr)
            {
                break;
            }
        }
    }

    // The description: the third packet, by the offsets of the third and fourth.
    // EB-132 fixed: the original reads it whether or not the save opened (MFC throws from the first seek), and a third
    // packet past the file's end makes its length negative; the port leaves the description empty.
    CFile packets;

    if (packets.Open(savePath, CFile::modeRead))
    {
        packets.Seek(DESCRIPTION_OFFSET, CFile::begin);
        long start = 0;
        long end = 0;
        packets.Read(&start, sizeof(start));
        packets.Read(&end, sizeof(end));
        packets.Seek(start, CFile::begin);
        long length = end - start;

        if (length < 0 || length > DESCRIPTION_LIMIT)
        {
            length = static_cast<long>(packets.GetLength()) - start;
        }

        if (length >= 0)
        {
            std::vector<char> text(static_cast<size_t>(length) + 1);
            packets.Read(text.data(), static_cast<UINT>(length));
            description = text.data();
        }

        packets.Close();
    }

    // Each line break gets a carriage return before it (writeDescription takes a doubled one back out).
    for (int i = 0; i < description.GetLength(); i++)
    {
        if (description[i] == '\n')
        {
            description.Insert(i, '\r');
            i++;
        }
    }

    loadMissionData(database, missionId);
    loadDropZones(database, missionId);
    return true;
}

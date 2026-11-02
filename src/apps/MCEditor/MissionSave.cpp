#include "stdafx.h"
#include "EditorMap.h"
#include "ControlLink.h"
#include "DaoDatabase.h"
#include "DropZoneRecord.h"
#include "EditorObject.h"
#include "LookOutPtRecord.h"
#include "MapOverlay.h"
#include "MissionRecordSet.h"
#include "MoverObject.h"
#include "Objective.h"
#include "ObjectiveMainRecord.h"
#include "ObjectiveRecord.h"
#include "ObjectLibrary.h"
#include "Parameter.h"
#include "StructureLink.h"
#include "StructureRecord.h"
#include "TileLibrary.h"
#include "UnitBrain.h"
#include "UnitRecord.h"
#include "lib/packet.h"
#include "resource.h"
#include "EditorFiles.h"

#include <fstream>

// The mission's records and files (0x00458d50, 0x004598e0, 0x0045ab80, 0x0045b450, 0x00470a90, 0x00471990,
// 0x0047de60): the NewMissions row, the units with the warrior FIT, the structures and their links, the objectives,
// the drop zones, the campaign file and the save game.

namespace
{
    /// <summary>The filter of a mission's rows.</summary>
    const char* const MISSION_ID_FILTER = "[MissionID] = %ld";
    /// <summary>The filter of a structure's row.</summary>
    const char* const STRUCTURE_FILTER = "[MissionID] = %d AND [Block] = %d AND [Vertex] = %d AND [Number] = %d";
    /// <summary>A structure's identifier: its building type, block, vertex and number.</summary>
    const char* const STRUCTURE_IDENTIFIER = "%s_%03ld%03ld%03ld";
    /// <summary>The tile set string offset of the GTiles set (whose missions are on Garotte).</summary>
    inline constexpr int32_t GTILES_OFFSET = 0x32;
    /// <summary>The map side (in tiles, less one) from which a mission counts as large.</summary>
    inline constexpr int32_t LARGE_MAP_ROWS = 0x78;
    /// <summary>The sides, in the order their units are written.</summary>
    inline constexpr int32_t SIDE_PASSES = 3;
    inline constexpr int32_t FIRST_SIDE = 2;
    /// <summary>The force groups a side's units are put in, and the most units in one with no objective.</summary>
    inline constexpr int32_t FORCE_GROUPS = 12;
    inline constexpr int32_t GROUP_SIZE = 12;
    /// <summary>What a mech's variant adds to its type's profile for its MechID (variants 0, 1 and 2).</summary>
    inline constexpr long VARIANT_OFFSETS[] = {100, 400, 300};
    /// <summary>A unit's facing in degrees: 90 a frame, from 45.</summary>
    inline constexpr int32_t FRAME_DEGREES = 90;
    inline constexpr int32_t FIRST_FRAME_DEGREES = 45;
    /// <summary>A new drop zone's formation, spacing and number of slots.</summary>
    const char* const DROP_ZONE_FORMATION = "Diamond";
    inline constexpr long DROP_ZONE_SPACING = 1;
    inline constexpr long DROP_ZONE_SLOTS = 4;
    /// <summary>The name a structure whose class has none is identified by.</summary>
    const char* const UNLINKED_TURRET = "UnlinkedTurret";
    /// <summary>The save's working files, in the current folder, and the packet file's line for no file.</summary>
    const char* const SAVE_WORK_FILE = "sol.tmp";
    const char* const RESPONSE_FILE = "work.rsp";
    const char* const NULL_ENTRY = "<NULL>";
    /// <summary>The longest line of the response file.</summary>
    inline constexpr int32_t RESPONSE_LINE_SIZE = 0xfe;

    /// <summary>The game folder the editor works in (the original's Paths\DataPath).</summary>
    CString dataPath()
    {
        return EditorDataPath();
    }

    /// <summary>
    /// Builds packet file <paramref name="packetName"/> from the files <paramref name="responseName"/> lists, a line
    /// each (the game's makepak): each file's bytes, LZ packed, as the packet of its line; a blank line or "<NULL>"
    /// leaves its packet empty.
    /// </summary>
    /// <remarks>
    /// 0x0047de60. EB-119 fixed: when the list doesn't open or the packet file can't be made, the original prints a
    /// fatal error to the (missing) console and exits the editor; the port writes nothing.
    /// </remarks>
    void buildPacketFile(const char* responseName, const char* packetName)
    {
        File response;

        if (response.open(responseName, READ, 50) != 0)
        {
            return;
        }

        PacketFile packets;

        if (packets.create(packetName) != 0)
        {
            response.close();
            return;
        }

        const uint32_t lines = response.getNumLines();
        packets.reserve(static_cast<int32_t>(lines), 1);

        for (uint32_t i = 0; i < lines; i++)
        {
            char line[0x100] = {};
            response.readLine(reinterpret_cast<uint8_t*>(line), RESPONSE_LINE_SIZE);

            if (line[0] == '\0' || std::strncmp(line, NULL_ENTRY, std::strlen(NULL_ENTRY)) == 0)
            {
                continue;
            }

            File source;

            if (source.open(line, READ, 50) != 0)
            {
                continue;
            }

            const uint32_t size = source.fileSize();

            if (size == 0)
            {
                continue;
            }

            std::vector<uint8_t> data(size);
            source.read(data.data(), static_cast<int32_t>(size));
            packets.writePacket(static_cast<int32_t>(i), data.data(), static_cast<int32_t>(size), STORAGE_TYPE_LZD);
        }

        packets.close();
        response.close();
    }

    /// <summary>
    /// Reports a database error while writing a structure (string 0xe012) and deletes it.
    /// </summary>
    void reportStructureError(DaoException* e)
    {
        CString format;
        format.LoadString(IDS_COULDNT_ADD_THE_STRUCTURE);
        CString text;
        text.Format(format, static_cast<LPCTSTR>(e->m_strDescription));
        CString caption;
        caption.LoadString(IDS_MCEDITOR);
        MessageBoxA(nullptr, text, caption, MB_OK);
        e->Delete();
    }

    /// <summary>Deletes the rows of <paramref name="records"/> that <paramref name="filter"/> finds.</summary>
    void deleteRows(DaoRecordset& records, const CString& filter)
    {
        records.Open(dbOpenDynaset, nullptr, 0);

        for (BOOL found = records.FindFirst(filter); found; found = records.FindNext(filter))
        {
            records.Delete();
        }
    }
}

std::optional<std::vector<char>> EditorMap::readTemplate(const CString& path)
{
    CFile file;

    if (!file.Open(path, CFile::modeRead))
    {
        return std::nullopt;
    }

    const auto length = static_cast<size_t>(file.GetLength());
    std::vector<char> text(length + 1);
    file.Read(text.data(), static_cast<UINT>(length));
    file.Close();
    return text;
}

bool EditorMap::saveUnits(DaoDatabase& database, long missionId)
{
    try
    {
        CString filter;
        filter.Format(MISSION_ID_FILTER, missionId);
        StructureRecord structures(&database);
        deleteRows(structures, filter);
        LookOutPtRecord lookouts(&database);
        deleteRows(lookouts, filter);
        // The objectives' rows go, and the recordset stays open for the new ones.
        ObjectiveMainRecord mainRows(&database);
        deleteRows(mainRows, filter);
        ObjectiveRecord scripting(&database);
        deleteRows(scripting, filter);
        UnitRecord units(&database);
        deleteRows(units, filter);
        const bool unitsWritten = writeUnits(database, missionId);
        bool written = writeStructures(database, missionId) && unitsWritten;
        guardWarningShown = false;
        long number = 0;

        for (Objective* objective : objectives)
        {
            if (objective->type == -1)
            {
                continue;
            }

            // An objective with a value set gets its Objectives row and its script.
            for (Parameter* parameter : objective->parameters)
            {
                if (parameter == nullptr || !parameter->isSet())
                {
                    continue;
                }

                mainRows.AddNew();
                mainRows.m_TimeLeft = -1.0f;
                mainRows.m_Priority = objective->primary ? 0 : 1;
                mainRows.m_DoNotFailIfDead = FALSE;
                mainRows.m_Alignment = objective->side;
                mainRows.m_MissionID = missionId;
                mainRows.m_Objective = number;
                mainRows.m_Description = objective->name;
                mainRows.Update();
                mainRows.MoveLast();
                written = objective->writeScripting(database, missionId, mainRows.m_ObjectiveID, number, *this,
                                                    objective->afterLastAction != 0) &&
                          written;
                number++;
                break;
            }
        }

        mainRows.Close();
        return written;
    }
    catch (DaoException* e)
    {
        e->Delete();
        return false;
    }
}

bool EditorMap::writeUnits(DaoDatabase& database, long missionId)
{
    // The warrior FIT is named after the mission's script.
    MissionRecordSet mission(&database);
    mission.Open(dbOpenDynaset, nullptr, 0);
    CString filter;
    filter.Format(MISSION_ID_FILTER, missionId);
    mission.FindFirst(filter);
    CString warriorPath = dataPath() + "\\Data\\Missions\\Warriors\\";
    warriorPath += mission.m_ScriptFileName;
    warriorPath += ".fit";
    std::ofstream warriors(static_cast<LPCTSTR>(warriorPath));
    warriors << "FITini\n";

    // Each side's units get force groups: those an objective refers to share its group (the first free one), the
    // others fill the first group no objective has, up to 12 units. The slots are an objective and a count.
    for (int32_t pass = 0, side = FIRST_SIDE; pass < SIDE_PASSES; pass++, side--)
    {
        std::pair<Objective*, int32_t> groups[FORCE_GROUPS] = {};

        for (EditorObject* object : movers)
        {
            auto& mover = static_cast<MoverObject&>(*object);

            if (mover.side != side)
            {
                continue;
            }

            bool grouped = false;

            // Original behaviour: a unit several objectives refer to is grouped by each, the last one winning, and
            // counts in every group.
            for (Objective* objective : objectives)
            {
                if (!objective->referencesObject(mover, *this))
                {
                    continue;
                }

                for (int32_t group = 0; group < FORCE_GROUPS; group++)
                {
                    auto& [groupObjective, count] = groups[group];

                    if (count == 0)
                    {
                        groupObjective = objective;
                    }
                    else if (groupObjective == nullptr || groupObjective != objective)
                    {
                        continue;
                    }

                    mover.forceGroup = group;
                    mover.role = count++;
                    grouped = true;
                    break;
                }
            }

            if (grouped)
            {
                continue;
            }

            for (int32_t group = 0; group < FORCE_GROUPS; group++)
            {
                auto& [groupObjective, count] = groups[group];

                if (groupObjective == nullptr && count < GROUP_SIZE)
                {
                    mover.forceGroup = group;
                    mover.role = count++;
                    break;
                }
            }
        }
    }

    // The rows and the orders, side by side; the warriors are numbered from 1 in that order.
    int32_t warrior = 0;

    for (int32_t pass = 0, side = FIRST_SIDE; pass < SIDE_PASSES; pass++, side--)
    {
        for (EditorObject* object : movers)
        {
            auto& mover = static_cast<MoverObject&>(*object);

            if (mover.side != side)
            {
                continue;
            }

            // The type's profile, and for a mech its variant's.
            const ObjectType& type = objectLibrary->moverGroups[objectIdGroup(mover.id)].types[objectIdType(mover.id)];
            long profile = type.profile;
            const bool isMech = objectIdClass(mover.id) == OBJECT_CLASS_MECH;

            if (isMech)
            {
                const int32_t variant = static_cast<MechObject&>(mover).variant;

                if (variant >= 0 && variant < static_cast<int32_t>(std::size(VARIANT_OFFSETS)))
                {
                    profile += VARIANT_OFFSETS[variant];
                }
            }

            warrior++;
            UnitRecord unit(&database);
            unit.Open(dbOpenDynaset, nullptr, 0);
            unit.AddNew();
            unit.m_ActivationDelay = 0.0;
            unit.m_ActivationRadius = 0.0;
            unit.m_Active = TRUE;
            unit.m_Alignment = mover.side == 1 ? "Clan" : "Other";
            unit.m_ForceGroup = mover.forceGroup + 1;
            unit.m_Exists = TRUE;
            unit.m_Icon = 0.0;

            if (isMech)
            {
                unit.m_MechID = profile;
            }
            else
            {
                unit.m_VehicleID = profile;
            }

            unit.m_MissionID = missionId;
            unit.m_Role = mover.role;
            int32_t x = 0;
            int32_t y = 0;
            cellToWorld(mover.cellCol, mover.cellRow, &x, &y);
            unit.m_XCoordinate = x;
            unit.m_YCoordinate = y;
            unit.m_MWID = mover.findPilotId(database);
            unit.m_Rotation = static_cast<int32_t>(objectIdFrame(mover.id)) * FRAME_DEGREES + FIRST_FRAME_DEGREES;
            unit.m_BrainFile = mover.brain != nullptr ? mover.brain->brainName() : "";

            // Original behaviour: the path of the brain's ABL file (the data path, the brain, ".abl") is built and not
            // used.
            if (mover.brain != nullptr)
            {
                mover.brain->write(warriors, *this, warrior);
            }

            unit.Update();
        }
    }

    warriors << "FITend\n";
    return true;
}

bool EditorMap::writeDropZones(DaoDatabase& database, long missionId)
{
    DropZoneRecord zones(&database);
    CString filter;
    filter.Format(MISSION_ID_FILTER, missionId);
    deleteRows(zones, filter);

    // The other side's zones, then the player's.
    for (const int32_t side : {2, 1})
    {
        for (EditorObject* zone : dropZones)
        {
            if (zone->side != side)
            {
                continue;
            }

            zones.AddNew();
            int32_t x = 0;
            int32_t y = 0;
            cellToWorld(zone->cellCol, zone->cellRow, &x, &y);
            zones.m_PlayerEntryPointX = x;
            zones.m_PlayerEntryPointY = y;
            zones.m_Formation = DROP_ZONE_FORMATION;
            zones.m_Slots = DROP_ZONE_SLOTS;
            zones.m_MissionID = missionId;
            zones.m_Rotation = 0;
            zones.m_Spacing = DROP_ZONE_SPACING;
            zones.Update();
        }
    }

    zones.Close();
    return true;
}

bool EditorMap::writeStructureRow(StructureRecord& structures, long missionId, const EditorObject& object,
                                  int32_t objectClass)
{
    int32_t block = 0;
    int32_t vertex = 0;
    int32_t number = 0;
    structureIndex(object, &block, &vertex, &number);
    CString filter;
    filter.Format(STRUCTURE_FILTER, missionId, block, vertex, number);

    if (structures.FindFirst(filter))
    {
        structures.Edit();
    }
    else
    {
        structures.AddNew();
    }

    structures.m_Block = block;
    structures.m_Vertex = vertex;
    structures.m_Number = number;
    structures.m_BuildingType = ObjectLibrary::className(objectClass);
    // A class with no name (a turret) is identified as an unlinked turret.
    const CString name = structures.m_BuildingType.IsEmpty() ? CString(UNLINKED_TURRET) : structures.m_BuildingType;
    CString identifier;
    identifier.Format(STRUCTURE_IDENTIFIER, static_cast<LPCTSTR>(name), static_cast<long>(block),
                      static_cast<long>(vertex), static_cast<long>(number));
    identifier.Replace(' ', '_');
    structures.m_Identifier = identifier;
    structures.m_InitialDamage = 0;
    structures.m_Alignment = object.side;
    structures.m_Captured = FALSE;
    structures.m_MissionID = missionId;
    structures.m_Captureable =
        objectClass != OBJECT_CLASS_POWER_STATION && objectClass != OBJECT_CLASS_PERIMETER_ALARM ? TRUE : FALSE;
    structures.Update();
    return true;
}

void EditorMap::addStructureLink(DaoDatabase& database, const EditorObject& structure, long controllerId)
{
    StructureLink link(&database);
    link.Open(dbOpenDynaset, nullptr, 0);
    link.AddNew();
    int32_t block = 0;
    int32_t vertex = 0;
    int32_t number = 0;
    structureIndex(structure, &block, &vertex, &number);
    link.m_Block = block;
    link.m_Vertex = vertex;
    link.m_Number = number;
    link.m_StructureID = controllerId;
    link.m_Inactive = FALSE;
    link.Update();
}

bool EditorMap::writeStructures(DaoDatabase& database, long missionId)
{
    try
    {
        // The controllers' rows and their links.
        for (ControlLink* link : controlLinks)
        {
            POINT cell;
            link->getCell(&cell);
            const EditorObject* controller = objectCoveringCell(cell.x, cell.y);

            if (controller == nullptr)
            {
                continue;
            }

            const int32_t controllerClass = objectIdClass(controller->id);

            if (controllerClass == 0 || !ControlLink::isControllerClass(controllerClass))
            {
                continue;
            }

            StructureRecord structures(&database);
            structures.Open(dbOpenDynaset, nullptr, 0);
            writeStructureRow(structures, missionId, *controller, controllerClass);
            structures.MoveLast();
            const long controllerId = structures.m_StructureID;
            std::vector<POINT> cells(static_cast<size_t>(link->linkedCount()));
            link->getLinkedCells(cells.data(), static_cast<int32_t>(cells.size()));

            for (const POINT& linkedCell : cells)
            {
                const EditorObject* linked = objectCoveringCell(linkedCell.x, linkedCell.y);

                if (linked == nullptr || !ControlLink::controls(controllerClass, objectIdClass(linked->id)))
                {
                    continue;
                }

                addStructureLink(database, *linked, controllerId);

                if (controllerClass != OBJECT_CLASS_POWER_STATION)
                {
                    continue;
                }

                // A power station also controls the turrets of the turret controls it is linked to.
                for (ControlLink* controls : controlLinks)
                {
                    if (!controls->isFor(*linked))
                    {
                        continue;
                    }

                    std::vector<POINT> turretCells(static_cast<size_t>(controls->linkedCount()));
                    controls->getLinkedCells(turretCells.data(), static_cast<int32_t>(turretCells.size()));

                    for (const POINT& turretCell : turretCells)
                    {
                        const EditorObject* turret = objectCoveringCell(turretCell.x, turretCell.y);

                        if (turret != nullptr && objectIdClass(turret->id) == OBJECT_CLASS_TURRET)
                        {
                            addStructureLink(database, *turret, controllerId);
                        }
                    }

                    break;
                }
            }
        }

        // The other structures with a role: classes 2-8, and the turrets no control has.
        for (EditorObject* object : objects)
        {
            const int32_t objectClass = objectIdClass(object->id);

            if (objectClass < OBJECT_CLASS_TURRET_CONTROL ||
                (objectClass > OBJECT_CLASS_ARTILLERY_CRATE && objectClass != OBJECT_CLASS_TURRET))
            {
                continue;
            }

            if (objectClass == OBJECT_CLASS_TURRET &&
                std::ranges::any_of(controlLinks, [&](const ControlLink* link)
                                    { return link->isFor(*object) || link->isLinked(*object); }))
            {
                continue;
            }

            StructureRecord structures(&database);
            structures.Open(dbOpenDynaset, nullptr, 0);
            writeStructureRow(structures, missionId, *object, objectClass);
        }

        return true;
    }
    catch (DaoException* e)
    {
        reportStructureError(e);
        return false;
    }
}

namespace
{
    /// <summary>Says string <paramref name="text"/> in a message box.</summary>
    /// <returns>false.</returns>
    bool refuseMission(UINT text)
    {
        CString message;
        message.LoadString(text);
        CString caption;
        caption.LoadString(IDS_MCEDITOR);
        MessageBoxA(nullptr, message, caption, MB_OK);
        return false;
    }
}

bool SingleMap::checkMission([[maybe_unused]] DaoDatabase& database, [[maybe_unused]] const CString& name)
{
    if (objectives.empty())
    {
        return refuseMission(IDS_THIS_MISSION_NEEDS_AT);
    }

    if (dropZones.empty())
    {
        return refuseMission(IDS_THIS_MAP_REQUIRES_AT);
    }

    if (movers.empty())
    {
        return refuseMission(IDS_THIS_MISSION_REQUIRES_AT);
    }

    return checkMissionSize();
}

bool EditorMap::checkMissionSize()
{
    // The bytes the game needs per object, unit or drop zone and building, and the most it has for them.
    constexpr uint32_t OBJECT_BYTES = 0x17c;
    constexpr uint32_t UNIT_BYTES = 0x1868;
    constexpr uint32_t BUILDING_BYTES = 0x6c;
    constexpr uint32_t MISSION_BYTES = 1000000;
    // The game play objects a unit and an objective count as, and the most a mission may have.
    constexpr int32_t UNIT_OBJECTS = 5;
    constexpr int32_t OBJECTIVE_OBJECTS = 4;
    constexpr int32_t MAX_GAME_OBJECTS = 400;

    // The buildings: the overlays that are damaged or have a damaged look.
    uint32_t buildings = 0;

    for (int32_t col = 0; col < maxRow; col++)
    {
        for (int32_t row = 0; row < maxCol; row++)
        {
            const MapOverlay* overlay = getTile(col, row)->overlay;

            if (overlay == nullptr)
            {
                continue;
            }

            const uint8_t flat[4] = {};

            if (overlay->damaged || tileLibrary->findOverlayTile(overlay->kinds, flat, true) != nullptr)
            {
                buildings++;
            }
        }
    }

    const auto units = static_cast<uint32_t>(movers.size() + dropZones.size());

    if (static_cast<uint32_t>(objects.size()) * OBJECT_BYTES + units * UNIT_BYTES + buildings * BUILDING_BYTES >
        MISSION_BYTES)
    {
        return refuseMission(IDS_THIS_MAP_CONTAINS_TOO);
    }

    // The game play objects: the objects and drop zones but scenery, the power stations, gates, sensor towers and
    // spray-can objects; then the units and objectives.
    // Original behaviour: a test for turrets with no control sits where only those classes reach it, so it never
    // counts.
    int32_t gameObjects = 0;

    for (const std::list<EditorObject*>* list : {&objects, &dropZones})
    {
        for (const EditorObject* object : *list)
        {
            switch (objectIdClass(object->id))
            {
                case 0:
                case 1:
                case OBJECT_CLASS_POWER_STATION:
                case OBJECT_CLASS_SENSOR_TOWER:
                case OBJECT_CLASS_GATE:
                case OBJECT_CLASS_SPRAY_CAN:
                    break;
                default:
                    gameObjects++;
                    break;
            }
        }
    }

    gameObjects += static_cast<int32_t>(movers.size()) * UNIT_OBJECTS +
                   static_cast<int32_t>(objectives.size()) * OBJECTIVE_OBJECTS;

    if (gameObjects > MAX_GAME_OBJECTS)
    {
        return refuseMission(IDS_THIS_MAP_CONTAINS_TOO_MANY_GAME);
    }

    return true;
}

bool SingleMap::saveMission(DaoDatabase& database, CString& name)
{
    const CString path = dataPath();
    missionText.write(path + "\\Data\\Missions\\" + (name + ".txt"), artillerySmall, artilleryLarge, artillerySensor,
                      artilleryCamera);
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

    mission.m_MissionFileName = name;
    mission.m_MissionName = name;
    mission.m_ScriptFileName = name + "Params";
    mission.m_BriefingFileName = name + ".txt";
    mission.m_MapSize = maxRow >= LARGE_MAP_ROWS ? 1 : 0;
    mission.m_MissionDateTime = COleDateTime::GetCurrentTime();
    mission.m_Multiplayer = FALSE;
    mission.m_TerrainFileName = name;
    mission.m_ArtilleryLarge = artilleryLarge;
    mission.m_ArtillerySensor = artillerySensor;
    mission.m_ArtilleryCamera = artilleryCamera;
    mission.m_TimeLimit = timeLimit;
    mission.m_ArtillerySmall = artillerySmall;
    mission.m_PlayerForceLimit = playerForceLimit;
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

    // The campaign file: Teditor\campaign.txt with the mission's name in it.
    const std::optional<std::vector<char>> campaign = readTemplate(path + "\\Teditor\\campaign.txt");

    if (!campaign.has_value())
    {
        return false;
    }

    // Original behaviour: the campaign file goes under the current folder, not the data path.
    const CString campaignPath = "data\\missions\\campaign" + name + ".fit";
    FILE* file = std::fopen(campaignPath, "w+b");

    if (file == nullptr)
    {
        return false;
    }

    std::fprintf(file, campaign->data(), static_cast<LPCTSTR>(name));
    std::fflush(file);
    std::fclose(file);
    return writeSave(name, nullptr);
}

bool SingleMap::writeSave(const CString& name, const char* saveName)
{
    const CString path = dataPath();
    const std::optional<std::vector<char>> save = readTemplate(path + "\\Teditor\\sol.txt");

    if (!save.has_value())
    {
        return false;
    }

    FILE* file = std::fopen(SAVE_WORK_FILE, "w+b");

    if (file == nullptr)
    {
        return false;
    }

    // Original behaviour: the game state name is the save name, which the mission save leaves null; the C library
    // prints that as "(null)".
    std::fprintf(file, save->data(), saveName != nullptr ? saveName : "(null)",
                 static_cast<long>(tileSetStringOffset == GTILES_OFFSET ? 1 : 0), resourcePoints);
    std::fflush(file);
    std::fclose(file);

    // The packet file's list: the save, then the player's inventory (without an Uller or with it).
    {
        CStdioFile response;

        if (response.Open(RESPONSE_FILE, CFile::modeCreate | CFile::modeWrite))
        {
            response.WriteString(CString(SAVE_WORK_FILE) + "\n");
            CString inventory = path + "\\teditor\\";
            inventory += noUller ? "issolo.pur\n" : "clansolo.pur\n";
            response.WriteString(inventory);
            response.Close();
        }
    }

    const CString savePath = path + "\\Data\\Savegame\\" + (saveName != nullptr ? CString(saveName) : name) + ".sol";
    buildPacketFile(RESPONSE_FILE, savePath);
    DeleteFileA(RESPONSE_FILE);
    DeleteFileA(SAVE_WORK_FILE);
    return true;
}

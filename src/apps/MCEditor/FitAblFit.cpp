#include "stdafx.h"
#include "FitAbl.h"
#include "MissionTimeOfDay.h"

// FitAbl's .FIT writer (0x11011fb0-0x11026bd0): the mission's settings, drop zones, objectives, warriors, parts,
// commander groups, trains and elemental carriers, from the mission database.

namespace
{
    /// <summary>The caption of the class's error messages.</summary>
    const char* const ERROR_TITLE = "Error";
    /// <summary>The .FIT file's number.</summary>
    inline constexpr int FIT_FILE = 2;
    /// <summary>The mates a commander group lists.</summary>
    inline constexpr short GROUP_SIZE = 12;
    /// <summary>The vehicle type id of an elemental carrier, as text.</summary>
    const char* const CARRIER_TYPE = "419";
    /// <summary>The vehicle type id of an elemental.</summary>
    inline constexpr short ELEMENTAL_TYPE = 400;
    /// <summary>The Vehicles table's VehicleType of an elemental.</summary>
    inline constexpr short ELEMENTAL_VEHICLE_TYPE = 3;

    /// <summary>A drop zone slot's offset in spacings: X or Y, 1-based slot, or literal 0 (printed as "0").</summary>
    struct SlotOffsets
    {
        /// <summary>The rotation it is for.</summary>
        short rotation;
        /// <summary>Slots 1-3's X and Y multipliers of the spacing; 0 prints the literal "0".</summary>
        std::array<short, 6> multipliers;
    };

    /// <summary>The Diamond formation's slot offsets by rotation (0x11016bb4-0x1101c484).</summary>
    const std::array<SlotOffsets, 8> DIAMOND_OFFSETS = {{
        {0, {0, -100, 100, 0, 100, -100}},
        {45, {-71, 71, -71, -71, -142, 0}},
        {90, {-100, 0, 0, -100, -100, -100}},
        {135, {-71, -71, 71, -71, 0, -142}},
        {-45, {71, 71, -71, 71, 0, 142}},
        {-90, {100, 0, 0, 100, 100, 100}},
        {-135, {71, -71, 71, 71, 142, 0}},
        {180, {0, -100, 100, 0, 100, -100}},
    }};

    /// <summary>A Column or Line formation's step between slots (X, Y in spacings) by rotation.</summary>
    struct FormationStep
    {
        short rotation;
        short x;
        short y;
    };

    /// <summary>The Column formation's steps (0x1101c4af-0x1101c8e7).</summary>
    const std::array<FormationStep, 8> COLUMN_STEPS = {{
        {0, -71, 71},
        {45, -100, 0},
        {90, -71, -71},
        {135, 0, -100},
        {-45, 0, 100},
        {-90, 71, 71},
        {-135, 100, 0},
        {180, 71, -71},
    }};

    /// <summary>The Line formation's steps (0x1101d5f2-0x1101d9de).</summary>
    const std::array<FormationStep, 8> LINE_STEPS = {{
        {0, -71, -71},
        {45, 0, -100},
        {90, 71, -71},
        {135, 100, 0},
        {-45, -100, 0},
        {-90, -71, 71},
        {-135, 0, 100},
        {180, 71, 71},
    }};

    /// <summary>A text's variant.</summary>
    COleVariant literal(const char* value)
    {
        return Vb::String(value);
    }

    /// <summary>"[MissionID] = " &amp; the mission's id.</summary>
    CString missionFilter(short missionId)
    {
        return "[MissionID] = " + Vb::CStr(missionId);
    }

    /// <summary><paramref name="prefix"/> &amp; <paramref name="value"/> as a string.</summary>
    CString join(const char* prefix, const COleVariant& value)
    {
        return Vb::CStr(Vb::Cat(literal(prefix), value));
    }

    /// <summary>How many of the VisualRangeTable's entries a line of the .FIT holds.</summary>
    inline constexpr int32_t VISUAL_RANGES_PER_LINE = 16;

    /// <summary>Port-only: the name the .FIT's [Environment] TimeOfDay gives <paramref name="time"/> (MCRedux reads it).</summary>
    const char* timeOfDayName(MissionTimeOfDay time)
    {
        switch (time)
        {
            case MissionTimeOfDay::Dusk:
                return "Dusk";
            case MissionTimeOfDay::Night:
                return "Night";
            default:
                return "Day";
        }
    }

    /// <summary>
    /// Port-only: writes a dusk or night mission's (<paramref name="time"/>) sight into its [GameScale]: every level's
    /// MissionVisualRange (the original reads a shorter table on past its end), which the original honours too.
    /// Nothing by day: the game system's stands.
    /// </summary>
    void writeVisualRanges(VbFile& fit, MissionTimeOfDay time)
    {
        if (time == MissionTimeOfDay::Day)
        {
            return;
        }

        CString line;
        line.Format("l[%d] VisualRangeTable =", VISUAL_RANGE_LEVELS);
        fit.Line(line);

        for (int32_t first = 0; first < VISUAL_RANGE_LEVELS; first += VISUAL_RANGES_PER_LINE)
        {
            line = "\t";

            for (int32_t level = first; level < first + VISUAL_RANGES_PER_LINE; ++level)
            {
                CString entry;
                entry.Format(level + 1 < VISUAL_RANGE_LEVELS ? "%d," : "%d", MissionVisualRange(level, time));
                line += entry;
            }

            fit.Line(line);
        }
    }

    /// <summary>
    /// Port-only: writes a dusk or night mission's [Environment] block, its TimeOfDay (MCRedux shows it with HD
    /// lighting on; the original reads no such block). Nothing by day.
    /// </summary>
    void writeEnvironment(VbFile& fit, MissionTimeOfDay time, const CString& quote)
    {
        if (time == MissionTimeOfDay::Day)
        {
            return;
        }

        fit.Line("[Environment]");
        fit.Line("st TimeOfDay = " + quote + timeOfDayName(time) + quote);
        fit.Line();
    }
}

bool FitAbl::writeFitFile(short missionId)
{
    const CString missionNumber = Vb::CStr(missionId);
    FitAblRecordset mission(database);
    mission.OpenDynaset("SELECT * FROM [NewMissions] WHERE [MissionID] = " + missionNumber);
    mission.MoveLast();

    if (mission.GetRecordCount() != 1)
    {
        Vb::MsgBox("Incorrect record count in NewMissions table.", Vb::MSGBOX_CRITICAL, ERROR_TITLE);
        return false;
    }

    const bool multiplayer = Vb::IsTrue(mission["Multiplayer"]);
    // Port-only: the time of day the mission is set at (MissionTimeOfDay).
    const COleVariant timeOfDayValue = mission["TimeOfDay"];
    const MissionTimeOfDay timeOfDay =
        Vb::IsNull(timeOfDayValue) ? MissionTimeOfDay::Day : MissionTimeOfDayOf(Vb::CLng(timeOfDayValue));
    FitAblRecordset assignments(database);

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(false)))
    {
        assignments.OpenDynaset("SELECT * FROM [MissionAssignments] WHERE [MissionID] = " + missionNumber +
                                " ORDER BY [ALIGNMENT] DESC, [FORCEGROUP] ASC, [Role] ASC");
        assignments.FindFirst(missionFilter(missionId));

        if (assignments.noMatch)
        {
            Vb::MsgBox("No Assignments found for Mission " + missionNumber + ".");
            return false;
        }
    }

    FitAblRecordset objectives(database);
    objectives.OpenDynaset("SELECT * FROM [Objectives] WHERE [MissionID] = " + missionNumber +
                           " ORDER BY [Alignment] DESC, [Objective] ASC");
    objectives.MoveLast();

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(true)) ? objectives.GetRecordCount() < 2
                                                          : objectives.GetRecordCount() < 1)
    {
        Vb::MsgBox("Incorrect number of objectives.", Vb::MSGBOX_CRITICAL, ERROR_TITLE);
        return false;
    }

    FitAblRecordset dropZones(database);
    dropZones.OpenDynaset("SELECT * FROM [PlayerEntryPoints] WHERE [MissionID] = " + missionNumber);

    if (dropZones.GetRecordCount() < 1)
    {
        Vb::MsgBox("No Dropzones were defined.", Vb::MSGBOX_CRITICAL, ERROR_TITLE);
        return false;
    }

    dropZones.MoveLast();

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(false)))
    {
        if (dropZones.GetRecordCount() < 1)
        {
            Vb::MsgBox("No drop zones specified for non-MP mission.", Vb::MSGBOX_CRITICAL, ERROR_TITLE);
            return false;
        }
    }
    else if (dropZones.GetRecordCount() != 6)
    {
        Vb::MsgBox("Incorrect number of drop zones (must be 6).", Vb::MSGBOX_CRITICAL, ERROR_TITLE);
        return false;
    }

    // The single-player assignments: every warrior and unit must exist; count the warriors (trains have none, and
    // so are counted apart) and the parts.
    COleVariant warriorCount = Vb::Integer(0);
    COleVariant partCount = Vb::Integer(0);
    short trainCount = 0;
    bool alliedTeam = false;
    short carrierCount = 0;
    std::vector<short> carrierParts(1);

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(false)))
    {
        mechs = std::make_unique<FitAblRecordset>(database);
        mechs->OpenDynaset("Mechs");
        vehicles = std::make_unique<FitAblRecordset>(database);
        vehicles->OpenDynaset("Vehicles");
        warriors = std::make_unique<FitAblRecordset>(database);
        warriors->OpenDynaset("NewMW");

        do
        {
            if (Vb::Ne(assignments["MWID"], Vb::Integer(0)))
            {
                if (Vb::Ne(assignments["Alignment"], literal("aTrain")))
                {
                    if (Vb::Eq(assignments["Alignment"], literal("Other")))
                    {
                        alliedTeam = true;
                    }

                    warriors->FindFirst(join("[MWID] = ", assignments["MWID"]));

                    if (warriors->noMatch)
                    {
                        Vb::MsgBox(Vb::CStr(Vb::Cat(Vb::Cat(Vb::Cat(Vb::Cat(literal("MWID "), assignments["MWID"]),
                                                                    literal(" not found in database. [")),
                                                            Vb::Integer(missionId)),
                                                    literal("]"))));
                        return false;
                    }

                    warriorCount = Vb::Add(warriorCount, Vb::Integer(1));
                }
                else
                {
                    ++trainCount;
                }
            }
            else if (Vb::Ne(assignments["Alignment"], literal("aTrain")))
            {
                Vb::MsgBox("MWID cannot be Zero. [" + missionNumber + "]");
                return false;
            }

            if (!Vb::IsNull(assignments["MechID"]))
            {
                if (!Vb::Ne(assignments["MechID"], Vb::Integer(0)))
                {
                    Vb::MsgBox("MechID cannot be Zero. [" + missionNumber + "]");
                    return false;
                }

                mechs->FindFirst(join("[ID] = ", assignments["MechID"]));

                if (mechs->noMatch)
                {
                    Vb::MsgBox(Vb::CStr(Vb::Cat(Vb::Cat(Vb::Cat(Vb::Cat(literal("MechID "), assignments["MechID"]),
                                                                literal(" not found in database. [")),
                                                        Vb::Integer(missionId)),
                                                literal("]"))));
                    return false;
                }

                if (Vb::IsTrue(Vb::And(Vb::Boolean(!Vb::IsNull(assignments["VehicleID"])),
                                       Vb::CmpNe(assignments["VehicleID"], Vb::Integer(0)))))
                {
                    Vb::MsgBox("Both a Mech and Vehicle seem to be assigned to the same warrior. [" + missionNumber +
                               "]");
                }
            }
            else if (!Vb::IsNull(assignments["VehicleID"]))
            {
                if (Vb::Ne(assignments["VehicleID"], Vb::Integer(0)))
                {
                    vehicles->FindFirst(join("[VehicleID] = ", assignments["VehicleID"]));

                    if (vehicles->noMatch)
                    {
                        Vb::MsgBox(
                            Vb::CStr(Vb::Cat(Vb::Cat(Vb::Cat(Vb::Cat(literal("VehicleID "), assignments["VehicleID"]),
                                                             literal(" not found in database. [")),
                                                     Vb::Integer(missionId)),
                                             literal("]"))));
                        return false;
                    }
                }
            }
            else
            {
                Vb::MsgBox("No MechID or VehicleID has been assigned. [" + missionNumber + "]");
                return false;
            }

            partCount = Vb::Add(partCount, Vb::Integer(1));
            assignments.FindNext(missionFilter(missionId));
        } while (!assignments.noMatch);
    }

    VbFile& fit = files[FIT_FILE];
    fit.Open(outputFolder + Vb::CStr(Vb::Cat(mission["MissionFileName"], literal(".FIT"))));
    quote = Vb::CStr(Vb::String("\""));

    fit.Line("FITini");
    fit.Line();
    fit.Line("// -------------------------------------------------------------------------");
    fit.Line("//");
    fit.Text("//      Mission Name: ").Value(mission["MissionName"]).Text("  Creation Date: ").Value(Vb::Today());
    fit.Line("  ");
    fit.Line("//");
    fit.Line("// -------------------------------------------------------------------------");
    fit.Line();
    fit.Line();
    fit.Line("[Output]");
    fit.Line();
    fit.Line("[GameScale]                     //For 90 Pixel mechs");
    fit.Line("f WorldUnitsPerMeter = 5.01");
    fit.Line("f MetersPerWorldUnit = 0.2");
    fit.Line("ul Duration = 60");
    fit.Line("f CycleLength = 0.125");
    writeVisualRanges(fit, timeOfDay);
    fit.Line();
    writeEnvironment(fit, timeOfDay, quote);
    fit.Line("[Artillery]");
    fit.Text("l NumLargeStrikes = ").Value(mission["ArtilleryLarge"]).EndLine();
    fit.Text("l NumSmallStrikes = ").Value(mission["ArtillerySmall"]).EndLine();
    fit.Text("l NumSensorStrikes = ").Value(mission["ArtillerySensor"]).EndLine();
    fit.Text("l NumCameraStrikes = ").Value(mission["ArtilleryCamera"]).EndLine();
    fit.Line();

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(true)))
    {
        fit.Line("[ISArtillery]");
        fit.Text("l NumLargeStrikes = ").Value(mission["ArtilleryLarge"]).EndLine();
        fit.Text("l NumSmallStrikes = ").Value(mission["ArtillerySmall"]).EndLine();
        fit.Text("l NumSensorStrikes = ").Value(mission["ArtillerySensor"]).EndLine();
        fit.Text("l NumCameraStrikes = ").Value(mission["ArtilleryCamera"]).EndLine();
        fit.Line();
        fit.Line("[ClanArtillery]");
        fit.Text("l NumLargeStrikes = ").Value(mission["ClanArtilleryLarge"]).EndLine();
        fit.Text("l NumSmallStrikes = ").Value(mission["ClanArtillerySmall"]).EndLine();
        fit.Text("l NumSensorStrikes = ").Value(mission["ClanArtillerySensor"]).EndLine();
        fit.Text("l NumCameraStrikes = ").Value(mission["ClanArtilleryCamera"]).EndLine();
        fit.Line();
    }

    fit.Line("[ElementSystem]");
    fit.Line("ul ElementHeapSize = 358399");
    fit.Line("ul MaxElements = 2500");
    fit.Line("ul MaxGroups = 1000");
    fit.Line();
    fit.Line("[CraterSystem]");
    fit.Line("l NumCraters = 100");
    fit.Line("ul CraterShapeSize = 20479");
    fit.Line("st CraterFile = " + quote + "feet" + quote);
    fit.Line();
    fit.Line("[ContactManager]");
    fit.Line("l MaxContacts = 1600");
    fit.Line();
    fit.Line("[PotentialContactManager]");
    fit.Line("l MaxPotentialContacts = 500");
    fit.Line();
    fit.Line("[Smoke Manager]");
    fit.Line("l NumSmokeTypes = 2");
    fit.Line("l MaxSmokesPerType = 100");
    fit.Line("ul SmokeSphereHeapSize = 50000");
    fit.Line();
    fit.Line("[Smoke0]");
    fit.Line("l SmokeType = 11");
    fit.Line();
    fit.Line("[Smoke1]");
    fit.Line("l SmokeType = 12");
    fit.Line();
    fit.Line("[CameraSystem]");
    fit.Line("ul CameraHeapSize = 8191");
    fit.Line("st CameraFileName = " + quote + "cameras" + quote);
    fit.Line();
    fit.Line("[ObjectSystem]");
    fit.Line("ul ObjectHeapSize = 1535999");
    fit.Line("ul ObjectTypeHeapSize = 716799");
    fit.Line("ul NumObjects = 5000");
    fit.Line("st ObjectFileName = " + quote + "object2" + quote);
    fit.Line();
    fit.Line("[SpriteSystem]");
    fit.Line("ul SpriteHeapSize = 524287");
    fit.Line("st SpriteFileName = " + quote + "sprites" + quote);
    fit.Line();
    fit.Line("ul SpriteManagerHeapSize = 2097151");
    fit.Line("ul SpriteDataHeapSize = 65535");
    fit.Line("st ShapeFileName = " + quote + "shapes" + quote);
    fit.Line();
    fit.Line("[SpriteManager]");
    fit.Line("ul LegHeapSize = 1572863");
    fit.Line("ul TorsoHeapSize = 2097151");
    fit.Line("ul RightArmHeapSize = 786431");
    fit.Line("ul LeftArmHeapSize = 786431");
    fit.Line("ul TotalMechs = 19");
    fit.Line("ul Use90Pixel = 1");
    fit.Line();
    fit.Line("[TerrainSystem]");
    fit.Text("st TerrainFileName = " + quote).Value(mission["TerrainFileName"]).Text(quote).EndLine();
    fit.Text("st TacMapGifName = " + quote).Value(mission["TerrainFileName"]).Text(quote).EndLine();
    fit.Line();
    fit.Line("[PaletteSystem]");
    const CString palette = Vb::Eq(mission["Planet"], literal("Garotte")) ? "palettex" : "Palette";
    fit.Line("st PaletteSystem = " + quote + palette + quote);
    fit.Line();
    fit.Line("[Music]");
    fit.Line("uc scenarioTuneNum = 0");
    fit.Line();
    fit.Line("[CollisionSystem]");

    // The collision grid by the map's size; any other size counts as large.
    const COleVariant mapSize = mission["MapSize"];
    line = Vb::CStr(static_cast<short>(Vb::Eq(mapSize, Vb::Integer(0))   ? 24
                                       : Vb::Eq(mapSize, Vb::Integer(1)) ? 36
                                                                         : 48));
    fit.Text("ul XGridSize =").Text(line).EndLine();
    fit.Text("ul YGridSize =").Text(line).EndLine();
    fit.Line("ul GridRadius = 200");
    fit.Line("ul MaxObjects = 300");
    fit.Line("ul MaxCollisions = 100");
    fit.Line("ul MaxPending = 40");
    fit.Line("ul CollisionHeapSize = 16383");
    fit.Line("f WarningDist = 250.0                           //This is in world Units!!!");
    fit.Line("f AlertTime = 2.5                                       //This is in seconds");
    fit.Line("ul NumAlerts = 20");
    fit.Line();
    fit.Line("[SensorContactShape]");
    fit.Line("st shapeName = " + quote + "blip" + quote);
    fit.Line();
    fit.Line("[ABLibraries]");
    fit.Line("st Library0 = " + quote + "orders" + quote);
    fit.Line("st Library1 = " + quote + "miscfunc" + quote);
    fit.Line();
    fit.Line("[Script]");
    fit.Text("st ScenarioScript = " + quote).Value(mission["MissionFileName"]).Text(quote).EndLine();
    fit.Line();
    fit.Line("[StatusWindow]");
    fit.Line("ul PosX = 200");
    fit.Line("ul PosY = 200");
    fit.Line("ul Width = 300");
    fit.Line("ul Height = 100");
    fit.Line();

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(true)))
    {
        fit.Line("[Teams]");
        fit.Line("b AlliedTeam = FALSE");
        fit.Line();
    }

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(false)))
    {
        fit.Line("[Campaign]");

        if (!Vb::IsNull(mission["BriefingFileName"]))
        {
            fit.Text("st BriefingFile = " + quote).Value(mission["BriefingFileName"]).Text(quote).EndLine();
        }
        else
        {
            Vb::MsgBox("Briefing File name not specified.");
            fit.Line("st BriefingFile = " + quote + " " + quote);
        }

        if (!Vb::IsNull(mission["TerrainFileName"]))
        {
            fit.Text("st MapFile = " + quote).Value(mission["TerrainFileName"]).Text(quote).EndLine();
        }
        else
        {
            Vb::MsgBox("Briefing Map name not specified.");
            fit.Line("st MapFile = " + quote + " " + quote);
        }

        if (!Vb::IsNull(mission["PlayerForceLimit"]))
        {
            fit.Line(join("l MaxTonnage = ", mission["PlayerForceLimit"]));
        }
        else
        {
            Vb::MsgBox("Player Tonnage Limit not specified.  Assuming 1200 ...");
            fit.Line("l MaxTonnage = 1200");
        }
    }

    // The drop zones.
    dropZones.MoveFirst();
    short dropZoneCount = 0;

    while (!dropZones.IsEOF())
    {
        ++dropZoneCount;
        dropZones.MoveNext();
    }

    if (dropZoneCount == 0)
    {
        Vb::MsgBox("No Drop Zones are specified. This mission will not work in the campaign game ...");
    }

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(false)))
    {
        fit.Line("l NumDropZones = " + Vb::CStr(dropZoneCount));
        fit.Line();
    }

    dropZones.MoveFirst();
    short dropZone = -1;

    while (!dropZones.IsEOF())
    {
        ++dropZone;
        line = "[DropZone" + Vb::CStr(dropZone) + "]";
        fit.Line(line);
        fit.Line(join("l NumSlots = ", dropZones["Slots"]));
        fit.Line();
        fit.Line(join("f PositionX = ", dropZones["PlayerEntryPointX"]));
        fit.Line(join("f PositionY = ", dropZones["PlayerEntryPointY"]));
        fit.Line();
        writeDropZoneOffsets(dropZones);
        dropZones.MoveNext();
    }

    // The objectives (in a multiplayer mission, the Clan's are those of alignment 1).
    fit.Line("[Objectives]");
    objectives.MoveFirst();
    counter = Vb::Integer(0);
    counter2 = Vb::Integer(0);

    while (!objectives.IsEOF())
    {
        counter = Vb::Add(counter, Vb::Integer(1));

        if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(true)) && Vb::Eq(objectives["Alignment"], Vb::Integer(1)))
        {
            counter2 = Vb::Add(counter2, Vb::Integer(1));
        }

        objectives.MoveNext();
    }

    line = join("ul NumObjectives = ", counter);
    fit.Text(line).Line("                            //There can be a maximum of eight objectives");

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(true)))
    {
        line = join("ul NumInnerSphereObjectives = ", Vb::Sub(counter, counter2));
        fit.Line(line);
        line = join("ul NumClanObjectives = ", counter2);
        fit.Line(line);
    }

    if (!Vb::IsNull(mission["TimeLimit"]) && Vb::Gt(mission["TimeLimit"], Vb::Integer(0)))
    {
        fit.Line(join("l TimeLeft =", mission["TimeLimit"]));
    }

    fit.Line();
    objectives.MoveFirst();
    const COleVariant lastObjective = Vb::Sub(counter, Vb::Integer(1));

    for (counter2 = Vb::Integer(0); Vb::Le(counter2, lastObjective); counter2 = Vb::Add(counter2, Vb::Integer(1)))
    {
        line = Vb::CStr(Vb::Cat(Vb::Cat(literal("[Objective"), counter2), literal("]")));
        fit.Line(line);
        fit.Text("st Name = " + quote).Value(objectives["Description"]).Text(quote).EndLine();
        fit.Text("ul Type =").Value(objectives["Priority"]);
        fit.Line("                                                     //Types are 0=primary, 1=secondary, 2=other, "
                 "-1=invisible");
        fit.Text("f TimeLeft = ").Value(objectives["TimeLeft"]);
        fit.Line("                                       //Time to accomplish objective.  -1.0 means forever");
        fit.Text("ul Status =").Value(objectives["Status"]);
        fit.Line("                                           //Status are 0=incomplete, 1=success, 2=failed");

        if (Vb::IsNull(objectives["ResourcePoints"]))
        {
            fit.Line("l Points = 999");
        }
        else
        {
            fit.Text("l Points =").Value(objectives["ResourcePoints"]).EndLine();
        }

        fit.Line();

        if (Vb::Lt(counter2, lastObjective))
        {
            objectives.MoveNext();
        }
    }

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(true)))
    {
        fit.Line("[Campaign]");
        fit.Line("l NumDropZones = " + Vb::CStr(static_cast<short>(dropZone + 1)));

        if (!Vb::IsNull(mission["BriefingFileName"]))
        {
            line = Vb::CStr(Vb::Cat(literal("IS"), mission["BriefingFileName"]));
            fit.Line("st ISBriefingFile = " + quote + line + quote);
            line = Vb::CStr(Vb::Cat(literal("C"), mission["BriefingFileName"]));
            fit.Line("st ClanBriefingFile = " + quote + line + quote);
        }
        else
        {
            Vb::MsgBox("Briefing File name not specified.");
        }

        if (!Vb::IsNull(mission["TerrainFileName"]))
        {
            fit.Text("st MapFile = " + quote).Value(mission["TerrainFileName"]).Text(quote).EndLine();
        }
        else
        {
            Vb::MsgBox("Briefing Map name not specified.");
            fit.Line("st MapFile = " + quote + " " + quote);
        }

        if (!Vb::IsNull(mission["PlayerForceLimit"]))
        {
            fit.Line(join("l MaxTonnage = ", mission["PlayerForceLimit"]));
        }
        else
        {
            Vb::MsgBox("Player Tonnage Limit not specified.  Assuming 1200 ...");
            fit.Line("l MaxTonnage = 1200");
        }
    }

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(false)))
    {
        // The warriors: the assignments that aren't trains, in their order.
        fit.Line("[Warriors]");
        fit.Text("ul NumWarriors =").Value(warriorCount).EndLine();

        if (!Vb::IsNull(mission["CaptureChance"]))
        {
            fit.Text("uc CaptureChance =").Value(mission["CaptureChance"]).EndLine();
        }

        // Original behaviour: no line break after it, the next Print # ends the line.
        fit.Value(Vb::Cat(Vb::Cat(literal("st BrainParameterFile = " + quote), mission["ScriptFileName"]),
                          Vb::String(quote)));
        fit.Line();
        const CString warriorFilter = missionFilter(missionId) + " AND [Alignment] <> 'aTrain'";
        assignments.FindFirst(warriorFilter);

        for (counter = Vb::Integer(1); Vb::Le(counter, warriorCount); counter = Vb::Add(counter, Vb::Integer(1)))
        {
            line = Vb::CStr(Vb::Cat(Vb::Cat(literal("[Warrior"), counter), literal("]")));
            fit.Line(line);
            warriors->FindFirst(join("[MWID] = ", assignments["MWID"]));
            const COleVariant fitFile = (*warriors)["FITFileName"];

            if (Vb::IsNull(fitFile) || Vb::Eq(Vb::Trim(fitFile), literal("")))
            {
                // The profile's name: "PMW" and the warrior's id in five digits.
                const COleVariant id = assignments["MWID"];
                const char* padding = "";

                if (Vb::Lt(id, Vb::Integer(10000)))
                {
                    padding = Vb::Lt(id, Vb::Integer(10))     ? "0000"
                              : Vb::Lt(id, Vb::Integer(100))  ? "000"
                              : Vb::Lt(id, Vb::Integer(1000)) ? "00"
                                                              : "0";
                }

                line = Vb::CStr(Vb::Cat(Vb::Cat(literal("PMW"), literal(padding)), id));
            }
            else
            {
                line = Vb::CStr((*warriors)["FITFileName"]);
            }

            fit.Line("st Profile = " + quote + line + quote);
            const COleVariant brain = assignments["BrainFile"];

            if (Vb::IsNull(brain) || Vb::Eq(brain, literal("")))
            {
                line = Vb::Eq(assignments["Alignment"], literal("Clan"))    ? "attackbrain"
                       : Vb::Eq(assignments["Alignment"], literal("Other")) ? "attackbrain"
                                                                            : "pbrain";
            }
            else
            {
                line = Vb::CStr(assignments["BrainFile"]);
            }

            fit.Line("st Brain = " + quote + line + quote);
            fit.Line();

            if (Vb::Lt(counter, warriorCount))
            {
                assignments.FindNext(warriorFilter);
            }
        }
    }

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(false)))
    {
        // The parts: every assignment, a mech or a vehicle.
        fit.Line("[Parts]");
        fit.Text("ul NumParts =").Value(partCount).EndLine();
        fit.Text("b AlliedTeam = ").Value(Vb::Boolean(alliedTeam)).EndLine();
        fit.Line();
        assignments.FindFirst(missionFilter(missionId));

        for (counter = Vb::Integer(1); Vb::Le(counter, partCount); counter = Vb::Add(counter, Vb::Integer(1)))
        {
            const bool isMech = !Vb::IsNull(assignments["MechID"]);

            if (isMech)
            {
                mechs->FindFirst(join("[ID] = ", assignments["MechID"]));
            }
            else
            {
                vehicles->FindFirst(join("[VehicleID] = ", assignments["VehicleID"]));
            }

            line = Vb::CStr(Vb::Cat(Vb::Cat(literal("[Part"), counter), literal("]")));
            fit.Line(line);

            if (isMech)
            {
                line = Vb::CStr((*mechs)["ObjectNumber"]);
            }
            else
            {
                line = Vb::CStr((*vehicles)["TypeID"]);

                // An elemental carrier: remember its part for the carriers' section.
                if (line == CARRIER_TYPE)
                {
                    ++carrierCount;
                    carrierParts.resize(carrierCount + 1);
                    carrierParts[carrierCount] = Vb::CInt(counter);
                }
            }

            fit.Line("ul ObjectNumber         = " + line);
            fit.Line("ul ControlType          = 2                 // player = 1, ai = 2, net = 3");
            fit.Text("b PlayerPart            = ")
                .Value(Vb::CmpEq(assignments["Alignment"], literal("Inner Sphere")))
                .EndLine();

            if (isMech)
            {
                fit.Line("ul ControlDataType      = 1                 // mech control data = 1");
            }
            else if (Vb::Eq((*vehicles)["VehicleType"], Vb::Integer(ELEMENTAL_VEHICLE_TYPE)))
            {
                fit.Line("ul ControlDataType      = 3                 // elemental control data = 3");
            }
            else
            {
                fit.Line("ul ControlDataType      = 2                 // vehicle control data = 1");
            }

            fit.Line("c MyIcon                = 0                 //");
            const COleVariant alignment = assignments["Alignment"];

            if (Vb::Eq(alignment, literal("Inner Sphere")))
            {
                fit.Line("c TeamID                = 0    // 0 = IS, 1 = CLAN, 2 = ALLIED");
                fit.Line("c CommanderID           = 0    // 0 = IS, 1 = CLAN, 2 = ALLIED");
            }
            else if (Vb::Eq(alignment, literal("Clan")) || Vb::Eq(alignment, literal("aTrain")))
            {
                fit.Line("c TeamID                = 1    // 0 = IS, 1 = CLAN, 2 = ALLIED");
                fit.Line("c CommanderID           = 1    // 0 = IS, 1 = CLAN, 2 = ALLIED");
            }
            else if (Vb::Eq(alignment, literal("Other")))
            {
                fit.Line("c TeamID                = 2    // 0 = IS, 1 = CLAN, 2 = ALLIED");
                fit.Line("c CommanderID           = 2    // 0 = IS, 1 = CLAN, 2 = ALLIED");
            }

            FitAblRecordset& unit = isMech ? *mechs : *vehicles;
            text = Vb::CStr(unit["Name"]);
            const COleVariant unitFile = unit["FITFileName"];

            if (Vb::IsNull(unitFile) || Vb::Eq(Vb::Trim(unitFile), Vb::String("")))
            {
                line = isMech ? Vb::CStr(Vb::Cat(Vb::String("PM"), unit["ID"]))
                              : Vb::CStr(Vb::Cat(Vb::String("PV"), unit["VehicleID"]));
            }
            else
            {
                line = Vb::CStr(unit["FITFileName"]);
            }

            fit.Line("st ObjectProfile        = " + quote + line + quote + "        // " + text);

            if (Vb::Ne(assignments["Alignment"], Vb::String("aTrain")))
            {
                fit.Text("ul Pilot                =").Value(Vb::Sub(counter, Vb::Integer(trainCount))).EndLine();
            }
            else
            {
                fit.Line("ul Pilot                = 1 // Trains don't have pilots.");
            }

            fit.Text("f PositionX             = ").Value(assignments["XCoordinate"]);
            fit.Line("             // Starting position in world.");
            fit.Text("f PositionY             = ").Value(assignments["YCoordinate"]).EndLine();
            fit.Line("f PositionZ             = -1.0              // Elevation automatically set by terrain.");
            fit.Text("f Rotation              = ").Value(assignments["Rotation"]);
            fit.Line("              // Rotation of Object in Degrees");
            fit.Line("ul Gesture              = 2                 // Initial Sprite Gesture -- Determines velocity in "
                     "Mechs -- non-mechs ignore");
            fit.Line("f Velocity              = 0.0               // Initial Velocity -- Start velocity for non-mech "
                     "objects -- mechs ignore");
            fit.Text("l Active                = ").Value(Vb::Abs(assignments["Active"])).EndLine();
            fit.Text("l Exists                = ").Value(Vb::Abs(assignments["Exists"])).EndLine();
            fit.Line();

            if (Vb::Lt(counter, partCount))
            {
                assignments.FindNext(missionFilter(missionId));
            }
        }
    }

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(false)))
    {
        // The commander groups: the parts of each force group of an alignment, 12 mates to a group.
        fit.Line("[Teams]");
        fit.Text("b AlliedTeam = ").Value(Vb::Boolean(alliedTeam)).EndLine();
        fit.Line();
        struct Commander
        {
            const char* alignment;
            const char* section;
        };

        const std::array<Commander, 3> commanders = {{
            {"Inner Sphere", "[Commander0Group:"},
            {"Clan", "[Commander1Group:"},
            {"Other", "[Commander2Group:"},
        }};

        for (const Commander& commander : commanders)
        {
            // The allied commander only when there are allied ("Other") units.
            if (commander.alignment == commanders[2].alignment && !alliedTeam)
            {
                break;
            }

            counter2 = Vb::Integer(0);
            counter = Vb::Integer(-1);
            COleVariant slots = Vb::Integer(0);
            auto finishGroup = [&]()
            {
                while (Vb::Lt(slots, Vb::Integer(GROUP_SIZE)))
                {
                    line += ", 0";
                    slots = Vb::Add(slots, Vb::Integer(1));
                }

                fit.Line(line);
                fit.Line();
                slots = Vb::Integer(0);
            };

            assignments.FindFirst(missionFilter(missionId));

            while (!assignments.noMatch)
            {
                counter2 = Vb::Add(counter2, Vb::Integer(1));

                if (Vb::Eq(assignments["Alignment"], Vb::String(commander.alignment)))
                {
                    if (Vb::Ne(counter, assignments["ForceGroup"]))
                    {
                        if (Vb::Gt(slots, Vb::Integer(0)))
                        {
                            finishGroup();
                        }

                        counter = assignments["ForceGroup"];
                        line = Vb::CStr(Vb::Cat(
                            Vb::Cat(Vb::String(commander.section), Vb::Sub(counter, Vb::Integer(1))), Vb::String("]")));
                        fit.Line(line);
                        line = Vb::CStr(Vb::Cat(Vb::String("l[12] Mates             = "), counter2));
                        slots = Vb::Integer(1);
                    }
                    else
                    {
                        slots = Vb::Add(slots, Vb::Integer(1));

                        if (Vb::Gt(slots, Vb::Integer(GROUP_SIZE)))
                        {
                            Vb::MsgBox(Vb::CStr(Vb::Cat(
                                Vb::Cat(Vb::Cat(Vb::String("Too many assignments to "), assignments["Alignment"]),
                                        Vb::String(" Group ")),
                                assignments["ForceGroup"])));
                            return false;
                        }

                        line = Vb::CStr(Vb::Cat(Vb::String(line + ", "), counter2));

                        // Original behaviour: a full group is written, but the next part of the same force group
                        // carries on from its line.
                        if (Vb::Eq(slots, Vb::Integer(GROUP_SIZE)))
                        {
                            fit.Line(line);
                            fit.Line();
                            slots = Vb::Integer(0);
                        }
                    }
                }
                else if (Vb::Gt(slots, Vb::Integer(0)))
                {
                    finishGroup();
                }

                assignments.FindNext(missionFilter(missionId));
            }

            if (Vb::Gt(slots, Vb::Integer(0)))
            {
                finishGroup();
            }
        }
    }

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(false)))
    {
        // The trains: force groups of "aTrain" assignments, numbered from 1; the last one's group is their count.
        const CString trainFilter = missionFilter(missionId) + " AND [Alignment] = 'aTrain'";
        assignments.FindFirst(trainFilter);
        COleVariant trains = Vb::Integer(0);

        while (!assignments.noMatch)
        {
            trains = assignments["ForceGroup"];
            assignments.FindNext(trainFilter);
        }

        if (!Vb::Lt(trains, Vb::Integer(1)))
        {
            fit.Line("[Trains]");
            line = join("l NumTrains             = ", trains);
            fit.Line(line);
            fit.Line();

            for (counter = Vb::Integer(1); Vb::Le(counter, trains); counter = Vb::Add(counter, Vb::Integer(1)))
            {
                line =
                    Vb::CStr(Vb::Cat(Vb::Cat(Vb::String("[Train"), Vb::Sub(counter, Vb::Integer(1))), Vb::String("]")));
                fit.Line(line);
                const CString carFilter = Vb::CStr(Vb::Cat(
                    Vb::String(missionFilter(missionId) + " AND [Alignment] = 'aTrain' AND [ForceGroup] = "), counter));
                assignments.FindFirst(carFilter);
                COleVariant cars = Vb::Integer(0);

                while (!assignments.noMatch)
                {
                    cars = Vb::Add(cars, Vb::Integer(1));
                    assignments.FindNext(carFilter);
                }

                line = join("l Numcars = ", cars);
                fit.Line(line);
                assignments.FindFirst(missionFilter(missionId));
                counter4 = Vb::Integer(0);
                counter2 = Vb::Integer(-1);

                while (!assignments.noMatch)
                {
                    counter4 = Vb::Add(counter4, Vb::Integer(1));

                    if (Vb::IsTrue(Vb::And(Vb::CmpEq(assignments["Alignment"], Vb::String("aTrain")),
                                           Vb::CmpEq(assignments["ForceGroup"], counter))))
                    {
                        counter2 = Vb::Add(counter2, Vb::Integer(1));
                        line = Vb::CStr(
                            Vb::Cat(Vb::Cat(Vb::Cat(Vb::String("l Car"), counter2), Vb::String(" = ")), counter4));
                        fit.Line(line);
                    }

                    if (Vb::Eq(counter2, Vb::Sub(cars, Vb::Integer(1))))
                    {
                        break;
                    }

                    assignments.FindNext(missionFilter(missionId));
                }

                fit.Line();
            }
        }
    }

    if (Vb::Eq(mission["Multiplayer"], Vb::Boolean(false)) && carrierCount > 0)
    {
        // The elemental carriers: each carrier's part, and its elementals (vehicles of type 400 placed at the
        // carrier's number).
        fit.Line("[Elemental Carriers]");
        fit.Text("l Carriers = ").Value(Vb::Integer(carrierCount)).EndLine();
        fit.Line();
        const COleVariant lastCarrier = Vb::Integer(static_cast<short>(carrierCount - 1));

        for (counter = Vb::Integer(0); Vb::Le(counter, lastCarrier); counter = Vb::Add(counter, Vb::Integer(1)))
        {
            line = Vb::CStr(Vb::Cat(Vb::Cat(Vb::String("[ECarrier"), counter), Vb::String("]")));
            fit.Line(line);
            fit.Text("l Carrier = ").Value(Vb::Integer(carrierParts[Vb::CInt(counter) + 1])).EndLine();
            assignments.FindFirst(missionFilter(missionId));
            counter4 = Vb::Integer(0);
            counter2 = Vb::Integer(-1);

            while (!assignments.noMatch)
            {
                counter4 = Vb::Add(counter4, Vb::Integer(1));
                const COleVariant carrier = Vb::Add(counter, Vb::Integer(1));

                if (Vb::IsTrue(Vb::And(Vb::And(Vb::Boolean(Vb::IsNull(assignments["MechID"])),
                                               Vb::CmpEq(assignments["XCoordinate"], carrier)),
                                       Vb::CmpEq(assignments["YCoordinate"], carrier))))
                {
                    vehicles->FindFirst(join("[VehicleID] = ", assignments["VehicleID"]));

                    if (Vb::IsTrue(Vb::And(Vb::Boolean(!vehicles->noMatch),
                                           Vb::CmpEq((*vehicles)["TypeID"], Vb::Integer(ELEMENTAL_TYPE)))))
                    {
                        counter2 = Vb::Add(counter2, Vb::Integer(1));
                        line = Vb::CStr(Vb::Cat(
                            Vb::Cat(Vb::Cat(Vb::String("l Elemental"), counter2), Vb::String(" = ")), counter4));
                        fit.Line(line);
                    }
                }

                assignments.FindNext(missionFilter(missionId));
            }

            fit.Line();
        }
    }

    fit.Line("FITend");
    fit.Close();
    return true;
}

void FitAbl::writeDropZoneOffsets(FitAblRecordset& dropZone)
{
    VbFile& fit = files[FIT_FILE];
    const COleVariant formation = dropZone["Formation"];
    const COleVariant rotation = dropZone["Rotation"];
    auto writeRotation = [&](int slot)
    { fit.Line(join(CString("f Rotation") + static_cast<char>('0' + slot) + " = ", dropZone["Rotation"])); };
    auto writeOffset = [&](const char* axis, int slot, const COleVariant& offset)
    { fit.Line(join(CString("f Offset") + axis + static_cast<char>('0' + slot) + " = ", offset)); };

    if (Vb::Eq(formation, Vb::String("Diamond")))
    {
        const auto entry = std::find_if(DIAMOND_OFFSETS.begin(), DIAMOND_OFFSETS.end(), [&](const SlotOffsets& item)
                                        { return Vb::Eq(rotation, Vb::Integer(item.rotation)); });

        // A rotation not in the table writes no offsets.
        if (entry == DIAMOND_OFFSETS.end())
        {
            return;
        }

        fit.Line("f OffsetX0 = 0");
        fit.Line("f OffsetY0 = 0");
        writeRotation(0);
        fit.Line();

        for (int slot = 1; slot <= 3; ++slot)
        {
            for (int axis = 0; axis < 2; ++axis)
            {
                const short multiplier = entry->multipliers[(slot - 1) * 2 + axis];
                const char* name = axis == 0 ? "X" : "Y";

                if (multiplier == 0)
                {
                    fit.Line(CString("f Offset") + name + static_cast<char>('0' + slot) + " = 0");
                }
                else
                {
                    writeOffset(name, slot, Vb::Mul(Vb::Integer(multiplier), dropZone["Spacing"]));
                }
            }

            writeRotation(slot);

            if (slot < 3)
            {
                fit.Line();
            }
        }

        fit.Line();
        return;
    }

    const bool column = Vb::Eq(formation, Vb::String("Column"));

    if (!column && !Vb::Eq(formation, Vb::String("Line")))
    {
        return;
    }

    // Original behaviour: a rotation not in the table keeps the step the last drop zone set.
    const std::array<FormationStep, 8>& steps = column ? COLUMN_STEPS : LINE_STEPS;
    const auto entry = std::find_if(steps.begin(), steps.end(), [&](const FormationStep& item)
                                    { return Vb::Eq(rotation, Vb::Integer(item.rotation)); });

    if (entry != steps.end())
    {
        counter = Vb::Integer(entry->x);
        counter2 = Vb::Integer(entry->y);
    }

    fit.Line("f OffsetX0 = 0");
    fit.Line("f OffsetY0 = 0");
    writeRotation(0);
    fit.Line();

    for (short slot = 1; slot <= 3; ++slot)
    {
        const COleVariant x = slot == 1 ? counter : Vb::Mul(Vb::Integer(slot), counter);
        const COleVariant y = slot == 1 ? counter2 : Vb::Mul(Vb::Integer(slot), counter2);
        writeOffset("X", slot, Vb::Mul(x, dropZone["Spacing"]));
        writeOffset("Y", slot, Vb::Mul(y, dropZone["Spacing"]));
        writeRotation(slot);

        if (slot < 3)
        {
            fit.Line();
        }
    }

    fit.Line();
}

#include "stdafx.h"
#include "terrain/MCTacticalMap.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "engine/MCFont.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiScrollTextObject.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCMsvcSort.h"
#include "main/MCMissionGlobals.h"
#include "main/MCGameStrings.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "object/MCArtillery.h"
#include "object/MCArtilleryType.h"
#include "object/MCArtilleryChunk.h"
#include "object/MCCameraDrone.h"
#include "object/MCCameraDroneType.h"
#include "object/MCBuilding.h"
#include "object/MCBuildingType.h"
#include "object/MCBuildingMarines.h"
#include "object/MCObjectDrawing.h"
#include "object/MCMasterComponent.h"
#include "object/MCContactSystem.h"
#include "object/MCBigGameObject.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCMover.h"
#include "object/MCMoverGameSystem.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectType.h"
#include "object/MCTreeBuilding.h"
#include "object/MCTreeBuildingType.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "terrain/MCTacticalMapLayout.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfx.h"
#include "vfx/MCVfxFunctions.h"

using namespace MCTacmapLayout;

namespace
{
    /// <summary>Colours of the info page's weapon sections (short, medium, long range, equipment).</summary>
    constexpr std::array<uint8_t, 4> RangeColors = {0x0e, 0xe5, 0xee, 0x14};

    /// <summary>A weapon of the info page's list.</summary>
    struct MCWeaponEntry
    {
        /// <summary>The weapon's MasterComponentList index.</summary>
        uint8_t MasterID = 0;
        /// <summary>0xff damaged, 0 no shots left, 1 ready.</summary>
        uint8_t State = 0;
        /// <summary>The range bracket (0, 10000, 20000) plus the damage: the sort key.</summary>
        int16_t SortKey = 0;
    };

    /// <summary>The MFD pixel of <paramref name="world"/> on the map pane (the map's position, less the pane's corner).</summary>
    std::pair<int32_t, int32_t> MapPixel(MCTacticalMap& map, const MCVector3D& world)
    {
        MCVector3D pos(world.X, world.Y, 0.0f);
        map.WorldToTacMap(pos, true);
        return {static_cast<int32_t>(std::floor(static_cast<double>(pos.X))) - map.MapPane.X0,
                static_cast<int32_t>(std::floor(static_cast<double>(pos.Y))) - map.MapPane.Y0};
    }

    /// <summary>
    /// Draws a contact's sensor range around its dot: red, or white for the home side's alignment, or yellow while
    /// the sensor is weakened.
    /// </summary>
    void DrawSensorRange(MCTacticalMap& map, MCGameObject& obj, int32_t xPos, int32_t yPos, int32_t homeAlignment)
    {
        std::shared_ptr<MCSensorSystem> sensor = static_cast<MCMover&>(obj).SensorSystem.lock();
        float range = -1.0f;

        if (sensor != nullptr && sensor->Enabled() != 0)
        {
            range = (sensor->GetSkilledRange() * WorldUnitsPerMeter) / map.MetersPerPixel;
        }

        if (range <= 0.0)
        {
            return;
        }

        uint8_t color = 0xef;

        if (obj.GetAlignment() == homeAlignment)
        {
            color = 0x1f;
        }

        if (sensor->Multiplier < 1.0)
        {
            color = 0xf2;
        }

        const auto radius = static_cast<int32_t>(range);
        AGEllipseDraw(map.MapPane, xPos, yPos, radius, radius, color);
    }

    /// <summary>The part diagram's colour of a location from what is left of it: green, yellow, orange or red.</summary>
    uint8_t DamageColor(float current, uint8_t maximum)
    {
        const auto percent = static_cast<int16_t>(std::floor((current / static_cast<float>(maximum)) * 100.0f));

        if (percent >= 0x4c)
        {
            return 0xb;
        }

        if (percent >= 0x33)
        {
            return 0xf2;
        }

        if (percent >= 0x1a)
        {
            return 0xeb;
        }

        return 0xef;
    }

    /// <summary>
    /// The table the part diagram's shape is drawn through for a <see cref="DamageColor"/> colour: rows of the fade
    /// palettes past the haze levels, or for a destroyed location (0x19) the map's colour remap.
    /// </summary>
    MCColorTable PartColorTable(MCTacticalMap& map, uint8_t color)
    {
        const int32_t row = GamePalette().NumBitmapHazeLevels;
        const auto fades = [](int32_t offset)
        {
            return std::span<const uint8_t>(GamePalette().FadePalettes)
                .subspan(static_cast<size_t>(offset))
                .first<256>();
        };

        switch (color)
        {
            case 0xb:
                return fades((row + 10) * 0x200);
            case 0x19:
                return std::span<const uint8_t>(map.ColorRemap.Bytes()).first<256>();
            case 0xeb:
                return fades(row * 0x200 + 0x1500);
            case 0xef:
                return fades(row * 0x200 + 0x1700);
            case 0xf2:
                return fades((row + 11) * 0x200);
            default:
                return fades((row + 12) * 0x200);
        }
    }

    /// <summary>String <paramref name="id"/> of the string table.</summary>
    std::string TableString(uint32_t id)
    {
        return LoadGameString(id, 0xfe);
    }

    /// <summary>Writes <paramref name="text"/> in <paramref name="font"/> at (<paramref name="x"/>, <paramref name="y"/>).</summary>
    void WriteText(MCGuiFont& font, MCPane& pane, int32_t x, int32_t y, std::string text)
    {
        font.WriteString(pane, x, y, text.data(), -1);
    }

    /// <summary>The width of <paramref name="text"/> in <paramref name="font"/>.</summary>
    int32_t TextWidth(MCGuiFont& font, std::string text)
    {
        return font.Width(text.data());
    }
}

auto MCTacticalMap::Draw() -> void
{
    if (IsShowing() == 0)
    {
        return;
    }

    // The page's background, drawn with its holes (colour 0xff) open.
    switch (DisplayType)
    {
        case MCTacmapPage::Map:
        {
            MapBackground->CopyTo(DisplayPort->Frame(), 0, 0, true);
            DrawMapPage();
            break;
        }
        case MCTacmapPage::Info:
        {
            if (LockObject(InfoObject) == nullptr)
            {
                VfxPaneWipe(DisplayPort->Frame(), 0x10);
            }

            InfoBackground->CopyTo(DisplayPort->Frame(), 0, 0, true);
            DrawInfoPage();
            break;
        }
        case MCTacmapPage::Mission:
        {
            MissionBackground->CopyTo(DisplayPort->Frame(), 0, 0, true);
            break;
        }
        case MCTacmapPage::Salvage:
        {
            SalvageBackground().CopyTo(DisplayPort->Frame(), 0, 0, true);
            break;
        }
    }

    // The status line: a button's help, or the default text.
    FillBox(0x15, 0xe2, 0x87, 0xe8, 0x10);
    WriteText(*BlueFont, Port().Frame(), 0x15, 0xe2, StatusText.has_value() ? *StatusText : TableString(0x97));
    MCGuiObject::Draw();
}

auto MCTacticalMap::DrawInfoPage() -> void
{
    const std::shared_ptr<MCGameObject> obj = LockObject(InfoObject);

    if (obj == nullptr || !IsMoverClass(obj->ObjectClass))
    {
        FillBox(0xe, 0x2a, 0x2c, 0x4c, 0x10);
        return;
    }

    std::shared_ptr<MCMechWarrior> pilot = obj->GetPilot();
    const auto mover = std::static_pointer_cast<MCMover>(obj);
    const bool showPilot = mover->NetPlayerId >= 0;

    if (DataDisplayMode == 2)
    {
        // Payload: the weapon list's background (the list is the info text).
        if (InfoViewBackgrounds[1] != nullptr)
        {
            VfxPaneCopy(InfoViewBackgrounds[1]->Frame(), 0, 0, DisplayPort->Frame(), 6, 0x5d, -1);
        }
    }
    else
    {
        // Armor: the part diagram, over the home side's or the enemy's background.
        const std::shared_ptr<MCGuiPort> background = InfoViewBackgrounds[obj->GetTeam() == HomeTeam() ? 0 : 2];

        if (background != nullptr)
        {
            VfxPaneCopy(background->Frame(), 0, 0, DisplayPort->Frame(), 6, 0x5d, -1);
        }

        const int32_t shape = obj->ObjectClass == MCObjectClass::BattleMech
                                  ? mover->NumArmorLocations() + 1 + mover->NumBodyLocations()
                                  : mover->NumBodyLocations();
        AGShapeDraw(Port().Frame(), PartShapes.Bytes(), shape, 0x22, 0x65);
        DrawParts();
    }

    DrawBar();

    if (obj->ObjectClass == MCObjectClass::BattleMech)
    {
        if (showPilot)
        {
            DrawPilot(*pilot);
        }
        else
        {
            FillBox(6, 0x2a, 0x88, 0x4c, 0x10);
        }

        // "<name> <weight class> <tons>".
        const auto tonnage = static_cast<int32_t>(obj->GetTonnage());
        uint32_t classId = 0x85;

        if (tonnage < 0x28)
        {
            classId = 0x82;
        }
        else if (tonnage < 0x3c)
        {
            classId = 0x83;
        }
        else if (tonnage < 0x50)
        {
            classId = 0x84;
        }

        std::string weightClass = TableString(classId);

        if (weightClass.size() > 9)
        {
            weightClass.resize(9);
        }

        // The original's line held 63 characters.
        std::string line =
            MCFormatPrintf(TableString(0x81).c_str(), mover->GetIfaceName().data(), weightClass.c_str(), tonnage);
        line.resize(std::min<size_t>(line.size(), 63));
        WriteText(*BlueFont, Port().Frame(), 0x47 - TextWidth(*BlueFont, line) / 2, 0x54, line);
    }
    else if (obj->ObjectClass == MCObjectClass::GroundVehicle)
    {
        FillBox(6, 0x2a, 0x88, 0x4c, 0x10);
        std::string line(mover->GetIfaceName());
        line.resize(std::min<size_t>(line.size(), 63));
        // Measured in greenFont, written in blueFont (as the original).
        WriteText(*BlueFont, Port().Frame(), 0x47 - TextWidth(*GreenFont, line) / 2, 0x54, line);

        // The passengers, with their pictures.
        const auto vehicle = std::static_pointer_cast<MCGroundVehicle>(LockObject(InfoObject));
        int32_t shown = 0;
        int32_t xPos = 10;

        for (int32_t seat = 0; seat < vehicle->Seats; seat++)
        {
            const std::shared_ptr<MCMechWarrior> passenger = vehicle->Passengers[seat].lock();

            if (passenger == nullptr)
            {
                continue;
            }

            VfxPaneCopy(InfoPorts[static_cast<size_t>(shown)]->Frame(), 0, 0, DisplayPort->Frame(), xPos, 0xc4, 0xfff);
            const int32_t yPos = 0xa6 - (GreenFont->Height() + 2) * shown;
            WriteText(*GreenFont, Port().Frame(), xPos, yPos, passenger->Name);
            shown++;
            xPos += 0x23;
        }
    }
}

auto MCTacticalMap::DrawMapPage() -> void
{
    // The mission timer (or the "no time limit" text), as UpdateMapPage last wrote it.
    MCPane& page = Port().Frame();

    if (Scenario()->TimeLimit < 0)
    {
        WriteText(*WhiteFont, page, 0x3c, 0xab, TableString(0xbc));
    }
    else if (MapTimeShown)
    {
        FillBox(0x37, 0xaa, 0x88, 0xb2, 0x12);
        WriteText(*(MapTimeRed ? RedFont : WhiteFont), page, 0x3c, 0xab, MapTimeText);
    }

    // The map picture, scrolled and zoomed, mapped onto the map area.
    const int32_t halfWidth = MapWidth >> 1;
    const int32_t halfHeight = MapHeight >> 1;
    const int32_t zoomedHalfWidth = halfWidth / Zoom;
    const int32_t zoomedHalfHeight = halfHeight / Zoom;
    const int32_t left = ((ScrollX - zoomedHalfWidth) + halfWidth) * 0x10000;
    const int32_t right = ((ScrollX - halfWidth) + zoomedHalfWidth + MapWidth) * 0x10000;
    const int32_t top = ((ScrollY - zoomedHalfHeight) + halfHeight) * 0x10000;
    const int32_t bottom = ((ScrollY - halfHeight) + zoomedHalfHeight + MapHeight) * 0x10000;
    std::array<MCScreenVertex, 4> vertices{};
    vertices[0] = {6, 0x22, 0, left, top, 0};
    vertices[1] = {0x88, 0x22, 0, right, top, 0};
    vertices[2] = {0x88, 0xa4, 0, right, bottom, 0};
    vertices[3] = {6, 0xa4, 0, left, bottom, 0};
    VfxMapPolygon(DisplayPort->Frame(), vertices, MapPort->Bitmap(), VfxMapTransparent);

    if (DrawRevealedTacMap == 0)
    {
        // The fog of war: the visible bits, one texel per vertex, over the same area.
        std::array<MCVector3D, 4> corners = {MCVector3D(6.0f, 34.0f, 0.0f), MCVector3D(136.0f, 34.0f, 0.0f),
                                             MCVector3D(136.0f, 164.0f, 0.0f), MCVector3D(6.0f, 164.0f, 0.0f)};
        std::array<int32_t, 4> u{};
        std::array<int32_t, 4> v{};

        for (size_t i = 0; i < corners.size(); i++)
        {
            TacMapToWorld(corners[i], true);
            const float column = (corners[i].X - MCTerrain::MapTopLeft3d100.X) * MCTerrain::OneOvermetersPerVertex;
            const float row = (MCTerrain::MapTopLeft3d100.Y - corners[i].Y) * MCTerrain::OneOvermetersPerVertex;
            u[i] = static_cast<int32_t>(column * 65536.0 + 0.5);
            v[i] = static_cast<int32_t>(row * 65536.0 + 0.5);
        }

        vertices[0] = {6, 0x22, 0, u[0] + 0x20000, v[0] + 0x10000, 0};
        vertices[1] = {0x88, 0x22, 0, u[1] - 0x10000, v[1] + 0x10000, 0};
        vertices[2] = {0x88, 0xa4, 0, u[2] - 0x10000, v[2] - 0x10000, 0};
        vertices[3] = {6, 0xa4, 0, u[3] + 0x10000, v[3] - 0x20000, 0};
        VfxMapPolygon(DisplayPort->Frame(), vertices, VisibilityPort->Bitmap(), VfxMapTransparent);
    }

    // What each camera window sees, as a rectangle in its colour.
    for (int32_t windowNum = 0; windowNum < 4; windowNum++)
    {
        // Port fix: the original read four windows whatever the terrain's NumberOfWindows.
        const std::shared_ptr<MCTerrainWindow> window = Terrain()->GetTerrainWindow(windowNum);
        const std::shared_ptr<MCCamera> camera = window != nullptr ? window->Camera.lock() : nullptr;

        if (camera == nullptr || !camera->Active)
        {
            continue;
        }

        const std::shared_ptr<MCViewWindow> view = camera->View();
        const MCVector2D screenTopLeft(0.0f, 0.0f);
        // Port: the corners of the world surface the view shows (its size follows the zoom).
        const MCVector2D screenBottomRight(static_cast<float>(view->WorldWidth()),
                                           static_cast<float>(view->WorldHeight()));
        MCVector3D topLeft;
        MCVector3D bottomRight;
        camera->InverseProject(screenTopLeft, topLeft);
        camera->InverseProject(screenBottomRight, bottomRight);
        WorldToTacMap(topLeft, true);
        WorldToTacMap(bottomRight, true);
        uint8_t color = 0x1f;

        switch (camera->CameraId)
        {
            case 1:
                color = 0xef;
                break;
            case 2:
                color = 0xb;
                break;
            case 3:
                color = 0xf2;
                break;
            case 4:
                color = 0xc;
                break;
        }

        MCPane& pane = MapPane;
        const auto paneLeft = static_cast<float>(pane.X0);
        const auto paneTop = static_cast<float>(pane.Y0);
        const auto x0 = static_cast<int32_t>(topLeft.X - paneLeft);
        const auto y0 = static_cast<int32_t>(topLeft.Y - paneTop);
        const auto x1 = static_cast<int32_t>(bottomRight.X - paneLeft);
        const auto y1 = static_cast<int32_t>(bottomRight.Y - paneTop);
        VfxLineDraw(pane, x0, y1, x1, y1, color);
        VfxLineDraw(pane, x1, y0, x1, y1, color);
        VfxLineDraw(pane, x1, y0, x0, y0, color);
        VfxLineDraw(pane, x0, y0, x0, y1, color);
    }

    DrawObjects();
}

auto MCTacticalMap::DrawObjects() -> void
{
    // (The markers' blink is stepped by UpdateMapPage, and the objectives' areas revealed by RevealObjectives: the
    // original did both here.)

    // The home side's pending objectives that have a position: a numbered dot.
    const auto numObjectives = static_cast<int32_t>(HomeTeam()->NumObjectives);

    for (int32_t i = 0; i < numObjectives; i++)
    {
        const MCScenarioObjective& objective = Scenario()->Objectives[HomeTeam()->FirstObjective + i];

        if (objective.Position[0] == -99.0f || objective.Position[1] == -99.0f || objective.Position[2] == -99.0f ||
            objective.Status != 0 || !MarkersLit)
        {
            continue;
        }

        const auto [xPos, yPos] = MapPixel(*this, MCVector3D(objective.Position[0], objective.Position[1], 0.0f));
        AGEllipseFill(MapPane, xPos, yPos, 2, 2, 0x1f);
        // The pending objective's colour (the status is 0 here).
        LineFont()->Print(xPos, yPos, std::format("{}", i + 1), 0xf2, MapPane);
    }

    // The sensor contacts: a dot (dark when not identified), and with the ranges on, the unit's sensor range.
    const int32_t homeAlignment = HomeTeam()->Alignment;
    std::vector<std::shared_ptr<MCBigGameObject>> contacts = HomeTeam()->GetSensorContacts();

    for (const std::shared_ptr<MCBigGameObject>& obj : contacts)
    {
        int tagged = 0;
        obj->GetContactType(HomeTeam()->Id, tagged);

        if (obj->GetAwake() == 0 || obj->IsDisabled() != 0 || obj->InTransport() != 0)
        {
            continue;
        }

        const bool mover = IsMoverClass(obj->ObjectClass);

        if (mover && obj->GetPilot()->Status == 2)
        {
            continue;
        }

        const auto [xPos, yPos] = MapPixel(*this, obj->GetPosition());
        AGEllipseFill(MapPane, xPos, yPos, 2, 2, tagged == 0 ? 10 : 0xcf);

        if (ShowRanges && mover)
        {
            DrawSensorRange(*this, *(obj.get()), xPos, yPos, homeAlignment);
        }
    }

    // The contacts in line of sight (mechs and vehicles).
    contacts = HomeTeam()->GetLosContacts();

    for (const std::shared_ptr<MCBigGameObject>& obj : contacts)
    {
        if (static_cast<int32_t>(obj->ObjectClass) <= 1 || obj->ObjectClass >= MCObjectClass::Elemental ||
            obj->GetAwake() == 0 || obj->IsDisabled() != 0 || obj->InTransport() != 0 || obj->GetPilot()->Status == 2)
        {
            continue;
        }

        const auto [xPos, yPos] = MapPixel(*this, obj->GetPosition());
        AGEllipseFill(MapPane, xPos, yPos, 2, 2, 0xcf);

        if (ShowRanges)
        {
            DrawSensorRange(*this, *(obj.get()), xPos, yPos, homeAlignment);
        }
    }

    if (ShowRanges)
    {
        // The home side's other sensors (not artillery's), then the enemy's revealed sensor buildings.
        for (const std::weak_ptr<MCSensorSystem>& link : HomeTeam()->Sensors())
        {
            const std::shared_ptr<MCSensorSystem> sensor = link.lock();

            if (sensor->Enabled() == 0 || LockObject(sensor->Owner)->ObjectClass == MCObjectClass::Artillery)
            {
                continue;
            }

            const float range = (sensor->GetSkilledRange() / MetersPerPixel) * WorldUnitsPerMeter;

            if (range <= 0.0)
            {
                continue;
            }

            const uint8_t color = sensor->Multiplier < 1.0 ? 0xec : 0x1f;
            const auto [xPos, yPos] = MapPixel(*this, LockObject(sensor->Owner)->GetPosition());
            const auto radius = static_cast<int32_t>(range);
            AGEllipseDraw(MapPane, xPos, yPos, radius, radius, color);
        }

        std::shared_ptr<MCTeam> enemy = HomeTeam() == InnerSphereTeam() ? ClanTeam() : InnerSphereTeam();

        for (const std::weak_ptr<MCSensorSystem>& link : enemy->Sensors())
        {
            const std::shared_ptr<MCSensorSystem> sensor = link.lock();

            if (LockObject(sensor->Owner)->IsBuilding() == 0 || sensor->Enabled() == 0 ||
                LockObject(sensor->Owner)->IsRevealed() == 0)
            {
                continue;
            }

            const float range = (sensor->GetSkilledRange() / MetersPerPixel) * WorldUnitsPerMeter;

            if (range <= 0.0)
            {
                continue;
            }

            const auto [xPos, yPos] = MapPixel(*this, LockObject(sensor->Owner)->GetPosition());
            const uint8_t color = sensor->Multiplier < 1.0 ? 0xf2 : 0xef;
            const auto radius = static_cast<int32_t>(range);
            AGEllipseDraw(MapPane, xPos, yPos, radius, radius, color);
        }
    }

    // The home side's mechs; the selected ones last, on top.
    std::shared_ptr<MCObjectList> mechList = HomeTeam() == InnerSphereTeam() ? InnerSphereMechList() : ClanMechList();
    std::vector<std::pair<int32_t, int32_t>> selected;

    for (std::shared_ptr<MCBaseObject> node : *mechList)
    {
        const auto obj = std::static_pointer_cast<MCGameObject>(node);

        if (obj->GetAwake() == 0 || obj->IsDisabled() != 0)
        {
            continue;
        }

        const std::pair<int32_t, int32_t> pixel = MapPixel(*this, obj->GetPosition());

        if (!TacticalInterface()->IsSelected(obj->PartId))
        {
            AGEllipseFill(MapPane, pixel.first, pixel.second, 2, 2, 0xf);
        }
        else
        {
            selected.push_back(pixel);
        }
    }

    for (const auto& [xPos, yPos] : selected)
    {
        AGEllipseFill(MapPane, xPos, yPos, 2, 2, 0xb);
    }

    // Artillery strikes: the home side's, and the enemy's in their last 4 seconds, blinking.
    std::shared_ptr<MCObjectList> defaultList = ObjectList()->FindList(MCObjectQueue::DefaultListName);

    if (defaultList == nullptr)
    {
        return;
    }

    for (std::shared_ptr<MCBaseObject> node : *defaultList)
    {
        if (node->ObjectClass != MCObjectClass::Artillery)
        {
            continue;
        }

        const auto strike = std::static_pointer_cast<MCArtillery>(node);
        const bool ours = strike->GetAlignment() == HomeTeam()->Alignment;

        if (!ours && strike->TimeToImpact >= 4.0)
        {
            continue;
        }

        const auto [xPos, yPos] = MapPixel(*this, strike->GetPosition());
        auto& type = static_cast<MCArtilleryType&>(*strike->GetObjectType());
        const auto diameter = static_cast<int32_t>(static_cast<int32_t>(type.NominalMinorRange) * 2);
        auto dotSize = static_cast<int32_t>(static_cast<float>(diameter) / MetersPerPixel);
        int32_t ring = 0;

        if (dotSize == 0)
        {
            // Too small to see: a dot, and while the strike is live, its sensor range.
            dotSize = 2;

            if (strike->TimeToImpact < 0.0)
            {
                int32_t range = 0;

                if (strike->SensorSystem.lock() != nullptr)
                {
                    range = static_cast<int32_t>(strike->SensorSystem.lock()->GetSkilledRange());
                }

                range = static_cast<int32_t>(static_cast<float>(range) / MetersPerPixel);
                ring = static_cast<int32_t>(static_cast<float>(range) * WorldUnitsPerMeter);
            }
        }

        if (MarkersLit)
        {
            continue;
        }

        uint8_t fill = 0xcf;

        if (ours)
        {
            if (ring > 0)
            {
                AGEllipseDraw(MapPane, xPos, yPos, ring, ring,
                              strike->SensorSystem.lock()->Multiplier < 1.0 ? 0xec : 0x1f);
            }

            fill = 0xf;
        }
        else if (ring > 0)
        {
            AGEllipseDraw(MapPane, xPos, yPos, ring, ring, strike->SensorSystem.lock()->Multiplier < 1.0 ? 0xf2 : 0xef);
        }

        AGEllipseFill(MapPane, xPos, yPos, dotSize, dotSize, fill);
    }
}

auto MCTacticalMap::AddSalvageString(MCGameObject& obj) const -> void
{
    MCGuiScrollTextObject& text = *SalvageText;
    // The original's lines held 63 characters.
    const auto print = [&text](std::string line, uint8_t color)
    {
        line.resize(std::min<size_t>(line.size(), 63));
        text.Print(line.data(), color);
    };

    if (obj.ObjectClass == MCObjectClass::BattleMech)
    {
        // A mech: its name, its undamaged weapons, and its sensor if undamaged.
        auto& mech = static_cast<MCMover&>(obj);
        print(std::string(mech.GetIfaceName()), 0xb);
        const uint32_t first = mech.NumOther;
        const uint32_t end = mech.NumWeapons + first;

        for (uint32_t i = first; i < end; i++)
        {
            const MCInventoryItem& item = mech.Inventory[i];

            if (item.Health == static_cast<int8_t>(MasterComponentList[item.MasterID].Health))
            {
                print(std::format("    {}", MasterComponentList[item.MasterID].Abbreviation), 0xc);
            }
        }

        const MCInventoryItem& sensor = mech.Inventory[mech.Sensor];

        if (sensor.Health == static_cast<int8_t>(MasterComponentList[sensor.MasterID].Health))
        {
            print(std::format("    {}", MasterComponentList[sensor.MasterID].Abbreviation), 0x1f);
        }
    }
    else if (obj.ObjectClass == MCObjectClass::GroundVehicle)
    {
        // A vehicle: its name and its salvage.
        print(std::string(static_cast<MCMover&>(obj).GetIfaceName()), 0xb);

        for (const MCSalvageItem& item : obj.GetSalvage())
        {
            print(std::format("    {} {}", item.NumItems, MasterComponentList[item.ItemId].Abbreviation), 0x1f);
        }
    }
    else
    {
        if (obj.IsBuilding() == 0)
        {
            SalvageText->ResetPortSize();
            return;
        }

        // A building or tree building: its name, its salvage.
        std::string name;

        if (obj.ObjectClass == MCObjectClass::Building)
        {
            name = static_cast<MCBuilding&>(obj).Name;
        }
        else if (obj.ObjectClass == MCObjectClass::TreeBuilding)
        {
            name = static_cast<MCTreeBuilding&>(obj).Name;
        }

        print(name, 0xb);

        for (const MCSalvageItem& item : obj.GetSalvage())
        {
            print(std::format("    {} {}", item.NumItems, MasterComponentList[item.ItemId].Abbreviation), 0x1f);
        }
    }

    text.PrintBlank(0x1f);
    SalvageText->ResetPortSize();
}

auto MCTacticalMap::DrawBar() -> void
{
    // The unit's effectiveness: green, yellow below half, red at a fifth.
    if (LockObject(InfoObject) == nullptr)
    {
        return;
    }

    const float effectiveness = static_cast<MCMover&>(*LockObject(InfoObject)).GetTotalEffectiveness();
    uint8_t color = 0xb;

    if (!(effectiveness >= 0.5))
    {
        color = effectiveness > 0.2 ? 0xf2 : 0xef;
    }

    FillBox(0x22, 0x5f, 0x6d, 0x61, 0x10);
    const auto length = static_cast<int32_t>(effectiveness * 75.0f);

    if (length != 0)
    {
        FillBox(0x22, 0x5f, static_cast<int16_t>(length + 0x22), 0x61, color);
    }
}

auto MCTacticalMap::DrawParts() -> void
{
    const auto mover = std::static_pointer_cast<MCMover>(LockObject(InfoObject));

    if (mover == nullptr)
    {
        return;
    }

    // The armor locations: a mech's front ones (0..7) or, in the rear view, its rear ones over the rear diagram;
    // other units all of theirs.
    int16_t first = 0;
    int16_t end = mover->ObjectClass == MCObjectClass::BattleMech ? 8 : mover->NumArmorLocations();

    if (DataDisplayMode == 1)
    {
        AGShapeDraw(Port().Frame(), PartShapes.Bytes(), mover->NumBodyLocations() + mover->NumArmorLocations(), 0x22,
                    0x65);
        first = 8;
        end = mover->NumArmorLocations();
    }

    for (int32_t i = first; i < end; i++)
    {
        AGShapeTranslateDraw(Port().Frame(), PartShapes.Bytes(), i, 0x22, 0x65,
                             PartColorTable(*this, ArmorColors[static_cast<size_t>(i)]));
    }

    // A mech's front view also shows its internal structure.
    if (mover->ObjectClass == MCObjectClass::BattleMech && DataDisplayMode == 0)
    {
        for (int32_t i = 0; i < mover->NumBodyLocations(); i++)
        {
            const int8_t numArmor = mover->NumArmorLocations();
            AGShapeTranslateDraw(Port().Frame(), PartShapes.Bytes(), static_cast<int16_t>(numArmor + i), 0x22, 0x65,
                                 PartColorTable(*this, BodyColors[static_cast<size_t>(i)]));
        }
    }
}

auto MCTacticalMap::GetColors() -> void
{
    const auto mover = std::static_pointer_cast<MCMover>(LockObject(InfoObject));

    if (mover == nullptr)
    {
        return;
    }

    for (int32_t i = 0; i < mover->NumBodyLocations(); i++)
    {
        const MCBodyLocation& location = mover->BodyAt(i);
        BodyColors[static_cast<size_t>(i)] =
            location.DamageState == 2 ? 0x19
                                      : DamageColor(location.CurInternalStructure, location.MaxInternalStructure);
    }

    for (int32_t i = 0; i < mover->NumArmorLocations(); i++)
    {
        const MCArmorLocation& location = mover->Armor[i];
        ArmorColors[static_cast<size_t>(i)] =
            location.CurArmor == 0.0 ? 0x19 : DamageColor(location.CurArmor, location.MaxArmor);
    }
}

auto MCTacticalMap::DrawPilot(MCMechWarrior& pilot) -> void
{
    // The picture.
    static constexpr std::array<int32_t, 4> skillOrder = {3, 0, 1, 2};
    FillBox(10, 0x2e, 0x21, 0x4c, 0x10);

    if (static_cast<MCMover&>(*LockObject(pilot.Vehicle)).NetPlayerId >= 0)
    {
        VfxPaneCopy(InfoPorts[0]->Frame(), 0, 0, DisplayPort->Frame(), 10, 0x2e, 0xfff);
    }

    // Four skill bars, 55 pixels at the best skill, each an outlined, shaded bar.
    MCPane& frame = Port().Frame();
    int32_t yPos = 0x2f;

    for (const int32_t skill : skillOrder)
    {
        const auto value = static_cast<float>(pilot.Skills[skill]);
        const auto length = static_cast<int32_t>(((value - MinPilotSkill) * 55.0f) / (MaxPilotSkill - MinPilotSkill));
        const int32_t barEnd = length + 0x4c;
        VfxLineDraw(frame, 0x4e, yPos, 0x4e, yPos + 1, 0xe3);
        VfxLineDraw(frame, 0x4f, yPos - 1, barEnd, yPos - 1, 0xe3);
        AGPixelWrite(frame, length + 0x4d, yPos - 1, 0x10);
        VfxLineDraw(frame, length + 0x4e, yPos - 1, length + 0x4e, yPos + 2, 0x10);
        AGPixelWrite(frame, length + 0x4d, yPos + 2, 0x10);
        VfxLineDraw(frame, length + 0x4d, yPos, length + 0x4d, yPos + 1, 0xe3);
        VfxLineDraw(frame, 0x4f, yPos + 2, barEnd, yPos + 2, 0xe5);
        VfxLineDraw(frame, 0x4f, yPos, barEnd, yPos, 0xe4);
        VfxLineDraw(frame, 0x4f, yPos + 1, barEnd, yPos + 1, 0xe4);
        yPos += 8;
    }

    // The wounds blank out health pips, right to left.
    int16_t pipX = 0x1b;

    for (int32_t i = 0; i < 6 && static_cast<float>(i) < pilot.Wounds; i++)
    {
        FillBox(pipX, 0x2b, static_cast<int16_t>(pipX + 1), 0x2c, 0x10);
        pipX = static_cast<int16_t>(pipX - 3);
    }

    // The callsign and the rank. Port fix: a rank above 3 printed an uninitialised buffer; it prints nothing.
    WriteText(*WhiteFont, frame, 10, 0x23, pilot.Callsign);
    WriteText(*WhiteFont, frame, 0x33, 0x23, pilot.Rank <= 3 ? TableString(0x86 + pilot.Rank) : std::string());
}

auto MCTacticalMap::DrawWeapons() const -> void
{
    MCGuiScrollTextObject& text = *InfoText;
    const int32_t firstPixel = text.FirstPixel;
    text.Clear();
    const auto mover = std::static_pointer_cast<MCMover>(LockObject(InfoObject));

    if (mover == nullptr || !IsMoverClass(mover->ObjectClass))
    {
        return;
    }

    // The weapons, sorted by range bracket (up to 75, 150, beyond) and then damage.
    const int32_t numWeapons = std::max<int32_t>(mover->NumWeapons, 0);
    std::vector<MCWeaponEntry> weapons(static_cast<size_t>(numWeapons));
    const int32_t firstWeapon = mover->NumOther;

    for (int32_t i = firstWeapon; i < numWeapons + firstWeapon; i++)
    {
        const MCInventoryItem& item = mover->Inventory[i];
        MCWeaponEntry& entry = weapons[static_cast<size_t>(i - firstWeapon)];
        entry.MasterID = item.MasterID;
        const MCMasterComponent& component = MasterComponentList[item.MasterID];
        const float longRange = component.WeaponRange[3];
        int16_t bracket = 0;

        if (longRange > 150.0f)
        {
            bracket = 20000;
        }
        else if (longRange > 75.0f)
        {
            bracket = 10000;
        }

        entry.SortKey = static_cast<int16_t>(component.Damage + static_cast<float>(bracket));

        if (item.Disabled != 0 || static_cast<int32_t>(item.Health) < static_cast<int8_t>(component.Health))
        {
            entry.State = 0xff;
        }
        else
        {
            entry.State = mover->GetWeaponShots(i) == 0 ? 0 : 1;
        }
    }

    // The original's qsort (rule R5: equal keys keep its order).
    MCMsvcSort(std::span(weapons),
               [](const MCWeaponEntry& a, const MCWeaponEntry& b)
               {
                   const auto keyA = static_cast<float>(a.SortKey);
                   const auto keyB = static_cast<float>(b.SortKey);

                   if (keyA == keyB)
                   {
                       return 0;
                   }

                   return keyB < keyA ? 1 : -1;
               });

    // Three sections under their headers: red when damaged, yellow without ammo, green ready. Clan weapons get the
    // bracket's clan icon.
    static constexpr std::array<int16_t, 3> bracketLimits = {0x4b, 0x96, 0xe1};
    static constexpr std::array<uint32_t, 3> headerIds = {0x55, 0x50, 0x6d};
    static constexpr std::array<char, 3> bracketIcons = {'{', '|', '}'};
    std::array<int32_t, 4> sectionCounts{};
    int32_t next = 0;
    uint8_t color = 0;

    for (size_t bracket = 0; bracket < 3; bracket++)
    {
        text.Print(TableString(headerIds[bracket]).data(), 0x1f);
        const auto limit = static_cast<float>(bracketLimits[bracket]);

        for (; next < numWeapons; next++)
        {
            const MCWeaponEntry& entry = weapons[static_cast<size_t>(next)];
            const MCMasterComponent& component = MasterComponentList[entry.MasterID];

            if (component.WeaponRange[3] > limit && bracket != 2)
            {
                break;
            }

            if (entry.State == 0xff)
            {
                color = 0xef;
            }
            else if (entry.State == 0)
            {
                color = 0xf2;
            }
            else if (entry.State == 1)
            {
                color = 0xc;
            }

            std::string line = std::format("{}    {}", bracketIcons[bracket], component.Abbreviation);

            if (component.TechBase == 1)
            {
                line[0] = static_cast<char>(0x1d + bracket);
            }

            text.Print(line.data(), color);
            sectionCounts[bracket]++;
        }
    }

    // The equipment: sensor, ECM, jammer, probe; red when disabled or destroyed.
    text.Print(TableString(0x37e).data(), 0x1f);
    const std::array<uint8_t, 4> equipment = {mover->Sensor, mover->Ecm, mover->Jammer, mover->Probe};

    for (const uint8_t index : equipment)
    {
        if (index == 0xff)
        {
            continue;
        }

        const MCInventoryItem& item = mover->Inventory[index];
        color = (item.Disabled != 0 || item.Health == 0) ? 0xef : 0xc;
        std::string line = std::format("    {}", MasterComponentList[item.MasterID].Abbreviation);
        text.Print(line.data(), color);
    }

    // The ammo: red when out, yellow under half.
    const std::string amountFormat = TableString(0x380);

    for (int32_t i = 0; i < mover->NumAmmoTypes(); i++)
    {
        const MCAmmoTally& ammo = mover->AmmoTypeTotal[i];
        const int32_t amount = ammo.CurAmount;

        if (amount == 9999)
        {
            continue;
        }

        if (amount == 0)
        {
            color = 0xef;
        }
        else
        {
            color = ammo.StartAmount / 2 <= amount ? 0xc : 0xf2;
        }

        std::string line = std::format("  {}", MasterComponentList[ammo.MasterId].Abbreviation);
        text.Print(line.data(), color);
        line = MCFormatPrintf(amountFormat.c_str(), amount);
        text.Print(line.data(), color);
    }

    // The weapon sections take the range colours.
    int32_t start = 0;

    for (size_t i = 0; i < 4; i++)
    {
        text.SectionStarts[i] = start;
        start += 1 + sectionCounts[i];
        text.SectionColors[i] = RangeColors[i];
    }

    text.FirstPixel = firstPixel;
    text.ResetPortSize();
    text.PositionScrollTab();
}

#include "stdafx.h"
#include "sprite/MCGVAppearance.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCPolygonElement.h"
#include "engine/MCVfxElement.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCNightLights.h"
#include "object/MCArtillery.h"
#include "object/MCArtilleryType.h"
#include "object/MCArtilleryChunk.h"
#include "object/MCCameraDrone.h"
#include "object/MCCameraDroneType.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCForces.h"
#include "object/MCTrain.h"
#include "object/MCTrainCar.h"
#include "object/MCTrainCarType.h"
#include "object/MCTrainManager.h"
#include "object/MCTurret.h"
#include "object/MCTurretType.h"
#include "sprite/MCParticles.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteMath.h"
#include "sprite/MCWeather.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

MCGVAppearance::~MCGVAppearance()
{
    if (AppearType != nullptr)
    {
        AppearanceTypeList().RemoveAppearance(*AppearType);
    }
}

auto MCGVAppearance::Init(std::shared_ptr<MCAppearanceType> tree, std::weak_ptr<MCGameObject> obj) -> int32_t
{
    Owner = std::move(obj);
    AppearType = std::static_pointer_cast<MCGVAppearanceType>(std::move(tree));

    if (AppearType != nullptr)
    {
        NumParts = static_cast<int32_t>(AppearType->NumParts);
        TurretOffset = AppearType->TurretOffset;
    }

    CurrentShape.fill({});
    CurrentFrame.fill(-1);
    Visible = false;
    ShapeMinY = -25.0f;
    ShapeMinX = -25.0f;
    CurrentTime = 0.0f;
    LastFrame = 0;
    CurrentState = MCGVActorState::Normal;
    InView = false;
    BodyRotation = 0.0f;
    ShapeMaxY = 50.0f;
    ShapeMaxX = 50.0f;
    FrameRate = 15.0f;
    FadeTableIndex = -1;
    return 0;
}

auto MCGVAppearance::RecalcBounds(MCOptionalRef<MCCamera> view) -> int
{
    if (!view.has_value())
    {
        return 0;
    }

    const MCCamera& cam = view->get();
    const std::shared_ptr<MCGameObject> obj = Owner.lock();
    const MCVector2D pos = obj->GetScreenPos(cam.CameraId - 1);
    float x = pos.X;
    float y = pos.Y;

    if (obj->ObjectClass == MCObjectClass::Turret)
    {
        x = static_cast<float>(static_cast<MCTurret&>(*obj).TileOffsetX) + x;
        y = static_cast<float>(static_cast<MCTurret&>(*obj).TileOffsetY) + y;
    }

    UpperLeft.X = x;
    UpperLeft.Y = y;
    LowerRight.Y = y;
    LowerRight.X = x;

    if (const std::shared_ptr<MCShape> body = CurrentShape[0].lock(); body != nullptr && !body->FrameList().empty())
    {
        // The bounds only ever grow: they cover every body frame drawn so far.
        const std::span<const uint8_t> shapeTable = body->FrameList();
        const MCFrameBounds bounds = MCShapeFrameBounds(shapeTable, MCClampShapeFrame(shapeTable, CurrentFrame[0]));
        MCGrowBounds(bounds, ShapeMinX, ShapeMinY, ShapeMaxX, ShapeMaxY);
        InView = true;
    }

    // Faithful: the screen limits are the camera's, not rounded down here.
    UpperLeft.X = ShapeMinX + x;
    UpperLeft.Y = ShapeMinY + y;
    LowerRight.X = ShapeMaxX + UpperLeft.X;
    LowerRight.Y = ShapeMaxY + UpperLeft.Y;

    if (0.0f <= LowerRight.X && 0.0f <= LowerRight.Y && UpperLeft.X <= cam.ViewWidth && UpperLeft.Y <= cam.ViewHeight)
    {
        return 1;
    }

    return 0;
}

auto MCGVAppearance::Render(int32_t depthFixup) -> int32_t
{
    const std::shared_ptr<MCGameObject> obj = Owner.lock();
    ScreenPos = obj->GetScreenPos(Eye()->CameraId - 1);
    const MCObjectClass objectClass = obj->ObjectClass;

    if (objectClass == MCObjectClass::Turret)
    {
        ScreenPos.X = static_cast<float>(static_cast<MCTurret&>(*obj).TileOffsetX) + ScreenPos.X;
        ScreenPos.Y = static_cast<float>(static_cast<MCTurret&>(*obj).TileOffsetY) + ScreenPos.Y;
    }

    // The body's and the turret's facings.
    if (objectClass == MCObjectClass::GroundVehicle)
    {
        const double body = MCActorFacing(*obj) + 5.0;
        BodyRotation = static_cast<float>(body);
        TurretRotation = static_cast<float>(body + static_cast<MCGroundVehicle&>(*obj).TurretRotation);
    }
    else if (objectClass == MCObjectClass::Turret)
    {
        const float rotation = static_cast<MCTurret&>(*obj).TurretRotation;
        TurretRotation = rotation;
        BodyRotation = rotation;
    }
    else if (objectClass == MCObjectClass::TrainCar)
    {
        BodyRotation = static_cast<float>(MCActorFacing(*obj) + 10.0);
        TurretRotation = 0.0f;
    }
    else if (objectClass == MCObjectClass::CameraDrone)
    {
        TurretRotation = 0.0f;
        // Faithful: 45 and -134 swap (-135 is left as it is).
        auto whole = static_cast<int32_t>(MCActorFacing(*obj));

        if (whole == 45)
        {
            whole = -135;
        }
        else if (whole == -134)
        {
            whole = 45;
        }

        BodyRotation = static_cast<float>(whole);
    }

    while (TurretRotation > 180.0f)
    {
        TurretRotation -= 360.0f;
    }

    while (TurretRotation < -180.0f)
    {
        TurretRotation += 360.0f;
    }

    if (TurretRotation < 0.0f)
    {
        TurretRotation += 360.0f;
    }

    // The body snaps to its nearest drawn rotation.
    const MCGVActorState state = CurrentState;
    const int32_t numRotations = AppearType->StateData(state).NumRotations + 1;
    const int32_t rotationIndex = static_cast<int16_t>(
        static_cast<int32_t>(std::floor(static_cast<double>(numRotations) * BodyRotation * (1.0 / 360.0))));
    BodyRotation = static_cast<float>(360.0 / numRotations * rotationIndex);
    std::array<std::shared_ptr<MCShape>, 2> shapes;
    shapes[0] = AppearType->GetShape(state, static_cast<int32_t>(BodyRotation), 0, FrameRate);

    if (static_cast<uint32_t>(NumParts) >= 2 && CurrentState != MCGVActorState::Destroyed)
    {
        shapes[1] = AppearType->GetShape(CurrentState, static_cast<int32_t>(TurretRotation), 1, FrameRate);
    }

    CurrentShape[0] = shapes[0];
    CurrentShape[1] = shapes[1];

    const auto depth = static_cast<int32_t>(static_cast<float>(depthFixup) - ScreenPos.Y);
    ElementList().OpenGroup(depth, false);
    // Port-only (HD lighting's headlights): a ground vehicle's parts stand on its feet, as it casts shadows (below).
    const std::optional<float> feet =
        objectClass == MCObjectClass::GroundVehicle ? MCSceneLights::FeetRow(*this, obj->Position) : std::nullopt;
    // Port-only (sun shadows): the parts cast as the vehicle's own shadow at dusk.
    std::array<MCSceneLights::MCSunShadowPart, std::tuple_size_v<decltype(shapes)>> sunShadow{};

    for (int32_t part = 0; part < NumParts; part++)
    {
        const std::shared_ptr<MCShape>& shape = shapes[part];

        if (shape == nullptr || shape->FrameList().empty())
        {
            continue;
        }

        float offsetX = 0.0f;
        float offsetY = 0.0f;

        if (part == 1)
        {
            // The turret sits turretOffset meters forward of the body's centre.
            const double distance = static_cast<double>(TurretOffset) * WorldUnitsPerMeter;
            offsetX = static_cast<float>(distance * std::sin(BodyRotation * 0x1.1df46a2526c7ap-6));
            offsetY = static_cast<float>(distance * std::cos(BodyRotation * 0x1.1df46a2526c7ap-6) * 0.5);
        }

        std::optional<MCColorTable> fadeTable;

        if (FadeTableIndex >= 0)
        {
            fadeTable = GamePalette().GetFadeTable(FadeTableIndex);
        }

        auto& element = ElementList().Make<MCVfxElement>(shape->FrameList(), ScreenPos.X - offsetX,
                                                         ScreenPos.Y - offsetY, CurrentFrame[part], 0, fadeTable, 0);
        element.Feet = feet;
        ElementList().Add(element);
        sunShadow[static_cast<size_t>(part)] = {
            shape->FrameList(), ScreenPos.X - offsetX, ScreenPos.Y - offsetY, CurrentFrame[part], 0, 0};
    }

    MCSceneLights::AddRecastShadows(sunShadow, depth, false);

    // Port-only (HD weather): a ground vehicle's parts take the rain (a train's cars, an artillery strike's shells and a
    // camera drone don't).
    if (objectClass == MCObjectClass::GroundVehicle)
    {
        MCWeather::AddUnit(sunShadow, obj->Position, ScreenPos.X, ScreenPos.Y, depth);
    }

    const int32_t selected = obj->Selected;

    if (selected == -1 || selected == 1)
    {
        RecalcBounds(EyeRef());
        DrawBars();
    }
    else
    {
        if (selected == 2)
        {
            RecalcBounds(EyeRef());
            const std::shared_ptr<MCGameObject>& selectedObj = obj;
            const int32_t alignment = selectedObj->GetAlignment();

            if (alignment == -1)
            {
                DrawSelectBrackets(0xfd);
            }
            else if (alignment == 0)
            {
                DrawSelectBrackets(0xfe);
            }
            else if (alignment == 1)
            {
                DrawSelectBrackets(selectedObj->GetAlignment() == HomeTeam()->Alignment ? 0xfc : 0xfb);
            }
        }

        if (obj->GetNumAttackers() >= 1)
        {
            RecalcBounds(EyeRef());
            DrawBars();
        }
    }

    if (DrawTerrainGrid != 0)
    {
        RecalcBounds(EyeRef());
        DrawSelectBox(0xfd);
    }

    // Port-only (HD lighting): a vehicle casts shadows from the lights by it, and at night, while it runs, shines its
    // headlight, unless its LIGHTS are off (mission/MCNightLights.h). (HD particles): it wades through water.
    if (objectClass == MCObjectClass::GroundVehicle)
    {
        MCSceneLights::AddCaster(*this, obj->Position);

        if (MCNightLights::Shines(static_cast<const MCMover&>(*obj)))
        {
            MCSceneLights::AddHeadlight(*this, obj->Position, obj->GetFrame(), 0.0f, std::nullopt, depthFixup);
        }

        MCParticles::AddMover(MCAddressKey::Of(*this), obj->Position, ShapeMaxX * 0.5f);
    }

    if (Highlighting)
    {
        if (HighlightTime > 0.0f)
        {
            HighlightTime -= FrameLength;
            DrawSelectBox(0xfc);
            return 0;
        }

        Highlighted = true;
    }

    return 0;
}

auto MCGVAppearance::Update() -> int32_t
{
    for (int32_t i = 0; i < NumParts; i++)
    {
        if (CurrentFrame[i] == -1)
        {
            CurrentFrame[i] = 0;
        }
    }

    if (Visible && Owner.lock()->IsCaptured() != 0 && !Highlighted && !Highlighting)
    {
        Highlighting = true;
        HighlightTime = 3.0f;
    }

    CurrentTime = FrameLength + CurrentTime;
    const auto wholeFrames = static_cast<int32_t>(std::floor(static_cast<double>(CurrentTime * FrameRate)));

    if (LastFrame < wholeFrames)
    {
        const int32_t played = LastFrame;
        LastFrame = wholeFrames;
        const int32_t advanced = wholeFrames - played;

        if (advanced != 0)
        {
            // The turret shows the body's frame.
            const auto frame = static_cast<uint32_t>(CurrentFrame[0] + advanced);
            CurrentFrame[0] = static_cast<int32_t>(frame);
            const uint32_t numFrames = AppearType->StateData(CurrentState).NumFrames;

            if (numFrames <= frame)
            {
                CurrentFrame[0] = static_cast<int32_t>(frame % numFrames);
                return 0;
            }

            CurrentFrame[1] = static_cast<int32_t>(frame);
        }
    }

    return 1;
}

auto MCGVAppearance::StateExists(MCGVActorState state) const -> int32_t
{
    const int32_t numStates = (AppearType->HasExtraState ? 1 : 0) + 3;

    if (static_cast<int32_t>(state) < numStates && static_cast<int32_t>(state) >= 0)
    {
        return static_cast<int32_t>(AppearType->StateData(state).NumFrames);
    }

    return 0;
}

auto MCGVAppearance::DrawBars() -> void
{
    // Port: the bar is an overlay, on the screen over the view: it follows the sprite through the zoom, its size
    // doesn't change.
    constexpr float barWidth = 38.0f;
    constexpr float barHeight = 4.0f;
    const float barY = (MCOverlayY(UpperLeft.Y) - 6.0f) - barHeight;
    const auto barX = static_cast<float>(std::floor(static_cast<double>(MCOverlayX(ScreenPos.X) - barWidth * 0.5f)));

    // How much of the unit is left, per class.
    const std::shared_ptr<MCGameObject> obj = Owner.lock();
    float health = 0.0f; // Port fix: the original leaves this unset for other classes.
    const MCObjectClass objectClass = obj->ObjectClass;

    if (objectClass == MCObjectClass::BattleMech || objectClass == MCObjectClass::GroundVehicle ||
        objectClass == MCObjectClass::Elemental || objectClass == MCObjectClass::Mover)
    {
        health = static_cast<MCMover&>(*obj).GetTotalEffectiveness();
        auto& vehicle = static_cast<MCGroundVehicle&>(*obj);

        if (obj->ObjectClass == MCObjectClass::GroundVehicle && vehicle.Refitter != 0)
        {
            // A refit vehicle shows the refit points left against the turret's full armor.
            const auto capacity = static_cast<float>(vehicle.Armor[GroundVehicleTurret].MaxArmor);
            health = vehicle.GetRefitPoints() / capacity * health;
        }
    }
    else if (objectClass == MCObjectClass::Turret)
    {
        int32_t damage = static_cast<int32_t>(obj->GetDamage());
        const auto dmgLevel = static_cast<int32_t>(static_cast<MCTurretType&>(*obj->GetObjectType()).DmgLevel);

        if (dmgLevel < damage)
        {
            damage = dmgLevel;
        }

        health = 1.0f - static_cast<float>(damage) / static_cast<float>(dmgLevel);
    }
    else if (objectClass == MCObjectClass::TrainCar)
    {
        int32_t damage = static_cast<int32_t>(obj->GetDamage());
        const int32_t dmgLevel = static_cast<MCTrainCarType&>(*obj->GetObjectType()).Damage;

        if (dmgLevel < damage)
        {
            damage = dmgLevel;
        }

        // Original behaviour (OB-054): the damage taken, not the health left.
        health = static_cast<float>(damage) / static_cast<float>(dmgLevel);
    }
    else if (objectClass == MCObjectClass::CameraDrone)
    {
        health = 1.0f;

        if (static_cast<MCCameraDrone&>(*obj).HitPoints < 1)
        {
            health = 0.0f;
        }
    }

    // Green, then yellow below half, red at a fifth.
    int32_t barColor;

    if (health < 0.5)
    {
        barColor = health <= 0.2 ? 0x103 : 0x102;
    }
    else
    {
        barColor = 0x101;
    }

    ElementList().OpenGroup(-50000, true);
    MCPolyElementData data;
    data.NumVertices = 0;
    data.TextureMapOff = false;
    data.Texture = {};
    data.FadeTable = std::nullopt;
    data.Translate = false;
    data.StatusBar = true;
    data.BarColor = barColor;
    data.Vertices[0].X = static_cast<int32_t>(barX - 1.0f);
    data.Vertices[0].Y = static_cast<int32_t>(barY - 1.0f);
    data.Vertices[1].X = static_cast<int32_t>(barX + barWidth + 1.0f);
    data.Vertices[1].Y = static_cast<int32_t>(barY + barHeight + 1.0f);
    float barLength = health * barWidth;

    // A unit that isn't quite dead shows at least one pixel.
    if (barLength < 1.0 && health > 0.001)
    {
        barLength = 1.0f;
    }

    data.BarPercent = static_cast<int32_t>(barLength);
    data.MeasuredBar = barLength;

    if (data.BarPercent > 0)
    {
        ElementList().Add(ElementList().Make<MCPolygonElement>(data, -50000));
    }
}

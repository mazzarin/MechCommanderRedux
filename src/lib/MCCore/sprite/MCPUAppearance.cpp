#include "stdafx.h"
#include "sprite/MCPUAppearance.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCPolygonElement.h"
#include "engine/MCVfxElement.h"
#include "main/MCMissionGlobals.h"
#include "object/MCGate.h"
#include "object/MCGateType.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCForces.h"
#include "object/MCTurret.h"
#include "object/MCTurretType.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteMath.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>
    /// Port-only (SPEC-first-use.md: the camera's surroundings): the state a pop-up's open and close cycle goes to from
    /// <paramref name="state"/> (none from a destroyed one).
    /// </summary>
    std::optional<MCPUActorState> NextInCycle(MCPUActorState state)
    {
        switch (state)
        {
            case MCPUActorState::Closed:
                return MCPUActorState::Opening;
            case MCPUActorState::Opening:
                return MCPUActorState::Open;
            case MCPUActorState::Open:
                return MCPUActorState::Closing;
            case MCPUActorState::Closing:
                return MCPUActorState::Closed;
            default:
                return std::nullopt;
        }
    }

    /// <summary>The pixel offset of a turret or gate; false for other classes.</summary>
    auto PixelOffset(MCGameObject& obj, int32_t& offsetX, int32_t& offsetY) -> bool
    {
        if (obj.ObjectClass == MCObjectClass::Turret)
        {
            offsetX = static_cast<MCTurret&>(obj).TileOffsetX;
            offsetY = static_cast<MCTurret&>(obj).TileOffsetY;
            return true;
        }

        if (obj.ObjectClass == MCObjectClass::Gate)
        {
            offsetX = static_cast<MCGate&>(obj).PixelOffsetX;
            offsetY = static_cast<MCGate&>(obj).PixelOffsetY;
            return true;
        }

        return false;
    }
}

MCPUAppearance::~MCPUAppearance()
{
    if (AppearType != nullptr)
    {
        AppearanceTypeList().RemoveAppearance(*AppearType);
    }
}

auto MCPUAppearance::Init(std::shared_ptr<MCAppearanceType> tree, std::weak_ptr<MCGameObject> obj) -> int32_t
{
    Owner = std::move(obj);
    AppearType = std::static_pointer_cast<MCPUAppearanceType>(std::move(tree));
    CurrentShape.reset();
    ShapeMinY = -25.0f;
    ShapeMinX = -25.0f;
    Visible = false;
    CurrentTime = 0.0f;
    LastFrame = 0;
    CurrentState = MCPUActorState::Closed;
    InView = false;
    Rotation = 0.0f;
    CurrentFrame = -1;
    ShapeMaxY = 50.0f;
    ShapeMaxX = 50.0f;
    FrameRate = 15.0f;
    return 0;
}

auto MCPUAppearance::RecalcBounds(MCOptionalRef<MCCamera> view) -> int
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
    int32_t offsetX = 0;
    int32_t offsetY = 0;

    if (PixelOffset(*obj, offsetX, offsetY))
    {
        x = static_cast<float>(offsetX) + x;
        y = static_cast<float>(offsetY) + y;
    }

    UpperLeft.X = x;
    UpperLeft.Y = y;
    LowerRight.Y = y;
    LowerRight.X = x;

    // The shape's bounds are taken once, from the first shape seen.
    if (const std::shared_ptr<MCShape> shape = CurrentShape.lock();
        shape != nullptr && !shape->FrameList().empty() && !InView)
    {
        const std::span<const uint8_t> shapeTable = shape->FrameList();
        const int32_t numShapeFrames = VfxShapeCount(shapeTable);

        if (numShapeFrames <= CurrentFrame)
        {
            // Port fix: the original clamps only the object's frame and measures past the shape table.
            CurrentFrame = numShapeFrames - 1;
        }

        MCGrowBounds(MCShapeFrameBounds(shapeTable, MCClampShapeFrame(shapeTable, CurrentFrame)), ShapeMinX, ShapeMinY,
                     ShapeMaxX, ShapeMaxY);
        InView = true;
    }

    UpperLeft.X = ShapeMinX + x;
    UpperLeft.Y = ShapeMinY + y;
    LowerRight.X = ShapeMaxX + UpperLeft.X;
    LowerRight.Y = ShapeMaxY + UpperLeft.Y;

    if (0.0f <= LowerRight.X && 0.0f <= LowerRight.Y &&
        UpperLeft.X <= static_cast<float>(static_cast<int32_t>(std::floor(cam.ViewWidth))) &&
        UpperLeft.Y <= static_cast<float>(static_cast<int32_t>(std::floor(cam.ViewHeight))))
    {
        return 1;
    }

    return 0;
}

auto MCPUAppearance::DrawnRotation(MCGameObject& obj, MCPUActorState state) const -> float
{
    float rotation = Rotation;

    if (obj.ObjectClass == MCObjectClass::GroundVehicle)
    {
        rotation = static_cast<float>(MCActorFacing(obj) + static_cast<MCGroundVehicle&>(obj).TurretRotation);
    }
    else if (obj.ObjectClass == MCObjectClass::Turret)
    {
        rotation = static_cast<MCTurret&>(obj).TurretRotation;
    }

    if (rotation < 0.0)
    {
        rotation = static_cast<float>(rotation + 360.0);
    }

    // Snap to the nearest drawn rotation.
    const int32_t numRotations = AppearType->StateData(state).NumRotations + 1;
    const int32_t rotationIndex = static_cast<int16_t>(static_cast<int32_t>(
        std::floor(static_cast<double>(static_cast<float>(numRotations) * rotation) * (1.0 / 360.0))));
    return static_cast<float>(360.0 / numRotations) * static_cast<float>(rotationIndex);
}

auto MCPUAppearance::ReadAhead() -> void
{
    const std::shared_ptr<MCGameObject> obj = Owner.lock();

    if (obj == nullptr || AppearType == nullptr)
    {
        return;
    }

    // The state's packet Render takes now, and the next state's in the open and close cycle at the same facing (snapped
    // to that state's rotations, as Render will once the state changes).
    const MCAppearanceType::MCAskingAhead asking;
    float frameRate = 0.0f;
    AppearType->GetShape(CurrentState, static_cast<int32_t>(DrawnRotation(*obj, CurrentState)), 0, frameRate);

    if (const std::optional<MCPUActorState> next = NextInCycle(CurrentState); next.has_value())
    {
        AppearType->GetShape(*next, static_cast<int32_t>(DrawnRotation(*obj, *next)), 0, frameRate);
    }
}

auto MCPUAppearance::Render(int32_t depthFixup) -> int32_t
{
    const std::shared_ptr<MCGameObject> obj = Owner.lock();
    ScreenPos = obj->GetScreenPos(Eye()->CameraId - 1);
    int32_t offsetX = 0;
    int32_t offsetY = 0;

    if (PixelOffset(*obj, offsetX, offsetY))
    {
        ScreenPos.X = static_cast<float>(offsetX) + ScreenPos.X;
        ScreenPos.Y = static_cast<float>(offsetY) + ScreenPos.Y;
    }

    Rotation = DrawnRotation(*obj, CurrentState);
    const MCPUActorState state = CurrentState;
    const std::shared_ptr<MCShape> shape = AppearType->GetShape(state, static_cast<int32_t>(Rotation), 0, FrameRate);
    CurrentShape = shape;

    const auto depth = static_cast<int32_t>(static_cast<float>(depthFixup) - ScreenPos.Y);
    ElementList().OpenGroup(depth, false);

    if (DrawTerrainGrid != 0)
    {
        DrawSelectBox(0xfd);
    }

    if (shape != nullptr && !shape->FrameList().empty())
    {
        std::optional<MCColorTable> fadeTable;

        if (FadeTableIndex >= 0)
        {
            fadeTable = GamePalette().GetFadeTable(FadeTableIndex);
        }

        ElementList().Add(ElementList().Make<MCVfxElement>(shape->FrameList(), ScreenPos.X, ScreenPos.Y, CurrentFrame,
                                                           0, fadeTable, 1));
        // Port-only (sun shadows): its own shadow at dusk.
        const MCSceneLights::MCSunShadowPart part{shape->FrameList(), ScreenPos.X, ScreenPos.Y, CurrentFrame};
        MCSceneLights::AddRecastShadows(std::span(&part, 1), depth, false);
    }

    const int32_t selected = obj->Selected;
    bool showBars = selected == -1 || selected == 1;

    if (!showBars)
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

        showBars = obj->GetNumAttackers() >= 1;
    }

    if (showBars)
    {
        RecalcBounds(EyeRef());
        DrawBars();
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

auto MCPUAppearance::SetDestroyed() -> void
{
    // Destroyed closed from closed, destroyed open from any other state.
    CurrentState =
        CurrentState != MCPUActorState::Closed ? MCPUActorState::DestroyedOpen : MCPUActorState::DestroyedClosed;
}

auto MCPUAppearance::SetCombatMode(bool combatMode) -> int32_t
{
    const auto lastOpeningFrame = static_cast<int32_t>(AppearType->StateData(MCPUActorState::Opening).NumFrames) - 1;
    const auto lastClosingFrame = static_cast<int32_t>(AppearType->StateData(MCPUActorState::Closing).NumFrames) - 1;

    // Finish an opening or closing that has played out.
    if (CurrentState == MCPUActorState::Closing && CurrentFrame == lastClosingFrame)
    {
        CurrentState = MCPUActorState::Closed;
        CurrentFrame = -1;
    }

    if (CurrentState == MCPUActorState::Opening && CurrentFrame == lastOpeningFrame)
    {
        CurrentState = MCPUActorState::Open;
        CurrentFrame = -1;
    }

    if (!combatMode)
    {
        if (CurrentState == MCPUActorState::Open)
        {
            CurrentState = MCPUActorState::Closing;
        }

        if (CurrentState == MCPUActorState::Closing && CurrentFrame == lastClosingFrame)
        {
            CurrentState = MCPUActorState::Closed;
            CurrentFrame = -1;
        }
    }
    else
    {
        if (CurrentState == MCPUActorState::Closed)
        {
            CurrentState = MCPUActorState::Opening;
        }

        if (CurrentState == MCPUActorState::Opening && CurrentFrame == lastOpeningFrame)
        {
            CurrentState = MCPUActorState::Open;
            CurrentFrame = -1;
        }

        if (CurrentState == MCPUActorState::Closing)
        {
            // Reopen from the matching point of the opening.
            const double closedShare =
                static_cast<double>(static_cast<uint32_t>(CurrentFrame)) / static_cast<double>(lastClosingFrame);
            CurrentFrame = static_cast<int32_t>(
                (1.0 - closedShare) * static_cast<double>(AppearType->StateData(MCPUActorState::Opening).NumFrames));
            CurrentState = MCPUActorState::Opening;
            return 1;
        }
    }

    return static_cast<int32_t>(CurrentState);
}

auto MCPUAppearance::Update() -> int32_t
{
    if (CurrentFrame == -1)
    {
        CurrentFrame = 0;
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
            const auto frame = static_cast<uint32_t>(CurrentFrame + advanced);
            CurrentFrame = static_cast<int32_t>(frame);
            const uint32_t numFrames = AppearType->StateData(CurrentState).NumFrames;

            if (numFrames <= frame)
            {
                CurrentFrame = static_cast<int32_t>(numFrames - 1);
                return 0;
            }
        }
    }

    return 1;
}

auto MCPUAppearance::StateExists(MCPUActorState state) const -> int32_t
{
    if (static_cast<int32_t>(state) < PUActorStateCount && static_cast<int32_t>(state) >= 0)
    {
        return static_cast<int32_t>(AppearType->StateData(state).NumFrames);
    }

    return 0;
}

auto MCPUAppearance::DrawBars() -> void
{
    // Port: the bar is an overlay, on the screen over the view: it follows the sprite through the zoom, its size
    // doesn't change.
    constexpr float barWidth = 38.0f;
    constexpr float barHeight = 4.0f;
    const float barY = (MCOverlayY(UpperLeft.Y) - 7.0f) - barHeight;
    const auto barX = static_cast<float>(std::floor(static_cast<double>(MCOverlayX(ScreenPos.X) - barWidth * 0.5f)));

    // A turret shows its bar only with its weapon deployed.
    const std::shared_ptr<MCGameObject> obj = Owner.lock();

    if (obj->ObjectClass == MCObjectClass::Turret && static_cast<MCTurret&>(*obj).WeaponDeployed == 0)
    {
        return;
    }

    double health = 0.0; // Port fix: the original leaves this unset for other classes.
    const MCObjectClass objectClass = obj->ObjectClass;

    if (objectClass == MCObjectClass::BattleMech || objectClass == MCObjectClass::GroundVehicle ||
        objectClass == MCObjectClass::Elemental || objectClass == MCObjectClass::Mover)
    {
        auto& mover = static_cast<MCMover&>(*obj);

        if (mover.WeaponEffectiveness < 0.0f || mover.MaxWeaponEffectiveness < mover.WeaponEffectiveness)
        {
            return;
        }

        health = mover.GetTotalEffectiveness();
    }
    else if (objectClass == MCObjectClass::Turret || objectClass == MCObjectClass::Gate)
    {
        int32_t damage = static_cast<int32_t>(obj->GetDamage());
        // Turret and gate types both keep their damage level in the same place.
        const auto dmgLevel = static_cast<int32_t>(static_cast<MCTurretType&>(*obj->GetObjectType()).DmgLevel);

        if (dmgLevel < damage)
        {
            damage = dmgLevel;
        }

        health = 1.0 - static_cast<double>(damage) / static_cast<double>(dmgLevel);
    }

    // Green, then yellow below half, red at a fifth. Port fix: these were palette indices (0x0b, 0xf2, 0xef), but the
    // status bar takes an AlphaTable row, so gates and turrets drew a black bar. The rows are the ones every other
    // bar uses (0x101 green, 0x102 yellow, 0x103 red).
    int32_t barColor;

    if (health < 0.5)
    {
        barColor = health <= 0.2 ? 0x103 : 0x102;
    }
    else
    {
        barColor = 0x101;
    }

    auto barLength = static_cast<float>(health * barWidth);

    // A unit that isn't quite dead shows at least one pixel.
    if (health * barWidth < 1.0 && health > 0.001)
    {
        barLength = 1.0f;
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
    data.BarPercent = static_cast<int32_t>(barLength);
    data.MeasuredBar = barLength;

    if (data.BarPercent > 0)
    {
        ElementList().Add(ElementList().Make<MCPolygonElement>(data, -50000));
    }
}

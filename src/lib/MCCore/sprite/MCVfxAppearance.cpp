#include "stdafx.h"
#include "sprite/MCVfxAppearance.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "color/MCPalette.h"
#include "engine/MCDeltaElement.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCPolygonElement.h"
#include "engine/MCVfxElement.h"
#include "lib/MCFatal.h"
#include "main/MCMissionGlobals.h"
#include "object/MCBigGameObject.h"
#include "object/MCTreeBuilding.h"
#include "object/MCTreeBuildingType.h"
#include "object/MCForces.h"
#include "object/MCTerrainObject.h"
#include "object/MCTerrainObjectType.h"
#include "object/MCTree.h"
#include "object/MCTreeType.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteMath.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>
    /// The pixel offset of the terrain-placed classes (tree, terrain object, the unused class 0x17, tree building).
    /// False for any other class.
    /// </summary>
    auto PixelOffset(MCGameObject& obj, int32_t& offsetX, int32_t& offsetY) -> bool
    {
        switch (obj.ObjectClass)
        {
            case MCObjectClass::Tree:
            {
                offsetX = static_cast<MCTree&>(obj).PixelOffsetX;
                offsetY = static_cast<MCTree&>(obj).PixelOffsetY;
                return true;
            }
            case MCObjectClass::TerrainObject:
            case static_cast<MCObjectClass>(0x17): // No class sets 0x17; the original reads the same offsets.
            {
                offsetX = static_cast<MCTerrainObject&>(obj).PixelOffsetX;
                offsetY = static_cast<MCTerrainObject&>(obj).PixelOffsetY;
                return true;
            }
            case MCObjectClass::TreeBuilding:
            {
                offsetX = static_cast<MCTreeBuilding&>(obj).PixelOffsetX;
                offsetY = static_cast<MCTreeBuilding&>(obj).PixelOffsetY;
                return true;
            }
            default:
                return false;
        }
    }
}

MCVfxAppearance::~MCVfxAppearance()
{
    if (AppearType != nullptr)
    {
        AppearanceTypeList().RemoveAppearance(*AppearType);
    }
}

auto MCVfxAppearance::Init(std::shared_ptr<MCAppearanceType> tree, std::weak_ptr<MCGameObject> obj) -> int32_t
{
    Owner = std::move(obj);
    // A building appearance binds its own type here too (MCVfxBuildingAppearance::BuildType), as the original did.
    AppearType = std::static_pointer_cast<MCVfxAppearanceType>(std::move(tree));
    CurrentShape.reset();
    CurrentFrame = -1;
    LoopEnd = -1;
    LoopStart = -1;
    ShapeMinY = -15.0f;
    ShapeMinX = -15.0f;
    Visible = false;
    CurrentTime = 0.0f;
    LastFrame = 0;
    HazeLevel = 0;
    CurrentState = MCActorState::Normal;
    InView = false;
    CurrentSubState = NoSubState;
    ShapeMaxY = 15.0f;
    ShapeMaxX = 15.0f;
    TypeChanged = true;
    return 0;
}

auto MCVfxAppearance::RecalcBounds(MCOptionalRef<MCCamera> view) -> int
{
    int result = 0;

    if (const std::shared_ptr<MCShape> shape = CurrentShape.lock(); shape != nullptr && !shape->FrameList().empty())
    {
        const MCFrameBounds bounds = MCShapeFrameBounds(shape->FrameList(), 0);
        ShapeMinX = bounds.MinX;
        ShapeMinY = bounds.MinY;
        ShapeMaxX = bounds.Width;
        ShapeMaxY = bounds.Height;
        // Faithful: the shape's packed size is what an off-screen appearance returns.
        result = VfxShapeResolution(shape->FrameList(), 0);
        InView = true;
    }

    if (!view.has_value())
    {
        return result;
    }

    const MCCamera& cam = view->get();
    const std::shared_ptr<MCGameObject> owner = Owner.lock();
    const MCVector2D pos = owner->GetScreenPos(cam.CameraId - 1);
    float x = pos.X;
    float y = pos.Y;
    int32_t offsetX = 0;
    int32_t offsetY = 0;

    if (PixelOffset(*owner, offsetX, offsetY))
    {
        x += static_cast<float>(offsetX);
        y += static_cast<float>(offsetY);
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

    return result;
}

auto MCVfxAppearance::Render(int32_t depthFixup) -> int32_t
{
    const std::shared_ptr<MCGameObject> obj = Owner.lock();
    ScreenPos = obj->GetScreenPos(Eye()->CameraId - 1);
    const double facing = MCActorFacing(*obj);
    const uint8_t subState = CurrentSubState;
    float frameRate = 0.0f;
    bool reverse = false;
    const std::shared_ptr<MCShape> shape =
        AppearType->GetShape(CurrentState, subState, static_cast<int32_t>(facing), frameRate, reverse);
    CurrentShape = shape;

    if (obj->IsCaptured() != 0 && !Highlighted && !Highlighting)
    {
        Highlighting = true;
        HighlightTime = 3.0f;
    }

    int32_t offsetX = 0;
    int32_t offsetY = 0;

    if (PixelOffset(*obj, offsetX, offsetY))
    {
        ScreenPos.X = static_cast<float>(offsetX) + ScreenPos.X;
        ScreenPos.Y = static_cast<float>(offsetY) + ScreenPos.Y;
    }

    const MCVfxAppearanceType& type = *AppearType;
    uint32_t numFrames = type.StateData(CurrentState).NumFrames;

    if (subState != NoSubState)
    {
        numFrames = type.SubStates[subState].NumFrames;
    }

    const int32_t depth = static_cast<int32_t>(static_cast<float>(depthFixup) - ScreenPos.Y);

    const std::optional<MCColorTable> hazeTable = GamePalette().GetHazePalette(HazeLevel);

    if (type.Delta && static_cast<int32_t>(numFrames) >= 2)
    {
        // Delta-compressed animations draw unscaled (only when the type is scaled).
        if (shape != nullptr && !shape->FrameList().empty() && type.Scaled)
        {
            ElementList().OpenGroup(depth, true);
            ElementList().Add(ElementList().Make<MCDeltaElement>(shape->FrameList(), static_cast<int32_t>(ScreenPos.X),
                                                                 static_cast<int32_t>(ScreenPos.Y), CurrentFrame,
                                                                 hazeTable));
        }
    }
    else if (shape != nullptr && !shape->FrameList().empty())
    {
        ElementList().OpenGroup(depth, true);
        // Scaled types have their own zoomed out shapes, so they draw unscaled.
        ElementList().Add(ElementList().Make<MCVfxElement>(shape->FrameList(), ScreenPos.X, ScreenPos.Y, CurrentFrame,
                                                           0, hazeTable, type.Scaled ? 1 : 0));
    }

    if (obj->Selected == 1 || obj->GetNumAttackers() > 0)
    {
        DrawBars();
    }

    if (Highlighting)
    {
        if (HighlightTime <= 0.0f)
        {
            Highlighted = true;
        }
        else
        {
            HighlightTime -= FrameLength;
            DrawSelectBox(0xfc);
        }
    }

    if (obj->Selected != 0)
    {
        const int32_t homeAlignment = HomeTeam()->Alignment;

        if (obj->GetAlignment() == homeAlignment || obj->GetAlignment() == 0 || static_cast<uint8_t>(obj->Status) == 2)
        {
            DrawSelectBox(0xe1);
        }
        else
        {
            DrawSelectBrackets(0xcf);
        }
    }

    if (DrawTerrainGrid != 0)
    {
        RecalcBounds(EyeRef());
        DrawSelectBox(0xe1);
    }

    return 0;
}

auto MCVfxAppearance::SetDamageLvl(uint32_t damageLevel) -> void
{
    DamageSet = true;

    if (damageLevel == 0)
    {
        SetTypeId(MCActorState::Normal, NoSubState);
        return;
    }

    const uint32_t blowUp1Frames = AppearType->StateData(MCActorState::BlowingUp1).NumFrames;

    if (AppearType->StateData(MCActorState::BlowingUp2).NumFrames + blowUp1Frames <= damageLevel)
    {
        SetTypeId(MCActorState::Destroyed, NoSubState);
        return;
    }

    if (blowUp1Frames <= damageLevel)
    {
        SetTypeId(MCActorState::BlowingUp2, NoSubState);
        CurrentFrame = static_cast<int32_t>(damageLevel - blowUp1Frames);
        return;
    }

    SetTypeId(MCActorState::BlowingUp1, NoSubState);
    CurrentFrame = static_cast<int32_t>(damageLevel);
}

auto MCVfxAppearance::Update() -> int32_t
{
    const int32_t startFrame = CurrentFrame;

    if (startFrame == -1)
    {
        CurrentFrame = 0;
    }

    const MCVfxAppearanceType& type = *AppearType;
    const uint8_t subState = CurrentSubState;
    FramesAdvanced = 0.0f;
    const MCActorData& data = type.StateData(CurrentState);
    uint32_t numFrames = data.NumFrames;

    if (subState != NoSubState)
    {
        numFrames = type.SubStates[subState].NumFrames;
    }

    if (numFrames <= 1)
    {
        return 1;
    }

    // Faithful: the state's frame rate, even in a sub-state.
    CurrentTime = FrameLength + CurrentTime;
    const float frames = CurrentTime * data.FrameRate;
    const int32_t played = LastFrame;

    if (frames < static_cast<float>(played))
    {
        Fatal(played, " Frame Count wrong");
    }

    const int32_t wholeFrames = static_cast<int32_t>(std::floor(static_cast<double>(frames)));

    if (played < wholeFrames)
    {
        FramesAdvanced = static_cast<float>(wholeFrames - played);
        LastFrame = wholeFrames;
    }

    if (FramesAdvanced == 0.0f)
    {
        return 1;
    }

    CurrentFrame = static_cast<int32_t>(static_cast<double>(static_cast<uint32_t>(CurrentFrame)) + FramesAdvanced);

    if (startFrame == -1 && type.Delta)
    {
        CurrentFrame = 0;
    }

    uint32_t stateFrames = data.NumFrames;
    bool loop = true;

    if (subState != NoSubState)
    {
        loop = type.SubStates[subState].Loop;
        stateFrames = type.SubStates[subState].NumFrames;
    }

    const auto frameNum = static_cast<uint32_t>(CurrentFrame);

    if (stateFrames <= frameNum)
    {
        if (loop && LoopEnd == -1)
        {
            CurrentFrame = static_cast<int32_t>(frameNum % stateFrames);
            return 0;
        }

        if (!loop)
        {
            CurrentFrame = static_cast<int32_t>(stateFrames - 1);
            return 1;
        }
    }

    if (static_cast<uint32_t>(LoopEnd) <= frameNum)
    {
        CurrentFrame = LoopStart;
        return 0;
    }

    return 1;
}

auto MCVfxAppearance::StateExists(MCActorState state) const -> int32_t
{
    if (static_cast<int32_t>(state) < static_cast<int32_t>(AppearType->NumStates) && static_cast<int32_t>(state) >= 0)
    {
        return static_cast<int32_t>(AppearType->StateData(state).NumFrames);
    }

    return 0;
}

auto MCVfxAppearance::DrawBars() -> void
{
    // Only tree buildings get a damage bar here.
    std::shared_ptr<MCGameObject> obj = Owner.lock();
    MCDrawDamageBar(*this, GetAppearanceType(), obj->ObjectClass == MCObjectClass::TreeBuilding ? obj : nullptr);
}

auto MCDrawDamageBar(const MCAppearance& appearance, std::shared_ptr<MCAppearanceType> type,
                     std::shared_ptr<MCGameObject> obj) -> void
{
    if (obj == nullptr)
    {
        return;
    }

    // The bar sits centred above the type's bounds (or the shape's, when the type has none).
    const MCVector2D& screenPos = appearance.ScreenPos;
    float left;
    float top;
    float right;

    if (type == nullptr || (type->BoundsUpperLeftX == 0 && type->BoundsUpperLeftY == 0 &&
                            type->BoundsLowerRightX == 0 && type->BoundsLowerRightY == 0))
    {
        left = appearance.UpperLeft.X;
        top = appearance.UpperLeft.Y;
        right = appearance.LowerRight.X;
    }
    else
    {
        left = static_cast<float>(type->BoundsUpperLeftX) + screenPos.X;
        top = static_cast<float>(type->BoundsUpperLeftY) + screenPos.Y;
        right = static_cast<float>(type->BoundsLowerRightX) + screenPos.X;
    }

    // Port: the bar is an overlay, on the screen over the view: it follows the sprite through the zoom, its size
    // doesn't change.
    left = MCOverlayX(left);
    top = MCOverlayY(top);
    right = MCOverlayX(right);
    constexpr float gap = 5.0f;
    constexpr float barWidth = 38.0f;
    constexpr float barHeight = 4.0f;
    const float barY = top - gap - barHeight - gap - barHeight;
    const float barX = (left + (right - left) * 0.5f) - barWidth * 0.5f;

    ElementList().OpenGroup(-50000, true);
    MCPolyElementData data;
    data.NumVertices = 0;
    data.TextureMapOff = false;
    data.Texture = {};
    data.FadeTable = std::nullopt;
    data.Translate = false;
    data.StatusBar = true;
    data.BarColor = 0x101;
    data.Vertices[0].X = static_cast<int32_t>(barX - 1.0f);
    data.Vertices[0].Y = static_cast<int32_t>(barY - 1.0f);
    data.Vertices[1].X = static_cast<int32_t>(barX + barWidth + 1.0f);
    data.Vertices[1].Y = static_cast<int32_t>(barY + barHeight + 1.0f);

    // Tree building and building types both keep their damage level in the same place.
    const auto dmgLevel =
        static_cast<float>(static_cast<int32_t>(static_cast<MCTreeBuildingType&>(*obj->GetObjectType()).DmgLevel));
    const float remaining = std::max(dmgLevel - obj->GetDamage(), 0.0f);
    const float ratio = remaining / dmgLevel;
    float barLength = ratio * barWidth;

    // A building that isn't quite destroyed shows at least one pixel.
    if (barLength < 1.0 && ratio > 0.001)
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

auto MCVfxAppearance::SetTypeId(MCActorState state, uint8_t subState) -> void
{
    if (static_cast<int32_t>(state) < static_cast<int32_t>(AppearType->NumStates) && static_cast<int32_t>(state) >= 0)
    {
        CurrentState = state;
        CurrentFrame = -1;
        CurrentTime = 0.0f;
        LastFrame = 0;
        CurrentSubState = NoSubState;

        if (state != MCActorState::Normal)
        {
            TypeChanged = true;
            return;
        }
    }

    if (subState != NoSubState && state == MCActorState::Normal && !AppearType->SubStates.empty())
    {
        CurrentSubState = subState;
    }

    TypeChanged = true;
}

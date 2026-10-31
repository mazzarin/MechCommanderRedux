#include "stdafx.h"
#include "object/MCLaser.h"
#include "camera/MCCamera.h"
#include "engine/MCCraterManager.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCPolygonElement.h"
#include "main/MCMissionGlobals.h"
#include "network/MCMultiPlayer.h"
#include "object/MCLaserType.h"
#include "object/MCObjectSystem.h"
#include "platform/MCRenderer.h"
#include "sprite/MCSceneLights.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfx.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>Radians to degrees, as MCX.EXE stores it.</summary>
    constexpr double RadiansToDegrees = 0x1.ca5dc1a6402aap+5;

    /// <summary>
    /// The 256x256 8-bit buffer a PPC's effect frame is drawn into before its polygons map it along the beam: made
    /// for the first PPC drawn, sized by its shape's edges, and kept for the rest of the run (as MCX.EXE's globals).
    /// </summary>
    struct MCPpcCanvas
    {
        /// <summary>The pixels.</summary>
        std::shared_ptr<uint8_t[]> Pixels = std::make_shared<uint8_t[]>(0x10000);
        /// <summary>The window the frame is drawn into, over <see cref="Pixels"/>.</summary>
        std::shared_ptr<MCWindow> Window = std::make_shared<MCWindow>();
        /// <summary>The pane the frame is drawn through.</summary>
        MCPane Pane;
        /// <summary>
        /// The picture the polygons map: the pixels as they read them (the window's XMax bytes a row, one fewer than
        /// the window draws them with), with the texture's handle.
        /// </summary>
        std::shared_ptr<MCWindow> TextureWindow = std::make_shared<MCWindow>();
        /// <summary>The texture over the pixels.</summary>
        std::weak_ptr<MCTexture> Texture;
    };

    /// <summary>The PPC canvas, made by the first PPC drawn for its type's edges.</summary>
    MCPpcCanvas& PpcCanvas(const MCLaserType& type)
    {
        static const std::unique_ptr<MCPpcCanvas> canvas = [&]
        {
            auto made = std::make_unique<MCPpcCanvas>();
            const auto shapeWidth = static_cast<int32_t>(type.Rppc - type.Lppc);
            const auto shapeHeight = static_cast<int32_t>(type.Bppc - type.Tppc);
            made->Pane.X0 = 0;
            made->Pane.Y0 = 0;
            made->Pane.X1 = shapeWidth;
            made->Pane.Y1 = shapeHeight;
            made->Window->Pixels = made->Pixels;
            made->Window->XMax = shapeWidth + 1;
            made->Window->YMax = shapeHeight + 1;
            made->Pane.Window = made->Window;
            made->Texture = MCRenderer::CreateTexture(std::span(made->Pixels.get(), 0x10000), made->Window->XMax,
                                                      made->Window->YMax, MCTextureUse::Dynamic);
            made->TextureWindow->Pixels = made->Pixels;
            made->TextureWindow->XMax = made->Window->XMax - 1;
            made->TextureWindow->YMax = made->Window->YMax - 1;
            made->TextureWindow->Texture = made->Texture;
            return made;
        }();
        return *canvas;
    }

    /// <summary>
    /// Sets the beam's duration and colours from stage <paramref name="stage"/> of the friendly set, or of the enemy
    /// set (the second half) when the shooter is missing or not on side 1. Past the last stage the duration is -1 and
    /// the colours 0.
    /// </summary>
    void SetStage(MCLaser& laser, const MCLaserType& type, uint32_t stage)
    {
        std::shared_ptr<MCGameObject> shooter = LockObject(laser.Source);
        const uint32_t numStages = type.NumStages;
        const uint32_t base = (shooter == nullptr || shooter->GetAlignment() != 1) ? numStages : 0;

        if (stage < numStages)
        {
            const MCLaserStage& current = type.Stages[base + stage];
            laser.StageTimeLeft = current.Duration;
            laser.HotColor = current.Hot;
            laser.CoolColor = current.Cool;
        }
        else
        {
            laser.StageTimeLeft = -1.0f;
            laser.HotColor = 0;
            laser.CoolColor = 0;
        }
    }

    /// <summary>Applies the laser's shot to its target (the server's job in multiplayer) and marks it applied.</summary>
    void ApplyShot(MCLaser& laser)
    {
        std::shared_ptr<MCGameObject> victim = LockObject(laser.Target);

        if (MultiPlayer() == nullptr)
        {
            victim->HandleWeaponHit(laser.ShotInfo, 0);
        }
        else if (MultiPlayer()->IsServer != 0)
        {
            victim->HandleWeaponHit(laser.ShotInfo, 1);
        }

        laser.DamageApplied = true;
    }

    /// <summary>
    /// Pulls the beam's end in from the target to <paramref name="fraction"/> of the way from
    /// <paramref name="start"/>, so it grows out over its first stage (or PPC frame).
    /// </summary>
    void ExtendBeam(MCVector3D& end, const MCVector3D& start, float fraction)
    {
        float dx = end.X - start.X;
        float dy = end.Y - start.Y;
        float dz = end.Z - start.Z;
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (length != 0.0f)
        {
            dx /= length;
            dy /= length;
            dz /= length;
        }

        const float reach = fraction * length;
        end.X = dx * reach + start.X;
        end.Y = start.Y + dy * reach;
        end.Z = start.Z + dz * reach;
    }

    /// <summary>Creates the hit (or, without a target, the miss) effect at the beam's end; a miss leaves a crater.</summary>
    void CreateHitEffect(MCLaser& laser, const MCLaserType& type, std::shared_ptr<MCGameObject> victim)
    {
        laser.HitEffectCreated = true;
        std::shared_ptr<MCGameObject> effect =
            CreateObject(static_cast<int32_t>(victim == nullptr ? type.LaserMissEffect : type.LaserHitEffect));

        if (effect == nullptr)
        {
            return;
        }

        if (laser.TargetPosition.has_value())
        {
            effect->SetPosition(*laser.TargetPosition);
        }

        AddToDefaultList(std::move(effect));

        if (victim == nullptr && laser.TargetPosition.has_value())
        {
            CraterManager().AddCrater(7, *laser.TargetPosition, 1);
        }
    }

    /// <summary>A screen vertex (colour, u and v as given; w zero).</summary>
    MCScreenVertex ScreenVertex(int32_t x, int32_t y, MCFixed16 c, MCFixed16 u, MCFixed16 v)
    {
        MCScreenVertex vertex;
        vertex.X = x;
        vertex.Y = y;
        vertex.C = c;
        vertex.U = u;
        vertex.V = v;
        vertex.W = 0;
        return vertex;
    }

    /// <summary>
    /// The quad of a beam from (startX, startY) to (endX, endY), (crossX, crossY) wide to each side, in palette colour
    /// <paramref name="color"/>.
    /// </summary>
    MCPolyElementData BeamQuad(float startX, float startY, float endX, float endY, float crossX, float crossY,
                               MCFixed16 color)
    {
        MCPolyElementData data;
        data.NumVertices = 4;
        data.Vertices[0] =
            ScreenVertex(static_cast<int32_t>(crossX + startX), static_cast<int32_t>(startY + crossY), color, 0, 0);
        data.Vertices[3] =
            ScreenVertex(static_cast<int32_t>(startX - crossX), static_cast<int32_t>(startY - crossY), color, 0, 0);
        data.Vertices[2] =
            ScreenVertex(static_cast<int32_t>(endX - crossX), static_cast<int32_t>(endY - crossY), color, 0, 0);
        data.Vertices[1] =
            ScreenVertex(static_cast<int32_t>(crossX + endX), static_cast<int32_t>(endY + crossY), color, 0, 0);
        return data;
    }
} // namespace

MCLaser::MCLaser() = default;

MCLaser::~MCLaser() = default;

auto MCLaser::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    MCGameObject::Init(objType);
    CurrentStage = 0xff;
    ObjectClass = MCObjectClass::Laser;
    JustCreated = true;
    return 0;
}

auto MCLaser::Update() -> int32_t
{
    const auto& type = TypeAs<MCLaserType>();

    // A PPC runs through the frames of its effect shape and hits on frame hitPPC.
    if (!type.LaserEffectShape.Empty())
    {
        if (!JustCreated)
        {
            PpcAnimTimeLeft -= FrameLength;

            if (PpcAnimTimeLeft < 0.0f)
            {
                PpcAnimTimeLeft = type.AnimPpc;
            }

            PpcFrameTimeLeft -= FrameLength;

            if (PpcFrameTimeLeft < 0.0f)
            {
                PpcFrameTimeLeft = type.LengthPpc;
                PpcFrame++;

                if (PpcFrame == static_cast<int32_t>(type.HitPpc) && LockObject(Target) != nullptr && !DamageApplied)
                {
                    ApplyShot(*this);
                }

                if (PpcFrame == static_cast<int32_t>(type.NumPpcFrames))
                {
                    return 0;
                }
            }
        }
        else
        {
            PpcFrame = 0;
            DamageApplied = false;
            PpcFrameTimeLeft = type.LengthPpc;
            PpcAnimTimeLeft = type.AnimPpc;
            SoundSystem()->PlayDigitalSample(type.SoundEffectId, 1, *this, false, false);
        }

        JustCreated = false;
        return 1;
    }

    // A beam steps through its colour stages, then hits.
    if (CurrentStage == 0xff)
    {
        CurrentStage = 0;
        SetStage(*this, type, 0);
        JustCreated = false;
        return 1;
    }

    if (0.0f <= StageTimeLeft)
    {
        StageTimeLeft -= FrameLength;
        JustCreated = false;
        return 1;
    }

    CurrentStage++;

    if (CurrentStage < type.NumStages)
    {
        SetStage(*this, type, CurrentStage);
        JustCreated = false;
        return 1;
    }

    if (LockObject(Target) == nullptr || DamageApplied)
    {
        return 0;
    }

    ApplyShot(*this);
    return 0;
}

auto MCLaser::Render() -> void
{
    if (JustCreated)
    {
        return;
    }

    std::shared_ptr<MCGameObject> shooter = LockObject(Source);

    if (shooter == nullptr)
    {
        return;
    }

    const MCVector3D start = shooter->GetPositionFromHS(static_cast<uint32_t>(SourceHotSpot));
    std::shared_ptr<MCGameObject> victim = LockObject(Target);

    if (victim != nullptr)
    {
        // Port fix (OB-017): end at the hot spot that was hit (the original used sourceHotSpot).
        const uint32_t hotSpot =
            victim->ObjectClass == MCObjectClass::Turret ? 0xffffffff : static_cast<uint32_t>(TargetHotSpot);
        SetTargetPosition(victim->GetPositionFromHS(hotSpot));
    }

    // Grow the beam out to the target over the first stage (or PPC frame).
    const auto& type = TypeAs<MCLaserType>();
    const bool ppc = !type.LaserEffectShape.Empty();

    if (!ppc)
    {
        if (CurrentStage == 0 && victim != nullptr && TargetPosition.has_value())
        {
            // Faithful: the first stage's length is always read from the enemy set (index numStages).
            const float firstDuration = type.NumStages != 0 ? type.Stages[type.NumStages].Duration : -1.0f;
            ExtendBeam(*TargetPosition, start, firstDuration - StageTimeLeft);
        }
    }
    else if (PpcFrame == 0 && victim != nullptr && TargetPosition.has_value())
    {
        ExtendBeam(*TargetPosition, start, type.LengthPpc - PpcFrameTimeLeft);
    }

    // The hit (or miss) effect, once: when the beam reaches its second stage, or the PPC its hit frame.
    if (!HitEffectCreated && (ppc ? PpcFrame == static_cast<int32_t>(type.HitPpc) : CurrentStage != 0))
    {
        CreateHitEffect(*this, type, victim);
    }

    // Port fix: the original goes on to draw from an unset end point when there is no target position.
    if (!TargetPosition.has_value())
    {
        return;
    }

    const MCVector3D end = *TargetPosition;
    MCVector2D start100;
    MCVector2D end100;

    if (Terrain() != nullptr)
    {
        start100 = MCTerrain::ProjectTerrain(start);
        end100 = MCTerrain::ProjectTerrain(end);
    }

    const float startX = (start100.X - Eye()->ScreenUL.X) + Eye()->HalfWidth;
    const float startY = (start100.Y - Eye()->ScreenUL.Y) + Eye()->HalfHeight;
    const float endX = (end100.X - Eye()->ScreenUL.X) + Eye()->HalfWidth;
    const float endY = (end100.Y - Eye()->ScreenUL.Y) + Eye()->HalfHeight;

    // (The original then tests the leftmost end against the screen's left edge, but draws either way.)

    if (!ppc)
    {
        // A beam: a quad pixelWidth wide across the facing, in the stage's outer colour.
        const float width = static_cast<float>(type.PixelWidth);
        // Original behaviour (OB-018): the facing is in degrees, and gets multiplied by 57.3 again before cos/sin.
        float crossX = static_cast<float>(std::cos(shooter->RelViewFacingTo(end) * RadiansToDegrees) * width);
        float crossY = static_cast<float>(std::sin(shooter->RelViewFacingTo(end) * RadiansToDegrees) * width);
        ElementList().OpenGroup(static_cast<int32_t>(startY), true);
        const auto depth = static_cast<int32_t>((endY + startY) * 0.5f);
        const MCFixed16 color = static_cast<MCFixed16>(CoolColor << 16);
        ElementList().Add(
            ElementList().Make<MCPolygonElement>(BeamQuad(startX, startY, endX, endY, crossX, crossY, color), depth));
        // Port-only (HD lighting): the beam lights the ground and units along it.
        MCSceneLights::AddBeam(startX, startY, endX, endY, width, static_cast<uint8_t>(CoolColor));

        if (HotColor == CoolColor)
        {
            return;
        }

        // The core, half as wide.
        // Original behaviour (OB-019): the core is drawn in the outer (cool) colour too; the hot colour is never used.
        crossX = static_cast<float>(crossX * 0.5);
        crossY = static_cast<float>(crossY * 0.5);
        ElementList().Add(
            ElementList().Make<MCPolygonElement>(BeamQuad(startX, startY, endX, endY, crossX, crossY, color), depth));
        return;
    }

    // A PPC: the current frame of the effect shape, drawn into its own buffer and stretched along the beam.
    MCPpcCanvas& canvas = PpcCanvas(type);
    AGShapeDraw(canvas.Pane, type.LaserEffectShape.Bytes(), PpcFrame, 0, 0);
    const std::shared_ptr<MCTexture> texture = canvas.Texture.lock();
    MCRenderer::UnlockTexture(*texture);
    const float width = static_cast<float>(type.PixelWidth);
    // Port-only (HD lighting): the bolt lights the ground and units along it, in its frame's colour.
    MCSceneLights::AddShapeBeam(startX, startY - width * 0.5f, endX, endY - width * 0.5f, width,
                                type.LaserEffectShape.Bytes(), PpcFrame);
    const auto top = static_cast<int32_t>(startY);
    ElementList().OpenGroup(top, true);
    const auto left = static_cast<int32_t>(startX);
    const auto right = static_cast<int32_t>(endX);
    MCPolyElementData data;
    data.NumVertices = 4;
    data.Vertices[0] = ScreenVertex(left, top, 0, canvas.Pane.X0 << 16, canvas.Pane.Y0 << 16);
    data.Vertices[3] =
        ScreenVertex(left, static_cast<int32_t>(startY - width), 0, canvas.Pane.X0 << 16, canvas.Pane.Y1 << 16);
    data.Vertices[2] =
        ScreenVertex(right, static_cast<int32_t>(endY - width), 0, canvas.Pane.X1 << 16, canvas.Pane.Y1 << 16);
    data.Vertices[1] = ScreenVertex(right, static_cast<int32_t>(endY), 0, canvas.Pane.X1 << 16, canvas.Pane.Y0 << 16);
    data.Texture = canvas.TextureWindow;
    data.FadeTable = std::nullopt;
    ElementList().Add(ElementList().Make<MCPolygonElement>(data, static_cast<int32_t>((endY + startY) * 0.5f)));
}

auto MCLaser::Connect(std::shared_ptr<MCGameObject> source, MCVector3D targetPos,
                      std::optional<MCWeaponShotInfo> shotInfo, int32_t sourceHotSpot) -> void
{
    Source = source;
    SourceHotSpot = sourceHotSpot;
    SetTargetPosition(targetPos);

    if (shotInfo.has_value())
    {
        const MCWeaponShotInfo shot = *shotInfo;
        ShotInfo.Init(LockObject(shot.Attacker), shot.MasterId, shot.Damage, shot.HitLocation, shot.EntryAngle);
    }
}

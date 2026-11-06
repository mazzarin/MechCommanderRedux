#include "stdafx.h"
#include "object/MCDebris.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCDice.h"
#include "main/MCMissionGlobals.h"
#include "object/MCDebrisType.h"
#include "object/MCBattleMech.h"
#include "object/MCObjectDrawing.h"
#include "camera/MCCameraList.h"
#include "color/MCPalette.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCVfxElement.h"
#include "platform/MCRenderer.h"
#include "sprite/MCArmAppearance.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCParticles.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteMath.h"
#include "terrain/MCTerrain.h"

MCArmStyle GHdArms = MCArmStyle::ByHit;

namespace
{
    /// <summary>Port-only (HD art's arms): what pulls a torn-off arm down, meters a second each second (twice the earth's).</summary>
    constexpr float WreckGravity = 19.6f;

    /// <summary>
    /// How a style throws an arm off: out the debris' way and up (meters a second), its spin (degrees a second), each
    /// give or take its Vary; and how much of its speed coming down a bounce sends it back up.
    /// </summary>
    struct WreckThrow
    {
        float Out;
        float OutVary;
        float Up;
        float UpVary;
        float Spin;
        float SpinVary;
        float Bounce;
    };

    constexpr WreckThrow DroppedThrow{3.0f, 1.0f, 2.0f, 0.5f, 160.0f, 40.0f, 0.2f};
    constexpr WreckThrow ThrownThrow{14.0f, 3.0f, 6.0f, 1.5f, 360.0f, 150.0f, 0.35f};
    constexpr WreckThrow ShatteredThrow{9.0f, 3.5f, 5.5f, 2.5f, 520.0f, 200.0f, 0.3f};
    /// <summary>A thrown arm spins outward (its top away from the mech) this share of the time.</summary>
    constexpr float ThrownOutwardSpin = 0.75f;
    /// <summary>A shattered arm's pieces fly within this many degrees of the debris' way, either side.</summary>
    constexpr float ShatterSpreadDegrees = 80.0f;
    /// <summary>A shattered arm is cut into pieces about this many window pixels a side, 2 to 3 across and 2 to 4 down.</summary>
    constexpr float ShatterPieceSide = 9.0f;
    constexpr int32_t ShatterLeastCuts = 2;
    constexpr int32_t ShatterMostAcross = 3;
    constexpr int32_t ShatterMostDown = 4;
    /// <summary>The widest or tallest frame that can be shattered: MCSpriteTurn::Piece holds a byte a bound.</summary>
    constexpr float ShatterMostSide = 255.0f;
    /// <summary>Coming down slower than this (meters a second), it stops bouncing.</summary>
    constexpr float WreckLeastBounce = 2.0f;
    /// <summary>A bounce keeps this share of its speed along the ground and of its spin.</summary>
    constexpr float WreckBounceKeeps = 0.55f;
    /// <summary>On the ground it skids to a halt, this many meters a second less each second.</summary>
    constexpr float WreckSkid = 14.0f;
    /// <summary>
    /// By the hit (MCArmStyle::ByHit): a hit going this far past what the arm had left (a share of its whole structure)
    /// throws it, and this far shatters it.
    /// </summary>
    constexpr float ThrownBlow = 0.25f;
    constexpr float ShatteredBlow = 1.0f;
    /// <summary>The dust it throws up landing: this much burst extent a window pixel of its width, at full speed (this
    /// fast, meters a second, coming down) or more.</summary>
    constexpr float WreckDustPerPixel = 0.8f;
    constexpr float WreckDustSpeed = 8.0f;
    /// <summary>The burst at the shoulder as the arm tears off, and as it shatters.</summary>
    constexpr MCParticles::MCBurstSize TearBurst{26.0f, 20.0f};
    constexpr MCParticles::MCBurstSize ShatterBurst{44.0f, 30.0f};
    /// <summary>The stump sparks this long (seconds), a small burst every StumpSparkEvery seconds.</summary>
    constexpr float StumpSparkSeconds = 3.0f;
    constexpr float StumpSparkEvery = 0.2f;
    constexpr MCParticles::MCBurstSize StumpSpark{8.0f, 6.0f};
    /// <summary>The least shoulder height an arm tears off at (meters).</summary>
    constexpr float LeastStumpHeight = 0.5f;
    constexpr float DegreesToRadians = std::numbers::pi_v<float> / 180.0f;

    /// <summary>
    /// The way along the ground (world units) that moves a point at <paramref name="at"/> by window pixels
    /// (<paramref name="screenX"/>, <paramref name="screenY"/>) as <paramref name="camera"/> shows it; none when the
    /// view shows the ground edge-on.
    /// </summary>
    std::optional<std::array<float, 2>> GroundStep(const MCVector3D& at, const MCCamera& camera, float screenX,
                                                   float screenY)
    {
        const MCVector2D from = ProjectToScreen(at, camera);
        const MCVector2D alongX = ProjectToScreen(MCVector3D(at.X + 1.0f, at.Y, at.Z), camera) - from;
        const MCVector2D alongY = ProjectToScreen(MCVector3D(at.X, at.Y + 1.0f, at.Z), camera) - from;
        const float determinant = alongX.X * alongY.Y - alongY.X * alongX.Y;

        if (determinant == 0.0f)
        {
            return std::nullopt;
        }

        return std::array{(alongY.Y * screenX - alongY.X * screenY) / determinant,
                          (alongX.X * screenY - alongX.Y * screenX) / determinant};
    }

    /// <summary>The ground's height at world (<paramref name="x"/>, <paramref name="y"/>).</summary>
    float GroundAt(float x, float y)
    {
        return MCTerrain::GetTerrainElevation(MCVector3D(x, y, 0.0f));
    }
}

MCDebris::MCDebris()
{
    Frame.I = UnitX;
    Frame.J = UnitY;
    Frame.K = UnitZ;
}

MCDebris::~MCDebris() = default;

auto MCDebris::GetAppearance() -> std::shared_ptr<MCAppearance>
{
    return Appearance;
}

auto MCDebris::OnScreen() -> int
{
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();

    if (camera == nullptr)
    {
        return 0;
    }

    ScreenPos = ProjectToScreen(Position, *camera);

    if (Appearance != nullptr && Appearance->RecalcBounds(*camera) != 0)
    {
        WindowsVisible = Turn;
        return 1;
    }

    return 0;
}

auto MCDebris::Update() -> int32_t
{
    const auto& debrisType = TypeAs<MCDebrisType>();

    if (JustCreated)
    {
        // The direction set by the creator becomes a velocity of armFallVelMag plus a random share.
        const float speed = static_cast<float>(RandomNumber(static_cast<int32_t>(debrisType.ArmFallVelRange))) +
                            debrisType.ArmFallVelMag;
        DecelRate = debrisType.ArmFallDecelRate;
        JustCreated = false;
        Velocity.X = speed * Velocity.X;
        Velocity.Y = speed * Velocity.Y;
        Velocity.Z = speed * Velocity.Z;
        Visible = OnScreen();
    }

    // Slide along the ground; once the fall animation is over, slow down (by decelRate) until stopped.
    if (!Stopped)
    {
        const float moveX = Velocity.X * FrameLength * WorldUnitsPerMeter;
        const float moveY = WorldUnitsPerMeter * FrameLength * Velocity.Y;

        if (FallDone)
        {
            const float change = FrameLength * DecelRate;
            float dirX = Velocity.X;
            float dirY = Velocity.Y;
            float dirZ = Velocity.Z;
            const float speed = std::sqrt(dirX * dirX + dirZ * dirZ + dirY * dirY);

            if (speed != 0.0f)
            {
                dirX /= speed;
                dirY /= speed;
                dirZ /= speed;
            }

            Velocity.Z = 0.0f;
            Velocity.X = dirX * change + Velocity.X;
            Velocity.Y = dirY * change + Velocity.Y;
            Velocity.Z = dirZ * change + Velocity.Z;

            if (std::sqrt(Velocity.Z * Velocity.Z + Velocity.Y * Velocity.Y + Velocity.X * Velocity.X) <= 0.0f)
            {
                Velocity.Z = 0.0f;
                Velocity.Y = 0.0f;
                Velocity.X = 0.0f;
                Stopped = true;
            }
        }

        Position.X = moveX + Position.X;
        Position.Y = moveY + Position.Y;
    }

    Visible = OnScreen();

    if (Appearance != nullptr)
    {
        Appearance->Visible = Visible;

        if (Appearance->Update() == 0)
        {
            FallDone = true;
        }
    }

    if (!Wreck.empty())
    {
        MoveWreck(FrameLength);
    }

    return 1;
}

auto MCDebris::RandomAngle(float& angle) -> void
{
    const auto& debrisType = TypeAs<MCDebrisType>();
    const float yaw = debrisType.ArmFallYaw + angle;
    angle = yaw;

    if (RollDice(50) != 0)
    {
        angle = static_cast<float>(RandomNumber(static_cast<int32_t>(debrisType.ArmFallYawRange))) + yaw;
    }
    else
    {
        angle = yaw - static_cast<float>(RandomNumber(static_cast<int32_t>(debrisType.ArmFallYawRange)));
    }
}

auto MCDebris::Render() -> void
{
    if (GamePaused != 0)
    {
        OnScreen();
    }

    if (!Wreck.empty())
    {
        RenderWreck();
        return;
    }

    if (WindowsVisible == Turn && !JustCreated && Appearance != nullptr)
    {
        Appearance->Render(0);
    }
}

auto MCDebris::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    if (const int32_t result = MCGameObject::Init(objType); result != 0)
    {
        return result;
    }

    JustCreated = true;
    CollisionsOn = 0;
    std::shared_ptr<MCAppearanceType> apprType = AppearanceTypeList().GetAppearance(objType->AppearName);

    if (apprType == nullptr)
    {
        return static_cast<int32_t>(0xdebb0002);
    }

    Appearance = std::make_shared<MCArmAppearance>();
    Appearance->Init();

    if ((apprType->AppearanceNum & 0xff000000) != 0x6000000)
    {
        return static_cast<int32_t>(0xdebb0004);
    }

    if (const int32_t result = Appearance->Init(apprType, WeakAs<MCGameObject>()); result != 0)
    {
        return result;
    }

    ObjectClass = MCObjectClass::Debris;
    return 0;
}

auto MCDebris::SetPaintScheme(int32_t paintScheme) const -> void
{
    Appearance->FadeTableIndex = paintScheme;
}

auto MCDebris::WreckUnit() -> float
{
    // 24 of the engine's bits.
    constexpr uint32_t DroppedBits = 8;
    constexpr float Scale = 1.0f / static_cast<float>(1u << 24);
    return static_cast<float>(WreckDice() >> DroppedBits) * Scale;
}

auto MCDebris::StyleOfBlow(float blow) -> MCArmStyle
{
    return blow < ThrownBlow ? MCArmStyle::Dropped : blow < ShatteredBlow ? MCArmStyle::Thrown : MCArmStyle::Shattered;
}

auto MCDebris::TearOff(MCBattleMech& mech, const MCDrawnArm& arm, int32_t paint, float blow) -> void
{
    const MCArmStyle style = GHdArms == MCArmStyle::ByHit ? StyleOfBlow(blow) : GHdArms;
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();

    if (style == MCArmStyle::Original || arm.Shape == nullptr || arm.Shape->FrameList().empty() || camera == nullptr ||
        !MCHdArt::Active() || MCRenderer::GpuDrawing() != MCGpuDrawing::On)
    {
        return;
    }

    // The arm's middle from the mech's foot, in window pixels (a mirrored frame's columns run the other way), and how
    // many window pixels a meter up takes there.
    const std::span<const uint8_t> frames = arm.Shape->FrameList();
    const int32_t frame = MCClampShapeFrame(frames, arm.Frame);
    const MCFrameBounds bounds = MCShapeFrameBounds(frames, frame);
    const float mirror = arm.Reverse != 0 ? -1.0f : 1.0f;
    const MCVector3D foot = mech.Position;
    const MCVector2D footOnScreen = ProjectToScreen(foot, *camera);
    const float pixelsPerMeter =
        footOnScreen.Y - ProjectToScreen(MCVector3D(foot.X, foot.Y, foot.Z + WorldUnitsPerMeter), *camera).Y;
    const float awayLength = std::hypot(Velocity.X, Velocity.Y);
    const bool tooBig =
        style == MCArmStyle::Shattered && (bounds.Width > ShatterMostSide || bounds.Height > ShatterMostSide);

    if (pixelsPerMeter <= 0.0f || awayLength <= 0.0f || tooBig)
    {
        return;
    }

    WreckDice.seed(std::bit_cast<uint32_t>(foot.X) ^ std::bit_cast<uint32_t>(foot.Y) * 31u ^
                   std::bit_cast<uint32_t>(ScenarioTime));
    WreckStyle = style;
    WreckShape = arm.Shape;
    WreckFrame = frame;
    WreckReverse = arm.Reverse;
    WreckPaint = paint;
    WreckMech = mech.WeakAs<MCGameObject>();
    StumpHeight = std::max(-(bounds.MinY + bounds.Height * 0.5f) / pixelsPerMeter, LeastStumpHeight);
    StumpSparking = StumpSparkSeconds;
    NextSpark = StumpSparkEvery;

    // The debris' way, and which way on the window is outward: the arm's top falls that way.
    const float awayX = Velocity.X / awayLength;
    const float awayY = Velocity.Y / awayLength;
    const float outward =
        ProjectToScreen(MCVector3D(foot.X + awayX, foot.Y + awayY, foot.Z), *camera).X >= footOnScreen.X ? 1.0f : -1.0f;
    const WreckThrow& thrown = style == MCArmStyle::Dropped  ? DroppedThrow
                               : style == MCArmStyle::Thrown ? ThrownThrow
                                                             : ShatteredThrow;
    const auto vary = [this](float value, float by) { return value + by * (2.0f * WreckUnit() - 1.0f); };

    // A piece of the frame (original pixels from its first column and row, inclusive), flying from where it is.
    const auto add = [&](float left, float top, float right, float bottom, float wayDegrees, float spinWay, bool whole)
    {
        const float middleX = mirror * (bounds.MinX + 0.5f * (left + right + 1.0f));
        const float middleY = bounds.MinY + 0.5f * (top + bottom + 1.0f);
        const std::optional<std::array<float, 2>> step = GroundStep(foot, *camera, middleX, 0.0f);

        if (!step.has_value())
        {
            return;
        }

        WreckPiece piece;
        piece.X = foot.X + (*step)[0];
        piece.Y = foot.Y + (*step)[1];
        piece.Height = std::max(-middleY / pixelsPerMeter, 0.0f);
        const float way = wayDegrees * DegreesToRadians;
        const float out = vary(thrown.Out, thrown.OutVary);
        piece.SpeedX = out * (awayX * std::cos(way) - awayY * std::sin(way));
        piece.SpeedY = out * (awayX * std::sin(way) + awayY * std::cos(way));
        piece.SpeedUp = vary(thrown.Up, thrown.UpVary);
        piece.Spin = spinWay * vary(thrown.Spin, thrown.SpinVary) * DegreesToRadians;
        piece.MiddleX = middleX;
        piece.MiddleY = middleY;
        piece.Width = right - left + 1.0f;

        if (!whole)
        {
            piece.Part = std::array{static_cast<uint8_t>(left), static_cast<uint8_t>(top), static_cast<uint8_t>(right),
                                    static_cast<uint8_t>(bottom)};
        }

        Wreck.push_back(piece);
    };

    const float right = bounds.Width - 1.0f;
    const float bottom = bounds.Height - 1.0f;

    if (style == MCArmStyle::Shattered)
    {
        // Cut into a grid of pieces.
        const int32_t across =
            std::clamp(static_cast<int32_t>(bounds.Width / ShatterPieceSide), ShatterLeastCuts, ShatterMostAcross);
        const int32_t down =
            std::clamp(static_cast<int32_t>(bounds.Height / ShatterPieceSide), ShatterLeastCuts, ShatterMostDown);

        for (int32_t row = 0; row < down; ++row)
        {
            for (int32_t column = 0; column < across; ++column)
            {
                const float left = std::floor(bounds.Width * static_cast<float>(column) / static_cast<float>(across));
                const float top = std::floor(bounds.Height * static_cast<float>(row) / static_cast<float>(down));
                const float pieceRight =
                    std::floor(bounds.Width * static_cast<float>(column + 1) / static_cast<float>(across)) - 1.0f;
                const float pieceBottom =
                    std::floor(bounds.Height * static_cast<float>(row + 1) / static_cast<float>(down)) - 1.0f;
                const float spinWay = WreckUnit() < 0.5f ? -1.0f : 1.0f;
                add(left, top, pieceRight, pieceBottom, vary(0.0f, ShatterSpreadDegrees), spinWay, false);
            }
        }
    }
    else
    {
        const float spinWay = style == MCArmStyle::Thrown && WreckUnit() >= ThrownOutwardSpin ? -outward : outward;
        add(0.0f, 0.0f, right, bottom, 0.0f, spinWay, true);
    }

    // No piece found the ground under it: the original's debris.
    if (Wreck.empty())
    {
        return;
    }

    // It tears off in a burst at the shoulder.
    MCParticles::AddBurst(MCVector3D(foot.X, foot.Y, foot.Z + StumpHeight * WorldUnitsPerMeter),
                          style == MCArmStyle::Shattered ? ShatterBurst : TearBurst, false);
}

auto MCDebris::MoveWreck(float seconds) -> void
{
    if (seconds <= 0.0f)
    {
        return;
    }

    // The stump sparks a while, wherever the mech has gone.
    if (StumpSparking > 0.0f)
    {
        StumpSparking -= seconds;
        NextSpark -= seconds;
        const std::shared_ptr<MCGameObject> mech = WreckMech.lock();

        for (; NextSpark <= 0.0f && mech != nullptr; NextSpark += StumpSparkEvery)
        {
            const MCVector3D at = mech->GetPosition();
            MCParticles::AddBurst(MCVector3D(at.X, at.Y, at.Z + StumpHeight * WorldUnitsPerMeter), StumpSpark, false);
        }
    }

    for (WreckPiece& piece : Wreck)
    {
        if (piece.Sunk)
        {
            continue;
        }

        piece.X += piece.SpeedX * seconds * WorldUnitsPerMeter;
        piece.Y += piece.SpeedY * seconds * WorldUnitsPerMeter;

        // Flying: falls and spins; coming down, bounces (throwing up dust, or sinking in water) or lands.
        if (!piece.Landed)
        {
            piece.SpeedUp -= WreckGravity * seconds;
            piece.Height += piece.SpeedUp * seconds;
            piece.Angle += piece.Spin * seconds;

            if (piece.Height <= 0.0f && piece.SpeedUp < 0.0f)
            {
                const float down = -piece.SpeedUp;
                piece.Height = 0.0f;
                const float dust = WreckDustPerPixel * piece.Width * std::min(down / WreckDustSpeed, 1.0f);
                MCParticles::AddBurst(MCVector3D(piece.X, piece.Y, GroundAt(piece.X, piece.Y)), {0.0f, dust}, false);

                if (MCParticles::WaterAt(piece.X, piece.Y))
                {
                    piece.Sunk = true;
                    continue;
                }

                const WreckThrow& thrown = WreckStyle == MCArmStyle::Dropped  ? DroppedThrow
                                           : WreckStyle == MCArmStyle::Thrown ? ThrownThrow
                                                                              : ShatteredThrow;

                if (down > WreckLeastBounce)
                {
                    piece.SpeedUp = down * thrown.Bounce;
                    piece.SpeedX *= WreckBounceKeeps;
                    piece.SpeedY *= WreckBounceKeeps;
                    piece.Spin *= WreckBounceKeeps;
                }
                else
                {
                    piece.SpeedUp = 0.0f;
                    piece.Landed = true;
                }
            }

            continue;
        }

        // Lying as it landed: skids to a halt.
        const float speed = std::hypot(piece.SpeedX, piece.SpeedY);
        const float slowed = std::max(speed - WreckSkid * seconds, 0.0f);
        const float keep = speed > 0.0f ? slowed / speed : 0.0f;
        piece.SpeedX *= keep;
        piece.SpeedY *= keep;
    }
}

auto MCDebris::RenderWreck() -> void
{
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();

    if (camera == nullptr || WreckShape == nullptr)
    {
        return;
    }

    std::optional<MCColorTable> paint;

    if (WreckPaint >= 0)
    {
        paint = GamePalette().GetFadeTable(WreckPaint);
    }

    // The sprite cache lets go of a part its mech no longer draws, and with it the part's tie to the HD art pack.
    const std::span<const uint8_t> frames = WreckShape->FrameList();

    if (!MCHdArt::Attached(frames))
    {
        MCHdArt::AttachFolder(WreckShape);
    }

    for (const WreckPiece& piece : Wreck)
    {
        if (piece.Sunk)
        {
            continue;
        }

        // Its middle where it flies, the frame's hot spot drawn as far from it as it lies from the middle; its sun
        // shadow on the ground under every unit and object, the piece sorted by the ground under it.
        const float ground = GroundAt(piece.X, piece.Y);
        const MCVector2D under = ProjectToScreen(MCVector3D(piece.X, piece.Y, ground), *camera);
        const MCVector2D at =
            ProjectToScreen(MCVector3D(piece.X, piece.Y, ground + piece.Height * WorldUnitsPerMeter), *camera);
        const MCSpriteTurn turn{piece.Angle, piece.MiddleX, piece.MiddleY, under.Y - at.Y, piece.Part};
        const float x = at.X - piece.MiddleX;
        const float y = at.Y - piece.MiddleY;

        ElementList().OpenGroup(MCSceneLights::ShadowDepth, false);
        auto& shadow = ElementList().Make<MCVfxElement>(frames, x, y, WreckFrame, WreckReverse, std::nullopt, true);
        shadow.SunShadow = true;
        shadow.Turn = turn;
        ElementList().Add(shadow);

        ElementList().OpenGroup(static_cast<int32_t>(std::floor(-under.Y)), true);
        auto& drawn = ElementList().Make<MCVfxElement>(frames, x, y, WreckFrame, WreckReverse, paint, true);
        drawn.Turn = turn;
        ElementList().Add(drawn);
    }
}

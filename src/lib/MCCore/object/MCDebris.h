#pragma once

#include "object/MCBigGameObject.h"

#include <random>

class MCArmAppearance;
class MCBattleMech;
class MCShape;
struct MCDrawnArm;

/// <summary>
/// Port-only (HD art's arms): how a blown-off mech arm shows, with HD art drawn by the GPU (MCDebris::TearOff): as the
/// original (its debris' own small arm piece sliding off), or the mech's own arm torn off its shoulder and dropped beside
/// it, thrown clear, or shattered into pieces, each falling onto the ground and lying there. The game has the hit that
/// took it off pick one of the three (<see cref="MCDebris::StyleOfBlow"/>); the others are the tools' to compare.
/// </summary>
enum class MCArmStyle : int32_t
{
    Original = 0,
    ByHit = 1,
    Dropped = 2,
    Thrown = 3,
    Shattered = 4,
};

/// <summary>
/// Port-only: how blown-off arms show with HD art (part of HD art, with no setting of its own): by the hit, unless a
/// picture tool says otherwise (<c>--hd-arms</c>).
/// </summary>
extern MCArmStyle GHdArms;

/// <summary>
/// A piece of debris (a mech's arm) thrown off when it is destroyed: an arm appearance that flies along its velocity,
/// then slows to a stop once its fall animation has finished.
/// </summary>
/// <remarks>Original source: <c>object\debris.cpp</c>, <c>object\debris.h</c>.</remarks>
class MCDebris : public MCBigGameObject
{
public:
    /// <summary>Starts in the world frame, visible.</summary>
    MCDebris();
    ~MCDebris() override;

    /// <summary>Makes the arm appearance.</summary>
    int32_t Init(std::shared_ptr<MCObjectType> objType) override;
    using MCBigGameObject::Init;
    int32_t Kill() override { return 0; }
    /// <summary>
    /// On the first update scales the velocity by the type's speed (plus a random part); then moves, and once the
    /// animation is done slows down at the type's deceleration until stopped.
    /// </summary>
    int32_t Update() override;
    void Render() override;
    std::shared_ptr<MCAppearance> GetAppearance() override;
    /// <summary>Projects the debris to the screen; true when its appearance is visible to the main camera.</summary>
    int OnScreen() override;
    MCVector3D GetVelocity() override { return Velocity; }
    void SetVelocity(MCVector3D& newVelocity) override { Velocity = newVelocity; }
    MCFrameOfRef GetFrame() override { return Frame; }
    void SetFrame(MCFrameOfRef& newFrame) override { Frame = newFrame; }

    /// <summary>
    /// Adds the type's yaw to <paramref name="angle"/>, then a random amount (of the yaw range) in a random
    /// direction.
    /// </summary>
    void RandomAngle(float& angle);
    /// <summary>Sets the arm appearance's paint scheme.</summary>
    void SetPaintScheme(int32_t paintScheme) const;

    /// <summary>
    /// Port-only (HD art's arms): with HD art drawn by the GPU (and GHdArms not the original), the debris is drawn as
    /// <paramref name="mech"/>'s own arm (<paramref name="arm"/>, as the mech last drew it, in paint scheme
    /// <paramref name="paint"/>) torn off its shoulder and flying the debris' way (its velocity, set before), dropped,
    /// thrown or shattered as GHdArms says (by the hit: by <paramref name="blow"/>, see <see cref="StyleOfBlow"/>): it
    /// falls onto the ground under it, bounces and lies there. Its moves throw their own dice, never the game's, and
    /// change nothing in the game.
    /// </summary>
    void TearOff(MCBattleMech& mech, const MCDrawnArm& arm, int32_t paint, float blow);

    /// <summary>
    /// Port-only (HD art's arms): how an arm taken off by a hit going <paramref name="blow"/> of the arm's whole structure
    /// past what it had left shows (MCBattleMech::RightArmBlow): just enough (worn down, or gone with its side torso or
    /// to a critical: 0) drops it, well past throws it, as much again as a whole arm shatters it.
    /// </summary>
    static MCArmStyle StyleOfBlow(float blow);

    /// <summary>Set until the first update, which scales the velocity.</summary>
    bool JustCreated = true;
    /// <summary>The arm appearance.</summary>
    std::shared_ptr<MCArmAppearance> Appearance;
    /// <summary>The last onScreen result, passed to the appearance's update.</summary>
    int32_t Visible = 1;
    /// <summary>The type's armFallDecelRate, copied on the first update.</summary>
    float DecelRate = 0;
    /// <summary>Set once the appearance's animation has finished: the debris then slows down.</summary>
    bool FallDone = false;
    /// <summary>Set once the debris has slowed to a stop: it no longer moves.</summary>
    bool Stopped = false;
    /// <summary>Velocity in meters per second (converted with worldUnitsPerMeter when moving).</summary>
    MCVector3D Velocity;
    /// <summary>Orientation, returned by <see cref="GetFrame"/>.</summary>
    MCFrameOfRef Frame;

    /// <summary>Port-only (HD art's arms): a piece of a torn-off arm, the whole arm or one shattered piece of it.</summary>
    struct WreckPiece
    {
        /// <summary>The world point under it, and how high above the ground there it is, in meters.</summary>
        float X = 0.0f;
        float Y = 0.0f;
        float Height = 0.0f;
        /// <summary>How fast it flies, meters a second, along the ground and up.</summary>
        float SpeedX = 0.0f;
        float SpeedY = 0.0f;
        float SpeedUp = 0.0f;
        /// <summary>How far it is turned clockwise on the window, and how fast it turns, radians (a second).</summary>
        float Angle = 0.0f;
        float Spin = 0.0f;
        /// <summary>Come to the ground for good: it skids to a halt and lies as it landed.</summary>
        bool Landed = false;
        /// <summary>Its part of the arm's frame (none: all of it), and its middle from the frame's hot spot.</summary>
        std::optional<std::array<uint8_t, 4>> Part;
        float MiddleX = 0.0f;
        float MiddleY = 0.0f;
        /// <summary>How wide it is, window pixels (the dust it throws up landing).</summary>
        float Width = 0.0f;
        /// <summary>Landed in water: gone under.</summary>
        bool Sunk = false;
    };

    /// <summary>Port-only (HD art's arms): the torn-off arm's pieces (none: drawn as the original).</summary>
    std::vector<WreckPiece> Wreck;
    /// <summary>Port-only (HD art's arms): how it was torn off.</summary>
    MCArmStyle WreckStyle = MCArmStyle::Original;
    /// <summary>Port-only (HD art's arms): the arm's frame, as its mech drew it last, and its paint scheme.</summary>
    std::shared_ptr<MCShape> WreckShape;
    int32_t WreckFrame = 0;
    int32_t WreckReverse = 0;
    int32_t WreckPaint = -1;
    /// <summary>Port-only (HD art's arms): the mech it was torn off, how high its shoulder is (meters), how long it sparks yet.</summary>
    std::weak_ptr<MCGameObject> WreckMech;
    float StumpHeight = 0.0f;
    float StumpSparking = 0.0f;
    float NextSpark = 0.0f;
    /// <summary>Port-only (HD art's arms): the wreck's own dice.</summary>
    std::mt19937 WreckDice;

private:
    /// <summary>Port-only (HD art's arms): moves the wreck on by <paramref name="seconds"/>: flying, bouncing, settling.</summary>
    void MoveWreck(float seconds);
    /// <summary>Port-only (HD art's arms): draws the wreck's pieces turned as they fly, and their sun shadows.</summary>
    void RenderWreck();
    /// <summary>Port-only (HD art's arms): a number in [0, 1) off the wreck's dice, the same on every platform.</summary>
    float WreckUnit();
};

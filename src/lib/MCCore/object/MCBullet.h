#pragma once

#include "object/MCBigGameObject.h"
#include "object/MCWeaponShotInfo.h"

class MCArmAppearance;
class MCSmoke;

/// <summary>
/// A projectile that flies from its owner's hot spot to a target (or a target position), then applies its shots'
/// damage and creates a hit or miss effect; a miss on a mined cell sets the mine off.
/// </summary>
/// <remarks>Original source: <c>object\bullet.cpp</c>, <c>object\bullet.h</c>.</remarks>
class MCBullet : public MCBigGameObject
{
public:
    MCBullet();
    ~MCBullet() override;

    /// <summary>Makes the arm appearance and the smoke and light objects of the type.</summary>
    int32_t Init(std::shared_ptr<MCObjectType> objType) override;
    using MCBigGameObject::Init;
    int32_t Kill() override { return 0; }
    /// <summary>
    /// Moves the bullet toward its target; once it stops getting closer applies the shots to the target and creates
    /// the hit (or miss) effect.
    /// </summary>
    /// <returns>1 while flying (or while the smoke trail lasts), 0 when done.</returns>
    int32_t Update() override;
    void Render() override;

    /// <summary>Projects the bullet to the screen; true unless its appearance is off screen.</summary>
    bool IsVisible();
    /// <summary>Sets the position the bullet flies to.</summary>
    void SetTargetPosition(MCVector3D position) { TargetPosition = position; }
    /// <summary>Sets the owner and the hot spot it fires from, and the target position.</summary>
    void Connect(MCGameObject& source, MCVector3D targetPos, int32_t sourceHotSpot);
    /// <summary>A new shot to apply on arrival (the caller fills it).</summary>
    MCWeaponShotInfo& NewShot() { return Shots.emplace_back(); }
    /// <summary>
    /// Whether a shot <paramref name="groundDistanceSq"/> (squared, along the ground) from where it flies has arrived,
    /// flying at <paramref name="velocity"/> with frames of <paramref name="frameLength"/>; lowers
    /// <paramref name="closestDistanceSq"/>, the closest it has been, when it gets closer. The original's test (here
    /// and in MCProjectileLaser): arrived once the distance stops shrinking. Port fix: with frames shorter than 15 a
    /// second's, a followed target's hot spot, which moves a few units whenever its animation turns a frame, can get
    /// further away than a shot's step, and the hit landed at once, mid-flight. So with shorter frames a shot that
    /// follows a target arrives within a step of it, or once it is further than its closest by a 15 fps step less this
    /// frame's (what the original's test lets the target gain at 15).
    /// </summary>
    static bool Approach(float& closestDistanceSq, double groundDistanceSq, float velocity, float frameLength,
                         bool followsTarget);

    /// <summary>The frames the original's arrival test holds for: 15 a second, the frame lock's (Ctrl+Alt+S).</summary>
    static constexpr float PacedFrameLength = 1.0f / 15.0f;

    /// <summary>Set until the first update, which plays the sound and places the bullet.</summary>
    bool JustCreated = true;
    /// <summary>The object that fired the bullet.</summary>
    std::weak_ptr<MCGameObject> Owner;
    /// <summary>The owner's hot spot the bullet leaves from.</summary>
    int32_t OwnerHotSpot = 0;
    /// <summary>The object the bullet flies to and damages; null for a shot at a position.</summary>
    std::weak_ptr<MCGameObject> Target;
    /// <summary>The target's hot spot where the hit effect is placed (not used for turret targets).</summary>
    int32_t TargetHotSpot = 0;
    /// <summary>Where the bullet flies to, once set.</summary>
    std::optional<MCVector3D> TargetPosition;
    /// <summary>The smallest squared ground distance to the target so far (starts at 1e8); growing again means arrival.</summary>
    float ClosestDistanceSq = 0;
    /// <summary>The arm appearance of the bullet.</summary>
    std::unique_ptr<MCArmAppearance> Appearance;
    /// <summary>The shots applied to the target on arrival.</summary>
    std::vector<MCWeaponShotInfo> Shots;
    /// <summary>The smoke trail.</summary>
    std::shared_ptr<MCSmoke> Smoke;
    /// <summary>The bullet's own flight position (the object position follows the owner's hot spot).</summary>
    MCVector3D BulletPosition;
    /// <summary>The light that travels with the bullet.</summary>
    std::shared_ptr<MCGameObject> Light;
    /// <summary>Port-only (HD lighting): how much of its muzzle flash is still to go, in seconds.</summary>
    float MuzzleFlashLeft = 0.0f;
    /// <summary>The draw rotation of the appearance: -150, or 150 when a mech owner faces the other way.</summary>
    int32_t DrawRotation = 0;
};

#pragma once

#include "object/MCBigGameObject.h"

class MCVfxAppearance;

/// <summary>
/// An explosion: a VFX appearance, a sound, an optional light, and half a second in one check of the objects around it
/// (see <see cref="MCExplosionType::HandleCollision"/>).
/// </summary>
/// <remarks>Original source: <c>object\explode.cpp</c>, <c>object\explode.h</c>.</remarks>
class MCExplosion : public MCBigGameObject
{
public:
    MCExplosion();
    ~MCExplosion() override;

    /// <summary>
    /// Makes the VFX appearance, sets the blast radius and damage from the type, and creates the light.
    /// </summary>
    int32_t Init(std::shared_ptr<MCObjectType> objType) override;
    using MCBigGameObject::Init;
    int32_t Kill() override { return 0; }
    /// <summary>Plays the sound on the first update, turns on collision after 0.5 s, and advances the effect.</summary>
    /// <returns>The appearance's update result: 0 once the effect has finished.</returns>
    int32_t Update() override;
    void Render() override;
    /// <summary>Checks the static objects of the 3x3 terrain blocks around the explosion for collisions.</summary>
    void HandleStaticCollision() override;
    /// <summary>Projects the explosion to the screen; true when its appearance is visible to the main camera.</summary>
    int OnScreen() override;
    /// <summary>The blast radius (the explosion radius, set by setExplRad).</summary>
    float GetExtentRadius() override { return ExplRadius; }
    /// <summary>Sets the blast radius.</summary>
    void SetExtentRadius(float newRadius) override { ExplRadius = newRadius; }

    /// <summary>The VFX appearance that draws the explosion.</summary>
    std::unique_ptr<MCVfxAppearance> Appearance;
    /// <summary>Set until the first update, which plays the sound.</summary>
    bool JustCreated = true;
    /// <summary>Seconds since the explosion started.</summary>
    float TimeAlive = 0;
    /// <summary>A copy of the type's damageChunkSize.</summary>
    float DamageChunkSize = 0;
    /// <summary>Set once the explosion has been checked for collisions (0.5 s in); the check is not repeated.</summary>
    bool CollisionChecked = false;
    /// <summary>The light of the type's lightObjectId, kept at the explosion's position.</summary>
    std::shared_ptr<MCGameObject> Light;
    /// <summary>Port-only (HD particles): set once its burst is made (the first frame it is drawn).</summary>
    bool Burst = false;
    /// <summary>
    /// Port-only (HD shake): whether its burst shakes the picture, set by what made it (MCObjectType::CreateExplosion's
    /// MCBlastShake: a large artillery strike's explosions, a building's end).
    /// </summary>
    bool Shakes = false;
};

/// <summary>
/// Creates an object of type <paramref name="objectTypeId"/> (an explosion) at <paramref name="position"/> and adds
/// it to the object list; a nonzero <paramref name="radius"/> also gives it a blast radius and
/// <paramref name="damage"/>. Does nothing for id -1.
/// </summary>
void CreateExplosion(int32_t objectTypeId, MCVector3D& position, float damage, float radius);

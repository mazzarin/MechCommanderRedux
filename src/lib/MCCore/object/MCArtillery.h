#pragma once

#include "object/MCBigGameObject.h"

class MCCamera;
class MCObjectEvent;
class MCSensorSystem;

/// <summary>
/// The object type numbers <see cref="CallArtillery"/> makes, by strike type: small, large, sensor and camera drone
/// strikes, then the same four kinds in multiplayer games.
/// </summary>
inline constexpr std::array<int32_t, 8> ArtilleryTypeTable = {249, 248, 250, 516, 508, 507, 509, 516};

/// <summary>Object type of the camera drone a sensor probe launches.</summary>
inline constexpr int32_t CameraDroneType = 0x205;

/// <summary>
/// Calls an artillery strike (or a sensor probe) for commander <paramref name="commanderId"/>: spends one of the
/// commander's strikes of that kind, creates the strike object at <paramref name="location"/> and, in a multiplayer
/// game run by the server, sends it to the others.
/// </summary>
/// <param name="strikeType">
/// 0-3 (and 4-7, the same kinds): which of the commander's four artillery counters is spent and which strike object
/// type is made. In multiplayer 4-6 are turned into 0-2.
/// </param>
/// <param name="seconds">Seconds until impact; -1 keeps the type's nominal time, under 3 means -1.</param>
/// <param name="randomOffset">Stored in the strike (<see cref="MCArtillery::RandomOffset"/>).</param>
void CallArtillery(int32_t commanderId, int32_t strikeType, MCVector3D location, int32_t seconds, int randomOffset);

/// <summary>
/// An artillery strike or sensor probe on its way: shows a countdown at the target, then sets off its explosion
/// pattern (or, for a probe, opens a shrinking sensor or launches a <see cref="MCCameraDrone"/>).
/// </summary>
/// <remarks>Original source: <c>object\artlry.cpp</c>, <c>object\artlry.h</c>.</remarks>
class MCArtillery : public MCBigGameObject
{
public:
    MCArtillery();
    /// <summary>Frees the sensor.</summary>
    ~MCArtillery() override;

    /// <summary>Sets up the object and, for a damaging strike, the table of explosions already set off.</summary>
    int32_t Init(std::shared_ptr<MCObjectType> objType) override;
    using MCBigGameObject::Init;
    int32_t Kill() override { return 0; }
    /// <summary>Counts down, animates, plays the incoming sound, sets off the explosions and runs the probe.</summary>
    /// <returns>0 when the strike is over.</returns>
    int32_t Update() override;
    /// <summary>Draws the countdown sprite and the time left.</summary>
    void Render() override;
    /// <summary>Selected by the mouse-over event (0x1c), deselected by 0x1d.</summary>
    int32_t HandleEvent(MCObjectEvent& event) override;
    /// <summary>
    /// After impact: sets off the mines in the 3x3 map tiles around the strike and runs collision checks with the
    /// terrain objects of the 3x3 terrain blocks around it.
    /// </summary>
    void HandleStaticCollision() override;
    /// <summary>Projects the strike to the screen; true when its sprite is inside the main camera.</summary>
    int OnScreen() override;

    /// <summary>
    /// First update or render: takes the type's times and start frame, and for a probe opens its sensor for the
    /// strike's side.
    /// </summary>
    void SetJustCreated();
    /// <summary>Recomputes the sprite's screen bounds; true when they overlap the camera's view.</summary>
    bool RecalcBounds(MCCamera& camera);
    /// <summary>Gives the sensor probe its team, time and range (-1 keeps the current value).</summary>
    void SetSensorData(std::shared_ptr<MCTeam> team, float sensorTime, float sensorRange);

    /// <summary>Set until the first update or render has run <see cref="SetJustCreated"/>.</summary>
    bool JustCreated = true;
    /// <summary>The sprite frame drawn (wraps at the type's frame count).</summary>
    uint32_t CurrentFrame = 0;
    /// <summary>Seconds the sprite has been animating.</summary>
    float FrameTime = 0;
    /// <summary>floor(frameTime * frameRate) at the last frame advance.</summary>
    int32_t FrameCount = 0;
    /// <summary>Screen bounds of the sprite: left, top.</summary>
    float BoundsLeft = 0;
    float BoundsTop = 0;
    /// <summary>Screen bounds of the sprite: right, bottom.</summary>
    float BoundsRight = 0;
    float BoundsBottom = 0;
    /// <summary>Seconds to impact (negative after it; -1 until set).</summary>
    float TimeToImpact = -1.0f;
    /// <summary>Seconds to launch (counts down with timeToImpact).</summary>
    float TimeToLaunch = -1.0f;
    /// <summary>The sensor probe's current range, world units.</summary>
    float SensorRange = 0;
    /// <summary>Seconds of sensor time left.</summary>
    float SensorTime = 0;
    /// <summary>The sensor probe's sensor (the sensor manager's).</summary>
    std::weak_ptr<MCSensorSystem> SensorSystem;
    /// <summary>Set once the strike has hit (from then on it collides and explodes).</summary>
    bool HasImpacted = false;
    /// <summary>Set once the sensor probe's sensor is running.</summary>
    bool SensorActive = false;
    /// <summary>
    /// Whether the impact is scattered: the last argument of <see cref="CallArtillery"/> (every caller passes 0). The
    /// scatter itself is gone from the original: when set, two RandomNumber(500) draws are made (and discarded) at
    /// impact.
    /// </summary>
    bool RandomOffset = true;
    /// <summary>Set once the incoming-shell sound has played.</summary>
    bool ImpactSoundPlayed = false;
    /// <summary>One flag per entry of the type's explosion pattern: set once that explosion went off.</summary>
    std::vector<bool> ExplosionsDone;
};

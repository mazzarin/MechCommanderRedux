#pragma once

#include "appear/MCAppearance.h"
#include "sprite/MCGVAppearanceType.h"

/// <summary>A ground vehicle: a body and an optional turret, each facing its own way.</summary>
/// <remarks>Original source: <c>sprite\gvactor.cpp</c>, <c>sprite\gvactor.h</c>.</remarks>
class MCGVAppearance : public MCAppearance
{
public:
    MCGVAppearance() = default;
    /// <summary>Drops the appearance's use of its type.</summary>
    ~MCGVAppearance() override;

    int32_t Init(std::shared_ptr<MCAppearanceType> tree = nullptr, std::weak_ptr<MCGameObject> obj = {}) override;

    /// <summary>Advances the animation and starts the target highlight.</summary>
    int32_t Update() override;

    /// <summary>Turns body and turret to the owner's facings and adds them to the draw list.</summary>
    int32_t Render(int32_t depthFixup = 0) override;

    std::shared_ptr<MCAppearanceType> GetAppearanceType() override { return AppearType; }

    /// <summary>Draws the vehicle's damage bar.</summary>
    void DrawBars() override;

    int RecalcBounds(MCOptionalRef<MCCamera> view) override;

    /// <summary>
    /// Port-only (SPEC-first-use.md: the camera's surroundings): the body's and the turret's packets as Render takes
    /// them, and the next rotation's either way, read ahead (MCAppearanceType::MCAskingAhead).
    /// </summary>
    void ReadAhead() override;

    /// <summary>Switches to <paramref name="state"/> when the type has it.</summary>
    void SetTypeId(MCGVActorState state)
    {
        if (StateExists(state) != 0)
        {
            CurrentState = state;
        }
    }

    /// <summary>The number of frames of <paramref name="state"/> (0 when the type hasn't it).</summary>
    int32_t StateExists(MCGVActorState state) const;

    /// <summary>The type.</summary>
    std::shared_ptr<MCGVAppearanceType> AppearType;
    /// <summary>The body's and the turret's shapes (expired once the cache drops them).</summary>
    std::array<std::weak_ptr<MCShape>, 2> CurrentShape;
    /// <summary>The body's and the turret's frames (-1: not started).</summary>
    std::array<int32_t, 2> CurrentFrame{-1, -1};
    /// <summary>The type's part count.</summary>
    int32_t NumParts = 0;
    /// <summary>The type's turret offset.</summary>
    float TurretOffset = 0.0f;
    /// <summary>Seconds into the animation.</summary>
    float CurrentTime = 0.0f;
    /// <summary>The frame rate (15 after init).</summary>
    float FrameRate = 15.0f;
    /// <summary>Frames played so far.</summary>
    int32_t LastFrame = 0;
    /// <summary>The current state.</summary>
    MCGVActorState CurrentState = MCGVActorState::Normal;
    /// <summary>The body's facing, in degrees.</summary>
    float BodyRotation = 0.0f;
    /// <summary>The turret's facing, in degrees (-180..180 then 0..360).</summary>
    float TurretRotation = 0.0f;
    /// <summary>The fade table (haze table index) to draw through, -1 for none.</summary>
    int32_t FadeTableIndex = -1;
    /// <summary>The shape's top-left offset from its hotspot (-25 before a shape).</summary>
    float ShapeMinX = -25.0f;
    float ShapeMinY = -25.0f;
    /// <summary>The shape's size (50 before a shape).</summary>
    float ShapeMaxX = 50.0f;
    float ShapeMaxY = 50.0f;

private:
    /// <summary>The body's facing (snapped to its state's drawn rotations) and the turret's, in degrees.</summary>
    struct MCGVRotations
    {
        float Body = 0.0f;
        float Turret = 0.0f;
    };

    /// <summary>The facings Render draws <paramref name="obj"/> at (from the last ones, for an object of another class).</summary>
    MCGVRotations DrawnRotations(MCGameObject& obj) const;

    /// <summary>
    /// Port-only: the degrees at the middle of the packet next to <paramref name="rotation"/>'s on
    /// <paramref name="side"/> (-1 or 1) among <paramref name="state"/>'s rotations, as the type picks a rotation's packet
    /// (MCGVAppearanceType::GetShape); <paramref name="rotation"/> itself when it has none.
    /// </summary>
    int32_t TurnedRotation(MCGVActorState state, float rotation, int32_t side) const;
};

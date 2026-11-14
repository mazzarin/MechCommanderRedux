#pragma once

#include "appear/MCAppearance.h"
#include "sprite/MCPUAppearanceType.h"

/// <summary>A pop-up turret: opens to fire, closes to hide.</summary>
/// <remarks>Original source: <c>sprite\puactor.cpp</c>, <c>sprite\puactor.h</c>.</remarks>
class MCPUAppearance : public MCAppearance
{
public:
    MCPUAppearance() = default;
    /// <summary>Drops the appearance's use of its type.</summary>
    ~MCPUAppearance() override;

    int32_t Init(std::shared_ptr<MCAppearanceType> tree = nullptr, std::weak_ptr<MCGameObject> obj = {}) override;

    /// <summary>Advances the animation (stopping on the state's last frame) and starts the target highlight.</summary>
    int32_t Update() override;

    int32_t Render(int32_t depthFixup = 0) override;

    std::shared_ptr<MCAppearanceType> GetAppearanceType() override { return AppearType; }

    /// <summary>Draws the turret's damage bar.</summary>
    void DrawBars() override;

    int RecalcBounds(MCOptionalRef<MCCamera> view) override;

    /// <summary>
    /// Port-only (SPEC-first-use.md: the camera's surroundings): the state's packet as Render takes it, and the next
    /// state's in the open and close cycle, read ahead (MCAppearanceType::MCAskingAhead).
    /// </summary>
    void ReadAhead() override;

    /// <summary>Switches to <paramref name="state"/> when the type has it.</summary>
    void SetTypeId(MCPUActorState state)
    {
        if (StateExists(state) != 0)
        {
            CurrentState = state;
        }
    }

    /// <summary>Switches to the destroyed state matching the current one.</summary>
    void SetDestroyed();

    /// <summary>
    /// Opens (<paramref name="combatMode"/>) or closes the turret, stepping through the opening and closing states as
    /// their animations finish. Returns the state.
    /// </summary>
    int32_t SetCombatMode(bool combatMode);

    /// <summary>The number of frames of <paramref name="state"/> (0 when absent).</summary>
    int32_t StateExists(MCPUActorState state) const;

    /// <summary>The type.</summary>
    std::shared_ptr<MCPUAppearanceType> AppearType;
    /// <summary>The shape drawn.</summary>
    std::weak_ptr<MCShape> CurrentShape;
    /// <summary>The frame drawn (-1: not started).</summary>
    int32_t CurrentFrame = -1;
    /// <summary>Seconds into the animation.</summary>
    float CurrentTime = 0.0f;
    /// <summary>The frame rate (15 after init).</summary>
    float FrameRate = 15.0f;
    /// <summary>Frames played so far.</summary>
    int32_t LastFrame = 0;
    /// <summary>The current state.</summary>
    MCPUActorState CurrentState = MCPUActorState::Closed;
    /// <summary>The facing, in degrees.</summary>
    float Rotation = 0.0f;
    /// <summary>The fade table (haze table index) to draw through, -1 for none; set by the owner.</summary>
    int32_t FadeTableIndex = -1;
    /// <summary>The shape's top-left offset from its hotspot (-25 before a shape).</summary>
    float ShapeMinX = -25.0f;
    float ShapeMinY = -25.0f;
    /// <summary>The shape's size (50 before a shape).</summary>
    float ShapeMaxX = 50.0f;
    float ShapeMaxY = 50.0f;

private:
    /// <summary>
    /// The facing Render draws <paramref name="obj"/> at in <paramref name="state"/>, snapped to that state's drawn
    /// rotations, in degrees.
    /// </summary>
    float DrawnRotation(MCGameObject& obj, MCPUActorState state) const;
};

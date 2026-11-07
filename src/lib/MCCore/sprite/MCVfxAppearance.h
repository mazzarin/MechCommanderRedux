#pragma once

#include "appear/MCAppearance.h"
#include "sprite/MCInBetween.h"
#include "sprite/MCVfxAppearanceType.h"

/// <summary>An object drawn from its type's shapes, animated through its states and facings.</summary>
/// <remarks>Original source: <c>sprite\actor.cpp</c>, <c>sprite\actor.h</c>.</remarks>
class MCVfxAppearance : public MCAppearance
{
public:
    MCVfxAppearance() = default;
    /// <summary>Drops the appearance's use of its type.</summary>
    ~MCVfxAppearance() override;

    /// <summary>Binds to <paramref name="tree"/> and resets the animation.</summary>
    int32_t Init(std::shared_ptr<MCAppearanceType> tree = nullptr, std::weak_ptr<MCGameObject> obj = {}) override;

    /// <summary>Advances the animation by the frame time.</summary>
    int32_t Update() override;

    /// <summary>Adds the current shape (and the selection box, bars) to the draw list.</summary>
    int32_t Render(int32_t depthFixup = 0) override;

    std::shared_ptr<MCAppearanceType> GetAppearanceType() override { return AppearType; }

    /// <summary>Draws the damage bar over a tree building.</summary>
    void DrawBars() override;

    /// <summary>Takes the shape's bounds and projects the owner; nonzero when on screen.</summary>
    int RecalcBounds(MCOptionalRef<MCCamera> view) override;

    /// <summary>
    /// Switches to <paramref name="state"/> (restarting it) and, in the normal state, to sub-state
    /// <paramref name="subState"/> (<see cref="NoSubState"/> for none).
    /// </summary>
    void SetTypeId(MCActorState state, uint8_t subState);

    /// <summary>Shows damage level <paramref name="damageLevel"/>: normal, blowing up 1 or 2, or destroyed.</summary>
    virtual void SetDamageLvl(uint32_t damageLevel);

    /// <summary>The number of frames of <paramref name="state"/> (0 when the type hasn't it).</summary>
    int32_t StateExists(MCActorState state) const;

    /// <summary>How <see cref="Render"/> draws the current state's frames.</summary>
    enum class MCFrameDrawing
    {
        /// <summary>The current frame.</summary>
        Plain,
        /// <summary>A delta animation: its first shape, and over it the frame's change (the next shape).</summary>
        Delta,
        /// <summary>Nothing: a delta animation of a type that isn't scaled.</summary>
        Nothing,
    };

    /// <summary>How <see cref="Render"/> draws the current state's frames.</summary>
    MCFrameDrawing FrameDrawing() const;

    /// <summary>
    /// Port-only (HD art): the in-between of the current frame toward the next that its share of the frame shown calls
    /// for (none in the frame's first step, at an end the animation stops at, or before it has started).
    /// </summary>
    std::optional<MCInBetween> InBetween() const;

    /// <summary>The type.</summary>
    std::shared_ptr<MCVfxAppearanceType> AppearType;
    /// <summary>The shape drawn (expired once the cache drops it).</summary>
    std::weak_ptr<MCShape> CurrentShape;
    /// <summary>The frame drawn (-1: not started).</summary>
    int32_t CurrentFrame = -1;
    /// <summary>Seconds into the current state.</summary>
    float CurrentTime = 0.0f;
    /// <summary>Frames advanced by the last update.</summary>
    float FramesAdvanced = 0.0f;
    /// <summary>Frames played so far in the current state.</summary>
    int32_t LastFrame = 0;
    /// <summary>Port-only (HD art's in-betweens): how far the animation is through the frame shown, 0 to 1.</summary>
    float FrameShare = 0.0f;
    /// <summary>Port-only (HD art's in-betweens): whether the current state's animation has gone back to its start.</summary>
    bool Looped = false;
    /// <summary>
    /// OB-187 (fixed): the state <see cref="SetTypeId"/> set last starts as under the original's frame lock: its
    /// first step is at least <see cref="LockedFrameLength"/> long. The expansion's trees fall and crumble from an
    /// empty first frame that the lock's first step passes; with faster frames the tree vanished until the next.
    /// </summary>
    bool StartsAsLocked = false;
    /// <summary>The frame the animation loops back to at <see cref="LoopEnd"/>.</summary>
    int32_t LoopStart = -1;
    /// <summary>The frame at which the animation goes back to <see cref="LoopStart"/> (-1: loop the whole state).</summary>
    int32_t LoopEnd = -1;
    /// <summary>The current state.</summary>
    MCActorState CurrentState = MCActorState::Normal;
    /// <summary>The current sub-state of the normal state, <see cref="NoSubState"/> for none.</summary>
    uint8_t CurrentSubState = NoSubState;
    /// <summary>
    /// The haze level of the palette's haze table the shape is drawn through (<c>GetHazePalette</c>), 0 for none.
    /// </summary>
    int32_t HazeLevel = 0;
    /// <summary>Set by <see cref="SetDamageLvl"/>.</summary>
    bool DamageSet = false;
    /// <summary>Set by <see cref="SetTypeId"/> and init.</summary>
    bool TypeChanged = true;
    /// <summary>Port-only (HD lighting): its frames are a light's glow (MCLight's), drawn as MCElement::Glow.</summary>
    bool Glow = false;
    /// <summary>The shape's top-left offset from its hotspot (from VFX_shape_minxy; -15 before a shape).</summary>
    float ShapeMinX = -15.0f;
    float ShapeMinY = -15.0f;
    /// <summary>The shape's size (VFX_shape_resolution; 15 before a shape).</summary>
    float ShapeMaxX = 15.0f;
    float ShapeMaxY = 15.0f;
};

/// <summary>
/// Draws the damage bar of <paramref name="obj"/> (a building or tree building) above
/// <paramref name="appearance"/>: centred over <paramref name="type"/>'s bounds, or the appearance's when the type
/// is null or has none. Nothing when <paramref name="obj"/> is null.
/// </summary>
/// <remarks>The body of <c>VFXAppearance::drawBars</c> and <c>VFXBuildingAppearance::drawBars</c>, which the original
/// repeats.</remarks>
void MCDrawDamageBar(const MCAppearance& appearance, std::shared_ptr<MCAppearanceType> type,
                     std::shared_ptr<MCGameObject> obj);

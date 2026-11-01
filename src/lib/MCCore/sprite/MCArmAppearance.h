#pragma once

#include "appear/MCAppearance.h"
#include "sprite/MCArmAppearanceType.h"
#include "sprite/MCInBetween.h"

/// <summary>A weapon effect's appearance, turned to its owner's direction of travel.</summary>
/// <remarks>Original source: <c>sprite\armactor.cpp</c>.</remarks>
class MCArmAppearance : public MCAppearance
{
public:
    MCArmAppearance() = default;
    /// <summary>Drops the appearance's use of its type.</summary>
    ~MCArmAppearance() override;

    int32_t Init(std::shared_ptr<MCAppearanceType> tree = nullptr, std::weak_ptr<MCGameObject> obj = {}) override;

    /// <summary>Advances the animation (stopping on the last frame).</summary>
    int32_t Update() override;

    /// <summary>Faces the owner's direction and adds the shape to the draw list.</summary>
    int32_t Render(int32_t depthFixup = 0) override;

    int RecalcBounds(MCOptionalRef<MCCamera> view) override;

    int32_t GetFrameNumber() override { return CurrentFrame; }

    /// <summary>
    /// Port-only (HD art): the in-between of the current frame toward the next that its share of the frame shown calls
    /// for (none in the frame's first step, on the last frame, or before the animation has started).
    /// </summary>
    std::optional<MCInBetween> InBetween() const;

    /// <summary>The type.</summary>
    std::shared_ptr<MCArmAppearanceType> AppearType;
    /// <summary>The shape drawn (expired once the cache drops it).</summary>
    std::weak_ptr<MCShape> CurrentShape;
    /// <summary>The frame drawn (-1: not started).</summary>
    int32_t CurrentFrame = -1;
    /// <summary>Whether the shape is drawn mirrored.</summary>
    bool Reverse = false;
    /// <summary>Seconds into the animation.</summary>
    float CurrentTime = 0.0f;
    /// <summary>The state's frame rate.</summary>
    float FrameRate = 0.0f;
    /// <summary>Frames played so far.</summary>
    int32_t LastFrame = 0;
    /// <summary>Port-only (HD art's in-betweens): how far the animation is through the frame shown, 0 to 1.</summary>
    float FrameShare = 0.0f;
    /// <summary>The facing, in degrees.</summary>
    float Rotation = 0.0f;
    /// <summary>The fade table (index into the palette's haze tables) to draw through, -1 for none.</summary>
    int32_t FadeTableIndex = -1;
    /// <summary>The shape's top-left offset from its hotspot (-15 before a shape).</summary>
    float ShapeMinX = -15.0f;
    float ShapeMinY = -15.0f;
    /// <summary>The shape's size (15 before a shape).</summary>
    float ShapeMaxX = 15.0f;
    float ShapeMaxY = 15.0f;
};

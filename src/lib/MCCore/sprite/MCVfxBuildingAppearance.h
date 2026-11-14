#pragma once

#include "sprite/MCVfxAppearance.h"
#include "sprite/MCVfxBuildingAppearanceType.h"

/// <summary>A building (or other terrain object) drawn as a damage frame over its ground tile.</summary>
/// <remarks>Original source: <c>sprite\bactor.cpp</c>, <c>sprite\bactor.h</c>.</remarks>
class MCVfxBuildingAppearance : public MCVfxAppearance
{
public:
    MCVfxBuildingAppearance() = default;

    int32_t Init(std::shared_ptr<MCAppearanceType> tree = nullptr, std::weak_ptr<MCGameObject> obj = {}) override;

    /// <summary>Starts the highlight when the building is first targeted.</summary>
    int32_t Update() override;

    /// <summary>Adds the tile, the building shape, bars and selection marks to the draw list.</summary>
    int32_t Render(int32_t depthFixup = 0) override;

    std::shared_ptr<MCAppearanceType> GetAppearanceType() override { return BuildType; }

    /// <summary>Draws the building's damage bar.</summary>
    void DrawBars() override;

    int RecalcBounds(MCOptionalRef<MCCamera> view) override;

    /// <summary>
    /// Port-only (SPEC-first-use.md: the camera's surroundings): the damage level's packet and the tile's as Render takes
    /// them, and the next level's, which a hit takes it to, read ahead (MCAppearanceType::MCAskingAhead).
    /// </summary>
    void ReadAhead() override;

    /// <summary>Picks damage frame <paramref name="damageLevel"/> (low 4 bits, clamped to the type's frames).</summary>
    void SetDamageLvl(uint32_t damageLevel) override;

    /// <summary>Sets the bounds from the current shape's frame, offset by the owner's footprint.</summary>
    void CalcCollideBounds();

    /// <summary>The damage frame drawn.</summary>
    uint32_t DamageLevel = 0;
    /// <summary>The ground tile drawn under the building (0..9).</summary>
    uint32_t TileNum = 0;
    /// <summary>The type.</summary>
    std::shared_ptr<MCVfxBuildingAppearanceType> BuildType;
    /// <summary>The tile's shape (expired once the cache drops it).</summary>
    std::weak_ptr<MCShape> TileShape;
    /// <summary>The animation state ABL setanimation picked, -1 for none.</summary>
    int32_t AnimState = -1;
};

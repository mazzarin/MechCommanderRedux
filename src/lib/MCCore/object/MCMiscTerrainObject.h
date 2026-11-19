#pragma once

#include "object/MCFire.h"
#include "object/MCGameObject.h"

class MCObjectEvent;

/// <summary>What a <see cref="MCMiscTerrainObject"/> is (set by the object block manager when it places it).</summary>
enum class MCMiscTerrainKind : int32_t
{
    /// <summary>Not placed yet.</summary>
    None = -1,
    Bridge = 5,
    Forest = 6,
    /// <summary>A heavy wall.</summary>
    Wall = 7,
    MediumWall = 8,
    LightWall = 9
};

/// <summary>
/// A damageable terrain feature at one terrain vertex: a bridge, a forest tile or a wall. It has no appearance of its
/// own; when destroyed it changes the terrain overlay tile and the move map's passability and line-of-fire bits.
/// </summary>
/// <remarks>
/// Original source: <c>object\bridge.cpp</c>, <c>object\bridge.h</c>. Derives <see cref="MCGameObject"/> directly (not
/// BigGameObject), as the original's vtable and constructor show.
/// </remarks>
class MCMiscTerrainObject : public MCGameObject
{
public:
    MCMiscTerrainObject();
    ~MCMiscTerrainObject() override;

    /// <summary>Marks the object just created, object class MiscTerrainObject, no damage.</summary>
    int32_t Init(std::shared_ptr<MCObjectType> objType) override;
    using MCGameObject::Init;
    int32_t Kill() override { return 0; }
    /// <summary>
    /// On the first update, places the object at its vertex and marks it destroyed already when its map tile shows
    /// it broken (the map says it was blown before).
    /// </summary>
    int32_t Update() override;
    /// <summary>
    /// Clears the passability bits of a destroyed feature once its fire is out, draws the damage bar when selected
    /// or pointed at, and the forest-edge shape over a burnt forest tile.
    /// </summary>
    void Render() override;
    std::shared_ptr<MCAppearance> GetAppearance() override { return nullptr; }
    /// <summary>Selected by the mouse-over event (0x1c), deselected by 0x1d.</summary>
    int32_t HandleEvent(MCObjectEvent& event) override;
    /// <summary>Its terrain block and vertex.</summary>
    void GetBlockAndVertexNumber(int32_t& blockNum, int32_t& vertexNum) override
    {
        blockNum = BlockNumber;
        vertexNum = VertexNumber;
    }

    /// <summary>
    /// Adds the shot's damage; past the kind's threshold it explodes (and a light wall plays its crumble sound). A
    /// forest tile also catches fire.
    /// </summary>
    int32_t HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) override;
    /// <summary>Forgets its fire (the fire ended).</summary>
    void KillFireObject() override { FireObject.BurntOut(); }
    float GetDamage() override { return Damage; }
    /// <summary>
    /// Sets the damage; past the kind's threshold the feature is destroyed: new overlay tile, the move map's tile
    /// made passable or blocked, and (for a bridge) the global move-map area closed.
    /// </summary>
    void SetDamage(float newDamage) override;
    /// <summary>
    /// Port-only (the workspace's SPEC-first-use.md): the overlay tile its vertex shows once it's destroyed
    /// (<see cref="SetDamage"/> adds its kind's step to the vertex's overlay), so the mission's load can have it in hand;
    /// none for a kind that changes none. Even one the map shows broken: its first update forgets that, so a hit steps
    /// it again.
    /// </summary>
    std::optional<int32_t> DestroyedOverlayTile() const;
    int IsBuilding() override { return 1; }
    /// <summary>Whether any of the four vertices around it is visible to the home team.</summary>
    int IsRevealed() override;
    /// <summary>Saves the line-of-fire bits of its map tile in <see cref="CellArray"/> and clears them.</summary>
    void ClearLineOfFire() override;
    /// <summary>Puts back the line-of-fire bits saved by <see cref="ClearLineOfFire"/>.</summary>
    void RestoreLineOfFire() override;

    using MCGameObject::GetScreenPos;
    /// <summary>Its vertex projected through the main camera.</summary>
    MCVector2D GetScreenPos() const;
    /// <summary>Draws the damage bar (green, yellow, red) above <paramref name="screenPos"/>.</summary>
    void DrawBars(MCVector2D screenPos);
    /// <summary>A 1-point hit, and more burning time for its fire if it has one.</summary>
    void LightOnFire(float timeToBurn);

    /// <summary>The line-of-fire bits of the nine cells of the tile, saved by <see cref="ClearLineOfFire"/>.</summary>
    static std::array<int32_t, 9> CellArray;

    /// <summary>Set until the first update places the object.</summary>
    bool JustCreated = true;
    /// <summary>The terrain vertex it stands on, within its block.</summary>
    int32_t VertexNumber = 0;
    /// <summary>The terrain block it stands in.</summary>
    int32_t BlockNumber = 0;
    /// <summary>Set once SetDamage has swapped the overlay tile for the destroyed one.</summary>
    bool OverlayDestroyed = false;
    /// <summary>Damage taken.</summary>
    float Damage = 0;
    /// <summary>Which feature it is.</summary>
    MCMiscTerrainKind Kind = MCMiscTerrainKind::None;
    /// <summary>The fire burning on it (made from the type's forest fire effect), if any.</summary>
    MCFireLink FireObject;
    /// <summary>Set once destroyed: further hits are ignored and render updates the map's passability.</summary>
    bool Destroyed = false;

private:
    /// <summary>Adds its kind's step to its vertex's overlay tile, which then shows it destroyed.</summary>
    void ShowDestroyedOverlay();
};

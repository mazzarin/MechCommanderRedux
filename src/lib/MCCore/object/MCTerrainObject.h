#pragma once

#include "object/MCBigGameObject.h"
#include "object/MCFire.h"

class MCCamera;
class MCObjectEvent;
class MCVfxAppearance;

/// <summary>
/// A static object on a terrain vertex (rocks, wrecks, props): it can be damaged and set on fire, and is drawn hazed
/// by how much of it the home team can see.
/// </summary>
/// <remarks>Original source: <c>object\terrobj.cpp</c>, <c>object\terrobj.h</c>.</remarks>
class MCTerrainObject : public MCBigGameObject
{
public:
    MCTerrainObject();
    ~MCTerrainObject() override;

    /// <summary>Makes the VFX appearance; a type with damage level 0 starts destroyed.</summary>
    int32_t Init(std::shared_ptr<MCObjectType> objType) override;
    using MCBigGameObject::Init;
    int32_t Kill() override { return 0; }
    /// <summary>
    /// On the first update, places the object in the world from its block, vertex and offsets, and measures its
    /// extent radius from the appearance when the type has none.
    /// </summary>
    int32_t Update() override;
    /// <summary>Draws the object hazed by how much of it is revealed, with its fire.</summary>
    void Render() override;
    void RenderAtEdge() override;
    std::shared_ptr<MCAppearance> GetAppearance() override;
    /// <summary>Selected by the mouse-over event (0x1c), deselected by 0x1d.</summary>
    int32_t HandleEvent(MCObjectEvent& event) override;
    /// <summary>Returns the block and vertex the object stands on.</summary>
    void GetBlockAndVertexNumber(int32_t& blockNum, int32_t& vertexNum) override
    {
        blockNum = BlockNumber;
        vertexNum = VertexNumber;
    }

    /// <summary>Applies a weapon hit (as whole damage, at most the damage level) and sets it burning.</summary>
    int32_t HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) override;
    void KillFireObject() override { FireObject.BurntOut(); }
    /// <summary>
    /// Sets the pixel offsets from <paramref name="offset"/> (the type's base pixel offsets win when nonzero) and the
    /// vertex and block numbers from <paramref name="numbers"/>.
    /// </summary>
    void SetTerrainPosition(MCVector2D& offset, MCVector2D& numbers) override;

    /// <summary>Sets the damage from a whole number.</summary>
    void SetDamage(int32_t newDamage) { Damage = static_cast<float>(newDamage); }
    using MCBigGameObject::SetDamage;

    /// <summary>Projects the object for <paramref name="cam"/>; true when on screen (stamps the render turn).</summary>
    bool IsVisible();

    /// <summary>
    /// Draws the object (not its fire) hazed by how many corners of its vertex square the home team sees, when any is.
    /// </summary>
    /// <returns>How many it sees.</returns>
    int32_t DrawAsSeen();
    /// <summary>
    /// Sets the object on fire (making the type's explosion object its fire) and adds burn time. The object draws the
    /// fire; nothing updates it, so it never burns out.
    /// </summary>
    void LightOnFire(float timeToBurn);

    /// <summary>Set until the first update has placed the object in the world.</summary>
    bool JustCreated = true;
    /// <summary>The object's VFX appearance.</summary>
    std::shared_ptr<MCVfxAppearance> Appearance;
    /// <summary>The pixel offset X of the object from its vertex.</summary>
    int32_t PixelOffsetX = 0;
    /// <summary>The pixel offset Y of the object from its vertex.</summary>
    int32_t PixelOffsetY = 0;
    /// <summary>The terrain vertex within its block.</summary>
    int32_t VertexNumber = 0;
    /// <summary>The terrain block.</summary>
    int32_t BlockNumber = 0;
    /// <summary>The map cell column of its vertex.</summary>
    int32_t CellColumn = 0;
    /// <summary>The map cell row of its vertex.</summary>
    int32_t CellRow = 0;
    /// <summary>The world X of its vertex.</summary>
    float VertexWorldX = 0;
    /// <summary>The world Y of its vertex.</summary>
    float VertexWorldY = 0;
    /// <summary>The elevation of its map cell, in meters.</summary>
    float CellElevation = 0;
    /// <summary>Set while the object is burning.</summary>
    bool Burning = false;
    /// <summary>The fire burning on the object.</summary>
    MCFireLink FireObject;
};

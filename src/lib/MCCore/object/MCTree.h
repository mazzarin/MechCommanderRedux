#pragma once

#include "object/MCBigGameObject.h"
#include "object/MCFire.h"

class MCCamera;
class MCObjectEvent;
class MCVfxAppearance;

/// <summary>
/// A tree on the map: a VFX appearance with a shadow. Weapons set it burning; a mover walking into it knocks it
/// down.
/// </summary>
/// <remarks>Original source: <c>object\tree.cpp</c>, <c>object\tree.h</c>.</remarks>
class MCTree : public MCBigGameObject
{
public:
    MCTree();
    ~MCTree() override;

    /// <summary>Makes the tree's VFX appearance; object class Tree, unburnt.</summary>
    int32_t Init(std::shared_ptr<MCObjectType> objType) override;
    using MCBigGameObject::Init;
    int32_t Kill() override { return 0; }
    /// <summary>
    /// On the first update, places the tree at its vertex and map tile and sets the type's extent radius from the
    /// appearance's size (fatal when larger than the collision grid's cells).
    /// </summary>
    int32_t Update() override;
    /// <summary>Advances the appearance (finishing a fall), then draws the tree, hazed by visibility, and its shadow.</summary>
    void Render() override;
    void RenderAtEdge() override;
    std::shared_ptr<MCAppearance> GetAppearance() override;
    /// <summary>Selected by the mouse-over event (0x1c), deselected by 0x1d.</summary>
    int32_t HandleEvent(MCObjectEvent& event) override;
    /// <summary>Its terrain block and vertex.</summary>
    void GetBlockAndVertexNumber(int32_t& blockNum, int32_t& vertexNum) override
    {
        blockNum = BlockNumber;
        vertexNum = VertexNumber;
    }

    /// <summary>
    /// A hit adds one point of damage and burns the tree: the burnt animation, and the first time a fire of the
    /// type's explosion object.
    /// </summary>
    int32_t HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) override;
    /// <summary>Forgets its fire (the fire ended).</summary>
    void KillFireObject() override { FireObject.BurntOut(); }
    /// <summary>
    /// Takes its pixel offset from <paramref name="pixelOffset"/> and its block and vertex from
    /// <paramref name="blockVertex"/>.
    /// </summary>
    void SetTerrainPosition(MCVector2D& pixelOffset, MCVector2D& blockVertex) override;
    /// <summary>The tree's orientation (tilted once knocked down).</summary>
    MCFrameOfRef GetFrame() override { return TreeFrame; }
    void SetFrame(MCFrameOfRef& newFrame) override { TreeFrame = newFrame; }
    /// <summary>Whether any of the four vertices around it is visible to the home team.</summary>
    int IsRevealed() override;

    /// <summary>
    /// Projects its vertex through <paramref name="cam"/>; true (and remembers the turn) when the tree or its shadow
    /// is on screen.
    /// </summary>
    bool IsVisible();

    /// <summary>
    /// Draws the tree (with its art's shadow, but at dusk and night), hazed by how many corners of its vertex square the
    /// home team sees, when any is.
    /// </summary>
    void DrawAsSeen();
    /// <summary>A 25-point hit, and more burning time for its fire if it has one.</summary>
    void LightOnFire(float timeToBurn);

    /// <summary>Set until the first update places the tree.</summary>
    bool JustCreated = true;
    /// <summary>The tree's VFX appearance.</summary>
    std::shared_ptr<MCVfxAppearance> Appearance;
    /// <summary>Horizontal pixel offset of its base on the tile.</summary>
    int32_t PixelOffsetX = 0;
    /// <summary>Vertical pixel offset of its base on the tile.</summary>
    int32_t PixelOffsetY = 0;
    /// <summary>The terrain vertex it stands on, within its block.</summary>
    int32_t VertexNumber = 0;
    /// <summary>The terrain block it stands in.</summary>
    int32_t BlockNumber = 0;
    /// <summary>The move-map column of its tile.</summary>
    int32_t TileCol = 0;
    /// <summary>The move-map row of its tile.</summary>
    int32_t TileRow = 0;
    /// <summary>World X of its tile's corner.</summary>
    float TileWorldX = 0;
    /// <summary>World Y of its tile's corner.</summary>
    float TileWorldY = 0;
    /// <summary>Elevation of its map tile, in meters.</summary>
    float TileElevation = 0;
    /// <summary>Set once a fire has been started on the tree; cleared by render when the fire is gone.</summary>
    bool FireStarted = false;
    /// <summary>The fire burning on it, if any.</summary>
    MCFireLink FireObject;
    /// <summary>Set by TreeType::handleCollision while the tree is falling; render clears it when the fall ends.</summary>
    bool Falling = false;
    /// <summary>Set once the tree has fallen; it can't be knocked down again.</summary>
    bool Fallen = false;
    /// <summary>Set once a weapon has hit (burnt) the tree: it falls with the burnt animation.</summary>
    bool Burnt = false;
    /// <summary>The tree's orientation, the identity until it is knocked down.</summary>
    MCFrameOfRef TreeFrame;
};

#pragma once

#include "object/MCBigGameObject.h"
#include "object/MCFire.h"

class MCCamera;
class MCMechWarrior;
class MCObjectEvent;
class MCSensorSystem;
class MCVfxBuildingAppearance;

/// <summary>
/// A building standing on a terrain vertex: it can be damaged, burn, be destroyed (letting out marines), be
/// captured, hold a sensor, and shows a blip on the tactical view when it belongs to the enemy.
/// </summary>
/// <remarks>Original source: <c>object\bldng.cpp</c>, <c>object\bldng.h</c>.</remarks>
class MCBuilding : public MCBigGameObject
{
public:
    /// <summary>
    /// The pilots a building can hold (a game rule: capturing the building moves them into the capturing vehicle's
    /// four passenger seats).
    /// </summary>
    static constexpr size_t MaxPrisoners = 4;

    MCBuilding();
    /// <summary>Frees the sensor.</summary>
    ~MCBuilding() override;

    /// <summary>
    /// Makes the building's VFX building appearance, copies tonnage, explosion and combat value from the type, loads
    /// its name, and sets up its team and sensor.
    /// </summary>
    int32_t Init(std::shared_ptr<MCObjectType> objType) override;
    using MCBigGameObject::Init;
    int32_t Kill() override { return 0; }
    /// <summary>On the first update, places the building in the world from its block, vertex and pixel offsets.</summary>
    int32_t Update() override;
    /// <summary>
    /// Draws the building (hazed by how much of it is revealed), burns it, plays its looping sound and draws the
    /// enemy blip.
    /// </summary>
    void Render() override;
    void RenderAtEdge() override;
    std::shared_ptr<MCAppearance> GetAppearance() override;
    /// <summary>Selected by the mouse-over event (0x1c), deselected by 0x1d.</summary>
    int32_t HandleEvent(MCObjectEvent& event) override;
    /// <summary>Returns the block and vertex the building stands on.</summary>
    void GetBlockAndVertexNumber(int32_t& blockNum, int32_t& vertexNum) override
    {
        blockNum = BlockNumber;
        vertexNum = VertexNumber;
    }

    /// <summary>
    /// Applies a weapon hit: past the damage level the building is destroyed (sensor off, fire, explosion, salvage
    /// removed, marines let out).
    /// </summary>
    int32_t HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) override;
    void KillFireObject() override { FireObject.BurntOut(); }
    /// <summary>
    /// Sets the pixel offsets from <paramref name="offset"/> (the type's base pixel offsets win unless the ground
    /// tile is 10) and the vertex and block numbers from <paramref name="numbers"/>.
    /// </summary>
    void SetTerrainPosition(MCVector2D& offset, MCVector2D& numbers) override;
    /// <summary>Sets the damage and shows the matching damage frame (at most 15).</summary>
    void SetDamage(float newDamage) override;
    /// <summary>Sets the alignment and moves the sensor to the matching team.</summary>
    void SetAlignment(int32_t align) override;
    void SetCommanderId(int32_t id) override { CommanderId = static_cast<int8_t>(id); }
    int32_t GetCommanderId() override { return CommanderId; }
    /// <summary>Whether it can be captured: flagged captureable and not destroyed (nor, single-player, already captured).</summary>
    int IsCaptureable() override;
    /// <summary>Whether it holds a prisoner.</summary>
    int IsPrison() override;
    /// <summary>Whether any corner of its vertex is visible to the home team.</summary>
    int IsRevealed() override;

    /// <summary>Projects the building for <paramref name="cam"/>; true when on screen (stamps the render turn).</summary>
    bool IsVisible();

    /// <summary>
    /// Draws the building hazed by how many corners of its vertex square the home team sees, when any is.
    /// </summary>
    /// <returns>How many it sees.</returns>
    int32_t DrawAsSeen();
    /// <summary>
    /// Sets the building on fire (making the fire object if needed) and adds burn time. Original behaviour (OB-154):
    /// that fire is never updated or drawn.
    /// </summary>
    void LightOnFire(float timeToBurn);
    /// <summary>Gives the building a sensor of <paramref name="range"/> (if above -1), optionally on a team.</summary>
    void SetSensorData(std::shared_ptr<MCTeam> team, float range, bool setTeam);
    /// <summary>Lets the type's number of marines out of the destroyed building.</summary>
    void CreateBuildingMarines();

    /// <summary>Set until the first update has placed the building in the world.</summary>
    bool JustCreated = true;
    /// <summary>The building's VFX building appearance.</summary>
    std::shared_ptr<MCVfxBuildingAppearance> Appearance;
    /// <summary>The pixel offset X of the building from its vertex.</summary>
    int32_t PixelOffsetX = 0;
    /// <summary>The pixel offset Y of the building from its vertex.</summary>
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
    /// <summary>Set while the building is burning.</summary>
    bool Burning = false;
    /// <summary>The fire burning on the building.</summary>
    MCFireLink FireObject;
    /// <summary>Seconds since the last burn damage.</summary>
    float BurnTime = 0;
    /// <summary>
    /// The ground tile drawn under it (copied to the appearance each frame); at 10 the type's base pixel offsets are
    /// not used.
    /// </summary>
    uint8_t TileNum = 0;
    /// <summary>The handle of the looping sound playing while it is visible, or 0xffffffff.</summary>
    uint32_t SoundHandle = 0xffffffff;
    /// <summary>The building's sensor, if it has one (the sensor manager's).</summary>
    std::weak_ptr<MCSensorSystem> SensorSystem;
    /// <summary>Set when it can be captured (by the ABL SetCaptureable).</summary>
    bool Captureable = false;
    /// <summary>The commander id (-1 = none).</summary>
    int8_t CommanderId = -1;
    /// <summary>The building's name, loaded from its string resource.</summary>
    std::string Name;
    /// <summary>
    /// The prison slots: the pilots held here (put there by ABL's prisoner routine); capturing the building moves
    /// them into the capturing vehicle's passenger seats.
    /// </summary>
    std::array<std::weak_ptr<MCMechWarrior>, MaxPrisoners> PrisonSlots{};
};

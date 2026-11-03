#pragma once

#include "lib/MCVector3D.h"
#include "main/MCGameContext.h"

/// <summary>A crater or footprint on the ground.</summary>
struct MCCraterData
{
    /// <summary>The crater shape (packet of the crater PAK), -1 for a free slot.</summary>
    int32_t CraterShapeId = -1;
    /// <summary>Where it is in the world.</summary>
    MCVector3D Position;
    /// <summary>The frame of the shape drawn (the shapes hold one frame per facing).</summary>
    int32_t Rotation = -1;
    /// <summary>Port-only (HD weather's snow): the game's time it was made (ScenarioTime), by which the snow fills it in.</summary>
    float MadeAt = 0.0f;
};

/// <summary>
/// The craters and footprints left on the ground: a ring of slots (the oldest is overwritten), drawn under everything
/// else with shapes from a PAK.
/// </summary>
/// <remarks>
/// Original source: <c>engine\crater.cpp</c>. The PAK holds the full-size shapes in its first half and the zoomed-out
/// ones in its second half.
/// </remarks>
class MCCraterManager
{
public:
    /// <summary>
    /// A ring of <paramref name="numCraters"/> free slots, drawn with <paramref name="shapes"/> (a PAK's packets: the
    /// full-size half, then the zoomed-out half), the PAK named <paramref name="fileName"/> (port: the HD art pack's
    /// folder for them).
    /// </summary>
    MCCraterManager(int32_t numCraters, std::vector<std::vector<uint8_t>> shapes, std::string fileName = {});

    ~MCCraterManager();
    MCCraterManager(const MCCraterManager&) = delete;
    MCCraterManager& operator=(const MCCraterManager&) = delete;

    /// <summary>
    /// Room for <paramref name="numCraters"/> craters (port: <see cref="HdFootprintsScale"/> times as many with
    /// <see cref="GHdFootprints"/> on), with every shape of the PAK <paramref name="craterFileName"/> (from the sprite
    /// path, or the CD's).
    /// </summary>
    static std::expected<std::unique_ptr<MCCraterManager>, std::string> Create(int32_t numCraters,
                                                                               std::string_view craterFileName);

    /// <summary>
    /// Adds crater <paramref name="craterType"/> at <paramref name="position"/> facing <paramref name="rotation"/>,
    /// when the tile there is seen by the player's side, isn't water or a road-like overlay, and isn't a bridge tile.
    /// </summary>
    void AddCrater(int32_t craterType, const MCVector3D& position, int32_t rotation);

    /// <summary>Puts a crater in the next slot of the ring, over the oldest when every slot is taken.</summary>
    void Place(int32_t craterType, const MCVector3D& position, int32_t rotation);

    /// <summary>
    /// Adds a VFX element for every crater on screen, in a group at the bottom of the draw order; port-only, under the
    /// snow, each filled in by its age (MCWeather::FilledIn).
    /// </summary>
    void Render();

    /// <summary>The slots.</summary>
    std::span<const MCCraterData> Craters() const { return _Craters; }

    /// <summary>The slot the next crater goes in.</summary>
    int32_t NextSlot() const { return _NextSlot; }

    /// <summary>The number of shapes in the PAK.</summary>
    int32_t ShapeCount() const { return _ShapeCount; }

    /// <summary>The shapes (port: the HD art audit names the draws of them).</summary>
    std::span<const std::vector<uint8_t>> Shapes() const { return _Shapes; }

    /// <summary>
    /// Port-only: ties the shapes to the HD art pack's frames of them (sprite/MCHdArt.h's craters/) when HD art is on;
    /// done at mission load, and again by whoever turns HD art on during one.
    /// </summary>
    void AttachHdArt() const;

private:
    /// <summary>The slots.</summary>
    std::vector<MCCraterData> _Craters;
    /// <summary>The slot the next crater goes in.</summary>
    int32_t _NextSlot = 0;
    /// <summary>The shapes (at least 11 entries; empty past the PAK's packets).</summary>
    std::vector<std::vector<uint8_t>> _Shapes;
    /// <summary>The number of shapes (packets) the PAK holds.</summary>
    int32_t _ShapeCount = 0;
    /// <summary>The PAK's name, lower case (the HD art pack's folder for its shapes).</summary>
    std::string _FileName;
    /// <summary>Keeps the HD art pack's ties to the shapes while the manager lives.</summary>
    std::shared_ptr<const int> _HdArtOwner = std::make_shared<const int>(0);
};

/// <summary>
/// Port-only: PREFS "HDFootprints": 1 keeps <see cref="HdFootprintsScale"/> times as many footprints and craters on the
/// ground as the mission's ring holds (its CraterSystem NumCraters) before the oldest give way, and prints a walking
/// mech's feet where its legs are drawn stepping (OB-185, MCBattleMech::Update); 0 as the original. Off by default; a
/// mission takes the ring's length as it loads.
/// </summary>
extern int GHdFootprints;

/// <summary>With <see cref="GHdFootprints"/> on, the mission's ring of craters is this many times as long.</summary>
inline constexpr int32_t HdFootprintsScale = 3;

/// <summary>The mission's craters (set for the whole scenario).</summary>
MCCraterManager& CraterManager();

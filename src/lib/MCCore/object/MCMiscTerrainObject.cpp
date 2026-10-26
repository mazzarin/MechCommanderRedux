#include "stdafx.h"
#include "object/MCMiscTerrainObject.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "camera/MCCamera.h"
#include "camera/MCViewWindow.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCPolygonElement.h"
#include "engine/MCVfxElement.h"
#include "lib/MCFatal.h"
#include "network/MCMultiPlayer.h"
#include "object/MCMiscTerrainObjectType.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectEvent.h"
#include "object/MCObjectSystem.h"
#include "object/MCVertexPlacement.h"
#include "object/MCWeaponShotInfo.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTerrain.h"
#include "terrain/MCTerrainTiles.h"

namespace
{
    /// <summary>The map tile under the object's position.</summary>
    MCMapTile& TileAt(const MCMiscTerrainObject& object, int32_t& tileR, int32_t& tileC)
    {
        int32_t cellR = 0;
        int32_t cellC = 0;
        tileR = 0;
        tileC = 0;
        MCScenarioMap::WorldToMapPos(object.Position, tileR, tileC, cellR, cellC);
        return GameMap().Map[GameMap().Width * tileR + tileC];
    }

    /// <summary>How many of the tile's nine cells are passable.</summary>
    int32_t CountPassable(const MCMapTile& tile)
    {
        int32_t count = 0;

        for (uint32_t shift = 0; shift < 0x12; shift += 2)
        {
            count += static_cast<int32_t>((tile.Cells & (0x4000u << shift)) >> (shift + 0xe));
        }

        return count;
    }

    /// <summary>Sets every cell of the tile: passable to <paramref name="passable"/>, see-through.</summary>
    void SetAllCells(MCMapTile& tile, uint32_t passable)
    {
        for (uint32_t shift = 0; shift < 0x12; shift += 2)
        {
            tile.Cells = (passable << (shift + 0xe)) | (~(0x4000u << shift) & tile.Cells);
            tile.Cells = (~(0x8000u << shift) & tile.Cells) | (1u << (shift + 0xf));
        }
    }

    /// <summary>
    /// Makes every cell of a burnt forest tile passable (unless its terrain is type 0x29 or 0x2a) and see-through.
    /// </summary>
    void ClearForestCells(MCMapTile& tile)
    {
        for (uint32_t shift = 0; shift < 0x12; shift += 2)
        {
            if ((tile.Cells & 0x7f) != 0x29 && (tile.Cells & 0x7f) != 0x2a)
            {
                tile.Cells = (1u << (shift + 0xe)) | (~(0x4000u << shift) & tile.Cells);
            }

            tile.Cells = (1u << (shift + 0xf)) | (~(0x8000u << shift) & tile.Cells);
        }
    }

    /// <summary>The damage level that destroys a misc terrain object of <paramref name="kind"/>, or 0.</summary>
    int32_t DmgLevelFor(const MCMiscTerrainObjectType& type, MCMiscTerrainKind kind)
    {
        switch (kind)
        {
            case MCMiscTerrainKind::Bridge:
                return static_cast<int32_t>(type.BridgeDmgLevel);
            case MCMiscTerrainKind::Forest:
                return static_cast<int32_t>(type.ForestDmgLevel);
            case MCMiscTerrainKind::Wall:
                return static_cast<int32_t>(type.WallDmgLevel);
            case MCMiscTerrainKind::MediumWall:
                return static_cast<int32_t>(type.MediumWallDmgLevel);
            case MCMiscTerrainKind::LightWall:
                return static_cast<int32_t>(type.LightWallDmgLevel);
            default:
                return 0;
        }
    }

    /// <summary>Whether <paramref name="kind"/> is one of the three walls.</summary>
    bool IsWall(MCMiscTerrainKind kind)
    {
        return kind == MCMiscTerrainKind::Wall || kind == MCMiscTerrainKind::MediumWall ||
               kind == MCMiscTerrainKind::LightWall;
    }
} // namespace

std::array<int32_t, 9> MCMiscTerrainObject::CellArray = {};

MCMiscTerrainObject::MCMiscTerrainObject() = default;

MCMiscTerrainObject::~MCMiscTerrainObject() = default;

auto MCMiscTerrainObject::Update() -> int32_t
{
    if (!JustCreated)
    {
        return 1;
    }

    // Set the object on its vertex, 60 pixels down the tile.
    JustCreated = false;
    OverlayDestroyed = false;
    CollisionsOn = 1;
    Position = PlaceOnVertex(Position, BlockNumber, VertexNumber, 0, 60);

    // An object whose tile already shows it broken (a blocked bridge; an open wall or forest) starts destroyed.
    int32_t tileR;
    int32_t tileC;
    const MCMapTile& tile = TileAt(*this, tileR, tileC);
    const auto& type = TypeAs<MCMiscTerrainObjectType>();

    if (Kind == MCMiscTerrainKind::Bridge)
    {
        for (uint32_t shift = 0; shift < 0x12; shift += 2)
        {
            if (((0x4000u << shift) & tile.Cells) >> (shift + 0xe) == 0)
            {
                Status = 2;
                Damage = static_cast<float>(static_cast<int32_t>(type.BridgeDmgLevel));
            }
        }
    }
    else if (Kind == MCMiscTerrainKind::Forest)
    {
        if (CountPassable(tile) == 9)
        {
            Damage = static_cast<float>(static_cast<int32_t>(type.ForestDmgLevel));
            Status = 2;
        }
    }
    else if (IsWall(Kind))
    {
        if (CountPassable(tile) == 9)
        {
            Damage = static_cast<float>(static_cast<int32_t>(type.WallDmgLevel));
            Status = 2;
        }
    }

    return 1;
}

auto MCMiscTerrainObject::HandleEvent(MCObjectEvent& event) -> int32_t
{
    if (event.Type == 0)
    {
        if (event.Id == 0x1c)
        {
            Selected = 1;
        }
        else if (event.Id == 0x1d)
        {
            Selected = 0;
        }
    }

    return 0;
}

auto MCMiscTerrainObject::GetScreenPos() const -> MCVector2D
{
    MCVector2D screenPos;
    MCCamera::VertexProject(BlockNumber, VertexNumber, screenPos);
    return screenPos;
}

auto MCMiscTerrainObject::Render() -> void
{
    // Once the fire is out, a destroyed object's tile is opened up (a destroyed bridge stays impassable).
    if (FireObject == nullptr && Destroyed)
    {
        int32_t tileR;
        int32_t tileC;
        MCMapTile& tile = TileAt(*this, tileR, tileC);

        if (Kind == MCMiscTerrainKind::Bridge)
        {
            SetAllCells(tile, 0);
        }
        else if (Kind == MCMiscTerrainKind::Forest)
        {
            ClearForestCells(tile);
        }
        else if (IsWall(Kind))
        {
            SetAllCells(tile, 1);
        }
    }

    MCVector2D screenPos;

    if (MCCamera::VertexProject(BlockNumber, VertexNumber, screenPos) == 0 || IsRevealed() == 0)
    {
        return;
    }

    if ((Selected == -1 || Selected == 1) && static_cast<uint8_t>(Status) != 2)
    {
        DrawBars(screenPos);
    }

    if (Kind != MCMiscTerrainKind::Forest)
    {
        return;
    }

    // A forest draws the edge shape that matches its overlay tile, hazed by how many corners the home team sees.
    const int32_t overlayTile = Terrain()->GetOverlayTile(BlockNumber, VertexNumber);
    int32_t edge;

    if (overlayTile < 0xd0e)
    {
        edge = overlayTile - 0xd0a;
    }
    else
    {
        if (0xd6d < overlayTile || overlayTile - 0xd66 < 0)
        {
            return;
        }

        edge = overlayTile - 0xd62;
    }

    if (edge < 0)
    {
        return;
    }

    const int32_t numVisible = MCVertexCell::Of(BlockNumber, VertexNumber).VisibleCorners();
    std::optional<MCColorTable> hazePalette;

    if (numVisible != 4)
    {
        hazePalette = HazePaletteFor(numVisible);
    }

    // Each edge has a full-size frame and, after it, a half-size one (for the original's zoomed-out mode).
    const int32_t frame = edge * 2;
    constexpr int32_t depthOffset = 0x3a;
    ElementList().OpenGroup(static_cast<int32_t>(-screenPos.Y - static_cast<float>(depthOffset)), true);
    const std::span<uint8_t> edgeShapes = TypeAs<MCMiscTerrainObjectType>().ForestEdgeShapes.Bytes();
    ElementList().Add(ElementList().Make<MCVfxElement>(edgeShapes, screenPos.X, screenPos.Y, frame, 0, hazePalette, 1));
}

auto MCMiscTerrainObject::DrawBars(MCVector2D screenPos) -> void
{
    // The damage bar over a selected wall, bridge or forest: green, then yellow under half, red at a fifth.
    // Port: an overlay, on the screen over the view: it follows the object through the zoom, its size doesn't change.
    screenPos = MCOverlayPoint(screenPos);
    MCPolyElementData data;
    constexpr float barWidth = 38.0f;
    constexpr float barHeight = 4.0f;
    const float top = (screenPos.Y - 6.0f) - barHeight;
    const auto left = static_cast<float>(std::floor(static_cast<double>(screenPos.X - barWidth * 0.5f)));
    int32_t damageTaken = static_cast<int16_t>(static_cast<int32_t>(std::floor(static_cast<double>(GetDamage()))));
    const int32_t maxDamage = DmgLevelFor(*&TypeAs<MCMiscTerrainObjectType>(), Kind);

    if (maxDamage < damageTaken)
    {
        damageTaken = maxDamage;
    }

    // Faithful: an object of no known kind divides by zero here.
    const float health = 1.0f - static_cast<float>(damageTaken) / static_cast<float>(maxDamage);
    int32_t barColor;

    if (health < 0.5f)
    {
        barColor = health <= 0.2f ? 0x103 : 0x102;
    }
    else
    {
        barColor = 0x101;
    }

    float barLength = health * barWidth;

    if (barLength < 1.0f && 0.001f < health)
    {
        barLength = 1.0f;
    }

    ElementList().OpenGroup(-50000, true);
    data.NumVertices = 0;
    data.TextureMapOff = false;
    data.Texture = {};
    data.FadeTable = std::nullopt;
    data.Translate = false;
    data.StatusBar = true;
    data.BarColor = barColor;
    data.Vertices[0].X = static_cast<int16_t>(static_cast<int32_t>(std::floor(static_cast<double>(left - 1.0f))));
    data.Vertices[0].Y = static_cast<int16_t>(static_cast<int32_t>(std::floor(static_cast<double>(top - 1.0f))));
    data.Vertices[1].X =
        static_cast<int16_t>(static_cast<int32_t>(std::floor(static_cast<double>(left + barWidth + 1.0f))));
    data.Vertices[1].Y =
        static_cast<int16_t>(static_cast<int32_t>(std::floor(static_cast<double>(top + barHeight + 1.0f))));
    data.BarPercent = static_cast<int16_t>(static_cast<int32_t>(std::floor(static_cast<double>(barLength))));
    data.MeasuredBar = barLength;

    if (0 < data.BarPercent)
    {
        ElementList().Add(ElementList().Make<MCPolygonElement>(data, -50000));
    }
}

auto MCMiscTerrainObject::SetDamage(float newDamage) -> void
{
    if (newDamage != Damage)
    {
        Damage = newDamage;
    }

    const auto& type = TypeAs<MCMiscTerrainObjectType>();
    int32_t tileR;
    int32_t tileC;

    if (Kind == MCMiscTerrainKind::Bridge)
    {
        // A destroyed bridge: the broken overlay, its span closed to movement (and its global map area shut).
        if (Damage < static_cast<float>(static_cast<int32_t>(type.BridgeDmgLevel)) || OverlayDestroyed)
        {
            return;
        }

        Terrain()->SetOverlayTile(BlockNumber, VertexNumber, 0xf);
        MCTerrain::ForceRedraw = true;
        OverlayDestroyed = true;
        Destroyed = true;
        Status = 2;
        MCMapTile& tile = TileAt(*this, tileR, tileC);

        switch (tile.Overlay & 0x7f)
        {
            case 0x25:
                tile.Overlay = (tile.Overlay & 0xffffffa6) | 0x26;
                break;
            case 0x27:
                tile.Overlay = (tile.Overlay & 0xffffffa8) | 0x28;
                break;
            case 0x37:
                tile.Overlay = (tile.Overlay & 0xffffffb8) | 0x38;
                break;
            case 0x39:
                tile.Overlay = (tile.Overlay & 0xffffffba) | 0x3a;
                break;
            default:
                break;
        }

        const int32_t area = GlobalMoveMap().CalcArea(tileR, tileC);

        if (area < 0)
        {
            Fatal(0, "Bad Global Area to close");
        }
        else
        {
            GlobalMoveMap().CloseArea(area);
        }

        SetAllCells(tile, 0);
        return;
    }

    if (Kind == MCMiscTerrainKind::Forest)
    {
        // A burnt forest: the cleared overlay, passable.
        if (Damage < static_cast<float>(static_cast<int32_t>(type.ForestDmgLevel)) || OverlayDestroyed)
        {
            return;
        }

        const int32_t overlayTile = Terrain()->GetOverlayTile(BlockNumber, VertexNumber);
        Terrain()->SetOverlayTile(BlockNumber, VertexNumber, overlayTile < 0xd0a || 0xd0d < overlayTile ? 8 : 4);
        MCTerrain::ForceRedraw = true;
        OverlayDestroyed = true;
        Destroyed = true;
        Status = 2;
        MCMapTile& tile = TileAt(*this, tileR, tileC);
        tile.Overlay = (tile.Overlay & 0xffffffbf) | 0x3f;
        ClearForestCells(tile);
        return;
    }

    if (IsWall(Kind))
    {
        // A knocked-down wall: the rubble overlay, passable once any fire is out.
        if (Damage < static_cast<float>(DmgLevelFor(type, Kind)) || OverlayDestroyed)
        {
            return;
        }

        Terrain()->GetOverlayTile(BlockNumber, VertexNumber);
        Terrain()->SetOverlayTile(BlockNumber, VertexNumber, 0x13);
        MCTerrain::ForceRedraw = true;
        OverlayDestroyed = true;
        Destroyed = true;
        Status = 2;
        MCMapTile& tile = TileAt(*this, tileR, tileC);
        tile.Overlay = (tile.Overlay & 0xffffffbd) | 0x3d;

        if (FireObject == nullptr)
        {
            SetAllCells(tile, 1);
        }
    }
}

auto MCMiscTerrainObject::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    if (const int32_t result = MCGameObject::Init(objType); result != 0)
    {
        return result;
    }

    JustCreated = true;
    CollisionsOn = 0;
    ObjectClass = MCObjectClass::MiscTerrainObject;
    Damage = 0.0f;
    Alignment = 0;
    return 0;
}

auto MCMiscTerrainObject::LightOnFire(float timeToBurn) -> void
{
    MCWeaponShotInfo shot;
    shot.Init(nullptr, -1, 1.0f, 0, 0.0f);

    if (MultiPlayer() == nullptr)
    {
        HandleWeaponHit(shot, 0);
    }
    else if (MultiPlayer()->IsServer != 0)
    {
        HandleWeaponHit(shot, 1);
    }

    if (FireObject != nullptr)
    {
        FireObject->AddTimeLeftToBurn(timeToBurn);
    }
}

auto MCMiscTerrainObject::ClearLineOfFire() -> void
{
    // Save each cell's line-of-sight bit and make it see-through (so a shot can pass while it is checked).
    int32_t tileR;
    int32_t tileC;
    MCMapTile& tile = TileAt(*this, tileR, tileC);
    size_t cell = 0;

    for (uint32_t shift = 0; shift < 0x12; shift += 2, cell++)
    {
        const uint32_t bit = 0x8000u << shift;
        CellArray[cell] = static_cast<int32_t>((tile.Cells & bit) >> (shift + 0xf));
        tile.Cells = (~bit & tile.Cells) | (1u << (shift + 0xf));
    }
}

auto MCMiscTerrainObject::RestoreLineOfFire() -> void
{
    int32_t tileR;
    int32_t tileC;
    MCMapTile& tile = TileAt(*this, tileR, tileC);
    size_t cell = 0;

    for (uint32_t shift = 0; shift < 0x12; shift += 2, cell++)
    {
        tile.Cells = (static_cast<uint32_t>(CellArray[cell]) << (shift + 0xf)) | (~(0x8000u << shift) & tile.Cells);
    }
}

auto MCMiscTerrainObject::HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) -> int32_t
{
    if (addMultiplayChunk != 0)
    {
        MultiPlayer()->AddWeaponHitChunk(*this, shotInfo, 0);
    }

    if (Destroyed)
    {
        return 0;
    }

    const float newDamage = GetDamage() + shotInfo.Damage;
    const auto& type = TypeAs<MCMiscTerrainObjectType>();

    switch (Kind)
    {
        case MCMiscTerrainKind::Bridge:
        case MCMiscTerrainKind::Wall:
        case MCMiscTerrainKind::MediumWall:
        {
            if (static_cast<float>(DmgLevelFor(type, Kind)) <= newDamage)
            {
                ObjType->CreateExplosion(Position, 0.0f, 0.0f);
                Status = 2;
            }

            break;
        }

        case MCMiscTerrainKind::Forest:
        {
            // A forest catches fire on any hit (the test is always true) and burns a second longer per hit.
            const auto threshold = static_cast<float>(static_cast<int32_t>(type.ForestDmgLevel));

            if (threshold <= newDamage)
            {
                ObjType->CreateExplosion(Position, 0.0f, 0.0f);
                Status = 2;
            }

            if (FireObject == nullptr && (newDamage < threshold || threshold <= newDamage))
            {
                std::shared_ptr<MCGameObject> effect = CreateObject(static_cast<int32_t>(type.ForestFireFX));

                if (effect != nullptr)
                {
                    effect->SetPosition(Position);
                    std::shared_ptr<MCFire> fire = FireObject.Light(std::static_pointer_cast<MCFire>(effect));
                    fire->SetPotentialContact(3);
                    fire->BurningObject = SharedAs<MCGameObject>();
                    fire->SetTonnage(40.0f);
                    FireObject.Burn();
                }
            }

            if (FireObject != nullptr)
            {
                FireObject->AddTimeLeftToBurn(1.0f);
            }

            break;
        }

        case MCMiscTerrainKind::LightWall:
        {
            if (static_cast<float>(static_cast<int32_t>(type.LightWallDmgLevel)) <= newDamage)
            {
                ObjType->CreateExplosion(Position, 0.0f, 0.0f);
                Status = 2;
                SoundSystem()->PlayDigitalSample(0x48, 1, *this, false, false);
            }

            break;
        }

        default:
            break;
    }

    SetDamage(newDamage);
    return 0;
}

auto MCMiscTerrainObject::IsRevealed() -> int
{
    return MCVertexCell::Of(BlockNumber, VertexNumber).AnyCornerVisible() ? 1 : 0;
}

#include "stdafx.h"
#include "object/MCGate.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "main/MCMissionGlobals.h"
#include "main/MCGameStrings.h"
#include "network/MCMultiPlayer.h"
#include "object/MCGateType.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectEvent.h"
#include "object/MCObjectSystem.h"
#include "object/MCVertexPlacement.h"
#include "object/MCWeaponShotInfo.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCPUAppearance.h"
#include "terrain/MCTerrain.h"

namespace
{
    /// <summary>The two shapes of gate: their object type ids pick which overlay types they set on the map.</summary>
    enum class MCGateKind
    {
        None,
        /// <summary>Types 0x284, 0x287, 0x2b2, 0x2b3, 0x403, 0x405.</summary>
        KindA,
        /// <summary>Types 0x285, 0x286, 0x2b4, 0x2b5, 0x404, 0x406.</summary>
        KindB
    };

    /// <summary>The shape of gate an object type is.</summary>
    MCGateKind GateKind(int32_t objTypeNum)
    {
        switch (objTypeNum)
        {
            case 0x284:
            case 0x287:
            case 0x2b2:
            case 0x2b3:
            case 0x403:
            case 0x405:
                return MCGateKind::KindA;
            case 0x285:
            case 0x286:
            case 0x2b4:
            case 0x2b5:
            case 0x404:
            case 0x406:
                return MCGateKind::KindB;
            default:
                return MCGateKind::None;
        }
    }

    /// <summary>
    /// Sets the gate tile's overlay type (the masked form MCX.EXE writes) and each of its nine cells' passable and
    /// line-of-sight bits.
    /// </summary>
    void SetGateTile(const MCVertexCell& cell, uint32_t keepMask, uint32_t overlayBits, uint32_t lineOfSight,
                     uint32_t passable)
    {
        MCMapTile& tile = GameMap().Map[GameMap().Width * cell.Row + cell.Col];
        tile.Overlay = (tile.Overlay & keepMask) | overlayBits;

        for (uint32_t shift = 0; shift < 0x12; shift += 2)
        {
            tile.Cells = (lineOfSight << (shift + 0xf)) | (~(0x8000u << shift) & tile.Cells);
            tile.Cells = (~(0x4000u << shift) & tile.Cells) | (passable << (shift + 0xe));
        }
    }

    /// <summary>
    /// Sets the open gate's tile: passable and see-through, with the overlay type of its shape and of
    /// <paramref name="innerSphereOverlay"/>'s side.
    /// </summary>
    void SetOpenGateTile(const MCVertexCell& cell, MCGateKind kind, bool innerSphereOverlay)
    {
        if (kind == MCGateKind::KindA)
        {
            if (innerSphereOverlay)
            {
                SetGateTile(cell, 0xffffffc9, 0x49, 1, 1);
            }
            else
            {
                SetGateTile(cell, 0xffffffc5, 0x45, 1, 1);
            }
        }
        else if (kind == MCGateKind::KindB)
        {
            if (innerSphereOverlay)
            {
                SetGateTile(cell, 0xffffffc7, 0x47, 1, 1);
            }
            else
            {
                SetGateTile(cell, 0xffffffc3, 0x43, 1, 1);
            }
        }
    }

    /// <summary>
    /// Takes the pop-up appearance's answer to a combat mode request: open (2), opening (1, with the opening sound
    /// when it was closed), closing (3, with the closing sound when it was open) or closed (0).
    /// </summary>
    /// <returns>True when the gate is now closed.</returns>
    bool TakeGateState(MCGate& gate, int32_t state)
    {
        switch (state)
        {
            case 2:
            {
                gate.IsOpen = true;
                gate.IsOpening = false;
                gate.IsClosing = false;
                gate.IsClosed = false;
                return false;
            }

            case 1:
            {
                if (gate.IsClosed)
                {
                    SoundSystem()->PlayDigitalSample(0x2a, 1, gate, false, false);
                }

                gate.IsOpening = true;
                gate.IsOpen = false;
                gate.IsClosing = false;
                gate.IsClosed = false;
                return false;
            }

            case 3:
            {
                if (gate.IsOpen)
                {
                    SoundSystem()->PlayDigitalSample(0x2d, 1, gate, false, false);
                }

                gate.IsClosing = true;
                gate.IsOpen = false;
                gate.IsOpening = false;
                gate.IsClosed = false;
                return false;
            }

            case 0:
            {
                gate.IsClosed = true;
                gate.IsOpen = false;
                gate.IsOpening = false;
                gate.IsClosing = false;
                return true;
            }

            default:
                return false;
        }
    }

    /// <summary>
    /// Makes the gate's fire (the type's blown effect). With <paramref name="burnNow"/> the fire goes to the object
    /// lists at once (and anything that isn't a fire goes to the lists too); without, the gate keeps the fire unburnt
    /// (OB-154) and deletes anything else.
    /// </summary>
    void StartFire(MCGate& gate, bool burnNow)
    {
        std::shared_ptr<MCGameObject> effect =
            CreateObject(static_cast<int32_t>(gate.TypeAs<MCGateType>().BlownEffectId));

        if (effect == nullptr)
        {
            return;
        }

        effect->SetPosition(gate.Position);

        if (effect->ObjectClass == MCObjectClass::Fire)
        {
            std::shared_ptr<MCFire> fire = gate.FireObject.Light(std::static_pointer_cast<MCFire>(effect));
            fire->SetPotentialContact(3);
            fire->BurningObject = gate.SharedAs<MCGameObject>();
            fire->SetTonnage(40.0f);

            if (burnNow)
            {
                gate.FireObject.Burn();
                gate.FireStarted = true;
            }
        }
        else if (burnNow)
        {
            AddToDefaultList(std::move(effect));
        }
        else
        {
            DestroyObject(*effect);
        }
    }
} // namespace

MCGate::MCGate() = default;

MCGate::~MCGate() = default;

auto MCGate::GetAppearance() -> std::shared_ptr<MCAppearance>
{
    return Appearance;
}

auto MCGate::SetTerrainPosition(MCVector2D& pixelOffset, MCVector2D& blockVertex) -> void
{
    PixelOffsetX = static_cast<int32_t>(pixelOffset.X);
    PixelOffsetY = static_cast<int32_t>(pixelOffset.Y);
    const auto& gateType = TypeAs<MCGateType>();

    if (gateType.BasePixelOffsetX != 0 || gateType.BasePixelOffsetY != 0)
    {
        PixelOffsetX = gateType.BasePixelOffsetX;
        PixelOffsetY = gateType.BasePixelOffsetY;
    }

    VertexNumber = static_cast<int32_t>(blockVertex.X);
    BlockNumber = static_cast<int32_t>(blockVertex.Y);
}

auto MCGate::IsVisible() -> bool
{
    if (Eye() == nullptr || Eye()->Active == 0)
    {
        return false;
    }

    int visible = MCCamera::VertexProject(BlockNumber, VertexNumber, ScreenPos);

    if (Appearance != nullptr)
    {
        visible = Appearance->RecalcBounds(EyeRef());
    }

    if (visible == 0)
    {
        return false;
    }

    WindowsVisible = Turn;
    return true;
}

auto MCGate::Update() -> int32_t
{
    if (JustCreated)
    {
        // Set the gate on its vertex.
        JustCreated = false;
        Position = PlaceOnVertex(Position, BlockNumber, VertexNumber, PixelOffsetX, PixelOffsetY);
        const MCVertexCell cell = MCVertexCell::Of(BlockNumber, VertexNumber);
        TileCol = cell.Col;
        TileWorldX = cell.WorldX();
        TileRow = cell.Row;
        TileWorldY = cell.WorldY();
        const float elevation = cell.Elevation(" tbldg MapTile Out of Bounds ");
        Appearance->Visible = true;
        TileElevation = elevation;
        Appearance->Update();
        Appearance->RecalcBounds(EyeRef());
    }

    if (!Destroyed)
    {
        OpenGate();
        OffendingObject.reset();
    }

    return 1;
}

auto MCGate::BlowAnyOffendingObject() -> void
{
    // Whatever is caught in a gate as it shuts takes 10 hits of 250, and the gate is destroyed.
    if (MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0)
    {
        return;
    }

    std::shared_ptr<MCGameObject> offender = LockObject(OffendingObject);

    if (offender == nullptr)
    {
        return;
    }

    const MCVector3D offenderPos = offender->GetPosition();
    const MCVector3D gatePos = GetPosition();
    const float dx = gatePos.X - offenderPos.X;
    const float dy = gatePos.Y - offenderPos.Y;
    const auto& gateType = TypeAs<MCGateType>();
    const double reach = static_cast<double>(gateType.LittleExtent) + offender->GetObjectType()->ExtentRadius;

    if (reach * reach <= static_cast<double>(dy) * dy + static_cast<double>(dx) * dx)
    {
        return;
    }

    const int multiplayer = MultiPlayer() != nullptr ? 1 : 0;
    MCWeaponShotInfo shot;
    shot.Init(nullptr, -3, 250.0f, 0, 0.0f);

    for (int32_t i = 0; i < 10; i++)
    {
        shot.HitLocation = offender->CalcHitLocation(std::nullopt, -1, 4, 0);
        offender->HandleWeaponHit(shot, multiplayer);
    }

    const auto gateDamage = static_cast<float>(static_cast<int32_t>(gateType.DmgLevel + 5));
    shot.Init(nullptr, -3, gateDamage, 0, 0.0f);
    HandleWeaponHit(shot, multiplayer);
}

auto MCGate::OpenGate() -> void
{
    if (Destroyed)
    {
        return;
    }

    const MCVertexCell cell = MCVertexCell::Of(BlockNumber, VertexNumber);

    // Ask the pop-up appearance to open or shut: locked (or forced) gates shut; blown open or neutral gates open;
    // others follow openRequested. A gate that has just shut crushes whatever is in it (unless blown or neutral).
    if (!LockedClosed && ForceGatesClosed == 0)
    {
        if (!BlownOpen && Alignment != 0)
        {
            if (TakeGateState(*this, Appearance->SetCombatMode(OpenRequested)))
            {
                BlowAnyOffendingObject();
            }
        }
        else
        {
            TakeGateState(*this, Appearance->SetCombatMode(true));
        }
    }
    else if (TakeGateState(*this, Appearance->SetCombatMode(false)))
    {
        BlowAnyOffendingObject();
    }

    // Open: the tile is passable and see-through. Shut: blocked (unless locked open) and see-through only when the
    // type doesn't block line of fire. The overlay type depends on the gate's shape and whose it is.
    const MCGateKind kind = GateKind(GetObjectType()->ObjTypeNum);

    if (IsOpen)
    {
        SetOpenGateTile(cell, kind, Alignment == 1 || Alignment == 0);
        OpenRequested = false;
        return;
    }

    const uint32_t lineOfSight = TypeAs<MCGateType>().BlocksLineOfFire ? 0 : 1;
    const uint32_t passable = LockedClosed ? 0 : 1;

    if (kind == MCGateKind::KindA)
    {
        if (Alignment == 1)
        {
            SetGateTile(cell, 0xffffffca, 0x4a, lineOfSight, passable);
        }
        else
        {
            SetGateTile(cell, 0xffffffc6, 0x46, lineOfSight, passable);
        }
    }
    else if (kind == MCGateKind::KindB)
    {
        if (Alignment == 1)
        {
            SetGateTile(cell, 0xffffffc8, 0x48, lineOfSight, passable);
        }
        else
        {
            SetGateTile(cell, 0xffffffc4, 0x44, lineOfSight, passable);
        }
    }

    OpenRequested = false;
}

auto MCGate::HandleEvent(MCObjectEvent& event) -> int32_t
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

auto MCGate::LightOnFire(float timeToBurn) -> void
{
    // A gate with no blown effect just takes a point of damage (the server's job in multiplayer).
    if (static_cast<int32_t>(TypeAs<MCGateType>().BlownEffectId) == -1)
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

        return;
    }

    if (FireObject == nullptr)
    {
        StartFire(*this, false);
    }

    if (FireObject != nullptr)
    {
        FireObject->AddTimeLeftToBurn(timeToBurn);
        FireStarted = true;
    }
}

auto MCGate::IsRevealed() -> int
{
    return MCVertexCell::Of(BlockNumber, VertexNumber).AnyCornerVisible() ? 1 : 0;
}

auto MCGate::Render() -> void
{
    if (JustCreated)
    {
        return;
    }

    if (Appearance != nullptr)
    {
        Appearance->Visible = IsVisible();
        Appearance->Update();
    }

    if (WindowsVisible != Turn)
    {
        return;
    }

    // Drawn when the home team sees any corner of its vertex square. (The original also handed the appearance a haze
    // table for the seen corners, which the pop-up appearance never reads, and reads each corner's seen bit and
    // drops it.)
    const int32_t numVisible = MCVertexCell::Of(BlockNumber, VertexNumber).VisibleCorners();

    if (numVisible != 0)
    {
        Appearance->Render(0);
    }

    if (DrawExtents)
    {
        // Debug: the extent radius as an ellipse.
        DrawExtentEllipse(Position, MCVector2D(ObjType->ExtentRadius, ObjType->ExtentRadius));
    }
}

auto MCGate::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    if (const int32_t result = MCGameObject::Init(objType); result != 0)
    {
        return result;
    }

    JustCreated = true;
    std::shared_ptr<MCAppearanceType> apprType = AppearanceTypeList().GetAppearance(objType->AppearName);

    if (apprType == nullptr)
    {
        return static_cast<int32_t>(0xdcdc0007);
    }

    Appearance = std::make_shared<MCPUAppearance>();
    Appearance->Init();

    if ((apprType->AppearanceNum & 0xff000000) != 0x9000000)
    {
        return -0x2fff6;
    }

    if (const int32_t result = Appearance->Init(apprType, WeakAs<MCGameObject>()); result != 0)
    {
        return result;
    }

    auto& gateType = TypeAs<MCGateType>();
    ObjectClass = MCObjectClass::Gate;
    Destroyed = false;
    Alignment = -1;

    // The open radius doubles as the extent the collision system checks.
    if (gateType.OpenRadius != 0.0f)
    {
        gateType.ExtentRadius = gateType.OpenRadius;
    }

    if (0.0f < gateType.ExtentRadius)
    {
        CollisionsOn = 1;
    }

    ExplDamage = gateType.ExplosionDamage;
    ExplRadius = gateType.ExplosionRadius;
    Name = LoadGameString(static_cast<uint32_t>(gateType.BuildingName), 0xfe);
    return 0;
}

auto MCGate::HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) -> int32_t
{
    if (addMultiplayChunk != 0)
    {
        MultiPlayer()->AddWeaponHitChunk(*this, shotInfo, 0);
    }

    const float newDamage = GetDamage() + shotInfo.Damage;
    SetDamage(newDamage);

    if (static_cast<float>(static_cast<int32_t>(TypeAs<MCGateType>().DmgLevel)) <= newDamage)
    {
        DestroyGate(false);
    }

    return 0;
}

auto MCGate::DestroyGate(bool fromNetwork) -> void
{
    Destroyed = true;
    Destroying = true;

    // Port fix: the original calls setDestroyed on a null appearance.
    if (Appearance != nullptr)
    {
        Appearance->Visible = OnScreen();
        Appearance->Update();
        Appearance->SetDestroyed();
    }

    // Blown open: the tile is passable and see-through for good (the network copy already got the map change).
    if (!fromNetwork)
    {
        SetOpenGateTile(MCVertexCell::Of(BlockNumber, VertexNumber), GateKind(GetObjectType()->ObjTypeNum),
                        Alignment == 1);
    }

    IsOpen = true;
    IsOpening = false;
    IsClosing = false;
    IsClosed = false;
    BlownOpen = true;
    Destroying = false;
    CollisionsOn = 0;
    Status = 2;

    // Set it burning (or keep a fire going) and blow it up.
    if (!FireStarted && !fromNetwork)
    {
        if (static_cast<int32_t>(TypeAs<MCGateType>().BlownEffectId) == -1)
        {
            if (FireObject != nullptr)
            {
                FireObject->AddTimeLeftToBurn(2.0f);
            }
        }
        else
        {
            StartFire(*this, true);
        }

        // Port-only (HD shake): its end shakes the picture.
        ObjType->CreateExplosion(Position, ExplDamage, ExplRadius, MCBlastShake::Shakes);
    }
}

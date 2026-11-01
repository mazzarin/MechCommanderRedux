#include "stdafx.h"
#include "object/MCTree.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCVfxElement.h"
#include "lib/MCFatal.h"
#include "main/MCMissionGlobals.h"
#include "network/MCMultiPlayer.h"
#include "object/MCCollisionSystem.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectEvent.h"
#include "object/MCObjectSystem.h"
#include "object/MCTreeType.h"
#include "object/MCVertexPlacement.h"
#include "object/MCWeaponShotInfo.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCVfxAppearance.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

MCTree::MCTree()
{
    TreeFrame.I = UnitX;
    TreeFrame.J = UnitY;
    TreeFrame.K = UnitZ;
}

MCTree::~MCTree() = default;

auto MCTree::GetAppearance() -> std::shared_ptr<MCAppearance>
{
    return Appearance;
}

auto MCTree::SetTerrainPosition(MCVector2D& pixelOffset, MCVector2D& blockVertex) -> void
{
    PixelOffsetX = static_cast<int32_t>(pixelOffset.X);
    PixelOffsetY = static_cast<int32_t>(pixelOffset.Y);
    VertexNumber = static_cast<int32_t>(blockVertex.X);
    BlockNumber = static_cast<int32_t>(blockVertex.Y);
}

auto MCTree::IsVisible() -> bool
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

    // The shadow can stick out past the tree: on screen when any of its box is.
    if (const std::span<uint8_t> shadow = TypeAs<MCTreeType>().NormalShadow.Bytes(); !shadow.empty())
    {
        const int32_t minXY = VfxShapeMinxy(shadow, 0);
        const float left = static_cast<float>(minXY >> 16) + ScreenPos.X;
        const float top = static_cast<float>(static_cast<int16_t>(minXY)) + ScreenPos.Y;
        const int32_t resolution = VfxShapeResolution(shadow, 0);

        if (0.0f <= static_cast<float>(resolution >> 16) + left &&
            0.0f <= static_cast<float>(static_cast<int16_t>(resolution)) + top)
        {
            const auto viewRight = static_cast<int16_t>(std::floor(static_cast<double>(Eye()->ViewWidth)));
            const auto viewBottom = static_cast<int16_t>(std::floor(static_cast<double>(Eye()->ViewHeight)));

            if (left <= static_cast<float>(viewRight) && top <= static_cast<float>(viewBottom))
            {
                WindowsVisible = Turn;
                return true;
            }
        }
    }

    if (visible == 0)
    {
        return false;
    }

    WindowsVisible = Turn;
    return true;
}

auto MCTree::Update() -> int32_t
{
    if (!JustCreated)
    {
        return 1;
    }

    // Set the tree on its vertex.
    JustCreated = false;
    Position = PlaceOnVertex(Position, BlockNumber, VertexNumber, PixelOffsetX, PixelOffsetY);
    const MCVertexCell cell = MCVertexCell::Of(BlockNumber, VertexNumber);
    TileCol = cell.Col;
    TileWorldX = cell.WorldX();
    TileRow = cell.Row;
    TileWorldY = cell.WorldY();
    const float elevation = cell.Elevation(" tree MapTile Out of Bounds ");
    Appearance->Visible = true;
    TileElevation = elevation;

    // Every tree measures the type's extent radius from its appearance's diagonal.
    Appearance->Update();
    Appearance->RecalcBounds(EyeRef());
    const double dx = static_cast<double>(Appearance->UpperLeft.X) - Appearance->LowerRight.X;
    const double dy = static_cast<double>(Appearance->UpperLeft.Y) - Appearance->LowerRight.Y;
    const auto radius = static_cast<float>(std::sqrt(dy * dy + dx * dx) / WorldUnitsPerMeter);

    if (static_cast<float>(CollisionSystem().GridRadius()) < radius)
    {
        Fatal(static_cast<int32_t>(std::floor(static_cast<double>(radius))), " Object extent radius TOO large ");
    }

    ObjType->ExtentRadius = radius;
    CollisionsOn = 1;
    return 1;
}

auto MCTree::HandleEvent(MCObjectEvent& event) -> int32_t
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

auto MCTree::LightOnFire(float timeToBurn) -> void
{
    // A fire does 25 points to the tree (the server's job in multiplayer), and burns on.
    MCWeaponShotInfo shot;
    shot.Init(nullptr, -1, 25.0f, 0, 0.0f);

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
        FireStarted = true;
    }
}

auto MCTree::Render() -> void
{
    if (JustCreated)
    {
        return;
    }

    if (FireObject == nullptr)
    {
        FireStarted = false;
    }

    if (Appearance != nullptr)
    {
        Appearance->Visible = IsVisible();

        // A falling tree comes to rest: fallen, or fallen burnt.
        if (Appearance->Update() == 0 && Falling)
        {
            Falling = false;
            CollisionsOn = 0;
            Fallen = true;

            if (Appearance->CurrentState == MCActorState::BlowingUp1)
            {
                Appearance->SetTypeId(MCActorState::Damaged, 0xff);
            }
            else if (Appearance->CurrentState == MCActorState::Destroyed)
            {
                Appearance->SetTypeId(MCActorState::FallenDamaged, 0xff);
            }

            Appearance->Update();
        }
    }

    if (WindowsVisible != Turn)
    {
        return;
    }

    // Hazed by how many corners of its vertex square the home team sees; drawn (with its shadow) when any is. (The
    // original also reads each corner's seen bit and drops it.)
    const int32_t numVisible = MCVertexCell::Of(BlockNumber, VertexNumber).VisibleCorners();
    int32_t hazeLevel = 0;

    if (numVisible != 0 && numVisible != 4 && Eye()->HazeLevel != 0x7fff)
    {
        hazeLevel = HazeLevelFor(numVisible);
    }

    Appearance->HazeLevel = hazeLevel;
    const std::optional<MCColorTable> hazePalette = GamePalette().GetHazePalette(hazeLevel);

    if (numVisible != 0)
    {
        // Standing, it sorts with the terrain; fallen, by its screen row. Port-only (sun shadows): at dusk and night it
        // casts its own shadow (MCVfxAppearance) instead of drawing its art's.
        const auto& type = TypeAs<MCTreeType>();
        const bool standing = Appearance->CurrentState == MCActorState::Normal;
        const bool artShadow = !MCSceneLights::RecastsSunShadows();
        Appearance->Render(standing ? 0 : static_cast<int32_t>(ScreenPos.Y));

        if (!standing)
        {
            if (const std::span<uint8_t> shadow = type.DestroyedShadow.Bytes(); artShadow && !shadow.empty())
            {
                ElementList().OpenGroup(static_cast<int32_t>(ScreenPos.Y), true);
                ElementList().Add(
                    ElementList().Make<MCVfxElement>(shadow, ScreenPos.X, ScreenPos.Y, 0, 0, hazePalette, 0));
            }
        }
        else if (const std::span<uint8_t> shadow = type.NormalShadow.Bytes(); artShadow && !shadow.empty())
        {
            ElementList().OpenGroup(static_cast<int32_t>(-ScreenPos.Y), true);
            ElementList().Add(ElementList().Make<MCVfxElement>(shadow, ScreenPos.X, ScreenPos.Y, 0, 0, hazePalette, 0));
        }
    }

    if (DrawExtents)
    {
        // Debug: the extent radius as an ellipse.
        DrawExtentEllipse(Position, MCVector2D(ObjType->ExtentRadius, ObjType->ExtentRadius));
    }
}

auto MCTree::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
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

    Appearance = std::make_shared<MCVfxAppearance>();
    Appearance->Init();

    if ((apprType->AppearanceNum & 0xff000000) != 0x2000000)
    {
        return static_cast<int32_t>(0xdcdc0009);
    }

    if (const int32_t result = Appearance->Init(apprType, WeakAs<MCGameObject>()); result != 0)
    {
        return result;
    }

    ObjectClass = MCObjectClass::Tree;
    Burnt = false;
    return 0;
}

auto MCTree::HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) -> int32_t
{
    if (addMultiplayChunk != 0)
    {
        MultiPlayer()->AddWeaponHitChunk(*this, shotInfo, 0);
    }

    // Any hit burns the tree: standing to burnt, fallen to fallen burnt.
    SetDamage(GetDamage() + 1.0f);
    Burnt = true;

    if (!Falling)
    {
        if (Appearance->CurrentState == MCActorState::Normal)
        {
            Appearance->SetTypeId(MCActorState::BlowingUp2, 0xff);
        }
        else if (Appearance->CurrentState == MCActorState::Damaged)
        {
            Appearance->SetTypeId(MCActorState::FallenDamaged, 0xff);
        }
    }

    Status = 2;

    // The first hit sets it alight; later ones keep the fire going 2 more seconds.
    if (!FireStarted)
    {
        if (ObjType->ExplosionObject != -1)
        {
            if (std::shared_ptr<MCFire> fire = FireObject.Light(CreateObjectAs<MCFire>(ObjType->ExplosionObject));
                fire != nullptr)
            {
                fire->BurningObject = SharedAs<MCGameObject>();
                fire->SetTonnage(40.0f);
                fire->SetPosition(Position);
                FireObject.Burn();
            }
        }

        FireStarted = true;
        return 0;
    }

    if (FireObject != nullptr)
    {
        FireObject->AddTimeLeftToBurn(2.0f);
    }

    return 0;
}

auto MCTree::IsRevealed() -> int
{
    return MCVertexCell::Of(BlockNumber, VertexNumber).AnyCornerVisible() ? 1 : 0;
}

#include "stdafx.h"
#include "object/MCMiscTerrainObjectType.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGamePaths.h"
#include "network/MCMultiPlayer.h"
#include "object/MCMiscTerrainObject.h"
#include "object/MCWeaponShotInfo.h"
#include "terrain/MCTerrain.h"
#include "terrain/MCTerrainTiles.h"
#include "sprite/MCShapeFile.h"

auto MCMiscTerrainObjectType::CreateInstance() -> std::shared_ptr<MCBaseObject>
{
    auto newObject = std::make_shared<MCMiscTerrainObject>();

    if (newObject->Init(shared_from_this()) != 0)
    {
        return nullptr;
    }

    newObject->IdNumber = NextIdNumber++;
    return newObject;
}

auto MCMiscTerrainObjectType::Init(MCFile& objFile, uint32_t fileSize) -> int32_t
{
    MCFitIniFile bridgeFile;

    if (const int32_t result = bridgeFile.Open(objFile, fileSize); result != 0)
    {
        return result;
    }

    if (const int32_t result = bridgeFile.SeekBlock("BridgeData"); result != 0)
    {
        return result;
    }

    MCFitReader read(bridgeFile);
    read.Value("WallDmgLevel", WallDmgLevel);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    MediumWallDmgLevel = bridgeFile.Read<uint32_t>("MediumWallDmgLevel").value_or(WallDmgLevel >> 1);

    // Original behaviour (OB-021): the light wall default halves the light wall level itself (0 after the failed
    // read), not the wall's.
    if (const MCFitResult<uint32_t> light = bridgeFile.Read<uint32_t>("LightWallDmgLevel"); light.has_value())
    {
        LightWallDmgLevel = *light;
    }
    else
    {
        if (light.error() == MCFitError::VariableNotFound)
        {
            LightWallDmgLevel = 0;
        }

        LightWallDmgLevel = LightWallDmgLevel >> 1;
    }

    read.Value("BridgeDmgLevel", BridgeDmgLevel);
    read.Value("ForestDmgLevel", ForestDmgLevel);
    read.Value("WallFireFX", WallFireFX);
    read.Value("BridgeFireFX", BridgeFireFX);
    read.Value("ForestFireFX", ForestFireFX);
    read.Value("BlownEffectId", BlownEffectId);
    read.Value("NormalEffectId", NormalEffectId);
    read.Value("DamageEffectId", DamageEffectId);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    // The forest edge shapes (the "x" set for a custom tile set).
    MCFitResult<std::string> edgesName = bridgeFile.Read<std::string>("ForestEdges");

    if (!edgesName.has_value())
    {
        return std::to_underlying(edgesName.error());
    }

    if (Terrain()->Tiles->CustomTileSet())
    {
        *edgesName += 'x';
    }

    MCShapeFile edgesFile;

    if (const int32_t result = edgesFile.Open(GamePath(SpritePath, *edgesName, ".shp")); result != 0)
    {
        return result;
    }

    ForestEdgeShapes = MCRegisteredBlock(edgesFile.FileSize(), MCDataKind::Shapes);
    edgesFile.Read(ForestEdgeShapes.Bytes());
    edgesFile.Close();
    return MCObjectType::Init(bridgeFile);
}

auto MCMiscTerrainObjectType::HandleCollision(MCGameObject& collidee, MCGameObject& collider) -> int
{
    // A mech or vehicle running into a light wall knocks it down (250 points, the server's job in multiplayer).
    const auto& object = static_cast<MCMiscTerrainObject&>(collidee);

    if (object.Kind == MCMiscTerrainKind::LightWall && MCObjectClass::BattleMech <= collider.ObjectClass &&
        collider.ObjectClass < MCObjectClass::Elemental)
    {
        MCWeaponShotInfo shot;
        shot.Init(collider.SharedAs<MCGameObject>(), -1, 250.0f, 0, 0.0f);

        if (MultiPlayer() == nullptr)
        {
            collidee.HandleWeaponHit(shot, 0);
        }
        else if (MultiPlayer()->IsServer != 0)
        {
            collidee.HandleWeaponHit(shot, 1);
        }
    }

    return 1;
}

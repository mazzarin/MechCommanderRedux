#include "stdafx.h"
#include "object/MCFireType.h"
#include "lib/MCDice.h"
#include "lib/MCFitIniFile.h"
#include "mission/MCWeatherPlay.h"
#include "network/MCMultiPlayer.h"
#include "object/MCBuilding.h"
#include "object/MCBuildingType.h"
#include "object/MCFire.h"
#include "object/MCMiscTerrainObject.h"
#include "object/MCTree.h"
#include "object/MCTreeBuilding.h"

auto MCFireType::CreateInstance() -> std::shared_ptr<MCBaseObject>
{
    auto newFire = std::make_shared<MCFire>();

    if (newFire->Init(shared_from_this()) != 0)
    {
        return nullptr;
    }

    newFire->IdNumber = NextIdNumber++;
    return newFire;
}

auto MCFireType::Init(MCFile& objFile, uint32_t fileSize) -> int32_t
{
    MCFitIniFile fireFile;

    if (const int32_t result = fireFile.Open(objFile, fileSize); result != 0)
    {
        return result;
    }

    if (const int32_t result = fireFile.SeekBlock("FireData"); result != 0)
    {
        return result;
    }

    MCFitReader read(fireFile);
    read.Value("DmgLevel", DmgLevel);
    read.Value("SoundEffectId", SoundEffectId);
    read.Value("startLoopFrame", StartLoopFrame);
    read.Value("numLoops", NumLoops);
    read.Value("endLoopFrame", EndLoopFrame);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    LightObjectId = fireFile.Read<uint32_t>("LightObjectId").value_or(0xffffffff);
    MaxExtentRadius = fireFile.Read<float>("maxExtentRadius").value_or(0.0f);
    TimeToMaxExtent = fireFile.Read<float>("TimeToMaxExtent").value_or(0.0f);
    const int32_t numShapes = fireFile.Read<int32_t>("TotalFireShapes").value_or(1);
    Shapes.assign(static_cast<size_t>(std::max(numShapes, 0)), MCFireShape{});

    for (size_t i = 0; i < Shapes.size(); i++)
    {
        MCFireShape& shape = Shapes[i];
        shape.OffsetX = fireFile.Read<float>(std::format("FireOffsetX{}", i)).value_or(0.0f);
        shape.OffsetY = fireFile.Read<float>(std::format("FireOffsetY{}", i)).value_or(0.0f);
        shape.Delay = fireFile.Read<float>(std::format("FireDelay{}", i)).value_or(0.0f);
        shape.RandomOffsetX = fireFile.Read<int32_t>(std::format("FireRandomOffsetX{}", i)).value_or(0);
        shape.RandomOffsetY = fireFile.Read<int32_t>(std::format("FireRandomOffsetY{}", i)).value_or(0);
        shape.RandomDelay = fireFile.Read<int32_t>(std::format("FireRandomDelay{}", i)).value_or(0);
    }

    return MCObjectType::Init(fireFile);
}

auto MCFireType::HandleCollision(MCGameObject&, MCGameObject& collider) -> int
{
    // The fire spreads (one chance in ten per collision) to what it touches; the server's job in multiplayer.
    if (MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0)
    {
        return 0;
    }

    if (collider.IsDestroyed() != 0)
    {
        return 0;
    }

    // Sets the collider alight for timeToBurn seconds, telling the other machines.
    const auto spread = [&](auto& target, float timeToBurn)
    {
        target.LightOnFire(timeToBurn);

        if (MultiPlayer() != nullptr)
        {
            MultiPlayer()->AddLightOnFireChunk(collider, static_cast<int32_t>(timeToBurn));
        }
    };

    // Port: the match's weather may damp the original's one chance in ten (MCWeatherPlay::Spread).
    const int32_t spreadChance = MCWeatherPlayNow().Spread;

    switch (collider.ObjectClass)
    {
        case MCObjectClass::Building:
        {
            if (RollDice(spreadChance) != 0)
            {
                spread(static_cast<MCBuilding&>(collider),
                       10.0f / static_cast<MCBuildingType&>(*collider.GetObjectType()).TimeToBurnDamage);
            }

            break;
        }

        case MCObjectClass::Tree:
        {
            if (RollDice(spreadChance) != 0)
            {
                spread(static_cast<MCTree&>(collider), 15.0f);
            }

            break;
        }

        case MCObjectClass::MiscTerrainObject:
        {
            if (RollDice(spreadChance) != 0)
            {
                spread(static_cast<MCMiscTerrainObject&>(collider), 15.0f);
            }

            break;
        }

        case MCObjectClass::TreeBuilding:
        {
            if (RollDice(spreadChance) != 0)
            {
                spread(static_cast<MCTreeBuilding&>(collider), 15.0f);
            }

            break;
        }

        default:
            break;
    }

    return 0;
}

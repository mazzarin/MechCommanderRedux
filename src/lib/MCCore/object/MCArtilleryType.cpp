#include "stdafx.h"
#include "object/MCArtilleryType.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGamePaths.h"
#include "main/MCMissionGlobals.h"
#include "network/MCMultiPlayer.h"
#include "object/MCArtillery.h"
#include "object/MCGateType.h"
#include "object/MCTurretType.h"
#include "object/MCWeaponShotInfo.h"
#include "sprite/MCSpriteManager.h"
#include "sprite/MCShapeFile.h"

auto MCArtilleryType::CreateInstance() -> std::shared_ptr<MCBaseObject>
{
    auto newStrike = std::make_shared<MCArtillery>();

    if (newStrike->Init(shared_from_this()) != 0)
    {
        return nullptr;
    }

    newStrike->IdNumber = NextIdNumber++;
    return newStrike;
}

auto MCArtilleryType::Init(MCFile& objFile, uint32_t fileSize) -> int32_t
{
    MCFitIniFile artFile;

    if (const int32_t result = artFile.Open(objFile, fileSize); result != 0)
    {
        return result;
    }

    if (const int32_t result = artFile.SeekBlock("Artillery"); result != 0)
    {
        return result;
    }

    const MCFitResult<std::string> spriteName = artFile.Read<std::string>("ArtillerySpriteName");

    if (!spriteName.has_value())
    {
        return std::to_underlying(spriteName.error());
    }

    MCFitReader read(artFile);
    read.Value("FrameCount", FrameCount);
    read.Value("StartFrame", StartFrame);
    read.Value("FrameRate", FrameRate);
    read.Value("NominalTimeToImpact", NominalTimeToImpact);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    NominalTimeToLaunch = artFile.Read<float>("NominalTimeToLaunch").value_or(NominalTimeToImpact - 10.0f);
    read.Value("NominalDamage", NominalDamage);
    read.Value("NominalMajorRange", NominalMajorRange);
    read.Value("NominalMajorHits", NominalMajorHits);
    read.Value("NominalMinorRange", NominalMinorRange);
    read.Value("NominalMinorHits", NominalMinorHits);
    read.Value("NominalSensorTime", NominalSensorTime);
    read.Value("NominalSensorRange", NominalSensorRange);
    read.Value("fontScale", FontScale);
    read.Value("fontXOffset", FontXOffset);
    read.Value("fontYOffset", FontYOffset);
    read.Value("fontColor", FontColor);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    // Only a damaging strike has an explosion pattern.
    if (NominalDamage != 0.0f)
    {
        int32_t numExplosions = 0;
        read.Value("NumExplosions", numExplosions);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }

        Blasts.assign(static_cast<size_t>(std::max(numExplosions, 0)), MCArtilleryBlast{});

        for (size_t i = 0; i < Blasts.size(); i++)
        {
            read.Value(std::format("ExplosionDelay{}", i), Blasts[i].Delay);
            read.Value(std::format("ExplosionOffsetX{}", i), Blasts[i].OffsetX);
            read.Value(std::format("ExplosionOffsetY{}", i), Blasts[i].OffsetY);
        }

        read.Value("ExplosionsPerExplosion", ExplosionsPerExplosion);
        read.Value("ExplosionRandomOffsetX", ExplosionRandomOffsetX);
        read.Value("ExplosionRandomOffsetY", ExplosionRandomOffsetY);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }

        MinArtilleryHeadRange = artFile.Read<int32_t>("MinArtilleryHeadRange").value_or(5);
    }

    MCShapeFile spriteFile;

    if (const int32_t result = spriteFile.Open(GamePath(ShapesPath, *spriteName, ".shp")); result != 0)
    {
        return result;
    }

    const uint32_t spriteSize = spriteFile.FileSize();

    // Faithful: an empty shape file fails (the original's shape heap had no zero-byte blocks), after a dump.
    if (spriteSize == 0)
    {
        SpriteManager()->DumpLru();
        return -0x2102ffff;
    }

    ShapeData = MCRegisteredBlock(spriteSize, MCDataKind::Shapes);
    spriteFile.Read(ShapeData.Bytes());
    spriteFile.Close();
    return MCObjectType::Init(artFile);
}

auto MCArtilleryType::HandleCollision(MCGameObject& collidee, MCGameObject& collider) -> int
{
    const auto& strike = static_cast<MCArtillery&>(collidee);

    if ((MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0) || !strike.HasImpacted)
    {
        return 0;
    }

    const MCVector3D colliderPos = collider.GetPosition();
    const MCVector3D strikePos = collidee.GetPosition();
    const double dx = static_cast<double>(colliderPos.X) - strikePos.X;
    const double dy = static_cast<double>(colliderPos.Y) - strikePos.Y;
    const auto distance = static_cast<float>(std::sqrt(dx * dx + dy * dy) * MetersPerWorldUnit);

    // A turret or gate counts as hit from anywhere within its little extent of the major range.
    if (collider.ObjectClass == MCObjectClass::Turret || collider.ObjectClass == MCObjectClass::Gate)
    {
        const double extent =
            collider.ObjectClass == MCObjectClass::Turret
                ? static_cast<double>(collider.TypeAs<MCTurretType>().LittleExtent) * MetersPerWorldUnit
                : static_cast<double>(collider.TypeAs<MCGateType>().LittleExtent) * MetersPerWorldUnit;

        if (extent < distance && NominalMajorRange < static_cast<float>(distance - extent))
        {
            return 0;
        }
    }

    // Beyond the major range the minor hit count lands, else the major one.
    const float hitCount = NominalMajorRange < distance ? NominalMinorHits : NominalMajorHits;

    for (int32_t hit = 0; static_cast<float>(hit) < hitCount; hit++)
    {
        MCWeaponShotInfo shot;
        shot.Init(nullptr, -3, NominalDamage, 0, 0.0f);

        if (IsMoverClass(collider.ObjectClass))
        {
            const int32_t hitTable = static_cast<float>(MinArtilleryHeadRange) < distance ? 4 : 2;
            shot.HitLocation = collider.CalcHitLocation(collidee.GetPosition(), -1, hitTable, 0);
            shot.SetEntryAngle(collider.RelFacingTo(collidee.GetPosition(), -1));
        }

        collider.HandleWeaponHit(shot, MultiPlayer() != nullptr ? 1 : 0);
    }

    return 0;
}

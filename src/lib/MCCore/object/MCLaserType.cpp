#include "stdafx.h"
#include "object/MCLaserType.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGamePaths.h"
#include "object/MCLaser.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectTypeManager.h"
#include "sprite/MCShapeFile.h"

auto MCLaserType::CreateInstance() -> std::shared_ptr<MCBaseObject>
{
    auto newLaser = std::make_shared<MCLaser>();

    if (newLaser->Init(shared_from_this()) != 0)
    {
        return nullptr;
    }

    newLaser->IdNumber = NextIdNumber++;
    return newLaser;
}

auto MCLaserType::Init(MCFile& objFile, uint32_t fileSize) -> int32_t
{
    MCFitIniFile laserFile;

    if (const int32_t result = laserFile.Open(objFile, fileSize); result != 0)
    {
        return result;
    }

    if (const int32_t result = laserFile.SeekBlock("LaserData"); result != 0)
    {
        return result;
    }

    MCFitReader read(laserFile);
    read.Value("PixelWidth", PixelWidth);
    read.Value("NumStages", NumStages);
    read.Value("DmgLevel", DmgLevel);
    read.Value("SoundEffectId", SoundEffectId);
    read.Value("LaserHitEffect", LaserHitEffect);
    read.Value("LaserMissEffect", LaserMissEffect);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    // A PPC: the effect shape and its frame data.
    if (const MCFitResult<std::string> shapeName = laserFile.Read<std::string>("LaserEffectShape");
        shapeName.has_value())
    {
        MCShapeFile shapeFile;

        if (const int32_t result = shapeFile.Open(GamePath(SpritePath, *shapeName, ".shp")); result != 0)
        {
            return result;
        }

        LaserEffectShape = MCRegisteredBlock(shapeFile.FileSize(), MCDataKind::Shapes);

        if (LaserEffectShape.Empty())
        {
            return static_cast<int32_t>(0xdcdc0001);
        }

        shapeFile.Read(LaserEffectShape.Bytes());
        shapeFile.Close();

        if (const int32_t result = laserFile.SeekBlock("PPCData"); result != 0)
        {
            return result;
        }

        read.Value("numPPCFrames", NumPpcFrames);
        read.Value("tPPC", Tppc);
        read.Value("lPPC", Lppc);
        read.Value("bPPC", Bppc);
        read.Value("rPPC", Rppc);
        read.Value("hitPPC", HitPpc);
        read.Value("lengthPPC", LengthPpc);
        read.Value("animPPC", AnimPpc);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }
    }

    // The stages: numStages friendly ones, then numStages enemy ones.
    Stages.assign(static_cast<size_t>(NumStages) * 2, MCLaserStage{});

    for (size_t i = 0; i < Stages.size(); i++)
    {
        const bool enemy = i >= NumStages;
        const size_t number = enemy ? i - NumStages : i;

        if (const int32_t result = laserFile.SeekBlock(std::format("{}Laser{}", enemy ? 'E' : 'F', number));
            result != 0)
        {
            return result;
        }

        read.Value("StageDuration", Stages[i].Duration);
        read.Value("StageCool", Stages[i].Cool);
        read.Value("StageHot", Stages[i].Hot);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }
    }

    const int32_t result = MCObjectType::Init(laserFile);
    ObjectTypeManager().Load(static_cast<int32_t>(LaserHitEffect), 1);
    ObjectTypeManager().Load(static_cast<int32_t>(LaserMissEffect), 1);
    return result;
}

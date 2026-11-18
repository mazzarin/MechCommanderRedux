#include "stdafx.h"
#include "object/MCSmokeType.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGamePaths.h"
#include "main/MCMissionGlobals.h"
#include "object/MCSmoke.h"
#include "sprite/MCShapeFile.h"

auto MCSmokeType::CreateInstance() -> std::shared_ptr<MCBaseObject>
{
    auto newSmoke = std::make_shared<MCSmoke>();

    if (newSmoke->Init(shared_from_this()) != 0)
    {
        return nullptr;
    }

    newSmoke->IdNumber = NextIdNumber++;
    return newSmoke;
}

auto MCSmokeType::Init(MCFile& objFile, uint32_t fileSize) -> int32_t
{
    MCFitIniFile smokeFile;

    if (const int32_t result = smokeFile.Open(objFile, fileSize); result != 0)
    {
        return result;
    }

    if (const int32_t result = smokeFile.SeekBlock("SmokeData"); result != 0)
    {
        return result;
    }

    MCFitReader read(smokeFile);
    read.Value("zVelocity", ZVelocity);
    read.Value("Duration", Duration);
    read.Value("SmokePerSecond", SmokePerSecond);
    read.Value("MaxSmokeSpheres", MaxSmokeSpheres);
    read.Value("SlowDownPercent", SlowDownPercent);
    read.Value("randomVelX", RandomVelX);
    read.Value("randomVelY", RandomVelY);
    read.Value("randomVelZ", RandomVelZ);
    read.Value("randomPosX", RandomPosX);
    read.Value("randomPosY", RandomPosY);
    read.Value("randomPosZ", RandomPosZ);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    FrameRate = smokeFile.Read<float>("FrameRate").value_or(15.0f);
    const MCFitResult<bool> hasRotation = smokeFile.Read<bool>("HasRotation");
    HasRotation = hasRotation.value_or(false);

    if (hasRotation.has_value())
    {
        read.Value("NumRotations", NumRotations);

        if (read.Failed())
        {
            return std::to_underlying(read.Error());
        }
    }

    const MCFitResult<std::string> shapeName = smokeFile.Read<std::string>("SmokeShape");

    if (!shapeName.has_value())
    {
        return std::to_underlying(shapeName.error());
    }

    MCShapeFile shapeFile;

    if (const int32_t result = shapeFile.Open(GamePath(ShapesPath, *shapeName, ".shp")); result != 0)
    {
        return result;
    }

    SmokeShape = MCRegisteredBlock(shapeFile.FileSize(), MCDataKind::Shapes);
    shapeFile.Read(SmokeShape.Bytes());
    ZVelocity = WorldUnitsPerMeter * ZVelocity;
    return MCObjectType::Init(smokeFile);
}

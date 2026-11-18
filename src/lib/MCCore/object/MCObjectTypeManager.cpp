#include "stdafx.h"
#include "object/MCObjectTypeManager.h"
#include "appear/MCAppearanceTypeList.h"
#include "lib/MCFatal.h"
#include "lib/MCFirstUse.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGamePaths.h"
#include "lib/MCPacketFile.h"
#include "platform/MCFrameLog.h"
#include "object/MCBaseObject.h"
#include "object/MCObjectType.h"
#include "object/MCArtillery.h"
#include "object/MCArtilleryType.h"
#include "object/MCArtilleryChunk.h"
#include "object/MCCameraDrone.h"
#include "object/MCCameraDroneType.h"
#include "object/MCBuilding.h"
#include "object/MCBuildingType.h"
#include "object/MCBuildingMarines.h"
#include "object/MCObjectDrawing.h"
#include "object/MCMiscTerrainObject.h"
#include "object/MCMiscTerrainObjectType.h"
#include "object/MCBullet.h"
#include "object/MCBulletType.h"
#include "object/MCDebris.h"
#include "object/MCDebrisType.h"
#include "object/MCElemental.h"
#include "object/MCElementalType.h"
#include "object/MCElementalGameSystem.h"
#include "object/MCExplosion.h"
#include "object/MCExplosionType.h"
#include "object/MCFire.h"
#include "object/MCFireType.h"
#include "object/MCEffectSystem.h"
#include "object/MCGate.h"
#include "object/MCGateType.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCJet.h"
#include "object/MCJetType.h"
#include "object/MCLaser.h"
#include "object/MCLaserType.h"
#include "object/MCLight.h"
#include "object/MCLightType.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCProjectileLaser.h"
#include "object/MCProjectileLaserType.h"
#include "object/MCSmoke.h"
#include "object/MCSmokeType.h"
#include "object/MCTreeBuilding.h"
#include "object/MCTreeBuildingType.h"
#include "object/MCTerrainObject.h"
#include "object/MCTerrainObjectType.h"
#include "object/MCTrain.h"
#include "object/MCTrainCar.h"
#include "object/MCTrainCarType.h"
#include "object/MCTrainManager.h"
#include "object/MCTree.h"
#include "object/MCTreeType.h"
#include "object/MCTurret.h"
#include "object/MCTurretType.h"

namespace
{
    /// <summary>
    /// A new, empty type of the class an "ObjectClass" block numbers; null for 0xd, which has none. A number no class
    /// has gives the original's Fatal code for it.
    /// </summary>
    std::expected<std::shared_ptr<MCObjectType>, int32_t> NewType(int32_t objectClassNum)
    {
        switch (objectClassNum)
        {
            case 0:
                return std::make_shared<MCTreeType>();
            case 1:
                return std::make_shared<MCBuildingType>();
            case 2:
                return std::make_shared<MCBattleMechType>();
            case 3:
                return std::make_shared<MCGroundVehicleType>();
            case 4:
                return std::make_shared<MCExplosionType>();
            case 5:
                return std::make_shared<MCFireType>();
            case 6:
                return std::make_shared<MCLaserType>();
            case 7:
                return std::make_shared<MCSmokeType>();
            case 8:
                return std::make_shared<MCBulletType>();
            case 9:
                return std::make_shared<MCDebrisType>();
            case 0xb:
                return std::make_shared<MCTerrainObjectType>();
            case 0xc:
                return std::make_shared<MCArtilleryType>();
            case 0xd:
                // Port fix: the original returns the type number as a pointer (and writes through it for a kept type).
                return nullptr;
            case 0xe:
                return std::make_shared<MCElementalType>();
            case 0xf:
                return std::make_shared<MCMiscTerrainObjectType>();
            case 0x10:
                return std::make_shared<MCJetType>();
            case 0x11:
                return std::make_shared<MCProjectileLaserType>();
            case 0x12:
                return std::make_shared<MCTreeBuildingType>();
            case 0x13:
                return std::make_shared<MCCameraDroneType>();
            case 0x14:
                return std::make_shared<MCTrainCarType>();
            case 0x15:
                return std::make_shared<MCTurretType>();
            case 0x16:
                return std::make_shared<MCGateType>();
            case 0x17:
                return std::make_shared<MCLightType>();
            case -1:
                return std::unexpected(static_cast<int32_t>(0xbeef0001));
            default:
                return std::unexpected(static_cast<int32_t>(0xbeef0003));
        }
    }

    /// <summary>The original's Fatal code for an object type's packet that won't open as a type (Load).</summary>
    constexpr int32_t BadTypePacket = static_cast<int32_t>(0xbeef0006);
}

MCObjectTypeManager::MCObjectTypeManager() = default;

MCObjectTypeManager::~MCObjectTypeManager()
{
    // The original's heap went with the types still loaded (kept or still used) without running their destructors;
    // the port deletes them, first to last, then frees the blocks nothing freed.
    while (!_Types.empty())
    {
        std::shared_ptr<MCObjectType> type = std::move(_Types.front());
        _Types.erase(_Types.begin());
    }

    TypeData.Clear();
    ObjectData.Clear();
}

auto MCObjectTypeManager::Create(std::string_view objectFileName)
    -> std::expected<std::unique_ptr<MCObjectTypeManager>, std::string>
{
    auto manager = std::make_unique<MCObjectTypeManager>();
    manager->_ObjectFile = std::make_unique<MCPacketFile>();
    const std::string fileName = GamePath(ObjectPath, objectFileName, ".pak");

    if (const int32_t result = manager->_ObjectFile->Open(fileName); result != 0)
    {
        return std::unexpected(std::format("could not open {} ({:#x})", fileName, static_cast<uint32_t>(result)));
    }

    return manager;
}

auto MCObjectTypeManager::Add(std::shared_ptr<MCObjectType> objType) -> std::shared_ptr<MCObjectType>
{
    return _Types.emplace_back(std::move(objType));
}

auto MCObjectTypeManager::Remove(MCObjectType& objType) -> void
{
    const auto position = std::ranges::find(_Types, &objType, &std::shared_ptr<MCObjectType>::get);

    if (position == _Types.end())
    {
        return;
    }

    objType.NumUsers--;

    if (objType.NumUsers < 1 && objType.KeepMe == 0)
    {
        // Out of the list; it goes with the last object that holds it.
        _Types.erase(position);
    }
}

auto MCObjectTypeManager::Find(int32_t objTypeNum) const -> std::shared_ptr<MCObjectType>
{
    for (const std::shared_ptr<MCObjectType>& type : _Types)
    {
        if (type->ObjTypeNum == objTypeNum)
        {
            return type;
        }
    }

    return nullptr;
}

auto MCObjectTypeManager::Get(int32_t objTypeNum) -> std::shared_ptr<MCBaseObject>
{
    std::shared_ptr<MCObjectType> objType = Find(objTypeNum);

    if (objType == nullptr)
    {
        objType = Load(objTypeNum, 0);
    }

    if (objType == nullptr)
    {
        return nullptr;
    }

    std::shared_ptr<MCBaseObject> object = objType->CreateInstance();

    if (object == nullptr)
    {
        return nullptr;
    }

    objType->NumUsers++;
    return object;
}

auto MCObjectTypeManager::Load(int32_t objTypeNum, int keepMe) -> std::shared_ptr<MCObjectType>
{
    return LoadType(objTypeNum, keepMe, true);
}

auto MCObjectTypeManager::LoadType(int32_t objTypeNum, int keepMe, bool fatal) -> std::shared_ptr<MCObjectType>
{
    // Original behaviour: a type already loaded isn't returned; the caller gets null.
    if (objTypeNum < 1 || Find(objTypeNum) != nullptr || _ObjectFile->SeekPacket(objTypeNum) != 0)
    {
        return nullptr;
    }

    // Port-only (lib/MCFirstUse.h): the type the reads below (its packet, its files, its appearance) are for.
    const auto name = [objTypeNum] { return std::format("object type {:#x}", objTypeNum); };
    const MCFirstUseContext context(name);

    // Port-only: a packet read here as the type is first needed (the frame log's), not with the load's (LoadMadeTypes).
    if (!_ObjectFile->Holds(objTypeNum))
    {
        MCFrameLog::Count(MCPacketsFirstUsedCount);
    }

    // The original stops the game on a packet that won't make a type; a type read ahead (fatal false) is left out.
    const auto refused = [fatal](int32_t errCode) -> std::shared_ptr<MCObjectType>
    {
        if (fatal)
        {
            Fatal(errCode);
        }

        return nullptr;
    };

    // The packet's "ObjectClass" block names the class to make.
    int32_t objectClassNum = -1;
    {
        MCFitIniFile classFile;

        if (classFile.Open(*_ObjectFile, static_cast<uint32_t>(_ObjectFile->GetPacketSize())) != 0 ||
            classFile.SeekBlock("ObjectClass") != 0)
        {
            return refused(BadTypePacket);
        }

        const MCFitResult<int32_t> classNum = classFile.Read<int32_t>("ObjectTypeNum");

        if (!classNum)
        {
            return refused(BadTypePacket);
        }

        objectClassNum = *classNum;
        classFile.Close();
    }

    _ObjectFile->SeekPacket(objTypeNum);
    std::expected<std::shared_ptr<MCObjectType>, int32_t> made = NewType(objectClassNum);

    if (!made.has_value())
    {
        return refused(made.error());
    }

    std::shared_ptr<MCObjectType> objType = std::move(*made);

    if (objType == nullptr)
    {
        return nullptr;
    }

    if (objType->Init(*_ObjectFile, static_cast<uint32_t>(_ObjectFile->GetPacketSize())) != 0)
    {
        return refused(BadTypePacket);
    }

    if (keepMe != 0)
    {
        // A kept type preloads its appearance and explosion.
        objType->KeepMe = 1;
        AppearanceTypeList().GetAppearance(objType->AppearName);
        Load(objType->ExplosionObject, 0);
    }

    objType->ObjTypeNum = objTypeNum;
    return Add(std::move(objType));
}

auto MCObjectTypeManager::LoadMadeTypes() -> MCPacketsHeld
{
    MCPacketsHeld read;
    // The types asked for (one that won't load is asked once).
    std::unordered_set<int32_t> asked;
    // Kept as Load keeps a type: its appearance with it (its explosion loaded and kept the next round).
    const auto keep = [](MCObjectType& type)
    {
        type.KeepMe = 1;
        AppearanceTypeList().GetAppearance(type.AppearName);
    };

    // What the game makes by its own numbers, no type's: a mine's blast (a mine laid, on the map or the staged battle's),
    // each kind of strike a commander may call (its counts the mission's, a script's or the enemy's; the multiplayer
    // kinds too), the camera drone a probe launches, and every weapon's effect (WeaponFXTable): a unit made in play
    // fires its weapons' (a dead vehicle's crew, whose weapons are the marine profile's; a reinforcement), which no
    // type loaded at the load names. Those the data lacks (-1, the expansion's in the campaign) are passed over.
    std::vector<int32_t> gameMade{MineExplosion, CameraDroneType};
    gameMade.insert(gameMade.end(), ArtilleryTypeTable.begin(), ArtilleryTypeTable.end());
    std::ranges::transform(WeaponFXTable, std::back_inserter(gameMade),
                           [](uint32_t effect) { return static_cast<int32_t>(effect); });

    for (;;)
    {
        std::vector<int32_t> wanted;
        std::vector<std::shared_ptr<MCObjectType>> loaded;
        const auto ask = [&](int32_t made)
        {
            if (made < 1 || !asked.insert(made).second)
            {
                return;
            }

            if (std::shared_ptr<MCObjectType> found = Find(made); found == nullptr)
            {
                wanted.push_back(made);
            }
            else if (found->KeepMe == 0)
            {
                loaded.push_back(std::move(found));
            }
        };

        std::ranges::for_each(gameMade, ask);

        for (const std::shared_ptr<MCObjectType>& type : _Types)
        {
            std::ranges::for_each(type->MadeTypes(), ask);
        }

        if (wanted.empty() && loaded.empty())
        {
            return read;
        }

        std::ranges::for_each(loaded, [&](const std::shared_ptr<MCObjectType>& type) { keep(*type); });

        if (wanted.empty() || _ObjectFile == nullptr)
        {
            continue;
        }

        read += _ObjectFile->HoldPackets(wanted);

        for (const int32_t objTypeNum : wanted)
        {
            if (const std::shared_ptr<MCObjectType> type = LoadType(objTypeNum, 0, false); type != nullptr)
            {
                keep(*type);
            }
        }

        _ObjectFile->ReleaseHeld();
    }
}

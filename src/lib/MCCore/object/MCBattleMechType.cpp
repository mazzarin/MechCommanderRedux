#include "stdafx.h"
#include "object/MCBattleMechType.h"
#include "object/MCBattleMech.h"
#include "object/MCMechGameSystem.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCAIControl.h"
#include "object/MCMiscTerrainObject.h"
#include "object/MCMiscTerrainObjectType.h"
#include "object/MCArtillery.h"
#include "object/MCArtilleryType.h"
#include "object/MCArtilleryChunk.h"
#include "object/MCCameraDrone.h"
#include "object/MCCameraDroneType.h"
#include "object/MCLaser.h"
#include "object/MCLaserType.h"
#include "object/MCBullet.h"
#include "object/MCBulletType.h"
#include "object/MCCollisionSystem.h"
#include "object/MCForces.h"
#include "object/MCContactSystem.h"
#include "object/MCDebris.h"
#include "object/MCDebrisType.h"
#include "object/MCElemental.h"
#include "object/MCElementalType.h"
#include "object/MCElementalGameSystem.h"
#include "object/MCExplosion.h"
#include "object/MCExplosionType.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "object/MCMoverGroup.h"
#include "object/MCJet.h"
#include "object/MCJetType.h"
#include "object/MCMechDynamics.h"
#include "object/MCNetControl.h"
#include "object/MCObjectType.h"
#include "object/MCProjectileLaser.h"
#include "object/MCProjectileLaserType.h"
#include "object/MCSmoke.h"
#include "object/MCSmokeType.h"
#include "object/MCEffectSystem.h"
#include "object/MCMechWarrior.h"
#include "object/MCMoverGameSystem.h"
#include "sound/MCRadio.h"
#include "sound/MCSoundSystem.h"
#include "object/MCObjectTypeManager.h"
#include "object/MCWeaponChunkDebug.h"
#include "object/MCWeaponShotInfo.h"
#include "object/MCMoverMath.h"

namespace
{

    /// <summary>
    /// Damage from bumping into <paramref name="other"/>: tonnage / 10 + 1/2 (an enemy) or tonnage / 100 + 1/2 (a
    /// friend), hitting <paramref name="victim"/> from <paramref name="other"/>'s side.
    /// </summary>
    void CollisionHit(MCGameObject& victim, MCGameObject& shooter, MCGameObject& tonnageOf, int32_t attackSource,
                      int friendly)
    {
        const int32_t hitLocation = victim.CalcHitLocation(shooter.GetPosition(), -1, attackSource, 0);
        const float entryAngle = victim.RelFacingTo(shooter.GetPosition(), -1);
        const double scale = friendly == 0 ? 0.1 : 0.01;
        MCWeaponShotInfo shotInfo;
        shotInfo.Init(shooter.SharedAs<MCGameObject>(), -1, static_cast<float>(tonnageOf.GetTonnage() * scale + 0.5),
                      hitLocation, entryAngle);
        victim.HandleWeaponHit(shotInfo, MultiPlayer() != nullptr);
    }
}

MCBattleMechType::MCBattleMechType()
    : CrashAvoidSelf(DefaultMechCrashAvoidSelf)
    , CrashAvoidPath(DefaultMechCrashAvoidPath)
    , CrashBlockSelf(DefaultMechCrashBlockSelf)
    , CrashBlockPath(DefaultMechCrashBlockPath)
    , CrashYieldTime(DefaultMechCrashYieldTime)
{
}

MCBattleMechType::~MCBattleMechType() = default;

auto MCBattleMechType::Init(MCFile& objFile, uint32_t fileSize) -> int32_t
{
    static constexpr std::array<std::string_view, NumMechBodyLocations> bodyLocationNames = {
        "Head", "CenterTorso", "LeftTorso", "RightTorso", "LeftArm", "RightArm", "LeftLeg", "RightLeg"};

    MCFitIniFile mechFile;

    if (const int32_t result = mechFile.Open(objFile, fileSize); result != 0)
    {
        return result;
    }

    if (const int32_t result = mechFile.SeekBlock("Header"); result != 0)
    {
        return result;
    }

    const MCFitResult<std::string> fileType = mechFile.Read<std::string>("FileType");

    if (!fileType.has_value())
    {
        return std::to_underlying(fileType.error());
    }

    if (*fileType != "MechType")
    {
        return -1;
    }

    if (const int32_t result = mechFile.SeekBlock("General"); result != 0)
    {
        return result;
    }

    MCFitReader read(mechFile);
    uint8_t type = 0;
    read.Value("ID", MechId);
    read.Value("Type", type);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    // "Type" 0 is 1, 1 is -1. Port fix: the original reads other values from past its two-entry table on the stack.
    static constexpr std::array<uint8_t, 2> typeMap = {1, 0xff};
    MechType = type < typeMap.size() ? typeMap[type] : 0;
    Name = mechFile.Read<std::string>("Name").value_or("");
    read.Value("Chassis", Chassis);
    read.Value("TonnageClass", TonnageClass);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    ExplRad = mechFile.Read<float>("ExplosionRadius").value_or(0.0f);
    ExplDmg = mechFile.Read<float>("ExplosionDamage").value_or(0.0f);
    uint8_t endo = 0;
    read.Value("EndoSteel", endo);
    EndoSteel = endo;
    read.Value("InternalStructureTonnage", InternalStructureTonnage);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    if (const int32_t result = mechFile.SeekBlock("InternalStructure"); result != 0)
    {
        return result;
    }

    for (int32_t location = 0; location < NumMechBodyLocations; location++)
    {
        read.Value(bodyLocationNames[location], InternalStructure[location]);
    }

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    if (const int32_t result = mechFile.SeekBlock("Debris"); result != 0)
    {
        return result;
    }

    read.Value("RightArmPiece", RightArmDebrisId);
    read.Value("LeftArmPiece", LeftArmDebrisId);
    read.Value("DestroyedPiece", DestroyedPiece);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    if (const int32_t result = mechFile.SeekBlock("Dynamics"); result != 0)
    {
        return result;
    }

    uint32_t dynamicsTypeId = 0;
    read.Value("Type", dynamicsTypeId);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    if (dynamicsTypeId != 1)
    {
        return -0x5fffd;
    }

    auto dynamicsType = MCMechDynamicsType::Create(mechFile);

    if (!dynamicsType.has_value())
    {
        return std::to_underlying(dynamicsType.error());
    }

    DynamicsType = std::move(*dynamicsType);

    if (mechFile.SeekBlock("MovementSystem") == 0)
    {
        // Original behaviour (OB-149): the yield time gets the last long read ("CrashBlockPath"), not the yield time
        // it just read; a missing long reads as zero.
        int32_t value = 0;
        const auto readLong = [&](std::string_view name)
        {
            const MCFitResult<int32_t> result = mechFile.Read<int32_t>(name);

            if (result.has_value())
            {
                value = *result;
                return true;
            }

            if (result.error() == MCFitError::VariableNotFound)
            {
                value = 0;
            }

            return false;
        };

        if (readLong("CrashAvoidSelf"))
        {
            CrashAvoidSelf = value;
        }

        if (readLong("CrashAvoidPath"))
        {
            CrashAvoidPath = value;
        }

        if (readLong("CrashBlockSelf"))
        {
            CrashBlockSelf = value;
        }

        if (readLong("CrashBlockPath"))
        {
            CrashBlockPath = value;
        }

        if (mechFile.Read<float>("CrashYieldTime").has_value())
        {
            CrashYieldTime = static_cast<float>(value);
        }
    }

    if (const int32_t result = LoadHotSpots(mechFile); result != 0)
    {
        return result;
    }

    return MCObjectType::Init(mechFile);
}

auto MCBattleMechType::HandleCollision(MCGameObject& collidee, MCGameObject& collider) -> int
{
    if (MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0)
    {
        return 0;
    }

    int friendly = 0;
    int collideeJumping = static_cast<MCMover&>(collidee).IsJumping();
    int colliderJumping = 0;
    uint32_t sampleId = 4;

    switch (collider.ObjectClass)
    {
        case MCObjectClass::BattleMech:
        {
            if (collidee.GetPilot()->Alignment != collider.GetPilot()->Alignment)
            {
                std::shared_ptr<MCMechWarrior> attackerPilot = collider.GetPilot();

                if (attackerPilot->CurTacOrder.Code == MCTacticalOrderCode::AttackObject)
                {
                    attackerPilot->NumRams++;
                }
                else if (attackerPilot->CurTacOrder.Code == MCTacticalOrderCode::JumpToPoint &&
                         collidee.GetPilot()->CurTacOrder.GetJumpTarget().get() == &collidee)
                {
                    collidee.GetPilot()->NumJumpAttacks++;
                }
            }

            colliderJumping = static_cast<MCMover&>(collider).IsJumping();
            auto& colliderMech = static_cast<MCBattleMech&>(collider);

            if (colliderJumping == 0 && colliderMech.JumpTime >= 0.0f)
            {
                colliderJumping = ScenarioTime - colliderMech.JumpTime < 0.5f ? 1 : 0;
            }

            [[fallthrough]];
        }

        case MCObjectClass::GroundVehicle:
        {
            bool jumpHit = true;

            if (collideeJumping == 0)
            {
                auto& collideeMech = static_cast<MCBattleMech&>(collidee);
                bool landing = false;

                if (collideeMech.JumpTime >= 0.0f)
                {
                    const float sinceJump = ScenarioTime - collideeMech.JumpTime;
                    collideeMech.JumpTime = -1.0f;

                    if (sinceJump < 0.5f)
                    {
                        collideeJumping = 1;
                        landing = true;
                    }
                    else
                    {
                        collideeJumping = 0;
                    }
                }

                if (!landing)
                {
                    jumpHit = colliderJumping != 0;
                }
            }

            if (collidee.GetPilot()->Alignment == collider.GetPilot()->Alignment)
            {
                friendly = 1;

                if (!jumpHit)
                {
                    return 0;
                }
            }
            else if (!jumpHit)
            {
                std::shared_ptr<MCGameObject> collideeRamTarget = collidee.GetPilot()->CurTacOrder.GetRamTarget();
                std::shared_ptr<MCGameObject> colliderRamTarget = collider.GetPilot()->CurTacOrder.GetRamTarget();

                if (collideeRamTarget.get() != &collider && colliderRamTarget.get() != &collidee)
                {
                    return 0;
                }
            }

            if (collidee.GetCollisionFreeFrom().get() == &collider && ScenarioTime <= collidee.GetCollisionFreeTime())
            {
                return 0;
            }

            collidee.SetCollisionFreeFrom(collider.SharedAs<MCGameObject>());
            collidee.SetCollisionFreeTime(ScenarioTime + 2.0f);

            MCFrameOfRef frame = collidee.GetFrame();
            MCMoverMath::RotateAboutK(frame, static_cast<float>(std::sin(MCMoverMath::HalfPi)),
                                      static_cast<float>(std::cos(MCMoverMath::HalfPi)));
            collidee.SetFrame(frame);
            collidee.GetVelocity();

            if (jumpHit)
            {
                if (collideeJumping != 0)
                {
                    // The jumper lands on the other: both take the other's weight (the collider's, twice).
                    const int32_t hitLocation = collidee.CalcHitLocation(collider.GetPosition(), -1, 3, 0);
                    const float entryAngle = collidee.RelFacingTo(collider.GetPosition(), -1);
                    MCWeaponShotInfo shotInfo;
                    shotInfo.Init(collider.SharedAs<MCGameObject>(), -1,
                                  static_cast<float>(collider.GetTonnage() * (friendly == 0 ? 0.1 : 0.01) + 0.5),
                                  hitLocation, entryAngle);
                    collidee.HandleWeaponHit(shotInfo, MultiPlayer() != nullptr);
                    const int32_t otherHitLocation = collider.CalcHitLocation(collidee.GetPosition(), -1, 2, 0);
                    const float otherEntryAngle = collider.RelFacingTo(collidee.GetPosition(), -1);
                    shotInfo.Init(collidee.SharedAs<MCGameObject>(), -1,
                                  static_cast<float>(collider.GetTonnage() * (friendly == 0 ? 0.1 : 0.01) + 0.5),
                                  otherHitLocation, otherEntryAngle);
                    collider.HandleWeaponHit(shotInfo, MultiPlayer() != nullptr);
                    MCVector3D position = collider.GetPosition();
                    ::CreateExplosion(static_cast<int32_t>(MechLandingExplosionType), position, 0.0f, 0.0f);
                }
            }
            else
            {
                CollisionHit(collidee, collider, collider, 1, friendly);
            }

            static_cast<MCMover&>(collidee).BounceToAdjCell();

            if (friendly != 0)
            {
                return 0;
            }
            break;
        }

        case MCObjectClass::Elemental:
        {
            if (collidee.GetPilot()->Alignment == collider.GetPilot()->Alignment)
            {
                return 0;
            }

            std::shared_ptr<MCGameObject> collideeRamTarget = collidee.GetPilot()->CurTacOrder.GetRamTarget();
            std::shared_ptr<MCGameObject> colliderRamTarget = collider.GetPilot()->CurTacOrder.GetRamTarget();

            if (collideeRamTarget.get() != &collider && colliderRamTarget.get() != &collidee)
            {
                return 0;
            }

            if (collidee.GetCollisionFreeFrom().get() == &collider && ScenarioTime <= collidee.GetCollisionFreeTime())
            {
                return 0;
            }

            if (collider.IsMarine() != 0)
            {
                return 0;
            }

            collidee.SetCollisionFreeFrom(collider.SharedAs<MCGameObject>());
            collidee.SetCollisionFreeTime(ScenarioTime + 2.0f);
            collidee.GetVelocity();
            const int32_t hitLocation = collidee.CalcHitLocation(collider.GetPosition(), -1, 1, 0);
            const float entryAngle = collidee.RelFacingTo(collider.GetPosition(), -1);
            MCWeaponShotInfo shotInfo;
            shotInfo.Init(collider.SharedAs<MCGameObject>(), -1, ElmDamageOnImpact, hitLocation, entryAngle);
            collidee.HandleWeaponHit(shotInfo, MultiPlayer() != nullptr);
            sampleId = 0x1e;
            break;
        }

        case MCObjectClass::Building:
        case MCObjectClass::TreeBuilding:
        {
            if (collidee.GetCollisionFreeFrom().get() == &collider && ScenarioTime <= collidee.GetCollisionFreeTime())
            {
                return 0;
            }

            collidee.SetCollisionFreeFrom(collider.SharedAs<MCGameObject>());
            collidee.SetCollisionFreeTime(ScenarioTime + 2.0f);
            const MCVector3D velocity = collidee.GetVelocity();

            const double speed = std::sqrt(
                (static_cast<double>(velocity.X) * velocity.X + static_cast<double>(velocity.Z) * velocity.Z) +
                static_cast<double>(velocity.Y) * velocity.Y);

            if (!(speed > MechCollisionThreshold))
            {
                static_cast<MCMover&>(collidee).BounceToAdjCell();
            }

            const int32_t hitLocation = collidee.CalcHitLocation(collider.GetPosition(), -1, 1, 0);
            const float entryAngle = collidee.RelFacingTo(collider.GetPosition(), -1);
            MCWeaponShotInfo shotInfo;
            shotInfo.Init(collider.SharedAs<MCGameObject>(), -1, static_cast<float>(collider.GetTonnage() * 0.1 + 0.5),
                          hitLocation, entryAngle);
            collidee.HandleWeaponHit(shotInfo, MultiPlayer() != nullptr);
            collider.HandleWeaponHit(shotInfo, MultiPlayer() != nullptr);
            break;
        }

        case MCObjectClass::Tree:
        {
            if (collidee.GetCollisionFreeFrom().get() == &collider && ScenarioTime <= collidee.GetCollisionFreeTime())
            {
                return 0;
            }

            collidee.SetCollisionFreeFrom(collider.SharedAs<MCGameObject>());
            collidee.SetCollisionFreeTime(ScenarioTime + 2.0f);
            MCFrameOfRef frame = collidee.GetFrame();
            collider.GetObjectType();
            double deflection = 0.0;

            if (TonnageClass < TonnageCollisionThreshold)
            {
                deflection = static_cast<double>(TonnageCollisionThreshold) / TonnageClass * TreeDeflection;
            }

            if (deflection > 0.0)
            {
                MCMoverMath::RotateAboutKUnroundedCos(
                    frame, static_cast<float>(std::sin(deflection * MCMoverMath::DegreesToRadians)),
                    std::cos(deflection * MCMoverMath::DegreesToRadians));
                collidee.SetFrame(frame);
            }
            break;
        }

        case MCObjectClass::TrainCar:
        {
            if (collidee.GetCollisionFreeFrom().get() == &collider && ScenarioTime <= collidee.GetCollisionFreeTime())
            {
                return 0;
            }

            collidee.SetCollisionFreeFrom(collider.SharedAs<MCGameObject>());
            collidee.SetCollisionFreeTime(ScenarioTime + 2.0f);
            MCFrameOfRef frame = collidee.GetFrame();
            MCMoverMath::RotateAboutK(frame, static_cast<float>(std::sin(MCMoverMath::HalfPi)),
                                      static_cast<float>(std::cos(MCMoverMath::HalfPi)));
            collidee.SetFrame(frame);
            collidee.GetVelocity();
            static_cast<MCMover&>(collidee).BounceToAdjCell();
            break;
        }

        default:
            return 0;
    }

    SoundSystem()->PlayDigitalSample(sampleId, 1, collidee, false, false);
    return 0;
}

auto MCBattleMechType::HandleDestruction(MCGameObject& collidee, uint32_t colliderId) -> int
{
    auto& mech = static_cast<MCBattleMech&>(collidee);

    if (mech.GetPilot() == nullptr)
    {
        Fatal(0, " No Pilot in this mech! ");
    }

    if (mech.GetPoint().get() == &mech)
    {
        mech.Group.lock()->SetPoint(nullptr);
    }

    if (mech.SensorSystem.lock() != nullptr)
    {
        mech.SensorSystem.lock()->Disable();
    }

    mech.DeathTimer = 0.8f;

    if (mech.Withdrawing != 0)
    {
        mech.GetPilot()->HandleAlarm(MCPilotAlarmType::VehicleWithdrawn, 0);
        TacticalInterface()->RemoveMech(mech.PartId);
        return 1;
    }

    mech.GetPilot()->HandleAlarm(MCPilotAlarmType::VehicleDestroyed, colliderId);
    mech.Status = 2;
    mech.LyingDead = false;
    mech.DeathExplosionDone = false;

    for (int32_t location = 0; location < mech.NumBodyLocations(); location++)
    {
        mech.DestroyBodyLocation(location);
    }

    if (mech.GetAlignment() == HomeTeam()->Alignment)
    {
        FriendlyDestroyed = 1;
        return 1;
    }

    EnemyDestroyed = 1;
    return 1;
}

auto MCBattleMechType::LoadHotSpots(MCFitIniFile& mechFile) -> int32_t
{
    if (const int32_t result = mechFile.SeekBlock("HotSpots"); result != 0)
    {
        return result;
    }

    const MCFitResult<std::string> hotSpotFileName = mechFile.Read<std::string>("HotSpotFileName");

    if (!hotSpotFileName.has_value())
    {
        return std::to_underlying(hotSpotFileName.error());
    }

    MCFitReader read(mechFile);
    read.Value("FootprintType", FootprintType);

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    MCPacketFile hotSpotFile;

    if (const int32_t result = hotSpotFile.Open(GamePath(ShapesPath, *hotSpotFileName, ".hsp")); result != 0)
    {
        return result;
    }

    MCPacketFile outlineFile;

    if (const int32_t result = outlineFile.Open(GamePath(ShapesPath, *hotSpotFileName, ".out")); result != 0)
    {
        return result;
    }

    MCFitIniFile infoFile;

    if (const int32_t result = infoFile.Open(GamePath(ShapesPath, *hotSpotFileName, ".inf")); result != 0)
    {
        return result;
    }

    MCFile jumpFile;

    if (const int32_t result = jumpFile.Open(GamePath(ShapesPath, *hotSpotFileName, ".jmp")); result != 0)
    {
        return result;
    }

    if (const int32_t result = infoFile.SeekBlock("Info"); result != 0)
    {
        return result;
    }

    MCFitReader info(infoFile);
    info.Value("numHotSpotPackets", NumHotSpotPackets);
    info.Value("numWeapons", NumWeapons);
    info.Value("numOthers", NumOthers);

    if (info.Failed())
    {
        return std::to_underlying(info.Error());
    }

    WeaponHotSpots.assign(NumWeapons, 0);

    for (uint32_t weapon = 0; weapon < NumWeapons; weapon++)
    {
        read.Value(std::format("weapon{}", weapon), WeaponHotSpots[weapon]);
    }

    if (read.Failed())
    {
        return std::to_underlying(read.Error());
    }

    // The packet after the gestures' holds 32 bytes of hot spot data per gesture.
    const auto numGestures = static_cast<int32_t>(NumHotSpotPackets);
    HotSpotData.assign(static_cast<size_t>(numGestures) * 32, 0);

    if (hotSpotFile.SeekPacket(numGestures) == 0)
    {
        if (static_cast<size_t>(hotSpotFile.GetPacketSize()) != HotSpotData.size())
        {
            return -0x5fff3;
        }

        hotSpotFile.ReadPacket(numGestures, HotSpotData);
    }

    NumFramesPerHotSpot.assign(static_cast<size_t>(numGestures) + 1, 0);
    GestureOutlines.assign(static_cast<size_t>(numGestures) + 1, {});
    std::vector<std::vector<uint8_t>> packets(static_cast<size_t>(numGestures));

    for (int32_t gesture = 0; gesture < numGestures; gesture++)
    {
        if (const int32_t result = infoFile.SeekBlock(std::format("Gesture{}", gesture)); result != 0)
        {
            return result;
        }

        info.Value("numFramesPerHotSpot", NumFramesPerHotSpot[gesture]);

        if (info.Failed())
        {
            return std::to_underlying(info.Error());
        }

        if (hotSpotFile.SeekPacket(gesture) != 0)
        {
            return -0x5fff2;
        }

        packets[gesture].assign(static_cast<size_t>(hotSpotFile.GetPacketSize()), 0);
        hotSpotFile.ReadPacket(gesture, packets[gesture]);

        if (outlineFile.SeekPacket(gesture) == 0 && outlineFile.GetPacketSize() != 0)
        {
            GestureOutlines[gesture].assign(static_cast<size_t>(outlineFile.GetPacketSize()), 0);
            outlineFile.ReadPacket(gesture, GestureOutlines[gesture]);
        }
    }

    LayOutHotSpotPackets(packets);

    // The jump jets' offsets: (x, y, z) per frame of the jump gesture, for each jet. Whole frames only: two retail
    // files (bw.jmp, ml.jmp) end in a few stray bytes.
    std::vector<uint8_t> jumpBytes(jumpFile.FileSize(), 0);
    jumpFile.Read(jumpBytes);
    const size_t jumpFloats = jumpBytes.size() / (JumpJetCount * 3 * sizeof(float)) * JumpJetCount * 3;
    JumpData.assign(jumpFloats, 0.0f);
    std::memcpy(JumpData.data(), jumpBytes.data(), jumpFloats * sizeof(float));
    return 0;
}

auto MCBattleMechType::CreateInstance() -> std::shared_ptr<MCBaseObject>
{
    auto newMech = std::make_shared<MCBattleMech>();

    if (newMech->Init(shared_from_this()) != 0)
    {
        return nullptr;
    }

    newMech->IdNumber = NextIdNumber++;
    return newMech;
}

auto MCBattleMechType::GestureOutline(uint32_t gesture) const -> std::span<const float>
{
    const std::vector<uint8_t>& outline = GestureOutlines[gesture];
    return {reinterpret_cast<const float*>(outline.data()), outline.size() / sizeof(float)};
}

auto MCBattleMechType::JumpJetOffset(int32_t jet, int32_t frame) const -> MCVector3D
{
    const size_t framesPerJet = JumpData.size() / (JumpJetCount * 3);

    if (framesPerJet == 0)
    {
        return {};
    }

    // OB-175: the jump gesture has a frame more than the file; its last frame holds the jets where they were.
    const size_t jetIndex = std::min(static_cast<size_t>(std::max(jet, 0)), JumpJetCount - 1);
    const size_t frameIndex = std::min(static_cast<size_t>(std::max(frame, 0)), framesPerJet - 1);
    const size_t first = (framesPerJet * jetIndex + frameIndex) * 3;
    return {JumpData[first], JumpData[first + 1], JumpData[first + 2]};
}

auto MCBattleMechType::LayOutHotSpotPackets(const std::vector<std::vector<uint8_t>>& packets) -> void
{
    // The original's type data heap put the weapon hot spots, the hot spot data, the three per-gesture tables, then
    // each gesture's packet and outline one after another, each block behind an 8-byte header and rounded up to 4
    // bytes (16 at least). A packet holding fewer hot spots than declared is read on into the blocks before it.
    struct Block
    {
        size_t Size = 0;
        std::span<const uint8_t> Bytes;
    };

    const auto blockTotal = [](size_t size)
    {
        const size_t total = (size + 0xb) & ~static_cast<size_t>(3);
        return total < 0x10 ? static_cast<size_t>(0x10) : total;
    };

    const auto numGestures = static_cast<int32_t>(NumHotSpotPackets);
    const size_t pointerTable = static_cast<size_t>(numGestures) * 4 + 4;
    std::vector<Block> blocks;
    blocks.push_back({static_cast<size_t>(NumWeapons) * 4, {}});
    blocks.push_back({HotSpotData.size(), HotSpotData});
    blocks.push_back({pointerTable, {}});
    blocks.push_back({pointerTable, {}});
    blocks.push_back({pointerTable, {}});
    std::vector<size_t> packetBlock(static_cast<size_t>(numGestures), 0);

    for (int32_t gesture = 0; gesture < numGestures; gesture++)
    {
        packetBlock[gesture] = blocks.size();
        blocks.push_back({packets[gesture].size(), packets[gesture]});

        if (!GestureOutlines[gesture].empty())
        {
            blocks.push_back({GestureOutlines[gesture].size(), GestureOutlines[gesture]});
        }
    }

    const size_t declared = static_cast<size_t>(NumWeapons) + NumOthers;
    HotSpotPackets.assign(static_cast<size_t>(numGestures), {});
    HotSpotPacketShippedFloats.assign(static_cast<size_t>(numGestures), 0);

    for (int32_t gesture = 0; gesture < numGestures; gesture++)
    {
        const size_t self = packetBlock[gesture];
        const Block& own = blocks[self];
        const size_t shipped = own.Size / sizeof(float);
        HotSpotPacketShippedFloats[gesture] = static_cast<uint32_t>(shipped);
        const size_t wanted = std::max(shipped, static_cast<size_t>(NumFramesPerHotSpot[gesture]) * declared * 3);
        std::vector<float>& packet = HotSpotPackets[gesture];
        packet.assign(3 + wanted, 0.0f);

        if (self + 1 < blocks.size() && !blocks[self + 1].Bytes.empty() && blocks[self + 1].Size >= sizeof(float))
        {
            std::memcpy(packet.data(), blocks[self + 1].Bytes.subspan(blocks[self + 1].Size - sizeof(float)).data(),
                        sizeof(float));
        }

        if (shipped != 0)
        {
            std::memcpy(&packet[3], own.Bytes.data(), shipped * sizeof(float));
        }

        std::vector<uint8_t> tail(blockTotal(own.Size) - 8 - own.Size, 0);
        const size_t tailBytes = (wanted - shipped) * sizeof(float);

        for (size_t above = self; above-- > 0 && tail.size() < tailBytes;)
        {
            const Block& block = blocks[above];
            tail.insert(tail.end(), 8, 0);

            if (!block.Bytes.empty())
            {
                tail.insert(tail.end(), block.Bytes.begin(),
                            block.Bytes.begin() + static_cast<std::ptrdiff_t>(block.Size));
            }
            else
            {
                tail.insert(tail.end(), block.Size, 0);
            }

            tail.insert(tail.end(), blockTotal(block.Size) - 8 - block.Size, 0);
        }

        tail.resize(tailBytes, 0);

        if (tailBytes != 0)
        {
            std::memcpy(&packet[3 + shipped], tail.data(), tailBytes);
        }
    }
}

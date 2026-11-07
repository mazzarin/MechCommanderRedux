#include "stdafx.h"
#include "object/MCTrainCar.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "appear/MCAppearanceType.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCVfxElement.h"
#include "lib/MCFatal.h"
#include "lib/MCDice.h"
#include "main/MCMissionGlobals.h"
#include "main/MCGameStrings.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCCollisionSystem.h"
#include "object/MCExplosion.h"
#include "object/MCForces.h"
#include "object/MCMechGameSystem.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"
#include "object/MCTeam.h"
#include "object/MCTrain.h"
#include "object/MCTrainCarType.h"
#include "object/MCTrainManager.h"
#include "object/MCVertexPlacement.h"
#include "object/MCWeaponShotInfo.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCGVAppearance.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>Degrees to radians, as MCX.EXE stores it (a hair under pi / 180).</summary>
    constexpr double DegreesToRadians = 0x1.1df46a2526c7ap-6;
    /// <summary>An eighth of a turn, as MCX.EXE stores it (a hair under pi / 4).</summary>
    constexpr double EighthTurn = 0x1.921fb5443e88cp-1;
    /// <summary>Radians to degrees at float precision, as TrainCar::relFacingTo uses it.</summary>
    constexpr double RadiansToDegreesFloat = 0x1.ca5dc2p+5;

    /// <summary>Turns <paramref name="frame"/> about its up axis by the angle of sine <paramref name="s"/>.</summary>
    void TurnFrame(MCFrameOfRef& frame, float s, float c)
    {
        const MCVector3D oldI = frame.I;
        frame.I = frame.I * c + frame.J * s;
        frame.J = frame.J * c - oldI * s;
    }
} // namespace

auto MCTrainCar::TurnedFrame(const MCFrameOfRef& frame) -> MCFrameOfRef
{
    MCFrameOfRef turned = frame;
    TurnFrame(turned, static_cast<float>(std::sin(EighthTurn)), static_cast<float>(std::cos(EighthTurn)));
    return turned;
}

MCTrainCar::MCTrainCar() = default;

MCTrainCar::~MCTrainCar() = default;

auto MCTrainCar::GetAppearance() -> std::shared_ptr<MCAppearance>
{
    return Appearance;
}

auto MCTrainCar::HandleStaticCollision() -> void
{
    if (CollisionsOn == 0 || !OnMap)
    {
        return;
    }

    int32_t blockNumber = 0;
    int32_t vertexNumber = 0;
    GetBlockAndVertexNumber(blockNumber, vertexNumber);
    std::shared_ptr<MCObjectList> list = ObjectList()->FindList(std::format("TBlk{}", blockNumber));

    // Port fix: the original reads the list's objects without checking that the block has a list.
    if (list == nullptr)
    {
        return;
    }

    // The terrain objects of its own block, on its own vertex.
    // Port fix (OB-015): every object is checked; the original only stepped to the next one after an
    // object with a type, so one without hung the game.
    for (std::shared_ptr<MCBaseObject> object : *list)
    {
        auto other = std::static_pointer_cast<MCGameObject>(object);

        if (other->GetObjectType() == nullptr)
        {
            continue;
        }

        int32_t otherBlock = -1;
        int32_t otherVertex = -1;

        switch (other->ObjectClass)
        {
            case MCObjectClass::Building:
            case MCObjectClass::Tree:
            case MCObjectClass::TerrainObject:
            case MCObjectClass::TreeBuilding:
                other->GetBlockAndVertexNumber(otherBlock, otherVertex);
                break;
            case MCObjectClass::MiscTerrainObject:
                // Original behaviour (OB-022): walls, bridges and forests read the train car's own vertex, so they
                // always match.
                GetBlockAndVertexNumber(otherBlock, otherVertex);
                break;
            default:
                break;
        }

        if (vertexNumber == otherVertex)
        {
            MCCollisionSystem::DetectStaticCollision(*this, *other);
        }
    }
}

auto MCTrainCar::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    if (const int32_t result = MCGameObject::Init(objType); result != 0)
    {
        return result;
    }

    ObjectClass = MCObjectClass::TrainCar;
    const auto& carType = static_cast<MCTrainCarType&>(*objType);
    Name = LoadGameString(static_cast<uint32_t>(carType.NameId), 0xfe);
    Damage = static_cast<float>(carType.Damage);
    SetTonnage(carType.TonnageClass);
    CollisionsOn = 1;
    Derailed = false;
    std::shared_ptr<MCAppearanceType> apprType = AppearanceTypeList().GetAppearance(objType->AppearName);

    if (apprType == nullptr)
    {
        return static_cast<int32_t>(0xdefc0003);
    }

    Appearance = std::make_shared<MCGVAppearance>();
    Appearance->Init();

    if ((apprType->AppearanceNum & 0xff000000) != 0x5000000)
    {
        return -0x2fff6;
    }

    if (const int32_t result = Appearance->Init(apprType, WeakAs<MCGameObject>()); result != 0)
    {
        return result;
    }

    SetPotentialContact(1);
    JustCreated = true;
    return 0;
}

auto MCTrainCar::SetPartId(int32_t trainNumber, int32_t carNumber) -> void
{
    PartId = carNumber + (trainNumber * 5 + 0x6400) * 0x14;
}

auto MCTrainCar::IsRevealed() -> int
{
    int32_t blockNumber = 0;
    int32_t vertexNumber = 0;
    GetBlockAndVertexNumber(blockNumber, vertexNumber);
    return MCVertexCell::Of(blockNumber, vertexNumber).AnyCornerVisible() ? 1 : 0;
}

auto MCTrainCar::OnScreen() -> int
{
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();

    if (!OnMap || camera == nullptr)
    {
        return 0;
    }

    ScreenPos = ProjectToScreen(Position, *camera);

    if (Appearance->RecalcBounds(*camera) == 0)
    {
        return 0;
    }

    WindowsVisible = Turn;
    return 1;
}

auto MCTrainCar::Update() -> int32_t
{
    JustCreated = false;
    bool visibleNow = false;

    if (!Wrecked)
    {
        // Damaged past half (or, from 10 points up, by chance) the car blows up and jumps the rails.
        const auto& carType = TypeAs<MCTrainCarType>();
        const int32_t maxDamage = carType.Damage;
        bool blowUp = static_cast<float>(maxDamage / 2) <= DamageTaken;

        if (!blowUp && 10.0f <= DamageTaken)
        {
            // Original behaviour (OB-024): the roll derails the car when its damage percentage is BELOW the roll, so
            // lightly damaged cars go more often.
            const int32_t roll = RandomNumber(100);
            blowUp = DamageTaken * 100.0f / static_cast<float>(maxDamage) < static_cast<float>(roll);
        }

        if (blowUp && !Derailed)
        {
            Damage = 0.0f;
            Status = 2;
            ObjType->HandleDestruction(*this, 0);
            Derail(LastHitAngle);
        }

        // Off the map it stops drawing; on a broken bridge it falls in.
        int32_t tileR = 0;
        int32_t tileC = 0;
        int32_t cellR = 0;
        int32_t cellC = 0;
        MCScenarioMap::WorldToMapPos(GetPosition(), tileR, tileC, cellR, cellC);
        OnMap = !(tileR < 0 || GameMap().Height <= tileR || tileC < 0 || GameMap().Width <= tileC);

        if (OnMap)
        {
            const uint32_t overlayType = GameMap().Map[GameMap().Width * tileR + tileC].Overlay & 0x7f;

            if (overlayType == 0x38 || overlayType == 0x3a)
            {
                Wrecked = true;
                Derailed = true;
                CollisionsOn = 0;
                MCVector3D where = GetPosition();
                CreateExplosion(MineExplosion, where, 0.0f, 0.0f);
                Status = 2;
                Train.lock()->RemoveCar(*this, false);
                Speed = 0.0f;
                Train.lock()->RecalcInfo();
                ObjType->HandleDestruction(*this, 0);
            }
        }

        if (OnMap && OnScreen() != 0)
        {
            visibleNow = true;
        }
    }

    MineCheck();

    if (Appearance != nullptr)
    {
        Appearance->Visible = visibleNow;

        if (IsDestroyed() != 0)
        {
            Appearance->SetTypeId(MCGVActorState::Destroyed);
        }
    }

    Appearance->Update();
    return 1;
}

auto MCTrainCar::Render() -> void
{
    if (Wrecked)
    {
        return;
    }

    if (!JustCreated)
    {
        if (Appearance != nullptr)
        {
            Appearance->Visible = OnScreen() != 0 && OnMap;
            Appearance->Update();
        }

        const int32_t contactType = GetContactType(HomeTeam()->Id);

        if (contactType == 2)
        {
            // A sensor contact: a blip sized by tonnage.
            RenderSensorBlip(SensorBlipShape(GetTonnage()));
        }
        else if (contactType == 1)
        {
            // Seen: drawn. The original also started and stopped a looping sound here, from a type field nothing
            // ever set (always -1), so a train car never makes one (OB-023).
            if (WindowsVisible == Turn)
            {
                Appearance->Render(0);
            }
        }
    }

    if (DrawExtents)
    {
        // Debug: the extent radius as an ellipse.
        DrawExtentEllipse(Position, MCVector2D(ObjType->ExtentRadius, ObjType->ExtentRadius));
    }
}

auto MCTrainCar::HandleWeaponHit(MCWeaponShotInfo& shotInfo, int addMultiplayChunk) -> int32_t
{
    if (addMultiplayChunk != 0)
    {
        MultiPlayer()->AddWeaponHitChunk(*this, shotInfo, 0);
    }

    if (0.0f < shotInfo.Damage && IsDestroyed() == 0)
    {
        LastHitAngle = shotInfo.EntryAngle;
        DamageTaken = shotInfo.Damage + DamageTaken;
        const float remaining = Damage - shotInfo.Damage;
        Damage = remaining;

        if (remaining <= 0.0f)
        {
            // Destroyed: it jumps the rails and leaves its train.
            Status = 2;
            Derail(static_cast<float>(RandomNumber(10) - 20));
            Train.lock()->RemoveCar(*this, false);
            Speed = 0.0f;
            Train.lock()->RecalcInfo();
            ObjType->HandleDestruction(*this, 0);
            CollisionsOn = 0;
        }
    }

    return 0;
}

auto MCTrainCar::Derail(float angle) -> void
{
    if (Derailed)
    {
        return;
    }

    // Off the rails: free its cells, then (by the train's speed and a roll) take the car ahead or behind with it.
    LockTrackCells(*this, Train.lock()->TrackDirection, 0);
    Assert(Train.lock() != nullptr, 0, "Car must have a train to derail");
    const std::vector<std::shared_ptr<MCTrainCar>> cars = Train.lock()->PinCars();
    const auto position =
        std::ranges::find_if(cars, [this](const std::shared_ptr<MCTrainCar>& car) { return car.get() == this; });
    Assert(position != cars.end(), 0, "Can't find carEntry for this car");
    const std::shared_ptr<MCTrainCar> ahead = position != cars.begin() ? *std::prev(position) : nullptr;
    const std::shared_ptr<MCTrainCar> behind = std::next(position) != cars.end() ? *std::next(position) : nullptr;
    Derailed = true;
    const auto roll = static_cast<float>(RandomNumber(100));
    std::shared_ptr<MCTrain> oldTrain = Train.lock();
    const bool slowEnough = static_cast<double>(std::abs(oldTrain->Speed)) * 5.0 <= roll;

    if ((Speed <= 0.0f && slowEnough) || behind == nullptr)
    {
        if ((Speed < 0.0f || !slowEnough) && ahead != nullptr)
        {
            ahead->Derail(-angle);
        }
    }
    else
    {
        behind->Derail(-angle);
    }

    // Port fix: the neighbour's derail can split the old train and take it out of the list (the original freed it,
    // then called RemoveCar on it). Only a train the manager still runs is used.
    if (TrainManager().Holds(*oldTrain))
    {
        oldTrain->RemoveCar(*this, false);
    }

    // Slew it round: a random swing (Faithful: -50..0 whichever side it was hit, 1..100 when head on).
    MCFrameOfRef turned = GetFrame();
    double swing;

    if (angle <= 0.0f && 0.0f <= angle)
    {
        swing = 50.0 - RandomNumber(100);
    }
    else
    {
        swing = -static_cast<double>(RandomNumber(50));
    }

    TurnFrame(turned, static_cast<float>(std::sin(swing * DegreesToRadians)),
              static_cast<float>(std::cos(swing * DegreesToRadians)));
    SetFrame(turned);

    // Into water: the car (and its one-car train) is gone.
    std::shared_ptr<MCObjectPosition> objectPosition = GetObjPosition();

    // Port fix: the original reads the object position without checking it for null.
    if (objectPosition != nullptr)
    {
        const int32_t tileR = objectPosition->TileR;
        const int32_t tileC = objectPosition->TileC;
        const MCMapTile& tile = GameMap().Map[GameMap().Width * tileR + tileC];

        if ((tile.Cells & 0x7f) == 0x2b)
        {
            std::shared_ptr<MCTrain> lostTrain = Train.lock();
            TrainManager().RemoveTrain(*lostTrain);
            lostTrain->ForgetCars();
            Wrecked = true;
            CollisionsOn = 0;
            return;
        }

        // Onto a wall or bridge: it takes the train's weight.
        if ((tile.Overlay & 0x7f) == 0x3e)
        {
            const int32_t vbs = MCTerrain::VerticesBlockSide;
            const int32_t partId = ((MCTerrain::BlocksMapSide * (tileR / vbs) + tileC / vbs) * 400 + tileC +
                                    ((tileR - vbs * (tileR / vbs)) - tileC / vbs) * vbs) *
                                       8 +
                                   0x1000;
            auto hit = std::static_pointer_cast<MCGameObject>(ObjectList()->FindObjectFromPart(partId));

            // Port fix: the original reads the object's class without checking that one was found.
            if (hit != nullptr && hit->ObjectClass == MCObjectClass::MiscTerrainObject)
            {
                MCWeaponShotInfo shot;
                shot.Init(nullptr, 0, Train.lock()->GetTotalTonnage() * 0.1f + 0.5f, 0, 0.0f);

                if (MultiPlayer() == nullptr)
                {
                    hit->HandleWeaponHit(shot, 0);
                }
                else if (MultiPlayer()->IsServer != 0)
                {
                    hit->HandleWeaponHit(shot, 1);
                }
            }
        }
    }

    MCVector3D where = GetPosition();
    CreateExplosion(MineExplosion, where, 0.0f, 0.0f);
}

auto MCTrainCar::MineCheck() -> void
{
    if (MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0)
    {
        return;
    }

    int32_t tileR = 0;
    int32_t tileC = 0;
    int32_t cellR = 0;
    int32_t cellC = 0;
    MCScenarioMap::WorldToMapPos(GetPosition(), tileR, tileC, cellR, cellC);
    // Each side's mines only go off under the other side.
    const uint32_t mine = Alignment == -1 || Alignment == 0 ? GameMap().GetInnerSphereMine(tileR, tileC, cellR, cellC)
                                                            : GameMap().GetClanMine(tileR, tileC, cellR, cellC);

    if (mine == 0)
    {
        return;
    }

    MCVector3D where = GetPosition();
    CreateExplosion(MineExplosion, where, MineSplashDamage, WorldUnitsPerMeter * MineSplashRange);
    const int32_t hitLocation = CalcHitLocation(std::nullopt, -1, 3, 0);
    MCWeaponShotInfo shot;
    shot.Init(nullptr, -2, MineBaseDamage, hitLocation, 0.0f);
    HandleWeaponHit(shot, MultiPlayer() != nullptr ? 1 : 0);
    MCMapTile& tile = GameMap().Map[GameMap().Width * tileR + tileC];

    if (GetAlignment() == -1 || GetAlignment() == 0)
    {
        tile.Overlay |= 0x1800;
    }
    else
    {
        tile.Overlay |= 0x6000;
    }

    if (MultiPlayer() != nullptr)
    {
        MultiPlayer()->AddMineChunk(cellR + tileR * 3, cellC + tileC * 3, Alignment == -1 || Alignment != 0 ? 0 : 1, 3,
                                    2);
    }
}

auto MCTrainCar::RelFacingTo(MCVector3D goal, int32_t) -> float
{
    const float x = Position.X;
    const float y = Position.Y;
    const MCFrameOfRef turned = TurnedFrame(Frame);
    MCVector3D facing;
    facing.X = -turned.J.X;
    facing.Y = -turned.J.Y;
    facing.Z = -turned.J.Z;

    MCVector3D toGoal;
    toGoal.X = goal.X - x;
    toGoal.Y = goal.Y - y;
    toGoal.Z = 0.0f;
    const double length =
        std::sqrt((static_cast<double>(toGoal.X) * toGoal.X + static_cast<double>(toGoal.Y) * toGoal.Y) +
                  static_cast<double>(toGoal.Z) * toGoal.Z);

    if (length != 0.0)
    {
        toGoal.X = static_cast<float>(toGoal.X / length);
        toGoal.Y = static_cast<float>(toGoal.Y / length);
        toGoal.Z = static_cast<float>(toGoal.Z / length);
    }

    const double cosine = static_cast<double>(toGoal.Z) * facing.Z + static_cast<double>(toGoal.Y) * facing.Y +
                          static_cast<double>(toGoal.X) * facing.X;
    const auto angle = static_cast<float>(AcosMatherr(cosine) * RadiansToDegreesFloat);

    // Negative to the left.
    if ((facing & toGoal).Z >= 0.0f)
    {
        return -angle;
    }

    return angle;
}

auto MCTrainCar::RelativePosition(float angle, float distance, uint32_t flags) -> MCVector3D
{
    // Off the map: nowhere.
    if (!OnMap)
    {
        return MCVector3D(-999999.0f, -999999.0f, -999999.0f);
    }

    // The point distance meters away at angle: flag 1, an absolute angle in radians; else degrees from the car's
    // facing. The x87 keeps some of the sums below at extended precision, done here in double.
    const float reach = -(WorldUnitsPerMeter * distance);
    const float x = Position.X;
    const float y = Position.Y;
    double offsetX;
    float offsetY;

    if ((flags & 1) != 0)
    {
        const double sine = std::sin(static_cast<double>(angle));
        const auto cosine = static_cast<float>(std::cos(static_cast<double>(angle)));
        offsetX = (sine + 0.0) * reach;
        offsetY = cosine * reach;
    }
    else
    {
        MCFrameOfRef turned = Frame;
        const double radians = (static_cast<double>(angle) + 45.0) * DegreesToRadians;
        TurnFrame(turned, static_cast<float>(std::sin(radians)), static_cast<float>(std::cos(radians)));
        const MCVector3D offset = turned.J * reach;
        offsetX = offset.X;
        offsetY = offset.Y;
    }

    const double targetX = offsetX + x;
    const auto targetY = static_cast<float>(static_cast<double>(offsetY) + y);

    // Flag 2 walks from the car out to the point; otherwise from the point back to the car.
    MCVector2D start;
    MCVector2D end;

    if ((flags & 2) != 0)
    {
        end.X = static_cast<float>(targetX);
        start.X = x;
        start.Y = y;
        end.Y = targetY;
    }
    else
    {
        start.Y = targetY;
        start.X = static_cast<float>(targetX);
        end.X = x;
        end.Y = y;
    }

    // Half a map cell per step.
    const double deltaX = static_cast<double>(end.X) - start.X;
    const auto deltaXf = static_cast<float>(deltaX);
    const float deltaY = end.Y - start.Y;
    const auto length =
        static_cast<float>(std::sqrt(static_cast<double>(deltaY) * deltaY + static_cast<double>(deltaXf) * deltaXf));
    double directionX = deltaX;
    float directionY = deltaY;

    if (length != 0.0)
    {
        directionX = static_cast<double>(deltaXf) / length;
        directionY = static_cast<float>(static_cast<double>(deltaY) / length);
    }

    const auto stepLength = static_cast<float>(static_cast<double>(MCTerrain::MetersPerVertex) * 0.33333334f * 0.5);
    const auto stepX = static_cast<float>(directionX * stepLength);
    const double stepYExact = static_cast<double>(directionY) * stepLength;
    const auto stepY = static_cast<float>(stepYExact);

    if (std::sqrt(stepYExact * stepY + static_cast<double>(stepX) * stepX) == 0.0)
    {
        return MCVector3D(x, y, 0.0f);
    }

    const MCVector2D span = start - end;
    const auto maxDistance =
        static_cast<float>(std::sqrt(static_cast<double>(span.X) * span.X + static_cast<double>(span.Y) * span.Y));
    float traveled = 0.0f;
    MCVector2D current = start;

    // Whether the cell under current is passable.
    const auto cellPassable = [&]()
    {
        MCVector3D point(current.X, current.Y, 0.0f);
        int32_t tileR;
        int32_t tileC;
        int32_t cellR;
        int32_t cellC;
        MCScenarioMap::WorldToMapPos(point, tileR, tileC, cellR, cellC);

        // Port fix: the walk can leave the map, where the original reads outside it. Off the map is impassable.
        if (!GameMap().OnMap(tileR, tileC))
        {
            return 0u;
        }

        return GameMap().Map[GameMap().Width * tileR + tileC].GetCellPassable(cellR, cellC);
    };

    uint32_t passable = cellPassable();
    MCVector2D previous = start;
    // Walk until the cell changes kind (or the distance runs out); the answer is the step before.
    const bool keepGoingWhilePassable = (flags & 2) != 0;

    if ((passable != 0) == keepGoingWhilePassable)
    {
        while (traveled < maxDistance)
        {
            previous = current;
            current.X = stepX + current.X;
            current.Y = stepY + current.Y;
            const double dx = static_cast<double>(current.X) - start.X;
            const double dy = static_cast<double>(current.Y) - start.Y;
            traveled = static_cast<float>(std::sqrt(dx * dx + dy * dy));
            passable = cellPassable();

            if ((passable != 0) != keepGoingWhilePassable)
            {
                break;
            }
        }
    }

    MCVector3D ground(previous.X, previous.Y, 0.0f);
    return MCVector3D(previous.X, previous.Y, GameMap().GetTerrainElevation(ground));
}

auto MCTrainCar::GetMaxAccel() -> float
{
    return TypeAs<MCTrainCarType>().Acceleration;
}

auto MCTrainCar::GetMaxDecel() -> float
{
    return TypeAs<MCTrainCarType>().Deceleration;
}

auto MCTrainCar::GetMaxSpeed() -> float
{
    return TypeAs<MCTrainCarType>().TopSpeed;
}

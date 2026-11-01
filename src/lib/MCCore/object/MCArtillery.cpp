#include "stdafx.h"
#include "object/MCArtillery.h"
#include "ai/MCMoveSystem.h"
#include "ai/MCScenarioMap.h"
#include "camera/MCCamera.h"
#include "camera/MCViewWindow.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCFontElement.h"
#include "engine/MCVfxElement.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "lib/MCDice.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCArtilleryType.h"
#include "object/MCCameraDrone.h"
#include "object/MCCollisionSystem.h"
#include "object/MCCommander.h"
#include "object/MCContactSystem.h"
#include "object/MCExplosion.h"
#include "object/MCForces.h"
#include "object/MCMechGameSystem.h"
#include "object/MCObjectDrawing.h"
#include "object/MCObjectEvent.h"
#include "object/MCObjectSystem.h"
#include "object/MCSensorSystem.h"
#include "object/MCTeam.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCArtilleryButton.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>Incoming-shell sample of the multiplayer strikes (types 507-509), from 4 seconds out.</summary>
    constexpr uint32_t SampleIncomingMultiplayer = 0x3f;
    /// <summary>Incoming-shell sample of the other strikes, from 2 seconds out.</summary>
    constexpr uint32_t SampleIncoming = 0x3e;
    /// <summary>Object type of the camera drone a sensor probe launches.</summary>
    constexpr int32_t CameraDroneType = 0x205;
    /// <summary>The large strikes' places in ArtilleryTypeTable (CallArtillery's strike types 1 and 5).</summary>
    constexpr size_t LargeStrike = 1;
    constexpr size_t LargeStrikeMultiplayer = 5;

    /// <summary>
    /// Sets off every entry of the strike's explosion pattern whose delay has passed since impact and that hasn't
    /// gone off: <c>explosionsPerExplosion</c> explosions scattered around the entry's offset.
    /// </summary>
    /// <returns>True when the pattern's last entry just went off (the strike is over).</returns>
    bool SetOffExplosions(MCArtillery& strike)
    {
        auto& type = strike.TypeAs<MCArtilleryType>();
        // Port-only (HD shake): a large strike's explosions shake the picture.
        const MCBlastShake shake = type.ObjTypeNum == ArtilleryTypeTable[LargeStrike] ||
                                           type.ObjTypeNum == ArtilleryTypeTable[LargeStrikeMultiplayer]
                                       ? MCBlastShake::Shakes
                                       : MCBlastShake::None;

        for (size_t i = 0; i < type.Blasts.size(); i++)
        {
            const MCArtilleryBlast& blast = type.Blasts[i];

            if (!(blast.Delay < std::fabs(strike.TimeToImpact)) || strike.ExplosionsDone[i])
            {
                continue;
            }

            const float centerX = strike.Position.X + blast.OffsetX;
            const float centerY = strike.Position.Y + blast.OffsetY;
            const float centerZ = strike.Position.Z;

            for (int32_t n = 0; n < type.ExplosionsPerExplosion; n++)
            {
                auto offsetX = static_cast<float>(RandomNumber(type.ExplosionRandomOffsetX));
                auto offsetY = static_cast<float>(RandomNumber(type.ExplosionRandomOffsetY));

                if (RollDice(50) != 0)
                {
                    offsetX = -offsetX;
                }

                if (RollDice(50) != 0)
                {
                    offsetY = -offsetY;
                }

                MCVector3D spot(offsetX + centerX, offsetY + centerY, centerZ);
                type.CreateExplosion(spot, 0.0f, 0.0f, shake);
            }

            strike.ExplosionsDone[i] = true;

            if (i + 1 == type.Blasts.size())
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>
    /// Sets off a live mine of the tile at (<paramref name="tileR"/>, <paramref name="tileC"/>): marks it spent with
    /// <paramref name="spentBits"/>, tells the other machines (the mine's <paramref name="side"/>) and blows it up.
    /// </summary>
    void SetOffMine(int32_t tileR, int32_t tileC, uint32_t spentBits, int32_t side)
    {
        MCVector3D minePos = MapTileCellToWorldPos(tileR, tileC, 1, 1);
        GameMap().Map[GameMap().Width * tileR + tileC].Overlay |= spentBits;

        if (MultiPlayer() != nullptr)
        {
            MultiPlayer()->AddMineChunk(tileR * 3, tileC * 3, side, 3, 2);
        }

        CreateExplosion(MineExplosion, minePos, MineSplashDamage, WorldUnitsPerMeter * MineSplashRange);
    }
} // namespace

void CallArtillery(int32_t commanderId, int32_t strikeType, MCVector3D location, int32_t seconds, int randomOffset)
{
    std::shared_ptr<MCCommander> commander = CommanderById(commanderId);

    // Spend one of the commander's strikes of the kind (none left: no strike).
    int32_t& strikesLeft = [&]() -> int32_t&
    {
        switch (strikeType)
        {
            case 0:
            case 4:
                return commander->NumSmallStrikes;
            case 1:
            case 5:
                return commander->NumLargeStrikes;
            case 2:
            case 6:
                return commander->NumSensorStrikes;
            case 3:
            case 7:
                return commander->NumCameraDrones;
            default:
                Fatal(0, " ArtilleryStrike: Bad StrikeType ");
        }
    }();

    if (strikesLeft < 1)
    {
        return;
    }

    strikesLeft--;

    if (MultiPlayer() != nullptr && 4 <= strikeType && strikeType <= 6)
    {
        strikeType -= 4;
    }

    std::shared_ptr<MCArtillery> newStrike =
        CreateObjectAs<MCArtillery>(ArtilleryTypeTable[static_cast<size_t>(strikeType)]);
    MCArtillery& strike = *newStrike;
    strike.RandomOffset = randomOffset != 0;
    strike.SetAlignment(CommanderById(commanderId)->GetTeam()->Alignment);
    AddToDefaultList(std::move(newStrike));

    strike.SetPosition(location);

    if (CommanderById(commanderId) == HomeCommander())
    {
        for (const MCGuiOwned<MCArtilleryButton>& button : TacticalMap()->ArtilleryButtons)
        {
            button->Draw();
        }
    }

    if (seconds != -1)
    {
        strike.TimeToImpact = static_cast<float>(seconds);
    }

    if (seconds < 3)
    {
        strike.TimeToImpact = -1.0f;
    }

    if (MultiPlayer() != nullptr && MultiPlayer()->IsServer != 0)
    {
        MultiPlayer()->AddArtilleryChunk(commanderId, strikeType, location, seconds);
    }
}

MCArtillery::MCArtillery() = default;

MCArtillery::~MCArtillery()
{
    if (SensorSystem.lock() != nullptr)
    {
        SensorSystem.lock()->SetTeam(nullptr);
        SensorSystemManager().FreeSensor(*SensorSystem.lock());
    }
}

auto MCArtillery::Init(std::shared_ptr<MCObjectType> objType) -> int32_t
{
    if (const int32_t result = MCGameObject::Init(objType); result != 0)
    {
        return result;
    }

    SetExists(1);
    JustCreated = true;
    ObjectClass = MCObjectClass::Artillery;
    HasImpacted = false;
    TimeToImpact = -1.0f;
    const auto& type = static_cast<MCArtilleryType&>(*objType);
    ExplosionsDone.assign(type.NominalDamage != 0.0f ? type.Blasts.size() : 0, false);
    return 0;
}

auto MCArtillery::Update() -> int32_t
{
    if (JustCreated)
    {
        SetJustCreated();
    }

    auto& type = TypeAs<MCArtilleryType>();

    if (!type.ShapeData.Empty())
    {
        FrameTime += FrameLength;
        const double frames = static_cast<double>(FrameTime * type.FrameRate);

        if (FrameCount < static_cast<int32_t>(std::floor(frames)))
        {
            const int32_t newCount = static_cast<int32_t>(std::floor(frames));
            const int32_t oldCount = FrameCount;
            FrameCount = newCount;

            if (newCount - oldCount != 0)
            {
                CurrentFrame += static_cast<uint32_t>(newCount - oldCount);

                if (type.FrameCount <= CurrentFrame)
                {
                    CurrentFrame %= type.FrameCount;
                }
            }
        }
    }

    TimeToImpact -= FrameLength;
    TimeToLaunch -= FrameLength;

    // After impact the rest of the pattern goes off on its delays.
    if (HasImpacted && 0.0 < type.NominalDamage)
    {
        CollisionsOn = 0;

        if (SetOffExplosions(*this))
        {
            return 0;
        }
    }

    if (TimeToImpact <= 5.0 && !ImpactSoundPlayed && SoundSystem() != nullptr && 0.0 < type.NominalDamage)
    {
        const int32_t typeNum = GetObjectType()->ObjTypeNum;

        if (typeNum >= 507 && typeNum <= 509 && TimeToImpact < 4.0)
        {
            ImpactSoundPlayed = true;
            SoundSystem()->PlayDigitalSample(SampleIncomingMultiplayer, 1, *this, false, false);
        }
        else if (TimeToImpact < 2.0)
        {
            ImpactSoundPlayed = true;
            SoundSystem()->PlayDigitalSample(SampleIncoming, 1, *this, false, false);
        }
    }

    // Impact: the first explosions go off, and from now on the strike collides.
    if (!HasImpacted && TimeToImpact <= 0.0 && 0.0 < type.NominalDamage)
    {
        if (SetOffExplosions(*this))
        {
            return 0;
        }

        if (RandomOffset)
        {
            RandomNumber(500);
            RandomNumber(500);
        }

        HasImpacted = true;
        CollisionsOn = 1;
    }

    // The sensor probe's sensor shrinks as its time runs out; the strike ends with it.
    if (0.0 < SensorTime && SensorActive)
    {
        SensorTime -= FrameLength;
        SensorRange = SensorTime / type.NominalSensorTime * type.NominalSensorRange * WorldUnitsPerMeter;
        SensorSystem.lock()->SetRange(SensorRange * MetersPerWorldUnit);
    }
    else if (SensorTime <= 0.0 && SensorActive)
    {
        return 0;
    }

    if (type.NominalDamage == 0.0f && TimeToImpact <= 0.0 && !SensorActive)
    {
        if (0.0f < type.NominalSensorTime)
        {
            SensorActive = true;
            SensorTime = type.NominalSensorTime;
            return 1;
        }

        // No sensor time: the strike launches a camera drone instead.
        if (NumCameraDrones == MCCameraDrone::MaxDrones)
        {
            Fatal(0, " Artillery.update: Too many camera drones ");
        }

        std::shared_ptr<MCCameraDrone> newDrone = CreateObjectAs<MCCameraDrone>(CameraDroneType);
        MCCameraDrone& drone = *newDrone;
        const int32_t partId = NumCameraDrones + MCCameraDrone::FirstPartId;
        NumCameraDrones++;
        drone.LaunchTime = ScenarioTime;
        drone.SetPartId(partId);
        MCVector3D here = GetPosition();
        drone.SetPosition(here);
        drone.SpiralDirection = -1;
        drone.SetAlignment(Alignment);
        GameObjectMap().AddObject(drone);
        AddToDefaultList(std::move(newDrone));

        drone.FindNextTargetTile();
        return 0;
    }

    return 1;
}

auto MCArtillery::Render() -> void
{
    if (OnScreen() == 0 || ObjType == nullptr)
    {
        return;
    }

    const auto& type = TypeAs<MCArtilleryType>();
    const std::span<uint8_t> shape = type.ShapeData.Bytes();

    if (JustCreated)
    {
        SetJustCreated();
    }

    // The home side sees its own strikes count down; everyone sees one in its last four seconds.
    if (GetAlignment() != HomeTeam()->Alignment && !(TimeToImpact < 4.0))
    {
        return;
    }

    if (shape.empty())
    {
        return;
    }

    if (!(0.0 < TimeToImpact) && type.NominalSensorTime == 0.0)
    {
        return;
    }

    // The 100% frames come first; the 50% ones (for the original's zoomed-out mode) follow them.
    const auto frame = static_cast<int32_t>(CurrentFrame);

    if (Selected != 0)
    {
        // The original then drew a select box with a routine that does nothing.
        RecalcBounds(*Eye());
    }

    ElementList().OpenGroup(-40000, true);
    ElementList().Add(ElementList().Make<MCVfxElement>(shape, ScreenPos.X, ScreenPos.Y, frame, 0, std::nullopt, 1));

    const int32_t seconds = std::abs(static_cast<int32_t>(std::floor(static_cast<double>(TimeToImpact))));
    const std::string timeString = std::format("{:01}:{:02}", seconds / 60, seconds % 60);
    // Port: the countdown is an overlay, on the screen over the view at its scale; the marker stays on the ground.
    const MCVector2D textPos = MCOverlayPoint(ScreenPos);
    ScreenPos.X = type.FontXOffset + textPos.X + 6.0f;
    ScreenPos.Y = type.FontYOffset + textPos.Y;
    // Blue after impact, yellow before.
    std::shared_ptr<MCGuiFont> font = TimeToImpact <= 0.0f ? BlueDropFont : YellowDropFont;
    const MCOptionalRef<MCGuiFont> fontRef = font != nullptr ? MCOptionalRef<MCGuiFont>(*font) : std::nullopt;
    ElementList().Add(ElementList().Make<MCFontElement>(fontRef, ScreenPos, timeString, -40000));
}

auto MCArtillery::HandleEvent(MCObjectEvent& event) -> int32_t
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

auto MCArtillery::HandleStaticCollision() -> void
{
    if (CollisionsOn == 0)
    {
        return;
    }

    int32_t blockNumber = 0;
    int32_t vertexNumber = 0;
    GetBlockAndVertexNumber(blockNumber, vertexNumber);

    // Mines in the 3x3 map tiles around the strike go off.
    int32_t centerR = 0;
    int32_t centerC = 0;
    int32_t cellR = 0;
    int32_t cellC = 0;
    MCScenarioMap::WorldToMapPos(GetPosition(), centerR, centerC, cellR, cellC);

    for (int32_t tileR = centerR - 1; tileR < centerR + 2; tileR++)
    {
        for (int32_t tileC = centerC - 1; tileC < centerC + 2; tileC++)
        {
            if (tileR * 3 <= -3 || tileR >= GameMap().Height || tileC * 3 <= -3 || tileC >= GameMap().Width)
            {
                continue;
            }

            const uint32_t overlay = GameMap().Map[GameMap().Width * tileR + tileC].Overlay;

            if ((overlay & 0x6000) == 0x4000)
            {
                SetOffMine(tileR, tileC, 0x6000, 1);
            }

            if (((overlay >> 11) & 3) == 2)
            {
                SetOffMine(tileR, tileC, 0x1800, 0);
            }
        }
    }

    // Then the terrain objects of the 3x3 terrain blocks around it.
    const int32_t firstBlock = blockNumber - MCTerrain::BlocksMapSide - 1;

    for (int32_t row = 0; row < 3; row++)
    {
        for (int32_t col = 0; col < 3; col++)
        {
            MCCollisionSystem::DetectBlockCollisions(*this, row * MCTerrain::BlocksMapSide + firstBlock + col);
        }
    }
}

auto MCArtillery::OnScreen() -> int
{
    const std::shared_ptr<MCCamera> camera = ActiveMainCamera();

    if (camera == nullptr)
    {
        return 0;
    }

    ScreenPos = ProjectToScreen(Position, *camera);

    if (RecalcBounds(*camera))
    {
        WindowsVisible = Turn;
        return 1;
    }

    return 0;
}

auto MCArtillery::SetJustCreated() -> void
{
    if (!JustCreated)
    {
        return;
    }

    JustCreated = false;
    const auto& type = TypeAs<MCArtilleryType>();

    if (TimeToImpact == -1.0f)
    {
        TimeToImpact = type.NominalTimeToImpact;
    }

    SensorTime = 0.0f;
    CollisionsOn = 0;
    SensorActive = false;
    SensorRange = type.NominalSensorRange;
    CurrentFrame = type.StartFrame;
    TimeToLaunch = type.NominalTimeToLaunch;

    if (SensorRange != 0.0f)
    {
        SensorSystem = SensorSystemManager().NewSensor();

        if (Alignment == -1)
        {
            SetSensorData(ClanTeam(), -1.0f, -1.0f);
        }
        else if (Alignment == 1)
        {
            SetSensorData(InnerSphereTeam(), -1.0f, -1.0f);
        }
    }
}

auto MCArtillery::RecalcBounds(MCCamera& camera) -> bool
{
    const float left = ScreenPos.X;
    const float top = ScreenPos.Y;
    BoundsLeft = left;
    BoundsTop = top;
    BoundsRight = left;
    BoundsBottom = top;

    if (const std::span<uint8_t> shape = TypeAs<MCArtilleryType>().ShapeData.Bytes(); !shape.empty())
    {
        const int32_t minXY = VfxShapeMinxy(shape, static_cast<int32_t>(CurrentFrame));
        BoundsLeft = static_cast<float>(minXY >> 16) + left;
        BoundsTop = static_cast<float>(static_cast<int16_t>(minXY)) + top;
        const int32_t size = VfxShapeResolution(shape, static_cast<int32_t>(CurrentFrame));
        BoundsRight = static_cast<float>(size >> 16) + BoundsLeft;
        BoundsBottom = static_cast<float>(static_cast<int16_t>(size)) + BoundsTop;
    }

    if (!(0.0f <= BoundsRight && 0.0f <= BoundsBottom))
    {
        return false;
    }

    const auto viewRight = static_cast<int16_t>(std::floor(static_cast<double>(camera.ViewWidth)));
    const auto viewBottom = static_cast<int16_t>(std::floor(static_cast<double>(camera.ViewHeight)));
    return BoundsLeft <= static_cast<float>(viewRight) && BoundsTop <= static_cast<float>(viewBottom);
}

auto MCArtillery::SetSensorData(std::shared_ptr<MCTeam> team, float newSensorTime, float newSensorRange) -> void
{
    if (newSensorTime != -1.0f)
    {
        SensorTime = newSensorTime;
    }

    if (newSensorRange != -1.0f)
    {
        SensorRange = newSensorRange;
    }

    SensorSystem.lock()->Owner = SharedAs<MCGameObject>();
    SensorSystem.lock()->SetTeam(team);
    // The range passed, even -1, not the one kept.
    SensorSystem.lock()->SetRange(newSensorRange);
    SensorSystem.lock()->ScanFrequency = 0.5f;
}

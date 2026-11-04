#include "stdafx.h"
#include "object/MCGroundVehicleDynamics.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCWeatherPlay.h"
#include "object/MCControl.h"
#include "object/MCGroundVehicleControlData.h"
#include "object/MCGroundVehicle.h"
#include "object/MCGroundVehicleType.h"
#include "object/MCGroundVehicleGameSystem.h"
#include "sprite/MCGVAppearance.h"

// The original turns on the x87 stack: the values it keeps at extended precision are doubles here.

float GvWalkSpeed = 0.0f;

namespace
{
    /// <summary>Degrees to radians, as MCX.EXE stores it (a hair under pi / 180).</summary>
    constexpr double DEGREES_TO_RADIANS = 0x1.1df46a2526c7ap-6;

    /// <summary>A control's signed 1/64 steps.</summary>
    constexpr float CONTROL_STEP = 0.015625f;
}

auto MCGroundVehicleDynamicsType::Create(MCFitIniFile& objFile)
    -> std::expected<std::shared_ptr<MCGroundVehicleDynamicsType>, MCFitError>
{
    if (const int32_t result = objFile.SeekBlock("VehicleDynamics"); result != 0)
    {
        return std::unexpected(static_cast<MCFitError>(result));
    }

    auto type = std::make_shared<MCGroundVehicleDynamicsType>();
    MCFitReader read(objFile);
    read.Value("maxTurretYawRate", type->MaxTurretYawRate);
    read.Value("maxTurretYaw", type->MaxTurretYaw);
    read.Value("maxVehicleYawRate", type->MaxVehicleYawRate);

    if (read.Failed())
    {
        return std::unexpected(read.Error());
    }

    type->MaxVehicleYawRate = std::max(type->MaxVehicleYawRate, 720);
    type->MaxVehiclePivotRate =
        objFile.Read<int32_t>("maxVehiclePivotRate")
            .value_or(static_cast<int32_t>(static_cast<float>(type->MaxVehicleYawRate) * 0.25f));
    read.Value("maxAccel", type->MaxAccel);
    read.Value("maxVelocity", type->MaxVelocity);

    if (read.Failed())
    {
        return std::unexpected(read.Error());
    }

    // The FIT's acceleration is replaced.
    type->MaxAccel = type->MaxVelocity * 5.0f;
    return type;
}

auto MCGroundVehicleDynamicsType::CreateInstance(MCGameObject& object) -> std::unique_ptr<MCDynamics>
{
    return std::make_unique<MCGroundVehicleDynamics>(
        std::static_pointer_cast<MCGroundVehicleDynamicsType>(shared_from_this()), object.WeakAs<MCGameObject>());
}

MCGroundVehicleDynamics::MCGroundVehicleDynamics(std::shared_ptr<MCGroundVehicleDynamicsType> type,
                                                 std::weak_ptr<MCGameObject> object)
    : MCDynamics(type, std::move(object)), Accel(type->MaxAccel)
{
}

auto MCGroundVehicleDynamics::Update() -> int32_t
{
    auto vehicle = std::static_pointer_cast<MCGroundVehicle>(LockObject(Me));
    const auto& dynType = static_cast<const MCGroundVehicleDynamicsType&>(*Type);
    auto& controlData = static_cast<MCGroundVehicleControlData&>(*vehicle->Control->ControlData);

    // This frame's turns, in degrees.
    const int32_t yawRate = controlData.Pivot != 0 ? dynType.MaxVehiclePivotRate : dynType.MaxVehicleYawRate;
    const float bodyTurn = static_cast<float>(static_cast<double>(FrameLength) *
                                              (static_cast<double>(controlData.Rotate) * CONTROL_STEP * yawRate));
    double turretTurn =
        static_cast<double>(controlData.TurretRotate) * CONTROL_STEP * dynType.MaxTurretYawRate * FrameLength;

    // The turret turns up to its limit either way.
    if (turretTurn != 0.0)
    {
        const float limit = static_cast<float>(dynType.MaxTurretYaw);
        const float negLimit = static_cast<float>(-dynType.MaxTurretYaw);
        float current = vehicle->TurretRotation;

        if (current > limit)
        {
            current = limit;
            turretTurn = 0.0;
        }

        if (current < negLimit)
        {
            current = negLimit;
            turretTurn = 0.0;
        }

        if (current + turretTurn > limit)
        {
            turretTurn = static_cast<double>(limit) - current;
        }

        if (current + turretTurn < negLimit)
        {
            turretTurn = static_cast<double>(negLimit) - current;
        }

        vehicle->TurretRotation = static_cast<float>(current + turretTurn);
    }

    // The body turns about its up axis (the sine is stored as a float, the cosine isn't).
    const MCFrameOfRef frame = vehicle->GetFrame();
    const double angle = static_cast<double>(bodyTurn) * DEGREES_TO_RADIANS;
    const float s = static_cast<float>(std::sin(angle));
    const double c = std::cos(angle);
    MCFrameOfRef turned = frame;
    turned.I.X = static_cast<float>(c * frame.I.X + static_cast<double>(frame.J.X) * s);
    turned.I.Y = static_cast<float>(frame.I.Y * c) + frame.J.Y * s;
    turned.I.Z = static_cast<float>(frame.I.Z * c) + frame.J.Z * s;
    turned.J.X = static_cast<float>(frame.J.X * c) - frame.I.X * s;
    turned.J.Y = static_cast<float>(frame.J.Y * c) - frame.I.Y * s;
    turned.J.Z = static_cast<float>(c * frame.J.Z - static_cast<double>(frame.I.Z * s));
    vehicle->SetFrame(turned);

    // Moving: the appearance animates.
    auto appearance = std::static_pointer_cast<MCGVAppearance>(vehicle->GetAppearance());
    const float throttle = static_cast<float>(static_cast<float>(controlData.Throttle) * 0.01);

    if (appearance != nullptr && throttle != 0.0f && dynType.MaxVelocity != 0.0f)
    {
        appearance->SetTypeId(MCGVActorState::Damaged);
        appearance->Update();
    }

    // Accelerate toward the throttle's share of the top (or walking) speed.
    float topSpeed = dynType.MaxVelocity;

    if (controlData.Walk != 0)
    {
        topSpeed = GvWalkSpeed;
    }

    // Port: as fast as the match's weather lets it (MCWeatherPlay::VehicleSpeed; outside a match, all of it).
    topSpeed *= MCWeatherPlayNow().VehicleSpeed;

    const float speedChange = static_cast<float>(static_cast<double>(topSpeed) * throttle - Velocity);

    if ((speedChange < 0.0f && 0.0f < Accel) || (0.0f < speedChange && Accel < 0.0f))
    {
        Accel = -Accel;
    }

    double step = static_cast<double>(FrameLength) * Accel;

    if (std::fabs(speedChange) < std::fabs(step))
    {
        step = speedChange;
    }

    Velocity = static_cast<float>(step + Velocity);
    return 1;
}

auto MCGroundVehicleDynamics::Brake() -> int32_t
{
    Velocity = 0.0f;
    return 0;
}

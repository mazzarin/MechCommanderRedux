#include "stdafx.h"
#include "object/MCBigGameObject.h"
#include "ai/MCMoveSystem.h"
#include "camera/MCCamera.h"
#include "camera/MCCameraList.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCVfxElement.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "main/MCGameContext.h"
#include "main/MCMissionGlobals.h"
#include "object/MCContactSystem.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectType.h"
#include "sound/MCSoundSystem.h"
#include "vfx/MCVfxFunctions.h"

MCBigGameObject::~MCBigGameObject()
{
    // The systems can be gone when an object outlives them (a test's, or one held outside the lists).
    if (std::shared_ptr<MCPotentialContact> contact = PotentialContact.lock();
        contact != nullptr && MCGameContext::Current().HasContactSystem())
    {
        PotentialContactManager().Remove(*contact);
    }

    if (const MCGameContext& context = MCGameContext::Current();
        ObjPosition.lock() != nullptr && context.HasMoveSystem() && context.MoveSystem().ObjectMap != nullptr)
    {
        GameObjectMap().RemoveObject(*this);
    }
}

auto MCBigGameObject::Init(std::shared_ptr<MCObjectType> type) -> int32_t
{
    ObjectClass = MCObjectClass::BigGameObject;
    ObjType = type;
    return 0;
}

auto MCBigGameObject::Kill() -> int32_t
{
    // Port fix: with no type the original used uninitialised ids.
    int32_t explosionId = -1;
    int32_t destroyedId = -1;

    if (GetObjectType() != nullptr)
    {
        explosionId = GetObjectType()->ExplosionObject;
        destroyedId = GetObjectType()->DestroyedObject;
    }

    if (explosionId != -1)
    {
        std::shared_ptr<MCGameObject> explosion = CreateObject(explosionId);
        MCVector3D here = GetPosition();
        explosion->SetPosition(here);
        MCVector3D velocity = GetVelocity();
        explosion->SetVelocity(velocity);
        AddToDefaultList(std::move(explosion));
    }

    if (destroyedId != -1)
    {
        std::shared_ptr<MCGameObject> wreck = CreateObject(destroyedId);
        MCVector3D here = GetPosition();
        wreck->SetPosition(here);
        MCVector3D velocity = GetVelocity();
        wreck->SetVelocity(velocity);
        MCFrameOfRef frame = GetFrame();
        wreck->SetFrame(frame);
        AddToDefaultList(std::move(wreck));
    }

    return static_cast<int32_t>(0xbeaddead);
}

auto MCBigGameObject::OnScreen() -> int
{
    const std::shared_ptr<MCCamera> camera = CameraList()->FindCameraFromIDNumber(1);

    if (camera == nullptr || camera->Active == 0)
    {
        return 0;
    }

    const float dx = Position.X - camera->Position.X;
    const float dy = Position.Y - camera->Position.Y;
    const float dz = Position.Z - camera->Position.Z;
    const float screenX = static_cast<float>(static_cast<double>(dy) * camera->CosAngle +
                                             static_cast<double>(dx) * camera->CosAngle + camera->HalfWidth);
    ScreenPos.X = screenX;
    const float screenY = static_cast<float>(((static_cast<double>(dx) * camera->SinAngle + camera->HalfHeight) -
                                              static_cast<double>(dy) * camera->SinAngle) -
                                             static_cast<double>(dz));
    ScreenPos.Y = screenY;

    if (screenX >= 0.0f && screenY >= 0.0f && screenX <= camera->ViewWidth && screenY <= camera->ViewHeight)
    {
        WindowsVisible = Turn;
        return 1;
    }

    return 0;
}

auto MCSensorBlipPoses(float& carried, float seconds) -> int32_t
{
    // A frame a pose long moves it on a pose, whatever the float rounding: a thousandth of a pose's slack.
    constexpr double PoseSlack = 1e-3;

    if (!std::isfinite(seconds) || seconds <= 0.0f)
    {
        return 0;
    }

    const double poses = static_cast<double>(carried) + static_cast<double>(seconds) * SensorBlipPosesPerSecond;
    const double whole = std::floor(poses + PoseSlack);
    carried = static_cast<float>(std::max(0.0, poses - whole));
    return static_cast<int32_t>(whole);
}

auto MCBigGameObject::RenderSensorBlip(std::span<uint8_t> shape) -> void
{
    // The blip's ping, each time round.
    constexpr uint32_t SensorBlipSample = 0x14;
    const int32_t poses = shape.empty() ? 0 : VfxShapeCount(shape);

    if (poses <= 0)
    {
        return;
    }

    // Round to the first pose again, with the ping (only with sound on: the train car's ignored it).
    if (poses <= BlipFrame)
    {
        if (SoundSystem() != nullptr && UseSound != 0)
        {
            SoundSystem()->PlayDigitalSample(SensorBlipSample, 1, *this, false, true);
        }

        BlipFrame %= poses;
    }

    ElementList().OpenGroup(-100000, true);
    ElementList().Add(ElementList().Make<MCVfxElement>(shape, ScreenPos.X, ScreenPos.Y, BlipFrame, 0, std::nullopt, 0));
    BlipFrame += MCSensorBlipPoses(BlipTime, FrameLength);
}

auto MCBigGameObject::SetPotentialContact(int32_t contactType) -> void
{
    // The contact list by alignment: the clan's for -1, the Inner Sphere's for 1, else the allied list.
    MCPotentialContactType listType;

    if (Alignment == -1)
    {
        listType = MCPotentialContactType::Clan;
    }
    else
    {
        listType = Alignment != 1 ? MCPotentialContactType::Allied : MCPotentialContactType::InnerSphere;
    }

    if (contactType == 0)
    {
        if (const std::shared_ptr<MCPotentialContact> contact = PotentialContact.lock())
        {
            PotentialContactManager().Remove(*contact);
        }

        PotentialContact.reset();
        return;
    }

    if (PotentialContact.lock() == nullptr)
    {
        PotentialContact = PotentialContactManager().Add(listType, *this, static_cast<int8_t>(contactType));
        return;
    }

    PotentialContactManager().Move(*PotentialContact.lock(), listType, static_cast<int8_t>(contactType));
}

auto MCBigGameObject::UpdateContactStatus(MCTeam& contactTeam) -> void
{
    if (PotentialContact.lock() != nullptr)
    {
        PotentialContact.lock()->UpdateStatus(contactTeam);
    }
}

auto MCBigGameObject::GetContactCount(int32_t teamId) -> int32_t
{
    Assert(teamId > -1, -1, " Bad Team Id ");

    if (PotentialContact.lock() != nullptr)
    {
        return static_cast<uint8_t>(PotentialContact.lock()->NumSensors[teamId]);
    }

    return 0;
}

// The "tagged" flag is the contact's LostVisual byte.

auto MCBigGameObject::SetContactTagged(int32_t teamId, int tagged) -> void
{
    Assert(teamId > -1, -1, " Bad Team Id ");
    Assert(PotentialContact.lock() != nullptr, -1, " Is Not Potential Contact ");

    if (PotentialContact.lock() != nullptr)
    {
        PotentialContact.lock()->LostVisual[teamId] = static_cast<uint8_t>(tagged);
    }
}

auto MCBigGameObject::GetContactTagged(int32_t teamId) -> int
{
    Assert(teamId > -1, -1, " Bad Team Id ");

    if (PotentialContact.lock() != nullptr)
    {
        return PotentialContact.lock()->LostVisual[teamId];
    }

    return 0;
}

auto MCBigGameObject::GetContactType(int32_t teamId, int& tagged) -> int32_t
{
    Assert(teamId > -1, -1, " Bad Team Id ");

    if (PotentialContact.lock() != nullptr)
    {
        tagged = PotentialContact.lock()->LostVisual[teamId];
        return static_cast<int32_t>(PotentialContact.lock()->ContactStatus[teamId]);
    }

    return 0;
}

auto MCBigGameObject::GetContactType(int32_t teamId) -> int32_t
{
    Assert(teamId > -1, -1, " Bad Team Id ");

    if (PotentialContact.lock() != nullptr)
    {
        return static_cast<int32_t>(PotentialContact.lock()->ContactStatus[teamId]);
    }

    return 0;
}

auto MCBigGameObject::GetScreenPos(int32_t) -> MCVector2D
{
    return ScreenPos;
}

auto MCBigGameObject::SetAlignment(int32_t newAlignment) -> void
{
    Alignment = newAlignment;

    if (PotentialContact.lock() != nullptr)
    {
        SetPotentialContact(PotentialContact.lock()->Visibility);
    }
}

auto MCBigGameObject::GetMechClass() -> MCMechClass
{
    if (ObjectClass != MCObjectClass::BattleMech)
    {
        return MCMechClass::None;
    }

    if (Tonnage < 35.0f)
    {
        return MCMechClass::Light;
    }

    if (Tonnage < 55.0f)
    {
        return MCMechClass::Medium;
    }

    if (Tonnage >= 75.0f)
    {
        return MCMechClass::Assault;
    }

    return MCMechClass::Heavy;
}

auto MCBigGameObject::DecrementAttackers() -> void
{
    Assert(NumAttackers > 0, 0);
    NumAttackers--;
}

auto MCBigGameObject::GetVitalInfo(std::span<uint8_t> vitalInfo) -> int32_t
{
    // The original only asks isCaptured and fills nothing.
    if (!vitalInfo.empty())
    {
        IsCaptured();
    }

    return 0x19;
}

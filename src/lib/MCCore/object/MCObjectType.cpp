#include "stdafx.h"
#include "object/MCObjectType.h"
#include "lib/MCFitIniFile.h"
#include "object/MCExplosion.h"
#include "object/MCGameObject.h"
#include "object/MCObjectSystem.h"

uint32_t NextIdNumber = 0x30000001;

void ReadEffectId(MCFitIniFile& file, std::string_view name, uint32_t& value)
{
    const MCFitResult<uint32_t> id = file.Read<uint32_t>(name);

    if (id.has_value())
    {
        value = *id;
    }
    else if (id.error() == MCFitError::VariableNotFound)
    {
        value = 0;
    }
}

auto MCObjectType::CreateInstance() -> std::shared_ptr<MCBaseObject>
{
    auto object = std::make_shared<MCBaseObject>();
    object->Init(shared_from_this());
    object->IdNumber = NextIdNumber++;
    return object;
}

auto MCObjectType::Init(MCFitIniFile& typeFile) -> int32_t
{
    int32_t result = typeFile.SeekBlock("ObjectType");

    if (result != 0)
    {
        return result;
    }

    NumUsers = 0;
    // The required entries, in the original's order: the first missing one is the error.
    const MCFitResult<int32_t> typeClass = typeFile.Read<int32_t>("Type");

    if (!typeClass)
    {
        return static_cast<int32_t>(typeClass.error());
    }

    TypeClass = *typeClass;
    const MCFitResult<int32_t> appearance = typeFile.Read<int32_t>("Appearance");

    if (!appearance)
    {
        return static_cast<int32_t>(appearance.error());
    }

    AppearName = static_cast<uint32_t>(*appearance);
    const MCFitResult<int32_t> explosion = typeFile.Read<int32_t>("ExplosionObject");

    if (!explosion)
    {
        return static_cast<int32_t>(explosion.error());
    }

    ExplosionObject = *explosion;
    const MCFitResult<int32_t> destroyed = typeFile.Read<int32_t>("DestroyedObject");

    if (!destroyed)
    {
        return static_cast<int32_t>(destroyed.error());
    }

    DestroyedObject = *destroyed;
    const MCFitResult<float> extent = typeFile.Read<float>("ExtentRadius");

    if (!extent)
    {
        return static_cast<int32_t>(extent.error());
    }

    ExtentRadius = *extent;
    KeepMe = typeFile.Read<int32_t>("KeepMe").value_or(0);
    IconNumber = typeFile.Read<int32_t>("IconNumber").value_or(-1);
    TeamId = typeFile.Read<int32_t>("Alignment").value_or(0);
    return 0;
}

auto MCObjectType::CreateExplosion(MCVector3D& position, float damage, float radius, MCBlastShake shake) const -> void
{
    if (ExplosionObject == -1)
    {
        return;
    }

    std::shared_ptr<MCGameObject> explosion = CreateObject(ExplosionObject);

    if (explosion == nullptr)
    {
        return;
    }

    explosion->SetPosition(position);

    if (radius != 0.0)
    {
        explosion->SetExplRad(radius);
        explosion->SetExplDmg(damage);
    }

    if (const auto blast = std::dynamic_pointer_cast<MCExplosion>(explosion); blast != nullptr)
    {
        blast->Shakes = shake == MCBlastShake::Shakes;
    }

    AddToDefaultList(std::move(explosion));
}

auto MCObjectType::HandleDestruction(MCGameObject& collidee, uint32_t /*colliderId*/) -> int
{
    if (ExplosionObject != -1)
    {
        MCVector3D position = collidee.GetPosition();
        CreateExplosion(position, 0.0f, 0.0f);
    }

    return 1;
}

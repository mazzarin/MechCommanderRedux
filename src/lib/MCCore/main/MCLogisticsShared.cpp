#include "stdafx.h"
#include "main/MCLogisticsShared.h"
#include "main/MCGameStrings.h"
#include "sound/MCSoundSystem.h"

auto LogWeightClass(float tonnage) -> int32_t
{
    if (tonnage < 40.0f)
    {
        return 0;
    }

    if (tonnage < 60.0f)
    {
        return 1;
    }

    if (tonnage < 80.0f)
    {
        return 2;
    }

    return 3;
}

auto LogSlotForm(uint8_t masterID) -> MCComponentForm
{
    return masterID < NumMasterComponents() ? MasterComponentList[masterID].Form : MCComponentForm::Simple;
}

auto LoadLogString(uint32_t id) -> std::string
{
    return LoadGameString(id, 0xfe);
}

auto LogFileBaseName(std::string_view path, size_t maxLength) -> std::string
{
    const size_t separator = path.find_last_of("\\/:");
    std::string_view name = separator == std::string_view::npos ? path : path.substr(separator + 1);
    const size_t dot = name.rfind('.');

    if (dot != std::string_view::npos)
    {
        name = name.substr(0, dot);
    }

    return std::string(name.substr(0, maxLength));
}

auto ReadText(MCFitIniFile& file, std::string_view name, size_t maxLength, std::string& text) -> bool
{
    MCFitResult<std::string> value = file.Read<std::string>(name);

    if (!value.has_value())
    {
        return false;
    }

    if (value->size() >= maxLength)
    {
        text = value->substr(0, maxLength);
        return false;
    }

    text = std::move(*value);
    return true;
}

auto ReadRequiredText(MCFitIniFile& file, std::string_view name, size_t maxLength, std::string_view message)
    -> std::string
{
    std::string text;
    const bool read = ReadText(file, name, maxLength, text);
    Assert(read, 0, message);
    return text;
}

auto MPPurchaseFileName(MCFitIniFile& file) -> std::string
{
    // The original read the name into a 9-byte buffer.
    constexpr size_t MaxPurchaseName = 9;
    std::string purchaseName = "purchase";

    if (file.SeekBlock("PurchaseInfo") != 0)
    {
        ReadText(file, "MainPurchaseFile", MaxPurchaseName, purchaseName);
    }

    return purchaseName;
}

auto PlayLogSound(uint32_t sampleId) -> void
{
    // A test may run the controls without a sound system.
    if (const std::shared_ptr<MCSoundSystem> sound = SoundSystem(); sound != nullptr)
    {
        sound->PlayInterfaceSample(sampleId);
    }
}

auto InternalClassString(int32_t internals) -> uint32_t
{
    return internals < 0x24   ? 100u
           : internals < 0x38 ? 0x4fu
           : internals < 0x51 ? 0x65u
           : internals < 0x79 ? 0x51u
                              : 0x66u;
}

auto WeightClassString(float tonnage) -> uint32_t
{
    if (tonnage < 40.0f)
    {
        return 0x4f;
    }

    if (tonnage < 60.0f)
    {
        return 0x50;
    }

    if (tonnage < 80.0f)
    {
        return 0x51;
    }

    return 0x52;
}

auto ArmorClassString(float armorTonnage) -> uint32_t
{
    if (armorTonnage <= 2.0f)
    {
        return 100;
    }

    if (armorTonnage <= 7.0f)
    {
        return 0x4f;
    }

    if (armorTonnage <= 12.0f)
    {
        return 0x65;
    }

    if (armorTonnage <= 17.0f)
    {
        return 0x51;
    }

    return 0x66;
}

auto PrepareInfoDescription(std::string& description) -> void
{
    if (description.size() > 3)
    {
        description[3] = '9';
    }
}

auto ComponentForm(uint8_t masterID) -> MCComponentForm
{
    return MasterComponentList[masterID].Form;
}

auto IsWeapon(MCComponentForm form) -> bool
{
    return form == MCComponentForm::WeaponEnergy || form == MCComponentForm::WeaponBallistic ||
           form == MCComponentForm::WeaponMissile;
}

auto IsEquipment(MCComponentForm form) -> bool
{
    return form == MCComponentForm::Sensor || form == MCComponentForm::Ecm || form == MCComponentForm::Probe;
}

auto UsesAmmo(uint8_t masterID) -> bool
{
    return ComponentForm(masterID) == MCComponentForm::WeaponBallistic ||
           ComponentForm(masterID) == MCComponentForm::WeaponMissile;
}

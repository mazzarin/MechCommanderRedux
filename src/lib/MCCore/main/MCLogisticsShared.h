#pragma once

// Helpers the logistics records, lists and screens share (main/MCLogistics*.cpp and the record files). Port-only
// (SPEC-menus.md's stage 9): with them, what the remade screens kept of the 1998 screens' rows (logistics\logrows.cpp's
// helpers: a component's form, a unit's class words) and their sound.

#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "object/MCMasterComponent.h"

/// <summary>The object packet file under <c>ObjectPath</c> the chassis profiles are read from.</summary>
inline constexpr std::string_view ObjectPakName = "object2.pak";

/// <summary>The weight class of a tonnage: 0 light (below 40), 1 medium, 2 heavy (60), 3 assault (80).</summary>
int32_t LogWeightClass(float tonnage);

/// <summary>
/// The form of component <paramref name="masterID"/>. An empty critical slot holds 0xff, one past the 255
/// components; the original read the form from past the end of the table there. The port gives Simple (0).
/// </summary>
MCComponentForm LogSlotForm(uint8_t masterID);

/// <summary>String <paramref name="id"/> of the string table (at most 0xfd characters).</summary>
std::string LoadLogString(uint32_t id);

/// <summary>
/// The file name part of <paramref name="path"/> without folder or extension (<c>_splitpath</c>'s fname), at most
/// <paramref name="maxLength"/> characters (the original's fname went straight into a 12-byte field).
/// </summary>
std::string LogFileBaseName(std::string_view path, size_t maxLength);

/// <summary>
/// Entry <paramref name="name"/> of the current block. One that can't be read is reported (<paramref name="message"/>,
/// the game goes on, as after the original's Assert) and reads as zero.
/// </summary>
template <MCFitValue T> T ReadRequired(MCFitIniFile& file, std::string_view name, std::string_view message)
{
    MCFitResult<T> value = file.Read<T>(name);
    Assert(value.has_value(), value.has_value() ? 0 : static_cast<uint32_t>(std::to_underlying(value.error())),
           message);
    return value.has_value() ? std::move(*value) : T{};
}

/// <summary>
/// Reads entry <paramref name="name"/> into <paramref name="value"/> as the original's unchecked reads did: a missing
/// entry stores zero, another error leaves the value alone.
/// </summary>
/// <returns>Whether the entry was read.</returns>
template <MCFitValue T> bool ReadEntry(MCFitIniFile& file, std::string_view name, T& value)
{
    MCFitResult<T> result = file.Read<T>(name);

    if (result.has_value())
    {
        value = std::move(*result);
        return true;
    }

    if (result.error() == MCFitError::VariableNotFound)
    {
        value = T{};
    }

    return false;
}

/// <summary>
/// The text of entry <paramref name="name"/>, as the original's <c>readIdString</c> into a buffer of
/// <paramref name="maxLength"/> bytes left it: a text of <paramref name="maxLength"/> characters or more is an error
/// (BufferTooSmall), cut to <paramref name="maxLength"/> characters in <paramref name="text"/>.
/// </summary>
/// <returns>Whether the text was read whole.</returns>
bool ReadText(MCFitIniFile& file, std::string_view name, size_t maxLength, std::string& text);

/// <summary>
/// <see cref="ReadText"/> of a text the profile must have: one that can't be read whole is reported
/// (<paramref name="message"/>) and what was read is returned.
/// </summary>
std::string ReadRequiredText(MCFitIniFile& file, std::string_view name, size_t maxLength, std::string_view message);

/// <summary>
/// The purchase list (a FIT under <c>MissionPath</c>, no extension) a multiplayer mission file names, with
/// <paramref name="file"/> on its General block: "purchase". Original behaviour (OB-100): MainPurchaseFile is read only
/// when the PurchaseInfo block is missing (from the block the file was on); with the block there, "purchase" is used.
/// </summary>
std::string MPPurchaseFileName(MCFitIniFile& file);

/// <summary>Plays the interface sample <paramref name="sampleId"/> (a click, a hover, a refusal); none without a sound system.</summary>
void PlayLogSound(uint32_t sampleId);

/// <summary>The string table index of the weight class of <paramref name="tonnage"/> (light .. assault).</summary>
uint32_t WeightClassString(float tonnage);

/// <summary>The string table index of the armor rating of <paramref name="armorTonnage"/>.</summary>
uint32_t ArmorClassString(float armorTonnage);

/// <summary>The string table index of the structure word for <paramref name="internals"/> internal structure points.</summary>
uint32_t InternalClassString(int32_t internals);

/// <summary>
/// Readies a description for the info box as the original did when it showed it: its fourth character (a colour
/// code's digit) becomes '9' (a shorter one is left).
/// </summary>
void PrepareInfoDescription(std::string& description);

/// <summary>The form of master component <paramref name="masterID"/>.</summary>
MCComponentForm ComponentForm(uint8_t masterID);

/// <summary>An energy, ballistic or missile weapon.</summary>
bool IsWeapon(MCComponentForm form);

/// <summary>A sensor, ECM or probe.</summary>
bool IsEquipment(MCComponentForm form);

/// <summary>A weapon with its own ammo (ballistic and missile): moving it moves an ammo item too.</summary>
bool UsesAmmo(uint8_t masterID);

#include "stdafx.h"
#include "mission/MCDifficultySettings.h"
#include "main/MCPrefs.h"
#include "mission/MCScenarioReading.h"

MCDifficultySettings DifficultySettings;

namespace
{
    /// <summary>Reads the two percentages of <paramref name="name"/>; a missing entry is fatal.</summary>
    void ReadPercentages(MCFitIniFile& file, std::string_view name, MCDifficultySettings::Percentages& values,
                         std::string_view message)
    {
        if (const MCFitResult<uint32_t> result = file.ReadArray<int32_t>(name, values); !result)
        {
            Fatal(std::to_underlying(result.error()), message);
        }
    }

    /// <summary><paramref name="value"/> scaled by the percentage of <paramref name="difficulty"/>.</summary>
    float Scale(float value, const MCDifficultySettings::Percentages& percentages, int32_t difficulty)
    {
        if (difficulty == 0)
        {
            return static_cast<float>(static_cast<double>(percentages[0]) / 100.0 * value);
        }

        if (difficulty == 2)
        {
            return static_cast<float>(static_cast<double>(percentages[1]) / 100.0 * value);
        }

        return value;
    }
}

auto MCDifficultySettings::Load(MCFitIniFile& gameSystemFile) -> void
{
    RequireFitBlock(gameSystemFile, "DifficultySettings", "No Difficulty Settings in gameSys");
    ReadPercentages(gameSystemFile, "PlayerSkills", PlayerSkills,
                    "No Difficulty Settings for Player Skills in gameSys");
    ReadPercentages(gameSystemFile, "EnemySkills", EnemySkills, "No Difficulty Settings for Enemy Skills in gameSys");
    ReadPercentages(gameSystemFile, "PlayerWeapons", PlayerWeapons,
                    "No Difficulty Settings for Player Weapons in gameSys");
    ReadPercentages(gameSystemFile, "EnemyWeapons", EnemyWeapons,
                    "No Difficulty Settings for Enemy Weapons in gameSys");
    ReadPercentages(gameSystemFile, "SalvageChance", SalvageChance,
                    "Do Difficulty Settings for Salvage Chance in GameSys");
}

auto MCDifficultySettings::ApplySkill(float skill, bool player, int32_t difficulty) const -> float
{
    return Scale(skill, player ? PlayerSkills : EnemySkills, difficulty);
}

auto MCDifficultySettings::ApplyWeapon(float value, bool player, int32_t difficulty) const -> float
{
    value = Scale(value, player ? PlayerWeapons : EnemyWeapons, difficulty);

    // Round down to a quarter.
    const auto quarters = static_cast<int32_t>(value / 0.25);

    if (value != quarters * 0.25)
    {
        value = static_cast<float>(static_cast<int32_t>(value / 0.25) * 0.25);
    }

    return std::clamp(value, 0.0f, 255.0f);
}

auto MCDifficultySettings::Salvage(int32_t normal, int32_t difficulty) const -> int32_t
{
    if (difficulty == 0)
    {
        return SalvageChance[0];
    }

    if (difficulty == 2)
    {
        return SalvageChance[1];
    }

    return normal;
}

auto ApplyDifficultySkill(float skill, int player) -> float
{
    return DifficultySettings.ApplySkill(skill, player != 0, GameDifficulty);
}

auto ApplyDifficultyWeapon(float value, int player) -> float
{
    return DifficultySettings.ApplyWeapon(value, player != 0, GameDifficulty);
}

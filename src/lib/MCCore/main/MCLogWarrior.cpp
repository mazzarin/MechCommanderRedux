#include "stdafx.h"
#include "main/MCLogWarrior.h"
#include "logistics/MCPurProfile.h"
#include "object/MCMoverGameSystem.h"

MCLogWarrior::MCLogWarrior() = default;

MCLogWarrior::~MCLogWarrior() = default;

auto MCLogWarrior::CalcRank() -> void
{
    // Evaluated in the x87's precision, as the original did.
    double weighted = 0.0;
    double totalWeight = 0.0;

    for (size_t skill = 0; skill < NumSkills; ++skill)
    {
        weighted = static_cast<double>(Skills[skill]) * SkillWeightings[skill] + weighted;
        totalWeight = totalWeight + SkillWeightings[skill];
    }

    for (int32_t level = 0; level < 4; ++level)
    {
        if (weighted / totalWeight < WarriorRankScale[level])
        {
            Rank = level;
            return;
        }
    }
}

auto MCLogWarrior::LoadDescription(int32_t descIndex) -> void
{
    if (descIndex < 0 || !Description.empty())
    {
        return;
    }

    Description = LoadDescriptionText(DescIndex);
}

auto MCLogWarrior::SetWounds(char wounds) -> void
{
    Wounds = static_cast<float>(wounds);
    Health = FullHealth - Wounds;

    if (Health <= 0.0f)
    {
        WarriorStatus = StatusKilled;
        Sold = true;
        Health = 0.0f;
    }
}

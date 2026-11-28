#include "stdafx.h"
#include "mission/MCScenarioObjectives.h"
#include "logistics/MCMissionNames.h"
#include "mission/MCScenarioReading.h"

MCObjectiveList::MCObjectiveList()
{
    for (MCScenarioObjective& slot : _Slots)
    {
        slot.Type = MCScenarioObjective::Unused;
        slot.Status = MCScenarioObjective::Unused;
    }
}

auto MCObjectiveList::Load(MCFitIniFile& file, uint32_t count) -> std::expected<void, std::string>
{
    // Issue #25: the count comes from the mission file; the original only asserted, and wrote the objectives past the
    // nine slots into the scenario's next members.
    if (count > static_cast<uint32_t>(MaxObjectives))
    {
        return std::unexpected(std::format("{} objectives, more than the {} a mission can have", count, MaxObjectives));
    }

    _Count = static_cast<int32_t>(count);

    for (int32_t i = 0; i < _Count; i++)
    {
        MCScenarioObjective& objective = _Slots[static_cast<size_t>(i)];
        objective = {};
        RequireFitBlock(file, std::format("Objective{}", i), " Could not find ObjectiveNumber Block ");
        objective.Name = RequireFit<std::string>(file, "Name", " Could not find Name in Objective Block ");
        objective.Type = RequireFit<uint32_t>(file, "Type", " Could not find Type in Objective Block ");
        objective.TimeLeft = RequireFit<float>(file, "TimeLeft", " Could not find TimeLeft in Objective Block ");
        objective.Status = RequireFit<uint32_t>(file, "Status", " Could not find Status in Objective Block");
        objective.Points = OptionalFit<int32_t>(file, "Points", 0);
        objective.Radius = OptionalFit<float>(file, "Radius", 0.0f);
        objective.Position = {-99.0f, -99.0f, -99.0f};
    }

    return {};
}

void MCObjectiveList::NameAsMenus(std::string_view briefingFile)
{
    for (int32_t i = 0; i < _Count; i++)
    {
        MCScenarioObjective& objective = _Slots[static_cast<size_t>(i)];
        objective.Name = MCMissionNames::Objective(briefingFile, i + 1, objective.Name);
    }
}

auto MCObjectiveList::SetStatus(int32_t number, uint32_t status) -> int32_t
{
    if (!IsValid(number))
    {
        return BadObjective;
    }

    (*this)[number].Status = status;
    return 0;
}

auto MCObjectiveList::Status(int32_t number) const -> uint32_t
{
    return IsValid(number) ? (*this)[number].Status : NoObjective;
}

auto MCObjectiveList::SetType(int32_t number, uint32_t type) -> int32_t
{
    if (!IsValid(number))
    {
        return BadObjective;
    }

    (*this)[number].Type = type;
    return 0;
}

auto MCObjectiveList::Type(int32_t number) const -> uint32_t
{
    return IsValid(number) ? (*this)[number].Type : NoObjective;
}

auto MCObjectiveList::SetPosition(int32_t number, float x, float y, float z) -> void
{
    if (IsValid(number))
    {
        (*this)[number].Position = {x, y, z};
    }
}

auto MCObjectiveList::SucceededPoints() const -> int32_t
{
    int32_t points = 0;

    for (const MCScenarioObjective& slot : _Slots)
    {
        if (slot.Status == MCScenarioObjective::Succeeded)
        {
            points += slot.Points;
        }
    }

    return points;
}

auto MCObjectiveList::ResourcePointsEarned(uint32_t scenarioResult, bool endedEarly) const -> int32_t
{
    if (scenarioResult <= 3)
    {
        return 0;
    }

    int32_t points = 0;

    for (const MCScenarioObjective& slot : _Slots)
    {
        if (slot.Status == MCScenarioObjective::Succeeded || endedEarly)
        {
            points += slot.Points;
        }
    }

    return points;
}

auto MCObjectiveList::AddTonnageBonus(int32_t unusedTonnage, int32_t tonsPerUnit, int32_t pointsPerUnit,
                                      const std::string& nameFormat) -> void
{
    // The original's search read one slot past the array when all nine were in use.
    const auto unused = std::ranges::find(_Slots, MCScenarioObjective::Unused, &MCScenarioObjective::Status);
    Assert(unused != _Slots.end(), MaxObjectives, " Too Many objectives in use ");

    MCScenarioObjective& bonus = *unused;
    bonus.Status = MCScenarioObjective::Succeeded;
    bonus.Type = MCScenarioObjective::TonnageBonus;
    bonus.Points = unusedTonnage / tonsPerUnit * pointsPerUnit;
    // The format is a string resource (a printf format with the tonnage).
    bonus.Name = MCFormatPrintf(nameFormat.c_str(), unusedTonnage);
}

#include "stdafx.h"
#include "logistics/MCPurPilotList.h"
#include "object/MCMoverGameSystem.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGamePaths.h"
#include "logistics/MCPurProfile.h"
#include "main/MCLogistics.h"

auto MCPurPilotList::Clear() -> void
{
    Pilots.clear();
}

auto MCPurPilotList::AddPilot(std::string_view fileName, int32_t status) -> void
{
    MCFitIniFile file;
    OpenProfile(file, WarriorPath, fileName, true, " could not open scenario file ");

    auto data = std::make_shared<MCPurPilotData>();
    data->FileName = fileName;
    int32_t result = file.SeekBlock("General");
    Assert(result == 0, result, " could not find general block in pilot file ");
    result = file.ReadIdLong("NameIndex", data->NameIndex);
    Assert(result == 0, result, "could not read NameIndex in pilot profile");
    data->Callsign = ReadProfileString(file, "Callsign", 0x14, " could not read callsign in pilot file ");
    data->PilotAudio = ReadProfileString(file, "pilotAudio", 0xff, " Could not find pilotAudio in General Block ");
    file.ReadIdLong("DescIndex", data->DescIndex);
    data->LoadDescription(data->DescIndex);
    result = file.SeekBlock("Skills");
    Assert(result == 0, result, " could not find skills block in pilot file ");
    result = file.ReadIdChar("Piloting", data->Piloting);
    Assert(result == 0, result, " could not read Piloting in pilot file ");
    result = file.ReadIdChar("Gunnery", data->Gunnery);
    Assert(result == 0, result, " could not read Gunnery in pilot file ");
    result = file.ReadIdChar("Jumping", data->Jumping);
    Assert(result == 0, result, " could not read Jumping in pilot file ");
    result = file.ReadIdChar("Sensors", data->Sensors);
    Assert(result == 0, result, " could not read Sensors in pilot file ");
    result = file.SeekBlock("Status");
    Assert(result == 0, result, " could not find status block in pilot file ");
    char wounds = 0;
    result = file.ReadIdChar("Wounds", wounds);
    Assert(result == 0, result, " could not read Wounds in pilot file ");
    data->Health = static_cast<char>(6 - wounds);
    data->Rank = 0;
    data->CalcRank();
    data->Cost = GlobalLog()->PilotCosts[data->Rank];
    data->Status = status;
    file.Close();
    Insert(std::move(data));
}

auto MCPurPilotList::Insert(std::shared_ptr<MCPurPilotData> pilot) -> void
{
    // Past the lower ranks, then past the smaller callsigns. Original behaviour (OB-165): the callsign walk doesn't
    // stop at a higher rank.
    auto place = Pilots.begin();

    while (place != Pilots.end() && (*place)->Rank < pilot->Rank)
    {
        ++place;
    }

    while (place != Pilots.end() && (*place)->Callsign < pilot->Callsign)
    {
        ++place;
    }

    Pilots.insert(place, std::move(pilot));
}

auto MCPurPilotList::SetPilotStatus(int32_t pilotId, int32_t status) -> void
{
    for (const std::shared_ptr<MCPurPilotData>& pilot : Pilots)
    {
        if (pilot->DescIndex == pilotId)
        {
            pilot->Status = status;
            return;
        }
    }
}

auto MCPurPilotList::GetPilotInfo(int32_t index, std::shared_ptr<MCPurPilotData>& pilot) -> int32_t
{
    if (index >= GetPilotCount())
    {
        return -1;
    }

    // A negative index is the first pilot (none in an empty list), as the original's walk gave.
    pilot = Pilots.empty() ? nullptr : Pilots[static_cast<size_t>(std::max(index, 0))];
    return 0;
}

auto MCPurPilotList::GetVisiblePilotCount() const -> int32_t
{
    return static_cast<int32_t>(std::ranges::count_if(Pilots, [](const std::shared_ptr<MCPurPilotData>& pilot)
                                                      { return pilot->Status == MCPurPilotData::ForHire; }));
}

auto MCPurPilotData::CalcRank() -> void
{
    // The skills weighted (piloting, jumping, sensors, gunnery); the rank is the first scale entry above it.
    double weighted =
        (static_cast<double>(Gunnery) * SkillWeightings[3] + static_cast<double>(Sensors) * SkillWeightings[2] +
         static_cast<double>(Jumping) * SkillWeightings[1] + static_cast<double>(Piloting) * SkillWeightings[0]) /
        (static_cast<double>(SkillWeightings[3]) + SkillWeightings[2] + SkillWeightings[1] + SkillWeightings[0]);

    for (int32_t level = 0; level < 4; ++level)
    {
        if (weighted < WarriorRankScale[level])
        {
            Rank = level;
            return;
        }
    }
}

auto MCPurPilotData::LoadDescription(int32_t descIndex) -> void
{
    if (descIndex > -1 && Description.empty())
    {
        Description = LoadDescriptionText(DescIndex);
    }
}

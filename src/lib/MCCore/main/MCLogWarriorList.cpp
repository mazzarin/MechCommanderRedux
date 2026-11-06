#include "stdafx.h"
#include "main/MCLogWarriorList.h"
#include "lib/MCPacketFile.h"
#include "logistics/MCMissionLogisticsBridge.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "main/MCLogisticsShared.h"

MCLogWarriorList::~MCLogWarriorList()
{
    Clear();
}

auto MCLogWarriorList::Position(int32_t index) const -> size_t
{
    return index < 0 ? 0 : std::min(static_cast<size_t>(index), Warriors.size());
}

auto MCLogWarriorList::FindById(uint32_t id) const -> std::shared_ptr<const MCLogWarrior>
{
    const auto found = std::ranges::find_if(Warriors, [&](const std::shared_ptr<MCLogWarrior>& warrior)
                                            { return static_cast<uint32_t>(warrior->Id) == id; });
    return found != Warriors.end() ? *found : nullptr;
}

auto MCLogWarriorList::Clear() -> void
{
    while (!Warriors.empty())
    {
        RemoveWarriorAtIndex(0);
    }
}

auto MCLogWarriorList::AddWarrior(std::string_view fileName, bool sorted) -> int32_t
{
    MCFitIniFile file;
    const int32_t result = file.Open(GamePath(WarriorPath, fileName, ".fit"));
    Assert(result == 0, static_cast<uint32_t>(result), " could not open scenario file ");
    return AddWarrior(file, sorted);
}

namespace
{
    /// <summary>The row the next pilot read takes. Never reset, as in the original.</summary>
    int32_t NextPilotRow = 0;

    /// <summary>The skill keys of the Skills, OriginalSkills and StartingSkills blocks, in skill order.</summary>
    constexpr std::array<std::string_view, MCLogWarrior::NumSkills> SkillNames = {"Piloting", "Jumping", "Sensors",
                                                                                  "Gunnery"};

    /// <summary>
    /// Reads the parts of a pilot profile both <see cref="MCLogWarriorList::ReplaceWarrior"/> and
    /// <see cref="MCLogWarriorList::AddWarrior"/> read: the status flags from the current block, the personality, the
    /// skills (current, original, starting, points) and the rank.
    /// </summary>
    void ReadWarriorSkills(MCFitIniFile& file, MCLogWarrior& warrior)
    {
        warrior.Assigned = file.Read<bool>("Assigned").value_or(false);
        warrior.Sold = file.Read<bool>("Sold").value_or(false);
        warrior.NotMineYet = file.Read<bool>("NotMineYet").value_or(false);
        warrior.Ejected = file.Read<bool>("Ejected").value_or(false);

        int32_t result = file.SeekBlock("PersonalityTraits");
        Assert(result == 0, 0, " Could not find PersonalityTraits Block ");
        warrior.Personality[0] =
            ReadRequired<char>(file, "Professionalism", " Could not find professionalism in PersonalityTraits Block ");
        warrior.Personality[1] =
            ReadRequired<char>(file, "Decorum", " Could not find decorum in PersonalityTraits Block ");
        warrior.Personality[2] =
            ReadRequired<char>(file, "Aggressiveness", " Could not find aggressiveness in PersonalityTraits Block ");
        warrior.Personality[3] =
            ReadRequired<char>(file, "Courage", " Could not find courage in PersonalityTraits Block ");

        result = file.SeekBlock("Skills");
        Assert(result == 0, 0, " Could not find Skills Block ");

        for (size_t skill = 0; skill < MCLogWarrior::NumSkills; ++skill)
        {
            warrior.Skills[skill] = ReadRequired<char>(
                file, SkillNames[skill], std::format(" Could not find {} in Skills Block ", SkillNames[skill]));
        }

        // The original and starting skills default to the current ones.
        const bool haveOriginal = file.SeekBlock("OriginalSkills") == 0;

        for (size_t skill = 0; skill < MCLogWarrior::NumSkills; ++skill)
        {
            warrior.OriginalSkills[skill] = haveOriginal
                                                ? file.Read<char>(SkillNames[skill]).value_or(warrior.Skills[skill])
                                                : warrior.Skills[skill];
        }

        const bool haveStarting = file.SeekBlock("StartingSkills") == 0;

        for (size_t skill = 0; skill < MCLogWarrior::NumSkills; ++skill)
        {
            warrior.StartingSkills[skill] = haveStarting
                                                ? file.Read<char>(SkillNames[skill]).value_or(warrior.Skills[skill])
                                                : warrior.Skills[skill];
        }

        const bool havePoints = file.SeekBlock("SkillPoints") == 0;

        for (size_t skill = 0; skill < MCLogWarrior::NumSkills; ++skill)
        {
            warrior.SkillPoints[skill] = havePoints ? file.Read<float>(SkillNames[skill]).value_or(0.0f) : 0.0f;
        }

        warrior.CalcRank();
    }

    /// <summary>Reads the Status block's Wounds into <paramref name="warrior"/>.</summary>
    void ReadWounds(MCFitIniFile& file, MCLogWarrior& warrior)
    {
        const int32_t result = file.SeekBlock("Status");
        Assert(result == 0, 0, " Could not find Status Block ");
        warrior.SetWounds(ReadRequired<char>(file, "Wounds", " Could not find Wounds in Skills Block "));
    }

    /// <summary>Opens the FIT file in packet <paramref name="packet"/> of a save.</summary>
    void OpenWarriorPacket(MCPacketFile& file, int32_t packet, MCFitIniFile& profile)
    {
        int32_t result = file.SeekPacket(packet);
        Assert(result == 0, 0, " Unable to find warrior file ");
        result = profile.Open(file, static_cast<uint32_t>(file.GetPacketSize()));
        Assert(result == 0, 0, " Unable to open warrior file ");
    }
}

auto MCLogWarriorList::ReplaceWarrior(MCPacketFile& file, int32_t packet) -> int32_t
{
    MCFitIniFile profile;
    OpenWarriorPacket(file, packet, profile);
    const int32_t result = profile.SeekBlock("General");
    Assert(result == 0, static_cast<uint32_t>(result), " Bad Saved pilot file ");
    const std::string callsign =
        ReadRequiredText(profile, "Callsign", 0xff, " Could not find CallSign in General Block ");

    // The warrior with that callsign takes the saved state.
    const auto found = std::ranges::find_if(Warriors, [&](const std::shared_ptr<MCLogWarrior>& warrior)
                                            { return warrior->Callsign == callsign; });

    if (found == Warriors.end())
    {
        return 5;
    }

    MCLogWarrior& warrior = **found;
    warrior.Id = GlobalLog()->NextWarriorID++;
    ReadWarriorSkills(profile, warrior);
    warrior.Deployed = false;
    ReadWounds(profile, warrior);
    return 0;
}

auto MCLogWarriorList::AddWarrior(MCPacketFile& file, int32_t packet, bool sorted) -> int32_t
{
    MCFitIniFile profile;
    OpenWarriorPacket(file, packet, profile);
    return AddWarrior(profile, sorted);
}

auto MCLogWarriorList::AddWarrior(MCFitIniFile& file, bool sorted) -> int32_t
{
    auto warrior = std::make_shared<MCLogWarrior>();
    warrior->Id = GlobalLog()->NextWarriorID++;

    // OB-089 (fixed): a profile without a [General] block was read as a saved pilot list (raw record images) into a
    // warrior that was never added; nothing in MCX.EXE writes such a file.
    if (file.SeekBlock("General") != 0)
    {
        Fatal(BLOCK_NOT_FOUND, " Pilot profile has no General Block ", file.GetFilename());
    }

    warrior->Name = ReadRequiredText(file, "Name", 0xff, " Could not find Name in General Block ");
    warrior->NameIndex = ReadRequired<int32_t>(file, "NameIndex", " Could not find NameIndex in Pilot General Block ");
    warrior->Callsign = ReadRequiredText(file, "Callsign", 0xff, " Could not find NameIndex in Pilot General Block ");
    warrior->Picture = ReadRequiredText(file, "Picture", 0xff, " Could not find Picture in Pilot General Block ");
    warrior->PilotVideo =
        ReadRequiredText(file, "pilotVideo", 0xff, " Could not find Pilotvideo in Pilot General Block ");
    warrior->PilotAudio =
        ReadRequiredText(file, "pilotAudio", 0xff, " Could not find Pilotaudio in Pilot General Block ");
    warrior->Brain = ReadRequiredText(file, "Brain", 0xff, " Could not find Brain in Pilot General Block ");

    warrior->DescIndex = -1;
    ReadEntry(file, "DescIndex", warrior->DescIndex);
    warrior->LoadDescription(warrior->DescIndex);
    warrior->PaintScheme = ReadRequired<int32_t>(file, "paintScheme", " Could not find paintScheme in General Block ");
    ReadWarriorSkills(file, *warrior);

    if (file.SeekBlock("Affinities") == 0)
    {
        warrior->MechClass = ReadRequired<char>(file, "MechClass", " Could not find MechClass in Affinities Block ");
        warrior->MechType = ReadRequired<char>(file, "MechType", " Could not find MechType in Affinities Block ");
        warrior->WeaponClass =
            ReadRequired<char>(file, "WeaponClass", " Could not find WeaponClass in Affinities Block ");
        const MCFitResult<uint32_t> types = file.ReadArray("WeaponTypes", std::span<uint8_t>(warrior->WeaponTypes));
        Assert(types.has_value(), 0, " Could not find WeaponTypes in Affinities Block ");
    }

    warrior->WarriorStatus = 0;
    ReadWounds(file, *warrior);
    warrior->DropLance = -1;
    warrior->DropSlot = -1;

    // A profile read from its own file is known by the file's base name (one read from a packet keeps none).
    if (!file.IsChild())
    {
        warrior->FileName = LogFileBaseName(file.GetFilename(), 11);
    }

    const std::shared_ptr<MCLogWarrior> added = warrior;
    AddWarrior(std::move(warrior), sorted);
    // The pilot takes the next row, as its inventory block's init did in the original; the inventory screen numbers
    // the rows again when it shows them.
    added->ListIndex = NextPilotRow++;
    return 0;
}

auto MCLogWarriorList::AddWarrior(std::shared_ptr<MCLogWarrior> warrior, bool sorted) -> int32_t
{
    size_t position = 0;

    if (sorted)
    {
        // By rank, and within a rank by callsign.
        while (position < Warriors.size())
        {
            if (warrior->Rank <= Warriors[position]->Rank)
            {
                while (position < Warriors.size() && Warriors[position]->Callsign < warrior->Callsign &&
                       Warriors[position]->Rank == warrior->Rank)
                {
                    ++position;
                }

                break;
            }

            ++position;
        }
    }

    Warriors.insert(Warriors.begin() + static_cast<ptrdiff_t>(position), std::move(warrior));
    return 0;
}

auto MCLogWarriorList::ExtractWarrior(int32_t index) -> std::shared_ptr<MCLogWarrior>
{
    const size_t position = Position(index);

    if (index >= GetWarriorCount() || position >= Warriors.size())
    {
        return nullptr;
    }

    std::shared_ptr<MCLogWarrior> warrior = std::move(Warriors[position]);
    Warriors.erase(Warriors.begin() + static_cast<ptrdiff_t>(position));
    return warrior;
}

auto MCLogWarriorList::Heal(int32_t amount) -> void
{
    for (const std::shared_ptr<MCLogWarrior>& warrior : Warriors)
    {
        if (warrior->WarriorStatus == MCLogWarrior::StatusKilled || warrior->Sold)
        {
            continue;
        }

        if (warrior->Wounds < static_cast<float>(amount))
        {
            warrior->Wounds = 0.0f;
            warrior->Health = MCLogWarrior::FullHealth;
        }
        else
        {
            warrior->Wounds -= static_cast<float>(amount);
            warrior->Health = MCLogWarrior::FullHealth - warrior->Wounds;
        }
    }
}

auto MCLogWarriorList::RemoveWarriorAtIndex(int32_t index) -> int32_t
{
    const size_t position = Position(index);

    if (position >= Warriors.size())
    {
        return -1;
    }

    Warriors.erase(Warriors.begin() + static_cast<ptrdiff_t>(position));
    return 0;
}

auto MCLogWarriorList::RemoveWarrior(uint8_t id) -> int32_t
{
    const auto found = std::ranges::find_if(Warriors, [&](const std::shared_ptr<MCLogWarrior>& warrior)
                                            { return static_cast<uint32_t>(warrior->Id) == id; });

    if (found == Warriors.end())
    {
        return -1;
    }

    Warriors.erase(found);
    return 0;
}

auto MCLogWarriorList::GetWarriorProfile(uint32_t id, std::string& profile) const -> int32_t
{
    const std::shared_ptr<const MCLogWarrior> warrior = FindById(id);

    if (warrior == nullptr)
    {
        return -1;
    }

    profile = warrior->FileName;
    return 0;
}

auto MCLogWarriorList::GetWarriorBrain(uint32_t id, std::string& brain) const -> int32_t
{
    const std::shared_ptr<const MCLogWarrior> warrior = FindById(id);

    if (warrior == nullptr)
    {
        return -1;
    }

    brain = warrior->Brain;
    return 0;
}

auto MCLogWarriorList::GetID(int32_t index) const -> int32_t
{
    const size_t position = Position(index);
    return index < GetWarriorCount() && position < Warriors.size() ? Warriors[position]->Id : -1;
}

auto MCLogWarriorList::SaveWarriorText(std::string_view fileName, int32_t index) const -> int32_t
{
    std::shared_ptr<MCLogWarrior> warrior;

    if (GetWarriorInfo(index, warrior) != 0)
    {
        return -1;
    }

    return MCMissionLogisticsBridge::LogisticsWarriorProfileWriter(fileName, *warrior);
}

auto MCLogWarriorList::GetWarriorInfo(int32_t index, std::shared_ptr<MCLogWarrior>& warrior) const -> int32_t
{
    const size_t position = Position(index);
    warrior = index < GetWarriorCount() && position < Warriors.size() ? Warriors[position] : nullptr;
    return warrior != nullptr ? 0 : -1;
}

auto MCLogWarriorList::GetWarriorIndex(std::shared_ptr<const MCLogWarrior> warrior) const -> int32_t
{
    const auto found =
        std::ranges::find_if(Warriors, [&](const std::shared_ptr<MCLogWarrior>& entry) { return entry == warrior; });
    return found != Warriors.end() ? static_cast<int32_t>(found - Warriors.begin()) : -1;
}

auto MCLogWarriorList::Exists(std::string_view callsign) const -> bool
{
    return std::ranges::any_of(Warriors, [&](const std::shared_ptr<MCLogWarrior>& warrior)
                               { return warrior->Callsign == callsign; });
}

auto MCLogWarriorList::SetDeployed(int32_t index, bool deployed) -> void
{
    if (index >= 0 && index < GetWarriorCount())
    {
        Warriors[static_cast<size_t>(index)]->Deployed = deployed;
    }
}

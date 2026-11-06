#pragma once

// Original source: mcx\logistics.cpp (LogWarrior).

/// <summary>A MechWarrior in logistics: names, portrait, skills, wounds and status.</summary>
/// <remarks>Original source: <c>logistics.cpp</c>, 300 (0x12c) bytes.</remarks>
class MCLogWarrior
{
public:
    /// <summary>The skills: piloting, jumping, sensors, gunnery.</summary>
    static constexpr size_t NumSkills = 4;
    /// <summary>The <see cref="WarriorStatus"/> of a killed pilot.</summary>
    static constexpr int32_t StatusKilled = 4;
    /// <summary>The health of an unhurt pilot (6 minus the wounds).</summary>
    static constexpr float FullHealth = 6.0f;

    MCLogWarrior();
    ~MCLogWarrior();
    MCLogWarrior(const MCLogWarrior&) = delete;
    MCLogWarrior& operator=(const MCLogWarrior&) = delete;

    /// <summary>The rank from the skills, weighted by <c>SkillWeightings</c> and cut by <c>WarriorRankScale</c>.</summary>
    void CalcRank();

    /// <summary>Loads the description of <see cref="DescIndex"/> (once; none for a negative <paramref name="descIndex"/>).</summary>
    void LoadDescription(int32_t descIndex);

    /// <summary>
    /// Sets the wounds and health from the profile's Wounds; no health left means killed (and sold).
    /// </summary>
    void SetWounds(char wounds);

    /// <summary>The profile file's base name (at most 11 characters; empty for one read from a save's packet).</summary>
    std::string FileName;
    std::string Name;
    /// <summary>A unique id (<see cref="MCLogistics::NextWarriorID"/>).</summary>
    int32_t Id = 0;
    std::string Callsign;
    std::string Picture;
    std::string PilotVideo;
    std::string PilotAudio;
    /// <summary>The ABL brain file.</summary>
    std::string Brain;
    int32_t PaintScheme = 0;
    /// <summary>0 green .. 3 elite.</summary>
    int32_t Rank = 0;
    int32_t NameIndex = 0;
    int32_t DescIndex = 0;
    /// <summary>The description text (empty when there is none).</summary>
    std::string Description;
    /// <summary>Professionalism, decorum, aggressiveness, courage.</summary>
    std::array<char, 4> Personality{};
    /// <summary>Piloting, jumping, sensors, gunnery.</summary>
    std::array<char, NumSkills> Skills{};
    std::array<char, NumSkills> OriginalSkills{};
    std::array<char, NumSkills> StartingSkills{};
    /// <summary>Skill points earned towards the next level of each skill.</summary>
    std::array<float, NumSkills> SkillPoints{};
    char MechClass = 0;
    char MechType = 0;
    char WeaponClass = 0;
    std::array<uint8_t, 2> WeaponTypes{};
    float Wounds = 0;
    /// <summary>6 minus the wounds (0 = dead).</summary>
    float Health = 0;
    /// <summary><see cref="StatusKilled"/> = killed.</summary>
    int32_t WarriorStatus = 0;
    /// <summary>The lance of the drop slot the pilot's mech is in (-1 = none).</summary>
    int32_t DropLance = 0;
    /// <summary>The slot in <see cref="DropLance"/> (-1 = none).</summary>
    int32_t DropSlot = 0;
    bool Assigned = false;
    /// <summary>Set while the pilot's mech is in a drop slot.</summary>
    bool Deployed = false;
    bool Sold = false;
    bool NotMineYet = false;
    bool Ejected = false;
    /// <summary>
    /// The row of the pilot list the pilot is shown at (what <see cref="MCLogistics::SetPilot"/> and
    /// <see cref="MCLogistics::ShiftPilots"/> take).
    /// </summary>
    int32_t ListIndex = 0;
};

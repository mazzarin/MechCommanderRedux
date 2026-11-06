#pragma once

/// <summary>A MechWarrior for hire, read from <c>&lt;WarriorPath&gt;&lt;name&gt;.fit</c>.</summary>
/// <remarks>Original source: <c>logistics\purchase.cpp</c> (<c>PurPilotData</c>).</remarks>
class MCPurPilotData
{
public:
    /// <summary>The pilot is for hire (shown in the store). The statuses are the purchase files' Status values.</summary>
    static constexpr int32_t ForHire = 0;
    /// <summary>Hired, and alive.</summary>
    static constexpr int32_t Hired = 1;
    /// <summary>Hired, and killed.</summary>
    static constexpr int32_t Killed = 2;
    /// <summary>Hired, then sold back.</summary>
    static constexpr int32_t SoldBack = 3;
    /// <summary>Taken off the store by a mission's purchase file (status 0 there puts the pilot back).</summary>
    static constexpr int32_t OffShop = 4;

    /// <summary>Sets <see cref="Rank"/> from the weighted skills (<c>SkillWeightings</c>, <c>WarriorRankScale</c>).</summary>
    void CalcRank();

    /// <summary>Loads the description into <see cref="Description"/> once, when <paramref name="descIndex"/> is one.</summary>
    void LoadDescription(int32_t descIndex);

    /// <summary>The profile's file name.</summary>
    std::string FileName;
    /// <summary>The callsign (20 characters at most).</summary>
    std::string Callsign;
    /// <summary>The pilot's speech sample set (pilotAudio).</summary>
    std::string PilotAudio;
    int32_t NameIndex = 0;
    char Piloting = 0;
    char Gunnery = 0;
    char Jumping = 0;
    char Sensors = 0;
    /// <summary>6 minus the file's Wounds; 0 once hired (<c>MCPilotPurchaseBlock</c> no longer draws the row).</summary>
    char Health = 0;
    int32_t Rank = 0;
    /// <summary>The hiring price: the logistics price of <see cref="Rank"/>.</summary>
    int32_t Cost = 0;
    /// <summary>The DescIndex, also the pilot's ID (<see cref="MCPurPilotList::SetPilotStatus"/>).</summary>
    int32_t DescIndex = -1;
    /// <summary>The description text; empty for none.</summary>
    std::string Description;
    /// <summary><see cref="ForHire"/> .. <see cref="OffShop"/>.</summary>
    int32_t Status = ForHire;
};

/// <summary>The pilots for hire, sorted by rank, then callsign.</summary>
/// <remarks>Original source: <c>logistics\purchase.cpp</c> (<c>PurPilotList</c>, a linked list).</remarks>
class MCPurPilotList
{
public:
    /// <summary>Frees every pilot.</summary>
    void Clear();

    /// <summary>Reads pilot <paramref name="fileName"/> with <paramref name="status"/> and inserts it.</summary>
    void AddPilot(std::string_view fileName, int32_t status);

    /// <summary>Inserts <paramref name="pilot"/>: after the pilots of a lower rank, then after those with a smaller callsign.</summary>
    void Insert(std::shared_ptr<MCPurPilotData> pilot);

    /// <summary>Sets the <see cref="MCPurPilotData::Status"/> of the pilot whose ID is <paramref name="pilotId"/>.</summary>
    void SetPilotStatus(int32_t pilotId, int32_t status);

    /// <returns>0, or -1 past the end.</returns>
    int32_t GetPilotInfo(int32_t index, std::shared_ptr<MCPurPilotData>& pilot);

    int32_t GetPilotCount() const { return static_cast<int32_t>(Pilots.size()); }

    /// <summary>How many pilots are still for hire.</summary>
    int32_t GetVisiblePilotCount() const;

    /// <summary>The pilots in order.</summary>
    std::vector<std::shared_ptr<MCPurPilotData>> Pilots;
};

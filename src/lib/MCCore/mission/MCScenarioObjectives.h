#pragma once

class MCFitIniFile;

/// <summary>A scenario objective (<c>Objective%d</c> in the scenario FIT).</summary>
/// <remarks>
/// The type and status are the scenario script's integers (ABL sets and reads them), so they stay numbers with names
/// for the values the game gives a meaning.
/// </remarks>
struct MCScenarioObjective
{
    /// <summary>Type of an objective the player must meet.</summary>
    static constexpr uint32_t Primary = 0;
    /// <summary>Type of an optional objective.</summary>
    static constexpr uint32_t Secondary = 1;
    /// <summary>Type of the unused-tonnage bonus <see cref="MCObjectiveList::AddTonnageBonus"/> adds.</summary>
    static constexpr uint32_t TonnageBonus = 3;
    /// <summary>Status of an objective not decided yet.</summary>
    static constexpr uint32_t Pending = 0;
    /// <summary>Status of an objective met.</summary>
    static constexpr uint32_t Succeeded = 1;
    /// <summary>Status of an objective failed.</summary>
    static constexpr uint32_t Failed = 2;
    /// <summary>Type and status of a slot past the scenario's objectives.</summary>
    static constexpr uint32_t Unused = static_cast<uint32_t>(-9999);

    /// <summary><c>Name</c>: the text shown on the tactical map and the results screen.</summary>
    std::string Name;
    /// <summary><c>Type</c> (see the constants).</summary>
    uint32_t Type = 0;
    /// <summary><c>TimeLeft</c> in seconds; above 0 starts the objective's timer at the scenario's start.</summary>
    float TimeLeft = 0;
    /// <summary><c>Status</c> (see the constants).</summary>
    uint32_t Status = 0;
    /// <summary>Where the objective is (-99, -99, -99 until the script sets it).</summary>
    std::array<float, 3> Position{};
    /// <summary><c>Points</c>: resource points earned when it succeeds.</summary>
    int32_t Points = 0;
    /// <summary><c>Radius</c>.</summary>
    float Radius = 0;
};

/// <summary>
/// The scenario's objectives: <see cref="Count"/> read from the scenario FIT, then unused slots up to
/// <see cref="MaxObjectives"/>. The scenario script reads and changes them by number.
/// </summary>
class MCObjectiveList
{
public:
    /// <summary>
    /// Kept limit: the slots there are. A scenario has at most 8 of its own, so the tonnage bonus always finds one
    /// left; the results screen counts the points of these slots.
    /// </summary>
    static constexpr int32_t MaxObjectives = 9;
    /// <summary>What a set function returns for a bad objective number (the original's code).</summary>
    static constexpr int32_t BadObjective = -0x550fff4;
    /// <summary>What a query returns for a bad objective number.</summary>
    static constexpr uint32_t NoObjective = 9999;

    /// <summary>No objectives: every slot unused.</summary>
    MCObjectiveList();

    /// <summary>
    /// Reads blocks <c>Objective0</c> .. <c>Objective(count-1)</c> of <paramref name="file"/>; the slots after them are
    /// unused. A missing block or required entry is fatal, as in the original.
    /// </summary>
    /// <returns>Nothing, or why the objectives can't be read: more than <see cref="MaxObjectives"/>.</returns>
    std::expected<void, std::string> Load(MCFitIniFile& file, uint32_t count);

    /// <summary>
    /// Port-only (the menus' names, ui/missions.json): each objective's name as the menus put it for the mission whose
    /// briefing is <paramref name="briefingFile"/> ("Destroy 12 of 12 Ordnance Trucks": "Destroy all 12 Ordnance
    /// Trucks"), so the tactical map and the results say what the briefing does. MCMissionNames::Objective.
    /// </summary>
    void NameAsMenus(std::string_view briefingFile);

    /// <summary>The scenario's own objectives (the bonus isn't counted).</summary>
    int32_t Count() const { return _Count; }

    /// <summary>Slot <paramref name="index"/> (0 .. <see cref="MaxObjectives"/> - 1).</summary>
    MCScenarioObjective& operator[](int32_t index) { return _Slots[static_cast<size_t>(index)]; }
    const MCScenarioObjective& operator[](int32_t index) const { return _Slots[static_cast<size_t>(index)]; }

    /// <summary>Sets objective <paramref name="number"/>'s status.</summary>
    /// <returns>0, or <see cref="BadObjective"/>.</returns>
    int32_t SetStatus(int32_t number, uint32_t status);

    /// <summary>Objective <paramref name="number"/>'s status, or <see cref="NoObjective"/>.</summary>
    uint32_t Status(int32_t number) const;

    /// <summary>Sets objective <paramref name="number"/>'s type.</summary>
    /// <returns>0, or <see cref="BadObjective"/>.</returns>
    int32_t SetType(int32_t number, uint32_t type);

    /// <summary>Objective <paramref name="number"/>'s type, or <see cref="NoObjective"/>.</summary>
    uint32_t Type(int32_t number) const;

    /// <summary>Moves objective <paramref name="number"/> (a bad number is ignored).</summary>
    void SetPosition(int32_t number, float x, float y, float z);

    /// <summary>Whether <paramref name="number"/> is one of the scenario's own objectives.</summary>
    bool IsValid(int32_t number) const { return number >= 0 && number < _Count; }

    /// <summary>The points of every slot whose objective succeeded (the bonus included).</summary>
    int32_t SucceededPoints() const;

    /// <summary>
    /// The resource points the scenario earned: none when it wasn't won (<paramref name="scenarioResult"/> 3 or
    /// less), else the succeeded objectives' points, or every slot's when the mission ended the scenario early.
    /// </summary>
    int32_t ResourcePointsEarned(uint32_t scenarioResult, bool endedEarly) const;

    /// <summary>
    /// Puts the unused-tonnage bonus in the first unused slot, succeeded: <paramref name="pointsPerUnit"/> points per
    /// <paramref name="tonsPerUnit"/> tons of <paramref name="unusedTonnage"/>, named from <paramref name="nameFormat"/>
    /// (a printf format taking the tonnage).
    /// </summary>
    void AddTonnageBonus(int32_t unusedTonnage, int32_t tonsPerUnit, int32_t pointsPerUnit,
                         const std::string& nameFormat);

private:
    /// <summary>The slots, always <see cref="MaxObjectives"/>.</summary>
    std::array<MCScenarioObjective, MaxObjectives> _Slots{};
    /// <summary>The scenario's own objectives.</summary>
    int32_t _Count = 0;
};

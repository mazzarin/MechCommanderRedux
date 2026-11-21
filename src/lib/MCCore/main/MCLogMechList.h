#pragma once

// Original source: mcx\logistics.cpp (LogMechList).

#include "main/MCLogMech.h"

class MCFitIniFile;
class MCPacketFile;

/// <summary>Each mech name index's place in the logistics mech order (one per mech name in the string table).</summary>
inline constexpr std::array<int32_t, 24> MechSort = {23, 19, 13, 10, 0, 3,  2, 6,  9,  8, 15, 14,
                                                     18, 20, 4,  16, 1, 12, 5, 11, 21, 7, 17, 22};

/// <summary>A list of mechs, by <see cref="MCLogMech::SortKey"/> (or by tonnage) when added sorted.</summary>
/// <remarks>
/// Original source: <c>logistics.cpp</c> (<c>LogMechList</c>, a linked list). A negative position reads the first
/// mech, as the original's walks from the head did.
/// </remarks>
class MCLogMechList
{
public:
    MCLogMechList() = default;
    ~MCLogMechList();
    MCLogMechList(const MCLogMechList&) = delete;
    MCLogMechList& operator=(const MCLogMechList&) = delete;

    /// <summary>Removes every mech (with its widgets).</summary>
    void Clear();

    /// <summary>The position of <paramref name="mech"/>, or -1.</summary>
    int32_t GetMechIndex(std::shared_ptr<const MCLogMech> mech) const;

    /// <summary>Adds the mech in profile <paramref name="fileName"/> under <c>ProfilePath</c>.</summary>
    std::shared_ptr<MCLogMech> AddMech(std::string_view fileName, bool required, bool sorted, bool widgets);

    /// <summary>Replaces the mech flown by the pilot that the save's packet <paramref name="packet"/> names with that packet's mech.</summary>
    /// <returns>0, or 5 when no mech has that pilot.</returns>
    int32_t ReplaceMech(MCPacketFile& file, int32_t packet);

    /// <summary>Adds the mech in packet <paramref name="packet"/> of a save (sorted, with widgets).</summary>
    std::shared_ptr<MCLogMech> AddMech(MCPacketFile& file, int32_t packet);

    /// <summary>
    /// Reads a mech from its profile and adds it (by sort key when <paramref name="sorted"/>, else first), with its
    /// repair, inventory and briefing widgets when <paramref name="widgets"/> is set.
    /// </summary>
    std::shared_ptr<MCLogMech> AddMech(MCFitIniFile& file, bool required, bool sorted, bool widgets);

    /// <summary>Puts <paramref name="mech"/> in (by tonnage when <paramref name="sorted"/>, else first).</summary>
    void AddMech(std::shared_ptr<MCLogMech> mech, bool sorted);

    /// <summary>Takes the mech at <paramref name="index"/> out of the list (null past the end).</summary>
    std::shared_ptr<MCLogMech> ExtractMech(int32_t index);

    /// <summary>Deletes the mech at <paramref name="index"/>.</summary>
    /// <returns>0, or -1 past the end.</returns>
    int32_t RemoveMech(int32_t index);

    /// <summary>Deletes <paramref name="mech"/>.</summary>
    /// <returns>0, or -1 when it isn't in the list.</returns>
    int32_t RemoveMech(const MCLogMech& mech);

    int32_t GetMechCount() const { return static_cast<int32_t>(Mechs.size()); }

    /// <summary>The pilot index of the mech at <paramref name="index"/>, or -1.</summary>
    int32_t GetMechPilotIndex(int32_t index) const;

    /// <summary>The mech at <paramref name="index"/> into <paramref name="mech"/> (null past the end).</summary>
    /// <returns>0, or -1 past the end.</returns>
    int32_t GetMechInfo(int32_t index, std::shared_ptr<MCLogMech>& mech) const;

    /// <summary>Writes the mech at <paramref name="index"/> as a profile text file.</summary>
    int32_t SaveMechText(std::string_view fileName, int32_t index) const;

    /// <summary>The mechs in list order.</summary>
    std::vector<std::shared_ptr<MCLogMech>> Mechs;
    /// <summary>The multiplayer player whose mechs these are (set by <see cref="MCLogistics::InitializeMultiplayer"/>).</summary>
    uint32_t PlayerID = 0;

private:
    /// <summary>Takes the mech at <paramref name="position"/> off the repair screen and deletes it.</summary>
    void Delete(size_t position);

    /// <summary>The position <paramref name="index"/> reads (a negative one the first), or the end.</summary>
    size_t Position(int32_t index) const;
};

/// <summary>Port-only: the sprite file every stock mission names (its "SpriteSystem" block's "SpriteFileName").</summary>
inline constexpr std::string_view MechSpriteFile = "sprites";

/// <summary>
/// Port-only (SPEC-memory.md's manifest: the players' part, worked out as each mech is picked): the HD pack's folders
/// of mech chassis <paramref name="chassis"/>'s tree, each part's first packet's (<c>&lt;part&gt;/&lt;mech&gt;/0</c>, as a
/// mission's hold list names them): its type's appearance (its packet in the object file) read from the sprite file
/// (<see cref="MechSpriteFile"/>) as a mission's load reads it (MCAppearanceTypeList), the 90-pixel parts' when Use90PixelSprite is on. None for a
/// chassis that isn't a mech's, or files that lack it.
/// </summary>
std::vector<std::filesystem::path> MechTreeFolders(uint32_t chassis);

#pragma once

// Port-only (SPEC-menus.md, "Models and views", stage 5): the campaign saves list, which LOAD GAME and SAVE GAME view
// (and the main menu's and the campaigns' CONTINUE will). The saves are the game's own, as today: SavePath's *.sav
// files, the user folder's first, two a mission written by the game (PortArthurBefore_Op%i_Mis%i as it begins,
// PortArthurAfter_Op%i_Mis%i as the next briefing opens; Cermak's in the expansion) and those the player names on SAVE
// GAME. Each is read as the list is refreshed: its campaign, mission and RP (its first packet), the mission's
// briefing's name for it (MCMissionNames) and when it was written (the file's time).

#include "lib/MCOptionalRef.h"

#include <SDL3/SDL_time.h>

class MCFitIniFile;

class MCSaves
{
public:
    /// <summary>The campaign a save plays (its [Planet] Setting: Port Arthur's, or Cermak's in the expansion).</summary>
    enum class MCCampaign : uint8_t
    {
        Original,
        Expansion,
    };

    /// <summary>When a save was written: as its mission began, as the next briefing opened, or by the player.</summary>
    enum class MCWhen : uint8_t
    {
        Before,
        After,
        Named,
    };

    struct MCSave
    {
        /// <summary>Its file's name, without .sav ("PortArthurBefore_Op2_Mis3", "Kaio, second try").</summary>
        std::string Name;
        /// <summary>Whether its settings could be read: one that can't is listed by its file's name alone.</summary>
        bool Read = false;
        MCCampaign Campaign = MCCampaign::Original;
        MCWhen When = MCWhen::Named;
        /// <summary>
        /// The mission it names (an after save names the one played, the one before the briefing it opens on): its
        /// operation and mission numbers, the operation's and mission's names and its scenario ("mis0203").
        /// </summary>
        int32_t Operation = 0;
        int32_t Mission = 0;
        std::string OperationName;
        std::string MissionName;
        std::string Scenario;
        uint32_t ResourcePoints = 0;
        /// <summary>When its file was last written; 0 if that can't be told.</summary>
        SDL_Time Saved = 0;
        bool InUserFolder = false;
        bool InGameFolder = false;
    };

    /// <summary>How a save went (<see cref="Write"/>).</summary>
    enum class MCWritten : uint8_t
    {
        Saved,
        DiskFull,
        Failed,
    };

    /// <summary>The game's list (read when a screen refreshes it).</summary>
    static MCSaves& Game();

    /// <summary>The saves on disk read again, newest first.</summary>
    void Refresh();

    /// <summary>Every save, newest first.</summary>
    const std::vector<MCSave>& All() const;

    /// <summary>The saves of <paramref name="campaign"/> (every campaign's without one), newest first.</summary>
    std::vector<std::reference_wrapper<const MCSave>> Of(std::optional<MCCampaign> campaign) const;

    /// <summary>The newest save that can be read, of <paramref name="campaign"/> or any; none without one.</summary>
    MCOptionalRef<const MCSave> Latest(std::optional<MCCampaign> campaign = std::nullopt) const;

    /// <summary>The save named <paramref name="name"/> (case ignored, as the file systems do); none if there's none.</summary>
    MCOptionalRef<const MCSave> Find(std::string_view name) const;

    /// <summary>The save named <paramref name="name"/> deleted (both folders' copies) and the list read again.</summary>
    /// <returns>Whether it's gone.</returns>
    bool Delete(std::string_view name);

    /// <summary>
    /// The campaign saved now as <paramref name="name"/> (over a save of that name, as the game writes it), and the list
    /// read again. A disk without room for a save is refused before anything is written.
    /// </summary>
    MCWritten Write(std::string_view name);

    /// <summary>The first default name with no save (the game's "savegame%03i", from 0, as today); none past 999.</summary>
    static std::optional<std::string> FreeName();

    /// <summary>Counts every change of the list, so a view can tell when to read it again.</summary>
    uint64_t Revision() const;

    /// <summary>The campaign's name ("Original campaign") and its short one, for a list's column ("Original").</summary>
    static std::string_view CampaignName(MCCampaign campaign);
    static std::string_view CampaignShortName(MCCampaign campaign);

    /// <summary>When a save was written, for a list: "Today 21:52", "Yesterday 23:05", "18 Nov 23:30", "18 Nov 2025".</summary>
    std::string DateText(SDL_Time saved) const;

    /// <summary>The same in a sentence: "today at 21:52", "yesterday at 23:05", "on 18 Nov at 23:30".</summary>
    std::string DateSentence(SDL_Time saved) const;

    /// <summary>For the tests: the clock the dates are told by (the system's, local time, without one).</summary>
    void SetClock(std::function<SDL_Time()> clock);

private:
    /// <summary>A campaign's master mission file's scenarios: each one's file and its operation and mission numbers.</summary>
    struct MCScenario
    {
        std::string File;
        int32_t Operation = 0;
        int32_t Mission = 0;
    };

    /// <summary>What a scenario's briefing names, read once a refresh.</summary>
    struct MCNamed
    {
        std::string Operation;
        std::string Mission;
    };

    /// <summary>The scenarios a campaign's master mission file lists; none when it can't be read.</summary>
    static std::vector<MCScenario> ReadScenarios(std::string_view master);
    void ReadSave(MCSave& save, std::span<const std::vector<MCScenario>> campaigns,
                  std::unordered_map<std::string, MCNamed>& named) const;
    SDL_Time Now() const;

    std::vector<MCSave> _Saves;
    uint64_t _Revision = 0;
    std::function<SDL_Time()> _Clock;
};

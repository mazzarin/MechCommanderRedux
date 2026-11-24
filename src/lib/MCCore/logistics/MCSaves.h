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
        /// <summary>
        /// Port-only (stage 7): the mission it opens on, its briefing's (the main menu's CONTINUE names it): an after
        /// save's next mission, else the one it names.
        /// </summary>
        int32_t OpensOperation = 0;
        int32_t OpensMission = 0;
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
        /// <summary>A name no file can have (<see cref="CanName"/>), refused before anything was written.</summary>
        BadName,
        DiskFull,
        Failed,
    };

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 7, SOLO MISSIONS): a single mission shipped, a .sol in SavePath (a save of a
    /// campaign of one mission, its master file "campaign" and its name): its briefing's name for its mission, and what
    /// it starts with.
    /// </summary>
    struct MCSolo
    {
        /// <summary>Its file's name, without .sol ("ACEHUNT"), as the game loads it.</summary>
        std::string Name;
        /// <summary>Whether its settings could be read: one that can't is listed by its file's name alone.</summary>
        bool Read = false;
        /// <summary>The mission by its briefing's name ("Destroy All Aces").</summary>
        std::string MissionName;
        /// <summary>Its mission's briefing file (the mission FIT's BriefingFile), as res/ui/missions.json names it.</summary>
        std::string Briefing;
        uint32_t ResourcePoints = 0;
        /// <summary>The tons that drop (its mission's MaxTonnage).</summary>
        int32_t Tons = 0;
        /// <summary>The units it starts with (its force's 'Mechs and vehicles); none for the shipped three.</summary>
        int32_t Units = 0;
    };

    /// <summary>The single missions on disk, read now, by their files' names.</summary>
    static std::vector<MCSolo> SoloMissions();

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
    /// The campaign saved now as <paramref name="name"/>, over a save of that name as the game writes its own, and the
    /// list read again. A name no file can have, and a disk without room for a save, are refused before anything is
    /// written, so the old save is kept. Past them the game writes the save's file afresh where it is, as 1998's did,
    /// so a save that fails once its file is begun loses the old one. 1998's OVERWRITE also deleted the save first, to
    /// clear a CD install's read-only flag; the port's saves are written in the user folder, so it doesn't.
    /// </summary>
    MCWritten Write(std::string_view name);

    /// <summary>The first default name with no save (the game's "savegame%03i", from 0, as today); none past 999.</summary>
    static std::optional<std::string> FreeName();

    /// <summary>
    /// The save the campaign would make now (SAVE GAME's "This save"): <paramref name="campaign"/>'s scenario
    /// <paramref name="mission"/> (the logistics' CurrentMission) with <paramref name="points"/> RP, named, unwritten.
    /// </summary>
    static MCSave Unsaved(MCCampaign campaign, int32_t mission, uint32_t points);

    /// <summary>
    /// Whether <paramref name="name"/> can name a save's file on every system the game runs on: not empty or blank, no
    /// folder, drive or control character in it, not ending in a dot or a space, and not a name Windows keeps for a
    /// device (CON, PRN, AUX, NUL, COM0-9, LPT0-9, with or without an extension).
    /// </summary>
    static bool CanName(std::string_view name);

    /// <summary>Counts every change of the list, so a view can tell when to read it again.</summary>
    uint64_t Revision() const;

    /// <summary>The campaign's name ("Original campaign") and its short one, for a list's column ("Original").</summary>
    static std::string_view CampaignName(MCCampaign campaign);
    static std::string_view CampaignShortName(MCCampaign campaign);

    /// <summary>Resource points as the menus write them, their thousands marked: "325,000".</summary>
    static std::string PointsText(uint32_t points);

    /// <summary>When a save was written, for a list: "Today 21:52", "Yesterday 23:05", "18 Nov 23:30", "18 Nov 2025".</summary>
    std::string DateText(SDL_Time saved) const;

    /// <summary>The same in a sentence: "today at 21:52", "yesterday at 23:05", "on 18 Nov at 23:30".</summary>
    std::string DateSentence(SDL_Time saved) const;

    /// <summary>For the tests: the clock the dates are told by (the system's, local time, without one).</summary>
    void SetClock(std::function<SDL_Time()> clock);

    /// <summary>For the tests: the bytes free on the saves' disk (the disk's own without one).</summary>
    void SetRoom(std::function<std::uintmax_t()> room) { _Room = std::move(room); }

    /// <summary>For the tests: how a save's file is deleted, whether it's gone (MCFileSystem::RemoveSave without one).</summary>
    void SetRemove(std::function<bool(std::string_view gamePath)> remove) { _Remove = std::move(remove); }

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

    /// <summary>The single mission <paramref name="name"/>'s .sol read (<see cref="SoloMissions"/>).</summary>
    static MCSolo ReadSolo(std::string name);

    /// <summary>The scenarios a campaign's master mission file lists; none when it can't be read.</summary>
    static std::vector<MCScenario> ReadScenarios(std::string_view master);
    /// <summary><paramref name="save"/>'s mission <paramref name="scenario"/>, named by its briefing (read once in <paramref name="named"/>).</summary>
    static void NameMission(MCSave& save, const MCScenario& scenario, std::unordered_map<std::string, MCNamed>& named);
    void ReadSave(MCSave& save, std::span<const std::vector<MCScenario>> campaigns,
                  std::unordered_map<std::string, MCNamed>& named) const;
    SDL_Time Now() const;
    /// <summary>Whether the saves' disk has room for a save.</summary>
    bool HasRoom() const;

    std::vector<MCSave> _Saves;
    uint64_t _Revision = 0;
    std::function<SDL_Time()> _Clock;
    std::function<std::uintmax_t()> _Room;
    std::function<bool(std::string_view gamePath)> _Remove;
};

#pragma once

#include "logistics/MCSaves.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// <summary>
/// Port-only (SPEC-menus.md's stage 7, "Models and views"): what the main menu and its submenus show, read from the game
/// as each shows, and what their choices do, apart from their screens (logistics/MCMainMenuScreen, the views): the logic
/// 1998's main menu (mainScreen.fit's buttons and their enabling, logistics/MCMainMenu.cpp's callbacks) and MULTIPLAYER
/// ACCESS (mpscreen.fit) held, so stage 9 takes their screens out without it.
/// </summary>
namespace MCMainMenuModel
{
    /// <summary>The main menu's choices that come and go, and the lines under them.</summary>
    struct MCChoices
    {
        /// <summary>RETURN TO GAME: between missions (a campaign's or a single mission's briefing behind the menu).</summary>
        bool Returns = false;
        /// <summary>Where it returns: "Original campaign · Operation 1, mission 1", "Solo mission".</summary>
        std::string ReturnLine;
        /// <summary>SAVE GAME: between a campaign's missions (1998's SAVE, not after a single mission's).</summary>
        bool Saves = false;
        /// <summary>The save CONTINUE plays, the latest (none: no CONTINUE; between missions RETURN TO GAME leads).</summary>
        std::string Latest;
        /// <summary>Where CONTINUE opens: "Original campaign · Operation 2, mission 3".</summary>
        std::string ContinueLine;
        /// <summary>SOLO MISSIONS: the game has single missions.</summary>
        bool Solo = false;
    };

    MCChoices Choices();

    /// <summary>A row of CAMPAIGNS or SOLO MISSIONS: its name and the line under it.</summary>
    struct MCRow
    {
        std::string Title;
        std::string Line;
    };

    /// <summary>
    /// CAMPAIGNS: a row a campaign (the original, the expansion; fan campaigns later, SPEC-campaigns.md) with where it
    /// stands, each one's latest save (none: not started), and the row picked as it opens (the latest save's campaign,
    /// else the original).
    /// </summary>
    struct MCCampaignList
    {
        std::vector<MCRow> Rows;
        std::vector<MCSaves::MCCampaign> Campaigns;
        std::vector<std::string> Latest;
        int Opens = 0;
    };

    MCCampaignList Campaigns();

    /// <summary>
    /// SOLO MISSIONS: a row a single mission the game ships, by its briefing's name with what it starts with under it
    /// ("Buy 245 tons with 325,000 RP"; one that can't be read by its file's name), and each one's file.
    /// </summary>
    struct MCSoloList
    {
        std::vector<MCRow> Rows;
        std::vector<std::string> Files;
    };

    MCSoloList SoloMissions();

    /// <summary>
    /// The menus' foot: the version running ("Redux HD 0.6.3"; none without an updater); UPDATE's word while an update
    /// to a release waits (UpdateButtonText: "Update to 0.7.1", or "Update" for the release running's content alone);
    /// and, while the HD art a program installed alone came for downloads or waits for its restart (MCUpdater::DueArt),
    /// its line (logistics/MCUpdateModel.h's DueArtLineText: "HD art: 120 of 340 MB", "New HD art is in"). That art
    /// alone names no release: a bare "Update" beside its line while it downloads (its page has the meter and STOP),
    /// RESTART in UPDATE's place once it's in where the game can start again, and the bare "Update" alone once it failed
    /// (its page says why, RETRY). A release due besides it keeps its UPDATE beside the art's line, and RESTART is then
    /// UPDATE's page's alone (the foot can't hold the version line, the art's line, RESTART and UPDATE).
    /// </summary>
    struct MCFoot
    {
        std::string Version;
        std::string Update;
        std::string Art;
        bool Restart = false;

        bool operator==(const MCFoot&) const = default;
    };

    MCFoot Foot();

    /// <summary>The foot's RESTART: the art in, the game starts again with it, as UPDATE's RESTART does.</summary>
    void RestartForArt();

    /// <summary>
    /// MULTIPLAYER entered (1998's MULTIPLAYER ACCESS): no single mission's game, the multiplayer game made and started
    /// (the main menu lets it go: <see cref="ToMainMenu"/>), logistics' state 1998's screen's.
    /// </summary>
    void EnterMultiplayer();

    /// <summary>MULTIPLAYER's network: whether there is one, and whether this build offers internet games.</summary>
    struct MCNetwork
    {
        bool Network = true;
        bool Internet = false;
    };

    MCNetwork LookForNetwork();

    /// <summary>INTERNET's line once the count comes: "37 players online", "1 player online".</summary>
    std::string OnlineText(int32_t online);

    /// <summary>For the tests: whether MULTIPLAYER finds a network (none: it looks).</summary>
    void SetNetworkForTests(std::optional<bool> network);

    /// <summary>
    /// The load of save <paramref name="name"/> into its briefing (CONTINUE, a campaign's CONTINUE, PLAY) begun, for its
    /// change's job; <see cref="Played"/> ends it once the screen is dark.
    /// </summary>
    MCSavedGameLoad Play(const std::string& name, bool campaign);

    /// <summary>The load of save <paramref name="name"/> ended with <paramref name="result"/>: false refused.</summary>
    bool Played(const std::string& name, int32_t result);

    /// <summary>A campaign started anew (1998's NEW CAMPAIGN and the expansion's): Betty's line, then its first briefing.</summary>
    void NewCampaign(MCSaves::MCCampaign campaign);

    /// <summary>RETURN TO GAME: the briefing the main menu was opened from.</summary>
    void ReturnToGame();

    /// <summary>ORIGINAL CINEMA: the intro movie, then the main menu again.</summary>
    void Cinema();

    /// <summary>
    /// 1998's CANCEL back to the main menu (a submenu's BACK and Escape): the multiplayer game let go, logistics' main
    /// screen set up again.
    /// </summary>
    void ToMainMenu();

    /// <summary>
    /// The words the model sets: INTERNET's line until the count comes (or when the service can't be reached), and a
    /// campaign with no save.
    /// </summary>
    inline constexpr std::string_view InternetLine = "Games on the internet";
    inline constexpr std::string_view NotStarted = "Not started";
}

// The 1998 main menu's callbacks (logistics\logmain.cpp), the model's since stage 9's step 2: new campaigns, the
// cinematics, back to the game, the multiplayer menu and the way out. The 1998 main screen's buttons, the flow and the
// network call them.

/// <summary>Starts a new original campaign.</summary>
void NewCampaign();

/// <summary>Starts a new Mech Commander Gold (MCX) campaign.</summary>
void NewMcxCampaign();

/// <summary>
/// Port-only: the campaign <paramref name="campaign"/> started from its start file <paramref name="startFile"/> on the
/// briefing screen at once, its palette left for the next frame to fade to: what <see cref="NewCampaign"/> and
/// <see cref="NewMcxCampaign"/> do in the fade from their click (the original's, then at the click).
/// <paramref name="firstPlanet"/> starts it on the first planet (the expansion's keeps the planet it had).
/// </summary>
void BeginCampaign(std::string_view campaign, std::string_view startFile, bool firstPlanet);

/// <summary>Opens the multiplayer menu.</summary>
void ShowMultiPlayer();

/// <summary>Replays the campaign's cinematics.</summary>
void ReplayCinema();

/// <summary>Returns from the menu to the logistics screen left (purchase, repair or briefing).</summary>
void ReturnToGame();

/// <summary>Ends the campaign (the player lost): the game quits.</summary>
void GameOverMan();

/// <summary>Back to the main menu (stops a modem or serial wait).</summary>
void Cancel();

/// <summary>Leaves the session (multiplayer) and goes back to the main menu; from a lobby launch, the game quits.</summary>
void DoExit();

/// <summary>Port: <see cref="DoExit"/>'s leaving a match: the multiplayer logistics taken down and the session left.</summary>
void LeaveMatch();

/// <summary>The exit button: asks for confirmation, then <see cref="DoExit"/>.</summary>
void CheckExit();

#pragma once

#include "gui/MCGuiRef.h"
#include "logistics/MCLogObject.h"
#include "logistics/MCLogOptionList.h"
#include "logistics/MCLogScrollTextObject.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCLogToolButton.h"
#include "logistics/MCMatchOptionsTab.h"
#include "logistics/MCPlayerNameObject.h"

class MCGuiEvent;
enum class MCFileCheck : uint8_t;
struct MCRoomPicks;

/// <summary>
/// The multiplayer session screen: the players and the two teams, each team's resource points and tech base, the
/// mission chosen by the host, and the load/start buttons. Port-only (the personal build): the game's room, which
/// hosting and joining open (the original's ready room is gone); it takes players until START.
/// </summary>
/// <remarks>Original source: <c>logistics\logsession.cpp</c> (<c>SessionScreen</c>).</remarks>
class MCSessionScreen : public MCLogObject
{
public:
    /// <summary>The most players in a session (a game rule: the name slots and the player lights).</summary>
    static constexpr size_t MaxPlayers = 6;
    /// <summary>The slots of a team (a game rule).</summary>
    static constexpr size_t TeamSlots = 3;
    /// <summary>A player id that stands for no player (an empty slot).</summary>
    static constexpr uint32_t NoPlayer = 0xffffffff;

    /// <summary>Makes the buttons, the RP spinners and texts, the tech base toggles and the six name slots.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;

    /// <summary>Forgets the drop targets, the mission and the map.</summary>
    void OnDestroy() override;

    /// <summary>
    /// Draws the background, the unassigned list, the mission and map names, the file name and each team's resource
    /// points per player. Port: draws them from the state, then the shared places (<see cref="MCLogScreenChrome"/>:
    /// the ticker, the clock, the lights' backing).
    /// </summary>
    void Draw() override;

    /// <summary>Port: the screen draws itself each frame (its port is a view).</summary>
    bool DrawsLive() override { return true; }

    /// <summary>Port: see <see cref="MCLogScreenChrome"/>.</summary>
    MCOptionalRef<MCLogScreenChrome> Chrome() override { return ScreenChrome; }

    /// <summary>Port: what the screen shows of the shared places.</summary>
    MCLogScreenChrome ScreenChrome;

    /// <summary>Name drops, the ping/resource point timer, and (port-only) a click on a tab.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>Port-only: while the UPDATE page shows over the screen, a click on it reaches only the page.</summary>
    std::shared_ptr<MCGuiObject> FindObject(int32_t xPos, int32_t yPos) override;

    /// <summary>
    /// Port-only: brings <paramref name="tab"/> in front, its panel in the communications box's place: the chat, or
    /// the MATCH OPTIONS list (logistics/MCMatchOptionsTab.h).
    /// </summary>
    void ShowTab(MCSessionTab tab);

    /// <summary>
    /// Port-only: the screen hidden with MATCH OPTIONS in front puts the chat back in front first, so the next screen's
    /// chat shows (each screen's chat window takes the shown state of the one before, MCLogistics::ShowChatOn).
    /// </summary>
    void ShowGuiWindow(bool show) override;

    /// <summary>
    /// Fills the screen from the session: the players, the teams and the controls for host or guest;
    /// <paramref name="refresh"/> (back from a mission) waits for everyone to check in again. Port-only (the personal
    /// build): a host opens it alone, waiting for players.
    /// </summary>
    void Activate(bool refresh);

    /// <summary>
    /// Port-only (the personal build): the host shows its room to every player again, as a player joining needs (the
    /// original's GO did it once): each game's screen starts afresh (<c>SwitchScreen</c>) and the host sends its picks
    /// again in the messages every build takes: the teams, the mission (every player asked for its file again), the
    /// tech bases, the points and the match options.
    /// </summary>
    void ShowRoomAgain();

    /// <summary>Port-only (the personal build): whether every player in the session is on the screen.</summary>
    bool AllShown();

    /// <summary>
    /// Port-only (the personal build): on the host, whether guest <paramref name="playerId"/> was shown the room with its
    /// game up (it had pinged), so no showing again waits for it.
    /// </summary>
    bool ShownWithGameUp(uint32_t playerId) const;

    /// <summary>Port-only (Redux HD): the host's picks as they stand, the players by name (a rematch's, network/MCRoomEnd.h).</summary>
    MCRoomPicks Picks() const;

    /// <summary>
    /// Port-only (Redux HD): the host's room, hosted again, takes <paramref name="picks"/>: the mission (asked of everyone
    /// there), the points and tech bases unless it fixes them, and each player there back in its slot by name.
    /// </summary>
    void TakePicks(const MCRoomPicks& picks);

    /// <summary>
    /// Port-only (Redux HD): each player on the screen whose name has a slot in <paramref name="picks"/>, on no team, goes
    /// back to it if it is free, told to everyone.
    /// </summary>
    void PlaceByName(const MCRoomPicks& picks);

    /// <summary>
    /// The session's player ids for the name slots: <paramref name="ids"/> ascending, the empty slots
    /// (<see cref="NoPlayer"/>) after them.
    /// </summary>
    static std::array<uint32_t, MaxPlayers> SortedPlayerIds(std::span<const uint32_t> ids);

    /// <summary>
    /// Moves <paramref name="playerId"/> to team <paramref name="team"/> (0 = unassigned) at <paramref name="slot"/>;
    /// unless <paramref name="remote"/>, tells the other players.
    /// </summary>
    void AssignPlayer(uint32_t playerId, int8_t team, int8_t slot, bool remote);

    /// <summary>Shows the map picture for mission file <paramref name="fileName"/> (none when empty).</summary>
    void SetMap(std::string_view fileName);

    void SetMissionName(std::string_view name);

    void SetMapName(std::string_view name);

    /// <summary>A player has the mission: when all have, the start button lights.</summary>
    void SomeoneCheckedIn();

    /// <summary>
    /// Player <paramref name="playerId"/> reports whether it has the mission file (<paramref name="check"/>, as the host
    /// checked it). When every player has: the load goes on (port: once the host is told whose game couldn't check its
    /// data); else the players without it are named, and (port) those whose data differ from the host's.
    /// </summary>
    void FileReport(uint32_t playerId, MCFileCheck check);

    /// <summary>
    /// Port-only (the personal build): player <paramref name="playerId"/> is gone from the room for good (it left before
    /// the match, or a guest away updating didn't rejoin in time): its name and its place on a team go, here only.
    /// </summary>
    void RemovePlayer(uint32_t playerId);

    /// <summary>
    /// Port-only (Redux HD): the room's host has gone and its session rolled over to this game, which hosts the room
    /// now: the host's controls come to this screen, the match options are what the room played, and the room as it
    /// stands (every player's picks, the mission) is shown to everyone again from here.
    /// </summary>
    void TakeOverRoom();

    /// <summary>Loads mission <paramref name="fileName"/> chosen by the host and tells the others.</summary>
    void LoadMission(std::string_view fileName);

    /// <summary>Forgets the loaded mission.</summary>
    void CancelMission();

    /// <summary>
    /// Port: copies who is on each team, the tech bases and the points into <c>MultiPlayer()->Match</c>, which is all the
    /// mech bay reads of the match. Called as the match starts, host and guest. (A configured match fills it itself and
    /// never shows this screen: see <c>MPGameConfiguration</c>.)
    /// </summary>
    void PublishMatchState() const;

    /// <summary>
    /// Lights the start button when every player is on a team and a mission is loaded. Port-only (the personal build):
    /// and every player in the session is on the screen (a player who just joined is shown first), and no guest is
    /// away updating, for a while at most in all (network/MCLobbyUpdate.h); while one is, <see cref="StartWaits"/>
    /// says whom the room waits for, in the update's line's place.
    /// </summary>
    void CheckGoodToGo();

    /// <summary>Sets team <paramref name="team"/>'s tech base (1 Inner Sphere, -1 Clan) and its toggles.</summary>
    void SetTeamTechBase(int8_t team, int8_t techBase);

    /// <summary>Enables the host's controls.</summary>
    void ControlsOn();

    /// <summary>Disables the host's controls (a guest's screen).</summary>
    void ControlsOff();

    /// <summary>Greys the RP spinners while <paramref name="lock"/>.</summary>
    void LockControls(bool lock);

    /// <summary>Sets team 1's resource points text.</summary>
    void SetTeam1RP(int32_t resourcePoints) const;

    /// <summary>Sets team 2's resource points text.</summary>
    void SetTeam2RP(int32_t resourcePoints) const;

    /// <summary>The tab back to the session list.</summary>
    MCGuiHandle<MCLogToolButton> SessionButton;
    /// <summary>The tab that leaves the session.</summary>
    MCGuiHandle<MCLogToolButton> ExitButton;
    /// <summary>Opens the load dialog (host).</summary>
    MCGuiHandle<MCLogButton> LoadMissionButton;
    /// <summary>Starts the mission (host, when <see cref="CheckGoodToGo"/> allows).</summary>
    MCGuiHandle<MCLogButton> StartButton;
    /// <summary>Team 1's resource points (a number in text).</summary>
    MCGuiHandle<MCLogTextObject> Team1RPText;
    /// <summary>Team 2's resource points (a number in text).</summary>
    MCGuiHandle<MCLogTextObject> Team2RPText;
    /// <summary>The mission description.</summary>
    MCGuiHandle<MCLogScrollTextObject> MissionText;
    /// <summary>Raises team 1's resource points.</summary>
    MCGuiHandle<MCLogSpinnerButton> Team1RPUp;
    /// <summary>Lowers team 1's resource points.</summary>
    MCGuiHandle<MCLogSpinnerButton> Team1RPDown;
    /// <summary>Raises team 2's resource points.</summary>
    MCGuiHandle<MCLogSpinnerButton> Team2RPUp;
    /// <summary>Lowers team 2's resource points.</summary>
    MCGuiHandle<MCLogSpinnerButton> Team2RPDown;
    /// <summary>The loaded mission's time ("m:ss"), drawn beside the mission name; empty without one.</summary>
    std::string MissionLabel;
    /// <summary>Team 1's Inner Sphere tech base toggle.</summary>
    MCGuiHandle<MCLogToolButton> Team1ISButton;
    /// <summary>Team 1's Clan tech base toggle.</summary>
    MCGuiHandle<MCLogToolButton> Team1ClanButton;
    /// <summary>Team 2's Inner Sphere tech base toggle.</summary>
    MCGuiHandle<MCLogToolButton> Team2ISButton;
    /// <summary>Team 2's Clan tech base toggle.</summary>
    MCGuiHandle<MCLogToolButton> Team2ClanButton;
    /// <summary>Team 1's tech base: 1 Inner Sphere, -1 Clan.</summary>
    int8_t Team1TechBase = 1;
    /// <summary>Team 2's tech base.</summary>
    int8_t Team2TechBase = -1;
    /// <summary>The name slots, one per player (top to bottom in the unassigned list).</summary>
    std::array<MCGuiHandle<MCPlayerNameObject>, MaxPlayers> PlayerNames;
    /// <summary>The number of players in the session.</summary>
    int32_t NumPlayers = 0;
    /// <summary>The number of players on no team.</summary>
    int32_t NumUnassigned = 0;
    /// <summary>Team 1's players by slot (<see cref="NoPlayer"/> = empty).</summary>
    std::array<uint32_t, TeamSlots> Team1Players = {};
    /// <summary>Team 2's players by slot.</summary>
    std::array<uint32_t, TeamSlots> Team2Players = {};
    /// <summary>Team 1's resource points last sent.</summary>
    int32_t Team1RP = 0;
    /// <summary>Team 2's resource points last sent.</summary>
    int32_t Team2RP = 0;
    /// <summary>The loaded mission's name.</summary>
    std::string MissionName;
    /// <summary>The loaded mission's file; empty = none.</summary>
    std::string MissionFile;
    /// <summary>The loaded mission's map name.</summary>
    std::string MapName;

    /// <summary>
    /// Port: the loaded mission's map picture, drawn stretched over the map box, or null. (The original stretched each
    /// map into the background picture over the ones before.)
    /// </summary>
    std::shared_ptr<MCLogPort> MapPicture;
    /// <summary>Port: the mission was taken away: the map box shows colour 0x10 rather than the background's art.</summary>
    bool MapBoxWiped = false;

    /// <summary>Port-only: the MATCH OPTIONS list, in the chat window's place while its tab is in front.</summary>
    MCGuiHandle<MCLogOptionList> MatchOptionsList;
    /// <summary>Port-only: the tab in front.</summary>
    MCSessionTab FrontTab = MCSessionTab::Chat;
    /// <summary>
    /// Port-only: the tabs' pictures, with MULTIPLAYER CHAT and with MATCH OPTIONS in front (null when the art isn't
    /// the tabs' as expected: then there is no MATCH OPTIONS tab).
    /// </summary>
    std::array<std::shared_ptr<MCLogPort>, 2> MatchTabs;
    /// <summary>Port-only: the chat's lines added when MATCH OPTIONS came in front (more since: MULTIPLAYER CHAT blinks).</summary>
    size_t ChatLinesSeen = 0;
    /// <summary>
    /// Port-only (the personal build): in a game a lobby started, which never shows the main menu, the line saying an
    /// update waits, left of the start button; a click opens the UPDATE page over the screen (logistics/MCUpdatePage.h).
    /// Null in other games, and in a build that doesn't update itself.
    /// </summary>
    MCGuiHandle<MCLogObject> UpdateLine;
    /// <summary>
    /// Port-only (the personal build): whom the room waits for, guests away updating ("WAITING FOR ANN TO REJOIN."), as
    /// <see cref="CheckGoodToGo"/> last found; empty when it waits for no one.
    /// </summary>
    std::string StartWaits;

    /// <summary>Port: draws the map box (<see cref="MapPicture"/>) into <paramref name="target"/>.</summary>
    void DrawMap(const MCPane& target) const;

    /// <summary>Port: frees <see cref="MapPicture"/>.</summary>
    void ClearMap();

private:
    /// <summary>Lays the names of the players on no team down the unassigned list (see the callers for the skips).</summary>
    void LayOutUnassigned(uint32_t skipId, size_t names, bool skipEmpty);

    /// <summary>Port-only (the personal build): <see cref="StartWaits"/>' line, in the update's line's place.</summary>
    void DrawStartWaits(const MCPane& frame);

    /// <summary>The spinners' and tech toggles' live state (the host's, or an unlocked mission's).</summary>
    void SetTeamControls(bool live) const;

    /// <summary>
    /// Port-only (the personal build): the host's tech bases (told to everyone) and points again, unless the mission
    /// fixes them.
    /// </summary>
    void SetTeamsAgain(const std::array<int8_t, 2>& techBases, const std::array<int32_t, 2>& points);

    /// <summary>Shows <paramref name="text"/> in the message dialog with one button running <paramref name="onOk"/>.</summary>
    static void ShowDialog(std::string_view text, std::function<void()> onOk, std::string_view upArt,
                           std::string_view downArt, std::function<void(int32_t)> onResult);

    /// <summary>The name slot of <paramref name="playerId"/>, or null.</summary>
    std::shared_ptr<MCPlayerNameObject> NameOf(uint32_t playerId);

    /// <summary>
    /// Port-only (the personal build): the host's watch on its room, each tick: a configured match's start, and a player
    /// who joined (or a guest whose game was not yet up when it was shown the room) shown the room
    /// (<see cref="ShowRoomAgain"/>) once its game is up (it pinged, <c>MCFidpPlayer::HeardPing</c>), or after
    /// <c>ShowAnywayMs</c>, as long as nothing is under way on the screen.
    /// </summary>
    void WatchRoom();

    /// <summary>
    /// The answers to the mission file's inquiry so far: when every player has the file, the load goes on (port: once
    /// the host is told whose game couldn't check its data); when none is still waited for and some lack it, they are
    /// named and the mission is called off.
    /// </summary>
    void FilesAnswered();

    /// <summary>Until when (the game clock) the screen keeps pinging after it opens.</summary>
    uint32_t _PingUntil = 0;
    /// <summary>The screen still pings.</summary>
    bool _Pinging = false;
    /// <summary>Port-only (Redux HD): the room has rolled over to this game, whose next look takes it (TakeOverRoom).</summary>
    bool _TakingOver = false;
    /// <summary>
    /// Port-only (the personal build): the name the host loaded its mission by, while one is loaded (Port-only, Redux HD:
    /// on a guest too, for a room rolled over to it).
    /// </summary>
    std::string _LoadedMission;
    /// <summary>Port-only (the personal build): the loaded mission fixes its points and tech bases (its Lock block).</summary>
    bool _MissionLocked = false;
    /// <summary>
    /// Port-only (the personal build): the host's guests whose game was up (they had pinged) when they were last shown
    /// the room; one shown before it was up is shown it again once it is.
    /// </summary>
    std::set<uint32_t> _ShownReady;
    /// <summary>Port-only (the personal build): when (the game clock) the host first saw each player in the session.</summary>
    std::map<uint32_t, uint32_t> _SeenSince;
};

/// <summary>
/// Port-only (Redux HD): the host's picks for its room, its players by name: a rematch's room, hosted again, takes them
/// (network/MCRoomEnd.h), each player back in its slot as it returns.
/// </summary>
struct MCRoomPicks
{
    /// <summary>The mission, as the host loaded it (MCSessionScreen::LoadMission's name); empty: none.</summary>
    std::string Mission;
    /// <summary>Each team's players' names by slot; empty: no one.</summary>
    std::array<std::string, MCSessionScreen::TeamSlots> Team1;
    std::array<std::string, MCSessionScreen::TeamSlots> Team2;
    std::array<int8_t, 2> TechBases = {1, -1};
    std::array<int32_t, 2> Points = {};
};

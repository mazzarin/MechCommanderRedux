#pragma once

#include "gui/MCGuiOwned.h"
#include "logistics/MCLogObject.h"
#include "logistics/MCLogScrollTextObject.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCLogToolButton.h"
#include "logistics/MCPlayerNameObject.h"

class MCGuiEvent;

/// <summary>
/// The multiplayer session (ready room) screen: the players and the two teams, each team's resource points and
/// tech base, the mission chosen by the host, and the load/start buttons.
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

    ~MCSessionScreen() override;

    /// <summary>Makes the buttons, the RP spinners and texts, the tech base toggles and the six name slots.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;

    void Destroy() override;

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

    /// <summary>Name drops and the ping/resource point timer.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// Fills the screen from the session: the players, the teams and the controls for host or guest;
    /// <paramref name="refresh"/> (back from a mission) waits for everyone to check in again.
    /// </summary>
    void Activate(bool refresh);

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

    /// <summary>Player <paramref name="playerId"/> reports whether it has the mission file (<paramref name="haveFile"/>).</summary>
    void FileReport(uint32_t playerId, bool haveFile);

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

    /// <summary>Lights the start button when every player is on a team and a mission is loaded.</summary>
    void CheckGoodToGo() const;

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
    MCGuiOwned<MCLogToolButton> SessionButton;
    MCGuiOwned<MCLogToolButton> ExitButton;
    /// <summary>Opens the load dialog (host).</summary>
    MCGuiOwned<MCLogButton> LoadMissionButton;
    /// <summary>Starts the mission (host, when <see cref="CheckGoodToGo"/> allows).</summary>
    MCGuiOwned<MCLogButton> StartButton;
    /// <summary>Team 1's resource points (a number in text).</summary>
    MCGuiOwned<MCLogTextObject> Team1RPText;
    /// <summary>Team 2's resource points (a number in text).</summary>
    MCGuiOwned<MCLogTextObject> Team2RPText;
    /// <summary>The mission description.</summary>
    MCGuiOwned<MCLogScrollTextObject> MissionText;
    MCGuiOwned<MCLogSpinnerButton> Team1RPUp;
    MCGuiOwned<MCLogSpinnerButton> Team1RPDown;
    MCGuiOwned<MCLogSpinnerButton> Team2RPUp;
    MCGuiOwned<MCLogSpinnerButton> Team2RPDown;
    /// <summary>The loaded mission's time ("m:ss"), drawn beside the mission name; empty without one.</summary>
    std::string MissionLabel;
    /// <summary>Team 1's Inner Sphere tech base toggle.</summary>
    MCGuiOwned<MCLogToolButton> Team1ISButton;
    /// <summary>Team 1's Clan tech base toggle.</summary>
    MCGuiOwned<MCLogToolButton> Team1ClanButton;
    /// <summary>Team 2's Inner Sphere tech base toggle.</summary>
    MCGuiOwned<MCLogToolButton> Team2ISButton;
    /// <summary>Team 2's Clan tech base toggle.</summary>
    MCGuiOwned<MCLogToolButton> Team2ClanButton;
    /// <summary>Team 1's tech base: 1 Inner Sphere, -1 Clan.</summary>
    int8_t Team1TechBase = 1;
    /// <summary>Team 2's tech base.</summary>
    int8_t Team2TechBase = -1;
    /// <summary>The name slots, one per player (top to bottom in the unassigned list).</summary>
    std::array<MCGuiOwned<MCPlayerNameObject>, MaxPlayers> PlayerNames;
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

    /// <summary>Port: draws the map box (<see cref="MapPicture"/>) into <paramref name="target"/>.</summary>
    void DrawMap(const MCPane& target) const;

    /// <summary>Port: frees <see cref="MapPicture"/>.</summary>
    void ClearMap();

private:
    /// <summary>Lays the names of the players on no team down the unassigned list (see the callers for the skips).</summary>
    void LayOutUnassigned(uint32_t skipId, size_t names, bool skipEmpty);

    /// <summary>The spinners' and tech toggles' live state (the host's, or an unlocked mission's).</summary>
    void SetTeamControls(bool live) const;

    /// <summary>Shows <paramref name="text"/> in the message dialog with one button running <paramref name="onOk"/>.</summary>
    static void ShowDialog(std::string_view text, std::function<void()> onOk, std::string_view upArt,
                           std::string_view downArt, std::function<void(int32_t)> onResult);

    /// <summary>The name slot of <paramref name="playerId"/>, or null.</summary>
    std::shared_ptr<MCPlayerNameObject> NameOf(uint32_t playerId);

    /// <summary>Until when (the game clock) the screen keeps pinging after it opens.</summary>
    uint32_t _PingUntil = 0;
    /// <summary>The screen still pings.</summary>
    bool _Pinging = false;
};

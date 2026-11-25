#pragma once

#include "logistics/MCRoomModel.h"

#include "gui/MCGuiRef.h"
#include "logistics/MCLogObject.h"
#include "logistics/MCLogOptionList.h"
#include "logistics/MCLogScrollTextObject.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCLogToolButton.h"
#include "logistics/MCMatchOptionsTab.h"
#include "logistics/MCPlayerNameObject.h"

class MCGuiEvent;

/// <summary>
/// The multiplayer session screen: the players and the two teams, each team's resource points and tech base, the
/// mission chosen by the host, and the load/start buttons. Port-only (the personal build): the game's room, which
/// hosting and joining open (the original's ready room is gone); it takes players until START.
/// </summary>
/// <remarks>
/// Original source: <c>logistics\logsession.cpp</c> (<c>SessionScreen</c>). Port-only (SPEC-menus.md's stage 9): the
/// room's logic is its model's (logistics/MCRoomModel.h), and this screen a view on it.
/// </remarks>
class MCSessionScreen
    : public MCLogObject
    , public MCRoomView
{
public:
    /// <summary>The most players in a session (a game rule: the name slots and the player lights).</summary>
    static constexpr size_t MaxPlayers = MCRoomModel::MaxPlayers;
    /// <summary>The slots of a team (a game rule).</summary>
    static constexpr size_t TeamSlots = MCRoomModel::TeamSlots;
    /// <summary>A player id that stands for no player (an empty slot).</summary>
    static constexpr uint32_t NoPlayer = MCRoomModel::NoPlayer;

    /// <summary>Makes the buttons, the RP spinners and texts, the tech base toggles and the six name slots.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;

    /// <summary>Forgets the drop targets and the map's picture.</summary>
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

    /// <summary>Name drops, and (port-only) a click on a tab.</summary>
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
    /// Port-only (SPEC-menus.md's stage 4): shown, the remade room (logistics/MCRoomScreen) shows in its place and the
    /// screen stays hidden, logistics' screen still. Hidden, the remade room hides with it.
    /// </summary>
    void ShowGuiWindow(bool show) override;

    // ---- The room's view (MCRoomView): the room's logic tells it as it goes ----

    void Opening() override;
    void Opened() override;
    void NameToRow(size_t seat, int32_t row) override;
    void NameToTeam(size_t seat, int8_t team, int8_t slot) override;
    void ShowName(size_t seat, bool shown) override;
    void ShowPoints(int8_t team) override;
    void TakeTypedPoints() override;
    void ShowMissionText(int32_t font, std::span<const std::string> lines) override;
    void ShowMap() override;
    void ShowControls() override;
    void ShowTechBases() override;
    void ShowStart() override;
    void LeftMissionPick() override;
    void DrawNow() override;
    void StopBoxTimers() override;

    /// <summary>The tab back to the session list.</summary>
    MCGuiHandle<MCLogToolButton> SessionButton;
    /// <summary>The tab that leaves the session.</summary>
    MCGuiHandle<MCLogToolButton> ExitButton;
    /// <summary>Opens the load dialog (host).</summary>
    MCGuiHandle<MCLogButton> LoadMissionButton;
    /// <summary>Starts the mission (host, when the room's <see cref="MCRoomModel::CheckGoodToGo"/> allows).</summary>
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
    /// <summary>Team 1's Inner Sphere tech base toggle.</summary>
    MCGuiHandle<MCLogToolButton> Team1ISButton;
    /// <summary>Team 1's Clan tech base toggle.</summary>
    MCGuiHandle<MCLogToolButton> Team1ClanButton;
    /// <summary>Team 2's Inner Sphere tech base toggle.</summary>
    MCGuiHandle<MCLogToolButton> Team2ISButton;
    /// <summary>Team 2's Clan tech base toggle.</summary>
    MCGuiHandle<MCLogToolButton> Team2ClanButton;
    /// <summary>
    /// Port-only (SPEC-menus.md's stage 4): the room's state, which this screen views (logistics' room model,
    /// <see cref="MCLogistics::Room"/>).
    /// </summary>
    static MCRoomModel& Room();
    /// <summary>The name slots, one per player (top to bottom in the unassigned list).</summary>
    std::array<MCGuiHandle<MCPlayerNameObject>, MaxPlayers> PlayerNames;

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

    /// <summary>Port: draws the map box (<see cref="MapPicture"/>) into <paramref name="target"/>.</summary>
    void DrawMap(const MCPane& target) const;

    /// <summary>Port: frees <see cref="MapPicture"/>.</summary>
    void ClearMap();

private:
    /// <summary>Port-only (the personal build): the room model's StartWaits line, in the update's line's place.</summary>
    void DrawStartWaits(const MCPane& frame);

    /// <summary>The spinners' and tech toggles' live state (the host's, or an unlocked mission's).</summary>
    void SetTeamControls(bool live) const;
};

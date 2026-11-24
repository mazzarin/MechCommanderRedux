#pragma once

// Port-only (SPEC-menus.md's stage 4, "The room"): the remade room (ui/room.rml), the room model's view
// (logistics/MCRoomModel): its name along the top with how it's found; the teams with their tech bases and points, the
// players on no team as cards the host drags onto a team or sends with TEAM 1 and TEAM 2; the map with CHOOSE MAP, the
// match options as played with VIEW or CHANGE; the chat; along the foot LEAVE, whom the room waits for and the host's
// BEGIN PLANNING. It shows in the 1998 session screen's place (MCSessionScreen::ShowGuiWindow), which stays logistics'
// screen, hidden, while its logic moves into the model: the flow's "room" kind is the same, what is sent is unchanged.

#include "logistics/MCChatView.h"
#include "logistics/MCRoomModel.h"
#include "ui/MCRmlScreen.h"

class MCRmlModel;

/// <summary>The remade room (<c>ui/room.rml</c>).</summary>
class MCRoomScreen final : public MCRmlScreen
{
public:
    MCRoomScreen();
    ~MCRoomScreen() override;

    /// <summary>For the tests and the recorder: what shows, as a player reads it.</summary>
    struct MCSeen
    {
        std::string Eyebrow;
        std::string Name;
        /// <summary>The facts at the top right, joined by " | ": "Public | Code K7QF | 4 of 6 players".</summary>
        std::string Facts;
        /// <summary>Each team's places: a player's name ("Bob (host)"), or "Open".</summary>
        std::vector<std::string> Team1;
        std::vector<std::string> Team2;
        /// <summary>The players on no team, by name.</summary>
        std::vector<std::string> Pool;
        std::string Tech1;
        std::string Tech2;
        std::string Points1;
        std::string Points2;
        /// <summary>The map's name; empty with none ("No map yet").</summary>
        std::string Map;
        std::vector<std::string> MapText;
        /// <summary>The match options as played: "Weather | Mission's".</summary>
        std::vector<std::string> Options;
        /// <summary>The chat's lines, as they read: "Bob: hello", "Ann joined.".</summary>
        std::vector<std::string> Chat;
        /// <summary>The foot's line: whom the room waits for, or the update waiting.</summary>
        std::string Status;
        /// <summary>The update waiting's line (a launcher's game's), which opens UPDATE; empty with none.</summary>
        std::string UpdateLine;
        bool Hosting = false;
        bool CanBegin = false;
        /// <summary>The host's room closing (its guests waited for): LEAVE and BEGIN PLANNING held.</summary>
        bool Closing = false;
    };

    MCSeen Seen() const;

    /// <summary>What a player's presses do (and the tests'): each the model's action, as a click would.</summary>
    void PressBegin();
    void PressLeave();
    void PressJoin(uint32_t playerId, int8_t team);
    /// <summary>A card dropped on team <paramref name="team"/>'s place <paramref name="slot"/> (no team: the pool).</summary>
    void Drop(uint32_t playerId, int8_t team, int8_t slot);
    /// <summary>The host's typed points for team <paramref name="team"/> (commas allowed), as the box takes them.</summary>
    void TypePoints(int8_t team, std::string_view typed);
    /// <summary>The host's tech base switch for team <paramref name="team"/>.</summary>
    void PickTechBase(int8_t team, int8_t techBase);
    /// <summary>A chat line sent, to the player's team or to all, as ENTER in the chat's box sends it.</summary>
    void Say(std::string_view text, bool toTeam);
    void PressChooseMap();
    void PressOptions();
    void PressUpdate();

    void OnFrame() override;
    void OnDestroy() override;

protected:
    void MakeModels(Rml::Context& context) override;
    void OnLoaded(Rml::ElementDocument& document) override;
    void OnShow() override;
    void OnHide() override;
    MCOptionalRef<Rml::Element> FirstFocus(Rml::ElementDocument& document) override;

private:
    /// <summary>A team's place as the document binds it.</summary>
    struct MCSlot
    {
        bool Open = true;
        int Id = 0;
        std::string Name;
        std::string Colour;
        bool Host = false;
        bool Me = false;

        bool operator==(const MCSlot&) const = default;
    };

    /// <summary>A player on no team (a type of its own: the model binds one list to each row type).</summary>
    struct MCCard
    {
        int Id = 0;
        std::string Name;
        std::string Colour;
        bool Host = false;
        bool Me = false;

        bool operator==(const MCCard&) const = default;
    };

    struct MCMapLine
    {
        std::string Text;

        bool operator==(const MCMapLine&) const = default;
    };

    struct MCOption
    {
        std::string Name;
        std::string Value;
        bool Off = false;

        bool operator==(const MCOption&) const = default;
    };

    /// <summary>The model's room copied into what the document binds, each change said.</summary>
    void ShowRoom();
    void ShowTeams(const std::vector<MCRoomModel::MCPlayer>& players);
    void ShowMatch();
    void ShowChat();
    void ShowFoot(const std::vector<MCRoomModel::MCPlayer>& players);
    /// <summary>The host's switches and points boxes set from the model (not a box being typed in).</summary>
    void ShowControls();
    /// <summary>The model's value <paramref name="name"/> set to <paramref name="now"/>, said when it changed.</summary>
    template <typename T> void Keep(const char* name, T& value, T now);

    std::unique_ptr<MCRmlModel> _Model;
    /// <summary>The card being dragged (its player's id).</summary>
    std::optional<uint32_t> _Dragged;

    // What the document binds (the model's values at its top level).
    std::string _Eyebrow;
    std::string _Name;
    std::string _Access;
    std::string _Code;
    int _Count = 0;
    int _Most = 0;
    bool _Hosting = false;
    bool _CanTeams = false;
    bool _CanForces = false;
    std::string _Tech1Name;
    std::string _Tech2Name;
    std::string _Points1;
    std::string _Points2;
    bool _Full1 = false;
    bool _Full2 = false;
    std::vector<MCSlot> _Slots1;
    std::vector<MCSlot> _Slots2;
    std::vector<MCCard> _Pool;
    bool _HasMap = false;
    std::string _MapTitle;
    std::string _MapPic;
    std::string _MapTime;
    std::vector<MCMapLine> _MapLines;
    std::string _OptionsWord;
    std::vector<MCOption> _Opts;
    /// <summary>The chat's lines as the document binds them (logistics/MCChatView, stage 8's too).</summary>
    MCChatView _Chat;
    bool _CanTeamChat = false;
    std::string _Say;
    std::string _UpdateLine;
    std::string _Waits;
    std::string _WaitName;
    std::string _WaitColour;
    std::string _WaitWhat;
    bool _CanBegin = false;
    bool _Closing = false;
};

/// <summary>
/// The remade room shown in the 1998 room's place (its session screen hidden); false when it can't show (the menus
/// can't start, or its document can't be read), and the 1998 room shows.
/// </summary>
bool ShowRoomScreen();

/// <summary>The remade room hidden (the 1998 room hidden, or another screen come up).</summary>
void HideRoomScreen();

/// <summary>Where the room is, as its eyebrow names it: "Internet", "LAN", or "Multiplayer" for a launcher's.</summary>
std::string RoomPlace();

/// <summary>The remade room (none before it first showed, or once the menus stopped).</summary>
MCGuiHandle<MCRoomScreen> RoomScreen();

/// <summary>Whether the remade room shows.</summary>
bool RoomScreenShown();

/// <summary>
/// Port-only (SPEC-menus.md's stage 4, a launcher's way in): a launcher's game on the painting alone
/// (<c>ui/launcher.rml</c>) while it joins its room, the join's dialog over it: "Joining Steel Rain..." ("Joining the
/// game..." until the room's name is known, as with a bare <c>-join</c>), or its host's "Setting up your game...";
/// CANCEL (and Escape) ends the game. The room opening hides it. It stands in the 1998 ready room's place, which is gone.
/// </summary>
class MCLauncherScreen final : public MCRmlScreen
{
public:
    MCLauncherScreen();

    /// <summary>What the join's dialog says now (its heading).</summary>
    static std::string JoinLine();

    void OnFrame() override;

protected:
    void OnShow() override;

private:
    /// <summary>The join's dialog said (again, when its line changed).</summary>
    void Say();

    std::string _Said;
};

/// <summary>
/// The launcher's join shown in the 1998 ready room's place (logistics' ConnectScreen, hidden); false when it can't
/// show (the menus can't start), and the 1998 screen shows.
/// </summary>
bool ShowLauncherScreen();

/// <summary>Whether the launcher's join shows.</summary>
bool LauncherScreenShown();

#pragma once

// Port-only (SPEC-menus.md's stage 8, step 1): the campaign's screens remade, BRIEFING and DEPLOY, one RmlUi screen
// (ui/campaign.rml) under the bar every campaign screen shares (MAIN MENU, the four tabs, RP, the clock): BRIEFING reads
// the mission (its map, title, objectives, fire support, orders and warnings: logistics/MCBriefingText), DEPLOY chooses
// the force (READY AREA's cards, the drop's three groups, the weight and BEGIN MISSION: logistics/MCDeploy's rules, the
// 1998 briefing's own). MCLogistics::SetUpBriefingScreen shows it; PURCHASING and the 'MECH BAY are its tabs too
// (logistics/MCLogisticsTabs). The flow's "campaign" kind (logistics/MCMenuScreens) shows while this does.

#include "logistics/MCBriefingText.h"
#include "logistics/MCChatView.h"
#include "logistics/MCDeploy.h"
#include "ui/MCRmlScreen.h"

namespace Rml
{
    class Event;
}

class MCLogPart;
class MCLogisticsTabs;
class MCRmlModel;

namespace MCCampaignMenu
{
    /// <summary>The corner of a drop zone's tag its tail leaves from (tools/upscale/zone_tags.py's br, bl, tr, tl).</summary>
    enum class MCTail : uint8_t
    {
        BottomRight,
        BottomLeft,
        TopRight,
        TopLeft,
    };

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 8): a team's drop zone's tag on a match's BRIEFING map, as the HD briefing maps
    /// tag 1998's painted flags: its group (1 to 3, DEPLOY's; F1 to F3), the corner its tail leaves from, the zone (its
    /// tail's tip) and its box, in the map picture's own pixels (the 1998 picture's).
    /// </summary>
    struct MCZoneTag
    {
        int Group = 0;
        MCTail Tail = MCTail::BottomRight;
        float X = 0;
        float Y = 0;
        float Left = 0;
        float Top = 0;
        float Width = 0;
        float Height = 0;

        bool operator==(const MCZoneTag&) const = default;
    };

    /// <summary>The map picture's size in its own pixels (the 1998 picture's), and the team's tags on it.</summary>
    struct MCZoneMap
    {
        float Width = 0;
        float Height = 0;
        std::vector<MCZoneTag> Tags;
    };
}

/// <summary>BRIEFING and DEPLOY.</summary>
class MCCampaignScreen final : public MCRmlScreen
{
public:
    /// <summary>A match's player in the bar's second row: the name in their colour, and their light.</summary>
    struct MCBarPlayer
    {
        std::string Name;
        /// <summary>The name's colour class (the chat's: MCChatView::ColourOf).</summary>
        std::string Colour;
        /// <summary>BEGIN MISSION pressed: the light green (a ring while they plan).</summary>
        bool Ready = false;

        bool operator==(const MCBarPlayer&) const = default;
    };

    /// <summary>The bar's tabs, in their order.</summary>
    enum class MCTab : uint8_t
    {
        Briefing,
        Purchasing,
        MechBay,
        Deploy,
    };

    /// <summary>A line of the briefing (an objective with its number, a paragraph of the orders, a warning).</summary>
    struct MCLine
    {
        std::string Text;
        int Number = 0;
    };

    /// <summary>What a card is: a unit, an open slot (a well), a slot the player can't fill (striped).</summary>
    enum class MCCardKind : uint8_t
    {
        Unit,
        Empty,
        Closed,
    };

    /// <summary>No picture, as a decorator (a card's bound decorators are set even where its pictures don't show).</summary>
    static constexpr std::string_view NoPicture = "none";

    /// <summary>A card as it shows: a unit's, or a drop slot.</summary>
    struct MCCard
    {
        int Kind = 0;
        std::string Who;
        std::string What;
        /// <summary>Its last line: the tons, or why it can't go.</summary>
        std::string Tons;
        /// <summary>The outline's and the pilot's pictures, as decorators (none: a vehicle's pilot, a slot's).</summary>
        std::string Outline{NoPicture};
        std::string Pilot{NoPicture};
        /// <summary>Whether it has a pilot (a 'Mech): the pilot's picture and the gauges show.</summary>
        bool Piloted = false;
        /// <summary>The condition bar's length (a percentage) and shade ("ok", "worn", "bad"; empty: no bar).</summary>
        int Condition = 0;
        std::string Shade;
        /// <summary>The pilot's health pips kept, of six.</summary>
        int Health = 0;
        bool Off = false;
        /// <summary>Picked (PURCHASING's details, the 'MECH BAY's lab show it).</summary>
        bool Picked = false;
        bool Target = false;
        bool Refuse = false;
        /// <summary>Why the unit carried can't go where it's held, under it.</summary>
        std::string Why;
        /// <summary>The drop slot it is (0 to 11; -1 in READY AREA).</summary>
        int Slot = -1;
    };

    /// <summary>A drop group: its head, and its four slots.</summary>
    struct MCGroup
    {
        std::string Name;
        std::string Tons;
        bool Closed = false;
        std::vector<MCCard> Cards;
    };

    /// <summary>A drop zone's tag as it shows over BRIEFING's map: its box in pixels from the map's corner, its picture.</summary>
    struct MCZoneShown
    {
        std::string Left;
        std::string Top;
        std::string Width;
        std::string Height;
        std::string Picture;

        bool operator==(const MCZoneShown&) const = default;
    };

    /// <summary>A segment of the weight's ramp: its height, and whether it is lit, cyan (would be added) or red.</summary>
    struct MCSegment
    {
        std::string Height;
        int State = 0;
    };

    /// <summary>The ramp segments' states.</summary>
    enum class MCLit : uint8_t
    {
        Dark,
        On,
        More,
        Over,
    };

    MCCampaignScreen();
    ~MCCampaignScreen() override;

    /// <summary>The screen set up for the mission logistics has now, on tab <paramref name="tab"/>.</summary>
    void SetUp(MCTab tab);

    MCTab Tab() const { return static_cast<MCTab>(_Tab); }
    /// <summary>A tab pressed (Q and E, a click).</summary>
    void ChooseTab(MCTab tab);

    /// <summary>What shows (the tests read it).</summary>
    const std::string& Eyebrow() const { return _Eyebrow; }
    const std::string& Title() const { return _Title; }
    const std::vector<MCLine>& Objectives() const { return _Objectives; }
    const std::vector<MCLine>& FireSupport() const { return _Fire; }
    const std::vector<MCLine>& Orders() const { return _Orders; }
    const std::vector<MCLine>& Warnings() const { return _Warnings; }
    const std::string& MapSource() const { return _MapSource; }
    /// <summary>A match's drop zones' tags on the map (none in single player), and as they show at the window's size.</summary>
    const MCCampaignMenu::MCZoneMap& ZoneMap() const { return _ZoneMap; }
    const std::vector<MCZoneShown>& ZonesShown() const { return _Zones; }
    const std::vector<MCCard>& ReadyCards() const { return _Ready; }
    const std::vector<MCGroup>& Groups() const { return _Groups; }
    const std::vector<MCSegment>& Ramp() const { return _Ramp; }
    const std::string& WeightNow() const { return _WeightNow; }
    const std::string& WeightFree() const { return _WeightFree; }
    bool CanBegin() const { return _CanBegin; }
    /// <summary>A match's players in the bar: this game's team, the other team, how many are ready of how many.</summary>
    const std::vector<MCBarPlayer>& OurTeam() const { return _Ours; }
    const std::vector<MCBarPlayer>& OtherTeam() const { return _Theirs; }
    const std::string& ReadyCount() const { return _ReadyCount; }
    const std::string& PlayerCount() const { return _PlayerCount; }
    /// <summary>A match's chat as shown, and whether the line typed goes to the team (TEAM) or to all (ALL).</summary>
    const std::vector<MCChatView::MCLine>& ChatLines() const { return _Chat.Lines(); }
    bool ToTeam() const { return _ToTeam; }
    /// <summary>The unit carried, by the mouse or the keys; none while none is.</summary>
    std::shared_ptr<MCLogPart> Carried() const { return _Carry.has_value() ? _Carry->Part.lock() : nullptr; }

    /// <summary>
    /// The right button (and ENTER) on READY AREA's card <paramref name="ready"/>: into the first open slot of group
    /// <paramref name="group"/>, else of the drop; on drop slot <paramref name="slot"/>'s unit, back to READY AREA.
    /// </summary>
    void SendReady(size_t ready, std::optional<size_t> group);
    void SendBack(size_t slot);

    /// <summary>
    /// The left button's drag, and the keys' carry (SPACE, the arrows, SPACE again or ESC): a card picked up from READY
    /// AREA (<paramref name="ready"/>) or a drop slot, held over slot <paramref name="slot"/> (none: over nothing), put
    /// down there, or put back where it was.
    /// </summary>
    void PickUpReady(size_t ready, bool byKeys);
    void PickUpSlot(size_t slot, bool byKeys);
    void HoldOver(std::optional<size_t> slot);
    void PutDown();
    void PutBack();

    /// <summary>BEGIN MISSION pressed.</summary>
    void PressBegin();

    /// <summary>BRIEFING's PLAY (or Space): the operation's video in its window over the briefing.</summary>
    void PlayVideo();
    /// <summary>Whether BRIEFING has the operation's video to play (a campaign mission naming one).</summary>
    bool HasVideo() const { return _Video; }

    bool OnKey(int key, int modifiers) override;
    void OnFrame() override;
    void OnDestroy() override;

    /// <summary>PURCHASING and the 'MECH BAY (logistics/MCLogisticsTabs).</summary>
    MCLogisticsTabs& Logistics() const { return *_Logistics; }
    /// <summary>The card of <paramref name="part"/> (in READY AREA, the force's lists, or in slot <paramref name="slot"/>).</summary>
    MCCard UnitCard(const MCLogPart& part, int slot) const;
    /// <summary>The keys the hint line names, as the tab and the carry stand.</summary>
    void SetKeys();

    /// <summary>The document's mouse (an Rml::EventListener's).</summary>
    class MCEvents;

protected:
    void MakeModels(Rml::Context& context) override;
    void OnLoaded(Rml::ElementDocument& document) override;
    void OnShow() override;
    void OnHide() override;
    MCOptionalRef<Rml::Element> FirstFocus(Rml::ElementDocument& document) override;

private:
    friend class MCEvents;

    /// <summary>A unit carried: where from (a READY AREA card, a slot), and by what.</summary>
    struct MCCarry
    {
        std::weak_ptr<MCLogPart> Part;
        std::optional<size_t> FromSlot;
        size_t FromReady = 0;
        bool ByKeys = false;
        std::optional<size_t> Over;
        /// <summary>Where on the card the mouse took it (pixels), and where the mouse is.</summary>
        float GrabX = 0;
        float GrabY = 0;
        float MouseX = 0;
        float MouseY = 0;
    };

    /// <summary>The weight as it shows with what is carried held where it is.</summary>
    struct MCWeight
    {
        float Base = 0;
        float Total = 0;
        bool Adds = false;
        bool Refused = false;
    };

    /// <summary>The mission's briefing read again: its title, objectives, fire support, orders, warnings and map.</summary>
    void ReadBriefing();
    /// <summary>The cards made again from the deploy data (READY AREA's, the drop's).</summary>
    void FillCards();
    /// <summary>The weight, the ramp and BEGIN MISSION as the drop and the carry stand.</summary>
    void FillWeight();
    /// <summary>The bar's RP and clock as they are now; whether they changed.</summary>
    bool FillBar();
    MCWeight Weighed() const;
    /// <summary>The bar's tabs marked at the tab shown (or left on), however it was chosen: Q and E step from it.</summary>
    void MarkTab();
    void Changed();
    /// <summary>Sound <paramref name="sample"/> (1998's interface sounds).</summary>
    static void Sound(uint32_t sample);
    /// <summary>What a place or send did: its sounds and, refused, the reason under the card.</summary>
    void Answer(MCDeploy::MCRefusal refusal, const MCLogPart& part, std::optional<size_t> card);
    /// <summary>The drop slot under the point (pixels), or with the keys' focus.</summary>
    std::optional<size_t> SlotAt(float x, float y) const;
    std::optional<size_t> FocusedSlot() const;
    /// <summary>
    /// The open slot an arrow (<paramref name="key"/>, RmlUi's) moves a unit carried by the keys to from slot
    /// <paramref name="from"/>: up or down its group, across to the nearest row of the next group with one open; from
    /// READY AREA (none), the drop's first open slot. None: nowhere that way.
    /// </summary>
    static std::optional<size_t> SlotToward(std::optional<size_t> from, int key);
    /// <summary>The card the element <paramref name="element"/> is in: a READY AREA card's index, or a slot's.</summary>
    static std::optional<std::pair<bool, size_t>> CardOf(MCOptionalRef<const Rml::Element> element);
    /// <summary>The carried card's place over the drop (pixels from its corner).</summary>
    void PlaceCarried();
    /// <summary>
    /// The drop zones' tags placed over the map as it is laid out now (the picture drawn to fit its box, at its left and
    /// top: the tags at its scale); whether they moved.
    /// </summary>
    bool PlaceZoneTags();
    /// <summary>The ramp's lit and cyan lengths moved along to the weight's (its timings); whether a segment changed.</summary>
    bool StepRamp(double now);
    /// <summary>A match's players in the bar, read again; whether they changed.</summary>
    bool FillPlayers();
    /// <summary>TEAM (true) or ALL chosen in one of the chat's places, the others following.</summary>
    void ChooseWhoReads(bool toTeam);
    /// <summary>The line typed sent on Enter, as the room's chat sends it, the boxes emptied.</summary>
    void SayTyped(const std::string& typed);
    /// <summary>A press, release or move of the mouse over the document.</summary>
    void OnMouse(Rml::Event& event);

    std::unique_ptr<MCRmlModel> _Model;
    std::unique_ptr<MCEvents> _Events;
    std::unique_ptr<MCLogisticsTabs> _Logistics;
    int _Tab = 0;
    /// <summary>The units READY AREA's cards stand for, in the cards' order.</summary>
    std::vector<std::weak_ptr<MCLogPart>> _ReadyUnits;
    std::optional<MCCarry> _Carry;
    uint64_t _Revision = 0;
    /// <summary>A refused send's reason under its READY AREA card, and when it goes.</summary>
    std::optional<size_t> _WhyCard;
    double _WhyUntil = 0;
    std::string _WhyText;
    /// <summary>The ramp's lit and cyan lengths as drawn (segments), moving to the weight's over its timings.</summary>
    float _ShownOn = 0;
    float _ShownMore = 0;
    double _LastFrame = 0;

    // The model's values.
    bool _Multiplayer = false;
    std::string _Points;
    std::string _Clock;
    std::string _Eyebrow;
    std::string _Title;
    std::vector<MCLine> _Objectives;
    /// <summary>The fire support, a line each (none: the briefing has no FIRE SUPPORT).</summary>
    std::vector<MCLine> _Fire;
    std::vector<MCLine> _Orders;
    std::string _WarningsHead;
    std::vector<MCLine> _Warnings;
    std::string _MapSource;
    MCCampaignMenu::MCZoneMap _ZoneMap;
    std::vector<MCZoneShown> _Zones;
    /// <summary>The map's box (pixels) the tags were last placed in.</summary>
    SDL_FPoint _ZoneBox{};
    bool _Video = false;
    /// <summary>The operation the video's window names ("Operation 1 · Beachhead").</summary>
    std::string _VideoEyebrow;
    std::vector<MCCard> _Ready;
    std::vector<MCGroup> _Groups;
    std::vector<MCCard> _Carried;
    std::string _CarriedLeft;
    std::string _CarriedTop;
    std::vector<MCSegment> _Ramp;
    std::string _WeightNow;
    std::string _WeightOf;
    std::string _WeightFree;
    bool _WeightMore = false;
    bool _WeightOver = false;
    bool _CanBegin = false;
    /// <summary>A unit carried by the keys: the empty slots take the focus.</summary>
    bool _KeysCarry = false;
    std::vector<MCBarPlayer> _Ours;
    std::vector<MCBarPlayer> _Theirs;
    std::string _ReadyCount;
    std::string _PlayerCount;
    /// <summary>A match's chat as the document binds it (stage 4's, the room's too).</summary>
    MCChatView _Chat;
    /// <summary>The line being typed, one in all the chat's places (each box's value).</summary>
    std::string _Say;
    bool _ToTeam = true;
};

namespace MCCampaignMenu
{
    /// <summary>
    /// The 1998 briefing's map area on its screen, the mission's tac map picture stretched over it: its first and last
    /// pixels each way (283 pixels square).
    /// </summary>
    constexpr int32_t MapLeft = 0x159;
    constexpr int32_t MapTop = 0x1a;
    constexpr int32_t MapRight = 0x273;
    constexpr int32_t MapBottom = 0x134;
    /// <summary>The 1998 briefing's map area on its screen, in pixels each way (its first to its last).</summary>
    constexpr float MapAreaWidth = static_cast<float>(MapRight - MapLeft + 1);
    constexpr float MapAreaHeight = static_cast<float>(MapBottom - MapTop + 1);

    /// <summary>
    /// Where the 1998 briefing marked the world position (<paramref name="worldX"/>, <paramref name="worldY"/>) on its
    /// map, in its screen's pixels: the map turned 45 degrees, <paramref name="scale"/> world units to a map pixel
    /// (MCLogistics::MCZoneMarks::Scale). Port: the original's drop zone marker worked it out in place.
    /// </summary>
    SDL_FPoint MapPointOf(float worldX, float worldY, float scale);

    /// <summary>
    /// The tag of group <paramref name="group"/> with its tail's tip at (<paramref name="x"/>, <paramref name="y"/>) on
    /// a map picture <paramref name="width"/> by <paramref name="height"/> (its own pixels): the tail at the bottom
    /// right, as 1998's flags, unless the tag would cross the picture's edge; then the first of the other corners
    /// (bottom left, top right, top left) that keeps it inside, or, with none, the one least outside.
    /// </summary>
    MCZoneTag ZoneTag(int group, float x, float y, float width, float height);

    /// <summary>
    /// In a match, the briefing map's picture and the tags of this game's team's three drop zones on it, each zone
    /// placed as 1998's briefing marked it; outside a match, none.
    /// </summary>
    MCZoneMap ZoneTags();

    /// <summary>A tag's picture, as the app's art names it (<c>art/zone_tag_F1_br.png</c>; its HD copy in art/hd).</summary>
    std::string ZoneTagSource(const MCZoneTag& tag);

    /// <summary>A match's player as the screens before the battle show them (the bar's second row, the loading screen).</summary>
    struct MCMatchPlayer
    {
        uint32_t Id = 0;
        std::string Name;
        /// <summary>The name's colour class (the chat's: MCChatView::ColourOf).</summary>
        std::string Colour;
        /// <summary>This game's own player.</summary>
        bool Me = false;
        /// <summary>On this game's player's team.</summary>
        bool Ours = false;
        /// <summary>BEGIN MISSION pressed (the players' lights green).</summary>
        bool Ready = false;
    };

    /// <summary>A match's players, this game's team first, each team by its slots; none outside a match.</summary>
    std::vector<MCMatchPlayer> MatchPlayers();

    /// <summary>
    /// For the job of a change into the campaign's screens, from its press: the screen's document read and laid out
    /// (MCRmlFlow::ReadAhead), so <see cref="Show"/> once the screen is dark finds it ready; none when it is read already.
    /// </summary>
    std::vector<MCScreenChange::MCPiece> Ahead();

    /// <summary>
    /// The screen shown, set up for the mission now: on BRIEFING, or the tab it was left on when it comes back
    /// (<see cref="ComeBack"/>). Port-only (stage 9): where it can't be read, the game stops (MCRmlFlow::Broken).
    /// </summary>
    void Show();

    /// <summary>
    /// Port-only, for a change's dark: <see cref="Show"/> as pieces (the screen set up, laid out, shown), each a frame of
    /// its own.
    /// </summary>
    std::vector<MCScreenChange::MCPiece> ShowPieces();

    /// <summary>
    /// The next <see cref="Show"/> is the way back to the screen (the main menu's RETURN TO GAME, SAVE GAME's "Game
    /// saved."): the tab it was left on.
    /// </summary>
    void ComeBack();

    /// <summary>The screen hidden (another screen comes up, the loading screen).</summary>
    void Hide();

    /// <summary>The screen, while the menus have it.</summary>
    MCOptionalRef<MCCampaignScreen> Screen();

    /// <summary>Whether it shows, and whether its first frame is whole (the flow's "campaign" kind's).</summary>
    bool Shown();
    bool Ready();

    /// <summary>The words: a refusal's under its card, the weight's (each given <see cref="TonsText"/>'s words).</summary>
    inline constexpr std::string_view TooHeavyFormat = "Too heavy: {} over";
    inline constexpr std::string_view NoSlotText = "No open slot";
    inline constexpr std::string_view OfFormat = "of {}";
    inline constexpr std::string_view FreeFormat = "{} free";
    inline constexpr std::string_view OverFormat = "{} over";

    /// <summary><paramref name="tons"/> in words: "1 ton", "25 tons".</summary>
    std::string TonsText(int32_t tons);
    /// <summary><paramref name="units"/> in words: "1 unit", "20 units".</summary>
    std::string UnitsText(int32_t units);
    /// <summary>A picture's source as a card's decorator (none for none).</summary>
    std::string Pictured(std::string_view source);
    /// <summary>A MechWarrior's picture (its file, as the logistics data names it) as an <c>art:</c> source.</summary>
    std::string PilotSource(std::string_view picture);
    /// <summary>The number after <paramref name="prefix"/> in an element's id (slot3, force12); none without one.</summary>
    std::optional<size_t> NumberAfter(std::string_view id, std::string_view prefix);
    /// <summary>The eyebrow over a campaign mission's title: its operation and its number in it.</summary>
    inline constexpr std::string_view EyebrowFormat = "{} · Mission {}";
    /// <summary>The operation's video's eyebrow: the operation's number and name.</summary>
    inline constexpr std::string_view VideoEyebrowFormat = "Operation {} · {}";

    /// <summary>The mission logistics has now as its briefing and the campaign name it (BRIEFING's, the loading screen's).</summary>
    struct MCMissionRead
    {
        /// <summary>Its name (the menus' for it, else as its briefing writes it).</summary>
        std::string Title;
        /// <summary>
        /// A campaign mission's operation (its name and number) and its number in it; a solo mission's operation by
        /// name alone (the numbers 0); a match's nothing (its COMMAND DESIGNATION names no map).
        /// </summary>
        std::string OperationName;
        int32_t Operation = 0;
        int32_t Mission = 0;
        MCBriefingText::MCRead Briefing;
        /// <summary>Its briefing map's picture (map:, the scenario's); empty with none.</summary>
        std::string MapSource;
    };

    MCMissionRead ReadMission();
}

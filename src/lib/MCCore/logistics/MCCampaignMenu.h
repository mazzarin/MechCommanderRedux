#pragma once

// Port-only (SPEC-menus.md's stage 8, step 1): the campaign's screens remade, BRIEFING and DEPLOY, one RmlUi screen
// (ui/campaign.rml) under the bar every campaign screen shares (MAIN MENU, the four tabs, RP, the clock): BRIEFING reads
// the mission (its map, title, objectives, fire support, orders and warnings: logistics/MCBriefingText), DEPLOY chooses
// the force (READY AREA's cards, the drop's three groups, the weight and BEGIN MISSION: logistics/MCDeploy's rules, the
// 1998 briefing's own). MCLogistics::SetUpBriefingScreen shows it over the 1998 briefing, which is set up as ever and
// hidden, staying logistics' screen; PURCHASING and the 'MECH BAY are the 1998 screens until step 2, their tabs and
// BRIEFING buttons going through a change of screen (gui/MCScreenFlow.h). The flow's "campaign" kind
// (logistics/MCMenuScreens) shows while this does.

#include "logistics/MCBriefingText.h"
#include "logistics/MCDeploy.h"
#include "ui/MCRmlScreen.h"

namespace Rml
{
    class Event;
}

class MCLogPart;
class MCRmlModel;

/// <summary>BRIEFING and DEPLOY.</summary>
class MCCampaignScreen final : public MCRmlScreen
{
public:
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

    /// <summary>The screen set up for the mission logistics has now, on tab <paramref name="tab"/> (BRIEFING or DEPLOY).</summary>
    void SetUp(MCTab tab);

    MCTab Tab() const { return static_cast<MCTab>(_Tab); }
    /// <summary>A tab pressed: BRIEFING and DEPLOY here, PURCHASING and the 'MECH BAY the 1998 screens, by a change.</summary>
    void ChooseTab(MCTab tab);

    /// <summary>What shows (the tests read it).</summary>
    const std::string& Eyebrow() const { return _Eyebrow; }
    const std::string& Title() const { return _Title; }
    const std::vector<MCLine>& Objectives() const { return _Objectives; }
    const std::string& FireSupport() const { return _Fire; }
    const std::vector<MCLine>& Orders() const { return _Orders; }
    const std::vector<MCLine>& Warnings() const { return _Warnings; }
    const std::string& MapSource() const { return _MapSource; }
    const std::vector<MCCard>& ReadyCards() const { return _Ready; }
    const std::vector<MCGroup>& Groups() const { return _Groups; }
    const std::vector<MCSegment>& Ramp() const { return _Ramp; }
    const std::string& WeightNow() const { return _WeightNow; }
    const std::string& WeightFree() const { return _WeightFree; }
    bool CanBegin() const { return _CanBegin; }
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
    /// <summary>The card of <paramref name="part"/> (in READY AREA, or in slot <paramref name="slot"/>).</summary>
    MCCard UnitCard(const MCLogPart& part, int slot) const;
    MCWeight Weighed() const;
    /// <summary>The keys the hint line names, as the tab and the carry stand.</summary>
    void SetKeys();
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
    static std::optional<std::pair<bool, size_t>> CardOf(const Rml::Element* element);
    /// <summary>The carried card's place over the drop (pixels from its corner).</summary>
    void PlaceCarried();
    /// <summary>The ramp's lit and cyan lengths moved along to the weight's (its timings); whether a segment changed.</summary>
    bool StepRamp(double now);
    /// <summary>A press, release or move of the mouse over the document.</summary>
    void OnMouse(Rml::Event& event);

    std::unique_ptr<MCRmlModel> _Model;
    std::unique_ptr<MCEvents> _Events;
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
    std::string _Fire;
    std::vector<MCLine> _Orders;
    std::string _WarningsHead;
    std::vector<MCLine> _Warnings;
    std::string _MapSource;
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
};

namespace MCCampaignMenu
{
    /// <summary>
    /// Port-only, for the tests: whether the campaign's screens are the remade ones (on in the game; the tests that
    /// click the 1998 briefing turn it off until step 2 records them again).
    /// </summary>
    bool& Remade();

    /// <summary>Whether the remade screen will show the next briefing (on, and the menus' RmlUi can start).</summary>
    bool Wanted();

    /// <summary>
    /// The screen shown over the 1998 briefing, set up for the mission now: on BRIEFING, or the tab it was left on when
    /// the 1998 PURCHASING or 'MECH BAY's BRIEFING button comes back (<see cref="ComeBack"/>); false when it can't show.
    /// </summary>
    bool Show();

    /// <summary>The next <see cref="Show"/> is the way back from the 1998 PURCHASING or 'MECH BAY: the tab it was left on.</summary>
    void ComeBack();

    /// <summary>The screen hidden (an old screen comes up, the loading screen).</summary>
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

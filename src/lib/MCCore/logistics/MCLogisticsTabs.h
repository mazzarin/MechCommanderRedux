#pragma once

// Port-only (SPEC-menus.md's stage 8, step 2): PURCHASING and the 'MECH BAY remade, two tabs of the campaign's screen
// (logistics/MCCampaignMenu, ui/campaign.rml), which binds their values into its model and hands them its mouse, keys
// and frames while one shows. On both the left column is the INVENTORY (1998's word: what the player owns outside the
// force), its switch MECHS (the mechs and vehicles stored), MECHWARRIORS (those no mech carries) and COMPONENTS.
// PURCHASING: the store beside it (MECHS, MECHWARRIORS, COMPONENTS, VEHICLES, the INVENTORY's switch following it), the
// details of what is under the pointer, else what was picked, and its BUY or SELL; the buy (or sell) window is 1998's
// purchase dialog (each, how many, the total, the RP left). The 'MECH BAY: the force beside it ("11 / 15"), and the
// lab, the unit picked: its outline coloured for damage, the repairs as sliders moving point by point as 1998's knobs,
// its MechWarrior (or its empty seat), its payload, REPAIR ALL and REFIT ALL (a stored unit's INTO THE FORCE). Every
// rule is 1998's: logistics/MCStore and logistics/MCMechBay, which the 1998 screens' blocks call too; the messages are
// 1998's (shown in the remade dialog). The right button (and ENTER on the keys' focus) does each card's one move; the
// left button picks, or drags a card to where it goes.

#include "logistics/MCCampaignMenu.h"
#include "logistics/MCMechBay.h"

#include <RmlUi/Core/Input.h>

namespace Rml
{
    class Element;
    class Event;
}

class MCLogPart;
class MCLogVehicle;
class MCLogWarrior;
class MCPurMech;
class MCPurPilotData;
class MCPurVehicle;
class MCRmlModel;
struct MCLogInventoryItem;

class MCLogisticsTabs
{
public:
    /// <summary>
    /// PURCHASING's switch: what the store lists (and the INVENTORY with it: VEHICLES shows its MECHS).
    /// </summary>
    enum class MCKind : uint8_t
    {
        Mechs,
        MechWarriors,
        Components,
        Vehicles,
    };

    /// <summary>
    /// The INVENTORY's switch: the mechs and vehicles stored (outside the force), the MechWarriors no mech carries, the
    /// spare parts. The 'MECH BAY's own; PURCHASING's follows the store's kind.
    /// </summary>
    enum class MCStock : uint8_t
    {
        Mechs,
        MechWarriors,
        Components,
    };

    /// <summary>
    /// Where a card is: the force, the INVENTORY's MECHS (a unit stored outside the force), the store, the INVENTORY's
    /// MECHWARRIORS or COMPONENTS (its goods), the lab's fitted parts and MechWarrior; a FORCE card's MechWarrior, by
    /// their photo (its index the card's: the user's, 26 Nov, "pick the pilot out of the mech").
    /// </summary>
    enum class MCPlace : uint8_t
    {
        Force,
        Stored,
        Store,
        Stock,
        Fitted,
        Pilot,
        Aboard,
    };

    /// <summary>A line of the details' stats, a MechWarrior's skill (its bar's length the value, of 100).</summary>
    struct MCStat
    {
        std::string Label;
        std::string Value;
    };

    /// <summary>
    /// A store 'Mech's variant: a row of the details' VARIANT (its letter, its word, what it changes in the game's own
    /// words, how many are left), and a pip under its store card's picture.
    /// </summary>
    struct MCVariantRow
    {
        /// <summary>Its place among the store 'Mech's variants (1998's button: 0 A, 1 W, 2 J), as ChooseVariant takes it.</summary>
        int Variant = 0;
        std::string Letter;
        std::string Word;
        std::string Says;
        std::string Left;
        /// <summary>The one the store 'Mech shows (its row teal, its pip cyan while any are left).</summary>
        bool On = false;
        /// <summary>None left: its row dim, its pip an empty box.</summary>
        bool Off = false;
    };

    /// <summary>A thing as a card (the store's goods, the INVENTORY's parts and MechWarriors).</summary>
    struct MCGood
    {
        std::string Picture{MCCampaignScreen::NoPicture};
        std::string Who;
        std::string What;
        /// <summary>A 'Mech's variant letter, in a chip after its chassis (Who).</summary>
        std::string Letter;
        /// <summary>A store 'Mech's variants, as pips under its picture (none for anything else).</summary>
        std::vector<MCVariantRow> Variants;
        /// <summary>A MechWarrior in the INVENTORY: their health (pips kept, of six) over their portrait.</summary>
        bool Warrior = false;
        int Health = 0;
        /// <summary>An INVENTORY MechWarrior's four skills, as bars beside their name (the lab's pilot's, in 1998's order).</summary>
        std::vector<MCStat> Skills;
        /// <summary>Its price or count (cream), and the word after it (grey: "2 LEFT", "OWNED").</summary>
        std::string Price;
        std::string Note;
        /// <summary>Out of reach (dim): why, in its last line's place.</summary>
        std::string Why;
        bool Off = false;
        bool Picked = false;
        /// <summary>A part's picture (its square icon).</summary>
        bool Part = false;
        /// <summary>A name longer than the frames' (LongName): set smaller to fit its card.</summary>
        bool Long = false;
    };

    /// <summary>A row of the details' payload: how many, the part, and DAMAGED for an owned 'Mech's damaged copies.</summary>
    struct MCKitRow
    {
        int Count = 0;
        std::string Name;
        std::string State;
    };

    struct MCFitRow
    {
        std::string Range;
        std::string Name;
        std::string Tons;
        std::string State;
    };

    /// <summary>A repair's line on the lab: what it stands at, what the rest costs, the slider's place and floor.</summary>
    struct MCRepair
    {
        std::string Now;
        std::string Cost;
        int Knob = MCMechBay::KnobLeft;
    };

    explicit MCLogisticsTabs(MCCampaignScreen& screen);
    ~MCLogisticsTabs();
    MCLogisticsTabs(const MCLogisticsTabs&) = delete;
    MCLogisticsTabs& operator=(const MCLogisticsTabs&) = delete;

    /// <summary>The values bound into the campaign's model, kept until <see cref="Unbind"/>.</summary>
    void Bind(MCRmlModel& model);
    /// <summary>The model let go of: the campaign's goes as its screen is destroyed, before these tabs.</summary>
    void Unbind() { _Model.reset(); }

    /// <summary>PURCHASING (or the 'MECH BAY) shown: everything read afresh.</summary>
    void Show(MCCampaignScreen::MCTab tab);
    /// <summary>The tab left (or the screen hidden): a deal or a drag dropped, nothing kept.</summary>
    void Leave();
    /// <summary>A frame: the lists read again when the data changed; whether anything shown changed.</summary>
    bool OnFrame();
    /// <summary>A press, release or move of the mouse over the document.</summary>
    void OnMouse(Rml::Event& event);
    /// <summary>A key before RmlUi's: whether it was taken (<paramref name="focus"/> the keys' focus).</summary>
    bool OnKey(int key, MCOptionalRef<const Rml::Element> focus);
    /// <summary>Escape: the deal window closed, or a drag put back; false with neither.</summary>
    bool Back();
    /// <summary>The hint line's keys as the tab stands.</summary>
    std::vector<MCRmlKeyHint> Keys() const;

    // The controls' actions (the tests press them).
    void ChooseKind(MCKind kind);
    void ChooseStock(MCStock stock);
    void ChooseVariant(int variant);
    /// <summary>The left button's click on a card: shown in the details or the lab.</summary>
    void Pick(MCPlace place, size_t index);
    /// <summary>The right button (ENTER) on a card: its one move (buy, sell, in or out of the force, fit, take out).</summary>
    void Send(MCPlace place, size_t index);
    /// <summary>The lab's INTO THE FORCE: the stored unit it shows joins the force, still picked.</summary>
    void IntoTheForce();
    /// <summary>
    /// A card dragged from <paramref name="from"/> and let go over <paramref name="onto"/> (over its card
    /// <paramref name="card"/>, if over one: a FORCE card a MechWarrior boards).
    /// </summary>
    void Drop(MCPlace from, size_t index, std::optional<MCPlace> onto, std::optional<size_t> card = std::nullopt);
    /// <summary>The details' BUY or SELL.</summary>
    void PressDeal();
    /// <summary>The deal window's HOW MANY (from one).</summary>
    void DealCount(int count);
    void DealConfirm();
    void DealCancel();
    /// <summary>A repair slider moved to <paramref name="position"/> (1998's knob pixels, KnobLeft to KnobRight).</summary>
    void MoveSlider(MCMechBay::MCKnob knob, int position);
    void RepairAll();
    void RefitAll();
    /// <summary>
    /// The FORCE's REPAIR FORCE (the user's, 26 Nov): each force 'Mech's REPAIR ALL in the force's order, at its cost, as
    /// far as the resource points go (short, 1998's message, as REPAIR ALL's).
    /// </summary>
    void RepairForce();

    /// <summary>What shows (the tests read it).</summary>
    MCKind Kind() const { return static_cast<MCKind>(_Kind); }
    /// <summary>What the INVENTORY shows (PURCHASING: its store's kind's).</summary>
    MCStock Stock() const { return static_cast<MCStock>(_StockKind); }
    const std::vector<MCCampaignScreen::MCCard>& ForceCards() const { return _Force; }
    /// <summary>The INVENTORY's MECHS: the units stored outside the force.</summary>
    const std::vector<MCCampaignScreen::MCCard>& StoredCards() const { return _Stored; }
    /// <summary>The force's count against the most it holds ("11 / 15"), and the INVENTORY's ("3 mechs").</summary>
    const std::string& ForceCount() const { return _ForceCount; }
    const std::string& StockCount() const { return _StockCount; }
    const std::vector<MCGood>& StoreGoods() const { return _Store; }
    const std::vector<MCGood>& StockGoods() const { return _StockGoods; }
    const std::vector<MCFitRow>& FittedRows() const { return _Fits; }
    const std::string& DetailsName() const { return _InfoName; }
    /// <summary>The details' button (BUY or SELL) and the buy window's title (what it does).</summary>
    const std::string& DealWord() const { return _DealWord; }
    const std::string& DealTitle() const { return _DealTitle; }
    bool DealOpen() const { return _DealOpen; }
    int DealCountShown() const { return _Deal.has_value() ? _Deal->Count : 0; }
    const std::string& DealTotal() const { return _DealTotal; }
    const std::string& LabName() const { return _LabName; }
    const std::string& LabWhat() const { return _LabWhat; }
    /// <summary>Whether the lab shows a stored unit (INTO THE FORCE at its foot).</summary>
    bool LabStored() const { return _LabStored; }
    const MCRepair& ArmorRepair() const { return _Armor; }
    const MCRepair& StructureRepair() const { return _Structure; }
    const MCRepair& EngineRepair() const { return _Engine; }
    /// <summary>The unit the lab shows, the unit or thing picked.</summary>
    std::shared_ptr<MCLogPart> LabUnit() const { return _LabPart.lock(); }
    /// <summary>The store 'Mech's variants as the details' VARIANT rows show them.</summary>
    const std::vector<MCVariantRow>& VariantRows() const { return _VariantRows; }
    /// <summary>The unit a card stands for (<paramref name="place"/> Force or Stored), if any.</summary>
    std::shared_ptr<MCLogPart> UnitAt(MCPlace place, size_t index) const;
    /// <summary>Whether the lab's 'Mech can be repaired here (a force 'Mech's, outside a match): sliders, not meters.</summary>
    bool Repairs() const { return _Repairs; }
    /// <summary>REPAIR FORCE: whether anything in the force needs it, and its label (with what it costs).</summary>
    bool CanRepairForce() const { return _CanRepairForce; }
    const std::string& RepairForceWord() const { return _RepairForceWord; }
    /// <summary>The details' payload (PURCHASING): its rows.</summary>
    const std::vector<MCKitRow>& KitRows() const { return _Kit; }

private:
    /// <summary>What a card stands for: a unit, a store's good, a spare part or a MechWarrior (one is set).</summary>
    struct MCThing
    {
        std::weak_ptr<MCLogPart> Unit;
        std::weak_ptr<MCPurMech> StoreMech;
        std::weak_ptr<MCPurPilotData> StorePilot;
        std::weak_ptr<MCPurVehicle> StoreVehicle;
        /// <summary>A part: the store's (Store) or a spare (Stock); a fitted one is its mech's item and copy.</summary>
        std::weak_ptr<MCLogInventoryItem> Part;
        std::weak_ptr<MCLogWarrior> Warrior;
        uint8_t ItemNum = 0;
        MCPlace Place = MCPlace::Force;

        bool Same(const MCThing& other) const;
        bool Gone() const;
    };

    /// <summary>The deal window's: what, its price each (positive), how many at most, and 1998's dialog type.</summary>
    struct MCDeal
    {
        MCThing Thing;
        bool Sale = false;
        int32_t Each = 0;
        int32_t Most = 1;
        int32_t Count = 1;
        /// <summary>1998's purchase type: the arrows work for purchases and a part's sale.</summary>
        int32_t Type = 0;
    };

    /// <summary>A card held by the mouse: where from, which, and whether it moved far enough to be a drag.</summary>
    struct MCHold
    {
        MCPlace From = MCPlace::Force;
        size_t Index = 0;
        float StartX = 0;
        float StartY = 0;
        bool Dragging = false;
    };

    bool Purchasing() const { return _Screen.Tab() == MCCampaignScreen::MCTab::Purchasing; }
    /// <summary>
    /// What the INVENTORY shows: PURCHASING's by its store's kind, the 'MECH BAY's by its own switch.
    /// </summary>
    MCStock Shelf() const;
    /// <summary>The lists made again from the logistics data; the details and the lab with them.</summary>
    void Fill();
    void FillUnits();
    void FillStore();
    void FillStock();
    void FillDetails();
    void FillLab();
    void FillDeal();
    /// <summary>The kinds', the INVENTORY's and the variants' switches marked at what the lists show.</summary>
    void MarkSwitches() const;
    /// <summary>A fingerprint of everything the lists show (a buy, a sale, a cheat, a teammate's unit).</summary>
    uint64_t Fingerprint() const;
    /// <summary>The thing a card at <paramref name="place"/> <paramref name="index"/> stands for.</summary>
    std::optional<MCThing> ThingAt(MCPlace place, size_t index) const;
    /// <summary>The card the element is in (its id: force3, store0), if any.</summary>
    static std::optional<std::pair<MCPlace, size_t>> CardOf(MCOptionalRef<const Rml::Element> element);
    /// <summary>Where a drag is let go: the column, and the card under it if any (its index in its list).</summary>
    struct MCSpot
    {
        MCPlace Place = MCPlace::Force;
        std::optional<size_t> Card;
    };

    /// <summary>
    /// The column a point (pixels) is over: the force, the INVENTORY (Stored with its MECHS shown, else Stock), the
    /// store, the lab; with the card under it.
    /// </summary>
    std::optional<MCSpot> SpotAt(float x, float y) const;
    /// <summary>
    /// The FORCE card a held MechWarrior (an INVENTORY card's, the lab's, a FORCE card's photo) would board at a point
    /// (pixels): its edge cyan while held there, as DEPLOY's open slot.
    /// </summary>
    std::optional<size_t> LandingAt(float x, float y) const;
    /// <summary>The deal window opened for <paramref name="thing"/>: a buy from the store, else a sale.</summary>
    void OfferDeal(const MCThing& thing);
    /// <summary>
    /// A force card's unit out of the force to the INVENTORY, a stored one into it (and picked): the 'MECH BAY's right
    /// button and drags.
    /// </summary>
    void MoveUnit(const MCThing& thing);
    /// <summary>A unit in or out of the force (1998's drag between the force and the inventory).</summary>
    /// <returns>Whether it joined (else the force is full, said as 1998's).</returns>
    bool IntoForce(MCLogPart& unit);
    void OutOfForce(MCLogPart& unit);
    /// <summary>A part fitted into the lab's mech, a part taken out to the spares, a MechWarrior boarding or leaving.</summary>
    void FitPart(MCLogInventoryItem& part);
    void TakeOutPart(size_t fitted);
    /// <summary>
    /// <paramref name="warrior"/> (a spare) boards <paramref name="mech"/>, the MechWarrior aboard it to the INVENTORY
    /// first; a MechWarrior leaves <paramref name="mech"/> for the INVENTORY; one moves from a 'Mech into another.
    /// </summary>
    void BoardPilot(MCLogWarrior& warrior, MCLogMech& mech);
    void PilotLeaves(MCLogMech& mech);
    void MovePilot(MCLogMech& from, MCLogMech& to);
    /// <summary>The 'Mech of FORCE card <paramref name="index"/> (none for a vehicle's).</summary>
    std::shared_ptr<MCLogMech> ForceMechAt(size_t index) const;
    /// <summary>The 'Mech a held MechWarrior comes from (the lab's, a FORCE card's photo's); none from the INVENTORY.</summary>
    std::shared_ptr<MCLogMech> PilotSource(MCPlace from, size_t index) const;
    /// <summary>REPAIR FORCE's label and whether it can act, as the force stands.</summary>
    void FillForceRepair();
    /// <summary>
    /// The lab's mech when it is one the player can work on (a force mech, not one stored in the INVENTORY).
    /// </summary>
    std::shared_ptr<MCLogMech> WorkedMech() const;
    void Changed();

    MCCampaignScreen& _Screen;
    MCOptionalRef<MCRmlModel> _Model;
    uint64_t _Fingerprint = 0;
    std::optional<MCThing> _Picked;
    std::optional<MCThing> _Hovered;
    std::optional<MCDeal> _Deal;
    std::optional<MCHold> _Hold;
    /// <summary>The FORCE card a held MechWarrior would board (edged cyan).</summary>
    std::optional<size_t> _Landing;
    std::weak_ptr<MCLogPart> _LabPart;
    /// <summary>The lab mech's knobs (where it arrived), read as it is picked.</summary>
    MCMechBay::MCKnobs _Knobs;
    std::vector<MCThing> _ForceThings;
    std::vector<MCThing> _StoredThings;
    std::vector<MCThing> _StoreThings;
    std::vector<MCThing> _StockThings;
    std::vector<MCThing> _FitThings;
    /// <summary>A refused move's reason under its card, and when it goes.</summary>
    std::optional<MCThing> _WhyThing;
    std::string _WhyText;
    double _WhyUntil = 0;

    // The model's values.
    int _Kind = 0;
    /// <summary>What the INVENTORY shows (an MCStock), and the 'MECH BAY's choice.</summary>
    int _StockKind = 0;
    MCStock _BayStock = MCStock::Components;
    std::string _ForceCount;
    std::string _StockCount;
    std::string _StoreCount;
    std::vector<MCCampaignScreen::MCCard> _Force;
    std::vector<MCCampaignScreen::MCCard> _Stored;
    std::vector<MCGood> _Store;
    std::vector<MCGood> _StockGoods;
    /// <summary>The card a drag holds (one or none), and where it is (pixels).</summary>
    std::vector<MCGood> _Lifted;
    std::string _LiftedLeft;
    std::string _LiftedTop;
    bool _InfoShown = false;
    std::string _InfoName;
    std::string _InfoWhat;
    std::string _InfoPicture{MCCampaignScreen::NoPicture};
    /// <summary>A store mech's variants (1998's three: A, W and J), those the store has none of, and the one shown.</summary>
    /// <summary>The details' name's variant letter (an owned 'Mech's; a store 'Mech's VARIANT rows say it).</summary>
    std::string _InfoLetter;
    std::vector<MCVariantRow> _VariantRows;
    std::vector<MCStat> _Stats;
    bool _Rated = false;
    int _Rating = 0;
    std::vector<MCKitRow> _Kit;
    std::string _Description;
    std::string _InfoCost;
    std::string _InfoNote;
    std::string _DealWord;
    bool _CanDeal = false;
    bool _DealOpen = false;
    /// <summary>How many the deal window's stepper goes to.</summary>
    int32_t _DealCountMost = 1;
    std::string _DealTitle;
    std::string _DealPicture{MCCampaignScreen::NoPicture};
    std::string _DealName;
    std::string _DealWhat;
    std::string _DealEach;
    std::string _DealTotal;
    std::string _DealLeft;
    // The lab.
    int _LabKind = 0;
    std::string _LabName;
    /// <summary>The lab's 'Mech's variant letter, in a chip after its chassis.</summary>
    std::string _LabLetter;
    std::string _LabWhat;
    std::string _LabPicture{MCCampaignScreen::NoPicture};
    bool _LabStored = false;
    MCRepair _Armor;
    MCRepair _Structure;
    MCRepair _Engine;
    bool _Repairs = false;
    bool _HasPilot = false;
    /// <summary>A 'Mech without a MechWarrior: its empty seat where the pilot shows.</summary>
    bool _Seat = false;
    std::string _PilotName;
    std::string _PilotRank;
    std::string _PilotPicture{MCCampaignScreen::NoPicture};
    int _PilotHealth = 0;
    std::vector<MCStat> _Skills;
    std::string _PayloadNow;
    std::string _PayloadOf;
    std::vector<MCCampaignScreen::MCSegment> _PayloadRamp;
    std::vector<MCFitRow> _Fits;
    std::string _Free;
    std::string _RepairAllWord;
    bool _CanRepairAll = false;
    bool _CanRefit = false;
    /// <summary>REPAIR FORCE at the FORCE's foot: shown outside a match, its label, and whether it acts.</summary>
    bool _ForceRepairs = false;
    std::string _RepairForceWord;
    bool _CanRepairForce = false;
};

namespace MCLogisticsTabsWords
{
    /// <summary>Why a part doesn't go into the lab's 'Mech, under its INVENTORY card.</summary>
    std::string FitWhy(MCMechBay::MCFit why);
}

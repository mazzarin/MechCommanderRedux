#pragma once

// Port-only (SPEC-menus.md's stage 8, step 2): PURCHASING and the 'MECH BAY remade, two tabs of the campaign's screen
// (logistics/MCCampaignMenu, ui/campaign.rml), which binds their values into its model and hands them its mouse, keys and
// frames while one shows. The left column is the force, then SPARE (1998's inventory: the mechs and vehicles outside the
// force); PURCHASING's store beside it (MECHS, MECHWARRIORS, COMPONENTS, VEHICLES: with MECHWARRIORS or COMPONENTS the
// left column is the INVENTORY's) with the details of what is under the pointer, else what was picked, and its BUY or
// SELL; the buy (or sell) window is 1998's purchase dialog (each, how many, the total, the RP left). The 'MECH BAY's lab
// shows the unit picked: its outline coloured for damage, the repairs as sliders moving point by point as 1998's knobs,
// its MechWarrior, its payload, REPAIR ALL and REFIT ALL; the INVENTORY beside it (COMPONENTS | MECHWARRIORS). Every
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
    /// <summary>PURCHASING's switch: what the store (and with MECHWARRIORS or COMPONENTS, the left column) lists.</summary>
    enum class MCKind : uint8_t
    {
        Mechs,
        MechWarriors,
        Components,
        Vehicles,
    };

    /// <summary>The 'MECH BAY's INVENTORY switch.</summary>
    enum class MCStock : uint8_t
    {
        Components,
        MechWarriors,
    };

    /// <summary>Where a card is: the force, SPARE, the store, the INVENTORY, the lab's fitted parts and MechWarrior.</summary>
    enum class MCPlace : uint8_t
    {
        Force,
        Spare,
        Store,
        Stock,
        Fitted,
        Pilot,
    };

    /// <summary>A thing as a card (the store's goods, the INVENTORY's parts and MechWarriors).</summary>
    struct MCGood
    {
        std::string Picture{MCCampaignScreen::NoPicture};
        std::string Who;
        std::string What;
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

    /// <summary>A line of the details' stats, a row of a payload, a fitted part's row.</summary>
    struct MCStat
    {
        std::string Label;
        std::string Value;
    };

    struct MCKitRow
    {
        int Count = 0;
        std::string Name;
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

    /// <summary>The values bound into the campaign's model.</summary>
    void Bind(MCRmlModel& model);

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
    /// <summary>A card dragged from <paramref name="from"/> and let go over <paramref name="onto"/>.</summary>
    void Drop(MCPlace from, size_t index, std::optional<MCPlace> onto);
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

    /// <summary>What shows (the tests read it).</summary>
    MCKind Kind() const { return static_cast<MCKind>(_Kind); }
    MCStock Stock() const { return static_cast<MCStock>(_StockKind); }
    const std::vector<MCCampaignScreen::MCCard>& ForceCards() const { return _Force; }
    const std::vector<MCCampaignScreen::MCCard>& SpareCards() const { return _Spare; }
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
    const MCRepair& ArmorRepair() const { return _Armor; }
    const MCRepair& StructureRepair() const { return _Structure; }
    const MCRepair& EngineRepair() const { return _Engine; }
    /// <summary>The unit the lab shows, the unit or thing picked.</summary>
    std::shared_ptr<MCLogPart> LabUnit() const { return _LabPart.lock(); }
    /// <summary>The store mech's variant shown (the details' switch).</summary>
    int Variant() const { return _Variant; }
    /// <summary>The unit a card on the left stands for (<paramref name="place"/> Force or Spare), if any.</summary>
    std::shared_ptr<MCLogPart> UnitAt(MCPlace place, size_t index) const;
    /// <summary>Whether the lab's 'Mech can be repaired here (a force 'Mech's, outside a match): sliders, not meters.</summary>
    bool Repairs() const { return _Repairs; }

private:
    /// <summary>What a card stands for: a unit, a store's good or a spare (one is set).</summary>
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
    /// <summary>The column a point (pixels) is over: the force's (Force), SPARE's, the store, the INVENTORY, the lab.</summary>
    std::optional<MCPlace> ColumnAt(float x, float y) const;
    /// <summary>The deal window opened for <paramref name="thing"/>: a buy from the store, else a sale.</summary>
    void OfferDeal(const MCThing& thing);
    /// <summary>A unit in or out of the force (1998's drag between the force and the inventory).</summary>
    void IntoForce(MCLogPart& unit);
    void OutOfForce(MCLogPart& unit);
    /// <summary>A part fitted into the lab's mech, a part taken out to the spares, a MechWarrior boarding or leaving.</summary>
    void FitPart(MCLogInventoryItem& part);
    void TakeOutPart(size_t fitted);
    void BoardPilot(MCLogWarrior& warrior);
    void PilotLeaves();
    /// <summary>The lab's mech when it is one the player can work on (a force mech, outside a match's spares).</summary>
    std::shared_ptr<MCLogMech> WorkedMech() const;
    void Changed();

    MCCampaignScreen& _Screen;
    MCOptionalRef<MCRmlModel> _Model;
    uint64_t _Fingerprint = 0;
    std::optional<MCThing> _Picked;
    std::optional<MCThing> _Hovered;
    std::optional<MCDeal> _Deal;
    std::optional<MCHold> _Hold;
    std::weak_ptr<MCLogPart> _LabPart;
    /// <summary>The lab mech's knobs (where it arrived), read as it is picked.</summary>
    MCMechBay::MCKnobs _Knobs;
    std::vector<MCThing> _ForceThings;
    std::vector<MCThing> _SpareThings;
    std::vector<MCThing> _StoreThings;
    std::vector<MCThing> _StockThings;
    std::vector<MCThing> _FitThings;
    /// <summary>A refused move's reason under its card, and when it goes.</summary>
    std::optional<MCThing> _WhyThing;
    std::string _WhyText;
    double _WhyUntil = 0;

    // The model's values.
    int _Kind = 0;
    int _StockKind = 0;
    /// <summary>The left column: 0 the force and SPARE, 1 the INVENTORY's MechWarriors, 2 its parts (PURCHASING).</summary>
    int _Left = 0;
    std::string _ForceCount;
    std::string _StockCount;
    std::string _StoreCount;
    std::vector<MCCampaignScreen::MCCard> _Force;
    std::vector<MCCampaignScreen::MCCard> _Spare;
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
    int _Variants = 0;
    bool _VariantOff0 = false;
    bool _VariantOff1 = false;
    bool _VariantOff2 = false;
    int _Variant = 0;
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
    std::string _LabWhat;
    std::string _LabPicture{MCCampaignScreen::NoPicture};
    MCRepair _Armor;
    MCRepair _Structure;
    MCRepair _Engine;
    bool _Repairs = false;
    bool _HasPilot = false;
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
};

namespace MCLogisticsTabsWords
{
    /// <summary>Why a part doesn't go into the lab's 'Mech, under its INVENTORY card.</summary>
    std::string FitWhy(MCMechBay::MCFit why);
}

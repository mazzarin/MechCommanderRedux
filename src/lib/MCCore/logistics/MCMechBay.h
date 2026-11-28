#pragma once

// Port-only (SPEC-menus.md's stage 8): the 'MECH BAY's rules, the original's (logistics\logrep.cpp's RepairScreen and
// mrblock.cpp's MechRepairBlock, the inventory blocks' drops on it), in one place both the 1998 repair screen's blocks
// and the remade 'MECH BAY (logistics/MCLogisticsTabs) call: the repair knobs (armour, internal structure, the engine: where
// the mech arrived is the floor, a knob moved back refunds), REPAIR ALL (the engine, then structure, then armour, as
// far as the resource points go), the refit (each damaged part swapped for a spare, what has none stripped), what
// fits in a mech and why not, a part fitted, taken out and put back, the order a mech's fitted parts are listed in, a
// MechWarrior boarding and leaving, a unit joining and leaving the force. The sounds, the words and the screens' rows
// are the caller's.

#include "main/MCLogMech.h"

class MCLogVehicle;
class MCLogWarrior;
struct MCLogInventoryItem;
struct MCLogInventoryStat;

namespace MCMechBay
{
    /// <summary>
    /// The master components a mech can only mount when its name index is 5, 0xe or 0x10 (a game rule:
    /// <see cref="WhyNotFit"/>).
    /// </summary>
    inline constexpr std::array<uint8_t, 5> RestrictedComps = {15, 37, 38, 42, 43};

    /// <summary>
    /// The most units the force holds (a game rule: the original turns back the unit just assigned to it past this,
    /// <see cref="ForceFull"/>).
    /// </summary>
    inline constexpr int32_t ForceUnits = 0xf;

    /// <summary>A repair knob.</summary>
    enum class MCKnob : uint8_t
    {
        Armor,
        Internal,
        Engine
    };

    /// <summary>The knobs' track: the original's 61 pixels (a knob's position), empty to whole.</summary>
    inline constexpr int32_t KnobLeft = 0xea;
    inline constexpr int32_t KnobRight = 0x127;

    /// <summary>The most engine damage the knob shows (an engine past it is set to it).</summary>
    inline constexpr uint8_t MostEngineHits = 3;

    /// <summary>The engine knob's place for engine damage <paramref name="hits"/> (0 whole, to 3).</summary>
    int32_t EngineKnob(uint8_t hits);

    /// <summary>Where a mech's knobs start (where it arrived) and how many track pixels a point takes.</summary>
    struct MCKnobs
    {
        float ArmorScale = 0.0f;
        int32_t ArmorStart = 0;
        float InternalScale = 0.0f;
        int32_t InternalStart = 0;
        int32_t EngineStart = 0;
    };

    /// <summary>Takes <paramref name="mech"/> out of the drop, as every change in the 'MECH BAY does first.</summary>
    void OutOfDrop(MCLogMech& mech);

    /// <summary>The current (or maximum) points of <paramref name="points"/> added up.</summary>
    int32_t Points(std::span<const MCLogMech::ArmorPoints> points, bool maximum);

    /// <summary>The engine's damage record in <paramref name="mech"/>'s inventory.</summary>
    std::shared_ptr<MCLogInventoryStat> EngineOf(const MCLogMech& mech);

    /// <summary><paramref name="mech"/>'s knobs' floors and scales, from where it arrived (<see cref="MCLogMech::Arrived"/>).</summary>
    MCKnobs KnobsOf(const MCLogMech& mech);

    /// <summary>Where <paramref name="knob"/> of <paramref name="mech"/> is (<see cref="MCLogMech::RepairSliders"/>).</summary>
    int32_t KnobAt(const MCLogMech& mech, MCKnob knob);

    /// <summary>
    /// Puts <paramref name="mech"/>'s three knobs at its engine damage, armour and internal structure, as the original's
    /// block did when it was made (when the mech joins the lists). An engine past damage level 3 is set to 3.
    /// </summary>
    void PlaceKnobs(MCLogMech& mech);

    /// <summary>Puts one knob of <paramref name="mech"/> at its level (after a repair of everything).</summary>
    void PlaceKnob(MCLogMech& mech, MCKnob knob);

    /// <summary>
    /// Moves <paramref name="knob"/> of <paramref name="mech"/> to <paramref name="position"/>, as the original's drag did
    /// on every step: the mech out of the drop, what the knob repaired since it arrived undone and refunded, then repaired
    /// up to the position as far as the resource points go (the engine by whole levels), the battle rating worked out.
    /// </summary>
    /// <returns>Where the drag now is (the original's last position), or none when the engine's level is out of reach.</returns>
    std::optional<int32_t> MoveKnob(MCLogMech& mech, const MCKnobs& knobs, MCKnob knob, int32_t position);

    /// <summary>What repairing the rest of <paramref name="knob"/>'s points (or the engine's levels) costs.</summary>
    int32_t RestCost(const MCLogMech& mech, MCKnob knob);

    /// <summary>Whether the engine, the internal structure or the armour is damaged.</summary>
    bool StructureDamaged(const MCLogMech& mech);

    /// <summary>
    /// Repairs <paramref name="points"/> of <paramref name="mech"/>'s armour points (all of it when negative): the head
    /// first, then one point at a time to the location most damaged (the centre torso, the rear and the cockpit count
    /// as more damaged).
    /// </summary>
    void RepairArmor(MCLogMech& mech, int32_t points);

    /// <summary>
    /// Repairs <paramref name="points"/> of <paramref name="mech"/>'s internal structure points (all of it when
    /// negative), most damaged first.
    /// </summary>
    void RepairInternal(MCLogMech& mech, int32_t points);

    /// <summary>
    /// REPAIR ALL: the mech out of the drop, the engine level by level, then the internal structure, then the armour, as
    /// far as the resource points go; the knobs placed at the levels.
    /// </summary>
    /// <returns>Whether the points ran short (the original's message 0x57).</returns>
    bool RepairAll(MCLogMech& mech);

    /// <summary>
    /// Whether any of <paramref name="mech"/>'s weapons, equipment or jammers is damaged: REFIT ALL works only then
    /// (1998's MCMechRepairBlock::ItemsDamaged, its button's CanRepairItems).
    /// </summary>
    bool ItemsDamaged(const MCLogMech& mech);

    /// <summary>
    /// REFIT ALL: the mech out of the drop, the other damage (not the engine) cleared, each damaged weapon or piece of
    /// equipment swapped for a spare; the spares used, in order, go to <paramref name="used"/>.
    /// </summary>
    /// <returns>The names of the damaged parts with no spare, comma-separated (the original's refit question).</returns>
    std::string Refit(MCLogMech& mech, std::vector<std::shared_ptr<MCLogInventoryItem>>& used);

    /// <summary>The refit question's OK: every damaged weapon or piece of equipment with no spare leaves the mech.</summary>
    void StripUnrepaired(MCLogMech& mech);

    /// <summary>One of the store's parts a refit buys: how many, at its price.</summary>
    struct MCRefitBuy
    {
        std::shared_ptr<MCLogInventoryItem> StorePart;
        int32_t Count = 0;
        int32_t Each = 0;
    };

    /// <summary>A mech's damaged parts a refit can't replace (no spare, none in the store): they come off.</summary>
    struct MCRefitDrop
    {
        std::shared_ptr<MCLogMech> Mech;
        std::vector<std::string> Parts;
    };

    /// <summary>
    /// Port-only (the user's, 27 Nov: REFIT FORCE "also offers to buy what you need from the store ... a price then an
    /// itemized list of what mechs would drop components because they aren't available in the store"; REFIT ALL the
    /// same): what refitting <paramref name="mechs"/>, in order, takes. Each damaged weapon or piece of equipment from a
    /// spare while one is left, else bought from the store while it has one (a part it has no limit of always), else it
    /// comes off.
    /// </summary>
    struct MCRefitPlan
    {
        /// <summary>Whether any of the mechs has a damaged weapon, piece of equipment or jammer (<see cref="ItemsDamaged"/>).</summary>
        bool Damaged = false;
        std::vector<MCRefitBuy> Buys;
        /// <summary>What the buys cost.</summary>
        int32_t Cost = 0;
        std::vector<MCRefitDrop> Drops;
    };

    MCRefitPlan PlanRefit(const std::vector<std::shared_ptr<MCLogMech>>& mechs);

    /// <summary>The plan's buys: each part into the spares, the store's count down and the resource points paying.</summary>
    void BuyForRefit(const MCRefitPlan& plan);

    /// <summary>Why a part can't go into a mech.</summary>
    enum class MCFit : uint8_t
    {
        /// <summary>It fits.</summary>
        None,
        /// <summary>No mech is picked.</summary>
        NoMech,
        /// <summary>More tons than the mech has free (the original's message 99).</summary>
        TooHeavy,
        /// <summary>One ECM, sensor or probe a mech, and it has one.</summary>
        OneFitted,
        /// <summary>A match's weapon bans ban it.</summary>
        Banned,
        /// <summary>Only a few mechs take it.</summary>
        NotThisMech
    };

    /// <summary>What a part weighs fitted, with its ammunition.</summary>
    float FittedTons(uint8_t masterID);

    /// <summary>
    /// A weapon's long range under this is short range, under <see cref="MediumRangeUnder"/> medium, else long (the
    /// original's lists, range words and colours).
    /// </summary>
    inline constexpr float ShortRangeUnder = 76.0f;
    inline constexpr float MediumRangeUnder = 151.0f;

    /// <summary>The lists of a mech's fitted parts, in the order the 'MECH BAY shows them (1998's weapon list's).</summary>
    enum class MCFitList : uint8_t
    {
        ShortRange,
        MediumRange,
        LongRange,
        Equipment
    };

    /// <summary>
    /// The list the part <paramref name="masterID"/> is in: a weapon's by its long range (<see cref="ShortRangeUnder"/>,
    /// <see cref="MediumRangeUnder"/>), equipment's; none for the rest (ammunition, the engine), which no list shows.
    /// </summary>
    std::optional<MCFitList> FitListOf(uint8_t masterID);

    /// <summary>
    /// The order a weapon list shows weapons of <paramref name="damages"/> in, each as its index: by damage, lowest first,
    /// as the original's exchange sort (not stable: weapons of equal damage can change places).
    /// </summary>
    std::vector<size_t> DamageOrder(std::span<const float> damages);

    /// <summary>A copy of a weapon or piece of equipment fitted in a mech: its item, and that copy's record.</summary>
    struct MCFittedCopy
    {
        std::shared_ptr<MCLogInventoryItem> Item;
        std::shared_ptr<MCLogInventoryStat> Copy;
    };

    /// <summary>
    /// <paramref name="mech"/>'s fitted weapons and equipment, a copy each, in 1998's order (MCMechRepairBlock's weapon
    /// list): the short-, medium- and long-range weapons, each list by damage (<see cref="DamageOrder"/>), then the
    /// equipment in the inventory's order. Each copy keeps its own record (its damage, its item number).
    /// </summary>
    std::vector<MCFittedCopy> FittedCopies(const MCLogMech& mech);

    /// <summary>
    /// Why the part <paramref name="masterID"/> (<paramref name="tonnage"/> fitted) can't go into
    /// <paramref name="mech"/> (the original's greyed rows).
    /// </summary>
    MCFit WhyNotFit(uint8_t masterID, float tonnage, MCOptionalRef<const MCLogMech> mech);

    /// <summary>
    /// Adds a spare component of <paramref name="masterID"/> with no copies counted yet (one copy record) to the
    /// component inventory (1998's CompInventoryBlock::AddSpare, which made its inventory row too).
    /// </summary>
    /// <returns>The new item.</returns>
    std::shared_ptr<MCLogInventoryItem> AddSpare(uint8_t masterID);

    /// <summary>
    /// Fits a new copy of the part <paramref name="masterID"/> (with a weapon's ammunition) into <paramref name="mech"/>,
    /// out of the drop, if its tons are free: the original's drop of a spare on the mech. The spare's count is the
    /// caller's (the original took it at the pick-up).
    /// </summary>
    /// <returns>Whether it went in.</returns>
    bool Fit(MCLogMech& mech, uint8_t masterID);

    /// <summary>A part taken out of a mech, on its way to the spares or back.</summary>
    struct MCTaken
    {
        uint8_t MasterID = 0;
        /// <summary>Which copy it was (its stat's item number).</summary>
        uint8_t ItemNum = 0;
        /// <summary>Its damage: a damaged part is thrown away when it goes to the spares.</summary>
        uint8_t Hits = 0;
    };

    /// <summary>
    /// Takes copy <paramref name="itemNum"/> of <paramref name="item"/> (and a weapon's ammunition) out of
    /// <paramref name="mech"/>, its tons freed and the battle rating worked out: the original's pick-up off the weapon
    /// list. None when the mech holds no such copy.
    /// </summary>
    std::optional<MCTaken> TakeOut(MCLogMech& mech, const MCLogInventoryItem& item, uint8_t itemNum);

    /// <summary>What a part going to the spares did to the inventory's rows.</summary>
    enum class MCSpareRow : uint8_t
    {
        /// <summary>A damaged part, thrown away.</summary>
        Thrown,
        /// <summary>The spare's row is new, or back from none: the inventory is listed again.</summary>
        Listed,
        /// <summary>The spare's count went up: its rows take it.</summary>
        Counted
    };

    /// <summary>
    /// A part taken out goes to the spares (a damaged one is thrown away); <paramref name="spare"/> is the spare it
    /// joined.
    /// </summary>
    MCSpareRow ToSpares(const MCTaken& taken, std::shared_ptr<MCLogInventoryItem>& spare);

    /// <summary>A part taken out goes back into <paramref name="mech"/> as it was (with a weapon's ammunition).</summary>
    void PutBack(MCLogMech& mech, const MCTaken& taken);

    /// <summary>
    /// <paramref name="mech"/>'s MechWarrior leaves it for the spares (the mech out of the drop, its battle rating worked
    /// out): the original's pilot dropped on the inventory.
    /// </summary>
    /// <returns>The pilot, or null when the mech had none.</returns>
    std::shared_ptr<MCLogWarrior> PilotOut(MCLogMech& mech);

    /// <summary>
    /// Whether a MechWarrior may board <paramref name="mech"/>: a force mech of the player's own (one already aboard
    /// leaves it for the INVENTORY first: <see cref="PilotOut"/>).
    /// </summary>
    bool CanBoard(const MCLogMech& mech);

    /// <summary>
    /// A spare <paramref name="warrior"/> taken for a mech: assigned, the force's pilot numbers moved past them (the
    /// original's pick-up).
    /// </summary>
    void Assign(MCLogWarrior& warrior);

    /// <summary>An assigned <paramref name="warrior"/> back to the spares (the original's drop anywhere else).</summary>
    void Unassign(MCLogWarrior& warrior);

    /// <summary>
    /// An assigned <paramref name="warrior"/> boards <paramref name="mech"/> (force row <paramref name="row"/>), its
    /// battle rating worked out: the original's drop on the picked mech.
    /// </summary>
    void Board(MCLogWarrior& warrior, MCLogMech& mech, int32_t row);

    /// <summary>
    /// A unit joins the force at its head: the drop slots' mech (or <paramref name="vehicle"/>) rows move one on.
    /// </summary>
    void MakeRoomInDrop(bool vehicle);

    /// <summary>
    /// Whether the force is past its size with the unit just assigned to it counted (the original's check after the
    /// pick-up assigned it: <c>0xf</c> units).
    /// </summary>
    bool ForceFull();

    /// <summary>
    /// A spare mech joins the force (assigned, the drop's slots numbered past it), or with the force full goes back to
    /// the spares.
    /// </summary>
    /// <returns>Whether it joined (else the original's message 0x285).</returns>
    bool JoinForce(MCLogMech& mech);

    /// <summary>A spare vehicle joins the force, or with the force full goes back.</summary>
    /// <returns>Whether it joined (else the original's message 0x285).</returns>
    bool JoinForce(MCLogVehicle& vehicle);

    /// <summary>
    /// <paramref name="mech"/> leaves the force for the spares: out of the drop, its pilot freed, the drop's slots
    /// numbered again.
    /// </summary>
    /// <returns>Whether a pilot was freed (the pilots are listed again).</returns>
    bool LeaveForce(MCLogMech& mech);

    /// <summary><paramref name="vehicle"/> leaves the force for the spares.</summary>
    void LeaveForce(MCLogVehicle& vehicle);
}

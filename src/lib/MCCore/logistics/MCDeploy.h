#pragma once

// Port-only (SPEC-menus.md's stage 8): DEPLOY's rules, the original's (logistics\logbri.cpp's BriefingScreen and
// MechBriefBlock), in one place both the 1998 briefing screen's blocks (logistics/MCMechBriefBlock) and the remade DEPLOY
// (logistics/MCCampaignMenu) call: which units wait in READY AREA, which drop slots the player may fill and what is in
// them, each group's tons and the drop's, what fits under the drop's limit (FitsTonnage), the moves (a unit put in a
// slot, swapped with the one there, sent to the first open slot, taken out), why a unit can't drop, and BEGIN
// MISSION's checks. The moves change the deploy data as the original's did, and tell the other players in a match;
// the sounds and the words are the caller's, since each screen says them its own way.

class MCLogMech;
class MCLogPart;

namespace MCDeploy
{
    /// <summary>The drop's groups (lances), the slots of a group and the drop's slots (a game rule).</summary>
    inline constexpr size_t Groups = 3;
    inline constexpr size_t GroupSlots = 4;
    inline constexpr size_t Slots = Groups * GroupSlots;

    /// <summary>
    /// The units READY AREA lists, in the original's order: the force's mechs with a pilot not in the drop, then its
    /// vehicles not in it (MCBriefingScreen::SetUpDeploy's pane).
    /// </summary>
    std::vector<std::shared_ptr<MCLogPart>> ReadyUnits();

    /// <summary>
    /// The unit in drop slot <paramref name="slot"/> (0 to 11, group by group): the player's from the deploy slots, else
    /// in a match a teammate's from the side's drop slots; null for none.
    /// </summary>
    std::shared_ptr<MCLogPart> SlotUnit(size_t slot);

    /// <summary>Whether drop slot <paramref name="slot"/> is one the player may fill (the mission's, or a match's share).</summary>
    bool Open(size_t slot);

    /// <summary>
    /// Whether group <paramref name="group"/> shows on the original's screen: the first always, the others in a match or
    /// when the mission opens their slots (the original's lance labels).
    /// </summary>
    bool GroupShown(size_t group);

    /// <summary>The player's slot <paramref name="part"/> is in; none when it isn't in the drop.</summary>
    std::optional<size_t> SlotOf(const MCLogPart& part);

    /// <summary>Group <paramref name="group"/>'s tons as its head shows them (what <see cref="CountTons"/> adds up).</summary>
    int32_t GroupTons(size_t group);

    /// <summary>
    /// Adds up the drop's tons (CurDeployTonnage) from the groups the player can fill, as the original's CalcTonnages and,
    /// in a match, MpCalcTonnages did.
    /// </summary>
    void CountTons();

    /// <summary>Whether <paramref name="part"/> fits under the drop's limit (the original's FitsTonnage).</summary>
    bool Fits(const MCLogPart& part);

    /// <summary>Whether the drop's limit holds (the mission's HammerDown block, or the cheat, lifts it).</summary>
    bool LimitHolds();

    /// <summary>Why a unit can't go into the drop.</summary>
    enum class MCRefusal : uint8_t
    {
        None,
        /// <summary>Too heavy for the tons free (the original's string 0x4c).</summary>
        TooHeavy,
        /// <summary>No open slot left (for the group asked for, or any).</summary>
        NoSlot,
        /// <summary>The mech's engine is damaged (the original's string 0x35d).</summary>
        EngineDamaged,
        /// <summary>A location of the mech is destroyed (the original's string 0x364).</summary>
        LocationDestroyed,
        /// <summary>Another player's unit (a match's teammate's), which the player can't move.</summary>
        NotOwn,
        /// <summary>A match's BEGIN MISSION pressed (the briefing's buttons locked): nothing moves.</summary>
        Locked,
    };

    /// <summary>
    /// Why <paramref name="part"/> can't leave READY AREA (the original refused to pick it up): a mech with a damaged
    /// engine or a destroyed location, another player's unit; <see cref="MCRefusal::None"/> when it can.
    /// </summary>
    MCRefusal CantDrop(const MCLogPart& part);

    /// <summary>
    /// The original's pick-up from READY AREA (MCMechBriefBlock::PickUp): why <paramref name="part"/> can't be carried
    /// (<see cref="CantDrop"/>, or the buttons locked); else it and its pilot marked in the drop while carried, as the
    /// original marked them, which a place (<see cref="Place"/>, <see cref="Send"/>) keeps and <see cref="Unlift"/> undoes.
    /// </summary>
    MCRefusal Lift(MCLogPart& part);

    /// <summary>A lifted unit put nowhere: back in READY AREA, it and its pilot out of the drop (the original's drop).</summary>
    void Unlift(MCLogPart& part);

    /// <summary>The group whose key is held (1, 2 or 3), for the right button's send to it; none with none held.</summary>
    std::optional<size_t> HeldGroup();

    /// <summary>The original's string saying <paramref name="refusal"/> (0x4c, 0x35d, 0x364); none for the others.</summary>
    std::optional<uint32_t> RefusalString(MCRefusal refusal);

    /// <summary>
    /// Puts <paramref name="part"/> (in READY AREA, or just taken out of a slot) into slot <paramref name="slot"/>: an
    /// empty one, or one filled, whose unit leaves the drop first (OB-166 kept: its pilot stays marked deployed). It
    /// goes in if it fits what is free then (else <see cref="MCRefusal::TooHeavy"/>, the slot's unit out all the same,
    /// as the original's); its HD art is held from then, the other players told.
    /// </summary>
    MCRefusal Place(MCLogPart& part, size_t slot);

    /// <summary>
    /// The right button's send: <paramref name="part"/> into the first open empty slot of group <paramref name="group"/>
    /// (1, 2 or 3 held: up to its first closed slot), else of the whole drop.
    /// </summary>
    MCRefusal Send(MCLogPart& part, std::optional<size_t> group);

    /// <summary>
    /// The unit in the player's slot <paramref name="slot"/> out of the drop (back to READY AREA): its tons taken off,
    /// its HD art let go, the other players told; false when the slot holds none of the player's.
    /// </summary>
    bool TakeOut(size_t slot);

    /// <summary>What BEGIN MISSION does now.</summary>
    enum class MCBegin : uint8_t
    {
        /// <summary>The drop can go: a unit in it, under its limit (or the limit lifted), every required unit in.</summary>
        Ready,
        /// <summary>Nothing in the drop, over its limit or a required unit out: Betty's 0x1b.</summary>
        Refused,
        /// <summary>The player owns more units than the limit (a mission's salvage): the original's string 0x373.</summary>
        TooManyUnits,
    };

    /// <summary>Whether BEGIN MISSION is lit (the original's launch button art): <see cref="MCBegin::Ready"/>'s first two.</summary>
    bool CanBegin();

    /// <summary>
    /// BEGIN MISSION pressed (the original's Launch, without its sounds or words): single player, the mission
    /// requested when the units are within their limit; a match, the player said ready (SendReadyForBattle and its chat
    /// line). What it did.
    /// </summary>
    MCBegin Begin();

    /// <summary>The units' limit's words (string 0x373) as BEGIN MISSION says them: the units owned, the limit, the excess.</summary>
    std::string TooManyUnitsText();

    /// <summary>
    /// Port-only: a count bumped by every change of the deploy data made here (the screens follow it: a teammate's
    /// unit coming in, a cheat, a move).
    /// </summary>
    uint64_t Revision();

    /// <summary>The count bumped (a change made elsewhere: a network's message, a cheat).</summary>
    void Changed();

    /// <summary>
    /// A match's ready lock: once the player's BEGIN MISSION has told the others it's ready (<see cref="Begin"/>), the
    /// drop and the tabs are theirs no more until a campaign or match loads (1998's briefing's ButtonsLocked, now the
    /// model's: the remade DEPLOY and the 1998 briefing read it).
    /// </summary>
    bool Locked();

    /// <summary>The ready lock set or let go (a load lets it go), the count bumped.</summary>
    void SetLocked(bool locked);
}

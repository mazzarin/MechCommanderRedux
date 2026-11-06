#pragma once

#include "gui/MCListView.h"
#include "gui/MCScrollPane.h"
#include "logistics/MCLogObject.h"

class MCGuiEvent;
class MCInventoryList;
class MCLogMech;
struct MCLogInventoryItem;
struct MCLogInventoryStat;

/// <summary>
/// A mech's panel on the repair (refit) screen (0x19a x 0x70): damage diagram, battle rating, pilot, the three repair
/// sliders (armor, internal structure, engine) and the weapon lists in a scroll pane.
/// </summary>
/// <remarks>
/// Original source: <c>logistics\mrblock.cpp</c> (<c>MechRepairBlock</c>). A row of the repair screen's force list
/// (<see cref="MCRepairScreen::ShowForce"/>), or a panel of the briefing box (outside any list).
/// </remarks>
class MCMechRepairBlock : public MCListRow
{
public:
    /// <summary>Where a slider's knob is with nothing repaired: the left end of the 61-pixel track.</summary>
    static constexpr int32_t SliderLeft = 0xea;
    /// <summary>The right end of a slider's track (all repaired).</summary>
    static constexpr int32_t SliderRight = 0x127;

    /// <summary>Destroys the block if nothing did (its cleanup takes it out of the info box).</summary>
    ~MCMechRepairBlock() override;

    /// <summary>
    /// Shows <paramref name="logMech"/>: makes the weapon list's scroll pane and slider art, and works out the
    /// sliders' floors from what the mech arrived with (<see cref="MCLogMech::Arrived"/>). A
    /// <paramref name="briefingLayout"/> panel is the briefing box's (<see cref="MCBriefingBox"/>): it is drawn in
    /// the box's layout, never framed.
    /// </summary>
    void Init(std::shared_ptr<MCLogMech> logMech, bool briefingLayout = false);

    /// <summary>Takes the block out of any info box showing it, and frees the slider art and the weapon lists.</summary>
    void OnDestroy() override;

    /// <summary>
    /// Port: draws the mech's details into the repair screen's info box (see <c>MCInvInfoBox</c>): its diagram,
    /// tonnage, classes, speed and description.
    /// </summary>
    void DrawInfo(MCLogPort& port) const;

    /// <summary>Sliders, repair buttons, pilot and item drag and drop.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// Draws the panel. <paramref name="row"/> &lt; 0 draws into <paramref name="port"/> (the briefing box);
    /// otherwise into the repair screen's scroll pane at that row. Port: the pane's rows are drawn each frame by
    /// <see cref="DrawRow"/>; for them this ends a pilot drag or a held repair button and does the rest of what the
    /// paint did (the battle rating, the weapon lists).
    /// </summary>
    void DrawBackground(int32_t row, MCOptionalRef<MCLogPort> port);

    /// <summary>
    /// Port: draws the row into <paramref name="port"/> (the repair screen's unit pane) with its top at
    /// <paramref name="top"/>, from the state: framed while selected, darkened while another mech is.
    /// </summary>
    void DrawRow(MCLogPort& port, int32_t top) override;

    /// <summary>
    /// Port: draws the picked-up pilot's drag icon into <paramref name="surface"/> (0x20 square): the row's square at
    /// (5, 0x25), the portrait, over the unit list's colour 0xff. Before <see cref="ClearPilot"/>.
    /// </summary>
    void OnBeginDragPilot(MCLogPort& surface);

    /// <summary>
    /// Port: draws the picked-up mech's drag icon into <paramref name="surface"/> (0x20 square): its body diagram at
    /// (2, 1) over colour 0x10.
    /// </summary>
    void OnBeginDragMech(MCLogPort& surface) const;

    /// <summary>
    /// Port: draws the drag icon of <paramref name="item"/>, picked out of the weapon list, into
    /// <paramref name="surface"/> (0x20 square): its picture (<c>lscicc</c>) at (1, 1).
    /// </summary>
    static void OnBeginDragItem(MCLogPort& surface, MCLogInventoryItem& item);

    /// <summary>Works out whether the repair buttons are live; draws them into a <paramref name="port"/> given.</summary>
    void DrawButtons(MCOptionalRef<MCLogPort> port);

    /// <summary>Draws the damage diagram into a <paramref name="port"/> given.</summary>
    void DrawDamageDiagram(MCOptionalRef<MCLogPort> port);

    /// <summary>Recomputes the battle rating; draws it into a <paramref name="port"/> given.</summary>
    void DrawBR(MCOptionalRef<MCLogPort> port);

    /// <summary><see cref="DrawSlider"/> 0.</summary>
    void DrawArmorSlider(MCOptionalRef<MCLogPort> port);

    /// <summary>Draws slider <paramref name="slider"/> (0 armor, 1 internal structure, 2 engine) into a <paramref name="port"/> given.</summary>
    void DrawSlider(MCOptionalRef<MCLogPort> port, int32_t slider);

    /// <summary><see cref="DrawSlider"/> 1.</summary>
    void DrawInternalSlider(MCOptionalRef<MCLogPort> port);

    /// <summary><see cref="DrawSlider"/> 2.</summary>
    void DrawEngineSlider(MCOptionalRef<MCLogPort> port);

    /// <summary>The selected mech's weapon list (see <see cref="DrawInventory"/>).</summary>
    void Draw() override;

    /// <summary>Draws the weapon list into a <paramref name="port"/> given (while the mech is in the force).</summary>
    void DrawInventory(MCOptionalRef<MCLogPort> port);

    /// <summary>
    /// Port-only: over the weapon list the mouse wheel scrolls it, as its arrows do. The list's pane is hidden (the
    /// block draws it), so the wheel finds the block, which would otherwise pass it on to the unit list.
    /// </summary>
    bool MouseWheel(int32_t steps, int32_t xPos, int32_t yPos) override;

    /// <summary>
    /// Works out the mech's condition (<c>MCLogMech::CalcStatus</c>); draws its status bar into a
    /// <paramref name="port"/> given.
    /// </summary>
    void DrawStatusBar(MCOptionalRef<MCLogPort> port = std::nullopt);

    /// <summary>With <paramref name="value"/> &lt; 0, puts the engine slider at the engine's damage level.</summary>
    void SetEngineSlider(int32_t value);

    /// <summary>With <paramref name="value"/> &lt; 0, puts the internal-structure slider at the current total.</summary>
    void SetInternalSlider(int32_t value);

    /// <summary>With <paramref name="value"/> &lt; 0, puts the armor slider at the current total.</summary>
    void SetArmorSlider(int32_t value);

    /// <summary>
    /// Puts <paramref name="mech"/>'s three knobs (<see cref="MCLogMech::RepairSliders"/>) at its engine damage, armor
    /// and internal structure, as the original's block did when it was made (when the mech joins the lists). An
    /// engine past damage level 3 is set to 3.
    /// </summary>
    static void PlaceSliders(MCLogMech& mech);

    /// <summary>Port: the pilot was picked up: its portrait's place shows blank until the next paint.</summary>
    void ClearPilot();

    /// <summary>Draws the pilot (status bar, portrait, callsign, rank, skills, health) into a <paramref name="port"/> given.</summary>
    void SetPilotStats(MCOptionalRef<MCLogPort> port);

    /// <summary>Port: drawn with the pilot's stats.</summary>
    void SetPilotHealth(MCOptionalRef<MCLogPort> port);

    /// <summary>
    /// Rebuilds the short-, medium- and long-range weapon lists and the equipment list from the mech's inventory (an
    /// entry per copy), and sorts the weapon lists by damage.
    /// </summary>
    void SetWeaponLists();

    /// <summary>
    /// Sorts <paramref name="entries"/> (inventory positions in <paramref name="inventory"/>) by their components'
    /// damage, lowest first, as the original's exchange sort (not stable); <paramref name="hits"/> follow their entries.
    /// </summary>
    static void SortByDamage(std::span<int32_t> entries, std::span<int32_t> hits, MCInventoryList& inventory);

    /// <summary>
    /// The inventory item of entry <paramref name="index"/> of <paramref name="list"/>. The entry's copy is found by
    /// counting the equal entries before it; that copy's item number goes to <paramref name="itemNum"/>.
    /// </summary>
    std::shared_ptr<MCLogInventoryItem> GetInvItem(const std::vector<int32_t>& list, int32_t index,
                                                   uint8_t& itemNum) const;

    /// <summary>
    /// Repairs <paramref name="points"/> of <paramref name="mech"/>'s armor points (all of it when negative): the head
    /// first, then one point at a time to the location most damaged (the center torso, the rear and the cockpit count
    /// as more damaged).
    /// </summary>
    static void RepairArmor(MCLogMech& mech, int32_t points);

    /// <summary>
    /// Repairs <paramref name="points"/> of <paramref name="mech"/>'s internal structure points (all of it when
    /// negative), most damaged first.
    /// </summary>
    static void RepairInternal(MCLogMech& mech, int32_t points);

    /// <summary>
    /// Makes the weapon list's content for <paramref name="target"/> (the block's own when absent): in multiplayer only for
    /// the player's own mechs and the one in the briefing box.
    /// </summary>
    void SetInventory(MCOptionalRef<MCScrollPane> target);

    /// <summary>
    /// Port: draws the weapon lists (headings and items) into <paramref name="content"/>, a weapon-list pane's view:
    /// what <see cref="SetInventory"/> painted into the pane's picture.
    /// </summary>
    void DrawWeaponList(MCLogPort& content);

    /// <summary>The item on line <paramref name="line"/> of the weapon list (null on a heading).</summary>
    std::shared_ptr<MCLogInventoryItem> GetItemFromScrollPane(int32_t line, uint8_t& itemNum) const;

    /// <summary>
    /// Starts dragging the copy <paramref name="itemNum"/> of <paramref name="item"/> out of the mech: makes the drag
    /// icon, shows the item's info, takes it (and a weapon's ammo) out of the mech's inventory.
    /// </summary>
    void SetUpItemDragIcon(MCLogInventoryItem& item, uint8_t itemNum, MCGuiEvent& event);

    /// <summary>Returns 0.</summary>
    static int DebugFunction1(int32_t arg1, int32_t arg2);

    /// <summary>
    /// The refit dialog's OK: every damaged weapon or piece of equipment with no replacement in the spare components
    /// leaves the mech.
    /// </summary>
    void StripUnrepaired();

    /// <summary>The mech's force row (<see cref="MCLogPart::SlotIndex"/>; 0 once the mech is gone).</summary>
    int32_t SlotIndex() const;
    /// <summary>The mech (null once it is gone); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogMech> Mech() const { return _Mech.lock(); }
    /// <summary>Points the block at <paramref name="mech"/> without the rest of <see cref="Init"/>.</summary>
    void SetMech(std::weak_ptr<MCLogMech> mech) { _Mech = std::move(mech); }
    /// <summary>The weapon list's pane (a child).</summary>
    MCGuiHandle<MCScrollPane> InventoryPane;
    /// <summary>A copy of the mech's name index (also the inventory row of its <c>MCMechInventoryBlock</c>).</summary>
    int32_t ListPosition = 0;
    /// <summary>The inventory positions of the short-range weapons, one per copy.</summary>
    std::vector<int32_t> ShortRangeWeapons;
    std::vector<int32_t> MediumRangeWeapons;
    std::vector<int32_t> LongRangeWeapons;
    /// <summary>Equipment (sensors, ECM, probes).</summary>
    std::vector<int32_t> Equipment;
    /// <summary>The damage (the copy's hits) of every entry of the four lists, in list order.</summary>
    std::vector<int32_t> ItemHits;
    /// <summary>The mech's diagram for the info box (0x1c x 0x1e), made when the mouse comes over the mech.</summary>
    std::shared_ptr<MCLogPort> DragPort;
    /// <summary>The slider knob (<c>logart\lsrupm05.tga</c>).</summary>
    std::shared_ptr<MCLogPort> SliderArtPort;
    /// <summary>Armor, internal structure or the engine is damaged (the structure repair button is live).</summary>
    bool CanRepairStructure = false;
    /// <summary>A weapon or piece of equipment is damaged (the item repair button is live).</summary>
    bool CanRepairItems = false;
    /// <summary>Slider pixels per armor point (61 over the total maximum armor).</summary>
    float ArmorPixelScale = 0.0f;
    /// <summary>The armor slider's knob for the armor the mech arrived with (<see cref="MCLogMech::Arrived"/>).</summary>
    int32_t ArmorSliderStart = 0;
    float InternalPixelScale = 0.0f;
    /// <summary>The internal structure slider's knob for what the mech arrived with.</summary>
    int32_t InternalSliderStart = 0;
    /// <summary>The engine slider's knob for the engine damage the mech arrived with.</summary>
    int32_t EngineSliderStart = 0;
    /// <summary>The engine's copy record (its hits are the damage level).</summary>
    std::shared_ptr<MCLogInventoryStat> EngineStat() const { return _EngineStat.lock(); }

private:
    /// <summary>The mech the block refers to (<see cref="Mech"/>).</summary>
    std::weak_ptr<MCLogMech> _Mech;
    /// <summary>The engine's copy record (its hits are the damage level).</summary>
    std::weak_ptr<MCLogInventoryStat> _EngineStat;

    // Port: the pieces of the panel, drawn into port with its top at top (row 0 of the briefing box's picture, or a row
    // of the repair screen's unit pane).

    /// <summary>The background, the name art, the tonnage line and (when <paramref name="framed"/>) the selection frame.</summary>
    void PaintBase(MCLogPort& port, int32_t top, bool briefing, bool framed);
    /// <summary>The repair buttons, live when <paramref name="items"/> / <paramref name="structure"/>.</summary>
    void PaintButtons(MCLogPort& port, int32_t top, bool onRows, bool items, bool structure);
    void PaintDiagram(MCLogPort& port, int32_t top, int32_t xPos) const;
    void PaintBR(MCLogPort& port, int32_t top);
    void PaintSlider(MCLogPort& port, int32_t top, int32_t slider, bool briefing);
    void PaintStatusBar(MCLogPort& port, int32_t top, float status, bool repairLayout);
    /// <summary>The status bar, the pilot's portrait, callsign, rank, skills and health.</summary>
    void PaintPilot(MCLogPort& port, int32_t top, float status, bool repairLayout);
    /// <summary>The weapon list (its pane, as scrolled) and its slider column.</summary>
    void PaintInventory(MCLogPort& port, int32_t top);
    /// <summary>The weapons' weight against the free weight.</summary>
    void PaintTonnage(MCLogPort& port, int32_t top);

    /// <summary>Port: a weapon or piece of equipment is damaged (the item repair button is live).</summary>
    bool ItemsDamaged() const;
    /// <summary>Port: the engine, internal structure or armor is damaged (the structure repair button is live).</summary>
    bool StructureDamaged() const;
    /// <summary>Port: the block lists the mech's weapons (in multiplayer only the player's own mechs, and the boxed one).</summary>
    bool ShowsInventory() const;

    /// <summary>The drop: a pilot, the whole mech, an item from the weapon list, or a slider let go.</summary>
    void HandleDrop(MCGuiEvent& event, int32_t eventType);
    /// <summary>A slider dragged to <paramref name="localX"/>: undoes the drag so far, then repairs up to there.</summary>
    void DragSlider(int32_t localX);
    /// <summary>The item repair button: every damaged copy replaced from the spare components.</summary>
    void RepairItems();
    /// <summary>The structure repair button: the engine level by level, then internal structure, then armor.</summary>
    void RepairStructure();

    /// <summary>Port: the pilot is being dragged out of the row (its portrait's place blank until the drop).</summary>
    bool _PilotLifted = false;
    /// <summary>Port: the repair button held while its repair runs: 0 none, 1 items, 2 structure.</summary>
    int32_t _PressedButton = 0;
    /// <summary>Port: the briefing box's panel (see <see cref="Init"/>).</summary>
    bool _BriefingLayout = false;
};

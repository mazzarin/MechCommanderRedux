#pragma once

#include "gui/MCListView.h"
#include "logistics/MCLogObject.h"

class MCScrollPane;
struct MCLogInventoryItem;

/// <summary>
/// Port: what an inventory screen shows in its info box under the inventory pane (at (2, 0x18a)) and in the column
/// header over the pane (at (0xc4, 0x65)); the screen draws it from here each frame. The original painted the blank
/// box, the details of the row under the mouse and the header into the screen's picture, where they stayed until
/// painted over.
/// </summary>
struct MCInvInfoBox
{
    /// <summary>Whose details the box shows.</summary>
    enum class Kind
    {
        /// <summary>None: the blank box only.</summary>
        None,
        /// <summary>A <c>MechInventoryBlock</c>'s mech.</summary>
        Mech,
        /// <summary>A <c>PilotInventoryBlock</c>'s pilot.</summary>
        Pilot,
        /// <summary>A <c>VehicleInventoryBlock</c>'s vehicle.</summary>
        Vehicle,
        /// <summary>A <c>CompInventoryBlock</c>'s component (a row of the component tab).</summary>
        Component,
        /// <summary>A <c>CompInventoryBlock</c>'s component in a mech's weapon list on the repair screen.</summary>
        RepairItem,
        /// <summary>A <c>MechRepairBlock</c>'s mech on the repair screen.</summary>
        RepairMech
    };

    /// <summary>The blank box shown (<c>Logistics::InventoryIconPorts</c>), or -1: the screen's picture shows.</summary>
    int32_t Art = -1;
    Kind InfoKind = Kind::None;
    /// <summary>The block whose details are shown (not a component's); it takes itself out when it goes.</summary>
    std::weak_ptr<MCGuiObject> Source;
    /// <summary>
    /// Draws the details of an item (a pilot) instead of <see cref="Source"/>: they keep showing while the box shows
    /// them, whatever happens to the row they were shown from (the original's painted pixels stayed).
    /// </summary>
    std::function<void(MCLogPort& port)> Details;
    /// <summary>
    /// A component's details, as they were when shown (the item can go while they are, as when it is dragged off a
    /// mech): its picture (<c>lscicc&lt;n&gt;</c>), its block's texts and its description.
    /// </summary>
    int32_t ComponentPicture = 0;
    std::string RangeText;
    std::string DamageText;
    std::string RecycleText;
    std::string Description;
    /// <summary>The tab whose column header is shown (0..3), or -1: the screen's picture shows.</summary>
    int32_t Header = -1;

    /// <summary>The screen's background art was painted over all of it.</summary>
    void Clear() { *this = MCInvInfoBox{}; }

    /// <summary>The blank box of tab <paramref name="tab"/> was painted over the box.</summary>
    void Blank(int32_t tab)
    {
        Art = tab;
        InfoKind = Kind::None;
        Source.reset();
        Details = nullptr;
    }
};

/// <summary>
/// The common base of the purchase and repair screens: the inventory pane on the left, with tabs for mechs, pilots,
/// components and vehicles, filled from the logistics inventory lists, and the unit pane on the right.
/// </summary>
/// <remarks>
/// Original source: <c>logistics\logscrn.cpp</c> (<c>LogInvScreen</c>). It had no virtual functions of its own (its
/// vtable was never emitted), so it shows up only through the methods the purchase and repair screens inherit.
/// Several methods only use <c>GlobalLog()</c>.
/// </remarks>
class MCLogInvScreen : public MCLogObject
{
public:
    /// <summary>The height of a unit block in the unit and store panes.</summary>
    static constexpr int32_t UnitBlockHeight = 0x70;
    /// <summary>The height of an inventory block.</summary>
    static constexpr int32_t InvBlockHeight = 0x2b;

    /// <summary>Forgets the screen (see <see cref="ForgetInfoSource"/>).</summary>
    ~MCLogInvScreen() override;

    /// <summary>Puts the force's repair blocks in the repair screen's unit pane, a row each (mechs, then vehicles).</summary>
    void CreateVehiclePane();

    /// <summary>
    /// Sets up the store's tabs (mechs, vehicles, components) unless <paramref name="pilotsOnly"/>, and the pilots
    /// for hire (always): their layouts (<see cref="MCPurchaseScreen::StoreLayouts"/>), and readies their
    /// descriptions, as the original's rows did when it painted each tab's picture.
    /// </summary>
    static void CreatePurVehiclePane(bool pilotsOnly);

    /// <summary>The purchase screen's store: its unit pane's list (null before the screen is made).</summary>
    static std::shared_ptr<MCListView> StoreList();

    /// <summary>
    /// The store's row of <paramref name="item"/>, if shown, takes its state again (the original's
    /// <c>drawBackground</c> of the item's one block).
    /// </summary>
    template <typename Item> static void RefreshStoreRow(const Item& item)
    {
        if (const std::shared_ptr<MCListView> store = StoreList(); store != nullptr)
        {
            if (const std::shared_ptr<MCListRow> row = store->RowOf<MCListRow>(item); row != nullptr)
            {
                row->OnRefresh();
            }
        }
    }

    /// <summary>Every row the store shows takes its state again.</summary>
    static void RefreshStoreRows();

    /// <summary>Numbers the mechs' inventory rows (their <c>ListIndex</c>, the order of the mech list).</summary>
    static void CreateMechInvBlock();

    /// <summary>Numbers the vehicles' inventory rows (their <c>ListIndex</c>, the order of the vehicle list).</summary>
    static void CreateVhclInvBlock();

    /// <summary>Numbers the pilots' inventory rows (their <c>ListIndex</c>, the order of the pilot list).</summary>
    void CreatePilotInvBlock() const;

    /// <summary>
    /// Re-indexes the spare components (their rows) and refreshes the component rows shown (see
    /// <see cref="RefreshComponentRows"/>).
    /// </summary>
    static void CreateCompInvBlock();

    /// <summary>
    /// The rows of <paramref name="item"/> in both screens' lists, if shown, take their state again (shown or hidden,
    /// and whether the item fits the selected mech): the original's <c>drawBackground</c> of the item's one block.
    /// </summary>
    static void RefreshComponentRows(const MCLogInventoryItem& item);

    /// <summary>Clears the info box under the inventory for tab <paramref name="tab"/> (negative = the current one).</summary>
    static void DrawBlankInvInfoBlock(int32_t tab);

    /// <summary>
    /// Shows the mech tab in both screens' inventory lists: a row per mech of the mech list
    /// (<paramref name="resetScroll"/> scrolls them to the top); <paramref name="redrawTabs"/> blanks the info box.
    /// </summary>
    void SetUpMechInv(bool resetScroll, bool redrawTabs);

    /// <summary>
    /// Shows the mechs for sale in the store (on the purchase screen only), each showing its first variant still on
    /// sale.
    /// </summary>
    void SetUpMechPurchase() const;

    /// <summary>
    /// Shows the pilot tab in both screens' inventory lists: a row per pilot of the pilot list (an assigned pilot is in
    /// the assigned list).
    /// </summary>
    void SetUpPilotInv(bool resetScroll, bool redrawTabs);

    /// <summary>Shows the component tab in both screens' inventory lists: a row per spare component that has a row.</summary>
    void SetUpCompInv(bool resetScroll, bool redrawTabs);

    /// <summary>Shows the vehicle tab in both screens' inventory lists: a row per vehicle of the vehicle list.</summary>
    void SetUpVhclInv(bool resetScroll, bool redrawTabs);

    /// <summary>Shows the vehicles for sale in the store (on the purchase screen only).</summary>
    void SetUpVehiclePurchase() const;

    /// <summary>Shows the pilots for hire in the store (on the purchase screen only).</summary>
    void SetUpPilotPurchase() const;

    /// <summary>
    /// Shows the components for sale in the store (on the purchase screen only), in their sort order (a row each
    /// for sort orders 0..49).
    /// </summary>
    void SetUpCompPurchase() const;

    /// <summary>
    /// A pilot was hired (no longer for hire): the store shows the pilots for hire again, the rows below the hired
    /// one moved up.
    /// </summary>
    static void RemovePilot();

    /// <summary>
    /// Port: draws the screen: its background art, the info box and column header (<see cref="Info"/>), then the
    /// shared places (<see cref="MCLogScreenChrome"/>). Outside the frame pass it refreshes the children, as the
    /// original's paint.
    /// </summary>
    void Draw() override;

    /// <summary>Port: the screen draws itself each frame (its port is a view).</summary>
    bool DrawsLive() override { return true; }

    /// <summary>Port: see <see cref="MCLogScreenChrome"/>.</summary>
    MCOptionalRef<MCLogScreenChrome> Chrome() override { return ScreenChrome; }

    /// <summary>
    /// Port: sets the screen's background art (<paramref name="artName"/> in <c>logart</c>, which the original loaded as
    /// the screen's picture) and lists the screen for <see cref="Of"/>.
    /// </summary>
    void InitLive(std::string_view artName);

    /// <summary>
    /// Port: shows the details of <paramref name="source"/> in the info box, over the blank box last shown (the
    /// original painted them there right after it).
    /// </summary>
    void ShowInfo(MCInvInfoBox::Kind kind, std::weak_ptr<MCGuiObject> source);

    /// <summary>
    /// Port: shows an item's details in the info box, drawn by <paramref name="details"/> (see
    /// <see cref="MCInvInfoBox::Details"/>), over the blank box last shown.
    /// </summary>
    void ShowDetails(MCInvInfoBox::Kind kind, std::function<void(MCLogPort& port)> details);

    /// <summary>
    /// Port: shows the details of <paramref name="item"/>'s component in the info box (as <see cref="ShowInfo"/>),
    /// from a mech's weapon list when <paramref name="repairItem"/>.
    /// </summary>
    void ShowComponentInfo(const MCLogInventoryItem& item, bool repairItem);

    /// <summary>Port: draws <see cref="Info"/> into <paramref name="port"/> (the screen's view).</summary>
    void DrawInfo(MCLogPort& port);

    /// <summary>Port: the inventory screen <paramref name="screen"/> is, or null for another screen.</summary>
    static std::shared_ptr<MCLogInvScreen> Of(const MCGuiObject& screen);

    /// <summary>Port: <paramref name="source"/> is going: no inventory screen shows its details any more.</summary>
    static void ForgetInfoSource(const MCLogObject& source);

    /// <summary>Port: see <see cref="MCInvInfoBox"/>.</summary>
    MCInvInfoBox Info;

    /// <summary>
    /// Set while the screen's chat button blinks (multiplayer; timer 7 on the purchase screen, 8 on the repair
    /// screen); <c>BriefingScreen::SetUpOperation</c> stops it.
    /// </summary>
    bool ChatBlinking = false;
    /// <summary>The inventory pane (left; a child): a list of the shown tab's rows.</summary>
    MCGuiHandle<MCListView> InventoryPane;
    /// <summary>
    /// The unit pane (a child): the store on the purchase screen (a list of the shown tab's rows), the force on the
    /// repair screen (still filled by hand).
    /// </summary>
    MCGuiHandle<MCListView> UnitPane;

    /// <summary>Port: what the screen shows of the shared places.</summary>
    MCLogScreenChrome ScreenChrome;
    /// <summary>Port: the background art's file name in <c>logart</c> (<see cref="InitLive"/>).</summary>
    std::string BackgroundArt;

protected:
    /// <summary>
    /// Makes the inventory pane (empty) and the unit pane (backed by <c>lsrbk01</c>; emptied when
    /// <paramref name="emptyUnitPane"/>), as the screen's children.
    /// </summary>
    void MakePanes(bool emptyUnitPane);

    /// <summary>Takes the panes off their content (the logistics screen's, not theirs) before they go.</summary>
    void FreePanes();

private:
};

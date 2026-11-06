#include "stdafx.h"
#include "logistics/MCLogInvScreen.h"
#include "gui/MCGuiFont.h"
#include "gui/MCScrollPane.h"
#include "logistics/MCRepairScreen.h"
#include "gui/MCListView.h"
#include "logistics/MCInventoryBlock.h"
#include "logistics/MCMechInventoryBlock.h"
#include "logistics/MCPilotInventoryBlock.h"
#include "logistics/MCVehicleInventoryBlock.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCDragIcon.h"
#include "logistics/MCLogRows.h"
#include "main/MCGamePaths.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCMechRepairBlock.h"
#include "logistics/MCVehicleRepairBlock.h"
#include "logistics/MCBriefingBox.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCPilotPurchaseBlock.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "logistics/MCCompPurchaseBlock.h"
#include "logistics/MCUnitLimits.h"
#include "logistics/MCPurProfile.h"
#include "main/MCLogistics.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The inventory screens' lists (the purchase screen's, then the repair screen's).</summary>
    std::array<std::shared_ptr<MCListView>, 2> InventoryLists()
    {
        return {GlobalLog()->PurchaseScreen->InventoryPane.Lock(), GlobalLog()->RepairScreen->InventoryPane.Lock()};
    }

    /// <summary>
    /// The pilot tab: a row per pilot in the pilot list, in its order (the assigned pilots are in the assigned list).
    /// </summary>
    MCListSource<MCLogWarrior> PilotSource()
    {
        MCListSource<MCLogWarrior> source;
        source.Items = []() -> const std::vector<std::shared_ptr<MCLogWarrior>>&
        { return GlobalLog()->WarriorList->Warriors; };
        source.MakeRow = [](MCListView& list, std::shared_ptr<MCLogWarrior> warrior)
        { return list.CreateRow<MCPilotInventoryBlock>([&](MCPilotInventoryBlock& row) { row.Init(warrior); }); };
        return source;
    }

    /// <summary>The mech tab: a row per mech in the mech list, in its order (the force's mechs are in the force list).</summary>
    MCListSource<MCLogMech> MechSource()
    {
        MCListSource<MCLogMech> source;
        source.Items = []() -> const std::vector<std::shared_ptr<MCLogMech>>& { return GlobalLog()->MechList->Mechs; };
        source.MakeRow = [](MCListView& list, std::shared_ptr<MCLogMech> mech)
        { return list.CreateRow<MCMechInventoryBlock>([&](MCMechInventoryBlock& row) { row.Init(mech); }); };
        return source;
    }

    /// <summary>
    /// The vehicle tab: a row per vehicle in the vehicle list, in its order (the force's vehicles are in the force
    /// list).
    /// </summary>
    MCListSource<MCLogVehicle> VehicleSource()
    {
        MCListSource<MCLogVehicle> source;
        source.Items = []() -> const std::vector<std::shared_ptr<MCLogVehicle>>&
        { return GlobalLog()->VehicleList->Vehicles; };
        source.MakeRow = [](MCListView& list, std::shared_ptr<MCLogVehicle> vehicle)
        { return list.CreateRow<MCVehicleInventoryBlock>([&](MCVehicleInventoryBlock& row) { row.Init(vehicle); }); };
        return source;
    }

    /// <summary>
    /// The component tab: a row per spare component with a row (<see cref="MCLogInventoryItem::ListIndex"/>, given out
    /// by <see cref="MCLogistics::ReIndexInventory"/>), in row order.
    /// </summary>
    MCListSource<MCLogInventoryItem> ComponentSource()
    {
        MCListSource<MCLogInventoryItem> source;
        source.Items = []() -> const std::vector<std::shared_ptr<MCLogInventoryItem>>&
        { return GlobalLog()->ComponentInventory->Items; };
        source.Filter = [](const MCLogInventoryItem& item) { return item.ListIndex >= 0; };
        source.Sort = [](const MCLogInventoryItem& first, const MCLogInventoryItem& second)
        { return first.ListIndex < second.ListIndex; };
        source.MakeRow = [](MCListView& list, std::shared_ptr<MCLogInventoryItem> item)
        { return list.CreateRow<MCCompInventoryBlock>([&](MCCompInventoryBlock& row) { row.Init(item); }); };
        return source;
    }

    /// <summary>Shows <paramref name="source"/> in both screens' inventory lists, as the original's one tab picture in both panes.</summary>
    template <typename Item> void ShowInvSource(const MCListSource<Item>& source, bool resetScroll)
    {
        for (const std::shared_ptr<MCListView>& list : InventoryLists())
        {
            const MCListLayout layout{MCLogInvScreen::InvBlockHeight, list->Width() - 0xd, list->Height(), 0x10};
            list->Show(source, layout, resetScroll);
        }
    }

    /// <summary>The store tabs, by inventory tab (<see cref="MCPurchaseScreen::StoreLayouts"/>).</summary>
    enum class MCStoreTab
    {
        Mechs,
        Pilots,
        Components,
        Vehicles
    };

    /// <summary>The store's layout of tab <paramref name="tab"/> on the purchase screen.</summary>
    MCListLayout& StoreLayout(MCStoreTab tab)
    {
        return GlobalLog()->PurchaseScreen->StoreLayouts[static_cast<size_t>(tab)];
    }

    /// <summary>
    /// The store's components that get a row: sort orders 0..49 (each component's place in <c>objsort.rsp</c>; the
    /// original numbered the rows over these only).
    /// </summary>
    constexpr int32_t StoreSortOrders = 50;

    /// <summary>The mech store: a row per mech type for sale, newest first.</summary>
    MCListSource<MCPurMech> MechStoreSource()
    {
        MCListSource<MCPurMech> source;
        source.Items = []() -> const std::vector<std::shared_ptr<MCPurMech>>&
        { return GlobalLog()->PurMechList->Mechs; };
        source.MakeRow = [](MCListView& list, std::shared_ptr<MCPurMech> purMech)
        { return list.CreateRow<MCMechPurchaseBlock>([&](MCMechPurchaseBlock& row) { row.Init(purMech); }); };
        return source;
    }

    /// <summary>The vehicle store: a row per vehicle type for sale, lightest first.</summary>
    MCListSource<MCPurVehicle> VehicleStoreSource()
    {
        MCListSource<MCPurVehicle> source;
        source.Items = []() -> const std::vector<std::shared_ptr<MCPurVehicle>>&
        { return GlobalLog()->PurVehicleList->Vehicles; };
        source.MakeRow = [](MCListView& list, std::shared_ptr<MCPurVehicle> purVehicle)
        { return list.CreateRow<MCVehiclePurchaseBlock>([&](MCVehiclePurchaseBlock& row) { row.Init(purVehicle); }); };
        return source;
    }

    /// <summary>The component store: a row per component for sale with a sort order under 50, in sort order.</summary>
    MCListSource<MCLogInventoryItem> CompStoreSource()
    {
        MCListSource<MCLogInventoryItem> source;
        source.Items = []() -> const std::vector<std::shared_ptr<MCLogInventoryItem>>&
        { return GlobalLog()->PurchaseComponents->Items; };
        source.Filter = [](const MCLogInventoryItem& item)
        { return item.SortOrder >= 0 && item.SortOrder < StoreSortOrders; };
        source.Sort = [](const MCLogInventoryItem& first, const MCLogInventoryItem& second)
        { return first.SortOrder < second.SortOrder; };
        source.MakeRow = [](MCListView& list, std::shared_ptr<MCLogInventoryItem> item)
        { return list.CreateRow<MCCompPurchaseBlock>([&](MCCompPurchaseBlock& row) { row.Init(item); }); };
        return source;
    }

    /// <summary>The pilot store: a row per pilot still for hire, in the hiring list's order.</summary>
    MCListSource<MCPurPilotData> PilotStoreSource()
    {
        MCListSource<MCPurPilotData> source;
        source.Items = []() -> const std::vector<std::shared_ptr<MCPurPilotData>>&
        { return GlobalLog()->PurPilotList->Pilots; };
        source.Filter = [](const MCPurPilotData& pilot) { return pilot.Status == MCPurPilotData::ForHire; };
        source.MakeRow = [](MCListView& list, std::shared_ptr<MCPurPilotData> pilot)
        { return list.CreateRow<MCPilotPurchaseBlock>([&](MCPilotPurchaseBlock& row) { row.Init(pilot); }); };
        return source;
    }

    /// <summary>
    /// Shows <paramref name="source"/> in the store with tab <paramref name="tab"/>'s layout, from the top, if the
    /// purchase screen is the one shown (the original filled the store only then).
    /// </summary>
    template <typename Item> void ShowStore(MCStoreTab tab, const MCListSource<Item>& source)
    {
        MCPurchaseScreen& screen = *GlobalLog()->PurchaseScreen;

        if (GlobalLog()->CurrentScreen().get() != &screen)
        {
            return;
        }

        screen.UnitPane->Show(source, StoreLayout(tab), true);
    }

    /// <summary>The column headers of the inventory tabs, drawn at (0xc4, 0x65).</summary>
    constexpr std::array<std::string_view, 4> InvHeaderArt = {"lscdwm.tga", "lscdwp.tga", "lscdwc.tga", "lscdwv.tga"};

    /// <summary>The inventory screens (the purchase and repair screens), for <see cref="MCLogInvScreen::Of"/>.</summary>
    std::vector<std::weak_ptr<MCLogInvScreen>> InvScreens;

    /// <summary>A component's details in the info box (<see cref="MCInvInfoBox"/>).</summary>
    void DrawComponentInfo(MCInvInfoBox& info, MCLogPort& port)
    {
        const std::shared_ptr<MCLogPort> picture = LogScreenArt(std::format("lscicc{:02}.tga", info.ComponentPicture));

        // The repair screen's weapon list copied the picture opaque, the component tab keyed.
        if (info.InfoKind == MCInvInfoBox::Kind::RepairItem)
        {
            VfxPaneCopy(picture->Frame(), 0, 0, port.Frame(), 9, 0x191, -1);
        }
        else
        {
            picture->CopyTo(port.Frame(), 9, 0x191, true);
        }

        auto write = [&](int32_t y, const std::string& text)
        { YellowDropFont->WriteString(port.Frame(), 0x53, y, text); };
        write(0x1a5, info.RangeText);
        write(0x19c, info.DamageText);
        write(0x193, info.RecycleText);

        if (!info.Description.empty())
        {
            DrawInfoDescription(port, 0xc5, 0x26, info.Description.data(), 8, 0x1b3);
        }
    }

    /// <summary>
    /// The inventory tab art common to the SetUp*Inv functions: the blank info box of tab <paramref name="tab"/>
    /// when <paramref name="redrawTabs"/>, then the tab's column header.
    /// </summary>
    void DrawInvTabArt(MCLogInvScreen& screen, int32_t tab, bool redrawTabs)
    {
        if (redrawTabs)
        {
            screen.Info.Blank(tab);
        }

        screen.Info.Header = tab;
    }
}

// MCLogInvScreen

MCLogInvScreen::~MCLogInvScreen()
{
    // This screen's entry has expired by now.
    std::erase_if(InvScreens, [](const std::weak_ptr<MCLogInvScreen>& screen) { return screen.expired(); });
}

auto MCLogInvScreen::MakePanes(bool emptyUnitPane) -> void
{
    InventoryPane = CreateChild<MCListView>(
        [](MCListView& pane)
        {
            pane.Init(0xb8, 0x10d, 8, 0x6b, std::nullopt);
            pane.ClearDisplayPort();
        });

    UnitPane = CreateChild<MCListView>(
        [&](MCListView& pane)
        {
            pane.Init(0x1aa, 0x1cc, 0xd3, 0x11, std::format("{}logart\\lsrbk01.tga", ArtPath).c_str());

            if (emptyUnitPane)
            {
                pane.ClearDisplayPort();
            }
        });
}

auto MCLogInvScreen::FreePanes() -> void
{
    // The panes may show the logistics screen's views: they let go of them before they go with the screen.
    for (const MCGuiHandle<MCListView>& pane : {InventoryPane, UnitPane})
    {
        if (const std::shared_ptr<MCListView> shown = pane.Lock(); shown != nullptr)
        {
            shown->ClearDisplayPort();
        }
    }
}

auto MCLogInvScreen::CreateVehiclePane() -> void
{
    // The force from the top; then each mech's row works out what it shows, in order, as the original painted each
    // block (a vehicle's paint left nothing to work out).
    MCRepairScreen& repair = *GlobalLog()->RepairScreen;
    repair.ShowForce(true);

    for (const std::shared_ptr<MCLogMech>& mech : GlobalLog()->ForceMechList->Mechs)
    {
        if (const std::shared_ptr<MCMechRepairBlock> row = repair.RowOf(*mech); row != nullptr)
        {
            row->DrawBackground(mech->SlotIndex, std::nullopt);
        }
    }
}

auto MCLogInvScreen::CreatePurVehiclePane(bool pilotsOnly) -> void
{
    // The original painted each tab's rows into a new picture of the tab (the content's width, and a row for each,
    // at least the pane's height); the store's list draws the rows, laid out the same.
    const std::shared_ptr<MCListView> pane = GlobalLog()->PurchaseScreen->UnitPane.Lock();
    auto layout = [&](uint8_t background)
    { return MCListLayout{UnitBlockHeight, pane->Width() - 0xd, pane->Height(), background}; };

    if (!pilotsOnly)
    {
        // The store shows the mechs from the top, whichever screen is shown (the original put the new mech picture
        // in the pane).
        StoreLayout(MCStoreTab::Mechs) = layout(0x10);
        pane->Show(MechStoreSource(), StoreLayout(MCStoreTab::Mechs), true);

        for (const std::shared_ptr<MCPurMech>& purMech : GlobalLog()->PurMechList->Mechs)
        {
            PrepareInfoDescription(purMech->Variants[static_cast<size_t>(purMech->CurVariant)]->Description);
        }

        StoreLayout(MCStoreTab::Vehicles) = layout(0x10);

        for (const std::shared_ptr<MCPurVehicle>& purVehicle : GlobalLog()->PurVehicleList->Vehicles)
        {
            PrepareInfoDescription(purVehicle->Data->Description);
        }

        StoreLayout(MCStoreTab::Components) = layout(0x10);

        for (const std::shared_ptr<MCLogInventoryItem>& item : GlobalLog()->PurchaseComponents->Items)
        {
            PrepareInfoDescription(item->Description);
        }
    }

    // The pilots for hire (those not yet hired), every time. Original behaviour (OB-076): when only the pilots were
    // rebuilt, the old pilot picture was replaced without being freed.
    StoreLayout(MCStoreTab::Pilots) = layout(0xff);

    for (const std::shared_ptr<MCPurPilotData>& pilot : GlobalLog()->PurPilotList->Pilots)
    {
        if (pilot->Status == MCPurPilotData::ForHire)
        {
            PrepareInfoDescription(pilot->Description);
        }
    }
}

auto MCLogInvScreen::StoreList() -> std::shared_ptr<MCListView>
{
    if (GlobalLog()->PurchaseScreen == nullptr)
    {
        return nullptr;
    }

    return GlobalLog()->PurchaseScreen->UnitPane.Lock();
}

auto MCLogInvScreen::RefreshStoreRows() -> void
{
    if (const std::shared_ptr<MCListView> store = StoreList(); store != nullptr)
    {
        for (const MCGuiHandle<MCListRow>& handle : store->Rows())
        {
            if (const std::shared_ptr<MCListRow> row = handle.Lock(); row != nullptr)
            {
                row->OnRefresh();
            }
        }
    }
}

auto MCLogInvScreen::CreateMechInvBlock() -> void
{
    // The original also painted every mech's row into a new tab picture (making the diagrams it lacked); the list
    // draws the rows now, and each row makes its diagram.
    int32_t index = 0;

    for (const std::shared_ptr<MCLogMech>& mech : GlobalLog()->MechList->Mechs)
    {
        mech->ListIndex = index++;
    }
}

auto MCLogInvScreen::CreateVhclInvBlock() -> void
{
    // As CreateMechInvBlock: the list draws the rows, each row makes its diagram.
    int32_t index = 0;

    for (const std::shared_ptr<MCLogVehicle>& vehicle : GlobalLog()->VehicleList->Vehicles)
    {
        vehicle->ListIndex = index++;
    }
}

auto MCLogInvScreen::CreatePilotInvBlock() const -> void
{
    // The original also painted every pilot's row into a new tab picture; the list draws the rows now.
    int32_t index = 0;

    for (const std::shared_ptr<MCLogWarrior>& warrior : GlobalLog()->WarriorList->Warriors)
    {
        warrior->ListIndex = index++;
    }
}

auto MCLogInvScreen::DrawBlankInvInfoBlock(int32_t tab) -> void
{
    if (tab < 0)
    {
        tab = GlobalLog()->CurrentInvTab;
    }

    if (tab < 0 || tab > 3)
    {
        return;
    }

    if (std::shared_ptr<MCLogInvScreen> screen = Of(*GlobalLog()->CurrentScreen()))
    {
        screen->Info.Blank(tab);
    }
    else
    {
        GlobalLog()->InventoryIconPorts[tab]->CopyTo(GlobalLog()->CurrentScreen()->Lport()->Frame(), 2, 0x18a, false);
    }
}

auto MCLogInvScreen::CreateCompInvBlock() -> void
{
    // The original also painted every row into a new tab picture; the list draws the rows now. The rows shown take
    // their state again, as each block's drawBackground did.
    GlobalLog()->ReIndexInventory();

    for (const std::shared_ptr<MCLogInventoryItem>& item : GlobalLog()->ComponentInventory->Items)
    {
        RefreshComponentRows(*item);
    }
}

auto MCLogInvScreen::RefreshComponentRows(const MCLogInventoryItem& item) -> void
{
    for (const std::shared_ptr<MCListView>& list : InventoryLists())
    {
        if (const std::shared_ptr<MCCompInventoryBlock> row = list->RowOf<MCCompInventoryBlock>(item); row != nullptr)
        {
            row->OnRefresh();
        }
    }
}

auto MCLogInvScreen::SetUpMechInv(bool resetScroll, bool redrawTabs) -> void
{
    GlobalLog()->CurrentInvTab = 0;
    DrawInvTabArt(*this, 0, redrawTabs);
    ShowInvSource(MechSource(), resetScroll);
}

auto MCLogInvScreen::SetUpMechPurchase() const -> void
{
    if (GlobalLog()->CurrentScreen() != GlobalLog()->PurchaseScreen)
    {
        return;
    }

    // Each mech shows the first variant still on sale (its row takes it when the list refreshes).
    for (const std::shared_ptr<MCPurMech>& purMech : GlobalLog()->PurMechList->Mechs)
    {
        int32_t variant = 0;

        if (purMech->Variants[0]->NumAvailable == 0)
        {
            if (purMech->Variants[1]->NumAvailable != 0)
            {
                variant = 1;
            }
            else if (purMech->Variants[2]->NumAvailable != 0)
            {
                variant = 2;
            }
        }

        purMech->CurVariant = variant;
    }

    ShowStore(MCStoreTab::Mechs, MechStoreSource());
}

auto MCLogInvScreen::SetUpPilotInv(bool resetScroll, bool redrawTabs) -> void
{
    GlobalLog()->CurrentInvTab = 1;
    DrawInvTabArt(*this, 1, redrawTabs);

    // The list keeps the row of a pilot still there (the one being dragged, which a drop refills the list under).
    ShowInvSource(PilotSource(), resetScroll);
}

auto MCLogInvScreen::SetUpCompInv(bool resetScroll, bool redrawTabs) -> void
{
    GlobalLog()->CurrentInvTab = 2;
    DrawInvTabArt(*this, 2, redrawTabs);
    ShowInvSource(ComponentSource(), resetScroll);
}

auto MCLogInvScreen::SetUpVhclInv(bool resetScroll, bool redrawTabs) -> void
{
    GlobalLog()->CurrentInvTab = 3;
    DrawInvTabArt(*this, 3, redrawTabs);
    ShowInvSource(VehicleSource(), resetScroll);
}

auto MCLogInvScreen::SetUpVehiclePurchase() const -> void
{
    ShowStore(MCStoreTab::Vehicles, VehicleStoreSource());
}

auto MCLogInvScreen::SetUpPilotPurchase() const -> void
{
    ShowStore(MCStoreTab::Pilots, PilotStoreSource());
}

auto MCLogInvScreen::SetUpCompPurchase() const -> void
{
    ShowStore(MCStoreTab::Components, CompStoreSource());
}

auto MCLogInvScreen::RemovePilot() -> void
{
    // The original copied the old rows into a new picture (3 pixels narrower than the others), closing the gap: the
    // rows below the hired one moved up a row. The list shows the pilots still for hire in that picture's layout.
    const std::shared_ptr<MCListView> pane = GlobalLog()->PurchaseScreen->UnitPane.Lock();
    MCListLayout& layout = StoreLayout(MCStoreTab::Pilots);
    layout = MCListLayout{UnitBlockHeight, pane->Width() - 0x10, pane->Height(), 0xff};
    pane->Show(PilotStoreSource(), layout, true);
}

auto MCLogInvScreen::Draw() -> void
{
    if (Lport()->ViewOpen())
    {
        VfxPaneCopy(LogScreenArt(BackgroundArt)->Frame(), 0, 0, Lport()->Frame(), 0, 0, -1);
        DrawInfo(*Lport());
        GlobalLog()->DrawScreenChrome(*this, Lport()->Frame());
    }

    MCLogObject::Draw();
}

auto MCLogInvScreen::InitLive(std::string_view artName) -> void
{
    BackgroundArt = artName;
    InvScreens.push_back(std::static_pointer_cast<MCLogInvScreen>(shared_from_this()));
}

auto MCLogInvScreen::ShowInfo(MCInvInfoBox::Kind kind, std::weak_ptr<MCGuiObject> source) -> void
{
    Info.InfoKind = kind;
    Info.Source = std::move(source);
    Info.Details = nullptr;
}

auto MCLogInvScreen::ShowDetails(MCInvInfoBox::Kind kind, std::function<void(MCLogPort& port)> details) -> void
{
    Info.InfoKind = kind;
    Info.Source.reset();
    Info.Details = std::move(details);
}

auto MCLogInvScreen::ShowComponentInfo(const MCLogInventoryItem& item, bool repairItem) -> void
{
    MCComponentTexts texts = MCCompInventoryBlock::Texts(item.MasterID);
    Info.InfoKind = repairItem ? MCInvInfoBox::Kind::RepairItem : MCInvInfoBox::Kind::Component;
    Info.Source.reset();
    Info.Details = nullptr;
    Info.ComponentPicture = item.RangeIndex;
    Info.RangeText = std::move(texts.Range);
    Info.DamageText = std::move(texts.Damage);
    Info.RecycleText = std::move(texts.Recycle);
    Info.Description = item.Description;
}

auto MCLogInvScreen::DrawInfo(MCLogPort& port) -> void
{
    if (Info.Header >= 0)
    {
        const std::shared_ptr<MCLogPort> header = LogScreenArt(InvHeaderArt[static_cast<size_t>(Info.Header)]);
        VfxPaneCopy(header->Frame(), 0, 0, port.Frame(), 0xc4, 0x65, -1);
    }

    if (Info.Art < 0)
    {
        return;
    }

    GlobalLog()->InventoryIconPorts[Info.Art]->CopyTo(port.Frame(), 2, 0x18a, false);

    switch (Info.InfoKind)
    {
        case MCInvInfoBox::Kind::None:
        {
            break;
        }

        case MCInvInfoBox::Kind::Component:
        case MCInvInfoBox::Kind::RepairItem:
        {
            DrawComponentInfo(Info, port);
            break;
        }

        case MCInvInfoBox::Kind::RepairMech:
        {
            if (const std::shared_ptr<MCGuiObject> source = Info.Source.lock())
            {
                static_cast<MCMechRepairBlock&>(*source).DrawInfo(port);
            }

            break;
        }

        default:
        {
            if (Info.Details)
            {
                Info.Details(port);
            }

            break;
        }
    }
}

auto MCLogInvScreen::Of(const MCGuiObject& screen) -> std::shared_ptr<MCLogInvScreen>
{
    for (const std::weak_ptr<MCLogInvScreen>& entry : InvScreens)
    {
        if (MCRefersTo(entry, screen))
        {
            return entry.lock();
        }
    }

    return nullptr;
}

auto MCLogInvScreen::ForgetInfoSource(const MCLogObject& source) -> void
{
    for (const std::weak_ptr<MCLogInvScreen>& entry : InvScreens)
    {
        const std::shared_ptr<MCLogInvScreen> screen = entry.lock();

        if (screen != nullptr && MCRefersTo(screen->Info.Source, source))
        {
            screen->Info.InfoKind = MCInvInfoBox::Kind::None;
            screen->Info.Source.reset();
        }
    }
}

#include "stdafx.h"
#include "logistics/MCMechRepairBlock.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCUpdateDisplay.h"
#include "main/MCGamePaths.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCDragIcon.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCMechBay.h"
#include "logistics/MCMechInventoryBlock.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCTicker.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "network/MCMultiPlayer.h"
#include "object/MCMasterComponent.h"
#include "sound/MCSoundSystem.h"
#include "vfx/MCVfxFunctions.h"
#include "logistics/MCBriefingScreen.h"

namespace
{
    /// <summary>What a mech row of the repair screen is doing with the mouse (one mouse: shared by every row).</summary>
    struct MCRepairDrag
    {
        /// <summary>The dragged item copy's item number.</summary>
        uint8_t ItemNum = 0;
        /// <summary>The dragged item's component.</summary>
        uint8_t MasterID = 0;
        /// <summary>Something is dragged with the left button held.</summary>
        bool LeftDrag = false;
        /// <summary>The drag icon's position.</summary>
        int32_t X = 0;
        int32_t Y = 0;
        /// <summary>Set by a right-button press, cleared by the release.</summary>
        bool RightHeld = false;
        /// <summary>The whole mech is dragged.</summary>
        bool Mech = false;
        /// <summary>An item is dragged out of the weapon list.</summary>
        bool Item = false;
        /// <summary>A repair slider is dragged; the GUI's <c>LastY</c> is the slider, <c>LastX</c> its position.</summary>
        bool Slider = false;
        /// <summary>Set when a click selects another mech; never read.</summary>
        bool MechSelected = false;
        /// <summary>A repair button is held (the buttons are redrawn on release).</summary>
        bool RepairButtonDown = false;
        /// <summary>The dragged item copy's damage.</summary>
        int32_t ItemHits = 0;
        /// <summary>The dragged item's inventory position, -1 when none.</summary>
        int32_t ItemIndex = -1;
    };

    MCRepairDrag Drag;

    void DrawLine(const MCPane& pane, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color)
    {
        VfxLineDraw(pane, x0, y0, x1, y1, color);
    }

    void WriteLine(MCGuiFont& font, const MCPane& pane, int32_t x, int32_t y, std::string_view text)
    {
        font.WriteString(pane, x, y, text);
    }

    MCRepairScreen& RepairScreen()
    {
        return *GlobalLog()->RepairScreen;
    }

    /// <summary>
    /// Shows a component's info under the inventory: its picture (<c>lscicc</c>), the range, damage and recycle texts
    /// and its description (loaded when missing; the original loaded it when it made the item an inventory block for
    /// the texts, the first time).
    /// </summary>
    void DrawItemInfo(MCLogInventoryItem& item, MCInventoryList& inventory)
    {
        inventory.LoadDescription(0, item);
        PrepareInfoDescription(item.Description);
        RepairScreen().ShowComponentInfo(item, true);
    }

}

MCMechRepairBlock::~MCMechRepairBlock()
{
    Destroy();
}

auto MCMechRepairBlock::StripUnrepaired() -> void
{
    MCMechBay::StripUnrepaired(*Mech());
    DrawBackground(SlotIndex(), std::nullopt);
    MCRepairScreen::CreateCompInvBlock();
    RepairScreen().SetUpCompInv(false, true);
}

auto MCMechRepairBlock::Init(std::shared_ptr<MCLogMech> logMech, bool briefingLayout) -> void
{
    _Mech = logMech;
    _BriefingLayout = briefingLayout;
    ShortRangeWeapons.clear();
    MediumRangeWeapons.clear();
    LongRangeWeapons.clear();
    Equipment.clear();
    ItemHits.clear();
    DragPort.reset();
    MCLogObject::InitWithoutPort(0, 0, 0x19a, 0x70);
    ListPosition = Mech()->NameIndex;

    InventoryPane =
        CreateChild<MCScrollPane>([](MCScrollPane& pane) { pane.Init(0x62, 0x58, 0x135, 0x11, std::string_view()); });

    SliderArtPort = std::make_shared<MCLogPort>();
    SliderArtPort->Load(std::format("{}logart\\lsrupm05.tga", ArtPath));

    // The sliders' scales and floors: where the mech arrived (MCMechBay::KnobsOf).
    const MCMechBay::MCKnobs knobs = MCMechBay::KnobsOf(*Mech());
    ArmorPixelScale = knobs.ArmorScale;
    InternalPixelScale = knobs.InternalScale;

    const auto engine =
        std::ranges::find_if(Mech()->Inventory->Items, [](const std::shared_ptr<MCLogInventoryItem>& item)
                             { return ComponentForm(item->MasterID) == MCComponentForm::Engine; });
    _EngineStat = (*engine)->Stats.front();
    ArmorSliderStart = knobs.ArmorStart;
    InternalSliderStart = knobs.InternalStart;
    EngineSliderStart = knobs.EngineStart;
}

auto MCMechRepairBlock::SlotIndex() const -> int32_t
{
    const std::shared_ptr<MCLogMech> mech = Mech();
    return mech != nullptr ? mech->SlotIndex : 0;
}

auto MCMechRepairBlock::OnDestroy() -> void
{
    MCLogInvScreen::ForgetInfoSource(*this);
    SliderArtPort.reset();
    ShortRangeWeapons.clear();
    MediumRangeWeapons.clear();
    LongRangeWeapons.clear();
    Equipment.clear();
    ItemHits.clear();
    MCLogObject::OnDestroy();
}

auto MCMechRepairBlock::HandleEvent(MCGuiEvent& event) -> void
{
    int32_t eventX = event.X;
    int32_t localX = eventX - GlobalX();
    int32_t eventY = event.Y;
    int32_t localY = eventY - GlobalY();

    if (OnPurchaseScreen())
    {
        return;
    }

    if (!Drag.LeftDrag)
    {
        if (!Parent.expired() && !Drag.RightHeld && (event.Type == 8 || event.Type == 9))
        {
            ParentObject()->HandleEvent(event);
            return;
        }

        if (event.Key == 0)
        {
            // The ticker explains what the cursor is over.
            static constexpr struct
            {
                RECT Area{};
                uint32_t StringId = 0;
            } helpAreas[] = {
                {{0x72, 5, 0x7a, 0x6a}, 0x37},   {{0x88, 2, 0xd2, 5}, 0x38},      {{0xea, 3, 299, 0x12}, 0x39},
                {{0xea, 0x17, 299, 0x26}, 0x3a}, {{0xea, 0x37, 299, 0x3f}, 0x3b}, {{0xea, 0x4c, 299, 0x54}, 0x3c},
                {{0xea, 0x61, 299, 0x69}, 0x3d}, {{0x134, 6, 0x195, 0xf}, 0x3e},  {{0x134, 0x11, 0x195, 0x69}, 0x3f},
                {{0x81, 6, 0xe6, 0x6c}, 0x40},
            };

            POINT point = {localX, localY};
            RECT pilotArea = {1, 0x1c, 0x6a, 0x6c};
            uint32_t stringId = 0;

            if (PtInRect(pilotArea, point) != 0)
            {
                stringId = Mech()->PilotIndex < 0 ? 0x35 : 0x36;
            }
            else
            {
                for (const auto& help : helpAreas)
                {
                    if (PtInRect(help.Area, point) != 0)
                    {
                        stringId = help.StringId;
                        break;
                    }
                }
            }

            GlobalLog()->Ticker->SetString(stringId != 0 ? LoadGameString(stringId, 0xfe) : std::string());
        }
    }

    std::shared_ptr<MCLogWarrior> warrior;
    GlobalLog()->AssignedWarriorList->GetWarriorInfo(Mech()->PilotIndex, warrior);
    int32_t eventType = event.Type;

    switch (eventType)
    {
        case 1:
        {
            if (Drag.RightHeld)
            {
                return;
            }
            break;
        }

        case 3:
        {
            if (Drag.LeftDrag || DebugFunction1(localX, localY) != 0)
            {
                return;
            }

            Drag.RightHeld = true;
            break;
        }

        case 4:
        {
            if (Drag.RightHeld)
            {
                return;
            }

            [[fallthrough]];
        }
        case 6:
        {
            if (Drag.LeftDrag && eventType == 6)
            {
                return;
            }

            HandleDrop(event, eventType);
            return;
        }

        case 7:
        {
            const auto pane = std::static_pointer_cast<MCScrollPane>(Child(0));

            if (pane->GlobalX() < eventX)
            {
                if (eventX < pane->GlobalX() + pane->Width() - 0xd && pane->GlobalY() < eventY &&
                    eventY < pane->GlobalY() + pane->Height())
                {
                    // Over the weapon list: show the item's info.
                    int32_t line = (eventY - pane->GlobalY() + pane->GetScrollOffset()) / (GreenFont->Height() + 2);
                    std::shared_ptr<MCLogInventoryItem> item = GetItemFromScrollPane(line, Drag.ItemNum);

                    if (item != nullptr)
                    {
                        MCRepairScreen::DrawBlankInvInfoBlock(2);
                        DrawItemInfo(*item, *Mech()->Inventory);
                        return;
                    }
                }
            }
            else if (eventX < pane->GlobalX())
            {
                // Over the mech: show its info.
                MCRepairScreen::DrawBlankInvInfoBlock(0);

                if (DragPort == nullptr)
                {
                    DragPort = std::make_shared<MCLogPort>();
                    DragPort->Init(0x1c, 0x1e);
                    VfxPaneWipe(DragPort->Frame(), 0x10);

                    for (int32_t location = 0; location < 8; ++location)
                    {
                        MCLogistics::DrawMechBodyLoc(*Mech(), location, *DragPort, 2, 0);
                    }

                    // The battle rating bar along the left edge: 26 pixels at 18010.
                    int32_t bar =
                        static_cast<int32_t>(static_cast<double>(Mech()->BattleRating) * (1.0 / 18010.0) * 26.0);
                    DrawLine(DragPort->Frame(), 0, 0x1b, 0, 0x1b - bar, 0xe4);
                    DrawLine(DragPort->Frame(), 1, 0x1b, 1, 0x1b - bar, 0xe4);
                }

                PrepareInfoDescription(Mech()->Description);
                RepairScreen().ShowInfo(MCInvInfoBox::Kind::RepairMech, weak_from_this());
            }

            if (Drag.LeftDrag)
            {
                Drag.X = eventX - 0xf;
                Drag.Y = eventY - 0xf;
                MCDragIcon::Current()->MoveTo(Drag.X, Drag.Y, false);
                return;
            }

            if (Drag.Slider)
            {
                DragSlider(localX);
            }

            return;
        }

        default:
            return;
    }

    // A button press (1 or 3).
    if (RepairScreen().SelectedMech() != Mech())
    {
        RepairScreen().SelectMech(Mech());
        Drag.MechSelected = true;
        Drag.RightHeld = false;
        return;
    }

    const auto pane = std::static_pointer_cast<MCScrollPane>(Child(0));

    if (pane->GlobalX() - 0xd + pane->Width() <= eventX && eventX <= pane->GlobalX() + pane->Width() &&
        pane->GlobalY() <= eventY && eventY <= pane->GlobalY() + pane->Height())
    {
        // The weapon list's scroll bar.
        pane->HandleEvent(event);
        DrawInventory(std::nullopt);
        return;
    }

    if (pane->GlobalX() < eventX && eventX < pane->GlobalX() + pane->Width() - 0xd && pane->GlobalY() < eventY &&
        eventY < pane->GlobalY() + pane->Height())
    {
        // Pick an item out of the weapon list.
        int32_t line = (eventY - pane->GlobalY() + pane->GetScrollOffset()) / (GreenFont->Height() + 2);
        std::shared_ptr<MCLogInventoryItem> item = GetItemFromScrollPane(line, Drag.ItemNum);

        if (item == nullptr)
        {
            PlayLogSound(0x33);
            return;
        }

        GuiSystem().SetCursorVisible(false);
        Drag.MasterID = item->MasterID;
        Drag.Item = true;
        PlayLogSound(0x35);
        GuiSystem().Grab(weak_from_this());

        if (eventType == 1)
        {
            Drag.LeftDrag = true;
        }

        SetUpItemDragIcon(*item, Drag.ItemNum, event);
        return;
    }

    if (0 < localX && localX < 100 && 0x1c < localY && localY < 0x6d && Mech()->PilotIndex >= 0)
    {
        // Pick up the pilot: the portrait becomes the drag icon and its place is blanked.
        SoundSystem()->PlayPilotSpeech(warrior->PilotAudio, 10);
        GuiSystem().SetCursorVisible(false);
        GuiSystem().Grab(weak_from_this());

        if (eventType == 1)
        {
            Drag.LeftDrag = true;
        }

        // Made on the screen, but kept off it until it is shown below (the original added it there).
        const std::shared_ptr<MCDragIcon> icon = MCDragIcon::Create(
            RepairScreen(), eventX - 0xf, eventY - 0xf, 0x20, 0x20,
            [this](MCLogPort& surface) { OnBeginDragPilot(surface); }, true);
        ClearPilot();
        Drag.X = GlobalX() + 5;
        Drag.Y = GlobalY() + 0x26;
        icon->BringToFront(true);
        icon->ShowAt(eventX - 0xf, eventY - 0xf);
        return;
    }

    if (localX < 0xe7)
    {
        // Pick up the whole mech.
        if (GlobalX() <= eventX && eventX <= GlobalX() + Width() && GlobalY() <= eventY &&
            eventY <= GlobalY() + Height())
        {
            PlayLogSound(0x35);
            GuiSystem().SetCursorVisible(false);
            GuiSystem().Grab(weak_from_this());
            Drag.Mech = true;

            if (eventType == 1)
            {
                Drag.LeftDrag = true;
            }

            Drag.Y = eventY - 0x10;
            Drag.X = eventX - 0x10;
            const std::shared_ptr<MCDragIcon> icon = MCDragIcon::Create(
                RepairScreen(), Drag.X, Drag.Y, 0x20, 0x20, [this](MCLogPort& surface) { OnBeginDragMech(surface); });
            icon->ShowAt(Drag.X, Drag.Y);
        }

        return;
    }

    if (MultiPlayer() != nullptr)
    {
        return;
    }

    // The sliders.
    int32_t slider;
    int32_t sliderPos;
    const MCLogMech::RepairSliderKnobs knobs = Mech()->RepairSliders;

    if (knobs.Armor - 2 <= localX && localX <= knobs.Armor + 6 && 0x36 <= localY && localY <= 0x3d)
    {
        slider = 0;
        sliderPos = knobs.Armor;
    }
    else if (knobs.Internal - 2 <= localX && localX <= knobs.Internal + 6 && 0x4b <= localY && localY <= 0x52)
    {
        slider = 1;
        sliderPos = knobs.Internal;
    }
    else if (knobs.Engine - 2 <= localX && localX <= knobs.Engine + 6 && 0x60 <= localY && localY <= 0x67)
    {
        slider = 2;
        sliderPos = knobs.Engine;
    }
    else
    {
        // The repair buttons.
        if (localX < 0xea || 299 < localX || RepairScreen().SelectedMech() != Mech())
        {
            return;
        }

        Drag.RepairButtonDown = true;

        if (0x17 <= localY && localY <= 0x26 && CanRepairStructure)
        {
            RepairStructure();
        }
        else if (3 <= localY && localY <= 0x12 && CanRepairItems)
        {
            RepairItems();
        }

        return;
    }

    PlayLogSound(0x35);
    LastY = slider;
    Drag.Slider = true;
    LastX = sliderPos;
    GuiSystem().Grab(weak_from_this());
}

auto MCMechRepairBlock::HandleDrop(MCGuiEvent& event, int32_t eventType) -> void
{
    // The mech dropped on the inventory leaves the force, and the force's new listing destroys this row: it lives
    // until the drop is done.
    const std::shared_ptr<MCGuiObject> self = shared_from_this();
    Drag.RightHeld = false;

    if (Drag.RepairButtonDown && Mech() == RepairScreen().SelectedMech())
    {
        DrawButtons(std::nullopt);
        Drag.RepairButtonDown = false;
    }

    if (GuiSystem().GrabbedObject() == nullptr)
    {
        return;
    }

    GuiSystem().SetCursorVisible(true);
    GuiSystem().Release();
    Drag.LeftDrag = false;
    DrawButtons(std::nullopt);
    MCDragIcon::Remove();
    bool droppedOnInventory = eventType == 6 || OverPaneInside(*(RepairScreen().InventoryPane), event);

    if (!Drag.Item)
    {
        if (Drag.Slider)
        {
            Drag.Slider = false;
            GuiSystem().Release();
            DrawArmorSlider(std::nullopt);
            DrawInternalSlider(std::nullopt);
            DrawEngineSlider(std::nullopt);
            DrawStatusBar();
            DrawButtons(std::nullopt);
            return;
        }

        if (!Drag.Mech)
        {
            // The pilot: dropped on the inventory, it leaves the mech.
            GuiSystem().Release();
            Drag.LeftDrag = false;
            MCDragIcon::Remove();
            std::shared_ptr<MCLogMech> pilotsMech = Mech();
            std::shared_ptr<MCLogWarrior> pilot;
            GlobalLog()->AssignedWarriorList->GetWarriorInfo(pilotsMech->PilotIndex, pilot);

            if (pilot != nullptr && droppedOnInventory)
            {
                std::shared_ptr<MCLogMech> current = Mech();
                const int32_t row = SlotIndex();
                MCMechBay::PilotOut(*current);
                DrawBackground(current->SlotIndex, std::nullopt);
                DrawBackground(row, std::nullopt);
                RepairScreen().CreatePilotInvBlock();
                RepairScreen().SetUpPilotInv(true, true);
                SoundSystem()->PlayPilotSpeech(pilot->PilotAudio, 2);
                return;
            }

            PlayLogSound(0x33);
            DrawBackground(pilotsMech->SlotIndex, std::nullopt);
            return;
        }

        // The whole mech: dropped on the inventory, it leaves the force.
        Drag.Mech = false;

        if (!droppedOnInventory)
        {
            PlayLogSound(0x33);
            return;
        }

        MCMechBay::OutOfDrop(*Mech());
        RepairScreen().SelectMech(nullptr);
        PlayLogSound(0x34);
        std::shared_ptr<MCLogMech> leaving = Mech();

        if (MCMechBay::LeaveForce(*leaving))
        {
            RepairScreen().CreatePilotInvBlock();
            RepairScreen().SetUpPilotInv(true, true);
        }

        if (leaving == RepairScreen().SelectedMech())
        {
            RepairScreen().ClearSelectedMech();
        }

        // The new listing destroys this row (the original took the block out of the pane).
        RepairScreen().RemoveMechFromList(*leaving);
        MCRepairScreen::CreateMechInvBlock();
        RepairScreen().SetUpMechInv(true, true);
        return;
    }

    // An item from the weapon list.
    Drag.Item = false;
    MCRepairScreen::DrawBlankInvInfoBlock(-1);

    if (droppedOnInventory)
    {
        // Into the component inventory (a damaged item is thrown away).
        MCMechBay::OutOfDrop(*Mech());
        PlayLogSound(0x34);
        std::shared_ptr<MCLogInventoryItem> item;
        const MCMechBay::MCTaken taken{
            .MasterID = Drag.MasterID, .ItemNum = Drag.ItemNum, .Hits = static_cast<uint8_t>(Drag.ItemHits)};

        switch (MCMechBay::ToSpares(taken, item))
        {
            case MCMechBay::MCSpareRow::Counted:
                MCLogInvScreen::RefreshComponentRows(*item);
                break;
            case MCMechBay::MCSpareRow::Listed:
            {
                MCRepairScreen::CreateCompInvBlock();
                RepairScreen().SetUpCompInv(false, true);
                break;
            }
            case MCMechBay::MCSpareRow::Thrown:
                break;
        }
    }
    else
    {
        // Back into the selected mech (with a weapon's ammo).
        std::shared_ptr<MCLogMech> target = RepairScreen().SelectedMech();
        PlayLogSound(0x34);
        MCMechBay::PutBack(*target, MCMechBay::MCTaken{.MasterID = Drag.MasterID,
                                                       .ItemNum = Drag.ItemNum,
                                                       .Hits = static_cast<uint8_t>(Drag.ItemHits)});

        if (const std::shared_ptr<MCMechRepairBlock> targetRow = RepairScreen().RowOf(*target); targetRow != nullptr)
        {
            targetRow->DrawBackground(target->SlotIndex, std::nullopt);
        }

        DrawStatusBar();
        RepairScreen().SetUpCompInv(false, true);
    }

    Drag.ItemIndex = -1;
    DrawButtons(std::nullopt);
}

auto MCMechRepairBlock::DragSlider(int32_t localX) -> void
{
    // Dragging a repair slider: undo the drag so far, then repair up to the new position as far as the resource points
    // go (MCMechBay::MoveKnob).
    static constexpr std::array<MCMechBay::MCKnob, 3> Knobs = {MCMechBay::MCKnob::Armor, MCMechBay::MCKnob::Internal,
                                                               MCMechBay::MCKnob::Engine};

    if (LastY < 0 || static_cast<size_t>(LastY) >= Knobs.size())
    {
        SDL_Log("MCMechRepairBlock: a slider drag on no slider (%d) of %s", LastY, Mech()->FileName.c_str());
        return;
    }

    MCMechBay::OutOfDrop(*Mech());
    DragPort.reset();
    const MCMechBay::MCKnobs knobs{.ArmorScale = ArmorPixelScale,
                                   .ArmorStart = ArmorSliderStart,
                                   .InternalScale = InternalPixelScale,
                                   .InternalStart = InternalSliderStart,
                                   .EngineStart = EngineSliderStart};
    const std::optional<int32_t> last = MCMechBay::MoveKnob(*Mech(), knobs, Knobs[static_cast<size_t>(LastY)], localX);

    if (!last.has_value())
    {
        return;
    }

    // The rows draw the sliders and the diagram each frame; the status bar's condition is worked out again.
    DrawStatusBar();
    LastX = *last;
    DrawBR(std::nullopt);
}

auto MCMechRepairBlock::RepairItems() -> void
{
    // Replace every damaged copy from the component inventory; list the ones missing.
    MCMechBay::OutOfDrop(*Mech());
    Drag.RepairButtonDown = true;
    PlayLogSound(0x35);
    _PressedButton = 1;
    UpdateDisplay(false, false, 0, false, 0);
    std::vector<std::shared_ptr<MCLogInventoryItem>> used;
    const std::string missing = MCMechBay::Refit(*Mech(), used);

    for (const std::shared_ptr<MCLogInventoryItem>& spare : used)
    {
        MCLogInvScreen::RefreshComponentRows(*spare);
    }

    if (!missing.empty())
    {
        // Ask whether to strip the damaged items that have no replacement (StripUnrepaired).
        GuiSystem().Release();
        Drag.LeftDrag = false;
        MCMenuDialog& dialog = *GlobalLog()->Refit;
        dialog.SetText(missing);
        dialog.Callback = nullptr;
        dialog.SetTwoButton(true);
        dialog.Ok.SetPictures("bh_okay.tga", "bg_okay.tga");
        dialog.Ok.Exec = [this] { StripUnrepaired(); };
        dialog.Cancel.SetPictures("bh_cancl.tga", "bg_cancl.tga");
        dialog.Activate();
        PlayLogSound(0x33);
        DrawBackground(SlotIndex(), std::nullopt);
        return;
    }

    MCRepairScreen::CreateCompInvBlock();
    RepairScreen().SetUpCompInv(false, true);
    DrawBackground(SlotIndex(), std::nullopt);
}

auto MCMechRepairBlock::RepairStructure() -> void
{
    // The engine level by level, then internal structure, then armor, as far as the resource points go.
    MCMechBay::OutOfDrop(*Mech());
    DragPort.reset();
    PlayLogSound(0x35);
    _PressedButton = 2;
    UpdateDisplay(false, false, 0, false, 0);
    const bool shortOfPoints = MCMechBay::RepairAll(*Mech());
    DrawBackground(SlotIndex(), std::nullopt);

    if (shortOfPoints)
    {
        GuiSystem().Release();
        Drag.LeftDrag = false;
        ShowLogMessage(0x57);
        PlayLogSound(0x33);
    }

    DrawBackground(SlotIndex(), std::nullopt);
}

auto MCMechRepairBlock::DrawBackground(int32_t row, MCOptionalRef<MCLogPort> port) -> void
{
    const std::shared_ptr<MCLogMech> mech = Mech();
    const bool framed = !_BriefingLayout && GlobalLog()->CurrentScreen() == GlobalLog()->RepairScreen &&
                        RepairScreen().SelectedMech() == mech;
    const bool hasPilot = mech->PilotIndex >= 0 || !mech->NetworkPilot.expired();

    if (!port)
    {
        // The repair screen's rows are drawn each frame (DrawRow) from the state; the pilot is back in place and no
        // button shows pressed. The rest is what the paint did besides painting (the weapon lists, the battle rating).
        _PilotLifted = false;
        _PressedButton = 0;

        if (hasPilot)
        {
            SetPilotStats(std::nullopt);
            SetPilotHealth(std::nullopt);
        }

        DrawButtons(std::nullopt);
        DrawDamageDiagram(std::nullopt);
        DrawBR(std::nullopt);
        DrawArmorSlider(std::nullopt);
        DrawInternalSlider(std::nullopt);
        DrawEngineSlider(std::nullopt);
        SetInventory(std::nullopt);
        DrawInventory(std::nullopt);
        return;
    }

    // The briefing box's picture.
    PaintBase(*port, 0, row < 0, framed);

    if (hasPilot)
    {
        SetPilotStats(port);
        SetPilotHealth(port);
    }

    DrawButtons(port);
    DrawDamageDiagram(port);
    DrawBR(port);
    DrawArmorSlider(port);
    DrawInternalSlider(port);
    DrawEngineSlider(port);
}

auto MCMechRepairBlock::PaintBase(MCLogPort& port, int32_t top, bool briefing, bool framed) -> void
{
    const std::shared_ptr<MCLogPort> rowArt = briefing ? LogScreenArt("lsbbkm00.tga") : GlobalLog()->RepairBackPort;

    if (rowArt == nullptr)
    {
        return;
    }

    // Drawn in place: the original put the base together in a new picture (zeroed, as the port's heap gives it) and
    // copied it as it is.
    MCLogBlockPort back(port.Frame(), 0, top, rowArt->Width(), rowArt->Height(), false);

    if (briefing)
    {
        VfxPaneCopy(rowArt->Frame(), 0, 0, back.Frame(), 0, 0, -1);
    }
    else
    {
        VfxPaneWipe(back.Frame(), 0);
        rowArt->CopyTo(back.Frame(), 0, 0, true);
    }

    std::shared_ptr<MCLogMech> logMech = Mech();
    static constexpr std::array<std::string_view, 3> chassisArt = {"lscflma", "lscflmw", "lscflmj"};

    if (logMech->NameVariant >= 0 && logMech->NameVariant < 3)
    {
        CopyArt(back, 5, 4,
                std::format("{}{:02}.tga", chassisArt[static_cast<size_t>(logMech->NameVariant)], logMech->NameIndex));
    }

    if (const std::shared_ptr<MCLogPort> art = LogScreenArt(std::format("lscdsm{:02}.tga", logMech->NameIndex)))
    {
        art->CopyTo(back.Frame(), briefing ? 0xdc : 0xd6, 8, true);
    }

    WriteLine(*BlueDropFont, back.Frame(), 6, 0x12,
              MCFormatPrintf(LoadGameString(0x4e, 0xfe).c_str(), static_cast<double>(logMech->CurTonnage),
                             logMech->WeightClassName.c_str()));

    if (framed)
    {
        // The selected mech's frame.
        DrawLine(back.Frame(), 1, 0, Width() + 1, 0, 0xf2);
        DrawLine(back.Frame(), 1, 0, 1, Height() - 2, 0xf2);
        DrawLine(back.Frame(), 1, Height() - 2, Width() + 1, Height() - 2, 0xf2);
        DrawLine(back.Frame(), Width() + 1, 0, Width() + 1, Height() - 2, 0xf2);
    }
}

auto MCMechRepairBlock::DrawButtons(MCOptionalRef<MCLogPort> port) -> void
{
    bool onRows = !port;

    // The buttons are live when there is something to repair. (Painting the briefing box, the original found nothing
    // to repair: it only looked for the repair screen's rows.)
    CanRepairItems = onRows && ItemsDamaged();
    CanRepairStructure = onRows && StructureDamaged();

    if (onRows)
    {
        // The rows draw their buttons each frame (DrawRow).
        return;
    }

    PaintButtons(*port, 0, false, CanRepairItems, CanRepairStructure);
}

auto MCMechRepairBlock::ItemsDamaged() const -> bool
{
    return MCMechBay::ItemsDamaged(*Mech());
}

auto MCMechRepairBlock::StructureDamaged() const -> bool
{
    return MCMechBay::StructureDamaged(*Mech());
}

auto MCMechRepairBlock::ShowsInventory() const -> bool
{
    // In multiplayer, only the player's own mechs (and the one in the briefing box: this panel, when it is the box's)
    // list their weapons.
    return MultiPlayer() == nullptr || GlobalLog()->ForceMechList->GetMechIndex(Mech()) >= 0 || _BriefingLayout;
}

auto MCMechRepairBlock::PaintButtons(MCLogPort& port, int32_t top, bool onRows, bool items, bool structure) -> void
{
    // Drawn in place: the original painted a scratch picture (wiped to 0xff) and copied it here keyed on 0xff.
    MCLogBlockPort work(port.Frame(), 0, top, Width(), Height(), true);

    if (!items)
    {
        GlobalLog()->RepairPorts[4]->CopyTo(work.Frame(), onRows ? 0xea : 0xf8, 3, true);
    }
    else
    {
        GlobalLog()->RepairPorts[1]->CopyTo(work.Frame(), 0xea, 3, true);
    }

    if (!structure)
    {
        GlobalLog()->RepairPorts[5]->CopyTo(work.Frame(), onRows ? 0xea : 0xf8, 0x17, true);
    }
    else
    {
        GlobalLog()->RepairPorts[3]->CopyTo(work.Frame(), 0xea, 0x17, true);
    }
}

auto MCMechRepairBlock::DrawDamageDiagram(MCOptionalRef<MCLogPort> port) -> void
{
    if (port.has_value())
    {
        PaintDiagram(*port, 0, 0x8e);
    }
}

auto MCMechRepairBlock::PaintDiagram(MCLogPort& port, int32_t top, int32_t xPos) const -> void
{
    const std::shared_ptr<MCLogMech> mech = Mech();
    MCLogBlockPort blank(port.Frame(), xPos, top + 8, 0x4b, 100, true);
    VfxPaneWipe(blank.Frame(), 0x10);

    // The internal structure (shape frames 11..18), then the armor over it (0..7), coloured by damage shade.
    std::array<int32_t, 8> shade = {};

    for (size_t location = 0; location < shade.size(); ++location)
    {
        shade[location] =
            RepairShade(PercentLeft(mech->Internals[location].CurArmor, mech->Internals[location].MaxArmor));
    }

    // Port: drawn through each table itself, never a copy set as the lookaside (VfxShapeTranslateDraw).
    for (int32_t table = 0; table < 5; ++table)
    {
        for (int32_t location = 0; location < 8; ++location)
        {
            if (shade[static_cast<size_t>(location)] == table)
            {
                VfxShapeTranslateDraw(port.Frame(), GlobalLog()->MechRepShapes[mech->NameIndex].Bytes(), location + 0xb,
                                      xPos, top + 8, InternalShadeTable(table));
            }
        }
    }

    for (size_t location = 0; location < shade.size(); ++location)
    {
        shade[location] = RepairShade(PercentLeft(mech->Armor[location].CurArmor, mech->Armor[location].MaxArmor));
    }

    for (int32_t table = 0; table < 5; ++table)
    {
        for (int32_t location = 0; location < 8; ++location)
        {
            if (shade[static_cast<size_t>(location)] == table)
            {
                VfxShapeTranslateDraw(port.Frame(), GlobalLog()->MechRepShapes[mech->NameIndex].Bytes(), location, xPos,
                                      top + 8, ArmorShadeTable(table));
            }
        }
    }
}

auto MCMechRepairBlock::DrawBR(MCOptionalRef<MCLogPort> port) -> void
{
    Mech()->CalcBR();

    if (port.has_value())
    {
        PaintBR(*port, 0);
    }
}

auto MCMechRepairBlock::PaintBR(MCLogPort& port, int32_t top) -> void
{
    // Drawn in place: the original painted a scratch picture (wiped to 0xff) and copied it here keyed on 0xff.
    MCLogBlockPort work(port.Frame(), 0, top, Width(), Height(), true);

    // The battle rating bar: 80 pixels at 18010, rising from y 0x58; the pilot modifier adds to it (or eats into it).
    int32_t rating = Mech()->BattleRating;
    int32_t clamped = static_cast<int32_t>(rating > 18010.0 ? 18010.0 : static_cast<double>(rating));
    int32_t bar = static_cast<int32_t>(static_cast<double>(clamped) * (1.0 / 18010.0) * 80.0);

    if (const std::shared_ptr<MCLogPort> art = LogScreenArt("lsrupm09.tga"))
    {
        art->CopyTo(work.Frame(), 0x72, 5, false);
    }

    MCPane& pane = work.Frame();
    int32_t barTop = 0x57 - bar;
    DrawLine(pane, 0x74, 0x58, 0x78, 0x58, 0xe5);
    DrawLine(pane, 0x74, barTop, 0x78, barTop, 0xe3);
    int32_t top0 = 0x58 - bar;
    DrawLine(pane, 0x73, 0x57, 0x73, top0, 0xe3);
    DrawLine(pane, 0x79, 0x57, 0x79, top0, 0xe5);

    for (int32_t x = 0x74; x <= 0x78; ++x)
    {
        DrawLine(pane, x, 0x57, x, top0, 0xe4);
    }

    DrawLine(pane, 0x74, 0x56 - bar, 0x78, 0x56 - bar, 0x10);
    VfxPixelWrite(pane, 0x73, barTop, 0x10);
    VfxPixelWrite(pane, 0x79, barTop, 0x10);

    int32_t modifier = Mech()->PilotModifier;

    if (modifier > 0)
    {
        // A good pilot: a red extension on top (up to the 18010 mark, and at least 7 pixels down from the top).
        double extra = clamped + modifier <= 18010.0 ? static_cast<double>(modifier) : 18010.0 - clamped;
        int32_t added = static_cast<int32_t>(static_cast<double>(static_cast<int32_t>(extra)) * (1.0 / 18010.0) * 80.0);
        int32_t extensionTop = 0x58 - added - bar;

        if (extensionTop < 7)
        {
            extensionTop = 0x52 - bar;
        }

        if (extensionTop != 0)
        {
            DrawLine(pane, 0x73, barTop, 0x73, extensionTop, 0xec);

            for (int32_t x = 0x74; x <= 0x78; ++x)
            {
                DrawLine(pane, x, barTop, x, extensionTop, 0xc);
            }

            DrawLine(pane, 0x79, barTop, 0x79, extensionTop, 0x54);
            DrawLine(pane, 0x74, extensionTop - 1, 0x78, extensionTop - 1, 0xec);
        }
    }
    else if (modifier < 0)
    {
        // A poor pilot: the top of the bar turns dark.
        int32_t taken = ~static_cast<int32_t>(static_cast<double>(modifier) * (1.0 / 18010.0) * 80.0);

        if (taken > bar)
        {
            taken = bar;
        }

        if (taken != 0)
        {
            DrawLine(pane, 0x74, barTop, 0x78, barTop, 0xae);
            int32_t bottom = taken - bar + 0x58;
            DrawLine(pane, 0x73, top0, 0x73, bottom, 0xae);

            for (int32_t x = 0x74; x <= 0x78; ++x)
            {
                DrawLine(pane, x, top0, x, bottom, 0xce);
            }

            DrawLine(pane, 0x79, top0, 0x79, bottom, 0xed);
        }
    }
}

auto MCMechRepairBlock::DrawArmorSlider(MCOptionalRef<MCLogPort> port) -> void
{
    DrawSlider(port, 0);
}

auto MCMechRepairBlock::DrawSlider(MCOptionalRef<MCLogPort> port, int32_t slider) -> void
{
    if (port.has_value())
    {
        PaintSlider(*port, 0, slider, true);
    }
}

auto MCMechRepairBlock::PaintSlider(MCLogPort& port, int32_t top, int32_t slider, bool briefing) -> void
{
    int32_t start = 0;
    int32_t position = 0;
    int32_t yPos = 0;

    if (slider == 0)
    {
        start = ArmorSliderStart;
        position = Mech()->RepairSliders.Armor;
        yPos = 0x37;
    }
    else if (slider == 1)
    {
        start = InternalSliderStart;
        position = Mech()->RepairSliders.Internal;
        yPos = 0x4c;
    }
    else if (slider == 2)
    {
        start = EngineSliderStart;
        position = Mech()->RepairSliders.Engine;
        yPos = 0x61;
    }

    // Drawn in place: the original painted a scratch picture (wiped to 0xff) and copied it here keyed on 0xff.
    MCLogBlockPort work(port.Frame(), 0, top, Width(), Height(), true);

    if (const std::shared_ptr<MCLogPort> art = LogScreenArt(std::format("lsrupm{}.tga", slider + 0xb)))
    {
        art->CopyTo(work.Frame(), briefing ? 0xf8 : 0xea, yPos, true);
    }

    // The track left of the knob: the part repaired before (grey), then the part repaired by this drag (red).
    int32_t shift = briefing ? 0xe : 0;
    MCPane& pane = work.Frame();

    if (0xec < position)
    {
        int32_t left = shift + 0xec;
        int32_t startX = start + shift;

        if (left < startX - 1)
        {
            DrawLine(pane, left, yPos + 1, startX - 1, yPos + 1, 0xe3);

            for (int32_t y = yPos + 2; y <= yPos + 6; ++y)
            {
                DrawLine(pane, left, y, startX - 1, y, 0xe4);
            }

            DrawLine(pane, left, yPos + 7, startX - 1, yPos + 7, 0xe5);
            DrawLine(pane, shift + 0xeb, yPos + 2, shift + 0xeb, yPos + 6, 0xe3);
        }

        if (start < position)
        {
            int32_t right = shift - 1 + position;
            int32_t from = startX < 0xec ? 0xec : startX;
            DrawLine(pane, from, yPos + 1, right, yPos + 1, 0xec);

            for (int32_t y = yPos + 2; y <= yPos + 6; ++y)
            {
                DrawLine(pane, from, y, right, y, 0xc);
            }

            DrawLine(pane, from, yPos + 7, right, yPos + 7, 0x87);
            DrawLine(pane, from - 1, yPos + 2, from - 1, yPos + 6, 0xec);
        }
    }

    VfxPaneCopy(SliderArtPort->Frame(), 0, 0, pane, shift + position, yPos, -1);
}

auto MCMechRepairBlock::DrawInternalSlider(MCOptionalRef<MCLogPort> port) -> void
{
    DrawSlider(port, 1);
}

auto MCMechRepairBlock::DrawEngineSlider(MCOptionalRef<MCLogPort> port) -> void
{
    DrawSlider(port, 2);
}

auto MCMechRepairBlock::Draw() -> void
{
    if (RepairScreen().SelectedMech() == Mech())
    {
        DrawInventory(std::nullopt);
    }
}

auto MCMechRepairBlock::DrawInventory(MCOptionalRef<MCLogPort> port) -> void
{
    if (port.has_value() && Mech()->Assigned != 0)
    {
        PaintInventory(*port, 0);
    }
}

auto MCMechRepairBlock::PaintInventory(MCLogPort& port, int32_t top) -> void
{
    MCLogBlockPort work(port.Frame(), 0x135, top + 0x11, 0x62, 0x58, false);
    const auto pane = std::static_pointer_cast<MCScrollPane>(Child(0));
    pane->DrawContentTo(work.Frame(), 0, 0);
    pane->DrawSliderColumn(port.Frame(), pane->Width() + 0x128, top + 0x11, false);
}

auto MCMechRepairBlock::DrawRow(MCLogPort& port, int32_t top) -> void
{
    // Put together as the original painted it into the rows' picture (over its colour 0xff): framed when selected,
    // darkened when another mech is.
    MCLogBlockPort row(port.Frame(), 0, top, 0x19d, 0x70, false);
    VfxPaneWipe(row.Frame(), 0xff);
    const bool selected = RepairScreen().SelectedMech() == Mech();
    PaintBase(row, 0, false, selected);
    const float status = Mech()->CalcStatus();

    if (Mech()->PilotIndex >= 0 || !Mech()->NetworkPilot.expired())
    {
        PaintPilot(row, 0, status, true);
    }
    else
    {
        // OB-134 (fixed): without a pilot the original showed the status bar only once a slider or repair had
        // painted it.
        PaintStatusBar(row, 0, status, true);
    }

    PaintButtons(row, 0, true, ItemsDamaged(), StructureDamaged());
    PaintDiagram(row, 0, 0x88);
    PaintBR(row, 0);
    PaintSlider(row, 0, 0, false);
    PaintSlider(row, 0, 1, false);
    PaintSlider(row, 0, 2, false);

    if (ShowsInventory())
    {
        PaintTonnage(row, 0);
    }

    if (Mech()->Assigned != 0)
    {
        PaintInventory(row, 0);
    }

    if (!selected)
    {
        MCLogistics::Darken(0, LogisticFadetable(), row);
    }

    if (_PilotLifted)
    {
        if (const std::shared_ptr<MCLogPort> blank = LogScreenArt("lsrupm10.tga"))
        {
            blank->CopyTo(port.Frame(), 6, top + 0x21, true);
        }
    }

    // A repair button held while its repair runs.
    if (_PressedButton != 0)
    {
        const bool items = _PressedButton == 1;
        GlobalLog()->RepairPorts[items ? 0 : 2]->CopyTo(port.Frame(), 0xea, top + (items ? 3 : 0x17), true);
    }
}

auto MCMechRepairBlock::OnBeginDragPilot(MCLogPort& surface) -> void
{
    VfxPaneWipe(surface.Frame(), 0xff);
    MCDragIcon::DrawFrom(surface, 5, 0x25, [this](MCLogPort& port) { DrawRow(port, 0); });
}

auto MCMechRepairBlock::OnBeginDragMech(MCLogPort& surface) const -> void
{
    VfxPaneWipe(surface.Frame(), 0x10);

    for (int32_t location = 0; location < 8; ++location)
    {
        MCLogistics::DrawMechBodyLoc(*Mech(), location, surface, 2, 1);
    }
}

auto MCMechRepairBlock::OnBeginDragItem(MCLogPort& surface, MCLogInventoryItem& item) -> void
{
    if (const std::shared_ptr<MCLogPort> art = LogScreenArt(std::format("lscicc{:02}.tga", item.RangeIndex)))
    {
        art->CopyTo(surface.Frame(), 1, 1, true);
    }
}

auto MCMechRepairBlock::MouseWheel(int32_t steps, int32_t xPos, int32_t yPos) -> bool
{
    const auto pane = std::static_pointer_cast<MCScrollPane>(Child(0));

    if (!OverPaneArea(*pane, xPos, yPos) || !pane->MouseWheel(steps, xPos, yPos))
    {
        return false;
    }

    // As a click on the list's scroll bar.
    DrawInventory(std::nullopt);
    return true;
}

auto MCMechRepairBlock::DrawStatusBar(MCOptionalRef<MCLogPort> port) -> void
{
    float status = Mech()->CalcStatus();
    const bool repairLayout = !_BriefingLayout;

    // The rows draw their status bar each frame (DrawRow).
    if (port.has_value())
    {
        PaintStatusBar(*port, 0, status, repairLayout);
    }
}

auto MCMechRepairBlock::PaintStatusBar(MCLogPort& port, int32_t top, float status, bool repairLayout) -> void
{
    // Drawn in place: the original painted a scratch picture (wiped to 0xff) and copied it here keyed on 0xff.
    MCLogBlockPort work(port.Frame(), 0, top, Width(), Height(), true);
    uint8_t color;

    if (status < 0.5f)
    {
        color = status > 0.2 ? 0xf2 : 0xef;
    }
    else
    {
        color = 0xb;
    }

    int32_t left = repairLayout ? 0x88 : 0x8e;
    int32_t right = repairLayout ? 0xd2 : 0xd8;

    for (int32_t y = 2; y <= 5; ++y)
    {
        DrawLine(work.Frame(), left, y, right, y, 0x13);
    }

    if (status != 0.0f)
    {
        auto end = static_cast<int32_t>(static_cast<double>(status) * 74.0 + (repairLayout ? 136.0 : 142.0));

        for (int32_t y = 2; y <= 5; ++y)
        {
            DrawLine(work.Frame(), left, y, end, y, color);
        }
    }
}

auto MCMechRepairBlock::SetEngineSlider(int32_t value) -> void
{
    if (value >= 0)
    {
        return;
    }

    MCMechBay::PlaceKnob(*Mech(), MCMechBay::MCKnob::Engine);
}

auto MCMechRepairBlock::SetInternalSlider(int32_t value) -> void
{
    if (value >= 0)
    {
        return;
    }

    MCMechBay::PlaceKnob(*Mech(), MCMechBay::MCKnob::Internal);
}

auto MCMechRepairBlock::SetArmorSlider(int32_t value) -> void
{
    if (value >= 0)
    {
        return;
    }

    MCMechBay::PlaceKnob(*Mech(), MCMechBay::MCKnob::Armor);
}

auto MCMechRepairBlock::ClearPilot() -> void
{
    _PilotLifted = true;
}

auto MCMechRepairBlock::SetPilotStats(MCOptionalRef<MCLogPort> port) -> void
{
    const std::shared_ptr<MCLogMech> mech = Mech();

    if (mech->PilotIndex < 0 && mech->NetworkPilot.expired())
    {
        return;
    }

    float status = mech->CalcStatus();
    const bool repairLayout = !_BriefingLayout;

    // The rows draw their pilot each frame (DrawRow).
    if (port.has_value())
    {
        PaintPilot(*port, 0, status, repairLayout);
    }
}

auto MCMechRepairBlock::PaintPilot(MCLogPort& port, int32_t top, float status, bool repairLayout) -> void
{
    const std::shared_ptr<MCLogMech> mech = Mech();
    std::shared_ptr<MCLogWarrior> warrior;

    if (mech->LocalPart == 0)
    {
        warrior = mech->NetworkPilot.lock();
    }
    else
    {
        GlobalLog()->AssignedWarriorList->GetWarriorInfo(mech->PilotIndex, warrior);
    }

    // The status bar, then the portrait.
    PaintStatusBar(port, top, status, repairLayout);
    std::shared_ptr<MCLogPort> portrait = warrior == nullptr
                                              ? LogScreenArt(std::format("pilot{:02}.tga", mech->PilotIndex))
                                              : LogScreenArt(warrior->Picture);

    if (portrait != nullptr)
    {
        portrait->CopyTo(port.Frame(), 6, top + 0x26, true);
    }

    // Port fix: without a pilot record the texts are skipped (the original read them through the null pointer).
    if (warrior == nullptr)
    {
        return;
    }

    WriteLine(*YellowDropFont, port.Frame(), 0x2d, top + 0x2a, warrior->Callsign);

    // An out-of-range rank shows the portrait's file name, which the text buffer last held.
    std::string rank = std::format("{}logart\\{}", ArtPath, warrior->Picture);

    if (warrior->Rank >= 0 && warrior->Rank <= 3)
    {
        rank = LoadGameString(0x70 + static_cast<uint32_t>(warrior->Rank), 0xfe);
    }

    WriteLine(*YellowDropFont, port.Frame(), 0x2d, top + 0x3c, rank);
    MCLogistics::DrawPilotSkillBar(*warrior, 3, 0x2e, top + 0x4a, 0, 0x36, WinHeight, port);
    MCLogistics::DrawPilotSkillBar(*warrior, 0, 0x2e, top + 0x53, 0, 0x36, WinHeight, port);
    MCLogistics::DrawPilotSkillBar(*warrior, 1, 0x2e, top + 0x5c, 0, 0x36, WinHeight, port);
    MCLogistics::DrawPilotSkillBar(*warrior, 2, 0x2e, top + 0x65, 0, 0x36, WinHeight, port);

    // One 2x2 pip per point of health.
    auto pips = static_cast<int32_t>(warrior->Health);
    int32_t xPos = 0xc;

    for (; pips > 0; --pips)
    {
        AGPixelWrite(port.Frame(), xPos, top + 0x22, 0xcf);
        AGPixelWrite(port.Frame(), xPos + 1, top + 0x22, 0xcf);
        AGPixelWrite(port.Frame(), xPos + 1, top + 0x23, 0xee);
        AGPixelWrite(port.Frame(), xPos, top + 0x23, 0xcf);
        xPos += 3;
    }
}

auto MCMechRepairBlock::SetPilotHealth(MCOptionalRef<MCLogPort>) -> void
{
    // Drawn with the pilot's stats (PaintPilot).
}

auto MCMechRepairBlock::SetWeaponLists() -> void
{
    ShortRangeWeapons.clear();
    MediumRangeWeapons.clear();
    LongRangeWeapons.clear();
    Equipment.clear();

    // One entry per copy, the item's position in the inventory; weapons go by long range: under 76 short, under 151
    // medium, else long. The copies' damage, by list.
    std::array<std::vector<int32_t>, 4> hits;
    int32_t index = 0;

    for (const std::shared_ptr<MCLogInventoryItem>& item : Mech()->Inventory->Items)
    {
        const int32_t position = index++;
        const MCMasterComponent& component = MasterComponentList[item->MasterID];
        size_t list = 0;

        if (IsWeapon(component.Form))
        {
            list = component.WeaponRange[3] < 76.0f ? 0 : component.WeaponRange[3] < 151.0f ? 1 : 2;
        }
        else if (IsEquipment(component.Form))
        {
            list = 3;
        }
        else
        {
            continue;
        }

        std::vector<int32_t>& entries =
            std::array{&ShortRangeWeapons, &MediumRangeWeapons, &LongRangeWeapons, &Equipment}[list][0];

        // Port fix: a count above the copies (AddCountToItem) read past them; the missing copies count as undamaged.
        for (int32_t copy = 0; copy < item->Count; ++copy)
        {
            entries.push_back(position);
            hits[list].push_back(
                static_cast<size_t>(copy) < item->Stats.size() ? item->Stats[static_cast<size_t>(copy)]->Hits : 0);
        }
    }

    for (size_t list = 0; list < 3; ++list)
    {
        std::vector<int32_t>& entries = std::array{&ShortRangeWeapons, &MediumRangeWeapons, &LongRangeWeapons}[list][0];
        SortByDamage(entries, hits[list], *Mech()->Inventory);
    }

    ItemHits.clear();

    for (const std::vector<int32_t>& listHits : hits)
    {
        ItemHits.insert(ItemHits.end(), listHits.begin(), listHits.end());
    }
}

auto MCMechRepairBlock::SortByDamage(std::span<int32_t> entries, std::span<int32_t> hits, MCInventoryList& inventory)
    -> void
{
    // An exchange sort by damage, lowest first; the hits follow their entries.
    auto damage = [&inventory](int32_t entry)
    { return MasterComponentList[inventory.GetMasterIDFromIndex(entry)].Damage; };

    for (size_t i = 0; i + 1 < entries.size(); ++i)
    {
        for (size_t j = i + 1; j < entries.size(); ++j)
        {
            if (damage(entries[j]) < damage(entries[i]))
            {
                std::swap(entries[i], entries[j]);
                std::swap(hits[i], hits[j]);
            }
        }
    }
}

auto MCMechRepairBlock::GetInvItem(const std::vector<int32_t>& list, int32_t index, uint8_t& itemNum) const
    -> std::shared_ptr<MCLogInventoryItem>
{
    int32_t earlier = index - 1;

    while (earlier >= 0 && list[static_cast<size_t>(earlier)] == list[static_cast<size_t>(index)])
    {
        --earlier;
    }

    int32_t copy = index - earlier - 1;
    std::shared_ptr<MCLogInventoryItem> item = Mech()->Inventory->GetItemInfo(list[static_cast<size_t>(index)]);
    itemNum = static_cast<uint8_t>(item->Stats[static_cast<size_t>(copy)]->ItemNum);
    return item;
}

auto MCMechRepairBlock::SetInventory(MCOptionalRef<MCScrollPane> target) -> void
{
    const std::shared_ptr<MCLogMech> mech = Mech();

    // In multiplayer, a mech outside the force gets its weapon list only in the briefing box shown (the briefing
    // screen makes a box only to show it).
    if (MultiPlayer() != nullptr && GlobalLog()->ForceMechList->GetMechIndex(mech) < 0 && !_BriefingLayout)
    {
        return;
    }

    MCScrollPane& pane = target.has_value() ? target->get() : *InventoryPane;

    auto content = std::make_shared<MCLogPort>();
    int32_t lines = 0;

    for (const std::shared_ptr<MCLogInventoryItem>& item : mech->Inventory->Items)
    {
        MCComponentForm form = ComponentForm(item->MasterID);

        if (IsWeapon(form) || IsEquipment(form))
        {
            lines += item->Count;
        }
    }

    int32_t lineHeight = GreenFont->Height() + 2;
    int32_t contentHeight = std::max(lineHeight * (lines + 4) + 4, pane.Height());

    // Port: the list is drawn into the pane each frame (DrawWeaponList) from the lists made here.
    content->InitView(pane.Width() - 0xd, contentHeight);
    SetWeaponLists();
    content->DrawContent = [this](MCGuiPort& view) { DrawWeaponList(static_cast<MCLogPort&>(view)); };
    pane.SetDisplayPort(std::move(content), false);

    // The tonnage bar (the weapons' weight against the free weight): the rows draw theirs each frame; the original also
    // painted one into the briefing screen's picture for the box, which the box (drawn after) covered.
}

auto MCMechRepairBlock::PaintTonnage(MCLogPort& port, int32_t top) -> void
{
    // Drawn in place: the original painted a scratch picture (wiped to 0xff) and copied it here keyed on 0xff.
    MCLogBlockPort work(port.Frame(), 0, top, Width(), Height(), true);
    int32_t fill = static_cast<int32_t>(static_cast<double>(Mech()->WeaponTonnage) / Mech()->FreeTonnage * 53.0);
    DrawTonnageBar(work.Frame(), 0x15e, fill);
}

auto MCMechRepairBlock::DrawWeaponList(MCLogPort& content) -> void
{
    MCPane& frame = content.Frame();
    int32_t lineHeight = GreenFont->Height() + 2;
    VfxPaneWipe(frame, 0x10);

    // The four headings: a coloured stripe (the first three with a black line under it) and the title.
    auto heading = [&](int32_t top, uint8_t color, bool underline, uint32_t title, int32_t titleY)
    {
        int32_t y = top;

        for (; y < GreenFont->Height() + 2 + top; ++y)
        {
            DrawLine(frame, 0, y, content.Width() - 2, y, color);
        }

        if (underline)
        {
            DrawLine(frame, 0, y, content.Width() - 2, y, 0x10);
        }

        WriteLine(*WhiteFont, frame, 1, titleY, LoadGameString(title, 0xfe));
    };

    const auto numShort = static_cast<int32_t>(ShortRangeWeapons.size());
    const auto numMedium = static_cast<int32_t>(MediumRangeWeapons.size());
    const auto numLong = static_cast<int32_t>(LongRangeWeapons.size());
    heading(0, 0xe, true, 0x55, 2);
    heading(lineHeight * (numShort + 1) + 1, 0xe5, true, 0x50, lineHeight * (numShort + 1) + 3);
    const int32_t shortAndMedium = numShort + numMedium;
    heading(lineHeight * (shortAndMedium + 2) + 2, 0xee, true, 0x6d, lineHeight * (shortAndMedium + 2) + 4);
    const int32_t weapons = shortAndMedium + numLong;
    heading(lineHeight * (weapons + 3) + 3, 0x14, false, 0x6f, lineHeight * (weapons + 3) + 5);

    // The entries: a range glyph (clan technology has its own) and the name, grey when damaged.
    MCInventoryList& inventory = *Mech()->Inventory;
    auto weaponLine = [&](int32_t entry, size_t hitsIndex, char glyph, int32_t line, int32_t gap)
    {
        const MCMasterComponent& component = MasterComponentList[inventory.GetMasterIDFromIndex(entry)];
        const char shown = component.TechBase != 1 ? static_cast<char>(glyph + 0x5e) : glyph;
        std::string name = std::format("{} {}", shown, component.Name);

        if (ItemHits[hitsIndex] == 0)
        {
            WriteLine(*BlueFont, frame, 2, (GreenFont->Height() + 2) * line + gap, name);
        }
        else
        {
            WriteLine(*GreyFont, frame, 2, (GreyFont->Height() + 2) * line + gap, name);
        }
    };

    int32_t line = 1;
    size_t hitsIndex = 0;

    for (int32_t entry : ShortRangeWeapons)
    {
        weaponLine(entry, hitsIndex++, 0x1d, line++, 2);
    }

    ++line;

    for (int32_t entry : MediumRangeWeapons)
    {
        weaponLine(entry, hitsIndex++, 0x1e, line++, 3);
    }

    ++line;

    for (int32_t entry : LongRangeWeapons)
    {
        weaponLine(entry, hitsIndex++, 0x1f, line++, 4);
    }

    for (int32_t entry : Equipment)
    {
        ++line;
        const MCMasterComponent& component = MasterComponentList[inventory.GetMasterIDFromIndex(entry)];
        std::shared_ptr<MCGuiFont> font = ItemHits[hitsIndex++] == 0 ? BlueFont : GreyFont;
        WriteLine(*font, frame, 2, (GreenFont->Height() + 2) * line + 5, component.Name);
    }
}

auto MCMechRepairBlock::GetItemFromScrollPane(int32_t line, uint8_t& itemNum) const
    -> std::shared_ptr<MCLogInventoryItem>
{
    // The lines: heading, short-range weapons, heading, medium, heading, long, heading, equipment.
    const auto numShort = static_cast<int32_t>(ShortRangeWeapons.size());
    const auto numMedium = static_cast<int32_t>(MediumRangeWeapons.size());
    const auto numLong = static_cast<int32_t>(LongRangeWeapons.size());
    const auto numEquipment = static_cast<int32_t>(Equipment.size());

    if (line <= numShort)
    {
        return line != 0 ? GetInvItem(ShortRangeWeapons, line - 1, itemNum) : nullptr;
    }

    if (line <= numMedium + 1 + numShort)
    {
        int32_t entry = line - 1 - numShort;
        return entry != 0 ? GetInvItem(MediumRangeWeapons, entry - 1, itemNum) : nullptr;
    }

    if (line <= numLong + numMedium + 2 + numShort)
    {
        int32_t entry = line - 2 - numMedium - numShort;
        return entry != 0 ? GetInvItem(LongRangeWeapons, entry - 1, itemNum) : nullptr;
    }

    if (line <= numEquipment + numLong + numMedium + 3 + numShort)
    {
        int32_t entry = line - 3 - numLong - numMedium - numShort;

        if (entry != 0)
        {
            return GetInvItem(Equipment, entry - 1, itemNum);
        }
    }

    return nullptr;
}

auto MCMechRepairBlock::SetUpItemDragIcon(MCLogInventoryItem& item, uint8_t itemNum, MCGuiEvent& event) -> void
{
    const std::shared_ptr<MCLogMech> mech = Mech();
    int32_t eventX = event.X;
    int32_t eventY = event.Y;
    MCRepairScreen::DrawBlankInvInfoBlock(2);
    // Made on the screen, but kept off it until it is raised below (the original added it there).
    const std::shared_ptr<MCDragIcon> icon = MCDragIcon::Create(
        RepairScreen(), eventX - 0x10, eventY - 0x10, 0x20, 0x20,
        [&item](MCLogPort& surface) { OnBeginDragItem(surface, item); }, true);
    const std::optional<MCMechBay::MCTaken> taken = MCMechBay::TakeOut(*mech, item, itemNum);

    if (!taken.has_value())
    {
        return;
    }

    Drag.ItemHits = taken->Hits;

    if (taken->Hits != 0)
    {
        MCLogistics::Darken(0, LogisticFadetable(), *icon->Lport());
    }

    DrawItemInfo(item, *mech->Inventory);
    RepairScreen().SetUpCompInv(false, false);
    SetInventory(std::nullopt);
    DrawInventory(std::nullopt);
    DrawBR(std::nullopt);
    DrawStatusBar();
    DrawButtons(std::nullopt);
    icon->BringToFront(true);
    icon->Raise();
}

auto MCMechRepairBlock::DrawInfo(MCLogPort& port) const -> void
{
    const std::shared_ptr<MCLogMech> mech = Mech();

    if (DragPort != nullptr)
    {
        DragPort->CopyTo(port.Frame(), 0xb, 0x191, true);
    }

    WriteText(*YellowDropFont, port, 0x53, 0x193,
              std::format("{:.0f} {}", mech->CurTonnage, LoadGameString(0x6e, 0x1e)));
    WriteText(*YellowDropFont, port, 0x53, 0x19c, mech->WeightClassName);
    WriteText(*YellowDropFont, port, 0xa8, 0x193, mech->ChassisClassName);
    WriteText(*YellowDropFont, port, 0xa8, 0x19c, mech->ExtraName1);
    WriteText(*YellowDropFont, port, 0xa8, 0x1a5, mech->ExtraName2);
    WriteText(*YellowDropFont, port, 0x53, 0x1a5, std::format("{} m/s", mech->MaxRunSpeed));
    DrawInfoDescription(port, 0xc3, 0x26, mech->Description, 8, 0x1b3);
}

auto MCMechRepairBlock::DebugFunction1(int32_t, int32_t) -> int
{
    return 0;
}

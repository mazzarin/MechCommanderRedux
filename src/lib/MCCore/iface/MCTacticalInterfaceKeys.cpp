#include "stdafx.h"
#include "main/MCMissionGlobals.h"
#include "iface/MCTacticalInterface.h"
#include "ai/MCTacticalOrder.h"
#include "camera/MCCamera.h"
#include "camera/MCMainWindow.h"
#include "gui/MCGuiChatWindow.h"
#include "iface/MCCommandParser.h"
#include "iface/MCMechBar.h"
#include "logistics/MCPreferencesMenu.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "object/MCForces.h"
#include "object/MCGroundVehicle.h"
#include "object/MCMover.h"
#include "object/MCMoverGroup.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTacticalMap.h"

namespace
{
    /// <summary>
    /// <paramref name="key"/> (a scan code) with the modifiers <paramref name="event"/> carries, in a binding's form.
    /// </summary>
    uint32_t WithModifiers(uint32_t key, const MCGuiEvent& event)
    {
        key &= ~KeyShift;

        if (event.ShiftKey != 0)
        {
            key += KeyShift;
        }

        key &= ~KeyCtrl;

        if (event.CtrlKey != 0)
        {
            key += KeyCtrl;
        }

        key &= ~KeyAlt;

        if (event.AltKey != 0)
        {
            key += KeyAlt;
        }

        return key;
    }

    /// <summary>Whether <paramref name="key"/> is a forced-order key (either ctrl, or one of the four bindings).</summary>
    bool IsForcedOrderKey(const MCTacticalInterface& iface, int16_t scanCode, uint32_t key)
    {
        return scanCode == 0x1d || scanCode == 0x11d || key == iface.Key(MCKeyCommand::ForcedOrder) ||
               key == iface.Key(MCKeyCommand::ForcedRun) || key == iface.Key(MCKeyCommand::ForcedJump) ||
               key == iface.Key(MCKeyCommand::ForcedOrderAlternate);
    }

    /// <summary>Marks whether the mech bar movers draw their order lines (while a forced order is being given).</summary>
    void DrawOrderLines(MCTacticalInterface& iface, bool selectedOnly, int32_t draw)
    {
        auto mark = [draw](std::shared_ptr<MCFriendlyMechIcon> button)
        {
            const std::shared_ptr<MCBaseObject> mover = button != nullptr ? LockObject(button->Mover) : nullptr;

            if (mover != nullptr && IsMoverClass(mover->ObjectClass))
            {
                static_cast<MCMover&>(*mover).DrawOrderLines = draw;
            }
        };

        if (selectedOnly)
        {
            for (int32_t partId : iface.SelectedMovers)
            {
                mark(iface.MechBar->GetButtonFromID(partId));
            }

            return;
        }

        for (const std::shared_ptr<MCFriendlyMechIcon>& button : iface.MechBar->Buttons)
        {
            mark(button);
        }
    }

    /// <summary>Brings up the tactical map (out of its hidden slide) on <paramref name="page"/>.</summary>
    void ShowTacticalMapPage(MCTacmapPage page)
    {
        TacticalMap()->HideMe(false);
        TacticalMap()->SetDisplayType(page);
    }

    /// <summary>The lance (0-3) <paramref name="key"/> names among the four bindings from <paramref name="first"/>, or -1.</summary>
    int32_t LanceKey(const MCTacticalInterface& iface, uint32_t key, MCKeyCommand first)
    {
        for (int32_t lance = 0; lance < 4; lance++)
        {
            if (key == iface.Key(static_cast<MCKeyCommand>(static_cast<int32_t>(first) + lance)))
            {
                return lance;
            }
        }

        return -1;
    }

    /// <summary>The artillery button (0-3) <paramref name="key"/> arms, or -1.</summary>
    int32_t ArtilleryKey(const MCTacticalInterface& iface, uint32_t key)
    {
        constexpr std::array<MCKeyCommand, 4> buttons = {MCKeyCommand::Artillery1, MCKeyCommand::Artillery2,
                                                         MCKeyCommand::Artillery3, MCKeyCommand::Artillery4};

        for (int32_t button = 0; button < 4; button++)
        {
            if (key == iface.Key(buttons[button]))
            {
                return button;
            }
        }

        return -1;
    }

    /// <summary>
    /// The scroll key (0-3: up, down, left, right) <paramref name="key"/> names among the four bindings from
    /// <paramref name="first"/>, or -1.
    /// </summary>
    int32_t ScrollKey(const MCTacticalInterface& iface, uint32_t key, MCKeyCommand first)
    {
        for (int32_t direction = 0; direction < 4; direction++)
        {
            if (key == iface.Key(static_cast<MCKeyCommand>(static_cast<int32_t>(first) + direction)))
            {
                return direction;
            }
        }

        return -1;
    }

    /// <summary>The direction (0-7 clockwise from up; -1 = none) the held scroll keys give together.</summary>
    int32_t HeldScrollDirection(const std::array<bool, 4>& held)
    {
        static constexpr std::array<std::array<int32_t, 3>, 3> directions = {{{7, 0, 1}, {6, -1, 2}, {5, 4, 3}}};
        const int32_t y = (held[1] ? 1 : 0) - (held[0] ? 1 : 0);
        const int32_t x = (held[3] ? 1 : 0) - (held[2] ? 1 : 0);
        return directions[static_cast<size_t>(y + 1)][static_cast<size_t>(x + 1)];
    }

    /// <summary>Lets go of the scroll keys among the four bindings from <paramref name="first"/> on <paramref name="key"/>'s key.</summary>
    void ReleaseScrollKey(const MCTacticalInterface& iface, uint32_t key, MCKeyCommand first, std::array<bool, 4>& held)
    {
        constexpr uint32_t modifiers = KeyShift | KeyCtrl | KeyAlt;

        for (size_t direction = 0; direction < held.size(); direction++)
        {
            const auto command =
                static_cast<MCKeyCommand>(static_cast<int32_t>(first) + static_cast<int32_t>(direction));

            if ((iface.Key(command) & ~modifiers) == (key & ~modifiers))
            {
                held[direction] = false;
            }
        }
    }

    /// <summary>A key that picks a mode, with the mode; <c>Armed</c> modes need an armed mover selected.</summary>
    struct MCModeKey
    {
        MCKeyCommand Key = MCKeyCommand::Unused;
        MCInterfaceMode Mode = MCInterfaceMode::None;
        bool Armed = false;
    };
}

auto MCTacticalInterface::HandleKeyDown(MCGuiEvent& event) -> void
{
    // Fold right alt/ctrl onto the left ones, then add the modifiers.
    const int16_t scanCode = event.ScanCode;
    uint32_t key = static_cast<uint32_t>(static_cast<int32_t>(scanCode));

    if (scanCode == 0x138 || scanCode == 0x11d)
    {
        key = static_cast<uint32_t>(static_cast<int16_t>(scanCode - 0x100));
    }

    key = WithModifiers(key, event);
    auto is = [this, key](MCKeyCommand command) { return key == Key(command); };

    if (Scenario() != nullptr)
    {
        if ((event.CtrlKey != 0 && (scanCode == 0x1d || scanCode == 0x11d)) || is(MCKeyCommand::ForcedOrder) ||
            is(MCKeyCommand::ForcedRun) || is(MCKeyCommand::ForcedJump) || is(MCKeyCommand::ForcedOrderAlternate))
        {
            if (ForcedOrder == MCForcedOrder::None && !ForcingOrder)
            {
                ForcedOrder = MCForcedOrder::Move;
            }

            ForcingOrder = true;
            DrawOrderLines(*this, true, 1);
        }

        // The keys that pick a mode, in the order the original tested them (after the map, page and zoom keys).
        auto sendToSelection = [this](MCTacticalOrderCode code)
        {
            if (AnySelected() && CommandParser != nullptr)
            {
                MCTacticalOrder order;
                order.Reset();
                order.Reset(MCOrderOrigin::Player, code, 0);
                CommandParser->SendTacOrder(order, false);
            }
        };

        const std::shared_ptr<MCTacticalMap> tacMap = ::TacticalMap();
        const int32_t lanceToSelect = LanceKey(*this, key, MCKeyCommand::SelectLance1);
        const int32_t lanceToAdd = LanceKey(*this, key, MCKeyCommand::AddLance1);
        const int32_t artillery = ArtilleryKey(*this, key);
        const int32_t scrollKey = ScrollKey(*this, key, MCKeyCommand::ScrollUp);
        const int32_t tacScrollKey = ScrollKey(*this, key, MCKeyCommand::TacticalMapScrollUp);

        // Port fix (OB-174): a scroll key set the one direction, so two held keys scrolled only the
        // last one's way; the held keys now scroll together, diagonals included.
        if (scrollKey >= 0)
        {
            ScrollKeysHeld[static_cast<size_t>(scrollKey)] = true;
            ScrollDirection = HeldScrollDirection(ScrollKeysHeld);
        }
        else if (tacScrollKey >= 0)
        {
            if (tacMap != nullptr)
            {
                ShowTacticalMapPage(MCTacmapPage::Map);
                TacScrollKeysHeld[static_cast<size_t>(tacScrollKey)] = true;
                TacScrollDirection = HeldScrollDirection(TacScrollKeysHeld);
            }
        }
        else if (is(MCKeyCommand::HoldTacticalMap))
        {
            TacMapShown = true;
        }
        else if (is(MCKeyCommand::TacticalMapZoomIn) || is(MCKeyCommand::TacticalMapZoomInAlternate))
        {
            if (tacMap != nullptr)
            {
                ShowTacticalMapPage(MCTacmapPage::Map);
                APostMessage(*tacMap, 0x1a);
            }
        }
        else if (is(MCKeyCommand::TacticalMapZoomOut) || is(MCKeyCommand::TacticalMapZoomOutAlternate))
        {
            if (tacMap != nullptr)
            {
                ShowTacticalMapPage(MCTacmapPage::Map);
                APostMessage(*tacMap, 0x1b);
            }
        }
        else if (is(MCKeyCommand::MapPage))
        {
            TacMapShown = false;

            if (tacMap != nullptr)
            {
                ShowTacticalMapPage(MCTacmapPage::Map);
            }
        }
        else if (is(MCKeyCommand::SalvagePage))
        {
            TacMapShown = false;

            if (tacMap != nullptr)
            {
                ShowTacticalMapPage(MCTacmapPage::Salvage);
            }
        }
        else if (MultiPlayer() != nullptr && is(MCKeyCommand::Chat))
        {
            // The chat line.
            TacMapShown = false;

            if (tacMap != nullptr && Scenario() != nullptr && EventsToMissionResultsScreen == 0 && GameAsked == 0)
            {
                ShowTacticalMapPage(MCTacmapPage::Salvage);
                const std::shared_ptr<MCGuiObject> chatInput = tacMap->ChatWindow->ChatInput;

                if (GuiSystem().TextObject() != chatInput)
                {
                    GuiSystem().SetText(chatInput);
                    FirstReturn = true;
                }
            }
        }
        else if (is(MCKeyCommand::MissionPage))
        {
            TacMapShown = false;

            if (tacMap != nullptr)
            {
                ShowTacticalMapPage(MCTacmapPage::Mission);
            }
        }
        else if (is(MCKeyCommand::InfoPage))
        {
            TacMapShown = false;

            if (tacMap != nullptr)
            {
                ShowTacticalMapPage(MCTacmapPage::Info);
            }
        }
        else if (is(MCKeyCommand::InterfaceSizeUp) || is(MCKeyCommand::InterfaceSizeUpAlternate) ||
                 is(MCKeyCommand::InterfaceSizeDown) || is(MCKeyCommand::InterfaceSizeDownAlternate))
        {
            // Port-only: a bigger or smaller interface, kept in PREFS; the alt held with the key leaves the map as
            // it is.
            TacMapShown = false;
            StepInterfaceSize(is(MCKeyCommand::InterfaceSizeUp) || is(MCKeyCommand::InterfaceSizeUpAlternate) ? 1 : -1);
        }
        else if (is(MCKeyCommand::ZoomIn) || is(MCKeyCommand::ZoomInAlternate))
        {
            ZoomIn();
        }
        else if (is(MCKeyCommand::ZoomOut) || is(MCKeyCommand::ZoomOutAlternate))
        {
            ZoomOut();
        }
        else if (is(MCKeyCommand::Stop) && GuiSystem().GrabbedObject() == nullptr)
        {
            SetMode(MCInterfaceMode::Stop);
            sendToSelection(MCTacticalOrderCode::Stop);
        }
        else if (is(MCKeyCommand::PowerUp))
        {
            SetMode(MCInterfaceMode::PowerUp);
            sendToSelection(MCTacticalOrderCode::PowerUp);
        }
        else if (is(MCKeyCommand::PowerDown))
        {
            SetMode(MCInterfaceMode::PowerDown);
            sendToSelection(MCTacticalOrderCode::PowerDown);
        }
        else if (is(MCKeyCommand::Eject))
        {
            SetMode(MCInterfaceMode::Eject);
        }
        else if (is(MCKeyCommand::Run) || is(MCKeyCommand::RunAlternate) || is(MCKeyCommand::ForcedRun))
        {
            SetMode(MCInterfaceMode::Run);
        }
        else if (is(MCKeyCommand::Info))
        {
            SetMode(MCInterfaceMode::Info);
        }
        else
        {
            static constexpr auto modeKeys = std::to_array<MCModeKey>({
                {MCKeyCommand::AttackOptimalRange, MCInterfaceMode::AttackOptimalRange, true},
                {MCKeyCommand::AttackLongRange, MCInterfaceMode::AttackLongRange, true},
                {MCKeyCommand::AttackMediumRange, MCInterfaceMode::AttackMediumRange, true},
                {MCKeyCommand::AttackConservingAmmo, MCInterfaceMode::AttackConservingAmmo, true},
                {MCKeyCommand::AttackShortRange, MCInterfaceMode::AttackShortRange, true},
                {MCKeyCommand::AttackFromPosition, MCInterfaceMode::AttackFromPosition, true},
                {MCKeyCommand::Jump, MCInterfaceMode::Jump, false},
                {MCKeyCommand::JumpAlternate, MCInterfaceMode::Jump, false},
                {MCKeyCommand::ForcedJump, MCInterfaceMode::Jump, false},
                {MCKeyCommand::Guard, MCInterfaceMode::Guard, false},
                {MCKeyCommand::AimHead, MCInterfaceMode::AimHead, true},
                {MCKeyCommand::AimLeftTorso, MCInterfaceMode::AimLeftTorso, true},
                {MCKeyCommand::AimRightTorso, MCInterfaceMode::AimRightTorso, true},
                {MCKeyCommand::AimCenterTorso, MCInterfaceMode::AimCenterTorso, true},
                {MCKeyCommand::AimLeftArm, MCInterfaceMode::AimLeftArm, true},
                {MCKeyCommand::AimRightArm, MCInterfaceMode::AimRightArm, true},
                {MCKeyCommand::AimLeftLeg, MCInterfaceMode::AimLeftLeg, true},
                {MCKeyCommand::AimRightLeg, MCInterfaceMode::AimRightLeg, true},
            });

            const auto modeKey = std::ranges::find_if(modeKeys, [&](const MCModeKey& entry)
                                                      { return is(entry.Key) && (!entry.Armed || AnySelected(true)); });

            if (modeKey != modeKeys.end())
            {
                SetMode(modeKey->Mode);
            }
            else if (is(MCKeyCommand::BreakLances) && AnySelected())
            {
                // Break up the selected movers' lances; a point takes its whole lance with it.
                for (int32_t partId : SelectedMovers)
                {
                    std::shared_ptr<MCFriendlyMechIcon> button = MechBar->GetButtonFromID(partId);

                    if (button == nullptr)
                    {
                        continue;
                    }

                    if (button->IsPoint)
                    {
                        for (const std::shared_ptr<MCFriendlyMechIcon>& other : MechBar->Buttons)
                        {
                            if (other != button && other->Lance == button->Lance)
                            {
                                other->Lance = NoLance;
                            }
                        }
                    }

                    button->Lance = NoLance;

                    // Port fix: the original read the group of a null mover when the button was missing.
                    if (const auto member = std::static_pointer_cast<MCMover>(LockObject(button->Mover));
                        member != nullptr && member->Group.lock() != nullptr)
                    {
                        member->Group.lock()->Remove(*member);
                    }
                }

                // Original behaviour (OB-157): each deselected lance leaves the list, so the walk skips every other one.
                for (size_t i = 0; i < SelectedLances.size(); i++)
                {
                    DeselectLance(SelectedLances[i].lock());
                }

                MechBar->PlaceButtons(true);
            }
            else if (lanceToSelect >= 0)
            {
                std::shared_ptr<MCMoverGroup> lance = HomeCommander()->GetGroup(lanceToSelect);

                if (lance->NumMovers() > 0)
                {
                    ClearMechSelection();
                    SelectLance(lance);
                    CommandParser->AddSubject(lance);
                    SoundSystem()->PlayInterfaceSample(0x30);
                }
            }
            else if (lanceToAdd >= 0)
            {
                std::shared_ptr<MCMoverGroup> lance = HomeCommander()->GetGroup(lanceToAdd);

                if (lance->NumMovers() > 0)
                {
                    SelectLance(lance);
                    CommandParser->AddSubject(lance);
                    SoundSystem()->PlayInterfaceSample(0x30);
                }
            }
            else if (is(MCKeyCommand::CameraFollow))
            {
                SetMode(MCInterfaceMode::CameraFollow);
            }
            else if (tacMap != nullptr && artillery >= 0)
            {
                tacMap->ActivateArtillery(artillery, 1);
            }
            else if (BunnyStrikesOn != 0 && is(MCKeyCommand::DebugStrike))
            {
                SetMode(MCInterfaceMode::DebugStrike);
            }
            else
            {
                if (is(MCKeyCommand::TogglePalette))
                {
                    TogglePalette();
                }

                // Laying mines: one selected mine layer with mines left.
                if ((is(MCKeyCommand::LayMines) || is(MCKeyCommand::LayMinesAlternate)) && SelectedMovers.size() == 1)
                {
                    std::shared_ptr<MCBaseObject> selected = ObjectList()->FindObjectFromPart(SelectedMovers[0]);

                    if (selected == nullptr || selected->ObjectClass != MCObjectClass::GroundVehicle ||
                        !static_cast<MCGroundVehicle&>(*selected).MineLayer ||
                        static_cast<MCGroundVehicle&>(*selected).MinesToLay < 1)
                    {
                        return;
                    }

                    SetMode(MCInterfaceMode::LayMines);
                }
            }
        }
    }

    // A key other than a lance-link key ends the hidden-cursor state of a lance link.
    if (GuiSystem().CursorHidden != 0 &&
        GuiSystem().CursorShape == static_cast<int32_t>(MCInterfaceCursor::LinkLance) && !IsLanceLink(CurrentMode))
    {
        GuiSystem().CursorHidden = false;
    }
}

auto MCTacticalInterface::HandleKeyUp(MCGuiEvent& event) -> void
{
    // Only the right alt is folded onto the left one here.
    const int16_t scanCode = event.ScanCode;
    uint32_t key = static_cast<uint32_t>(static_cast<int32_t>(scanCode));

    if (scanCode == 0x138)
    {
        key = 0x38;
    }

    key = WithModifiers(key, event);

    if (IsForcedOrderKey(*this, scanCode, key))
    {
        ForcingOrder = false;
        ForcedOrder = MCForcedOrder::None;
        DrawOrderLines(*this, false, 0);
    }

    // Port fix (OB-174): any key-up stopped the arrow scrolling (unless it went to a main window
    // pane); now a released scroll key drops out and the ones still held go on.
    ReleaseScrollKey(*this, key, MCKeyCommand::ScrollUp, ScrollKeysHeld);
    ReleaseScrollKey(*this, key, MCKeyCommand::TacticalMapScrollUp, TacScrollKeysHeld);
    ScrollDirection = HeldScrollDirection(ScrollKeysHeld);
    TacScrollDirection = HeldScrollDirection(TacScrollKeysHeld);

    if (const std::shared_ptr<MCGuiObject> target = event.TargetObject();
        target != nullptr && target->ParentObject().get() == MainHolder().get())
    {
        return;
    }

    if (!IsLanceLink(CurrentMode))
    {
        SetMode(MCInterfaceMode::None);
    }

    if (scanCode == RotateKey && Eye() != nullptr)
    {
        ScrollWait = 0;
    }

    if (const std::shared_ptr<MCTacticalMap> tacMap = ::TacticalMap(); tacMap != nullptr)
    {
        if (static_cast<int16_t>(key) == static_cast<int16_t>(Key(MCKeyCommand::HoldTacticalMap)) && TacMapShown)
        {
            tacMap->HideMe(tacMap->IsHidden() == 0);
        }

        if (const int32_t artillery = ArtilleryKey(*this, key); artillery >= 0)
        {
            tacMap->ActivateArtillery(artillery, 0);
        }
    }

    // The lance-link keys (ctrl+F1-F4) wait for a click on the lance's point.
    if (const int32_t lance = LanceKey(*this, key, MCKeyCommand::LinkLance1); lance >= 0 && AnySelected())
    {
        SetMode(static_cast<MCInterfaceMode>(static_cast<int32_t>(MCInterfaceMode::LinkLance1) + lance));
        GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(MCInterfaceCursor::LinkLance));
        GuiSystem().CursorHidden = true;
    }

    if (key == Key(MCKeyCommand::SelectVisible) && Scenario() != nullptr)
    {
        SelectVisibleMechs();
    }
}

auto MCTacticalInterface::ReleaseScrollKeys() -> void
{
    ScrollKeysHeld = {};
    TacScrollKeysHeld = {};
    ScrollDirection = -1;
    TacScrollDirection = -1;
}

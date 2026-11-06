#include "stdafx.h"
#include "iface/MCTacticalInterface.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearance.h"
#include "camera/MCCamera.h"
#include "camera/MCMainWindow.h"
#include "camera/MCViewWindow.h"
#include "iface/MCCommandParser.h"
#include "iface/MCMechBar.h"
#include "iface/MCOrderSink.h"
#include "network/MCMultiPlayer.h"
#include "object/MCArtillery.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "object/MCMiscTerrainObject.h"
#include "object/MCMover.h"
#include "object/MCMoverGroup.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCArtilleryButton.h"
#include "terrain/MCTacticalMap.h"

namespace
{
    /// <summary>Brings up the tactical map (out of its hidden slide) on <paramref name="page"/>.</summary>
    void ShowTacticalMapPage(MCTacmapPage page)
    {
        TacticalMap()->HideMe(false);
        TacticalMap()->SetDisplayType(page);
    }

    /// <summary>Whether the tactical map may show <paramref name="object"/>'s data: a revealed non-elemental mover.</summary>
    bool CanShowInfo(MCGameObject& object)
    {
        return IsMoverClass(object.ObjectClass) && object.ObjectClass != MCObjectClass::Elemental &&
               object.IsRevealed() != 0;
    }

    /// <summary>
    /// The map view <paramref name="window"/> is, held as its camera holds it (the camera owns its view); null for a
    /// window without a camera.
    /// </summary>
    std::shared_ptr<MCViewWindow> ViewOf(MCGuiObject& window)
    {
        const std::shared_ptr<MCCamera> camera = window.GetCamera();
        std::shared_ptr<MCViewWindow> view = camera != nullptr ? camera->View() : nullptr;
        SDL_assert(view == nullptr || view.get() == &window);
        return view;
    }

    /// <summary>A bridge (a misc terrain object of kind bridge): clicking one moves onto it.</summary>
    bool IsBridge(const MCBaseObject& object)
    {
        return object.ObjectClass == MCObjectClass::MiscTerrainObject &&
               static_cast<const MCMiscTerrainObject&>(object).Kind == MCMiscTerrainKind::Bridge;
    }

    /// <summary>
    /// The attack modifiers the range, ammunition and aimed-shot modes put on an attack order (the cases every target
    /// kind shares); the optimal range only with <paramref name="withOptimalRange"/>.
    /// </summary>
    /// <returns>False when <paramref name="mode"/> is not one of them.</returns>
    bool SetAttackModifier(MCTacticalOrder& order, MCInterfaceMode mode, bool withOptimalRange)
    {
        if (IsAimedShot(mode))
        {
            order.AttackParams.Pursue = 0;
            order.AttackParams.AimLocation = AimedLocation(mode);
            return true;
        }

        switch (mode)
        {
            case MCInterfaceMode::AttackOptimalRange:
            {
                if (!withOptimalRange)
                {
                    return false;
                }

                order.AttackParams.Range = -1;
                return true;
            }
            case MCInterfaceMode::AttackLongRange:
            {
                order.AttackParams.Range = 2;
                return true;
            }
            case MCInterfaceMode::AttackMediumRange:
            {
                order.AttackParams.Range = 1;
                return true;
            }
            case MCInterfaceMode::AttackShortRange:
            {
                order.AttackParams.Range = 0;
                return true;
            }
            case MCInterfaceMode::AttackFromPosition:
            {
                order.AttackParams.Pursue = 0;
                return true;
            }
            case MCInterfaceMode::AttackConservingAmmo:
            {
                order.AttackParams.Type = 3;
                return true;
            }
            default:
                return false;
        }
    }
}

auto MCTacticalInterface::QueueForcedOrder(MCTacticalOrder& order) -> void
{
    order.Pack();
    MCOrderSink& sink = Orders();

    if (MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0)
    {
        std::vector<int32_t> moverParts(SelectedMovers.size());

        for (size_t i = 0; i < SelectedMovers.size(); i++)
        {
            if (std::shared_ptr<MCFriendlyMechIcon> button = MechBar->GetButtonFromID(SelectedMovers[i]);
                button != nullptr)
            {
                moverParts[i] = button->PartId;
            }
        }

        sink.SendToServer(order, false, moverParts, {}, true);
    }

    for (int32_t partId : SelectedMovers)
    {
        std::shared_ptr<MCFriendlyMechIcon> button = MechBar->GetButtonFromID(partId);

        if (button == nullptr)
        {
            continue;
        }

        const std::shared_ptr<MCBaseObject> object = LockObject(button->Mover);

        if (object == nullptr || !IsMoverClass(object->ObjectClass))
        {
            continue;
        }

        auto& member = static_cast<MCMover&>(*object);

        if (member.GetPilot() == nullptr)
        {
            continue;
        }

        if (MultiPlayer() != nullptr)
        {
            order.Id = 0;
            order.SetId(*member.GetPilot());
        }

        sink.Queue(member, order);
    }
}

auto MCTacticalInterface::HandleMouse(MCGuiEvent& event) -> void
{
    switch (event.Type)
    {
        case 1:
        {
            // Left button down on the map: remember where, or give the forced order.
            if (event.TargetObject() != MainHolder()->GetActivePane())
            {
                return;
            }

            MouseDownX = static_cast<float>(event.X);
            MouseDownY = static_cast<float>(event.Y);

            if (!ForcingOrder)
            {
                MouseDown = true;
                return;
            }

            if (ForcedOrder == MCForcedOrder::None)
            {
                SoundSystem()->PlayInterfaceSample(0x46);
                return;
            }

            const std::shared_ptr<MCGuiObject> target = event.Target.lock();
            // Port: the mouse position in a view is on its world surface (through the zoom).
            MCVector2D screenPos = MCWindowPoint(*target, event.X, event.Y);
            // Port fix: zeroed; the original left the point uninitialised when the window has no camera.
            MCWayPathPoint point{MCVector3D(0.0f, 0.0f, 0.0f), ForcedOrder == MCForcedOrder::Run};

            if (target != nullptr && target->GetCamera() != nullptr)
            {
                target->GetCamera()->InverseProject(screenPos, point.Location);
            }

            MCTacticalOrder forcedOrder;
            forcedOrder.Reset();

            if (ForcedOrder == MCForcedOrder::Jump)
            {
                forcedOrder.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::JumpToPoint, 0);
                forcedOrder.InitWayPath(std::span(&point, 1));
                forcedOrder.MoveParams.Wait = 0;
            }
            else
            {
                forcedOrder.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::MoveToPoint, 0);
                forcedOrder.InitWayPath(std::span(&point, 1));
                forcedOrder.MoveParams.WayPath.Mode[0] = static_cast<uint8_t>(ForcedOrder);
                forcedOrder.MoveParams.Wait = 0;

                if (CurrentMode == MCInterfaceMode::LayMines)
                {
                    forcedOrder.MoveParams.Mode = 1;
                }
            }

            QueueForcedOrder(forcedOrder);
            return;
        }

        case 4:
        {
            // Left button up: ends a drag selection, or gives the order the click means.
            if (!MouseDown)
            {
                return;
            }

            const std::shared_ptr<MCGuiObject> target = event.Target.lock();
            MouseDown = false;

            if (target != MainHolder()->GetActivePane())
            {
                // The GUI hands a mouse event only to the object under the mouse, its target.
                SDL_assert(target != nullptr);
                MainHolder()->SetActivePane(*target);
                UpdateMouseState(event);
                return;
            }

            if (const std::shared_ptr<MCViewWindow> dragWindow = DragTarget.lock(); dragWindow != nullptr)
            {
                GuiSystem().CursorHidden = false;

                if (event.ShiftKey == 0)
                {
                    ClearMechSelection();
                    CommandParser->ClearSubjects();
                    SelectedMovers.clear();
                }

                DeselectEnemy();
                GuiSystem().Release();
                const std::array<float, 4>& box = dragWindow->SelectionBox;
                const auto left = static_cast<int32_t>(std::min(box[0], box[2]));
                const auto right = static_cast<int32_t>(box[2] < box[0] ? box[0] : box[2]);
                const auto top = static_cast<int32_t>(std::min(box[1], box[3]));
                const auto bottom = static_cast<int32_t>(box[3] < box[1] ? box[1] : box[3]);

                for (const std::shared_ptr<MCFriendlyMechIcon>& button : MechBar->Buttons())
                {
                    const std::shared_ptr<MCBaseObject> member = LockObject(button->Mover);

                    if (member == nullptr || member->GetAppearance() == nullptr)
                    {
                        continue;
                    }

                    // Port: the box is in the view's own coordinates; the mover's position is mapped there through
                    // the zoom. Inside as Win32's PtInRect: the right and bottom edges are out.
                    const std::shared_ptr<MCAppearance> appearance = member->GetAppearance();
                    const std::shared_ptr<MCCamera> camera = dragWindow->GetCamera();
                    const MCVector2D screenPos = dragWindow->WorldToWindow(
                        camera != nullptr ? appearance->GetScreenPos(*camera) : appearance->GetScreenPos());
                    const auto x = static_cast<int32_t>(screenPos.X);
                    const auto y = static_cast<int32_t>(screenPos.Y);

                    if (x >= left && x < right && y >= top && y < bottom)
                    {
                        SelectMech(button->PartId);
                        CommandParser->AddSubject(button->PartId);
                    }
                }

                dragWindow->SelectionBox[1] = 0.0f;
                dragWindow->SelectionBox[0] = 0.0f;
                dragWindow->SelectionBox[3] = 0.0f;
                dragWindow->SelectionBox[2] = 0.0f;
                DragTarget.reset();

                if (IsLanceLink(CurrentMode))
                {
                    SetMode(MCInterfaceMode::None);
                    GuiSystem().CursorHidden = false;
                }

                UpdateInterface();
                return;
            }

            const std::shared_ptr<MCCamera> camera = target != nullptr ? target->GetCamera() : nullptr;

            if (CurrentMode == MCInterfaceMode::DebugStrike && camera != nullptr)
            {
                // The debug "bunny" strike: a large strike where the player clicked.
                const MCVector2D screenPos = MCWindowPoint(*target, event.X, event.Y);
                MCVector3D strikePos;
                camera->InverseProject(screenPos, strikePos);
                HomeCommander()->SetNumLargeStrikes(HomeCommander()->NumLargeStrikes + 1);
                CallArtillery(HomeCommander()->Id, 1, strikePos, 3, 0);

                for (const MCGuiHandle<MCArtilleryButton>& button : TacticalMap()->ArtilleryButtons)
                {
                    button->Draw();
                }

                return;
            }

            HandleClick(event);
            return;
        }

        case 6:
        {
            ClearMechSelection();
            return;
        }
        case 7:
        {
            // Mouse move: track what is under the mouse, and drag out a selection box.
            if (event.TargetObject() != MainHolder()->GetActivePane())
            {
                return;
            }

            if (!MouseDown)
            {
                UpdateMouseState(event);

                if (const std::shared_ptr<MCGameObject> enemy = LockObject(SelectedEnemy); enemy != nullptr)
                {
                    enemy->SetSelected(0);
                }

                if (MouseTarget == MCMouseTarget::Enemy)
                {
                    const auto enemy = std::static_pointer_cast<MCGameObject>(LockObject(MouseObject));
                    SelectedEnemy = enemy;

                    if (enemy != nullptr)
                    {
                        enemy->SetSelected(1);
                    }
                }
            }

            if (event.LeftButton == 0 || !MouseDown)
            {
                return;
            }

            int32_t x = event.X;
            int32_t y = event.Y;
            std::shared_ptr<MCViewWindow> dragWindow = DragTarget.lock();
            bool dragging = dragWindow != nullptr;

            if (!dragging)
            {
                // x87: the distance is compared at extended precision.
                const double dy = static_cast<double>(static_cast<float>(y)) - MouseDownY;
                const double dx = static_cast<double>(x) - MouseDownX;
                dragging = static_cast<double>(DragDistance) < std::sqrt(dx * dx + dy * dy);
            }

            if (!dragging)
            {
                return;
            }

            GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(MCInterfaceCursor::Normal));
            GuiSystem().CursorHidden = true;

            if (dragWindow == nullptr)
            {
                // The event's target is the active pane, a map view.
                const std::shared_ptr<MCGuiObject> target = event.TargetObject();
                SDL_assert(target != nullptr);
                dragWindow = ViewOf(*target);

                if (dragWindow == nullptr)
                {
                    return;
                }

                DragTarget = dragWindow;
                GuiSystem().Grab(dragWindow);
                dragWindow->SelectionBox[0] = MouseDownX;
                dragWindow->SelectionBox[1] = MouseDownY;
            }

            x = std::max(x, dragWindow->GlobalX());
            y = std::max(y, dragWindow->GlobalY());

            if (dragWindow->GlobalX() + dragWindow->Width() <= x)
            {
                x = dragWindow->GlobalX() - 1 + dragWindow->Width();
            }

            if (dragWindow->GlobalY() + dragWindow->Height() <= y)
            {
                y = dragWindow->GlobalY() - 1 + dragWindow->Height();
            }

            dragWindow->SelectionBox[2] = static_cast<float>(x - dragWindow->GlobalX());
            dragWindow->SelectionBox[3] = static_cast<float>(y - dragWindow->GlobalY());
            return;
        }

        default:
            return;
    }
}

auto MCTacticalInterface::HandleClick(MCGuiEvent& event) -> void
{
    // What the click means depends on what is under the mouse and the mode. Aimed shots work only on mechs: on
    // anything else they are plain attacks (from the optimal range).
    const std::shared_ptr<MCGuiObject> target = event.Target.lock();
    const std::shared_ptr<MCCamera> camera = target != nullptr ? target->GetCamera() : nullptr;
    const MCInterfaceMode mode = CurrentMode;
    MCInterfaceMode command = mode;
    const auto clicked = std::static_pointer_cast<MCGameObject>(LockObject(MouseObject));
    int32_t clickedPartId = 0;

    if (clicked != nullptr)
    {
        clickedPartId = clicked->PartId;

        if (clicked->ObjectClass != MCObjectClass::BattleMech && IsAimedShot(mode))
        {
            command = MCInterfaceMode::AttackOptimalRange;
        }
    }

    MCTacticalOrder order;
    order.Reset();

    auto sendOrder = [&](bool sortMovers)
    {
        CommandParser->SendTacOrder(order, sortMovers);
        UpdateInterface();
    };

    auto finish = [&]() { UpdateInterface(); };

    // The mode reset after a lance link, then the usual redraw.
    auto endLanceLink = [&]()
    {
        SetMode(MCInterfaceMode::None);
        GuiSystem().CursorHidden = false;
        UpdateInterface();
    };

    auto initAttack = [&]()
    {
        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::AttackObject, 0);
        order.Target = clicked;
        order.AttackParams.Type = 1;
        order.AttackParams.Method = 0;
        order.AttackParams.Range = -4;
        order.AttackParams.Pursue = 1;
    };

    // An order on the clicked object: its code, the way path's run flag and the wait flag.
    auto initObjectOrder = [&](MCTacticalOrderCode code, bool run, int32_t wait)
    {
        order.Reset(MCOrderOrigin::Player, code, 0);
        order.Target = clicked;
        order.MoveParams.WayPath.Mode[0] = run ? 1 : 0;
        order.MoveParams.Wait = wait;
    };

    // Lay mines: move onto the object with move mode 1.
    auto initLayMines = [&]()
    {
        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::MoveToObject, 0);
        order.Target = clicked;
        order.MoveParams.WayPath.Mode[0] = 0;
        order.MoveParams.Wait = 0;
        order.MoveParams.Mode = 1;
    };

    // Jumps onto the clicked object, when every selected mover can.
    auto jumpToObject = [&]() -> bool
    {
        if (!CanSelectionJumpTo(clicked->GetPosition(), clicked, ForcingOrder))
        {
            return false;
        }

        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::JumpToObject, 0);
        order.Target = clicked;
        order.MoveParams.WayPath.Mode[0] = 2;
        order.MoveParams.Wait = 0;
        order.SetWayPoint(0, clicked->GetPosition());
        return true;
    };

    // An order along a one-point way path at MouseWorldPos.
    auto initPointOrder = [&](MCTacticalOrderCode code, bool run)
    {
        const MCWayPathPoint point{MouseWorldPos, run};
        order.Reset(MCOrderOrigin::Player, code, 0);
        order.InitWayPath(std::span(&point, 1));
        order.MoveParams.Wait = 0;
    };

    // Unprojects the click into MouseWorldPos; false without a camera.
    auto unprojectClick = [&]() -> bool
    {
        if (camera == nullptr)
        {
            return false;
        }

        camera->InverseProject(MCWindowPoint(*target, event.X, event.Y), MouseWorldPos);
        return true;
    };

    auto showInfo = [&]()
    {
        ShowTacticalMapPage(MCTacmapPage::Info);
        ::TacticalMap()->SetID(clickedPartId);
        finish();
    };

    auto followClicked = [&]()
    {
        if (camera != nullptr)
        {
            camera->ChangeTarget(clicked, false);
        }

        finish();
    };

    auto selectEnemyOnMap = [&](std::shared_ptr<MCGameObject> enemy)
    {
        if (const std::shared_ptr<MCGameObject> previous = LockObject(SelectedEnemy); previous != nullptr)
        {
            previous->SetSelected(0);
        }

        SelectedEnemy = enemy;

        if (enemy != nullptr)
        {
            enemy->SetSelected(1);
        }

        ::TacticalMap()->SetID(enemy->PartId);
    };

    switch (MouseTarget)
    {
        case MCMouseTarget::OwnMover:
        {
            initAttack();

            if (SetAttackModifier(order, command, true))
            {
                sendOrder(false);
                return;
            }

            auto& member = static_cast<MCMover&>(*clicked);

            switch (command)
            {
                case MCInterfaceMode::Eject:
                case MCInterfaceMode::Stop:
                case MCInterfaceMode::PowerUp:
                case MCInterfaceMode::PowerDown:
                {
                    // Eject, stop, power up and down go to the mover alone.
                    if (command == MCInterfaceMode::Eject)
                    {
                        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::Eject, 0);
                    }
                    else if (command == MCInterfaceMode::Stop)
                    {
                        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::Stop, 0);
                    }
                    else if (command == MCInterfaceMode::PowerUp)
                    {
                        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::PowerUp, 1);
                    }
                    else
                    {
                        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::PowerDown, 1);
                    }

                    if (MultiPlayer() == nullptr || MultiPlayer()->IsServer != 0)
                    {
                        Orders().Give(member, order);
                        finish();
                        return;
                    }

                    int32_t partId = member.PartId;

                    if (command == MCInterfaceMode::Stop)
                    {
                        member.GetPilot()->ClearTacOrderQueue();
                    }

                    Orders().SendToServer(order, false, std::span(&partId, 1), {}, false);
                    finish();
                    return;
                }

                case MCInterfaceMode::Run:
                {
                    initObjectOrder(MCTacticalOrderCode::MoveToObject, true, 1);
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::Refit:
                {
                    order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::Refit, 0);
                    order.Target = clicked;
                    order.MoveParams.WayPath.Mode[0] = 1;
                    sendOrder(false);
                    return;
                }
                case MCInterfaceMode::Jump:
                {
                    if (!jumpToObject())
                    {
                        finish();
                        return;
                    }

                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::Guard:
                {
                    initObjectOrder(MCTacticalOrderCode::Guard, false, 1);
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::LayMines:
                {
                    initLayMines();
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::LinkLance1:
                case MCInterfaceMode::LinkLance2:
                case MCInterfaceMode::LinkLance3:
                case MCInterfaceMode::LinkLance4:
                {
                    // Link the selected movers into a lance, the clicked one its point.
                    SoundSystem()->PlayBettySample(5);
                    const int32_t lance = LinkedLance(mode);

                    if (!IsSelected(clickedPartId))
                    {
                        SelectMech(clickedPartId);
                    }

                    // Port fix: zeroed; the original left the entry of a missing button uninitialised.
                    std::vector<std::shared_ptr<MCMover>> movers(SelectedMovers.size());
                    // The original's point index is left at the mode's number when the point is not found.
                    auto pointIndex = static_cast<int32_t>(command);

                    for (size_t i = 0; i < SelectedMovers.size(); i++)
                    {
                        std::shared_ptr<MCFriendlyMechIcon> button = MechBar->GetButtonFromID(SelectedMovers[i]);

                        if (button == nullptr)
                        {
                            continue;
                        }

                        movers[i] = std::static_pointer_cast<MCMover>(LockObject(button->Mover));
                        button->IsPoint = movers[i] == LockObject(MouseObject);

                        if (button->IsPoint)
                        {
                            pointIndex = static_cast<int32_t>(i);
                        }
                    }

                    for (const std::shared_ptr<MCLanceIcon>& lanceIcon : MechBar->LanceIcons())
                    {
                        if (const std::shared_ptr<MCMoverGroup> group =
                                lanceIcon != nullptr ? lanceIcon->Group.lock() : nullptr;
                            group != nullptr)
                        {
                            lanceIcon->NumActiveMovers = group->NumMovers();
                        }
                    }

                    SetUnit(lance, movers, pointIndex);
                    MechBar->PlaceButtons(true);
                    SetMode(MCInterfaceMode::None);
                    GuiSystem().CursorHidden = false;
                    ClearMechSelection();
                    SelectLance(HomeCommander()->GetGroup(lance));
                    CommandParser->AddSubject(HomeCommander()->GetGroup(lance));
                    UpdateInterface();
                    return;
                }

                case MCInterfaceMode::Info:
                {
                    showInfo();
                    return;
                }
                case MCInterfaceMode::CameraFollow:
                {
                    followClicked();
                    return;
                }
                default:
                    break;
            }

            // Otherwise the click selects the mover; shift adds it to the selection or takes it out.
            if (event.ShiftKey == 0)
            {
                ClearMechSelection();
                DeselectEnemy();
                CommandParser->ClearSubjects();

                if (!AnySelected())
                {
                    ::TacticalMap()->SetID(clickedPartId);
                }

                SelectMech(clickedPartId);
            }
            else
            {
                if (IsSelected(clickedPartId))
                {
                    DeselectMech(clickedPartId);
                    CommandParser->RemoveSubject(clickedPartId);
                    finish();
                    return;
                }

                SelectMech(clickedPartId);
                DeselectEnemy();
            }

            CommandParser->AddSubject(clickedPartId);
            SoundSystem()->PlayInterfaceSample(0x10);
            finish();
            return;
        }

        case MCMouseTarget::Enemy:
        case MCMouseTarget::EnemyObject:
        {
            if (!AnySelected())
            {
                // Nothing selected: the click shows the enemy on the tactical map.
                if (!CanShowInfo(*clicked))
                {
                    finish();
                    return;
                }

                selectEnemyOnMap(clicked);

                if (mode == MCInterfaceMode::Info)
                {
                    ShowTacticalMapPage(MCTacmapPage::Info);
                }

                if (mode == MCInterfaceMode::CameraFollow && camera != nullptr)
                {
                    camera->ChangeTarget(clicked, false);
                }

                finish();
                return;
            }

            order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::AttackObject, 0);
            order.MoveParams.WayPath.Mode[0] = mode == MCInterfaceMode::Run ? 1 : 0;
            order.AttackParams.Type = 1;
            order.AttackParams.Pursue = 1;
            order.Target = clicked;
            order.AttackParams.Method = 0;
            order.AttackParams.Range = -4;

            if (SetAttackModifier(order, command, false))
            {
                sendOrder(false);
                return;
            }

            switch (command)
            {
                case MCInterfaceMode::Run:
                {
                    initObjectOrder(MCTacticalOrderCode::MoveToObject, true, 1);
                    sendOrder(false);
                    return;
                }
                case MCInterfaceMode::Guard:
                {
                    initObjectOrder(MCTacticalOrderCode::Guard, true, 1);
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::LayMines:
                {
                    initLayMines();
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::Jump:
                {
                    if (!CanSelectionJumpTo(clicked->GetPosition(), nullptr, ForcingOrder))
                    {
                        finish();
                        return;
                    }

                    if (IsMoverClass(clicked->ObjectClass))
                    {
                        // A jump attack.
                        order.AttackParams.Method = 1;
                        sendOrder(false);
                        return;
                    }

                    if (camera == nullptr)
                    {
                        finish();
                        return;
                    }

                    MCWayPathPoint point;
                    camera->InverseProject(MCWindowPoint(*target, event.X, event.Y), point.Location);
                    order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::JumpToPoint, 0);
                    order.InitWayPath(std::span(&point, 1));
                    order.MoveParams.Wait = 0;
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::LinkLance1:
                case MCInterfaceMode::LinkLance2:
                case MCInterfaceMode::LinkLance3:
                case MCInterfaceMode::LinkLance4:
                {
                    endLanceLink();
                    return;
                }
                case MCInterfaceMode::Info:
                {
                    if (!CanShowInfo(*clicked))
                    {
                        finish();
                        return;
                    }

                    showInfo();
                    return;
                }
                case MCInterfaceMode::CameraFollow:
                {
                    followClicked();
                    return;
                }
                default:
                {
                    if (CanCapture)
                    {
                        if (CaptureBlocked)
                        {
                            finish();
                            return;
                        }

                        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::Capture, 0);
                        order.Target = clicked;
                        order.MoveParams.WayPath.Mode[0] = 1;
                        sendOrder(false);
                        return;
                    }

                    order.MoveParams.Wait = 0;
                    order.MoveParams.WayPath.Mode[0] = command == MCInterfaceMode::Run ? 1 : 0;
                    sendOrder(true);
                    return;
                }
            }
        }

        case MCMouseTarget::DisabledEnemy:
        {
            if (!AnySelected())
            {
                selectEnemyOnMap(clicked);
                finish();
                return;
            }

            initAttack();
            order.MoveParams.WayPath.Mode[0] = 0;

            if (!SetAttackModifier(order, command, false))
            {
                switch (command)
                {
                    case MCInterfaceMode::Jump:
                    {
                        if (CanSelectionJumpTo(clicked->GetPosition(), nullptr, ForcingOrder))
                        {
                            order.AttackParams.Method = 1;
                        }
                        break;
                    }
                    case MCInterfaceMode::LayMines:
                        initLayMines();
                        break;
                    case MCInterfaceMode::Info:
                    {
                        if (!CanShowInfo(*clicked))
                        {
                            finish();
                            return;
                        }

                        showInfo();
                        return;
                    }
                    case MCInterfaceMode::CameraFollow:
                    {
                        followClicked();
                        return;
                    }
                    case MCInterfaceMode::LinkLance1:
                    case MCInterfaceMode::LinkLance2:
                    case MCInterfaceMode::LinkLance3:
                    case MCInterfaceMode::LinkLance4:
                    {
                        SetMode(MCInterfaceMode::None);
                        GuiSystem().CursorHidden = false;
                        [[fallthrough]];
                    }
                    default:
                        initObjectOrder(MCTacticalOrderCode::MoveToObject, command == MCInterfaceMode::Run, 1);
                        break;
                }
            }

            sendOrder(false);
            return;
        }

        case MCMouseTarget::Ally:
        {
            if (!AnySelected())
            {
                if (command == MCInterfaceMode::Info)
                {
                    ShowTacticalMapPage(MCTacmapPage::Info);
                }
                else if (command == MCInterfaceMode::CameraFollow)
                {
                    followClicked();
                    return;
                }

                ::TacticalMap()->SetID(clicked->PartId);
                finish();
                return;
            }

            initAttack();

            if (SetAttackModifier(order, command, true))
            {
                sendOrder(false);
                return;
            }

            switch (command)
            {
                case MCInterfaceMode::Run:
                {
                    initObjectOrder(MCTacticalOrderCode::Guard, true, 0);
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::Jump:
                {
                    if (!jumpToObject())
                    {
                        finish();
                        return;
                    }

                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::LayMines:
                {
                    initLayMines();
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::LinkLance1:
                case MCInterfaceMode::LinkLance2:
                case MCInterfaceMode::LinkLance3:
                case MCInterfaceMode::LinkLance4:
                {
                    endLanceLink();
                    return;
                }
                case MCInterfaceMode::Info:
                {
                    showInfo();
                    return;
                }
                case MCInterfaceMode::CameraFollow:
                {
                    followClicked();
                    return;
                }
                default:
                {
                    // Including guard: guard it.
                    initObjectOrder(MCTacticalOrderCode::Guard, false, 1);
                    sendOrder(true);
                    return;
                }
            }
        }

        case MCMouseTarget::SensorContact:
        {
            switch (command)
            {
                case MCInterfaceMode::Run:
                {
                    initObjectOrder(MCTacticalOrderCode::MoveToObject, true, 0);
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::LayMines:
                {
                    initLayMines();
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::LinkLance1:
                case MCInterfaceMode::LinkLance2:
                case MCInterfaceMode::LinkLance3:
                case MCInterfaceMode::LinkLance4:
                {
                    endLanceLink();
                    return;
                }
                case MCInterfaceMode::Info:
                {
                    finish();
                    return;
                }
                case MCInterfaceMode::CameraFollow:
                {
                    followClicked();
                    return;
                }
                default:
                {
                    initObjectOrder(MCTacticalOrderCode::MoveToObject, false, 0);
                    sendOrder(true);
                    return;
                }
            }
        }
        case MCMouseTarget::Object:
        {
            initAttack();
            order.MoveParams.WayPath.Mode[0] = 0;

            if (SetAttackModifier(order, command, true))
            {
                sendOrder(false);
                return;
            }

            switch (command)
            {
                case MCInterfaceMode::Run:
                {
                    if (!IsBridge(*clicked))
                    {
                        initObjectOrder(MCTacticalOrderCode::MoveToObject, true, 0);
                        sendOrder(true);
                        return;
                    }

                    // A bridge: move onto the point clicked (the attack order goes out unchanged without a camera).
                    if (unprojectClick())
                    {
                        initPointOrder(MCTacticalOrderCode::MoveToPoint, true);
                        order.MoveParams.WayPath.Mode[0] = 1;
                    }

                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::Repair:
                {
                    order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::GetFixed, 0);
                    order.Target = clicked;
                    order.MoveParams.WayPath.Mode[0] = 1;
                    sendOrder(false);
                    return;
                }
                case MCInterfaceMode::Jump:
                {
                    if (!unprojectClick() || !CanSelectionJumpTo(MouseWorldPos, nullptr, ForcingOrder))
                    {
                        finish();
                        return;
                    }

                    initPointOrder(MCTacticalOrderCode::JumpToPoint, false);
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::Guard:
                {
                    initObjectOrder(MCTacticalOrderCode::Guard, false, 0);
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::LayMines:
                {
                    initLayMines();
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::LinkLance1:
                case MCInterfaceMode::LinkLance2:
                case MCInterfaceMode::LinkLance3:
                case MCInterfaceMode::LinkLance4:
                {
                    endLanceLink();
                    return;
                }
                case MCInterfaceMode::Info:
                {
                    finish();
                    return;
                }
                case MCInterfaceMode::CameraFollow:
                {
                    followClicked();
                    return;
                }
                default:
                {
                    if (static_cast<uint8_t>(clicked->Status) == 2 || static_cast<uint8_t>(clicked->Status) == 1)
                    {
                        // Wrecked: move to it.
                        initObjectOrder(MCTacticalOrderCode::MoveToObject, false, 0);
                        sendOrder(false);
                        return;
                    }

                    if (!IsBridge(*clicked) && clicked->GetAlignment() != HomeTeam()->Alignment)
                    {
                        // Someone else's: attack it.
                        order.AttackParams.Range = -4;
                        sendOrder(false);
                        return;
                    }

                    // The player's own, or a bridge: move to the point clicked.
                    if (!unprojectClick())
                    {
                        finish();
                        return;
                    }

                    initPointOrder(MCTacticalOrderCode::MoveToPoint, command == MCInterfaceMode::Run);
                    order.MoveParams.WayPath.Mode[0] = command == MCInterfaceMode::Run ? 1 : 0;
                    sendOrder(false);
                    return;
                }
            }
        }

        case MCMouseTarget::Nothing:
        {
            if (!unprojectClick())
            {
                finish();
                return;
            }

            if (IsAttackMode(mode) || IsAimedShot(mode))
            {
                // Attack the point.
                order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::AttackPoint, 0);
                order.AttackParams.TargetPoint = MouseWorldPos;
                order.Target.reset();
                order.AttackParams.Type = 1;
                order.AttackParams.Method = 0;
                order.AttackParams.Range = -4;
                order.AttackParams.Pursue = 1;

                if (IsAttackMode(command))
                {
                    SetAttackModifier(order, command, true);
                }

                sendOrder(true);
                return;
            }

            initPointOrder(MCTacticalOrderCode::MoveToPoint, false);
            order.MoveParams.WayPath.Mode[0] = 0;

            switch (command)
            {
                case MCInterfaceMode::None:
                {
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::Run:
                {
                    order.MoveParams.WayPath.Mode[0] = 1;
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::Jump:
                {
                    if (!CanSelectionJumpTo(MouseWorldPos, nullptr, ForcingOrder))
                    {
                        finish();
                        return;
                    }

                    initPointOrder(MCTacticalOrderCode::JumpToPoint, false);
                    order.MoveParams.WayPath.Mode[0] = 0;
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::Guard:
                {
                    initPointOrder(MCTacticalOrderCode::Guard, false);
                    order.MoveParams.WayPath.Mode[0] = 0;
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::LayMines:
                {
                    order.MoveParams.Mode = 1;
                    sendOrder(true);
                    return;
                }
                case MCInterfaceMode::LinkLance1:
                case MCInterfaceMode::LinkLance2:
                case MCInterfaceMode::LinkLance3:
                case MCInterfaceMode::LinkLance4:
                {
                    endLanceLink();
                    return;
                }
                case MCInterfaceMode::CameraFollow:
                {
                    // Move the camera to the point.
                    camera->ChangeTarget(nullptr, false);
                    camera->SetPosition(MouseWorldPos);
                    finish();
                    return;
                }
                default:
                {
                    finish();
                    return;
                }
            }
        }

        default:
        {
            finish();
            return;
        }
    }
}

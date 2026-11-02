#include "stdafx.h"
#include "iface/MCTacticalInterface.h"
#include "ai/MCMoveSystem.h"
#include "camera/MCCamera.h"
#include "camera/MCMainWindow.h"
#include "gui/MCFloatHelp.h"
#include "iface/MCMechBar.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "main/MCGameStrings.h"
#include "network/MCMultiPlayer.h"
#include "object/MCBuilding.h"
#include "object/MCForces.h"
#include "object/MCGate.h"
#include "object/MCMechWarrior.h"
#include "object/MCMiscTerrainObject.h"
#include "object/MCMover.h"
#include "object/MCObjectEvent.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"
#include "object/MCTrainCar.h"
#include "object/MCTreeBuilding.h"
#include "object/MCTurret.h"
#include "platform/MCInput.h"
#include "terrain/MCTacticalMap.h"

namespace
{
    /// <summary>Sets the cursor to <paramref name="cursor"/>.</summary>
    void SetCursor(MCInterfaceCursor cursor)
    {
        GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(cursor));
    }

    /// <summary>
    /// The name of the network player whose mover roster holds <paramref name="object"/>, or an empty string.
    /// </summary>
    std::string NetPlayerName(const MCBaseObject& object)
    {
        for (int32_t player = 0; player < 6; player++)
        {
            for (const std::weak_ptr<MCMover>& slot : MultiPlayer()->PlayerMoverRoster[player])
            {
                const std::shared_ptr<MCMover> mover = LockObject(slot);

                if (mover == nullptr)
                {
                    break;
                }

                if (mover.get() == &object)
                {
                    const auto entry = MultiPlayer()->SessionManager->GetPlayerNumber(player);
                    return entry != nullptr ? std::string(entry->Name) : std::string();
                }
            }
        }

        return {};
    }

    /// <summary>A string table entry formatted with a mover's name (the original's 254-byte buffer).</summary>
    std::string FormatGameString(uint32_t id, std::string_view name)
    {
        const std::string format = LoadGameString(id, 0xfe);
        return MCFormatPrintf(format.c_str(), std::string(name).c_str());
    }

    /// <summary>The cursor of a plain attack mode (or none): the range modes' and the aimed shots'.</summary>
    std::optional<MCInterfaceCursor> RangeCursor(MCInterfaceMode mode)
    {
        switch (mode)
        {
            case MCInterfaceMode::AttackOptimalRange:
            case MCInterfaceMode::AttackConservingAmmo:
                return MCInterfaceCursor::OptimalRange;
            case MCInterfaceMode::AttackLongRange:
                return MCInterfaceCursor::LongRange;
            case MCInterfaceMode::AttackMediumRange:
                return MCInterfaceCursor::MediumRange;
            case MCInterfaceMode::AttackShortRange:
                return MCInterfaceCursor::ShortRange;
            case MCInterfaceMode::AttackFromPosition:
                return MCInterfaceCursor::FromPosition;
            default:
                return std::nullopt;
        }
    }
}

auto MCTacticalInterface::UpdateMouseState(MCOptionalRef<MCGuiEvent> mouseEvent) -> void
{
    MCFloatHelp& tag = *FloatingTags[0];
    MCGuiEvent cursorEvent;
    std::string text;
    CursorOffset = 0;

    if (ScreenWindow() == nullptr || ObjectList() == nullptr)
    {
        return;
    }

    if (!mouseEvent.has_value())
    {
        // The per-frame call: a mouse event at the cursor, over the window under it (the map, not the mech bar).
        const MCPoint cursor = MCInput::GetCursorPos();
        cursorEvent.Clear();
        cursorEvent.X = cursor.x;
        cursorEvent.Y = cursor.y;
        cursorEvent.Target = ScreenWindow()->FindObject(cursor.x, cursor.y);

        if (cursorEvent.TargetObject() == MechBar)
        {
            cursorEvent.Target = MainHolder()->GetActivePane();
        }

        if (cursorEvent.Target.expired())
        {
            return;
        }

        mouseEvent = cursorEvent;
    }

    MCGuiEvent& event = *mouseEvent;
    const std::shared_ptr<MCGuiObject> window = event.Target.lock();

    if (window->ObjectType == 7)
    {
        return;
    }

    if (window->ParentObject().get() != MechBar.get())
    {
        HideTags();
        MechBar->HighlightId = -1;
    }

    // The floating tag, in the colours the object calls for.
    auto showTag = [&](uint8_t backColor, uint8_t textColor)
    {
        tag.SetBackColor(backColor);
        tag.TextColor = textColor;
        tag.SetHelpText(text.data());
        tag.ShowGuiWindow(true);
    };

    // What is under the mouse.
    if (window->GetCamera() == nullptr)
    {
        if (window->ObjectType == 8)
        {
            // A mech icon: its mover.
            MouseTarget = MCMouseTarget::OwnMover;
            MouseObject = LockObject(static_cast<MCMechIcon&>(*window).Mover);
        }
    }
    else
    {
        MCObjectEvent objectEvent;
        objectEvent.Init(0, event);
        auto object = std::static_pointer_cast<MCGameObject>(ObjectList()->FindObjectFromEvent(objectEvent));
        MouseObject = object;

        if (object == nullptr)
        {
            MouseTarget = MCMouseTarget::Nothing;
        }
        else
        {
            int tagged = 0;
            const int32_t contactType = object->GetContactType(HomeTeam()->Id, tagged);

            if (tagged == 0 && contactType == 2)
            {
                MouseTarget = MCMouseTarget::SensorContact;
            }
            else if (HomeTeam()->LineOfSight(object->GetPosition()) == 0)
            {
                MouseTarget = MCMouseTarget::Nothing;
            }
            else
            {
                auto tagByAlignment = [&](bool capturedIsAlly)
                {
                    if (capturedIsAlly && object->IsCaptured() != 0)
                    {
                        showTag(0x1f, 0xc);
                    }
                    else if (object->GetAlignment() == HomeTeam()->Alignment)
                    {
                        showTag(0, 0xb);
                    }
                    else if (HomeTeam()->LineOfSight(object->GetPosition()) != 0)
                    {
                        showTag(0, 0xef);
                    }
                };

                switch (object->ObjectClass)
                {
                    case MCObjectClass::BattleMech:
                    case MCObjectClass::GroundVehicle:
                    {
                        auto mover = std::static_pointer_cast<MCMover>(object);
                        const std::string_view name = MCPrintfText(mover->GetIfaceName());
                        tag.HelpObject = object;

                        if (mover->NetPlayerId >= 0 && mover->IsCaptureable() == 0)
                        {
                            // The player's own: pilot and mover, and the mech bar highlights its icon.
                            if (mover->GetAwake() != 0)
                            {
                                text = MultiPlayer() != nullptr
                                           ? std::format("{}\n{}\n{}", mover->GetPilot()->Callsign, name,
                                                         NetPlayerName(*object))
                                           : std::format("{}\n{}", mover->GetPilot()->Callsign, name);
                            }
                            else
                            {
                                text =
                                    FormatGameString(mover->IsCaptureable() != 0 ? 0x99 : 0x9a, mover->GetIfaceName());
                            }

                            MouseTarget = MCMouseTarget::OwnMover;
                            showTag(0, 0xb);
                            MechBar->HighlightId = object->PartId;
                            MechBar->Draw();
                            break;
                        }

                        if ((mover->IsCaptured() != 0 && mover->GetAlignment() == HomeTeam()->Alignment) ||
                            AlliedTeam() == mover->GetTeam())
                        {
                            // Captured by the player, or an ally.
                            text = name;
                            MouseTarget = MCMouseTarget::Ally;
                            showTag(0x1f, 0xc);
                            break;
                        }

                        if (MultiPlayer() != nullptr && mover->GetAlignment() == HomeTeam()->Alignment)
                        {
                            // A teammate's mover.
                            text = std::format("{}\n{}\n{}", mover->GetPilot()->Callsign, name, NetPlayerName(*object));
                            MouseTarget = MCMouseTarget::Ally;
                            showTag(0, 0xb);
                            break;
                        }

                        if (contactType != 1)
                        {
                            break;
                        }

                        // An enemy in sight.
                        if (mover->IsDisabled() != 0)
                        {
                            MouseTarget = MCMouseTarget::DisabledEnemy;
                        }
                        else if (mover->IsDestroyed() == 0)
                        {
                            MouseTarget = MCMouseTarget::Enemy;
                        }

                        if (mover->IsCaptureable() != 0)
                        {
                            text = FormatGameString(0x99, mover->GetIfaceName());
                        }
                        else if (mover->GetAwake() != 0)
                        {
                            text = MultiPlayer() != nullptr ? std::format("{}\n{}", name, NetPlayerName(*object))
                                                            : std::string(name);
                        }
                        else
                        {
                            text = FormatGameString(object->ObjectClass == MCObjectClass::BattleMech ? 0x9c : 0x9d,
                                                    mover->GetIfaceName());
                        }

                        showTag(0, 0xef);
                        break;
                    }

                    case MCObjectClass::Artillery:
                    case MCObjectClass::Debris:
                    case MCObjectClass::Unused14:
                    {
                        MouseTarget = MCMouseTarget::Nothing;
                        MouseObject.reset();
                        break;
                    }
                    case MCObjectClass::Building:
                    case MCObjectClass::MiscTerrainObject:
                    case MCObjectClass::TreeBuilding:
                    case MCObjectClass::Turret:
                    case MCObjectClass::Gate:
                    {
                        switch (object->ObjectClass)
                        {
                            case MCObjectClass::Building:
                                text = static_cast<MCBuilding&>(*object).Name;
                                break;
                            case MCObjectClass::TreeBuilding:
                                text = static_cast<MCTreeBuilding&>(*object).Name;
                                break;
                            case MCObjectClass::Turret:
                                text = static_cast<MCTurret&>(*object).Name;
                                break;
                            case MCObjectClass::Gate:
                                text = static_cast<MCGate&>(*object).Name;
                                break;
                            default:
                            {
                                // A misc terrain object: its kind's name (none for the kinds without one).
                                uint32_t stringId = 0;

                                switch (static_cast<MCMiscTerrainObject&>(*object).Kind)
                                {
                                    case MCMiscTerrainKind::Bridge:
                                        stringId = 0x9e;
                                        break;
                                    case MCMiscTerrainKind::Forest:
                                        stringId = 0x9f;
                                        break;
                                    case MCMiscTerrainKind::Wall:
                                        stringId = 0xa0;
                                        break;
                                    case MCMiscTerrainKind::MediumWall:
                                        stringId = 0xa1;
                                        break;
                                    case MCMiscTerrainKind::LightWall:
                                        stringId = 0xa2;
                                        break;
                                    default:
                                        break;
                                }

                                // Port fix: empty for a kind without a name; the original formatted an unset buffer.
                                text = stringId != 0 ? MCFormatPrintf(LoadGameString(stringId, 0xfe).c_str())
                                                     : std::string();
                                break;
                            }
                        }

                        if (const std::shared_ptr<MCGameObject> highlighted = LockObject(_HighlightedObject);
                            highlighted != nullptr)
                        {
                            highlighted->SetSelected(0);
                            _HighlightedObject.reset();
                        }

                        // A turret shows its tag only while deployed (or fixed).
                        if (object->ObjectClass != MCObjectClass::Turret ||
                            static_cast<MCTurret&>(*object).WeaponDeployed != 0 ||
                            static_cast<MCTurret&>(*object).FixedTurret != 0)
                        {
                            _HighlightedObject = object;
                            object->SetSelected(1);
                            tag.HelpObject = object;

                            if (object->IsCaptured() != 0 && object->GetAlignment() == HomeTeam()->Alignment)
                            {
                                showTag(0x1f, 0xc);
                            }
                            else
                            {
                                tagByAlignment(false);
                            }
                        }

                        [[fallthrough]];
                    }
                    default:
                    {
                        const bool scrap = object->ObjectClass == MCObjectClass::MiscTerrainObject &&
                                           static_cast<MCMiscTerrainObject&>(*object).Kind == MCMiscTerrainKind::Bridge;

                        if (object->GetAlignment() == HomeTeam()->Alignment || object->IsDestroyed() != 0 || scrap)
                        {
                            MouseTarget = MCMouseTarget::Object;
                        }
                        else
                        {
                            MouseTarget = MCMouseTarget::EnemyObject;
                        }
                        break;
                    }
                    case MCObjectClass::CameraDrone:
                    {
                        MouseTarget = MCMouseTarget::Object;
                        text = MCFormatPrintf(LoadGameString(0x96, 0xfe).c_str());
                        tag.HelpObject = object;
                        tagByAlignment(false);
                        break;
                    }
                    case MCObjectClass::TrainCar:
                    {
                        tag.HelpObject = object;
                        text = static_cast<MCTrainCar&>(*object).Name;
                        tag.SetBackColor(0x1f);
                        tag.TextColor = 0xc;
                        tagByAlignment(true);
                        MouseTarget = object->GetAlignment() == HomeTeam()->Alignment ? MCMouseTarget::Ally
                                                                                      : MCMouseTarget::Enemy;
                        break;
                    }
                }
            }
        }
    }

    // Port: in a view, on its world surface (through the zoom).
    const MCVector2D mousePos = MCWindowPoint(*window, event.X, event.Y);

    // A forced order (see HandleMouse): a move, a run or a jump to the point.
    if (ForcingOrder)
    {
        bool allowed = window->GetCamera() != nullptr;

        for (size_t i = 0; allowed && i < SelectedMovers.size(); i++)
        {
            std::shared_ptr<MCBaseObject> object = ObjectList()->FindObjectFromPart(SelectedMovers[i]);

            // A mover whose order queue is full takes no more.
            if (IsMoverClass(object->ObjectClass) && static_cast<MCMover&>(*object).GetPilot() != nullptr &&
                static_cast<MCMover&>(*object).GetPilot()->GetTacOrderQueueSize() >= 0xf)
            {
                allowed = false;
            }
        }

        if (allowed)
        {
            if (CurrentMode != MCInterfaceMode::LayMines && CurrentMode != MCInterfaceMode::Run &&
                CurrentMode != MCInterfaceMode::Jump && GuiSystem().CursorHidden == 0)
            {
                SetMode(MCInterfaceMode::None);
            }

            MCVector3D point;

            if (const std::shared_ptr<MCBaseObject> mouseObject = LockObject(MouseObject); mouseObject != nullptr)
            {
                point = static_cast<MCGameObject&>(*mouseObject).GetPosition();
            }
            else
            {
                window->GetCamera()->InverseProject(mousePos, point);
            }

            const bool run = CurrentMode == MCInterfaceMode::Run;

            if (CurrentMode != MCInterfaceMode::Jump)
            {
                if (GameMap().CellPassable(point) != 0)
                {
                    SetCursorOffset(mousePos);
                    SetCursor(run ? MCInterfaceCursor::Run : MCInterfaceCursor::Move);
                    ForcedOrder = run ? MCForcedOrder::Run : MCForcedOrder::Move;
                    return;
                }

                SetCursor(MCInterfaceCursor::Forbidden);
                ForcedOrder = run ? MCForcedOrder::Run : MCForcedOrder::Move;
                return;
            }

            if (CanSelectionJumpTo(point, nullptr, ForcingOrder))
            {
                SetCursorOffset(mousePos);
                SetCursor(MCInterfaceCursor::Jump);
                ForcedOrder = MCForcedOrder::Jump;
                return;
            }
        }

        SetCursor(MCInterfaceCursor::Forbidden);
        ForcedOrder = MCForcedOrder::None;
        return;
    }

    // The refit and repair modes follow what the mouse is over.
    const auto object = std::static_pointer_cast<MCGameObject>(LockObject(MouseObject));

    if (CurrentMode == MCInterfaceMode::Refit && !RefitCheck(object))
    {
        SetMode(MCInterfaceMode::None);
    }
    else if (CurrentMode == MCInterfaceMode::None && RefitCheck(object))
    {
        SetMode(MCInterfaceMode::Refit);
    }

    if (CurrentMode == MCInterfaceMode::Repair && !GetFixedCheck(object))
    {
        SetMode(MCInterfaceMode::None);
    }
    else if (CurrentMode == MCInterfaceMode::None && GetFixedCheck(object))
    {
        SetMode(MCInterfaceMode::Repair);
    }

    // Port: the original asserts that mouseObject and homeTeam are null or readable (" Mouseobject is bad!!! ",
    // " homeTeam is bad!!! "), probing them with Win32's IsBadReadPtr. The port has no memory probe and treats a
    // non-null pointer as readable (as aObject does), so both asserts always pass and are left out.
    CanCapture = false;
    CaptureBlocked = false;

    if (object != nullptr && object->IsCaptureable() != 0 &&
        (CurrentMode == MCInterfaceMode::None || CurrentMode == MCInterfaceMode::Run) &&
        object->GetAlignment() != HomeTeam()->Alignment && HomeTeam()->LineOfSight(object->GetPosition()) != 0)
    {
        CanCapture = true;
        CaptureBlocked = object->GetCaptureBlocker(HomeTeam()->Alignment) != nullptr;
    }

    if (const std::shared_ptr<MCGameObject> highlighted = LockObject(_HighlightedObject);
        highlighted != nullptr && (object == nullptr || object->IsBuilding() == 0))
    {
        highlighted->SetSelected(0);
        _HighlightedObject.reset();
    }

    ::TacticalMap()->UpdateOrderPalette();

    if (window->GetCamera() == nullptr && window->ObjectType != 8)
    {
        return;
    }

    UpdateCursor(*window, mousePos);
}

auto MCTacticalInterface::UpdateCursor(MCGuiObject& window, MCVector2D mousePos) -> void
{
    const auto object = std::static_pointer_cast<MCGameObject>(LockObject(MouseObject));

    if (CurrentMode == MCInterfaceMode::Info)
    {
        // The info mode wants a revealed mover (not an elemental).
        const bool showable = object != nullptr && IsMoverClass(object->ObjectClass) &&
                              object->ObjectClass != MCObjectClass::Elemental && object->IsRevealed() != 0;
        SetCursor(showable ? MCInterfaceCursor::Info : MCInterfaceCursor::Forbidden);
        return;
    }

    if (!AnySelected() || CurrentMode == MCInterfaceMode::CameraFollow)
    {
        SetCursor(MCInterfaceCursor::Normal);
        return;
    }

    // An attack cursor needs an armed mover selected.
    auto armedCursor = [this](MCInterfaceCursor cursor)
    { SetCursor(AnySelected(true) ? cursor : MCInterfaceCursor::Forbidden); };
    // An aimed shot: the aimed cursor over a mech, a plain attack over anything else.
    auto aimedCursor = [&]()
    {
        if (!AnySelected(true))
        {
            SetCursor(MCInterfaceCursor::Forbidden);
        }
        else if (object != nullptr && object->ObjectClass == MCObjectClass::BattleMech)
        {
            SetCursor(MCInterfaceCursor::FromPosition);
        }
        else
        {
            SetCursor(MCInterfaceCursor::Attack);
        }
    };

    // The jump cursor where the selection can jump to, else forbidden.
    auto jumpCursor = [&](MCVector3D point)
    {
        if (CanSelectionJumpTo(point, nullptr, ForcingOrder))
        {
            SetCursorOffset(mousePos);
            SetCursor(MCInterfaceCursor::Jump);
        }
        else
        {
            SetCursor(MCInterfaceCursor::Forbidden);
        }
    };

    // The world point under the mouse; false without a camera.
    auto mousePoint = [&](MCVector3D& point)
    {
        const std::shared_ptr<MCCamera> camera = window.GetCamera();

        if (camera == nullptr)
        {
            return false;
        }

        camera->InverseProject(mousePos, point);
        return true;
    };

    auto runCursor = [&]()
    {
        SetCursorOffset(mousePos);
        SetCursor(MCInterfaceCursor::Run);
    };

    switch (MouseTarget)
    {
        case MCMouseTarget::OwnMover:
        {
            // A mover of the player's (or its icon).
            if (const std::optional<MCInterfaceCursor> range = RangeCursor(CurrentMode); range.has_value())
            {
                armedCursor(*range);
                return;
            }

            if (IsAimedShot(CurrentMode))
            {
                aimedCursor();
                return;
            }

            switch (CurrentMode)
            {
                case MCInterfaceMode::Run:
                {
                    runCursor();
                    return;
                }
                case MCInterfaceMode::Refit:
                {
                    SetCursor(MCInterfaceCursor::Refit);
                    return;
                }
                case MCInterfaceMode::Jump:
                {
                    if (MCVector3D point; mousePoint(point))
                    {
                        jumpCursor(point);
                    }

                    return;
                }
                case MCInterfaceMode::Guard:
                {
                    SetCursor(MCInterfaceCursor::Guard);
                    return;
                }
                default:
                {
                    SetCursor(MCInterfaceCursor::Normal);
                    return;
                }
            }
        }
        case MCMouseTarget::Enemy:
        case MCMouseTarget::EnemyObject:
        {
            // An enemy. (Unlike over the player's own, the optimal range mode here takes the default: attack.)
            if (CurrentMode != MCInterfaceMode::AttackOptimalRange)
            {
                if (const std::optional<MCInterfaceCursor> range = RangeCursor(CurrentMode); range.has_value())
                {
                    armedCursor(*range);
                    return;
                }
            }

            if (IsAimedShot(CurrentMode))
            {
                aimedCursor();
                return;
            }

            switch (CurrentMode)
            {
                case MCInterfaceMode::Run:
                {
                    runCursor();
                    return;
                }
                case MCInterfaceMode::Jump:
                {
                    if (object != nullptr)
                    {
                        // Jumping onto a mover attacks it (the attack cursor); onto anything else, to its place.
                        const MCVector3D point = object->GetPosition();

                        if (IsMoverClass(object->ObjectClass))
                        {
                            SetCursor(CanSelectionJumpTo(point, nullptr, ForcingOrder) ? MCInterfaceCursor::Attack
                                                                                       : MCInterfaceCursor::Forbidden);
                            return;
                        }

                        jumpCursor(point);
                        return;
                    }

                    if (MCVector3D point; mousePoint(point))
                    {
                        jumpCursor(point);
                    }

                    return;
                }
                case MCInterfaceMode::Guard:
                {
                    SetCursor(MCInterfaceCursor::Guard);
                    return;
                }
                default:
                {
                    if (CanCapture)
                    {
                        SetCursor(CaptureBlocked ? MCInterfaceCursor::CaptureBlocked : MCInterfaceCursor::Capture);
                    }
                    else
                    {
                        armedCursor(MCInterfaceCursor::Attack);
                    }

                    return;
                }
            }
        }
        case MCMouseTarget::DisabledEnemy:
        {
            if (const std::optional<MCInterfaceCursor> range = RangeCursor(CurrentMode); range.has_value())
            {
                armedCursor(*range);
                return;
            }

            if (IsAimedShot(CurrentMode))
            {
                aimedCursor();
                return;
            }

            if (CurrentMode == MCInterfaceMode::Run)
            {
                runCursor();
                return;
            }

            SetCursor(MCInterfaceCursor::Forbidden);
            return;
        }
        case MCMouseTarget::Ally:
        {
            PlainCursor(window, mousePos);
            return;
        }
        case MCMouseTarget::SensorContact:
        {
            // A contact seen on sensors only: a move if the cell is passable.
            if (MCVector3D point; mousePoint(point))
            {
                if (GameMap().CellPassable(point) != 0)
                {
                    SetCursorOffset(mousePos);
                    SetCursor(CurrentMode == MCInterfaceMode::Run ? MCInterfaceCursor::Run : MCInterfaceCursor::Move);
                }
                else
                {
                    SetCursor(MCInterfaceCursor::Forbidden);
                }
            }

            return;
        }
        case MCMouseTarget::Object:
        case MCMouseTarget::Nothing:
        {
            if (CurrentMode == MCInterfaceMode::Repair)
            {
                SetCursor(MCInterfaceCursor::Refit);
                return;
            }

            if (MouseTarget != MCMouseTarget::Object)
            {
                PlainCursor(window, mousePos);
                return;
            }

            if (object->IsRevealed() == 0)
            {
                SetCursorOffset(mousePos);
                SetCursor(CurrentMode == MCInterfaceMode::Run ? MCInterfaceCursor::Run : MCInterfaceCursor::Move);
                return;
            }

            MCInterfaceCursor cursor = MCInterfaceCursor::Forbidden;

            switch (CurrentMode)
            {
                case MCInterfaceMode::Run:
                {
                    SetCursorOffset(mousePos);
                    cursor = MCInterfaceCursor::Run;
                    break;
                }
                case MCInterfaceMode::AttackLongRange:
                case MCInterfaceMode::AttackMediumRange:
                case MCInterfaceMode::AttackShortRange:
                case MCInterfaceMode::AttackFromPosition:
                {
                    cursor = AnySelected(true) ? *RangeCursor(CurrentMode) : MCInterfaceCursor::Forbidden;
                    break;
                }
                case MCInterfaceMode::Jump:
                {
                    if (CanSelectionJumpTo(object->GetPosition(), nullptr, ForcingOrder))
                    {
                        SetCursorOffset(mousePos);
                        cursor = MCInterfaceCursor::Jump;
                    }
                    break;
                }
                case MCInterfaceMode::Guard:
                    cursor = MCInterfaceCursor::Guard;
                    break;
                default:
                {
                    // Walking onto it: the move cursor for a wreck, a bridge or the player's own; else the attack
                    // cursor.
                    if (static_cast<uint8_t>(object->Status) == 2 || static_cast<uint8_t>(object->Status) == 1 ||
                        (object->ObjectClass == MCObjectClass::MiscTerrainObject &&
                         static_cast<MCMiscTerrainObject&>(*object).Kind == MCMiscTerrainKind::Bridge) ||
                        object->GetAlignment() == HomeTeam()->Alignment)
                    {
                        SetCursorOffset(mousePos);
                        cursor = MCInterfaceCursor::Move;
                    }
                    else
                    {
                        cursor = MCInterfaceCursor::Attack;
                    }
                    break;
                }
            }

            SetCursor(cursor);

            if (CanCapture)
            {
                SetCursor(CaptureBlocked ? MCInterfaceCursor::CaptureBlocked : MCInterfaceCursor::Capture);
            }

            return;
        }

        default:
            return;
    }
}

auto MCTacticalInterface::PlainCursor(MCGuiObject& window, MCVector2D mousePos) -> void
{
    // Over an ally, or nothing: the plain mode cursors.
    if (const std::optional<MCInterfaceCursor> range = RangeCursor(CurrentMode); range.has_value())
    {
        SetCursor(AnySelected(true) ? *range : MCInterfaceCursor::Forbidden);
        return;
    }

    if (IsAimedShot(CurrentMode))
    {
        if (!AnySelected(true))
        {
            SetCursor(MCInterfaceCursor::Forbidden);
        }
        else if (const std::shared_ptr<MCBaseObject> object = LockObject(MouseObject);
                 object != nullptr && object->ObjectClass == MCObjectClass::BattleMech)
        {
            SetCursor(MCInterfaceCursor::FromPosition);
        }
        else
        {
            SetCursor(MCInterfaceCursor::Attack);
        }

        return;
    }

    auto mousePoint = [&](MCVector3D& point)
    {
        const std::shared_ptr<MCCamera> camera = window.GetCamera();

        if (camera == nullptr)
        {
            return false;
        }

        camera->InverseProject(mousePos, point);
        return true;
    };

    switch (CurrentMode)
    {
        case MCInterfaceMode::Jump:
        {
            // Port fix: the original projects through the window's camera without checking there is one.
            if (MCVector3D point; mousePoint(point))
            {
                if (CanSelectionJumpTo(point, nullptr, ForcingOrder))
                {
                    SetCursorOffset(mousePos);
                    SetCursor(MCInterfaceCursor::Jump);
                }
                else
                {
                    SetCursor(MCInterfaceCursor::Forbidden);
                }
            }

            return;
        }

        case MCInterfaceMode::Guard:
        {
            SetCursor(MCInterfaceCursor::Guard);
            return;
        }
        case MCInterfaceMode::Info:
        {
            SetCursor(MouseTarget == MCMouseTarget::Ally ? MCInterfaceCursor::Info : MCInterfaceCursor::Move);
            return;
        }
        default:
        {
            if (!AnySelected())
            {
                return;
            }

            if (CanCapture)
            {
                SetCursor(CaptureBlocked ? MCInterfaceCursor::CaptureBlocked : MCInterfaceCursor::Capture);
                return;
            }

            MCVector3D point;

            if (!mousePoint(point))
            {
                return;
            }

            if (GameMap().CellPassable(point) != 0)
            {
                SetCursorOffset(mousePos);
                SetCursor(CurrentMode == MCInterfaceMode::Run ? MCInterfaceCursor::Run : MCInterfaceCursor::Move);
            }
            else
            {
                SetCursor(MCInterfaceCursor::Forbidden);
            }

            return;
        }
    }
}

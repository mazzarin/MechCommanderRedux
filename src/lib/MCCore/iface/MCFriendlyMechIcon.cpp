#include "stdafx.h"
#include "iface/MCFriendlyMechIcon.h"
#include "ai/MCTacticalOrder.h"
#include "camera/MCCamera.h"
#include "camera/MCMainWindow.h"
#include "gui/MCGuiFont.h"
#include "gui/MCFloatHelp.h"
#include "iface/MCCommandParser.h"
#include "iface/MCMechBar.h"
#include "iface/MCOrderSink.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "main/MCGamePaths.h"
#include "main/MCMissionGlobals.h"
#include "network/MCMultiPlayer.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"
#include "object/MCObjectType.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTacticalMap.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>Sets the cursor to <paramref name="cursor"/>.</summary>
    void SetCursor(MCInterfaceCursor cursor)
    {
        GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(cursor));
    }

    /// <summary>Points the main window's active camera at the mover and shows the floating tags.</summary>
    void CenterCameraOn(std::shared_ptr<MCBaseObject> mover)
    {
        // Port fix: with no active pane the original used the icon itself as the camera, calling its resize
        // (slot 0x20) with the mover pointer.
        std::shared_ptr<MCCamera> camera;

        if (MainHolder()->GetActivePane() != nullptr)
        {
            camera = MainHolder()->GetActivePane()->GetCamera();
        }

        if (camera == nullptr)
        {
            return;
        }

        camera->ChangeTarget(std::move(mover), false);
        TacticalInterface()->FloatingTags[0]->ShowGuiWindow(true);
    }

    /// <summary>
    /// A single-mover order: sent to the server in a multiplayer client, otherwise given to the mover straight away.
    /// </summary>
    void GiveMoverOrder(MCTacticalOrder& order, MCMover& mover)
    {
        MCOrderSink& sink = TacticalInterface()->Orders();

        if (MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0)
        {
            int32_t partId = mover.PartId;
            sink.SendToServer(order, false, std::span(&partId, 1), {}, false);
        }
        else
        {
            sink.Give(mover, order);
        }
    }
}

auto MCFriendlyMechIcon::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view bitmapName)
    -> int32_t
{
    ShowingWoundedPilot = false;
    ShowingDeadPilot = false;
    const int32_t result = MCMechIcon::Init(xPos, yPos, width, height, bitmapName);

    if (result != 0)
    {
        return result;
    }

    PilotImage = std::make_unique<MCGuiPort>();
    DiagramX = 6;
    DiagramY = 0x10;
    Lance = NoLance;
    IsPoint = false;
    return 0;
}

auto MCFriendlyMechIcon::OnDestroy() -> void
{
    PilotImage.reset();
    IconBackground.reset();
    MCMechIcon::OnDestroy();
}

auto MCFriendlyMechIcon::Enter() -> void
{
    std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
    MCFloatHelp& tag = *iface->FloatingTags[0];
    const auto bar = std::static_pointer_cast<MCMechBar>(ParentObject());
    const auto shown = std::static_pointer_cast<MCGameObject>(LockObject(Mover));
    bar->HighlightId = PartId;
    bar->Draw();

    // The pilot's tag, shown when the mover was seen this turn.
    if (shown != nullptr && Active && shown->GetPilot() != nullptr)
    {
        MCTacticalInterface::TagMover(tag, *shown);

        if (shown->GetWindowsVisible() == Turn)
        {
            tag.ShowGuiWindow(true);
        }
    }

    // The cursor for the current mode; an attack mode with no armed mover selected leaves the cursor as it is.
    MCInterfaceCursor cursor = MCInterfaceCursor::Normal;
    const MCInterfaceMode mode = iface->CurrentMode;

    if (iface->AnySelected())
    {
        auto attackCursor = [&](MCInterfaceCursor attack)
        {
            if (!iface->AnySelected(true))
            {
                return false;
            }

            cursor = attack;
            return true;
        };

        bool setCursor = true;

        switch (mode)
        {
            case MCInterfaceMode::AttackOptimalRange:
            case MCInterfaceMode::AttackConservingAmmo:
            {
                setCursor = attackCursor(MCInterfaceCursor::OptimalRange);
                break;
            }
            case MCInterfaceMode::AttackLongRange:
            {
                setCursor = attackCursor(MCInterfaceCursor::LongRange);
                break;
            }
            case MCInterfaceMode::AttackMediumRange:
            {
                setCursor = attackCursor(MCInterfaceCursor::MediumRange);
                break;
            }
            case MCInterfaceMode::AttackShortRange:
            {
                setCursor = attackCursor(MCInterfaceCursor::ShortRange);
                break;
            }
            case MCInterfaceMode::Info:
            {
                cursor = MCInterfaceCursor::Info;
                break;
            }
            default:
            {
                if (mode == MCInterfaceMode::AttackFromPosition || IsAimedShot(mode))
                {
                    setCursor = attackCursor(MCInterfaceCursor::FromPosition);
                    break;
                }

                // One selected refit vehicle over a mover needing a refit: the refit mode.
                if (iface->SelectedMovers.size() == 1 && static_cast<MCMover&>(*shown).NeedsRefit(0) != 0)
                {
                    const auto selected = std::static_pointer_cast<MCGameObject>(
                        ObjectList()->FindObjectFromPart(iface->SelectedMovers[0]));

                    if (selected != nullptr && selected->ObjectClass == MCObjectClass::GroundVehicle &&
                        selected->GetRefitPoints() > 0.0f)
                    {
                        SetCursor(MCInterfaceCursor::Refit);
                        iface->SetMode(MCInterfaceMode::Refit);
                        setCursor = false;
                    }
                }
                break;
            }
        }

        if (!setCursor)
        {
            MCGuiObject::Enter();
            return;
        }
    }

    SetCursor(cursor);
    MCGuiObject::Enter();
}

auto MCFriendlyMechIcon::Draw() -> void
{
    DrawIcon(*DisplayPort);
}

auto MCFriendlyMechIcon::UpdateModel() -> void
{
    MCMechIcon::UpdateModel();
    const auto shown = std::static_pointer_cast<MCGameObject>(LockObject(Mover));

    if (shown == nullptr || !Active)
    {
        return;
    }

    std::shared_ptr<MCMechWarrior> pilot = shown->GetPilot();

    if (pilot == nullptr)
    {
        return;
    }

    // Wounded (6 or more wounds), then dead or gone: the portrait changes once. (The original did this as it drew
    // the pilot.)
    if (6.0f <= pilot->Wounds && !ShowingWoundedPilot)
    {
        PilotImage->Init(3);
        ShowingWoundedPilot = true;
    }

    const int32_t status = pilot->Status;

    if ((status == 3 || status == 5 || status == 6) && !ShowingDeadPilot)
    {
        PilotImage->Init(4);
        ShowingDeadPilot = true;
    }
}

auto MCFriendlyMechIcon::DrawIcon(MCGuiPort& target) -> void
{
    // The icon's picture held its background, drawn over each time.
    if (IconBackground != nullptr)
    {
        IconBackground->CopyTo(target.Frame(), 0, 0, false);
    }

    const auto shown = std::static_pointer_cast<MCMover>(LockObject(Mover));
    DrawWeapon(target);
    MCMechIcon::DrawIcon(target);
    FillPortBox(target, 2, 2, 0x31, 9, LanceColors[Lance]);

    // A vehicle with a name shows it instead of its pilot.
    if (shown->ObjectClass == MCObjectClass::GroundVehicle)
    {
        WhiteFont->WriteString(target.Frame(), 5, 3, shown->GetIfaceName(), -1);
        return;
    }

    DrawPilot(target);
}

auto MCFriendlyMechIcon::Display() -> void
{
    if (Active)
    {
        MCMechIcon::Display();
    }
}

auto MCFriendlyMechIcon::DrawBox(uint8_t color, int32_t left, int32_t top, int32_t right, int32_t bottom) -> void
{
    if (TacticalInterface()->MechBar->Dancing)
    {
        return;
    }

    if (left == -1)
    {
        left = 0;
    }

    if (top == -1)
    {
        top = 0;
    }

    if (right == -1)
    {
        right = Width() - 1;
    }

    if (bottom == -1)
    {
        bottom = Height() - 1;
    }

    VfxLineDraw(Frame(), left, top, right, top, color);
    VfxLineDraw(Frame(), left, top, left, bottom, color);
    VfxLineDraw(Frame(), left, bottom, right, bottom, color);
    VfxLineDraw(Frame(), right, top, right, bottom, color);
}

auto MCFriendlyMechIcon::DrawPilot(MCGuiPort& target) -> void
{
    const auto shown = std::static_pointer_cast<MCGameObject>(LockObject(Mover));

    if (shown == nullptr || !Active)
    {
        return;
    }

    std::shared_ptr<MCMechWarrior> pilot = shown->GetPilot();

    if (pilot == nullptr)
    {
        return;
    }

    // The health bar loses 3 pixels per wound from its right end.
    if (0.0f < pilot->Wounds)
    {
        const auto left = static_cast<int16_t>(47.0f - pilot->Wounds * 3.0f);
        FillPortBox(target, left, 0xb, 0x30, 0xd, 0x10);
    }

    VfxPaneCopy(PilotImage->Frame(), 0, 0, target.Frame(), 0x1c, 0xe, 0xfff);

    if (shown->ObjectClass == MCObjectClass::BattleMech && !pilot->Callsign.empty())
    {
        WhiteFont->WriteString(target.Frame(), 5, 3, pilot->Callsign.data(), -1);
    }
}

auto MCFriendlyMechIcon::DrawWeapon(MCGuiPort& target) -> void
{
    // The bar runs from x 2 on a mech (beside the portrait), from 0xd otherwise.
    int32_t start = 2;
    const auto shown = std::static_pointer_cast<MCMover>(LockObject(Mover));

    if (shown == nullptr || shown->ObjectClass != MCObjectClass::BattleMech)
    {
        FillPortBox(target, 2, 0xb, 0x2e, 0xc, 0x10);
        start = 0xd;
    }
    else
    {
        FillPortBox(target, 2, 0xb, 0x1b, 0xc, 0x10);
    }

    if (shown == nullptr)
    {
        return;
    }

    const float effectiveness = shown->GetTotalEffectiveness();
    int32_t length = static_cast<int16_t>(std::floor(static_cast<double>(effectiveness) * 25.0));

    if (length == 0 && 0.001 < static_cast<double>(effectiveness))
    {
        length = 3;
    }

    uint32_t color;

    if (effectiveness < 0.5f)
    {
        color = effectiveness <= 0.2f ? 0xef : 0xf2;
    }
    else
    {
        color = 0xe;
    }

    if (length == 0)
    {
        return;
    }

    // A two-pixel bar, lit on its top and left, shaded (colour - 1) on its bottom and right.
    const int32_t end = length + start;
    VfxLineDraw(target.Frame(), start, 0xb, start, 0xc, color);
    VfxLineDraw(target.Frame(), start, 0xb, end, 0xb, color);
    VfxLineDraw(target.Frame(), start + 1, 0xc, end, 0xc, color - 1);
    VfxLineDraw(target.Frame(), end, 0xb, end, 0xc, color - 1);
}

auto MCFriendlyMechIcon::SetID(int32_t newPartId) -> void
{
    std::shared_ptr<MCBaseObject> object = ObjectList()->FindObjectFromPart(newPartId);

    if (object == nullptr)
    {
        return;
    }

    const MCObjectClass objectClass = object->ObjectClass;

    if (objectClass != MCObjectClass::BattleMech && objectClass != MCObjectClass::GroundVehicle &&
        objectClass != MCObjectClass::Elemental && objectClass != MCObjectClass::Mover)
    {
        return;
    }

    std::string shapeName;

    if (objectClass == MCObjectClass::BattleMech)
    {
        DiagramX = 2;
        DiagramY = 0xe;
        shapeName = std::format("mi{:02}", object->GetObjectType()->IconNumber);
    }
    else
    {
        DiagramX = 0xe;
        DiagramY = 0xe;
        shapeName = std::format("vi{}", object->GetObjectType()->IconNumber);
    }

    // Port: the original loaded the background into the icon's own picture; it is kept apart, and the icon's view
    // takes its size.
    if (IconBackground == nullptr)
    {
        IconBackground = std::make_unique<MCGuiPort>();
    }

    IconBackground->Init(const_cast<char*>("guiub00.tga"));
    Port().InitView(IconBackground->Width(), IconBackground->Height());

    MCFile shapeFile;

    if (shapeFile.Open(GamePath(ArtPath, shapeName, ".shp")) != 0)
    {
        Fatal(0, "Unable to open damage display shape file");
    }

    DamageShapes = {};

    // An empty file still fails, as it did when the GUI heap's malloc(0) returned null.
    if (shapeFile.GetLength() == 0)
    {
        shapeFile.Close();
        Fatal(0, "Not enough memory for damage display shape file");
    }

    DamageShapes = MCRegisteredBlock(shapeFile.GetLength(), MCDataKind::Shapes);
    shapeFile.Read(DamageShapes.Bytes());
    shapeFile.Close();
    DamageShapeName = shapeName;
    std::ranges::transform(DamageShapeName, DamageShapeName.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    AttachHdArt();

    auto shown = std::static_pointer_cast<MCMover>(object);
    NumParts = shown->NumBodyLocations();
    Mover = object;
    PartId = newPartId;

    if (shown->GetPilot() != nullptr && !shown->GetPilot()->Picture.empty())
    {
        PilotImage->Init(shown->GetPilot()->Picture.data());
    }

    IsPoint = shown == shown->GetPoint();
    // The original drew the icon here; it draws itself each frame.
    UpdateModel();
}

auto MechIconHandleEvent(MCGuiObject& icon, MCGuiEvent& event) -> void
{
    std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
    auto& mechIcon = static_cast<MCMechIcon&>(icon);
    const auto mover = std::static_pointer_cast<MCMover>(LockObject(mechIcon.Mover));
    MCTacticalOrder order;
    order.Reset();

    if (event.Type == 1)
    {
        GuiSystem().Grab(icon.weak_from_this());
        return;
    }

    if (event.Type == 6)
    {
        CenterCameraOn(mover);
        return;
    }

    if (event.Type != 4 || GuiSystem().GrabbedObject().get() != &icon || mover == nullptr)
    {
        return;
    }

    GuiSystem().Release();

    // An attack mode (or aimed shot, which only a mech can take) makes the selection attack this mover.
    MCInterfaceMode mode = iface->CurrentMode;

    if ((IsAttackMode(mode) || IsAimedShot(mode)) && iface->AnySelected(true))
    {
        if (mover->ObjectClass != MCObjectClass::BattleMech && IsAimedShot(mode))
        {
            mode = MCInterfaceMode::AttackOptimalRange;
        }

        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::AttackObject, 0);
        order.Target = mover != nullptr ? mover->SharedAs<MCGameObject>() : nullptr;
        order.AttackParams.Type = 1;
        order.AttackParams.Method = 0;
        order.AttackParams.Range = -4;
        order.AttackParams.Pursue = -1;

        // Only the range modes and from-position change the order here (no ammunition conserving, no aimed shot).
        switch (mode)
        {
            case MCInterfaceMode::AttackOptimalRange:
                order.AttackParams.Range = -1;
                break;
            case MCInterfaceMode::AttackLongRange:
                order.AttackParams.Range = 2;
                break;
            case MCInterfaceMode::AttackMediumRange:
                order.AttackParams.Range = 1;
                break;
            case MCInterfaceMode::AttackShortRange:
                order.AttackParams.Range = 0;
                break;
            case MCInterfaceMode::AttackFromPosition:
                order.AttackParams.Pursue = 0;
                break;
            default:
                break;
        }

        iface->CommandParser->SendTacOrder(order, true);
        iface->UpdateInterface();
        return;
    }

    switch (iface->CurrentMode)
    {
        case MCInterfaceMode::None:
        {
            // Select the mover; shift adds it to (or takes it out of) the selection.
            const int32_t iconPartId = mechIcon.PartId;

            if (event.ShiftKey == 0)
            {
                iface->DeselectEnemy();
                iface->ClearMechSelection();
                iface->CommandParser->ClearSubjects();

                if (!iface->AnySelected())
                {
                    TacticalMap()->SetID(iconPartId);
                }

                iface->SelectMech(iconPartId);
            }
            else
            {
                if (iface->IsSelected(iconPartId))
                {
                    iface->DeselectMech(iconPartId);
                    iface->CommandParser->RemoveSubject(iconPartId);
                    iface->UpdateInterface();
                    return;
                }

                iface->SelectMech(iconPartId);
                iface->DeselectEnemy();
            }

            iface->CommandParser->AddSubject(iconPartId);
            SoundSystem()->PlayInterfaceSample(0x10);
            break;
        }

        case MCInterfaceMode::Eject:
        {
            order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::Eject, 0);
            GiveMoverOrder(order, *mover);
            iface->UpdateInterface();
            return;
        }
        case MCInterfaceMode::Stop:
        {
            order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::HoldFire, 0);
            GiveMoverOrder(order, *mover);
            iface->UpdateInterface();
            return;
        }
        case MCInterfaceMode::Refit:
        {
            order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::Refit, 0);
            order.Target = mover != nullptr ? mover->SharedAs<MCGameObject>() : nullptr;
            iface->CommandParser->SendTacOrder(order, false);
            iface->UpdateInterface();
            return;
        }
        case MCInterfaceMode::Guard:
        {
            order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::Guard, 0);
            order.Target = mover != nullptr ? mover->SharedAs<MCGameObject>() : nullptr;
            order.MoveParams.WayPath.Mode[0] = 0;
            order.MoveParams.Wait = -1;
            iface->CommandParser->SendTacOrder(order, true);
            iface->UpdateInterface();
            return;
        }
        case MCInterfaceMode::PowerUp:
        {
            order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::PowerUp, 0);
            GiveMoverOrder(order, *mover);
            iface->UpdateInterface();
            return;
        }
        case MCInterfaceMode::PowerDown:
        {
            order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::PowerDown, 0);
            GiveMoverOrder(order, *mover);
            iface->UpdateInterface();
            return;
        }
        case MCInterfaceMode::LinkLance1:
        case MCInterfaceMode::LinkLance2:
        case MCInterfaceMode::LinkLance3:
        case MCInterfaceMode::LinkLance4:
        {
            // Make the selection the lance, with this mover as its point.
            SoundSystem()->PlayBettySample(5);
            const int32_t groupId = LinkedLance(iface->CurrentMode);

            if (!iface->IsSelected(mover->PartId))
            {
                iface->SelectMech(mover->PartId);
            }

            std::vector<std::shared_ptr<MCMover>> movers(iface->SelectedMovers.size());
            // Port fix: the original leaves the point index unset when no selected button holds this mover.
            int32_t pointIndex = 0;

            for (size_t i = 0; i < iface->SelectedMovers.size(); i++)
            {
                std::shared_ptr<MCFriendlyMechIcon> button = iface->MechBar->GetButtonFromID(iface->SelectedMovers[i]);

                if (button == nullptr)
                {
                    continue;
                }

                movers[i] = std::static_pointer_cast<MCMover>(LockObject(button->Mover));
                button->IsPoint = movers[i] == mover;

                if (button->IsPoint)
                {
                    pointIndex = static_cast<int32_t>(i);
                }
            }

            iface->SetUnit(groupId, movers, pointIndex);
            iface->MechBar->PlaceButtons(true);
            iface->SetMode(MCInterfaceMode::None);
            GuiSystem().CursorHidden = false;
            icon.Enter();
            iface->SelectLance(HomeCommander()->GetGroup(groupId));
            iface->CommandParser->AddSubject(HomeCommander()->GetGroup(groupId));
            iface->UpdateInterface();
            return;
        }

        case MCInterfaceMode::Info:
        {
            TacticalMap()->HideMe(false);
            TacticalMap()->SetDisplayType(MCTacmapPage::Info);
            TacticalMap()->SetID(mechIcon.PartId);
            iface->UpdateInterface();
            return;
        }
        case MCInterfaceMode::CameraFollow:
            CenterCameraOn(mover);
            break;
        default:
            break;
    }

    iface->UpdateInterface();
}

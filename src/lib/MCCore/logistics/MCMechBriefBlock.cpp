#include "stdafx.h"
#include "logistics/MCMechBriefBlock.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCScrollPane.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCDragIcon.h"
#include "logistics/MCLogRows.h"
#include "logistics/MCTicker.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "network/MCMultiPlayer.h"
#include "object/MCMasterComponent.h"
#include "platform/MCInput.h"
#include "sound/MCSoundSystem.h"
#include "vfx/MCVfxFunctions.h"
#include "lib/MCFatal.h"

namespace
{
    /// <summary>The drag of a unit block (one at a time).</summary>
    struct MCBriefDrag
    {
        /// <summary>Set while a block is dragged with the left button.</summary>
        bool Left = false;
        /// <summary>Set while a block is dragged with the right button.</summary>
        bool Right = false;
        /// <summary>The drag icon's position.</summary>
        int32_t X = 0;
        int32_t Y = 0;
        /// <summary>Set when the block was picked up from a drop slot, not the deploy pane.</summary>
        bool FromSlot = false;
    };

    /// <summary>The drag going on, if any.</summary>
    MCBriefDrag Drag;

    /// <summary>A block's size (a drop slot's).</summary>
    constexpr int32_t BlockWidth = 0x34;
    constexpr int32_t BlockHeight = 0x2e;

    /// <summary>The sounds of the drop slots: a unit placed (or back), refused, picked up.</summary>
    constexpr uint32_t PlacedSound = 0x34;
    constexpr uint32_t RefusedSound = 0x33;
    constexpr uint32_t PickedUpSound = 0x35;

    /// <summary>Puts string <paramref name="id"/> on the ticker.</summary>
    void ShowHelp(uint32_t id)
    {
        GlobalLog()->Ticker->SetString(LoadGameString(id, 0xfe));
    }

    /// <summary>The unit's tonnage added to (or, with <paramref name="sign"/> -1, taken from) <c>curDeployTonnage</c>.</summary>
    int32_t DeployTonnageWith(MCLogPart& part, float sign)
    {
        return static_cast<int32_t>(static_cast<float>(CurDeployTonnage) + sign * part.CurTonnage);
    }

    /// <summary>Fills a <paramref name="width"/> by <paramref name="height"/> box of <paramref name="target"/> with <paramref name="color"/>.</summary>
    void FillArea(const MCPane& target, int32_t xPos, int32_t yPos, int32_t width, int32_t height, int32_t color)
    {
        MCLogBlockPort box(target, xPos, yPos, width, height, false);
        VfxPaneWipe(box.Frame(), color);
    }

    /// <summary>Whether drop slot <paramref name="slot"/> of <paramref name="lance"/> is one the player can fill.</summary>
    bool LocalSlot(int32_t lance, int32_t slot)
    {
        return GlobalLog()->LocalDropSlot[lance * 4 + slot] != 0;
    }

    /// <summary>Drop slot <paramref name="slot"/> of <paramref name="lance"/>'s place on the screen.</summary>
    const RECT& SlotRect(int32_t lance, int32_t slot)
    {
        return GlobalLog()->BriefingScreen->SlotRects[static_cast<size_t>(lance * 4 + slot)];
    }

    /// <summary>The unit is too heavy for the slot: says so.</summary>
    void TooHeavy()
    {
        ShowLogMessage(0x4cu);
        SoundSystem()->PlayBettySample(0);
        GlobalLog()->BriefingScreen->DrawTonnageBar();
    }

    /// <summary>
    /// Why <paramref name="mech"/> can't drop: string 0x35d for a damaged engine, 0x364 for a destroyed location; 0
    /// when it can.
    /// </summary>
    uint32_t DropRefusal(MCLogMech& mech)
    {
        const auto& items = mech.Inventory->Items;
        const auto engine =
            std::ranges::find_if(items, [](const std::shared_ptr<MCLogInventoryItem>& item)
                                 { return MasterComponentList[item->MasterID].Form == MCComponentForm::Engine; });

        // Port fix: a mech without an engine item (the original dereferenced null) counts as undamaged.
        if (engine != items.end() && !(*engine)->Stats.empty() && (*engine)->Stats.front()->Hits != 0)
        {
            return 0x35d;
        }

        for (const auto& location : mech.Internals)
        {
            if (location.CurArmor == 0)
            {
                return 0x364;
            }
        }

        return 0;
    }
}

auto MCMechBriefBlock::Init(const std::shared_ptr<MCLogPart>& part, int32_t xPos, int32_t yPos) -> void
{
    if (part->PartType == 1)
    {
        _Mech = std::static_pointer_cast<MCLogMech>(part);
    }
    else
    {
        _Vehicle = std::static_pointer_cast<MCLogVehicle>(part);
    }

    MCLogObject::InitWithoutPort(xPos, yPos, BlockWidth, BlockHeight);
}

auto MCMechBriefBlock::Part() const -> std::shared_ptr<MCLogPart>
{
    if (const std::shared_ptr<MCLogMech> mech = Mech())
    {
        return mech;
    }

    return Vehicle();
}

auto MCMechBriefBlock::DrawRow(MCLogPort& port, int32_t top) -> void
{
    PaintBlock(port.Frame(), X(), top, false);
}

auto MCMechBriefBlock::OnDestroy() -> void
{
    _Mech.reset();
    _Vehicle.reset();
    MCLogObject::OnDestroy();
}

auto MCMechBriefBlock::HandleEvent(MCGuiEvent& event) -> void
{
    if (!Drag.Left && !Drag.Right)
    {
        if (event.X < GlobalX() || event.X > GlobalX() + 1 + Width())
        {
            return;
        }

        if (event.Y < GlobalY() || event.Y > GlobalY() + 1 + Height())
        {
            return;
        }
    }

    // The parent is always a logistics object (the briefing screen or the deploy pane).
    const auto owner = std::static_pointer_cast<MCLogObject>(ParentObject());

    if (owner != nullptr && !Drag.Left && (event.Type == 8 || event.Type == 9))
    {
        owner->HandleEvent(event);
        return;
    }

    switch (event.Type)
    {
        case 1:
        {
            if (!Drag.Right)
            {
                PickUp(event);
            }

            return;
        }
        case 3:
        {
            if (!Drag.Left)
            {
                Drag.Right = true;
                PickUp(event);
            }

            return;
        }
        case 7:
        {
            if (Drag.Left)
            {
                Drag.Y = event.Y - 0x17;
                Drag.X = event.X - 0x1a;
                MCDragIcon::Current()->MoveTo(Drag.X, Drag.Y, false);
                return;
            }

            if (GuiSystem().GrabbedObject() == nullptr && event.Key == 0)
            {
                ShowHelp(GlobalX() < 0xd1 ? 0x22 : 0x21);
            }

            return;
        }
        case 4:
        case 6:
        {
            // A release ends the drag of its own button.
            if ((event.Type == 6 && Drag.Left) || (event.Type == 4 && Drag.Right))
            {
                return;
            }

            if (GuiSystem().GrabbedObject() == nullptr)
            {
                Drag.Right = false;
                return;
            }

            Drop(event);
            return;
        }

        default:
            return;
    }
}

auto MCMechBriefBlock::PlaceInEmptySlot(int32_t lance, int32_t slot) const -> bool
{
    const std::shared_ptr<MCLogVehicle> vehicle = Vehicle();
    const std::shared_ptr<MCLogMech> mech = Mech();
    auto& deploy = GlobalLog()->DeploySlots[lance][slot];

    if (mech != nullptr)
    {
        if (!MCBriefingScreen::FitsTonnage(*mech) && GlobalLog()->HammerDown == 0)
        {
            return false;
        }

        GlobalLog()->SendAddMechMessage(*mech, lance, slot);
        SoundSystem()->PlayInterfaceSample(PlacedSound);
        deploy.Unit = GlobalLog()->ForceMechList->GetMechIndex(mech);
        CurDeployTonnage = DeployTonnageWith(*mech, 1.0f);
        mech->Deployed = true;
        GlobalLog()->AssignedWarriorList->SetDeployed(mech->PilotIndex, true);
        return true;
    }

    if (!MCBriefingScreen::FitsTonnage(*vehicle) && GlobalLog()->HammerDown == 0)
    {
        return false;
    }

    GlobalLog()->SendAddVehicleMessage(*vehicle, lance, slot);
    SoundSystem()->PlayInterfaceSample(PlacedSound);
    deploy.Vehicle = GlobalLog()->ForceVehicleList->GetVehicleIndex(vehicle);
    CurDeployTonnage = DeployTonnageWith(*vehicle, 1.0f);
    vehicle->Deployed = true;
    return true;
}

auto MCMechBriefBlock::Settle() -> void
{
    // The original moved the block onto the screen at the slot's corner; the screen makes the slot's block. The block
    // lifted out of a slot goes (the drop pinned it).
    MCBriefingScreen& screen = *GlobalLog()->BriefingScreen;
    screen.ShowSlots();
    screen.CalcTonnages();
}

auto MCMechBriefBlock::DropInFreeSlot() -> bool
{
    // Lance 1, 2 or 3 with its key held; else any.
    int32_t lance = -1;

    if ((MCInput::GetAsyncKeyState('1') & 0x8000) != 0)
    {
        lance = 0;
    }
    else if ((MCInput::GetAsyncKeyState('2') & 0x8000) != 0)
    {
        lance = 1;
    }
    else if ((MCInput::GetAsyncKeyState('3') & 0x8000) != 0)
    {
        lance = 2;
    }

    const int32_t from = lance < 0 ? 0 : lance;
    const int32_t to = lance < 0 ? 3 : lance + 1;

    for (int32_t l = from; l < to; l++)
    {
        for (int32_t s = 0; s < 4; s++)
        {
            if (!LocalSlot(l, s))
            {
                // Any lance: the slots the player can fill may have gaps; the lance asked for ends at its first.
                if (lance < 0)
                {
                    continue;
                }
                break;
            }

            const auto& deploy = GlobalLog()->DeploySlots[l][s];

            if (deploy.Unit != -1 || deploy.Vehicle != -1)
            {
                continue;
            }

            if (!PlaceInEmptySlot(l, s))
            {
                TooHeavy();
                return false;
            }

            Settle();
            return true;
        }
    }

    // Port fix (OB-083): with no key held and no free slot the original went on to search a fourth lance past the end
    // of the slot tables (and, after a refusal, the later lances' slots against an uninitialised point).
    return false;
}

auto MCMechBriefBlock::DropInSlotAt(POINT point, int32_t firstLance) -> bool
{
    for (int32_t lance = firstLance; lance < 3; lance++)
    {
        for (int32_t slot = 0; slot < 4; slot++)
        {
            if (!LocalSlot(lance, slot) || PtInRect(SlotRect(lance, slot), point) == 0)
            {
                continue;
            }

            auto& deploy = GlobalLog()->DeploySlots[lance][slot];
            bool fits;

            if (deploy.Unit > -1)
            {
                // A mech was there: it leaves the force's drop (its block goes with the slot's next showing). (Original
                // behaviour (OB-166): its pilot stays marked deployed, and a mech put in its place isn't marked.)
                std::shared_ptr<MCLogMech> occupant;
                GlobalLog()->ForceMechList->GetMechInfo(deploy.Unit, occupant);
                CurDeployTonnage = DeployTonnageWith(*occupant, -1.0f);
                occupant->Deployed = false;
                deploy.Unit = -1;

                if (Mech() != nullptr)
                {
                    fits = MCBriefingScreen::FitsTonnage(*Mech());

                    if (fits)
                    {
                        GlobalLog()->SendAddMechMessage(*Mech(), lance, slot);
                        SoundSystem()->PlayInterfaceSample(PlacedSound);
                        deploy.Unit = GlobalLog()->ForceMechList->GetMechIndex(Mech());
                        CurDeployTonnage = DeployTonnageWith(*Mech(), 1.0f);
                        Mech()->Deployed = true;
                    }
                }
                else
                {
                    fits = MCBriefingScreen::FitsTonnage(*Vehicle());

                    if (fits)
                    {
                        GlobalLog()->SendAddVehicleMessage(*Vehicle(), lance, slot);
                        SoundSystem()->PlayInterfaceSample(PlacedSound);
                        deploy.Unit = -1;
                        deploy.Vehicle = GlobalLog()->ForceVehicleList->GetVehicleIndex(Vehicle());
                        CurDeployTonnage = DeployTonnageWith(*Vehicle(), 1.0f);
                        Vehicle()->Deployed = true;
                    }
                }
            }
            else if (deploy.Vehicle > -1)
            {
                // A vehicle was there.
                std::shared_ptr<MCLogVehicle> occupant;
                GlobalLog()->ForceVehicleList->GetVehicleInfo(deploy.Vehicle, occupant);
                CurDeployTonnage = DeployTonnageWith(*occupant, -1.0f);
                occupant->Deployed = false;
                deploy.Vehicle = -1;

                if (Mech() != nullptr)
                {
                    fits = MCBriefingScreen::FitsTonnage(*Mech());

                    if (fits)
                    {
                        GlobalLog()->SendAddMechMessage(*Mech(), lance, slot);
                        SoundSystem()->PlayInterfaceSample(PlacedSound);
                        deploy.Vehicle = -1;
                        deploy.Unit = GlobalLog()->ForceMechList->GetMechIndex(Mech());
                        CurDeployTonnage = DeployTonnageWith(*Mech(), 1.0f);
                        Mech()->Deployed = true;
                        GlobalLog()->AssignedWarriorList->SetDeployed(Mech()->PilotIndex, true);
                    }
                }
                else
                {
                    fits = MCBriefingScreen::FitsTonnage(*Vehicle());

                    if (fits)
                    {
                        GlobalLog()->SendAddVehicleMessage(*Vehicle(), lance, slot);
                        SoundSystem()->PlayInterfaceSample(PlacedSound);
                        deploy.Vehicle = GlobalLog()->ForceVehicleList->GetVehicleIndex(Vehicle());
                        CurDeployTonnage = DeployTonnageWith(*Vehicle(), 1.0f);
                        Vehicle()->Deployed = true;
                    }
                }
            }
            else
            {
                fits = PlaceInEmptySlot(lance, slot);
            }

            // (The slots don't overlap: no other slot is under the point.)
            if (!fits)
            {
                TooHeavy();
                return false;
            }

            Settle();
            return true;
        }
    }

    return false;
}

auto MCMechBriefBlock::Drop(MCGuiEvent& event) -> void
{
    // The slots' and the pane's new showing destroys this block (it was lifted out of a slot, or its unit left the
    // pane): pinned to the end, and its unit kept from here.
    const std::shared_ptr<MCGuiObject> pin = shared_from_this();
    const std::shared_ptr<MCLogMech> mech = Mech();
    const std::shared_ptr<MCLogVehicle> vehicle = Vehicle();
    MCBriefingScreen& screen = *GlobalLog()->BriefingScreen;
    const bool rightButton = event.Type == 6;
    GuiSystem().Release();
    Drag.Right = false;
    Drag.Left = false;
    MCDragIcon::Remove();

    // A right-button drop from the deploy pane goes to a free slot; a left-button drop to the slot under the mouse,
    // whatever was there going back to the pane.
    bool placed = false;

    if (rightButton)
    {
        placed = !Drag.FromSlot && DropInFreeSlot();
    }
    else
    {
        placed = DropInSlotAt(POINT{event.X, event.Y}, 0);
    }

    if (placed)
    {
        screen.SetUpDeploy();
        return;
    }

    // Nowhere to go: back to the deploy pane (a refusal sound when dropped off the pane).
    uint32_t sound = PlacedSound;

    if (!Drag.FromSlot)
    {
        MCScrollPane& pane = *screen.DeployPane;
        const bool overPane = event.X > pane.GlobalX() && event.X < pane.GlobalX() + pane.Width() &&
                              event.Y > pane.GlobalY() && event.Y < pane.GlobalY() + pane.Height();

        if (!overPane)
        {
            sound = RefusedSound;
        }
    }

    SoundSystem()->PlayInterfaceSample(sound);

    if (mech != nullptr)
    {
        mech->Deployed = false;
        GlobalLog()->AssignedWarriorList->SetDeployed(mech->PilotIndex, false);
    }
    else
    {
        vehicle->Deployed = false;
    }

    // Back in the pane (the original moved the block there): the pane's list shows the unit again.
    screen.SetUpDeploy();
}

auto MCMechBriefBlock::PickUp(MCGuiEvent& event) -> void
{
    const std::shared_ptr<MCGuiObject> pin = shared_from_this();
    const std::shared_ptr<MCLogVehicle> vehicle = Vehicle();
    MCBriefingScreen& screen = *GlobalLog()->BriefingScreen;
    const auto owner = std::static_pointer_cast<MCLogObject>(ParentObject());

    // Its briefing shows.
    const std::shared_ptr<MCLogPart> part = Part();
    screen.ShowBriefing(part);

    if (part->LocalPart == 0 || screen.ButtonsLocked)
    {
        return;
    }

    const bool fromSlot = owner.get() == &screen;
    Drag.FromSlot = fromSlot;
    SoundSystem()->PlayInterfaceSample(PickedUpSound);
    GuiSystem().Grab(weak_from_this());

    if (event.Type == 1)
    {
        Drag.Left = true;
    }

    Drag.X = GlobalX();
    Drag.Y = GlobalY();
    // Made on the screen, but kept off it until it is raised below (the original added it there).
    MCDragIcon::Create(
        screen, Drag.X, Drag.Y, BlockWidth, BlockHeight, [this](MCLogPort& surface) { OnBeginDrag(surface); }, true);

    if (fromSlot)
    {
        // Out of its slot (which shows empty again): the unit leaves the drop.
        screen.LiftFromSlot(*this);
        bool found = false;

        for (int32_t lance = 0; lance < 3 && !found; lance++)
        {
            for (int32_t slot = 0; slot < 4; slot++)
            {
                if (!LocalSlot(lance, slot) || PtInRect(SlotRect(lance, slot), POINT{event.X, event.Y}) == 0)
                {
                    continue;
                }

                auto& deploy = GlobalLog()->DeploySlots[lance][slot];

                if (Mech() == nullptr)
                {
                    vehicle->Deployed = false;
                    deploy.Vehicle = -1;
                    CurDeployTonnage = DeployTonnageWith(*vehicle, -1.0f);
                }
                else
                {
                    Mech()->Deployed = false;
                    deploy.Unit = -1;
                    CurDeployTonnage = DeployTonnageWith(*Mech(), -1.0f);
                    GlobalLog()->AssignedWarriorList->SetDeployed(Mech()->PilotIndex, false);
                }

                screen.CalcTonnages();
                GlobalLog()->SendRemoveForceMessage(lance, slot);
                found = true;
                break;
            }
        }

        Assert(found, 0, "Could not find the slot this item occupied");
    }
    else
    {
        // Out of the deploy pane (setUpDeploy rebuilds it without the block). A mech with a damaged engine or a
        // destroyed location can't drop.
        if (Mech() == nullptr)
        {
            vehicle->Deployed = true;
        }
        else
        {
            if (const uint32_t refusal = DropRefusal(*Mech()); refusal != 0)
            {
                const std::string text = LoadGameString(refusal, 0xfe);
                GuiSystem().Release();
                Drag.Left = false;
                Drag.Right = false;
                screen.SetUpDeploy();
                ShowLogMessage(text);
                return;
            }

            Mech()->Deployed = true;
            GlobalLog()->AssignedWarriorList->SetDeployed(Mech()->PilotIndex, true);
        }

        screen.SetUpDeploy();
    }

    MCDragIcon::Current()->BringToFront(true);
    MCDragIcon::Current()->Raise();
}

auto MCMechBriefBlock::OnBeginDrag(MCLogPort& surface) -> void
{
    MCPane& target = surface.Frame();
    MCBriefingScreen& screen = *GlobalLog()->BriefingScreen;

    if (!Parent.expired() && ParentObject().get() == &screen)
    {
        // In a drop slot: framed, over the empty slot.
        FillArea(target, 0, 0, BlockWidth, BlockHeight, 0x10);

        if (screen.EmptySlot != nullptr)
        {
            screen.EmptySlot->CopyTo(target, 0, 0, true);
        }

        PaintBlock(target, 0, 0, true);
        return;
    }

    // The deploy pane is colour 0x10 around its blocks.
    VfxPaneWipe(target, 0x10);
    PaintBlock(target, 0, 0, false);
}

auto MCMechBriefBlock::PaintBlock(const MCPane& target, int32_t xPos, int32_t yPos, bool framed) -> void
{
    const std::shared_ptr<MCLogVehicle> vehicle = Vehicle();
    // Drawn in place: the original put the block together in a picture of its art and copied it keyed on 0xff.
    const std::shared_ptr<MCLogPort> art = LogScreenArt(Mech() == nullptr ? "lscupv00.tga" : "lscupm00.tga");
    MCLogBlockPort port(target, xPos, yPos, art != nullptr ? art->Width() : 1, art != nullptr ? art->Height() : 1,
                        true);

    if (art != nullptr)
    {
        VfxPaneCopy(art->Frame(), 0, 0, port.Frame(), 0, 0, -1);
    }

    // The name centred at the top.
    auto writeName = [&](std::string_view name)
    {
        const std::string text(name);
        const int32_t textWidth = GreenFont->Width(text.c_str());
        GreenFont->WriteString(port.Frame(), (Width() - textWidth) / 2, 3, text.c_str(), -1);
    };

    if (Mech() == nullptr)
    {
        writeName(vehicle->FileName);
        MCLogBlockPort body(port.Frame(), 0, 0, port.Width(), port.Height(), true);

        for (int32_t location = 0; location < 5; location++)
        {
            MCLogistics::DrawVehicleBodyLoc(*vehicle, location, body, 0xd, 0xe);
        }
    }
    else
    {
        std::shared_ptr<MCLogWarrior> warrior;

        if (Mech()->LocalPart == 0)
        {
            warrior = Mech()->NetworkPilot.lock();
        }
        else
        {
            GlobalLog()->AssignedWarriorList->GetWarriorInfo(Mech()->PilotIndex, warrior);
        }

        // Port fix: a mech without a pilot shows no callsign (the original read through null).
        writeName(warrior != nullptr ? warrior->Callsign : "");

        {
            MCLogBlockPort body(port.Frame(), 2, 0xe, 0x19, 0x1e, true);
            VfxPaneWipe(body.Frame(), 0x10);

            for (int32_t location = 0; location < 8; location++)
            {
                MCLogistics::DrawMechBodyLoc(*Mech(), location, body, 0, 0);
            }
        }

        // The pilot's picture.
        const std::string picture = warrior == nullptr ? std::format("{}pilot{:02}.tga", ArtPath, Mech()->PilotIndex)
                                                       : std::format("{}{}", ArtPath, warrior->Picture);

        if (const std::shared_ptr<MCLogPort> pilot = LogArt(picture))
        {
            pilot->CopyTo(port.Frame(), 0x1c, 0xe, true);
        }

        // The mech's status bar (green, yellow, red) and the pilot's health bar.
        VfxLineDraw(port.Frame(), 3, 0xb, 0x19, 0xb, 0x12);
        VfxLineDraw(port.Frame(), 3, 0xc, 0x19, 0xc, 0x12);
        const float status = Mech()->StatusValue;

        if (status != 0.0f)
        {
            uint32_t color;

            if (!(status < 0.5))
            {
                color = 0xb;
            }
            else if (status > 0.2)
            {
                color = 0xf2;
            }
            else
            {
                color = 0xef;
            }

            const int32_t barEnd = static_cast<int32_t>(status * 23.0f) + 3;
            VfxLineDraw(port.Frame(), 3, 0xb, barEnd, 0xb, color);
            VfxLineDraw(port.Frame(), 3, 0xc, barEnd, 0xc, color);
        }

        if (warrior != nullptr && warrior->Health < 6.0f)
        {
            const auto healthEnd = static_cast<int32_t>(warrior->Health * 3.0f + 31.0f);
            VfxLineDraw(port.Frame(), healthEnd, 0xb, 0x2f, 0xb, 0x10);
            VfxLineDraw(port.Frame(), healthEnd, 0xc, 0x2f, 0xc, 0x10);
        }
    }

    if (MultiPlayer() != nullptr)
    {
        // Another player's unit is darkened.
        const int32_t index = Mech() != nullptr ? GlobalLog()->ForceMechList->GetMechIndex(Mech())
                                                : GlobalLog()->ForceVehicleList->GetVehicleIndex(vehicle);

        if (index < 0)
        {
            MCLogistics::Darken(0, LogisticFadetable(), port);
        }
    }

    if (framed)
    {
        // In a slot: a bevelled frame.
        VfxLineDraw(port.Frame(), 0, 0, Width() - 1, 0, 0x32);
        VfxLineDraw(port.Frame(), 0, 1, 0, Height() - 2, 0x32);
        VfxLineDraw(port.Frame(), 0, Height() - 1, Width() - 1, Height() - 1, 0x15);
        VfxLineDraw(port.Frame(), Width() - 1, 0, Width() - 1, Height() - 2, 0x15);
    }
}

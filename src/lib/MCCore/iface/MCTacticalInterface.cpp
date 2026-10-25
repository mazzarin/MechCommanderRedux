#include "stdafx.h"
#include "iface/MCTacticalInterface.h"
#include "camera/MCCamera.h"
#include "gui/MCGuiInput.h"
#include "camera/MCMainWindow.h"
#include "gui/MCFloatHelp.h"
#include "iface/MCCommandParser.h"
#include "iface/MCKeyBindings.h"
#include "iface/MCMechBar.h"
#include "iface/MCOrderSink.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGamePaths.h"
#include "mission/MCScenario.h"
#include "main/MCGameContext.h"
#include "main/MCMissionGlobals.h"
#include "network/MCMultiPlayer.h"
#include "object/MCArtillery.h"
#include "object/MCBattleMech.h"
#include "object/MCForces.h"
#include "object/MCMasterComponent.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCMoverGroup.h"
#include "object/MCObjectEvent.h"
#include "object/MCObjectQueue.h"
#include "object/MCObjectSystem.h"
#include "platform/MCFrameLog.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"

namespace
{
    /// <summary>
    /// The port's value for the original's <c>GetSystemMetrics(SM_CXDRAG)</c>, the drag distance used when
    /// <c>iface.fit</c> has none: Windows' default.
    /// </summary>
    constexpr int16_t SystemDragWidth = 4;

    /// <summary>
    /// The strike types <see cref="MCTacticalInterface::CallStrike"/> takes (artillery object type numbers), each with
    /// the <c>CallArtillery</c> strike type for team 0 and team 1.
    /// </summary>
    struct MCStrikeTypeEntry
    {
        int32_t ObjectType = 0;
        std::array<int32_t, 2> StrikeType{};
    };

    constexpr auto StrikeTypes = std::to_array<MCStrikeTypeEntry>({
        {0xf9, {0, 4}},
        {0xf8, {1, 5}},
        {0xfa, {2, 6}},
        {0x1fc, {0, 4}},
        {0x1fb, {1, 5}},
        {0x1fd, {2, 6}},
        {0x204, {3, 7}},
    });

    /// <summary>Each live mech bar mover, in the bar's order.</summary>
    template <typename Visit> void ForEachLiveBarMover(const MCMechBar& bar, Visit visit)
    {
        for (const std::shared_ptr<MCFriendlyMechIcon>& button : bar.Buttons)
        {
            const auto mover = std::static_pointer_cast<MCMover>(LockObject(button->Mover));

            if (mover != nullptr && mover->IsDisabled() == 0)
            {
                visit(*mover);
            }
        }
    }
}

auto TacticalInterface() -> std::shared_ptr<MCTacticalInterface>
{
    return MCGameContext::Current().TacticalInterface();
}

auto UpdateMouseStateCallback() -> void
{
    if (Turn > 1)
    {
        MCFrameLog::Scope part("logic.mouseState");
        TacticalInterface()->UpdateMouseState(std::nullopt);
    }
}

MCTacticalInterface::MCTacticalInterface() = default;

MCTacticalInterface::~MCTacticalInterface()
{
    // As the original's destroy: the bar, the parser, then the tags.
    MechBar.reset();
    CommandParser.reset();

    for (MCGuiOwned<MCFloatHelp>& tag : FloatingTags)
    {
        tag.reset();
    }

    ReserveIcons.clear();

    if (_ScrollCallback != nullptr && HasGuiSystem())
    {
        GuiSystem().RemoveCallback(*_ScrollCallback);
    }
}

auto MCTacticalInterface::Init() -> int32_t
{
    ReleaseScrollKeys();

    // The mech bar, along the bottom of the screen.
    MechBar = MCMakeGui<MCMechBar>();
    MechBar->Init(0, 0, 1, 1, {});
    MechBar->Resize(0x280, 0x3d);
    MechBar->ShowGuiWindow(false);
    ScreenWindow()->AddChild(MechBar);
    MechBar->MoveTo(0, GuiSystem().Height() - MechBar->Height() - 1, false);
    MechBar->SetDepth(0x4b);

    // iface.fit's parameters.
    MCFitIniFile ifaceFile;

    if (const int32_t result = ifaceFile.Open(GamePath(InterfacePath, "iface", ".fit")); result != 0)
    {
        return result;
    }

    if (const int32_t result = ifaceFile.SeekBlock("Parameters"); result != 0)
    {
        return result;
    }

    DragDistance = ifaceFile.Read<int16_t>("Drag Distance").value_or(SystemDragWidth);
    ScrollSpeed = ifaceFile.Read<int16_t>("Scroll Speed").value_or(ScrollSpeed);
    TacScrollSpeed = ifaceFile.Read<int16_t>("Tac Scroll Speed").value_or(TacScrollSpeed);
    ScrollStart = ifaceFile.Read<int16_t>("Scroll Start").value_or(ScrollStart);
    ShuffleFrames = ifaceFile.Read<int16_t>("Shuffle Frames").value_or(0xf);
    ifaceFile.Close();

    // The floating tags.
    for (MCGuiOwned<MCFloatHelp>& tag : FloatingTags)
    {
        tag = MCMakeGui<MCFloatHelp>();
        Assert(tag->Init(0, 0, 10, 10, {}) == 0, 0, "Error initializing floating tags");
        tag->SetBackColor(0xf4);
        ScreenWindow()->AddChild(tag);
        tag->SetDepth(0x28);
        tag->ShowGuiWindow(false);
    }

    // The default key bindings. Port: the player's (MCKeyBindings), which are the defaults unless the CONTROLS screen
    // changed them.
    Keys = MCKeyBindings::Player();

    RotateKey = 0x38;
    SelectedEnemy.reset();
    CursorOffset = 0;
    ForcingOrder = false;
    ForcedOrder = MCForcedOrder::None;
    return 0;
}

auto MCTacticalInterface::HandleEvent(MCGuiEvent& event) -> void
{
    // Mission messages and keys.
    switch (event.Type)
    {
        case 0x1404:
        {
            if (Scenario() != nullptr)
            {
                Scenario()->CreatePartObject(event.Data);
            }

            return;
        }
        case 0x1403:
        {
            if (Scenario() != nullptr)
            {
                Scenario()->DestroyPartObject(event.Data);
            }

            return;
        }
        case 0x1402:
        {
            RemoveMech(event.Data);
            return;
        }
        case 9:
        {
            HandleKeyDown(event);
            return;
        }
        case 8:
        {
            HandleKeyUp(event);
            return;
        }
        default:
        {
            if (event.Type > 8)
            {
                return;
            }

            HandleMouse(event);
            return;
        }
    }
}

auto MCTacticalInterface::ZoomIn(float factor, bool sound) -> void
{
    // Port: the view shows fewer lines of the world (the original switched the camera to scale 100); the tactical
    // map's zoom button follows.
    if (Eye() == nullptr || Eye()->View() == nullptr || GamePaused != 0 || GameAsked != 0)
    {
        return;
    }

    if (Eye()->View()->ZoomBy(1.0f / factor))
    {
        if (sound)
        {
            SoundSystem()->PlayInterfaceSample(0x44);
        }

        Eye()->ForceUpdate = true;
        MCTerrain::ForceRedraw = true;
    }
}

auto MCTacticalInterface::ZoomOut(float factor, bool sound) -> void
{
    // Port: the view shows more lines of the world (the original switched the camera to scale 1); the tactical map's
    // zoom button follows.
    if (Eye() == nullptr || Eye()->View() == nullptr || GamePaused != 0 || GameAsked != 0)
    {
        return;
    }

    if (Eye()->View()->ZoomBy(factor))
    {
        if (sound)
        {
            SoundSystem()->PlayInterfaceSample(0x45);
        }

        Eye()->ForceUpdate = true;
        MCTerrain::ForceRedraw = true;
    }
}

auto MCTacticalInterface::StartScenario() -> void
{
    CommandParser = std::make_unique<MCCommandParser>(*this);
    MouseTarget = MCMouseTarget::NotYetUpdated;
    MouseObject.reset();
    SetMode(MCInterfaceMode::None);
    UpdateInterface();

    if (MainHolder() != nullptr)
    {
        MainHolder()->ShowGuiWindow(true);
    }

    if (_ScrollCallback == nullptr)
    {
        _ScrollCallback = std::make_shared<MCGuiCallback>();
        _ScrollCallback->SetExec(ScrollScreen);
        GuiSystem().AddCallback(_ScrollCallback);
    }

    SelectedEnemy.reset();
    ::TacticalMap()->SetDepth(0x50);

    if (std::shared_ptr<MCFriendlyMechIcon> first = MechBar->GetButton(0); first != nullptr)
    {
        ::TacticalMap()->SetID(first->PartId);
    }

    MechBar->ShowGuiWindow(true);
    MechBar->InitLances();
    DragTarget.reset();
    ForcingOrder = false;
    TakePlayerKeys();
}

auto MCTacticalInterface::TakePlayerKeys() -> void
{
    // Port: the player's key bindings as they are now (the CONTROLS screen may have changed them since Init).
    Keys = MCKeyBindings::Player();

    // The salvage page's key (alt+S by default) becomes alt+C, the chat page, in multiplayer. Port: unless the player
    // gave it another key.
    uint32_t& salvageKey = Keys[static_cast<size_t>(MCKeyCommand::SalvagePage)];

    if (salvageKey == MCKeyBindings::Defaults()[static_cast<size_t>(MCKeyCommand::SalvagePage)])
    {
        MCKeyBindings::BindKey(salvageKey, MultiPlayer() != nullptr ? 0x2e : 0x1f, 0);
    }
}

auto MCTacticalInterface::EndScenario() -> void
{
    ClearMechSelection();
    _HighlightedObject.reset();
    CommandParser.reset();
    MechBar->CleanUp();
    MechBar->ShowGuiWindow(false);
    HideTags();

    if (MainHolder() != nullptr)
    {
        MainHolder()->ShowGuiWindow(false);
    }

    GuiSystem().RemoveCallback(*_ScrollCallback);
    _ScrollCallback.reset();
    ReserveIcons.clear();
}

auto MCTacticalInterface::AddMech(int32_t partId, int32_t lance, bool active, bool onBar) -> void
{
    auto icon = MCMakeGui<MCFriendlyMechIcon>();
    icon->Init(0, 0, 0x34, 0x2e, {});
    icon->SetID(partId);
    icon->Active = active;
    icon->ShowGuiWindow(active);

    if (!onBar)
    {
        icon->Lance = NoLance;
        ReserveIcons.push_back(std::move(icon));
        return;
    }

    icon->Lance = lance;
    MCFriendlyMechIcon& button = *icon;

    if (MechBar->AddButton(std::move(icon)))
    {
        MechBar->Draw();
        button.SetEventRoutine(MechIconHandleEvent);
    }
}

auto MCTacticalInterface::ActivateMech(int32_t partId) const -> void
{
    if (std::shared_ptr<MCFriendlyMechIcon> icon = MechBar->GetButtonFromID(partId); icon != nullptr)
    {
        icon->Active = true;
        icon->ShowGuiWindow(true);
        icon->SetID(icon->PartId);
        MechBar->PlaceButtons(true);
    }
}

auto MCTacticalInterface::RemoveMech(int32_t partId) -> void
{
    // The original narrowed the part id to 16 bits for this check.
    if (IsOurs(static_cast<int16_t>(partId)))
    {
        std::shared_ptr<MCFriendlyMechIcon> icon = MechBar->GetButtonFromID(partId);

        if (icon != nullptr)
        {
            if (!icon->IsPoint)
            {
                // Drop it from the selection (OB-156 fixed) and tell the mover.
                std::erase(SelectedMovers, partId);

                if (ObjectList() != nullptr)
                {
                    if (std::shared_ptr<MCBaseObject> object = ObjectList()->FindObjectFromPart(partId);
                        object != nullptr)
                    {
                        MCObjectEvent deselect;
                        deselect.Init(0x1d);
                        object->HandleEvent(deselect);
                    }
                }
            }
            else
            {
                // The lance's point died: once no member is left standing, the lance goes from the selection.
                std::shared_ptr<MCLanceIcon> lanceIcon = MechBar->GetLanceIconFromID(icon->Lance);
                Assert(lanceIcon != nullptr, partId, " InterfaceObject.RemoveMech: NULL lanceIcon ");
                lanceIcon->Linked = false;
                const std::shared_ptr<MCMoverGroup> group = lanceIcon->Group.lock();
                const bool anyStanding =
                    std::ranges::any_of(group->GetMovers(), [](const std::shared_ptr<MCMover>& member)
                                        { return member->IsDisabled() == 0; });

                if (!anyStanding)
                {
                    DeselectLance(group);
                    CommandParser->RemoveSubject(*group);
                }
            }
        }

        if (CommandParser != nullptr)
        {
            CommandParser->RemoveSubject(partId);
        }
    }

    if (const std::shared_ptr<MCGameObject> enemy = LockObject(SelectedEnemy);
        enemy != nullptr && enemy->PartId == partId)
    {
        SelectedEnemy.reset();
    }
}

auto MCTacticalInterface::UpdateInterface() const -> void
{
    MechBar->Draw();
}

auto MCTacticalInterface::CallStrike(int32_t strikeType, std::optional<MCVector3D> position,
                                     std::shared_ptr<MCGameObject> target, bool forCommander, bool forClans,
                                     float delay) -> void
{
    const auto entry = std::ranges::find(StrikeTypes, strikeType, &MCStrikeTypeEntry::ObjectType);

    if (entry == StrikeTypes.end())
    {
        return;
    }

    MCVector3D location;

    if (position.has_value())
    {
        location = *position;
    }
    else
    {
        if (target == nullptr)
        {
            return;
        }

        location = target->GetPosition();
    }

    int32_t commanderId = 0;
    std::shared_ptr<MCTeam> team = InnerSphereTeam();

    if (forCommander)
    {
        commanderId = HomeCommander()->GetId();
        team = HomeCommander()->GetTeam();
    }
    else if (forClans)
    {
        if (MultiPlayer() != nullptr)
        {
            Fatal(0, " Iface.CallStrike: Need more info than clanStrike in MultiPlayer() ");
        }

        team = ClanTeam();
        commanderId = 1;
    }

    // Artillery and sensor strikes must be aimed at a point the team can see; the others can go anywhere.
    const int32_t artilleryType = entry->StrikeType[team->Id];
    const bool needsSight = artilleryType == 0 || artilleryType == 1 || artilleryType == 4 || artilleryType == 5;

    if (needsSight && team->LineOfSight(location) == 0)
    {
        SoundSystem()->PlayInterfaceSample(0x33);
        return;
    }

    const auto seconds = static_cast<int32_t>(delay);

    if (MultiPlayer() != nullptr && MultiPlayer()->IsServer == 0)
    {
        MultiPlayer()->SendPlayerArtillery(artilleryType, location, seconds);
        return;
    }

    CallArtillery(commanderId, artilleryType, location, seconds, 0);
}

auto MCTacticalInterface::HideTags() -> void
{
    for (const MCGuiOwned<MCFloatHelp>& tag : FloatingTags)
    {
        tag->ShowGuiWindow(false);
    }
}

auto MCTacticalInterface::SetUnit(int32_t groupId, std::span<const std::shared_ptr<MCMover>> movers,
                                  int32_t pointIndex) const -> void
{
    HomeCommander()->SetGroup(groupId, movers, pointIndex);

    if (MultiPlayer() != nullptr)
    {
        MultiPlayer()->SendPlayerMoverGroup(groupId, movers, pointIndex);
    }

    // Relink every icon to its lance and mark the points.
    for (const std::shared_ptr<MCFriendlyMechIcon>& button : MechBar->Buttons)
    {
        button->Lance = NoLance;
    }

    for (int32_t lance = 0; lance < static_cast<int32_t>(MCMechBar::NumLances); lance++)
    {
        std::shared_ptr<MCMoverGroup> group = HomeCommander()->GetGroup(lance);

        for (const std::shared_ptr<MCMover>& member : group->GetMovers())
        {
            if (member != nullptr)
            {
                MechBar->GetButtonFromID(member->PartId)->Lance = lance;
            }
        }

        if (std::shared_ptr<MCMover> point = group->GetPoint(); point != nullptr)
        {
            MechBar->GetButtonFromID(point->PartId)->IsPoint = true;
        }

        // Original behaviour: marks the lance icon of groupId each time, not that of the lance just relinked.
        MechBar->GetLanceIconFromID(groupId)->Linked = true;
    }
}

auto MCTacticalInterface::SetPoint(int32_t partId, bool isPoint) const -> void
{
    if (std::shared_ptr<MCFriendlyMechIcon> button = MechBar->GetButtonFromID(partId); button != nullptr)
    {
        button->IsPoint = isPoint;
    }
}

auto MCTacticalInterface::GetMechIconFromID(int32_t partId) const -> std::shared_ptr<MCFriendlyMechIcon>
{
    if (std::shared_ptr<MCFriendlyMechIcon> icon = MechBar->GetButtonFromID(partId); icon != nullptr)
    {
        return icon;
    }

    const auto found = std::ranges::find_if(ReserveIcons, [partId](const std::shared_ptr<MCFriendlyMechIcon>& icon)
                                            { return icon->PartId == partId; });
    return found != ReserveIcons.end() ? *found : nullptr;
}

auto MCTacticalInterface::Orders() const -> MCOrderSink&
{
    return _OrderSink != nullptr ? *_OrderSink : GameOrderSink();
}

auto MCTacticalInterface::TagMover(MCFloatHelp& tag, MCGameObject& mover) -> void
{
    std::string text =
        std::format("{}\n{}", mover.GetPilot()->Callsign, MCPrintfText(static_cast<MCMover&>(mover).GetIfaceName()));
    tag.HelpObject = mover.WeakAs<MCGameObject>();
    tag.SetBackColor(0);
    tag.TextColor = 0xb;
    tag.SetHelpText(text.data());
}

auto MCTacticalInterface::CheatHealAll() const -> void
{
    ForEachLiveBarMover(*MechBar,
                        [](MCMover& mover)
                        {
                            for (int32_t j = 0; j < mover.NumArmorLocations(); j++)
                            {
                                mover.Armor[j].CurArmor = static_cast<float>(mover.Armor[j].MaxArmor);
                            }

                            for (int32_t j = 0; j < mover.NumBodyLocations(); j++)
                            {
                                mover.BodyAt(j).CurInternalStructure =
                                    static_cast<float>(mover.BodyAt(j).MaxInternalStructure);
                            }

                            if (mover.ObjectClass == MCObjectClass::BattleMech)
                            {
                                static_cast<MCBattleMech&>(mover).CalcLegStatus();
                                static_cast<MCBattleMech&>(mover).CalcTorsoStatus();
                            }

                            const uint32_t firstWeapon = mover.NumOther;

                            for (uint32_t j = firstWeapon; j < firstWeapon + mover.NumWeapons; j++)
                            {
                                mover.Inventory[j].Disabled = 0;
                            }

                            for (int32_t j = 0; j < mover.NumAmmoTypes(); j++)
                            {
                                if (mover.AmmoTypeTotal[j].CurAmount != 9999)
                                {
                                    mover.AmmoTypeTotal[j].CurAmount = mover.AmmoTypeTotal[j].StartAmount;
                                }
                            }
                        });
}

auto MCTacticalInterface::CheatDeadEye() const -> void
{
    ForEachLiveBarMover(*MechBar,
                        [](MCMover& mover)
                        {
                            if (mover.GetPilot() != nullptr)
                            {
                                mover.GetPilot()->Skills[SkillGunnery] = 120;
                            }
                        });
}

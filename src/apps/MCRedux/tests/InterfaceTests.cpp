#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "ai/MCTacticalOrder.h"
#include "camera/MCMainWindow.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCCommandParser.h"
#include "iface/MCKeyBindings.h"
#include "iface/MCMechBar.h"
#include "iface/MCOrderSink.h"
#include "iface/MCTacticalInterface.h"
#include "object/MCForces.h"
#include "object/MCGroundVehicle.h"
#include "object/MCMover.h"
#include "object/MCMoverGroup.h"
#include "platform/MCDisplay.h"

// The tactical interface (iface/): the command parser's subject limits, the mover ranking, and, on a booted mission,
// the key bindings, the click orders, the selection and the cursor the mouse state picks.

namespace
{
    /// <summary>An order the interface gave, as the recording sink saw it.</summary>
    struct MCRecordedOrder
    {
        /// <summary>The mover's part id, or the lance's id for a lance order.</summary>
        int32_t Subject = 0;
        bool ToLance = false;
        bool Queued = false;
        MCTacticalOrder Order;
    };

    /// <summary>An order sink that records the orders instead of giving them.</summary>
    class MCRecordingOrderSink final : public MCOrderSink
    {
    public:
        void Give(MCMover& mover, MCTacticalOrder& order) override
        {
            Orders.push_back({mover.PartId, false, false, order});
        }

        void Give(MCMoverGroup& group, MCTacticalOrder& order, std::span<const MCVector3D>) override
        {
            Orders.push_back({group.GetId(), true, false, order});
        }

        void Queue(MCMover& mover, MCTacticalOrder& order) override
        {
            Orders.push_back({mover.PartId, false, true, order});
        }

        void SendToServer(MCTacticalOrder&, bool, std::span<int32_t>, std::span<const std::weak_ptr<MCMoverGroup>>,
                          bool) override
        {
            ServerOrders++;
        }

        std::vector<MCRecordedOrder> Orders;
        int32_t ServerOrders = 0;
    };

    /// <summary>Sends the interface a key going down (type 9) or up (type 8).</summary>
    void SendKey(int16_t scanCode, uint32_t modifiers = 0, bool down = true)
    {
        MCGuiEvent event;
        event.Clear();
        event.Type = down ? 9 : 8;
        event.ScanCode = scanCode;
        event.ShiftKey = (modifiers & KeyShift) != 0 ? 1 : 0;
        event.CtrlKey = (modifiers & KeyCtrl) != 0 ? 1 : 0;
        event.AltKey = (modifiers & KeyAlt) != 0 ? 1 : 0;
        TacticalInterface()->HandleEvent(event);
    }

    /// <summary>The middle of the main view, in screen coordinates.</summary>
    std::pair<int32_t, int32_t> ViewCentre()
    {
        std::shared_ptr<MCGuiObject> pane = MainHolder()->GetActivePane();
        return {pane->GlobalX() + pane->Width() / 2, pane->GlobalY() + pane->Height() / 2};
    }

    /// <summary>A left click (down, then up) on the main view at (<paramref name="x"/>, <paramref name="y"/>).</summary>
    void Click(int32_t x, int32_t y, bool shift = false)
    {
        for (const int32_t type : {1, 4})
        {
            MCGuiEvent event;
            event.Clear();
            event.Type = type;
            event.X = x;
            event.Y = y;
            event.ShiftKey = shift ? 1 : 0;
            event.Target = MainHolder()->GetActivePane();
            TacticalInterface()->HandleEvent(event);
        }
    }

    /// <summary>Selects every active mover on the mech bar, as a shift-click on each does.</summary>
    std::vector<int32_t> SelectWholeForce()
    {
        std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
        iface->ClearMechSelection();
        std::vector<int32_t> selected;

        for (const std::shared_ptr<MCFriendlyMechIcon>& button : iface->MechBar->Buttons)
        {
            if (button->Active)
            {
                iface->SelectMech(button->PartId);
                iface->CommandParser->AddSubject(button->PartId);
                selected.push_back(button->PartId);
            }
        }

        return selected;
    }

    /// <summary>The orders recorded for movers (not lances), by part id, all with <paramref name="code"/>.</summary>
    std::vector<int32_t> OrderedMovers(const MCRecordingOrderSink& sink, MCTacticalOrderCode code)
    {
        std::vector<int32_t> movers;

        for (const MCRecordedOrder& recorded : sink.Orders)
        {
            if (!recorded.ToLance && recorded.Order.Code == code)
            {
                movers.push_back(recorded.Subject);
            }
        }

        std::ranges::sort(movers);
        return movers;
    }
}

TEST_CASE("iface: the parser orders twelve movers and, as the original, three lances (OB-068)")
{
    MCTacticalInterface iface;
    MCCommandParser parser(iface);

    for (int32_t partId = 100; partId < 111; partId++)
    {
        CHECK(parser.AddSubject(partId));
    }

    // A mover already there is not added twice; a thirteenth is refused (the full check comes first).
    CHECK(parser.AddSubject(105));
    CHECK_EQ(parser.Subjects.size(), size_t{11});
    CHECK(parser.AddSubject(111));
    CHECK_EQ(parser.Subjects.size(), size_t{12});
    CHECK(!parser.AddSubject(112));
    CHECK(!parser.IsSubject(112));

    const std::array lances = {std::make_shared<MCMoverGroup>(0), std::make_shared<MCMoverGroup>(1),
                               std::make_shared<MCMoverGroup>(2), std::make_shared<MCMoverGroup>(3)};
    CHECK(parser.AddSubject(lances[0]));
    CHECK(parser.AddSubject(lances[1]));
    CHECK(parser.AddSubject(lances[2]));
    // OB-068: the fourth lance is refused although the commander has four.
    CHECK(!parser.AddSubject(lances[3]));
    CHECK(parser.IsSubject(*lances[2]));
    CHECK(!parser.IsSubject(*lances[3]));

    parser.RemoveSubject(*lances[0]);
    CHECK(!parser.IsSubject(*lances[0]));
    CHECK(parser.AddSubject(lances[3]));
    CHECK_EQ(parser.GroupSubjects.size(), size_t{3});

    parser.ClearSubjects();
    CHECK(parser.Subjects.empty());
    CHECK(parser.GroupSubjects.empty());
}

TEST_CASE("iface: movers rank nearest first; equal distances keep the original qsort's order")
{
    CHECK((RankByDistance(std::vector<float>{5.0f, 1.0f, 3.0f}) == std::vector<size_t>{1, 2, 0}));

    // The Visual C++ 6 qsort sorts a short run by moving the largest (the first of equals) to the end: three equal
    // distances come out as 1, 2, 0.
    CHECK((RankByDistance(std::vector<float>{2.0f, 2.0f, 2.0f}) == std::vector<size_t>{1, 2, 0}));

    // A missing mover's distance (nearly FLT_MAX) ranks it last.
    const float missing = std::bit_cast<float>(0x7f7fc99eu);
    CHECK((RankByDistance(std::vector<float>{missing, 10.0f, 4.0f}) == std::vector<size_t>{2, 1, 0}));
    CHECK(RankByDistance({}).empty());
}

TEST_CASE("iface: the aimed shots aim at the body locations their keys show")
{
    // The keypad's layout over a mech: 8 head, 7/9 left/right torso, 5 centre torso, 4/6 arms, 1/3 legs.
    CHECK_EQ(AimedLocation(MCInterfaceMode::AimHead), 0);
    CHECK_EQ(AimedLocation(MCInterfaceMode::AimCenterTorso), 1);
    CHECK_EQ(AimedLocation(MCInterfaceMode::AimLeftTorso), 2);
    CHECK_EQ(AimedLocation(MCInterfaceMode::AimRightTorso), 3);
    CHECK_EQ(AimedLocation(MCInterfaceMode::AimLeftArm), 4);
    CHECK_EQ(AimedLocation(MCInterfaceMode::AimRightArm), 5);
    CHECK_EQ(AimedLocation(MCInterfaceMode::AimLeftLeg), 6);
    CHECK_EQ(AimedLocation(MCInterfaceMode::AimRightLeg), 7);
    CHECK(IsAttackMode(MCInterfaceMode::AttackConservingAmmo));
    CHECK(!IsAttackMode(MCInterfaceMode::Jump));
    CHECK(IsLanceLink(MCInterfaceMode::LinkLance3));
    CHECK_EQ(LinkedLance(MCInterfaceMode::LinkLance3), 2);
}

/// <summary>
/// Each key of the default bindings picks the mode its help text names, and the order keys order the selection at
/// once: backspace stops, page up and page down power up and down.
/// </summary>
TEST_CASE_ISOLATED("game: the default keys pick their modes and the order keys order the selection")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
    REQUIRE(iface != nullptr);
    const auto sinkPin = std::make_shared<MCRecordingOrderSink>();
    MCRecordingOrderSink& sink = *sinkPin;
    iface->SetOrderSink(sinkPin);
    std::vector<int32_t> force = SelectWholeForce();
    std::ranges::sort(force);
    REQUIRE(!force.empty());
    REQUIRE(iface->AnySelected(true));

    struct MCModeBinding
    {
        std::string_view Name;
        int16_t ScanCode;
        uint32_t Modifiers;
        MCInterfaceMode Mode;
    };

    const MCModeBinding bindings[] = {
        {"home ejects", 0x147, 0, MCInterfaceMode::Eject},
        {"space runs", 0x39, 0, MCInterfaceMode::Run},
        {"ctrl+space runs", 0x39, KeyCtrl, MCInterfaceMode::Run},
        {"O attacks from the optimal range", 0x18, 0, MCInterfaceMode::AttackOptimalRange},
        {"L attacks from long range", 0x26, 0, MCInterfaceMode::AttackLongRange},
        {"M attacks from medium range", 0x32, 0, MCInterfaceMode::AttackMediumRange},
        {"S attacks from short range", 0x1f, 0, MCInterfaceMode::AttackShortRange},
        {"C attacks from position", 0x2e, 0, MCInterfaceMode::AttackFromPosition},
        {"A attacks conserving ammunition", 0x1e, 0, MCInterfaceMode::AttackConservingAmmo},
        {"J jumps", 0x24, 0, MCInterfaceMode::Jump},
        {"G guards", 0x22, 0, MCInterfaceMode::Guard},
        {"keypad 8 aims at the head", 0x48, 0, MCInterfaceMode::AimHead},
        {"keypad 7 aims at the left torso", 0x47, 0, MCInterfaceMode::AimLeftTorso},
        {"keypad 9 aims at the right torso", 0x49, 0, MCInterfaceMode::AimRightTorso},
        {"keypad 5 aims at the centre torso", 0x4c, 0, MCInterfaceMode::AimCenterTorso},
        {"keypad 4 aims at the left arm", 0x4b, 0, MCInterfaceMode::AimLeftArm},
        {"keypad 6 aims at the right arm", 0x4d, 0, MCInterfaceMode::AimRightArm},
        {"keypad 1 aims at the left leg", 0x4f, 0, MCInterfaceMode::AimLeftLeg},
        {"keypad 3 aims at the right leg", 0x51, 0, MCInterfaceMode::AimRightLeg},
        {"I shows info", 0x17, 0, MCInterfaceMode::Info},
        {"T follows with the camera", 0x14, 0, MCInterfaceMode::CameraFollow},
    };

    for (const MCModeBinding& binding : bindings)
    {
        MCTest::Scope scope(binding.Name);
        iface->SetMode(MCInterfaceMode::None);
        SendKey(binding.ScanCode, binding.Modifiers);
        CHECK_EQ(static_cast<int32_t>(iface->CurrentMode), static_cast<int32_t>(binding.Mode));
    }

    CHECK(sink.Orders.empty());

    // The order keys: the mode, and the order to every selected mover now.
    struct MCOrderBinding
    {
        std::string_view Name;
        int16_t ScanCode;
        MCInterfaceMode Mode;
        MCTacticalOrderCode Code;
    };

    const MCOrderBinding orderKeys[] = {
        {"backspace stops", 0xe, MCInterfaceMode::Stop, MCTacticalOrderCode::Stop},
        {"page up powers up", 0x149, MCInterfaceMode::PowerUp, MCTacticalOrderCode::PowerUp},
        {"page down powers down", 0x151, MCInterfaceMode::PowerDown, MCTacticalOrderCode::PowerDown},
    };

    for (const MCOrderBinding& binding : orderKeys)
    {
        MCTest::Scope scope(binding.Name);
        sink.Orders.clear();
        SendKey(binding.ScanCode);
        CHECK_EQ(static_cast<int32_t>(iface->CurrentMode), static_cast<int32_t>(binding.Mode));
        CHECK(OrderedMovers(sink, binding.Code) == force);
        CHECK_EQ(sink.Orders.size(), force.size());
    }

    // F9 held makes the next click a forced order; let go, it doesn't. Releasing a key off the map also ends the mode.
    SendKey(0x43);
    CHECK(iface->ForcingOrder);
    SendKey(0x43, 0, false);
    CHECK(!iface->ForcingOrder);
    CHECK(iface->CurrentMode == MCInterfaceMode::None);

    // With nothing selected the attack keys pick nothing, and the order keys order nobody.
    iface->ClearMechSelection();
    iface->SetMode(MCInterfaceMode::None);
    sink.Orders.clear();
    SendKey(0x26);
    CHECK(iface->CurrentMode == MCInterfaceMode::None);
    SendKey(0xe);
    CHECK(iface->CurrentMode == MCInterfaceMode::Stop);
    CHECK(sink.Orders.empty());

    iface->SetOrderSink(nullptr);
}

/// <summary>
/// Port: the keys the player bound on the CONTROLS screen are the ones a mission listens to (the scenario takes them
/// when it starts, TakePlayerKeys). Jump on K and guard on shift+J: K jumps, J (no longer a key) does nothing,
/// shift+J guards; the salvage page keeps the key it was given instead of the multiplayer swap's.
/// </summary>
TEST_CASE_ISOLATED("game: a mission takes the keys the player bound")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
    REQUIRE(iface != nullptr);
    CHECK_EQ(iface->Key(MCKeyCommand::Jump), MCKeyBindings::Defaults()[static_cast<size_t>(MCKeyCommand::Jump)]);
    MCKeyBindings::MCKeyTable keys = MCKeyBindings::Defaults();
    MCKeyBindings::Bind(keys, MCKeyCommand::Jump, 0x25);
    MCKeyBindings::Bind(keys, MCKeyCommand::Guard, 0x24 | KeyShift);
    MCKeyBindings::Bind(keys, MCKeyCommand::SalvagePage, 0x2f | KeyAlt);
    MCKeyBindings::SetPlayer(keys);
    iface->TakePlayerKeys();
    CHECK_EQ(iface->Key(MCKeyCommand::Jump), 0x25u);
    CHECK_EQ(iface->Key(MCKeyCommand::SalvagePage), 0x2fu | KeyAlt);
    SelectWholeForce();

    iface->SetMode(MCInterfaceMode::None);
    SendKey(0x25);
    CHECK(iface->CurrentMode == MCInterfaceMode::Jump);
    SendKey(0x25, 0, false);

    iface->SetMode(MCInterfaceMode::None);
    SendKey(0x24);
    CHECK(iface->CurrentMode == MCInterfaceMode::None);
    SendKey(0x24, 0, false);

    SendKey(0x24, KeyShift);
    CHECK(iface->CurrentMode == MCInterfaceMode::Guard);
    SendKey(0x24, KeyShift, false);

    MCKeyBindings::SetPlayer(MCKeyBindings::Defaults());
}

/// <summary>
/// A click on open ground orders the selection by the mode: walk there (no mode), run there, attack the point with
/// the range of an attack mode; a jump needs jump jets, which mission 1's Uller W has none of.
/// </summary>
TEST_CASE_ISOLATED("game: a click on the ground moves, runs or attacks the point by the mode")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
    const auto sinkPin = std::make_shared<MCRecordingOrderSink>();
    MCRecordingOrderSink& sink = *sinkPin;
    iface->SetOrderSink(sinkPin);
    std::vector<int32_t> force = SelectWholeForce();
    std::ranges::sort(force);
    REQUIRE(!force.empty());
    const auto [x, y] = ViewCentre();

    auto clickGround = [&](MCInterfaceMode mode)
    {
        sink.Orders.clear();
        iface->SetMode(mode);
        iface->MouseTarget = MCMouseTarget::Nothing;
        iface->MouseObject.reset();
        Click(x, y);
    };

    {
        MCTest::Scope scope("walk");
        clickGround(MCInterfaceMode::None);
        CHECK(OrderedMovers(sink, MCTacticalOrderCode::MoveToPoint) == force);

        for (const MCRecordedOrder& recorded : sink.Orders)
        {
            CHECK_EQ(recorded.Order.MoveParams.WayPath.Mode[0], uint8_t{0});
            CHECK_EQ(recorded.Order.MoveParams.WayPath.NumPoints, 1);
        }
    }

    {
        MCTest::Scope scope("run");
        clickGround(MCInterfaceMode::Run);
        CHECK(OrderedMovers(sink, MCTacticalOrderCode::MoveToPoint) == force);

        for (const MCRecordedOrder& recorded : sink.Orders)
        {
            CHECK_EQ(recorded.Order.MoveParams.WayPath.Mode[0], uint8_t{1});
        }
    }

    {
        MCTest::Scope scope("attack the point from long range");
        clickGround(MCInterfaceMode::AttackLongRange);
        CHECK(OrderedMovers(sink, MCTacticalOrderCode::AttackPoint) == force);

        for (const MCRecordedOrder& recorded : sink.Orders)
        {
            CHECK_EQ(recorded.Order.AttackParams.Range, 2);
        }
    }

    {
        MCTest::Scope scope("lay mines");
        clickGround(MCInterfaceMode::LayMines);
        CHECK(OrderedMovers(sink, MCTacticalOrderCode::MoveToPoint) == force);

        for (const MCRecordedOrder& recorded : sink.Orders)
        {
            CHECK_EQ(recorded.Order.MoveParams.Mode, 1);
        }
    }

    {
        MCTest::Scope scope("no jump jets, no jump");
        CHECK(!iface->CanSelectionJump());
        CHECK(!iface->CanSelectionJumpTo(iface->MouseWorldPos, nullptr, false));
        clickGround(MCInterfaceMode::Jump);
        CHECK(sink.Orders.empty());
    }

    CHECK_EQ(sink.ServerOrders, 0);
    iface->SetOrderSink(nullptr);
}

/// <summary>
/// F and ctrl+F pick the lay-mines mode for one selected mine layer (a ground vehicle with mines left), as MCX.EXE
/// tests class 3 and GroundVehicle's mineLayer and minesToLay; a mech, a plain vehicle, two movers or an empty layer
/// get no mode. The boot skips logistics, where the player buys a mine layer, so the scenario's first ground vehicle is
/// made one as a mine layer's type makes it (mines to lay, hence a layer).
/// </summary>
TEST_CASE_ISOLATED("game: F picks the lay-mines mode for one selected mine layer only")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(5));
    std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
    REQUIRE(iface != nullptr);
    std::shared_ptr<MCGroundVehicle> layer;

    for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId && layer == nullptr; partId++)
    {
        if (std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
            mover != nullptr && mover->ObjectClass == MCObjectClass::GroundVehicle)
        {
            layer = std::static_pointer_cast<MCGroundVehicle>(mover);
        }
    }

    std::shared_ptr<MCMover> mech;

    for (const std::shared_ptr<MCFriendlyMechIcon>& button : iface->MechBar->Buttons)
    {
        if (const auto mover = std::static_pointer_cast<MCMover>(LockObject(button->Mover));
            button->Active && mover != nullptr && mover->ObjectClass == MCObjectClass::BattleMech)
        {
            mech = mover;
            break;
        }
    }

    REQUIRE(layer != nullptr);
    REQUIRE(mech != nullptr);
    REQUIRE(!layer->MineLayer);

    // The selection as the key reads it (SelectMech refuses the scenario's vehicles, which start disabled).
    auto select = [&](std::initializer_list<int32_t> partIds)
    {
        iface->ClearMechSelection();
        iface->SelectedMovers.assign(partIds);
        iface->SetMode(MCInterfaceMode::None);
    };

    constexpr int16_t KeyF = 0x21;

    {
        MCTest::Scope scope("F with a vehicle that lays no mines");
        select({layer->PartId});
        SendKey(KeyF);
        CHECK(iface->CurrentMode == MCInterfaceMode::None);
        SendKey(KeyF, 0, false);
    }

    layer->MinesToLay = 10;
    layer->MineLayer = true;

    {
        MCTest::Scope scope("F with the mine layer");
        select({layer->PartId});
        SendKey(KeyF);
        CHECK(iface->CurrentMode == MCInterfaceMode::LayMines);
        SendKey(KeyF, 0, false);
        CHECK(iface->CurrentMode == MCInterfaceMode::None);
    }

    {
        MCTest::Scope scope("ctrl+F with the mine layer");
        select({layer->PartId});
        SendKey(KeyF, KeyCtrl);
        CHECK(iface->CurrentMode == MCInterfaceMode::LayMines);
        SendKey(KeyF, KeyCtrl, false);
    }

    {
        MCTest::Scope scope("F with a mech");
        select({mech->PartId});
        SendKey(KeyF);
        CHECK(iface->CurrentMode == MCInterfaceMode::None);
        SendKey(KeyF, 0, false);
    }

    {
        MCTest::Scope scope("F with the mine layer and a mech");
        select({layer->PartId, mech->PartId});
        SendKey(KeyF);
        CHECK(iface->CurrentMode == MCInterfaceMode::None);
        SendKey(KeyF, 0, false);
    }

    {
        MCTest::Scope scope("F with a mine layer out of mines");
        layer->MinesToLay = 0;
        select({layer->PartId});
        SendKey(KeyF);
        CHECK(iface->CurrentMode == MCInterfaceMode::None);
        SendKey(KeyF, 0, false);
    }

    iface->ClearMechSelection();
}

/// <summary>
/// Clicking one of the player's movers selects it alone; shift adds another and takes a selected one out; F1 selects
/// the first lance; the selection never passes twelve movers.
/// </summary>
TEST_CASE_ISOLATED("game: clicks and lance keys select movers; shift adds and removes")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
    const auto sinkPin = std::make_shared<MCRecordingOrderSink>();
    MCRecordingOrderSink& sink = *sinkPin;
    iface->SetOrderSink(sinkPin);
    const auto [x, y] = ViewCentre();
    std::vector<std::shared_ptr<MCMover>> movers;

    for (const std::shared_ptr<MCFriendlyMechIcon>& button : iface->MechBar->Buttons)
    {
        if (const auto mover = std::static_pointer_cast<MCMover>(LockObject(button->Mover));
            button->Active && mover != nullptr)
        {
            movers.push_back(mover);
        }
    }

    REQUIRE(movers.size() >= 2);

    auto clickMover = [&](std::shared_ptr<MCMover> mover, bool shift)
    {
        iface->SetMode(MCInterfaceMode::None);
        iface->MouseTarget = MCMouseTarget::OwnMover;
        iface->MouseObject = mover;
        Click(x, y, shift);
    };

    iface->ClearMechSelection();
    clickMover(movers[0], false);
    CHECK((iface->SelectedMovers == std::vector<int32_t>{movers[0]->PartId}));
    CHECK(iface->CommandParser->IsSubject(movers[0]->PartId));

    clickMover(movers[1], true);
    CHECK(iface->IsSelected(movers[0]->PartId));
    CHECK(iface->IsSelected(movers[1]->PartId));
    CHECK(iface->CommandParser->IsSubject(movers[1]->PartId));

    clickMover(movers[0], true);
    CHECK(!iface->IsSelected(movers[0]->PartId));
    CHECK(!iface->CommandParser->IsSubject(movers[0]->PartId));
    CHECK(iface->IsSelected(movers[1]->PartId));

    // A plain click starts over with the one mover.
    clickMover(movers[0], false);
    CHECK((iface->SelectedMovers == std::vector<int32_t>{movers[0]->PartId}));
    CHECK(sink.Orders.empty());

    // F1: the first lance, mover by mover (it isn't linked), when it has any.
    std::shared_ptr<MCMoverGroup> lance = HomeCommander()->GetGroup(0);

    if (lance->NumMovers() > 0)
    {
        SendKey(0x3b);
        CHECK(iface->CommandParser->IsSubject(*lance));

        for (const std::shared_ptr<MCMover>& member : lance->GetMovers())
        {
            CHECK(iface->IsSelected(member->PartId));
        }
    }

    // SelectMech leaves repeats to its callers (they ask IsSelected first); the selection stops at twelve.
    for (int32_t round = 0; round < 20; round++)
    {
        iface->SelectMech(movers[round % movers.size()]->PartId);
    }

    CHECK_EQ(iface->SelectedMovers.size(), MCTacticalInterface::MaxSelectedMovers);
    iface->ClearMechSelection();
    CHECK(!iface->AnySelected());
    CHECK(iface->CommandParser->Subjects.empty());
    iface->SetOrderSink(nullptr);
}

/// <summary>
/// The mouse state picks the cursor by what the mouse is over and the mode: over open ground the move (or run)
/// cursor where the ground is passable, the attack mode's cursor in an attack mode, the plain one with nothing
/// selected; over one of the player's movers, its kind (an own mover) is seen and the plain cursor shows.
/// </summary>
TEST_CASE_ISOLATED("game: the mouse state picks the cursor for what is under the mouse")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
    std::shared_ptr<MCGuiObject> pane = MainHolder()->GetActivePane();
    REQUIRE(pane != nullptr);

    auto hover = [&](int32_t x, int32_t y)
    {
        MCGuiEvent event;
        event.Clear();
        event.Type = 7;
        event.X = x;
        event.Y = y;
        event.Target = pane;
        iface->UpdateMouseState(event);
        return static_cast<MCInterfaceCursor>(GuiSystem().CurrentCursor);
    };

    // A spot of open ground: the first point of a coarse grid with nothing under it.
    std::optional<std::pair<int32_t, int32_t>> ground;
    std::optional<std::pair<int32_t, int32_t>> ownMover;

    iface->ClearMechSelection();
    iface->SetMode(MCInterfaceMode::None);

    for (int32_t y = pane->GlobalY() + 8; y < pane->GlobalY() + pane->Height() - 8 && !ownMover.has_value(); y += 6)
    {
        for (int32_t x = pane->GlobalX() + 8; x < pane->GlobalX() + pane->Width() - 8; x += 6)
        {
            hover(x, y);

            if (!ground.has_value() && iface->MouseTarget == MCMouseTarget::Nothing)
            {
                ground = std::pair(x, y);
            }

            if (!ownMover.has_value() && iface->MouseTarget == MCMouseTarget::OwnMover)
            {
                ownMover = std::pair(x, y);
            }
        }
    }

    REQUIRE(ground.has_value());
    const auto [groundX, groundY] = *ground;

    // Nothing selected: the plain cursor.
    iface->ClearMechSelection();
    iface->SetMode(MCInterfaceMode::None);
    CHECK(hover(groundX, groundY) == MCInterfaceCursor::Normal);

    SelectWholeForce();
    REQUIRE(iface->AnySelected(true));
    const MCInterfaceCursor walk = hover(groundX, groundY);
    CHECK(walk == MCInterfaceCursor::Move || walk == MCInterfaceCursor::Forbidden);

    if (walk == MCInterfaceCursor::Move)
    {
        iface->SetMode(MCInterfaceMode::Run);
        CHECK(hover(groundX, groundY) == MCInterfaceCursor::Run);
    }

    const std::pair<MCInterfaceMode, MCInterfaceCursor> attacks[] = {
        {MCInterfaceMode::AttackLongRange, MCInterfaceCursor::LongRange},
        {MCInterfaceMode::AttackMediumRange, MCInterfaceCursor::MediumRange},
        {MCInterfaceMode::AttackShortRange, MCInterfaceCursor::ShortRange},
        {MCInterfaceMode::AttackFromPosition, MCInterfaceCursor::FromPosition},
        {MCInterfaceMode::AttackOptimalRange, MCInterfaceCursor::OptimalRange},
        {MCInterfaceMode::Guard, MCInterfaceCursor::Guard},
    };

    for (const auto& [mode, cursor] : attacks)
    {
        iface->SetMode(mode);
        CHECK_EQ(static_cast<int32_t>(hover(groundX, groundY)), static_cast<int32_t>(cursor));
    }

    // Aimed shots over open ground: a plain attack.
    iface->SetMode(MCInterfaceMode::AimHead);
    CHECK(hover(groundX, groundY) == MCInterfaceCursor::Attack);

    // The camera-follow mode always shows the plain cursor.
    iface->SetMode(MCInterfaceMode::CameraFollow);
    CHECK(hover(groundX, groundY) == MCInterfaceCursor::Normal);

    if (ownMover.has_value())
    {
        // Over one of the player's movers with no mode: the plain cursor; with the guard mode, guard it.
        iface->SetMode(MCInterfaceMode::None);
        CHECK(hover(ownMover->first, ownMover->second) == MCInterfaceCursor::Normal);
        CHECK(iface->MouseTarget == MCMouseTarget::OwnMover);
        iface->SetMode(MCInterfaceMode::Guard);
        CHECK(hover(ownMover->first, ownMover->second) == MCInterfaceCursor::Guard);
    }

    iface->ClearMechSelection();
}

/// <summary>
/// The arrows scroll the map while held, two of them diagonally; letting go of one leaves the other scrolling, and
/// another key's key-up doesn't stop them (OB-174). Ctrl+arrows do the same for the tactical map.
/// </summary>
TEST_CASE_ISOLATED("game: held arrow keys scroll together, diagonally, until each is let go (OB-174)")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
    REQUIRE(iface != nullptr);

    // The directions run clockwise from up: 0 up, 2 right, 4 down, 6 left, the odd ones between.
    constexpr int16_t up = 0x148;
    constexpr int16_t down = 0x150;
    constexpr int16_t left = 0x14b;
    constexpr int16_t right = 0x14d;
    CHECK_EQ(iface->ScrollDirection, -1);

    SendKey(up);
    CHECK_EQ(iface->ScrollDirection, 0);
    SendKey(left);
    CHECK_EQ(iface->ScrollDirection, 7);

    // Another key's key-up leaves the arrows held.
    SendKey(0x17, 0, false);
    CHECK_EQ(iface->ScrollDirection, 7);

    SendKey(up, 0, false);
    CHECK_EQ(iface->ScrollDirection, 6);
    SendKey(down);
    CHECK_EQ(iface->ScrollDirection, 5);
    SendKey(right);
    CHECK_EQ(iface->ScrollDirection, 4);
    SendKey(left, 0, false);
    CHECK_EQ(iface->ScrollDirection, 3);
    SendKey(down, 0, false);
    CHECK_EQ(iface->ScrollDirection, 2);
    SendKey(up);
    CHECK_EQ(iface->ScrollDirection, 1);

    // A key let go with ctrl down is still that arrow.
    SendKey(up, KeyCtrl, false);
    SendKey(right, 0, false);
    CHECK_EQ(iface->ScrollDirection, -1);

    // Ctrl+arrows: the tactical map, the same way.
    SendKey(down, KeyCtrl);
    SendKey(right, KeyCtrl);
    CHECK_EQ(iface->TacScrollDirection, 3);
    CHECK_EQ(iface->ScrollDirection, -1);
    SendKey(down, KeyCtrl, false);
    CHECK_EQ(iface->TacScrollDirection, 2);

    // Losing focus lets go of every scroll key.
    SendKey(up);
    iface->ReleaseScrollKeys();
    CHECK_EQ(iface->ScrollDirection, -1);
    CHECK_EQ(iface->TacScrollDirection, -1);
    SendKey(left);
    CHECK_EQ(iface->ScrollDirection, 6);
}

TEST_CASE_ISOLATED("game: edge scrolling follows the window's new size (a window made full screen)")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const std::shared_ptr<MCDisplay> display = GuiSystem().Display();
    REQUIRE(display != nullptr);

    // The mouse scrolls once it is past these edges: one pixel in at the top left, four at the bottom right.
    constexpr int32_t grownWidth = 1600;
    constexpr int32_t grownHeight = 1000;
    REQUIRE(SDL_SetWindowSize(&display->Window(), grownWidth, grownHeight));
    SDL_SyncWindow(&display->Window());
    MCTestGame::RunFrame(1.0f / 15.0f);

    int32_t width = 0;
    int32_t height = 0;
    display->WindowScreenSize(width, height);
    REQUIRE_EQ(GuiSystem().Width(), width);
    REQUIRE_EQ(GuiSystem().Height(), height);
    const tagRECT area = GuiSystem().ScrollRect;
    CHECK_EQ(area.left, 1);
    CHECK_EQ(area.top, 1);
    CHECK_EQ(area.right, width - 4);
    CHECK_EQ(area.bottom, height - 4);
}

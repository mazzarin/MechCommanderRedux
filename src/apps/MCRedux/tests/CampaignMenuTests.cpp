#include "stdafx.h"
#include "MCTest.h"
#include "MCMenusPictures.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCBriefingText.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCDeploy.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCPurchaseScreen.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCUnitLimits.h"
#include "gui/MCUpdateDisplay.h"
#include "main/MCGamePaths.h"
#include "main/MCLogPart.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "platform/MCRenderer.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "ui/MCRmlChecks.h"
#include "ui/MCRmlMenus.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

// Port-only (SPEC-menus.md's stage 8): the campaign's screens remade, BRIEFING and DEPLOY (logistics/MCCampaignMenu),
// over the 1998 briefing, with DEPLOY's rules the 1998 briefing's own (logistics/MCDeploy).

namespace
{
    constexpr float FlowFrame = 1.0f / 60.0f;
    constexpr int32_t MostChangeFrames = 600;
    /// <summary>RmlUi's clock moved on before a picture: past every motion (the ramp's fill, a refusal's reason).</summary>
    constexpr double SettleSeconds = 3.0;

    /// <summary>As brf0101.txt writes it: its codes, a title, objectives carried on a line, the fire support, warnings.</summary>
    constexpr std::string_view FirstBriefing = "%fm%fc1%c****CLASSIFIED****%n\r\n"
                                               "%fs%fc4%cFEDCOM:BEACHHEAD/01PRDN01%n\r\n"
                                               "%fc5%c----------%n\r\n"
                                               "%fc6%c***LIBERATE WORK CAMPS***%n\r\n"
                                               "%fc21-Destroy Camp \"Alpha\" HQ and%n\r\n"
                                               "%t9Natural Gas Tower%n\r\n"
                                               "2-Destroy all three Garrison Buildings at%n\r\n"
                                               "%t9the Garrison Base%n%n\r\n"
                                               "%fc6FIRE SUPPORT:%n\r\n"
                                               "%fc2%t12ARTILLERY: 4 Small  1 Large%n\r\n"
                                               "%t12SENSOR PROBE: 0%n\r\n"
                                               "%t12CAMERA DRONE: 2%n\r\n"
                                               "%fc5%c----------%n\r\n"
                                               "%fc4The Smoke Jaguars have been putting dissidents into camps.%n%n\r\n"
                                               "At %fc2\"Alpha\"%fc4, destroy  the Camp HQ.%n%n\r\n"
                                               "%fc1%fm%c**TACTICAL WARNING**%n\r\n"
                                               "%fc2%fs*Automated turret defenses reported in%n\r\n"
                                               "%t9area.%n\r\n"
                                               "%fc2*Clan ULLER OmniMech reported in area.%n%n\r\n"
                                               "%fc6<<<<END DISPATCH\r\n";

    /// <summary>Frames until the change of screen running (and one queued behind it) is done.</summary>
    void RunWhileChanging()
    {
        for (int32_t pass = 0; pass < 2; pass++)
        {
            for (int32_t frame = 0; frame < MostChangeFrames && MCScreenChange::Running(); frame++)
            {
                MCTestGame::RunFrame(FlowFrame);
            }

            MCTestGame::RunFrame(FlowFrame);
        }
    }

    /// <summary>The remade screen, which must show.</summary>
    MCCampaignScreen& Shown()
    {
        REQUIRE(MCCampaignMenu::Screen().has_value());
        REQUIRE(MCCampaignMenu::Shown());
        return MCCampaignMenu::Screen()->get();
    }

    Rml::Element& ElementOf(const MCRmlScreen& screen, const std::string& id)
    {
        REQUIRE(screen.Document().has_value());
        Rml::Element* element = screen.Document()->get().GetElementById(id);
        REQUIRE(element != nullptr);
        return *element;
    }

    Rml::Vector2f Middle(const Rml::Element& element)
    {
        auto& shown = const_cast<Rml::Element&>(element);
        return shown.GetAbsoluteOffset(Rml::BoxArea::Border) + shown.GetBox().GetSize(Rml::BoxArea::Border) / 2.0f;
    }

    /// <summary>A click of <paramref name="button"/> (SDL's) in the middle of <paramref name="element"/>.</summary>
    void ClickOn(const Rml::Element& element, uint8_t button = SDL_BUTTON_LEFT)
    {
        const Rml::Vector2f at = Middle(element);
        MCScreenInput::PixelMove(at.x, at.y, FlowFrame);
        MCScreenInput::PixelButton(button, true, at.x, at.y, FlowFrame);
        MCScreenInput::PixelButton(button, false, at.x, at.y, FlowFrame);
    }

    /// <summary>A left drag from <paramref name="from"/>'s middle to <paramref name="to"/>'s, held there for a frame.</summary>
    void DragTo(const Rml::Element& from, const Rml::Element& to, bool release = true)
    {
        constexpr int32_t Steps = 4;
        const Rml::Vector2f start = Middle(from);
        const Rml::Vector2f end = Middle(to);
        MCScreenInput::PixelMove(start.x, start.y, FlowFrame);
        MCScreenInput::PixelButton(SDL_BUTTON_LEFT, true, start.x, start.y, FlowFrame);

        for (int32_t step = 1; step <= Steps; step++)
        {
            const Rml::Vector2f at = start + (end - start) * (static_cast<float>(step) / Steps);
            MCScreenInput::PixelMove(at.x, at.y, FlowFrame);
        }

        if (release)
        {
            MCScreenInput::PixelButton(SDL_BUTTON_LEFT, false, end.x, end.y, FlowFrame);
        }
    }

    /// <summary>A new campaign's first briefing with the remade screens, logistics up.</summary>
    void StartFirstBriefing()
    {
        MCCampaignMenu::Remade() = true;
        REQUIRE(MCTestGame::StartLogistics());
        RunWhileChanging();
        NewCampaign();
        RunWhileChanging();
    }

    /// <summary>
    /// The window's picture once settled, its hash named by <paramref name="what"/> and its size ("deploy 640x480:
    /// 0x..."), saved with <c>--shots</c>.
    /// </summary>
    std::string DrawnWindow(std::string_view what)
    {
        constexpr int32_t SettleFrames = 4;
        const std::shared_ptr<MCDisplay> display = MCInput::Display();
        REQUIRE(display != nullptr);

        for (int32_t frame = 0; frame < SettleFrames; frame++)
        {
            MCTestGame::RunFrame(FlowFrame);
        }

        const auto window = display->ReadWindow();
        REQUIRE(window.has_value());

        if (const std::optional<std::filesystem::path> path =
                MCMenusPictures::ShotPath(std::format("campaign_{}_{}x{}", what, window->Width, window->Height));
            path.has_value())
        {
            REQUIRE(display->SaveWindowScreenshot(*path).has_value());
        }

        return std::format("{} {}x{}: {:#010x}", what, window->Width, window->Height,
                           MCMenusPictures::PictureHash(*window));
    }

    /// <summary>The window's picture saved (with --shots) as <paramref name="name"/> at its size.</summary>
    void Shot(std::string_view name)
    {
        // The hash isn't checked here: the tests that check it draw on the software renderer.
        static_cast<void>(DrawnWindow(name));
    }

    /// <summary>
    /// READY AREA's first unit carried by the mouse over drop slot <paramref name="slot"/> with one ton too few free:
    /// refused there, as deploy_b_heavy's frame.
    /// </summary>
    void HoldTooHeavy(const MCCampaignScreen& screen, size_t slot)
    {
        const std::shared_ptr<MCLogPart> held = MCDeploy::ReadyUnits()[0];
        MaxDeployTonnage = CurDeployTonnage + static_cast<int32_t>(held->CurTonnage) - 1;
        DragTo(ElementOf(screen, "ready0"), ElementOf(screen, std::format("slot{}", slot)), false);
        REQUIRE(screen.Carried() == held);
    }

    /// <summary>The screen's BRIEFING showing the campaign's briefing <paramref name="file"/> (a file in the game's missions).</summary>
    void ShowBriefing(MCCampaignScreen& screen, std::string_view file)
    {
        GlobalLog()->BriefingTextPath = GamePath(MissionPath, file);
        screen.SetUp(MCCampaignScreen::MCTab::Briefing);
        MCTestGame::RunFrame(FlowFrame);
    }
}

TEST_CASE("logistics: a briefing's text is read into its objectives, fire support, orders and warnings")
{
    const MCBriefingText::MCRead read = MCBriefingText::Read(FirstBriefing);
    REQUIRE_EQ(read.Objectives.size(), size_t{2});
    CHECK_EQ(read.Objectives[0], std::string("Destroy Camp \"Alpha\" HQ and Natural Gas Tower"));
    CHECK_EQ(read.Objectives[1], std::string("Destroy all three Garrison Buildings at the Garrison Base"));
    REQUIRE(read.FireSupport.has_value());
    CHECK((*read.FireSupport ==
           MCBriefingText::MCFireSupport{.SmallArtillery = 4, .LargeArtillery = 1, .CameraDrones = 2}));
    CHECK_EQ(MCBriefingText::FireSupportText(*read.FireSupport),
             std::string("4 small artillery, 1 large artillery, 2 camera drones"));
    CHECK_EQ(MCBriefingText::FireSupportText({}), std::string("None"));
    REQUIRE_EQ(read.Orders.size(), size_t{2});
    CHECK_EQ(read.Orders[0], std::string("The Smoke Jaguars have been putting dissidents into camps."));
    CHECK_EQ(read.Orders[1], std::string("At \"Alpha\", destroy the Camp HQ."));
    CHECK_EQ(read.WarningsHead, std::string("TACTICAL WARNING"));
    REQUIRE_EQ(read.Warnings.size(), size_t{2});
    CHECK_EQ(read.Warnings[0], std::string("Automated turret defenses reported in area."));
    CHECK_EQ(read.Warnings[1], std::string("Clan ULLER OmniMech reported in area."));

    // A match's side: no fire support, nothing after the dispatch's end.
    const MCBriefingText::MCRead side = MCBriefingText::Read(
        "%fc6%c*** CAPTURE ENEMY BASE ***%n%fc21-Capture Enemy Headquarters%n%n"
        "%fc5%c----------%n%fc4Your team must capture the base.%n%n%fc6<<<<END DISPATCH%nNot read");
    CHECK_EQ(side.Objectives, std::vector<std::string>{"Capture Enemy Headquarters"});
    CHECK(!side.FireSupport.has_value());
    CHECK_EQ(side.Orders, std::vector<std::string>{"Your team must capture the base."});
    CHECK(side.Warnings.empty());
}

/// <summary>
/// A new campaign's first briefing shows the remade BRIEFING over the 1998 briefing (set up as ever, hidden, logistics'
/// screen still): the mission's operation and number over its title, its objectives, fire support, orders, warnings and
/// map, read from its briefing; Q and E change between BRIEFING and DEPLOY; Escape goes to the main menu. With
/// <c>--shots</c> each tab is saved at the three windows the frames are recorded at.
/// </summary>
TEST_CASE_ISOLATED("game: a new campaign's briefing shows the remade BRIEFING, read from the mission's briefing")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    MCCampaignScreen& screen = Shown();
    CHECK(logistics->CurrentScreen() == logistics->BriefingScreen);
    CHECK(logistics->BriefingScreen->ShowWindow == 0);
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > campaign"));
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Briefing);
    CHECK_EQ(screen.Eyebrow(), std::string("Beachhead · Mission 1"));
    CHECK_EQ(screen.Title(), std::string("Liberate work camps"));
    REQUIRE_EQ(screen.Objectives().size(), size_t{3});
    CHECK_EQ(screen.Objectives()[0].Text, std::string("Destroy Camp \"Alpha\" HQ and Natural Gas Tower"));
    CHECK_EQ(screen.Objectives()[2].Number, 3);
    CHECK_EQ(screen.FireSupport(), std::string("None"));
    CHECK_EQ(screen.Orders().size(), size_t{5});
    CHECK_EQ(screen.Warnings().size(), size_t{2});
    CHECK_EQ(screen.MapSource(), std::string("map:mis0101"));

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);

    for (const MCMenusPictures::MCWindowSize& size : MCMenusPictures::FrameWindows)
    {
        REQUIRE(MCMenusPictures::SetWindowPixels(*display, size.Width, size.Height));
        Shot("briefing");
    }

    // Q: DEPLOY (round from the first tab to the last); E: BRIEFING again.
    MCScreenInput::RealKey(SDL_SCANCODE_Q);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Deploy);
    CHECK_EQ(screen.ReadyCards().size(), MCDeploy::ReadyUnits().size());
    CHECK_EQ(screen.Groups().size(), MCDeploy::Groups);

    for (const MCMenusPictures::MCWindowSize& size : MCMenusPictures::FrameWindows)
    {
        REQUIRE(MCMenusPictures::SetWindowPixels(*display, size.Width, size.Height));
        Shot("deploy");
    }

    MCScreenInput::RealKey(SDL_SCANCODE_E);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Briefing);

    // E: the 1998 PURCHASING, through a change; its BRIEFING button back to the tab left on.
    MCScreenInput::RealKey(SDL_SCANCODE_E);
    RunWhileChanging();
    CHECK(!MCCampaignMenu::Shown());
    CHECK(logistics->CurrentScreen() == logistics->PurchaseScreen);
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > campaign"));
    logistics->SetUpBriefingScreen(true);
    RunWhileChanging();
    CHECK(MCCampaignMenu::Shown());
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Briefing);
    CHECK(logistics->CurrentScreen() == logistics->BriefingScreen);

    // From DEPLOY, Q: the 1998 'MECH BAY; back to DEPLOY.
    screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
    MCScreenInput::RealKey(SDL_SCANCODE_Q);
    RunWhileChanging();
    CHECK(!MCCampaignMenu::Shown());
    CHECK(logistics->CurrentScreen() == logistics->RepairScreen);
    logistics->SetUpBriefingScreen(true);
    RunWhileChanging();
    CHECK(MCCampaignMenu::Shown());
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Deploy);

    // Escape: the main menu.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    RunWhileChanging();
    CHECK(!MCCampaignMenu::Shown());
    CHECK(logistics->CurrentScreen() == logistics->MainScreen);
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main"));
}

/// <summary>
/// DEPLOY moves units by 1998's rules (MCDeploy): the right button sends a READY AREA card into the first open slot and
/// a slot's unit back; a left drag puts one in the slot it's dropped on, swapping with the one there; held over a slot
/// with too few tons free it's refused, the slot and its card red with the reason, and let go it goes back; BEGIN
/// MISSION is dark until a unit is in the drop.
/// </summary>
TEST_CASE_ISOLATED("game: DEPLOY sends, carries and refuses units as the 1998 briefing does")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
    MCTestGame::RunFrame(FlowFrame);
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    REQUIRE(logistics->LocalDropSlot[0]);
    REQUIRE(screen.ReadyCards().size() >= 2);
    CHECK(!screen.CanBegin());
    const int32_t startTons = CurDeployTonnage;
    const int32_t limit = MaxDeployTonnage;
    const std::shared_ptr<MCLogPart> first = MCDeploy::ReadyUnits()[0];
    const auto firstTons = static_cast<int32_t>(first->CurTonnage);

    // The right button: into the first open slot, and back.
    ClickOn(ElementOf(screen, "ready0"), SDL_BUTTON_RIGHT);
    CHECK(MCDeploy::SlotUnit(0) == first);
    CHECK_EQ(CurDeployTonnage, startTons + firstTons);
    CHECK(screen.CanBegin());
    CHECK_EQ(screen.WeightNow(), std::to_string(startTons + firstTons));
    ClickOn(ElementOf(screen, "slot0"), SDL_BUTTON_RIGHT);
    CHECK(MCDeploy::SlotUnit(0) == nullptr);
    CHECK_EQ(CurDeployTonnage, startTons);
    CHECK(first->Deployed == 0);

    // A left drag into slot 1 (open in the first mission's first group).
    REQUIRE(MCDeploy::Open(1));
    DragTo(ElementOf(screen, "ready0"), ElementOf(screen, "slot1"));
    CHECK(MCDeploy::SlotUnit(1) == first);
    CHECK(first->Deployed != 0);
    CHECK(screen.Carried() == nullptr);

    // Too heavy: held over slot 0, it's refused with the reason; let go, it goes back.
    const std::shared_ptr<MCLogPart> second = MCDeploy::ReadyUnits()[0];
    HoldTooHeavy(screen, 0);
    REQUIRE(!screen.Groups().empty());
    CHECK(screen.Groups()[0].Cards[0].Refuse);
    CHECK_EQ(screen.WeightFree(), std::string("1 ton over"));
    Shot("deploy_heavy");
    const Rml::Vector2f slot0 = Middle(ElementOf(screen, "slot0"));
    MCScreenInput::PixelButton(SDL_BUTTON_LEFT, false, slot0.x, slot0.y, FlowFrame);
    CHECK(screen.Carried() == nullptr);
    CHECK(MCDeploy::SlotUnit(0) == nullptr);
    CHECK(second->Deployed == 0);

    // The keys: SPACE picks READY AREA's first card up; RIGHT takes it to the drop's first open slot, DOWN to the next
    // open one in its group and UP back (the focus with it, the slot marked); SPACE puts it down there.
    MaxDeployTonnage = limit;
    REQUIRE(MCRmlMenus::Running().has_value());
    Rml::Context& context = MCRmlMenus::Running()->get().Menus().Context();
    const auto focused = [&context]
    { return context.GetFocusElement() != nullptr ? context.GetFocusElement()->GetId() : std::string(); };
    ElementOf(screen, "ready0").Focus();
    MCScreenInput::RealKey(SDL_SCANCODE_SPACE);
    MCTestGame::RunFrame(FlowFrame);
    const std::shared_ptr<MCLogPart> keyed = screen.Carried();
    REQUIRE(keyed == second);
    MCScreenInput::RealKey(SDL_SCANCODE_RIGHT);
    MCTestGame::RunFrame(FlowFrame);
    CHECK_EQ(focused(), std::string("slot0"));
    CHECK(screen.Groups()[0].Cards[0].Target);
    MCScreenInput::RealKey(SDL_SCANCODE_DOWN);
    MCTestGame::RunFrame(FlowFrame);
    CHECK_EQ(focused(), std::string("slot1"));
    CHECK(!screen.Groups()[0].Cards[0].Target);
    MCScreenInput::RealKey(SDL_SCANCODE_UP);
    MCTestGame::RunFrame(FlowFrame);
    CHECK_EQ(focused(), std::string("slot0"));
    MCScreenInput::RealKey(SDL_SCANCODE_SPACE);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(screen.Carried() == nullptr);
    CHECK(MCDeploy::SlotUnit(0) == keyed);
    CHECK(MCDeploy::SlotUnit(1) == first);
}

/// <summary>
/// BRIEFING, DEPLOY with a unit in the drop, and DEPLOY with a unit held too heavy over a slot, on the software
/// renderer, the whole window at 640x480, 1280x720 and 1280x1024 (the 1998 briefing map and the units' 1998 outlines and
/// pilots among it), the hashes recorded: the Mac's and Windows' alike with the game's data. With <c>--shots</c> each is
/// saved.
/// </summary>
TEST_CASE_ISOLATED("game: BRIEFING and DEPLOY draw the same on every machine")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    GRenderer = static_cast<int>(MCRendererKind::Software);
    StartFirstBriefing();
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    REQUIRE(display->RendererKind() == MCRendererKind::Software);
    MCCampaignScreen& screen = Shown();
    REQUIRE(MCRmlMenus::Running().has_value());
    double clock = 0.0;
    MCRmlMenus::Running()->get().SetClock([&clock] { return clock; });

    struct MCDrawn
    {
        std::string_view What;
        std::array<uint32_t, MCMenusPictures::FrameWindows.size()> Hashes;
    };

    constexpr std::array<MCDrawn, 3> Drawn = {{
        {"briefing", {0x68d30a18, 0x1de01535, 0x490e3adb}},
        {"deploy", {0xd9a96ad1, 0x84209082, 0xb5314cc1}},
        {"deploy_heavy", {0xce5965c8, 0x662c22f0, 0x95431918}},
    }};

    const auto drawEach = [&display, &clock](const MCDrawn& drawn)
    {
        for (size_t i = 0; i < MCMenusPictures::FrameWindows.size(); i++)
        {
            const MCMenusPictures::MCWindowSize& size = MCMenusPictures::FrameWindows[i];
            REQUIRE(MCMenusPictures::SetWindowPixels(*display, size.Width, size.Height));
            clock += SettleSeconds;
            CHECK_EQ(DrawnWindow(drawn.What),
                     std::format("{} {}x{}: {:#010x}", drawn.What, size.Width, size.Height, drawn.Hashes[i]));
        }
    };

    drawEach(Drawn[0]);
    screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
    screen.SendReady(0, std::nullopt);
    REQUIRE(MCDeploy::SlotUnit(0) != nullptr);
    drawEach(Drawn[1]);

    // Held: the carried card's place follows the window, so it's picked up again at each size.
    for (size_t i = 0; i < MCMenusPictures::FrameWindows.size(); i++)
    {
        const MCMenusPictures::MCWindowSize& size = MCMenusPictures::FrameWindows[i];
        REQUIRE(MCMenusPictures::SetWindowPixels(*display, size.Width, size.Height));
        MCTestGame::RunFrame(FlowFrame);
        HoldTooHeavy(screen, 1);
        clock += SettleSeconds;
        CHECK_EQ(DrawnWindow(Drawn[2].What),
                 std::format("{} {}x{}: {:#010x}", Drawn[2].What, size.Width, size.Height, Drawn[2].Hashes[i]));
        screen.PutBack();
        MCTestGame::RunFrame(FlowFrame);
    }
}

/// <summary>
/// BRIEFING and DEPLOY at every window the players' reports name, with the campaigns' fullest briefings (the expansion's
/// six objectives and all four kinds of fire support, xbrf0302; its six warnings, xbrf0103) and DEPLOY with a unit held
/// too heavy: every element inside the window, no two siblings overlapping, no text spilling or cut
/// (MCRmlChecks::BoxProblems); every text at least 8 units high, at its contrast (MCRmlChecks::Illegible).
/// </summary>
TEST_CASE_ISOLATED("game: BRIEFING and DEPLOY lay out and read at every window the players' reports name")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    REQUIRE(MCRmlMenus::Running().has_value());
    MCRmlMenus& menus = MCRmlMenus::Running()->get();

    const auto checkEach = [&menus](std::string_view what)
    {
        for (const MCMenusPictures::MCWindowSize& window : MCMenusPictures::PlayersWindows)
        {
            menus.SetWindow(window.Width, window.Height);
            menus.Frame();
            // The screen's frame at this size (a card carried by the keys follows the focus), laid out again.
            menus.Menus().Tend();
            menus.Frame();
            const float unit = MCRmlMenus::MenusUnit(window.Width, window.Height);
            const std::string where = std::format("{} {}x{}: ", what, window.Width, window.Height);
            CHECK_EQ(where + MCRmlChecks::BoxProblems(menus.Menus().Context()), where);
            CHECK_EQ(where + MCRmlChecks::Illegible(menus.Menus().Context(), unit), where);
        }
    };

    ShowBriefing(screen, "xbrf0302.txt");
    REQUIRE_EQ(screen.Objectives().size(), size_t{6});
    CHECK_EQ(screen.FireSupport(),
             std::string("4 small artillery, 2 large artillery, 3 sensor probes, 2 camera drones"));
    checkEach("briefing xbrf0302");
    ShowBriefing(screen, "xbrf0103.txt");
    CHECK_EQ(screen.Warnings().size(), size_t{6});
    checkEach("briefing xbrf0103");

    screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
    screen.SendReady(0, std::nullopt);
    MCTestGame::RunFrame(FlowFrame);
    checkEach("deploy");

    // Held by the keys over slot 1 with a ton too few free.
    const std::shared_ptr<MCLogPart> held = MCDeploy::ReadyUnits()[0];
    MaxDeployTonnage = CurDeployTonnage + static_cast<int32_t>(held->CurTonnage) - 1;
    screen.PickUpReady(0, true);
    // A frame: the open slots take the focus now (the keys' carry).
    MCTestGame::RunFrame(FlowFrame);
    ElementOf(screen, "slot1").Focus();
    screen.HoldOver(1);
    REQUIRE(screen.Carried() == held);
    REQUIRE(!screen.Groups().empty());
    CHECK(screen.Groups()[0].Cards[1].Refuse);
    checkEach("deploy held");
    screen.PutBack();
}

/// <summary>
/// BEGIN MISSION: dark and refused with no unit in the drop (Betty's line, nothing else); with one, the loading screen
/// comes up over the remade screen, which never shows under it, and the mission plays with logistics gone.
/// </summary>
TEST_CASE_ISOLATED("game: DEPLOY's BEGIN MISSION goes to the loading screen and the mission")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(!screen.CanBegin());
    ClickOn(ElementOf(screen, "begin"));
    RunWhileChanging();
    CHECK(MCCampaignMenu::Shown());
    CHECK(!LoadingScreenUp);

    // Enter on READY AREA's first card: into the first open slot (ENTER as the right button).
    Rml::Element& first = ElementOf(screen, "ready0");
    first.Focus();
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(MCDeploy::SlotUnit(0) != nullptr);
    CHECK(screen.CanBegin());

    constexpr int32_t MostFrames = 600;
    const auto playing = [] { return Scenario() != nullptr && Mission()->State == MCMissionState::Scenario; };
    ClickOn(ElementOf(screen, "begin"));
    bool loadingSeen = false;
    bool shownUnderLoading = false;

    for (int32_t frame = 0; frame < MostFrames && !playing(); frame++)
    {
        MCTestGame::RunFrame(FlowFrame);
        loadingSeen = loadingSeen || LoadingScreenUp;
        shownUnderLoading = shownUnderLoading || (LoadingScreenUp && MCCampaignMenu::Shown());
    }

    CHECK(loadingSeen);
    CHECK(!shownUnderLoading);
    REQUIRE(playing());
    CHECK(!MCCampaignMenu::Shown());
    CHECK(GlobalLog() == nullptr);
}

#include "stdafx.h"
#include "MCTest.h"
#include "MCMenusPictures.h"
#include "MCTestGuest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "lib/MCFile.h"
#include "logistics/MCBriefingText.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCChatView.h"
#include "logistics/MCDeploy.h"
#include "logistics/MCLoadingMenu.h"
#include "logistics/MCLogChat.h"
#include "logistics/MCLogisticsTabs.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMainMenuScreen.h"
#include "logistics/MCMatchMaps.h"
#include "logistics/MCMechBay.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCOperationVideo.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCRoomScreen.h"
#include "logistics/MCSavesMenu.h"
#include "logistics/MCStore.h"
#include "logistics/MCUnitLimits.h"
#include "gui/MCUpdateDisplay.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCGameSession.h"
#include "main/MCGameStrings.h"
#include "main/MCInventoryList.h"
#include "main/MCLogMech.h"
#include "main/MCLogMechList.h"
#include "main/MCLogPart.h"
#include "main/MCLogVehicleList.h"
#include "main/MCLogWarriorList.h"
#include "main/MCLogistics.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "linkup/MCFidpMessage.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCLinkupMessages.h"
#include "linkup/MCSessionManager.h"
#include "main/MCMPPlayerLights.h"
#include "network/MCGameDataHash.h"
#include "network/MCMultiPlayer.h"
#include "network/MCMultiPlayerHandlers.h"
#include "platform/MCDirectPlay.h"
#include "platform/MCRenderer.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "ui/MCRmlChecks.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlMenus.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include "object/MCMasterComponent.h"
#include "logistics/MCMenuDialog.h"
#include "main/MCLogisticsShared.h"

// Port-only (SPEC-menus.md's stage 8): the campaign's screens remade, BRIEFING and DEPLOY (logistics/MCCampaignMenu),
// with DEPLOY's rules the 1998 briefing's own (logistics/MCDeploy).

namespace
{
    constexpr float FlowFrame = 1.0f / 60.0f;
    constexpr int32_t MostChangeFrames = 600;
    /// <summary>RmlUi's clock moved on before a picture: past every motion (the ramp's fill, a refusal's reason).</summary>
    constexpr double SettleSeconds = 3.0;
    /// <summary>The pictures' resource points (the first mission gives none; enough for the buy window's).</summary>
    constexpr int32_t ShotPoints = 48600;

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

    /// <summary>The store's first good in stock (its parts: the first mission's store has some sold out).</summary>
    size_t FirstInStore(const MCLogisticsTabs& tabs)
    {
        const auto inStore =
            std::ranges::find_if(tabs.StoreGoods(), [](const MCLogisticsTabs::MCGood& good) { return !good.Off; });
        REQUIRE(inStore != tabs.StoreGoods().end());
        return static_cast<size_t>(inStore - tabs.StoreGoods().begin());
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

    /// <summary>The frames' window sizes' hashes of a picture (640x480, 1280x720, 1280x1024).</summary>
    using MCFrameHashes = std::array<uint32_t, MCMenusPictures::FrameWindows.size()>;

    /// <summary>
    /// The window drawn as <paramref name="what"/> at each of the frames' sizes, RmlUi's clock moved past every motion
    /// first, checked against <paramref name="hashes"/>.
    /// </summary>
    void DrawnEach(MCDisplay& display, double& clock, std::string_view what, const MCFrameHashes& hashes)
    {
        for (size_t i = 0; i < MCMenusPictures::FrameWindows.size(); i++)
        {
            const MCMenusPictures::MCWindowSize& size = MCMenusPictures::FrameWindows[i];
            REQUIRE(MCMenusPictures::SetWindowPixels(display, size.Width, size.Height));
            clock += SettleSeconds;
            CHECK_EQ(DrawnWindow(what), std::format("{} {}x{}: {:#010x}", what, size.Width, size.Height, hashes[i]));
        }
    }

    /// <summary>
    /// At every window the players' reports name: every element inside the window, no two siblings overlapping, no text
    /// spilling or cut (MCRmlChecks::BoxProblems); every text at least 8 units high, at its contrast (Illegible).
    /// </summary>
    void CheckLaidOut(MCRmlMenus& menus, std::string_view what)
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
    }

    /// <summary>
    /// At every window the players' reports name, each time limit BRIEFING shows (the mission's beside OBJECTIVES, an
    /// objective's beside its words) on one line, its right edge at its row's: what fault has crept back where not, as
    /// "<window>: <words> <lines high>, <units off the right>; " (WIP 5 wrapped "MISSION TIME / LIMIT 15:00").
    /// </summary>
    void CheckTimeLimitsOneLine(MCRmlMenus& menus, const MCCampaignScreen& screen, std::string_view what, size_t limits)
    {
        constexpr float MostLines = 1.5f;
        constexpr float EdgeSlack = 0.5f;
        REQUIRE(screen.Document().has_value());

        for (const MCMenusPictures::MCWindowSize& window : MCMenusPictures::PlayersWindows)
        {
            menus.SetWindow(window.Width, window.Height);
            menus.Frame();
            Rml::ElementList shown;
            screen.Document()->get().QuerySelectorAll(shown, ".time-limit");
            std::erase_if(shown, [](Rml::Element* limit) { return !limit->IsVisible(true); });
            const std::string where = std::format("{} {}x{}: ", what, window.Width, window.Height);
            CHECK_EQ(where + std::to_string(shown.size()), where + std::to_string(limits));
            std::string faults;

            for (Rml::Element* limit : shown)
            {
                const float lines = limit->GetBox().GetSize(Rml::BoxArea::Content).y / limit->GetLineHeight();
                Rml::Element& row = *limit->GetParentNode();
                const float rowRight =
                    row.GetAbsoluteOffset(Rml::BoxArea::Content).x + row.GetBox().GetSize(Rml::BoxArea::Content).x;
                const float right =
                    limit->GetAbsoluteOffset(Rml::BoxArea::Border).x + limit->GetBox().GetSize(Rml::BoxArea::Border).x;

                if (lines > MostLines || std::abs(rowRight - right) > EdgeSlack)
                {
                    faults += std::format("{} {:.1f}, {:.1f}; ", limit->GetInnerRML(), lines, rowRight - right);
                }
            }

            CHECK_EQ(where + faults, where);
        }
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

    /// <summary>
    /// The first briefing's operation video opened by Space, run until it has shown <paramref name="frames"/> frames
    /// (Betty's line first); the window.
    /// </summary>
    MCOperationVideoWindow& PlayVideoTo(uint32_t frames)
    {
        constexpr int32_t MostFrames = 2400;
        MCScreenInput::RealKey(SDL_SCANCODE_SPACE);
        MCTestGame::RunFrame(FlowFrame);
        REQUIRE(MCVideoMenu::Shown());
        MCOperationVideoWindow& window = MCVideoMenu::Window()->get();
        REQUIRE(window.Video().has_value());

        for (int32_t frame = 0; frame < MostFrames && window.Video()->get().Shown() < frames; frame++)
        {
            MCTestGame::RunFrame(FlowFrame);
        }

        REQUIRE(window.Video().has_value());
        REQUIRE_EQ(window.Video()->get().Shown(), frames);
        return window;
    }

    /// <summary>The drop zone tags BRIEFING's map shows, in the document's order (F1 to F3).</summary>
    std::vector<Rml::Element*> ZoneTagsShown(const MCCampaignScreen& screen)
    {
        REQUIRE(screen.Document().has_value());
        Rml::ElementList tags;
        screen.Document()->get().QuerySelectorAll(tags, "#map .zone-tag");
        std::erase_if(tags, [](Rml::Element* tag) { return !tag->IsVisible(true); });
        return tags;
    }

    /// <summary>The screen's BRIEFING showing the campaign's briefing <paramref name="file"/> (a file in the game's missions).</summary>
    void ShowBriefing(MCCampaignScreen& screen, std::string_view file)
    {
        GlobalLog()->BriefingTextPath = GamePath(MissionPath, file);
        screen.SetUp(MCCampaignScreen::MCTab::Briefing);
        MCTestGame::RunFrame(FlowFrame);
    }

    /// <summary>
    /// The mission begun, past its start-up until units move (as MCTestGame::StartMission waits): the world's part of
    /// the screen (clear of the interface: the panels at the left, the mech bar at the bottom), in palette indices, shows
    /// the mission's ground, no one colour over half of it and the dark round the map (index 16, the game's black: all
    /// a camera scrolled off the map shows) a tenth at most.
    /// </summary>
    void CheckWorldShown(std::string_view what)
    {
        constexpr int32_t StartedTurn = 30;
        constexpr int32_t MostStartFrames = 3000;
        constexpr uint8_t Dark = 0x10;
        constexpr size_t DarkMostShare = 10;

        for (int32_t frame = 0; frame < MostStartFrames && Turn <= StartedTurn; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        REQUIRE(Turn > StartedTurn);
        const MCWindow& screen = ScreenPort()->Bitmap();
        const int32_t width = screen.XMax + 1;
        const int32_t height = screen.YMax + 1;
        const std::vector<uint8_t> shown = MCScreenInput::ShownScreen();
        REQUIRE_EQ(shown.size(), static_cast<size_t>(width) * static_cast<size_t>(height));
        std::array<size_t, 256> counts{};
        size_t pixels = 0;

        for (int32_t y = height / 4; y < height - height / 8; y++)
        {
            for (int32_t x = width / 3; x < width; x++)
            {
                counts[shown[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)]]++;
                pixels++;
            }
        }

        const auto commonest = std::ranges::max_element(counts);
        std::cout << std::format("  {}: of the world's {} pixels, {} index {} (the commonest), {} the dark\n", what,
                                 pixels, *commonest, commonest - counts.begin(), counts[Dark]);
        MCScreenInput::SaveShot(std::format("campaign_world_{}", what), MCScreenInput::ScreenHash());
        CHECK(*commonest * 2 < pixels);
        CHECK(counts[Dark] * DarkMostShare < pixels);
    }
}

TEST_CASE("logistics: a briefing's text is read into its objectives, fire support, orders and warnings")
{
    const MCBriefingText::MCRead read = MCBriefingText::Read(FirstBriefing);
    REQUIRE_EQ(read.Objectives.size(), size_t{2});
    CHECK_EQ(read.Objectives[0].Text, std::string("Destroy Camp \"Alpha\" HQ and Natural Gas Tower"));
    CHECK_EQ(read.Objectives[1].Text, std::string("Destroy all three Garrison Buildings at the Garrison Base"));
    CHECK(read.TimeLimit.empty());
    REQUIRE(read.FireSupport.has_value());
    CHECK((*read.FireSupport ==
           MCBriefingText::MCFireSupport{.SmallArtillery = 4, .LargeArtillery = 1, .CameraDrones = 2}));
    const std::vector<std::string> fireLines{"4 small artillery", "1 large artillery", "2 camera drones"};
    CHECK_EQ(MCBriefingText::FireSupportLines(*read.FireSupport), fireLines);
    CHECK_EQ(MCBriefingText::FireSupportLines({}), std::vector<std::string>{"None"});
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
    CHECK((side.Objectives == std::vector<MCBriefingText::MCObjective>{{.Text = "Capture Enemy Headquarters"}}));
    CHECK(!side.FireSupport.has_value());
    CHECK_EQ(side.Orders, std::vector<std::string>{"Your team must capture the base."});
    CHECK(side.Warnings.empty());
}

/// <summary>
/// A centred line of briefing text too long for 1998's briefing pane reads whole, its last character once: the remade
/// BRIEFING lays the words out itself, from MCBriefingText's parts. 1998's pane wrote such a line from its left edge and
/// drew the character that overflowed twice (MCSmuti's OB-070, gone from the briefing with the 1998 screens).
/// </summary>
TEST_CASE("logistics: an over-long centred line of briefing text reads whole, its last character once")
{
    constexpr std::string_view Long = "Your lance must capture the enemy base before the Clan reinforcements reach the "
                                      "landing zone at dawn, holding the headquarters until the DropShip lands.";
    const MCBriefingText::MCRead read =
        MCBriefingText::Read(std::format("%fc6%c*** CAPTURE ENEMY BASE ***%n%fc21-Capture Enemy Headquarters%n%n"
                                         "%fc5%c----------%n%fc4%c{}%n%n%fc6<<<<END DISPATCH%n",
                                         Long));
    CHECK_EQ(read.Orders, std::vector<std::string>{std::string(Long)});
}

/// <summary>
/// A briefing's time limits read apart from its objectives' words, in each way the campaigns write them (every briefing
/// surveyed, 25 Nov): the mission's under the objectives (brf0204's "MISSION TIME LIMIT- 15:00") or after the fire
/// support (xbrf0201's "TIME LIMIT: 25 MINUTES", xbrf0303's "***25 MINUTE TIME LIMIT***"), an objective's own under it
/// (brf0206's "OBJECTIVE TIME LIMIT- 06:00"). An order that speaks of the time limit stays an order. In WIP 5 the
/// mission's was read as the objective's last words and wrapped with them ("MISSION TIME / LIMIT- 15:00").
/// </summary>
TEST_CASE("logistics: a briefing's time limits read apart from its objectives, as each campaign writes them")
{
    using MCBriefingText::MCObjective;
    constexpr std::string_view Head =
        "%fm%fc1%c****CLASSIFIED****%n%fc5%c----------%n%fc6%c***DESTROY ENEMY ASSETS***%n";
    constexpr std::string_view Fire = "%fc6FIRE SUPPORT:%n%fc2%t12ARTILLERY: 2 Small  0 Large%n%t12SENSOR PROBE: 1%n";
    constexpr std::string_view Orders = "%fc5%c----------%n%fc4Destroy them before the time limit expires.%n%n"
                                        "%fc6<<<<END DISPATCH%n";

    const MCBriefingText::MCRead mission = MCBriefingText::Read(
        std::format("{}%fc21-Destroy Smoke Jaguar Hyperpulse%n%t9Generators%nMISSION TIME LIMIT- 15:00%n%n{}{}", Head,
                    Fire, Orders));
    CHECK((mission.Objectives == std::vector<MCObjective>{{.Text = "Destroy Smoke Jaguar Hyperpulse Generators"}}));
    CHECK_EQ(mission.TimeLimit, std::string("15:00"));
    CHECK_EQ(mission.Orders, std::vector<std::string>{"Destroy them before the time limit expires."});

    const MCBriefingText::MCRead objective = MCBriefingText::Read(std::format(
        "{}%fc21-Capture Enemy Base HQ%n%t9OBJECTIVE TIME LIMIT- 06:00%n%fc22-Defend Captured Base HQ%n%n{}{}", Head,
        Fire, Orders));
    CHECK((objective.Objectives == std::vector<MCObjective>{{.Text = "Capture Enemy Base HQ", .TimeLimit = "6:00"},
                                                            {.Text = "Defend Captured Base HQ"}}));
    CHECK(objective.TimeLimit.empty());

    const MCBriefingText::MCRead minutes = MCBriefingText::Read(
        std::format("{}%fc21-Destroy the Convoy%n%n{}%n%fm%fc1%cTIME LIMIT: 25 MINUTES%n{}", Head, Fire, Orders));
    CHECK_EQ(minutes.TimeLimit, std::string("25:00"));
    CHECK((minutes.Objectives == std::vector<MCObjective>{{.Text = "Destroy the Convoy"}}));

    const MCBriefingText::MCRead starred = MCBriefingText::Read(
        std::format("{}%fc21-Destroy the Convoy%n%n{}%fc5%c----------%n%fm%fc1%c***25 MINUTE TIME LIMIT***%n{}", Head,
                    Fire, Orders));
    CHECK_EQ(starred.TimeLimit, std::string("25:00"));
    CHECK_EQ(starred.Orders, std::vector<std::string>{"Destroy them before the time limit expires."});
}

/// <summary>
/// A match's drop zone tag (tools/upscale/zone_tags.py's pictures, 39 by 19 map pixels) hangs its tail's tip on its
/// zone: from the bottom right, as 1998's flags, wherever that keeps it on the map's picture; near the left edge from
/// the bottom left, near the top from the top right, in the top left corner from the top left; hard by the right edge,
/// where none fits, still the bottom right (the least outside). Its picture is its group's in its tail's corner.
/// </summary>
TEST_CASE("logistics: a match's drop zone tag hangs its tail on its zone, mirrored at the map's edges")
{
    using MCCampaignMenu::MCTail;
    using MCCampaignMenu::MCZoneTag;
    constexpr float Side = 283.0f;

    struct MCCase
    {
        std::string_view Name;
        float X;
        float Y;
        MCTail Tail;
        std::string_view Source;
    };

    constexpr std::array<MCCase, 6> Cases = {{
        {"the middle", 140.0f, 140.0f, MCTail::BottomRight, "art/zone_tag_F1_br.png"},
        {"the left edge", 10.0f, 140.0f, MCTail::BottomLeft, "art/zone_tag_F1_bl.png"},
        {"the top edge", 140.0f, 6.0f, MCTail::TopRight, "art/zone_tag_F1_tr.png"},
        {"the top left corner", 10.0f, 6.0f, MCTail::TopLeft, "art/zone_tag_F1_tl.png"},
        {"the bottom edge", 140.0f, 281.0f, MCTail::BottomRight, "art/zone_tag_F1_br.png"},
        {"hard by the right edge", 282.5f, 140.0f, MCTail::BottomRight, "art/zone_tag_F1_br.png"},
    }};

    for (const MCCase& each : Cases)
    {
        const MCTest::Scope scope{std::string(each.Name)};
        const MCZoneTag tag = MCCampaignMenu::ZoneTag(1, each.X, each.Y, Side, Side);
        CHECK(tag.Tail == each.Tail);
        CHECK_EQ(MCCampaignMenu::ZoneTagSource(tag), std::string(each.Source));
        CHECK_EQ(tag.Group, 1);
        CHECK_EQ(tag.X, each.X);
        CHECK_EQ(tag.Y, each.Y);
        CHECK_EQ(tag.Width, 39.0f);
        CHECK_EQ(tag.Height, 19.0f);

        // Inside the picture wherever a corner fits.
        if (each.Name != Cases.back().Name)
        {
            CHECK(tag.Left >= 0.0f && tag.Top >= 0.0f && tag.Left + tag.Width <= Side && tag.Top + tag.Height <= Side);
        }
    }

    // The tip: the bottom right's at its picture's (37.25, 17.25), the top left's at (0.5, 0.25).
    const MCZoneTag middle = MCCampaignMenu::ZoneTag(2, 140.0f, 140.0f, Side, Side);
    CHECK_EQ(middle.Left, 140.0f - 37.25f);
    CHECK_EQ(middle.Top, 140.0f - 17.25f);
    CHECK_EQ(MCCampaignMenu::ZoneTagSource(middle), std::string("art/zone_tag_F2_br.png"));
    const MCZoneTag corner = MCCampaignMenu::ZoneTag(3, 10.0f, 6.0f, Side, Side);
    CHECK_EQ(corner.Left, 10.0f - 0.5f);
    CHECK_EQ(corner.Top, 6.0f - 0.25f);
    CHECK_EQ(MCCampaignMenu::ZoneTagSource(corner), std::string("art/zone_tag_F3_tl.png"));
}

/// <summary>
/// A new campaign's first briefing shows the remade BRIEFING: the mission's operation and number over its title, its
/// objectives, fire support, orders, warnings and map, read from its briefing; Q and E change between BRIEFING and DEPLOY; Escape goes to the main menu. With
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
    CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > campaign"));
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Briefing);
    CHECK_EQ(screen.Eyebrow(), std::string("Beachhead · Mission 1"));
    CHECK_EQ(screen.Title(), std::string("Liberate work camps"));
    REQUIRE_EQ(screen.Objectives().size(), size_t{3});
    CHECK_EQ(screen.Objectives()[0].Text, std::string("Destroy Camp \"Alpha\" HQ and Natural Gas Tower"));
    CHECK_EQ(screen.Objectives()[2].Number, 3);
    REQUIRE_EQ(screen.FireSupport().size(), size_t{1});
    CHECK_EQ(screen.FireSupport()[0].Text, std::string("None"));
    CHECK_EQ(screen.Orders().size(), size_t{5});
    CHECK_EQ(screen.Warnings().size(), size_t{2});
    CHECK_EQ(screen.MapSource(), std::string("map:mis0101"));
    // The campaign's map has its own F1 painted: no drop zone tags over it.
    CHECK(screen.ZoneMap().Tags.empty());
    CHECK(screen.ZonesShown().empty());
    CHECK(ZoneTagsShown(screen).empty());

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

    // E: PURCHASING, the same screen (no change of screens).
    MCScreenInput::RealKey(SDL_SCANCODE_E);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(MCCampaignMenu::Shown());
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Purchasing);
    CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > campaign"));
    MCLogisticsTabs& tabs = screen.Logistics();
    CHECK_EQ(tabs.ForceCards().size(),
             logistics->ForceMechList->Mechs.size() + logistics->ForceVehicleList->Vehicles.size());
    CHECK_EQ(tabs.StoreGoods().size(), logistics->PurMechList->Mechs.size());
    tabs.Pick(MCLogisticsTabs::MCPlace::Store, 0);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(!tabs.DetailsName().empty());

    for (const MCMenusPictures::MCWindowSize& size : MCMenusPictures::FrameWindows)
    {
        REQUIRE(MCMenusPictures::SetWindowPixels(*display, size.Width, size.Height));
        Shot("purchase");
    }

    tabs.ChooseKind(MCLogisticsTabs::MCKind::Components);
    ResourcePoints = ShotPoints;
    tabs.Pick(MCLogisticsTabs::MCPlace::Store, FirstInStore(tabs));
    MCTestGame::RunFrame(FlowFrame);
    Shot("purchase_parts");
    tabs.PressDeal();
    MCTestGame::RunFrame(FlowFrame);
    CHECK(tabs.DealOpen());
    Shot("purchase_buy");
    tabs.DealCancel();
    tabs.ChooseKind(MCLogisticsTabs::MCKind::Mechs);

    // E: the 'MECH BAY, on the force's first 'Mech.
    MCScreenInput::RealKey(SDL_SCANCODE_E);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::MechBay);
    CHECK(tabs.LabUnit() == logistics->ForceMechList->Mechs.front());
    CHECK(!tabs.LabName().empty());

    for (const MCMenusPictures::MCWindowSize& size : MCMenusPictures::FrameWindows)
    {
        REQUIRE(MCMenusPictures::SetWindowPixels(*display, size.Width, size.Height));
        Shot("mechbay");
    }

    // From DEPLOY, Q: the 'MECH BAY; Q again, PURCHASING.
    screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
    MCScreenInput::RealKey(SDL_SCANCODE_Q);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::MechBay);
    MCScreenInput::RealKey(SDL_SCANCODE_Q);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Purchasing);

    // Escape: the main menu.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    RunWhileChanging();
    CHECK(!MCCampaignMenu::Shown());
    CHECK(MCMenuScreens::Top() == MCMenuScreens::Main());
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
        MCFrameHashes Hashes;
    };

    constexpr std::array<MCDrawn, 3> Drawn = {{
        {"briefing", {0x90f15c0d, 0x82e3cd26, 0xbdcabc1b}},
        {"deploy", {0x51e41acf, 0xef3d9420, 0x6fe719cf}},
        {"deploy_heavy", {0x0e66cfce, 0x250a7a16, 0x7cf73106}},
    }};

    const auto drawEach = [&display, &clock](const MCDrawn& drawn)
    { DrawnEach(*display, clock, drawn.What, drawn.Hashes); };

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

    const auto checkEach = [&menus](std::string_view what) { CheckLaidOut(menus, what); };

    ShowBriefing(screen, "xbrf0302.txt");
    REQUIRE_EQ(screen.Objectives().size(), size_t{6});
    // The fire support a line each, so no line breaks inside one.
    const std::vector<std::string> fire = [&screen]
    {
        std::vector<std::string> lines;

        for (const MCCampaignScreen::MCLine& line : screen.FireSupport())
        {
            lines.push_back(line.Text);
        }

        return lines;
    }();
    const std::vector<std::string> fireLines{"4 small artillery", "2 large artillery", "3 sensor probes",
                                             "2 camera drones"};
    CHECK_EQ(fire, fireLines);
    checkEach("briefing xbrf0302");
    ShowBriefing(screen, "xbrf0103.txt");
    CHECK_EQ(screen.Warnings().size(), size_t{6});
    checkEach("briefing xbrf0103");

    // The time limits: the mission's (brf0204's 15 minutes) and an objective's (brf0206's first, 6 minutes).
    ShowBriefing(screen, "brf0204.txt");
    CHECK_EQ(screen.TimeLimit(), std::string("15:00"));
    CheckTimeLimitsOneLine(menus, screen, "briefing brf0204", 1);
    checkEach("briefing brf0204");
    ShowBriefing(screen, "brf0206.txt");
    REQUIRE(!screen.Objectives().empty());
    CHECK_EQ(screen.Objectives()[0].Limit, std::string("6:00"));
    CheckTimeLimitsOneLine(menus, screen, "briefing brf0206", 1);
    checkEach("briefing brf0206");

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
/// as designed comes up in the briefing's place, which never shows under it: the mission's operation and number, its
/// name, objectives and the orders' first two paragraphs, its meter filling as the load goes; it's gone before the
/// battlefield, and the mission plays with logistics gone.
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
    bool remadeLoading = false;
    int mostPercent = 0;

    for (int32_t frame = 0; frame < MostFrames && !playing(); frame++)
    {
        MCTestGame::RunFrame(FlowFrame);
        loadingSeen = loadingSeen || LoadingScreenUp;
        shownUnderLoading = shownUnderLoading || (LoadingScreenUp && MCCampaignMenu::Shown());

        if (MCLoadingMenu::Shown())
        {
            remadeLoading = true;
            mostPercent = std::max(mostPercent, MCLoadingMenu::Screen()->get().Percent());
        }
    }

    CHECK(loadingSeen);
    CHECK(!shownUnderLoading);
    CHECK(remadeLoading);
    CHECK(mostPercent > 0);
    REQUIRE(playing());
    CHECK(!MCCampaignMenu::Shown());
    CHECK(!MCLoadingMenu::Shown());
    CHECK(GlobalLog() == nullptr);
    REQUIRE(MCLoadingMenu::Screen().has_value());
    const MCLoadingScreen& loading = MCLoadingMenu::Screen()->get();
    CHECK_EQ(loading.Eyebrow(), std::string("Operation 1 · Beachhead · Mission 1"));
    CHECK_EQ(loading.Title(), std::string("Liberate work camps"));
    CHECK_EQ(loading.Objectives().size(), size_t{3});
    CHECK_EQ(loading.Orders().size(), MCLoadingMenu::OrdersShown);
    CHECK_EQ(loading.MapSource(), std::string("image(map:mis0101 contain center top)"));
}

namespace
{
    /// <summary>
    /// The rows of the screen as shown that are 1998's loading static: every pixel one of the static's 32 indices, many
    /// of them (a row of noise has nearly all 32; a plain row, one or two).
    /// </summary>
    size_t StaticRows()
    {
        constexpr uint8_t StaticIndices = 0x20;
        constexpr size_t NoiseLeast = 16;
        const MCWindow& screen = ScreenPort()->Bitmap();
        const auto width = static_cast<size_t>(screen.XMax + 1);
        const std::vector<uint8_t> shown = MCScreenInput::ShownScreen();
        size_t rows = 0;

        for (size_t start = 0; start + width <= shown.size(); start += width)
        {
            const std::span<const uint8_t> row(shown.data() + start, width);
            std::array<bool, StaticIndices> seen{};
            bool noise = true;

            for (const uint8_t pixel : row)
            {
                if (pixel >= StaticIndices)
                {
                    noise = false;
                    break;
                }

                seen[pixel] = true;
            }

            rows += noise && static_cast<size_t>(std::ranges::count(seen, true)) >= NoiseLeast ? 1 : 0;
        }

        return rows;
    }
}

/// <summary>
/// A load with no menus before it (-mission's game segment, as -benchmark, the staged battle, the trailer's recorder and
/// the tests' StartMission begin): the loading screen as designed shows over every frame of the load, read from the
/// mission's own files with no logistics (the operation and number, the name, objectives, orders and map), its meter
/// filling; 1998's static, connect shape and bar aren't drawn under it; it's gone before the battlefield.
/// </summary>
TEST_CASE_ISOLATED("game: a mission begun with no menus shows the loading screen as designed, without 1998's static")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    size_t loadFrames = 0;
    size_t shownFrames = 0;
    size_t staticRows = 0;
    int mostPercent = 0;
    MCTestGame::OnPresent = [&]
    {
        if (!LoadingScreenUp)
        {
            return;
        }

        loadFrames++;
        staticRows += StaticRows();

        if (MCLoadingMenu::Shown())
        {
            shownFrames++;
            mostPercent = std::max(mostPercent, MCLoadingMenu::Screen()->get().Percent());
        }
    };

    const bool started = MCTestGame::StartMission(1);
    MCTestGame::OnPresent = {};
    REQUIRE(started);
    CHECK(loadFrames > 0);
    CHECK_EQ(shownFrames, loadFrames);
    CHECK_EQ(staticRows, size_t{0});
    CHECK(mostPercent > 0);
    CHECK(!LoadingScreenUp);
    CHECK(!MCLoadingMenu::Shown());
    CHECK(GlobalLog() == nullptr);
    REQUIRE(MCLoadingMenu::Screen().has_value());
    const MCLoadingScreen& loading = MCLoadingMenu::Screen()->get();
    CHECK_EQ(loading.Eyebrow(), std::string("Operation 1 · Beachhead · Mission 1"));
    CHECK_EQ(loading.Title(), std::string("Liberate work camps"));
    CHECK_EQ(loading.Objectives().size(), size_t{3});
    CHECK_EQ(loading.Orders().size(), MCLoadingMenu::OrdersShown);
    CHECK_EQ(loading.MapSource(), std::string("image(map:mis0101 contain center top)"));
}

/// <summary>
/// The loading screen as designed, shown from the first briefing with its meter at 62%: on the software renderer, the
/// whole window at 640x480, 1280x720 and 1280x1024, the hashes recorded; at every window the players' reports name,
/// laid out and legible (MCRmlChecks). With <c>--shots</c> each picture is saved.
/// </summary>
TEST_CASE_ISOLATED("game: the loading screen draws the same on every machine and lays out at every window")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr int ShownPercent = 62;
    GRenderer = static_cast<int>(MCRendererKind::Software);
    StartFirstBriefing();
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    REQUIRE(display->RendererKind() == MCRendererKind::Software);
    MCCampaignMenu::Hide();
    MCLoadingMenu::Show();
    REQUIRE(MCLoadingMenu::Shown());
    MCLoadingMenu::Progress(ShownPercent);
    CHECK_EQ(MCLoadingMenu::Screen()->get().Percent(), ShownPercent);
    constexpr std::array<uint32_t, MCMenusPictures::FrameWindows.size()> Hashes = {0xeeb50e22, 0xd2457be9, 0xb00d4e12};

    for (size_t i = 0; i < MCMenusPictures::FrameWindows.size(); i++)
    {
        const MCMenusPictures::MCWindowSize& size = MCMenusPictures::FrameWindows[i];
        REQUIRE(MCMenusPictures::SetWindowPixels(*display, size.Width, size.Height));
        CHECK_EQ(DrawnWindow("loading"), std::format("loading {}x{}: {:#010x}", size.Width, size.Height, Hashes[i]));
    }

    REQUIRE(MCRmlMenus::Running().has_value());
    MCRmlMenus& menus = MCRmlMenus::Running()->get();

    for (const MCMenusPictures::MCWindowSize& window : MCMenusPictures::PlayersWindows)
    {
        menus.SetWindow(window.Width, window.Height);
        menus.Frame();
        const float unit = MCRmlMenus::MenusUnit(window.Width, window.Height);
        const std::string where = std::format("loading {}x{}: ", window.Width, window.Height);
        CHECK_EQ(where + MCRmlChecks::BoxProblems(menus.Menus().Context()), where);
        CHECK_EQ(where + MCRmlChecks::Illegible(menus.Menus().Context(), unit), where);
    }

    MCLoadingMenu::Hide();
}

/// <summary>
/// BRIEFING's operation video (a campaign mission's, OPVID1 here: 176x128, 732 frames at 15 a second): Space opens it
/// over the briefing, after Betty's "incoming transmission"; its frames come as the clock reaches them, how far along
/// under it; Space holds it (no frame comes, its time stays) and lets it go on; Q and E change no tab under it; Escape
/// closes it, back to BRIEFING as it was; the mouse's PLAY and CLOSE do the same.
/// </summary>
TEST_CASE_ISOLATED("game: BRIEFING's PLAY opens the operation's video, Space holds it and Escape closes it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr uint32_t FramesPlayed = 30;
    constexpr int32_t HeldFrames = 60;
    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    REQUIRE(screen.HasVideo());
    // Before its first frame (Betty's line), the operation's picture (held there, saved with --shots).
    screen.PlayVideo();
    REQUIRE(MCVideoMenu::Shown());
    MCVideoMenu::Window()->get().TogglePause();
    Shot("video_incoming");
    REQUIRE(MCVideoMenu::Window()->get().Video().has_value());
    CHECK_EQ(MCVideoMenu::Window()->get().Video()->get().Shown(), 0u);
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(!MCVideoMenu::Shown());
    MCOperationVideoWindow& window = PlayVideoTo(FramesPlayed);
    const MCOperationVideo& video = window.Video()->get();
    CHECK_EQ(video.Width(), 176);
    CHECK_EQ(video.Height(), 128);
    CHECK_EQ(video.Frames(), 732u);
    CHECK(!video.Incoming());
    CHECK(video.Picture() != nullptr);
    CHECK_EQ(window.TimeText(), std::string("0:01 of 0:49"));
    CHECK_EQ(window.Percent(), 4);

    // Space holds it, and lets it go on.
    MCScreenInput::RealKey(SDL_SCANCODE_SPACE);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(video.Paused());
    const uint32_t held = video.Shown();

    for (int32_t frame = 0; frame < HeldFrames; frame++)
    {
        MCTestGame::RunFrame(FlowFrame);
    }

    CHECK_EQ(video.Shown(), held);
    MCScreenInput::RealKey(SDL_SCANCODE_SPACE);

    for (int32_t frame = 0; frame < HeldFrames; frame++)
    {
        MCTestGame::RunFrame(FlowFrame);
    }

    CHECK(!video.Paused());
    CHECK(video.Shown() > held);

    // Q and E: no tab changes under it.
    MCScreenInput::RealKey(SDL_SCANCODE_Q);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(MCVideoMenu::Shown());
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Briefing);

    // Escape closes it: BRIEFING as it was, and Escape there still the way to the main menu.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(!MCVideoMenu::Shown());
    CHECK(!window.Video().has_value());
    CHECK(MCCampaignMenu::Shown());
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Briefing);
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > campaign"));

    // The mouse: PLAY, then CLOSE.
    ClickOn(ElementOf(screen, "play"));
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(MCVideoMenu::Shown());
    ClickOn(ElementOf(MCVideoMenu::Window()->get(), "close"));
    MCTestGame::RunFrame(FlowFrame);
    CHECK(!MCVideoMenu::Shown());
    CHECK(MCCampaignMenu::Shown());
}

/// <summary>
/// OB-191 on the remade BRIEFING: PLAY pressed again at once, as a quick second click comes, plays the operation's video
/// once, from its start, and on. A second click where PLAY is lands on the video window's veil; PLAY's own press while
/// the window shows (MCCampaignScreen::PlayVideo, as a click or Space reaching it would) leaves the video as it is, at
/// once and once its frames come; CLOSE at once leaves BRIEFING with nothing to start later. 1998's operation tab armed a
/// start for each click, the later one stopping the movie the first had started, and the mission tab clicked just after
/// left a start waiting, which flipped back to the movie (OB-191, fixed on the 1998 briefing and gone with it).
/// </summary>
TEST_CASE_ISOLATED(
    "game: BRIEFING's PLAY pressed again at once plays the operation's video once, from its start, and on "
    "(OB-191)")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr uint32_t FramesPlayed = 30;
    constexpr int32_t MostFrames = 2400;
    constexpr int32_t PlayOnFrames = 30;
    constexpr int32_t LaterFrames = 180;
    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    REQUIRE(screen.HasVideo());
    const Rml::Element& play = ElementOf(screen, "play");
    const auto playing = []() -> const MCOperationVideo*
    {
        const MCOptionalRef<MCOperationVideoWindow> window = MCVideoMenu::Window();

        if (!window.has_value() || !window->get().Shown() || !window->get().Video().has_value())
        {
            return nullptr;
        }

        return &window->get().Video()->get();
    };

    // Two clicks on PLAY, the second at once: one video, from its start.
    ClickOn(play);
    const MCOperationVideo* const video = playing();
    REQUIRE(video != nullptr);
    ClickOn(play);
    REQUIRE_EQ(playing(), video);
    CHECK(!video->Paused());

    // PLAY's press again at once: the same video, not started again.
    const uint32_t started = video->Shown();
    screen.PlayVideo();
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE_EQ(playing(), video);
    CHECK(video->Shown() >= started);

    // Its frames come; PLAY's press while they do: it plays on, none shown again.
    for (int32_t frame = 0; frame < MostFrames && video->Shown() < FramesPlayed; frame++)
    {
        MCTestGame::RunFrame(FlowFrame);
    }

    REQUIRE_EQ(video->Shown(), FramesPlayed);
    screen.PlayVideo();

    for (int32_t frame = 0; frame < PlayOnFrames; frame++)
    {
        MCTestGame::RunFrame(FlowFrame);
    }

    REQUIRE_EQ(playing(), video);
    CHECK(video->Shown() > FramesPlayed);
    CHECK(!video->Paused());

    // CLOSE, then PLAY and Escape at once: BRIEFING, and nothing starts later.
    ClickOn(ElementOf(MCVideoMenu::Window()->get(), "close"));
    REQUIRE(playing() == nullptr);
    screen.PlayVideo();
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(playing() != nullptr);
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);

    for (int32_t frame = 0; frame < LaterFrames; frame++)
    {
        MCTestGame::RunFrame(FlowFrame);
    }

    CHECK(!MCVideoMenu::Shown());
    CHECK(MCCampaignMenu::Shown());
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Briefing);
}

/// <summary>
/// The operation's video held at its sixtieth frame over the briefing: on the software renderer, the whole window at
/// 640x480, 1280x720 and 1280x1024, the hashes recorded; at every window the players' reports name, laid out and
/// legible (MCRmlChecks). With <c>--shots</c> each picture is saved.
/// </summary>
TEST_CASE_ISOLATED("game: the operation's video draws the same on every machine and lays out at every window")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr uint32_t HeldAt = 60;
    GRenderer = static_cast<int>(MCRendererKind::Software);
    StartFirstBriefing();
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    REQUIRE(display->RendererKind() == MCRendererKind::Software);
    static_cast<void>(Shown());
    MCOperationVideoWindow& window = PlayVideoTo(HeldAt);
    window.TogglePause();
    REQUIRE(window.Video()->get().Paused());
    REQUIRE(MCRmlMenus::Running().has_value());
    MCRmlMenus& menus = MCRmlMenus::Running()->get();
    double clock = 0.0;
    menus.SetClock([&clock] { return clock; });
    constexpr std::array<uint32_t, MCMenusPictures::FrameWindows.size()> Hashes = {0xa83abd2c, 0x8b3dc24f, 0x2c1ebcbb};

    for (size_t i = 0; i < MCMenusPictures::FrameWindows.size(); i++)
    {
        const MCMenusPictures::MCWindowSize& size = MCMenusPictures::FrameWindows[i];
        REQUIRE(MCMenusPictures::SetWindowPixels(*display, size.Width, size.Height));
        clock += SettleSeconds;
        CHECK_EQ(DrawnWindow("video"), std::format("video {}x{}: {:#010x}", size.Width, size.Height, Hashes[i]));
    }

    for (const MCMenusPictures::MCWindowSize& size : MCMenusPictures::PlayersWindows)
    {
        menus.SetWindow(size.Width, size.Height);
        menus.Frame();
        const float unit = MCRmlMenus::MenusUnit(size.Width, size.Height);
        const std::string where = std::format("video {}x{}: ", size.Width, size.Height);
        CHECK_EQ(where + MCRmlChecks::BoxProblems(menus.Menus().Context()), where);
        CHECK_EQ(where + MCRmlChecks::Illegible(menus.Menus().Context(), unit), where);
    }

    window.Close();
}

/// <summary>
/// A match's briefing (a LAN game's, its name STEEL RAIN): Escape asks "Exit multiplayer?" (1998's string) as
/// dialog_exit_mp has it, the game's name above it, opening on CANCEL; CANCEL keeps the briefing; EXIT leaves the match
/// for the LAN games it came from, MULTIPLAYER ACCESS and the main menu under them (1998 went to the main menu).
/// </summary>
TEST_CASE_ISOLATED("game: a match's briefing asks before leaving, and leaves for the games list it came from")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    auto game = std::make_shared<MCMultiPlayer>();
    MCGameContext::Current().SetMultiPlayer(game);
    REQUIRE_EQ(game->Start(), 0);
    game->SessionManager->CurrentConnection = MCNetProtocol::TcpIp;
    game->SessionName = "STEEL RAIN";

    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(MCRmlDialogs::Shown());
    const MCGuiHandle<MCRmlDialog> dialog = MCRmlDialogs::Dialog();
    REQUIRE(dialog);
    CHECK_EQ(dialog->Said().Eyebrow, std::string("STEEL RAIN"));
    CHECK_EQ(dialog->Said().Heading, std::string("Exit multiplayer?"));
    CHECK_EQ(dialog->Said().Main, std::string("Exit"));
    CHECK_EQ(dialog->Said().Other, std::string("Cancel"));
    const Rml::Element* focus = dialog->Layer().Context().GetFocusElement();
    REQUIRE(focus != nullptr);
    CHECK_EQ(focus->GetId(), std::string("other"));
    Shot("exit_mp");

    // Escape: its CANCEL, the briefing as it was.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(!MCRmlDialogs::Shown());
    CHECK(MCCampaignMenu::Shown());
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Briefing);

    // EXIT: the LAN games.
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(MCRmlDialogs::Shown());
    dialog->PressMain();
    RunWhileChanging();
    CHECK(!MCCampaignMenu::Shown());
    CHECK(MCTestGuest::OnLan());
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > multiplayer > lan"));
}

namespace
{
    /// <summary>Frames at the mission's rate until its results show (twenty seconds: the scenario's end, their fade).</summary>
    constexpr int32_t MostFramesToResults = 300;
    constexpr float MissionFrame = 1.0f / 15.0f;
    /// <summary>A mission's results: above 3 a win (NEXT MISSION), below a loss (REPLAY MISSION).</summary>
    constexpr uint32_t Won = 5;
    constexpr uint32_t Lost = 2;
    constexpr std::string_view FirstTitle = "Liberate work camps";
    constexpr std::string_view FirstEyebrow = "Beachhead · Mission 1";

    /// <summary>
    /// The remade briefing up on BRIEFING where the campaign stands (<paramref name="eyebrow"/> over
    /// <paramref name="title"/>), the campaign logistics' screen, the main menu under it in the stack.
    /// </summary>
    void CheckRemadeBriefing(std::string_view eyebrow, std::string_view title)
    {
        REQUIRE(MCCampaignMenu::Shown());
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();
        REQUIRE(logistics != nullptr);
        CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
        CHECK_EQ(MCScreenFlow::StackText(), std::string("main > campaign"));
        CHECK_EQ(Shown().Eyebrow(), std::string(eyebrow));
        CHECK_EQ(Shown().Title(), std::string(title));
    }

    /// <summary>Whether the mission plays (its start done).</summary>
    bool Playing()
    {
        return Scenario() != nullptr && Mission()->State == MCMissionState::Scenario && !Scenario()->StartingUp;
    }

    /// <summary>
    /// DEPLOY's BEGIN MISSION with READY AREA's first card sent into the drop (Enter), run until the mission plays.
    /// </summary>
    void BeginFromDeploy()
    {
        constexpr int32_t MostFrames = 3000;
        MCCampaignScreen& screen = Shown();
        screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
        MCTestGame::RunFrame(FlowFrame);
        ElementOf(screen, "ready0").Focus();
        MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
        MCTestGame::RunFrame(FlowFrame);
        REQUIRE(screen.CanBegin());
        ClickOn(ElementOf(screen, "begin"));

        for (int32_t frame = 0; frame < MostFrames && !Playing(); frame++)
        {
            MCTestGame::RunFrame(FlowFrame);
        }

        REQUIRE(Playing());
        CHECK(!MCCampaignMenu::Shown());
        CHECK(!MCLoadingMenu::Shown());
    }

    /// <summary>The mission playing ended as <paramref name="result"/>, and its results closed: the way back done.</summary>
    void EndAndCloseResults(uint32_t result)
    {
        ScenarioResult = result;
        Mission()->EndScenarioRequested = -1;

        for (int32_t frame = 0; frame < MostFramesToResults && Mission()->State != MCMissionState::Results; frame++)
        {
            MCTestGame::RunFrame(MissionFrame);
        }

        REQUIRE(Mission()->State == MCMissionState::Results);
        Mission()->CloseResultsScreen();
        RunWhileChanging();
    }
}

/// <summary>
/// The ways back to the remade briefing between missions (SPEC-menus.md's stage 8 walk): Escape to the main menu and
/// its RETURN TO GAME, SAVE GAME's "Game saved.", LOAD GAME's LOAD; each opens it where the campaign stands.
/// </summary>
TEST_CASE_ISOLATED("game: RETURN TO GAME, SAVE GAME and LOAD GAME go back to the remade briefing")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    RunWhileChanging();
    NewCampaign();
    RunWhileChanging();
    CheckRemadeBriefing(FirstEyebrow, FirstTitle);

    // DEPLOY, Escape: the main menu; its RETURN TO GAME: the briefing again, on DEPLOY (as 1998's went back to the
    // screen left).
    Shown().ChooseTab(MCCampaignScreen::MCTab::Deploy);
    MCTestGame::RunFrame(FlowFrame);
    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    RunWhileChanging();
    CHECK(!MCCampaignMenu::Shown());
    CHECK(MCMenuScreens::Top() == MCMenuScreens::Main());
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main"));
    MCScreenFlow::Via("return to game", ReturnToGame)();
    RunWhileChanging();
    REQUIRE(MCCampaignMenu::Shown());
    CHECK(Shown().Tab() == MCCampaignScreen::MCTab::Deploy);
    Shown().ChooseTab(MCCampaignScreen::MCTab::Briefing);
    MCTestGame::RunFrame(FlowFrame);
    CheckRemadeBriefing(FirstEyebrow, FirstTitle);

    // SAVE GAME from the main menu, Enter under its default name, "Game saved." and Enter: the briefing.
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Save()));
    RunWhileChanging();
    REQUIRE(MCSavesMenu::Screen().has_value());
    MCSavesScreen& saves = MCSavesMenu::Screen()->get();
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    REQUIRE(saves.Dialog());
    REQUIRE(saves.Dialog()->Shown());
    CHECK_EQ(saves.Dialog()->Saying().Question, std::string(MCSavesMenu::SavedText));
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    RunWhileChanging();
    CHECK(!saves.Shown());
    CheckRemadeBriefing(FirstEyebrow, FirstTitle);

    // LOAD GAME from the main menu, Enter on the save just made: its briefing.
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();
    REQUIRE(MCScreenFlow::Open(MCMenuScreens::Load()));
    RunWhileChanging();
    REQUIRE(MCSavesMenu::Screen().has_value());
    MCSavesScreen& load = MCSavesMenu::Screen()->get();
    CHECK(load.Mode() == MCSavesScreen::MCMode::Load);
    REQUIRE_EQ(load.Rows().size(), size_t{1});
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    RunWhileChanging();
    CHECK(!load.Shown());
    CHECK_EQ(GlobalLog()->CurrentMission, 0);
    CheckRemadeBriefing(FirstEyebrow, FirstTitle);
}

/// <summary>
/// The way into a mission and back with the remade screens (SPEC-menus.md's stage 8 walk): the first briefing's BEGIN
/// MISSION, the mission won, its results' NEXT MISSION: the next mission's remade briefing; BEGIN MISSION again, lost,
/// REPLAY MISSION: that mission's briefing again; the third as a quick-start mission's, won: back to SOLO MISSIONS. Each
/// mission's start takes the menus' way back down (the stack cleared), and each landing makes it again (the main menu
/// under the briefing, or under SOLO MISSIONS). The second mission's pilots join the lance in its first frames: its end
/// came in the mech bar's dance, which crashed the way back.
/// </summary>
TEST_CASE_ISOLATED("game: the results' NEXT MISSION and REPLAY MISSION go back to the remade briefing")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::SaveGameCleanup cleanup;
    StartFirstBriefing();
    CheckRemadeBriefing(FirstEyebrow, FirstTitle);
    BeginFromDeploy();
    CHECK(MCScreenFlow::Stack().empty());

    EndAndCloseResults(Won);
    REQUIRE(GlobalLog() != nullptr);
    CHECK_EQ(GlobalLog()->CurrentMission, 1);
    constexpr std::string_view SecondEyebrow = "Beachhead · Mission 2";
    constexpr std::string_view SecondTitle = "Rescue captured pilots";
    CheckRemadeBriefing(SecondEyebrow, SecondTitle);

    BeginFromDeploy();
    EndAndCloseResults(Lost);
    REQUIRE(GlobalLog() != nullptr);
    CHECK_EQ(GlobalLog()->CurrentMission, 1);
    CheckRemadeBriefing(SecondEyebrow, SecondTitle);

    // As a quick-start mission's end (BriefingTests' EndToResultsAndBack): the remade SOLO MISSIONS, the main menu
    // under it.
    BeginFromDeploy();
    Solo = true;
    EndAndCloseResults(Won);
    CHECK(!Solo);
    CHECK(!MCCampaignMenu::Shown());
    CHECK(MCMainMenuScreens::Shown(MCMainPanel::Solo));
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > solo missions"));
}

/// <summary>
/// A solo mission's PLAY (SOLO MISSIONS' first, picked as it opens) opens its remade briefing: its name (its
/// briefing's STARCOM line, as two of the expansion's solo missions write it), no force yet (BEGIN MISSION dark), the
/// tons that drop and the RP to buy them; the same four tabs as a campaign's.
/// (Its win's way back to SOLO MISSIONS: the results' test.)
/// </summary>
TEST_CASE_ISOLATED("game: a solo mission's PLAY opens its remade briefing, its force to buy")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    RunWhileChanging();
    MCTestGuest::ClickOn(MCMainPanel::Main, "solo");
    RunWhileChanging();
    const MCMainMenuScreen& solos = MCTestGuest::Panel(MCMainPanel::Solo);
    REQUIRE(!solos.Rows().empty());
    REQUIRE_EQ(solos.Rows().front().Title, std::string("Destroy All Aces"));
    REQUIRE_EQ(solos.Picked(), 0);
    MCTestGuest::ClickOn(MCMainPanel::Solo, "main");
    RunWhileChanging();
    REQUIRE(MCCampaignMenu::Shown());
    CHECK(Solo);
    MCCampaignScreen& screen = Shown();
    CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
    CHECK_EQ(MCScreenFlow::StackText(), std::string("main > campaign"));
    CHECK_EQ(screen.Eyebrow(), std::string("Solo"));
    CHECK_EQ(screen.Title(), std::string("Destroy All Aces"));
    CHECK_EQ(screen.Objectives().size(), size_t{5});
    CHECK(ResourcePoints > 0);

    // DEPLOY: nothing ready, BEGIN MISSION dark, the drop's tons all free.
    screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(screen.ReadyCards().empty());
    CHECK(!screen.CanBegin());
    CHECK_EQ(screen.WeightNow(), std::string("0"));
    CHECK_EQ(screen.WeightFree(), std::format("{} tons free", MaxDeployTonnage));
    screen.ChooseTab(MCCampaignScreen::MCTab::Briefing);
    MCTestGame::RunFrame(FlowFrame);

    // E: PURCHASING, the same screen: no force yet, the store's 'Mechs to buy with the mission's RP; Q back.
    MCScreenInput::RealKey(SDL_SCANCODE_E);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(MCCampaignMenu::Shown());
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Purchasing);
    CHECK(MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing));
    MCLogisticsTabs& tabs = screen.Logistics();
    CHECK(tabs.ForceCards().empty());
    CHECK(!tabs.StoreGoods().empty());
    MCScreenInput::RealKey(SDL_SCANCODE_Q);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Briefing);
    CHECK(Solo);
}

namespace
{
    /// <summary>Each match test's port, its own so the tests run beside each other and the others (--jobs).</summary>
    constexpr std::string_view BriefingMatchPort = "28985";
    constexpr std::string_view GuestLoadingPort = "28987";
    constexpr std::string_view HostLoadingPort = "28989";
    constexpr std::string_view LostLoadingPort = "28993";
    constexpr std::string_view LogisticsMatchPort = "28997";
    constexpr std::string_view ReadyOncePort = "28945";
    constexpr std::string_view LauncherLeavePort = "28947";
    constexpr std::string_view InnerSphereZonesPort = "28937";
    constexpr std::string_view ClanZonesPort = "28939";
    /// <summary>The room's teams (MCRoomModel::AssignPlayer's): the first plays the Inner Sphere's side, home team 0.</summary>
    constexpr int32_t InnerSphereTeam = 1;
    constexpr int32_t ClanTeam = 2;
    /// <summary>A drop's weight for BEGIN MISSION to go (any above none).</summary>
    constexpr int32_t DroppedTons = 35;
    /// <summary>1998's "has left the game" that ends a launcher's game (MCLogistics::HandleLostPlayer), and its length.</summary>
    constexpr uint32_t LauncherLeftString = 0x365;
    constexpr int32_t LeftStringLength = 0xfe;
    constexpr std::string_view MatchMission = "ARENA";
    /// <summary>A message said over the briefing, in the 1998 dialog's capitals.</summary>
    constexpr std::string_view BriefingMessage = "THE GAME SAYS SOMETHING.";
    /// <summary>A player's light once they've pressed BEGIN MISSION (MCMPPlayerLights' status 2).</summary>
    constexpr int32_t ReadyLight = 2;
    /// <summary>The chat's switch: TEAM, then ALL.</summary>
    constexpr int AllChoice = 1;

    /// <summary>
    /// A LAN match hosted as COMMANDER (on <paramref name="myTeam"/>, team 1 unless said) on <paramref name="port"/>
    /// with <paramref name="ann"/> (the tests' player, on the other team), its mission's file hers too, and the room's
    /// BEGIN PLANNING pressed: the match's remade briefing.
    /// </summary>
    MCCampaignScreen& PlanMatch(MCTestGuest::Guest& ann, std::string_view port, int32_t myTeam = InnerSphereTeam)
    {
        REQUIRE(MCTestGame::StartLogistics());
        MCTestGuest::OpenLan();
        const std::string address = std::format("127.0.0.1:{}", port);
        MCTestGuest::HostFromLan(address, "COMMANDER", "COMMANDER'S GAME", 4);
        MCTestGuest::Settle();
        MCSessionManager& manager = *MultiPlayer()->SessionManager;
        const uint32_t sameData = MCMissionDataHash(GamePath(SavePath, MatchMission, ".mpk"));
        REQUIRE(sameData != 0);
        REQUIRE(ann.Join(*manager.DirectPlay, address));
        REQUIRE(
            MCTestGuest::SettleUntil([&] { return MCTestGuest::RoomShows(ann.Id()) && ann.SessionScreens() == 1; }));
        REQUIRE(manager.AddPlayerToGroup(MultiPlayer()->AllPlayerGroupID, ann.Id()));
        ann.AnswerFiles(sameData);
        MCRoomModel& room = GlobalLog()->Room;
        room.AssignPlayer(manager.MyPlayerID, myTeam, 0, false);
        room.AssignPlayer(ann.Id(), myTeam == InnerSphereTeam ? ClanTeam : InnerSphereTeam, 0, false);
        room.LoadMission(MatchMission);
        REQUIRE(
            MCTestGuest::SettleUntil([&] { return ann.MissionsLoaded() == 1 && MCTestGuest::DialogText().empty(); }));
        MCTestGuest::Settle();
        const MCGuiHandle<MCRoomScreen> remade = RoomScreen();
        REQUIRE(remade);
        REQUIRE(remade->Seen().CanBegin);
        remade->PressBegin();
        REQUIRE(MCTestGuest::SettleUntil([] { return MCCampaignMenu::Shown(); }));
        RunWhileChanging();
        return Shown();
    }

    /// <summary>
    /// The bar's clock out of the pictures: the game's clock moves a frame at a time, and the network's waits take
    /// however many frames they take (hidden in place, its digits' widths moved RP beside it).
    /// </summary>
    void HideClock(const MCCampaignScreen& screen)
    {
        REQUIRE(screen.Document().has_value());
        Rml::ElementList money;
        screen.Document()->get().QuerySelectorAll(money, ".tabs-row .money");
        REQUIRE(!money.empty());
        money.back()->SetProperty("display", "none");
    }

    /// <summary>
    /// A game's word to the host <paramref name="hostId"/> of <paramref name="type"/> (its BEGIN MISSION's, or its load
    /// done), sent for <paramref name="guest"/>, the match's player number <paramref name="number"/>.
    /// </summary>
    void SendCheckIn(MCTestGuest::Guest& guest, uint32_t hostId, MCMPMessageType type, int32_t number)
    {
        MCMPPlayerCheckInMessage message{};
        message.Header = PlainHeader(type);
        message.CheckInId = static_cast<int8_t>(number);
        guest.SendTo(hostId, message);
    }

    /// <summary>
    /// A LAN match planned on <paramref name="port"/> (<see cref="PlanMatch"/>) and every player's BEGIN MISSION sent
    /// (this game's as the 1998 button sends it, then <paramref name="ann"/>'s): the load behind the loading screen,
    /// until this game's load is done, the battlefield entered behind it and the match waiting for her game. Her player
    /// number.
    /// </summary>
    int32_t LoadToTheWait(MCTestGuest::Guest& ann, std::string_view port)
    {
        PlanMatch(ann, port);
        MCSessionManager& manager = *MultiPlayer()->SessionManager;
        const std::shared_ptr<MCFidpPlayer> her = manager.GetPlayer(ann.Id());
        REQUIRE(her != nullptr);
        MultiPlayer()->SendReadyForBattle();
        SendCheckIn(ann, manager.MyPlayerID, MCMPMessageType::ReadyForBattle, her->PlayerNumber);
        REQUIRE(MCTestGuest::SettleUntil([] { return MCLoadingMenu::Shown(); }));
        REQUIRE(MCLoadingMenu::Screen().has_value());
        const MCLoadingScreen& loading = MCLoadingMenu::Screen()->get();
        REQUIRE(MCTestGuest::SettleUntil([&loading] { return loading.What() != MCLoadingMenu::LoadingText; }));
        MCTestGame::RunFrame(FlowFrame);
        return her->PlayerNumber;
    }

    /// <summary>The system's word that this game's session is lost (DPSYS_SESSIONLOST), as the session manager gives it.</summary>
    void LoseSession()
    {
        DPMSG_GENERIC lost{.dwType = DPSYS_SESSIONLOST};
        MCFidpMessage message(0, sizeof(lost));
        message.SetMessageBuffer(LinkupBytes(lost));
        MultiPlayerSystemCallback(message);
    }

    /// <summary>
    /// A match's BRIEFING's drop zone tags, planned by <see cref="PlanMatch"/> on ARENA (its zones away from its map's
    /// edges): the team's three of the mission's six from <paramref name="firstZone"/> (1998's MarkedZone), F1 to F3,
    /// each zone where 1998's briefing marked it (MCCampaignMenu::MapPointOf) in the picture it stretched over its
    /// map area, each tag inside the picture with its tail at the bottom right; at every window the players' reports
    /// name and the frames' (4:3 to 21:9) each tail's tip within a pixel of its zone on the map as drawn (the picture
    /// fitted to its box at its left and top), each tag at the picture's scale; the screen laid out with no problems.
    /// Saved with --shots as <paramref name="what"/> at the frames' windows, 1920x1080 and 3440x1440.
    /// </summary>
    void CheckZoneTags(MCCampaignScreen& screen, int32_t firstZone, std::string_view what)
    {
        constexpr size_t Zones = 3;
        constexpr float Pixel = 1.0f;
        constexpr float Exact = 1e-3f;
        constexpr std::array<MCMenusPictures::MCWindowSize, 2> MoreShots = {{{1920, 1080}, {3440, 1440}}};
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();
        REQUIRE(logistics != nullptr);
        // 1998's marks, as its briefing worked them out from the match's map (MCLogistics::ZoneMarks).
        const MCLogistics::MCZoneMarks& marks = logistics->ZoneMarks;
        REQUIRE_EQ(marks.Zone, firstZone);
        REQUIRE(marks.Scale > 0.0f);
        // The match map's own briefing picture, its size the one the marks were worked out on.
        const std::optional<MCLogistics::MCPicturePixels> picture =
            MCLogistics::MapPictureAt(GamePath(TerrainPath, logistics->MissionFileName, ".log.tga"));
        REQUIRE(picture.has_value());
        CHECK_EQ(marks.MapWidth, picture->Width);
        CHECK_EQ(marks.MapHeight, picture->Height);
        const MCCampaignMenu::MCZoneMap& map = screen.ZoneMap();
        CHECK_EQ(map.Width, static_cast<float>(picture->Width));
        CHECK_EQ(map.Height, static_cast<float>(picture->Height));
        REQUIRE_EQ(map.Tags.size(), Zones);
        // 1998 stretched the picture over its map area, its first to its last pixel each way.
        const float areaWidth = static_cast<float>(MCCampaignMenu::MapRight - MCCampaignMenu::MapLeft + 1);
        const float areaHeight = static_cast<float>(MCCampaignMenu::MapBottom - MCCampaignMenu::MapTop + 1);

        for (size_t i = 0; i < Zones; ++i)
        {
            const MCTest::Scope scope{std::format("{} F{}", what, i + 1)};
            const MCCampaignMenu::MCZoneTag& tag = map.Tags[i];
            const MCLogistics::DropZonePosition& zone =
                logistics->DropZonePositions[static_cast<size_t>(firstZone) + i];
            const SDL_FPoint marked = MCCampaignMenu::MapPointOf(zone.X, zone.Y, marks.Scale);
            CHECK_EQ(tag.Group, static_cast<int>(i + 1));
            CHECK(std::abs(tag.X - (marked.x - MCCampaignMenu::MapLeft) / areaWidth * map.Width) < Exact);
            CHECK(std::abs(tag.Y - (marked.y - MCCampaignMenu::MapTop) / areaHeight * map.Height) < Exact);
            CHECK(tag.Tail == MCCampaignMenu::MCTail::BottomRight);
            CHECK(tag.Left >= 0.0f && tag.Top >= 0.0f && tag.Left + tag.Width <= map.Width &&
                  tag.Top + tag.Height <= map.Height);
        }

        REQUIRE(MCRmlMenus::Running().has_value());
        MCRmlMenus& menus = MCRmlMenus::Running()->get();
        Rml::Element& mapElement = ElementOf(screen, "map");
        std::vector<MCMenusPictures::MCWindowSize> windows(MCMenusPictures::PlayersWindows.begin(),
                                                           MCMenusPictures::PlayersWindows.end());
        windows.insert(windows.end(), MCMenusPictures::FrameWindows.begin(), MCMenusPictures::FrameWindows.end());

        for (const MCMenusPictures::MCWindowSize& window : windows)
        {
            // The window's frame, the screen's frame at its size (the tags follow the map's box), laid out again.
            menus.SetWindow(window.Width, window.Height);
            menus.Frame();
            menus.Menus().Tend();
            menus.Frame();
            const MCTest::Scope scope{std::format("{} {}x{}", what, window.Width, window.Height)};
            const Rml::Vector2f origin = mapElement.GetAbsoluteOffset(Rml::BoxArea::Content);
            const Rml::Vector2f box = mapElement.GetBox().GetSize(Rml::BoxArea::Content);
            const float scale = std::min(box.x / map.Width, box.y / map.Height);
            const std::vector<Rml::Element*> tags = ZoneTagsShown(screen);
            REQUIRE_EQ(tags.size(), Zones);
            REQUIRE_EQ(screen.ZonesShown().size(), Zones);

            for (size_t i = 0; i < Zones; ++i)
            {
                const MCCampaignMenu::MCZoneTag& tag = map.Tags[i];
                const Rml::Vector2f at = tags[i]->GetAbsoluteOffset(Rml::BoxArea::Border);
                const Rml::Vector2f size = tags[i]->GetBox().GetSize(Rml::BoxArea::Border);
                // The tail's tip where its picture has it, at the size drawn.
                const float tipX = at.x + (tag.X - tag.Left) / tag.Width * size.x;
                const float tipY = at.y + (tag.Y - tag.Top) / tag.Height * size.y;
                CHECK(std::abs(tipX - (origin.x + tag.X * scale)) <= Pixel);
                CHECK(std::abs(tipY - (origin.y + tag.Y * scale)) <= Pixel);
                CHECK(std::abs(size.x - tag.Width * scale) <= Pixel);
                CHECK(std::abs(size.y - tag.Height * scale) <= Pixel);
                CHECK_EQ(screen.ZonesShown()[i].Picture, std::format("image({})", MCCampaignMenu::ZoneTagSource(tag)));
            }
        }

        CheckLaidOut(menus, what);
        const std::shared_ptr<MCDisplay> display = MCInput::Display();
        REQUIRE(display != nullptr);
        std::vector<MCMenusPictures::MCWindowSize> shots(MCMenusPictures::FrameWindows.begin(),
                                                         MCMenusPictures::FrameWindows.end());
        shots.insert(shots.end(), MoreShots.begin(), MoreShots.end());

        for (const MCMenusPictures::MCWindowSize& size : shots)
        {
            REQUIRE(MCMenusPictures::SetWindowPixels(*display, size.Width, size.Height));
            Shot(what);
        }
    }

    /// <summary>A line typed in the chat's box <paramref name="box"/> and sent with Enter, as a player does.</summary>
    void TypeLine(const MCCampaignScreen& screen, const std::string& box, std::string_view line)
    {
        ElementOf(screen, box).Focus();
        MCTestGuest::RealType(line);
        MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
        MCTestGame::RunFrame(FlowFrame);
    }
}

/// <summary>
/// A match's remade briefing (briefing_b_mp, deploy_b_mp): the players in the bar's second row by team, this game's
/// first, each a light and the name in the player's colour, and how many are ready of how many; the chat under the
/// orders and under DEPLOY's groups, each line its sayer's name in their colour, a team's line marked; a line typed and
/// sent with Enter goes to the team (TEAM, the switch's first), the box emptied; ALL chosen in one place holds in every
/// other. The same on every machine at the frames' sizes, and laid out at every window the players' reports name. A
/// player leaving goes from the bar, the chat saying so as the room's does, no dialog over it.
/// </summary>
TEST_CASE_ISOLATED("game: a match's remade briefing shows its players by team with their lights, and its chat")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    GRenderer = static_cast<int>(MCRendererKind::Software);
    MCTestGuest::Guest ann("ANN", MCMatchRule::Supported);
    MCCampaignScreen& screen = PlanMatch(ann, BriefingMatchPort);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    const std::shared_ptr<MCFidpPlayer> me = manager.GetPlayer(manager.MyPlayerID);
    const std::shared_ptr<MCFidpPlayer> her = manager.GetPlayer(ann.Id());
    REQUIRE(me != nullptr);
    REQUIRE(her != nullptr);
    const std::string myColour = MCChatView::ColourOf(me->PlayerNumber);
    const std::string herColour = MCChatView::ColourOf(her->PlayerNumber);

    // Its title the side briefing's (isbrf0703.txt's "*** ARENA COMBAT ***"), in sentence case.
    CHECK_EQ(screen.Title(), std::string("Arena combat"));

    // The bar: COMMANDER's team, then ANN's, nobody ready; no video (a match has none).
    CHECK(screen.Tab() == MCCampaignScreen::MCTab::Briefing);
    CHECK(!screen.HasVideo());
    CHECK_EQ(screen.OurTeam(), (std::vector<MCCampaignScreen::MCBarPlayer>{{.Name = "COMMANDER", .Colour = myColour}}));
    CHECK_EQ(screen.OtherTeam(), (std::vector<MCCampaignScreen::MCBarPlayer>{{.Name = "ANN", .Colour = herColour}}));
    CHECK_EQ(screen.ReadyCount(), std::string("0"));
    CHECK_EQ(screen.PlayerCount(), std::string("2"));

    // ANN's BEGIN MISSION (her light green): one of two ready.
    GlobalLog()->PlayerLights->SetPlayerStatus(ann.Id(), ReadyLight);
    MCTestGame::RunFrame(FlowFrame);
    CHECK_EQ(screen.OtherTeam(),
             (std::vector<MCCampaignScreen::MCBarPlayer>{{.Name = "ANN", .Colour = herColour, .Ready = true}}));
    CHECK_EQ(screen.ReadyCount(), std::string("1"));

    // ANN's lines as the chat holds them (MCLogChat::ProcessChatString's): to all, then to her team.
    const int32_t herChat = GlobalLog()->PlayerColors[static_cast<size_t>(her->PlayerNumber)];
    GlobalLog()->Chat->AddLine(std::format("%fc{}ANN: %fc6Same map again?", herChat));
    GlobalLog()->Chat->AddLine(std::format("%fc{}ANN: %fc4Jump jets on mine.", herChat));
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(screen.ChatLines().size() >= 2);
    const MCChatView::MCLine& toAll = screen.ChatLines()[screen.ChatLines().size() - 2];
    CHECK_EQ(toAll, (MCChatView::MCLine{.Name = "ANN", .Colour = herColour, .Text = "Same map again?"}));
    CHECK(screen.ChatLines().back().Team);

    // The menus on the test's clock from here: the box's caret blinks on it.
    REQUIRE(MCRmlMenus::Running().has_value());
    MCRmlMenus& menus = MCRmlMenus::Running()->get();
    double clock = 0.0;
    menus.SetClock([&clock] { return clock; });

    // Typed under the orders and sent with Enter: to the team, the box emptied.
    CHECK(screen.ToTeam());
    TypeLine(screen, "say-brief", "On my way");
    REQUIRE(!screen.ChatLines().empty());
    CHECK_EQ(screen.ChatLines().back(),
             (MCChatView::MCLine{.Name = "COMMANDER", .Colour = myColour, .Text = "On my way", .Team = true}));
    CHECK_EQ(ElementOf(screen, "say-brief").GetAttribute<Rml::String>("value", "unread"), std::string());

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    REQUIRE(display->RendererKind() == MCRendererKind::Software);
    HideClock(screen);
    DrawnEach(*display, clock, "briefing_mp", {0x7100b114, 0x912dc805, 0xd719000e});
    CheckLaidOut(menus, "briefing mp");

    // DEPLOY: ALL chosen under the groups holds in every place; a line then goes to all.
    screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
    MCTestGame::RunFrame(FlowFrame);
    Rml::Element& toDeploy = ElementOf(screen, "to-deploy");
    REQUIRE(toDeploy.GetNumChildren() > AllChoice);
    ClickOn(*toDeploy.GetChild(AllChoice));
    MCTestGame::RunFrame(FlowFrame);
    CHECK(!screen.ToTeam());

    for (const std::string id : {"to-brief", "to-brief-side", "to-deploy", "to-deploy-side"})
    {
        CHECK_EQ(MCRmlKit::Choice(ElementOf(screen, id)), AllChoice);
    }

    TypeLine(screen, "say-deploy", "Good luck, all.");
    REQUIRE(!screen.ChatLines().empty());
    CHECK_EQ(screen.ChatLines().back(),
             (MCChatView::MCLine{.Name = "COMMANDER", .Colour = myColour, .Text = "Good luck, all."}));
    DrawnEach(*display, clock, "deploy_mp", {0xff3d4309, 0x6d3e14f9, 0x97a96359});
    CheckLaidOut(menus, "deploy mp");

    // ANN leaves: gone from the bar, the chat saying so as the room's does (the host's line,
    // MCMultiPlayer::PlayerLeftGame); no dialog (the 1998 one's "Returning you to the Main Menu" returned nowhere).
    ann.Leave();
    REQUIRE(MCTestGuest::SettleUntil([&screen] { return screen.OtherTeam().empty(); }));
    MCTestGuest::Settle();
    CHECK_EQ(screen.PlayerCount(), std::string("1"));
    REQUIRE(!screen.ChatLines().empty());
    CHECK_EQ(screen.ChatLines().back(),
             (MCChatView::MCLine{.Name = "COMMANDER", .Colour = myColour, .Text = "ANN has left the game."}));
    CHECK(MCTestGuest::DialogText().empty());
    CHECK(MCCampaignMenu::Shown());
}

/// <summary>
/// A match's BRIEFING (briefing_b_mp_zones_hdtags) on the Inner Sphere's side (the room's team 1, home team 0): its
/// map tags the side's drop zones, the mission's first three, F1 to F3 as the HD briefing maps tag 1998's flags, each
/// tail on its zone at every window (<see cref="CheckZoneTags"/>). DEPLOY shows none; BRIEFING again shows the same.
/// </summary>
TEST_CASE_ISOLATED("game: a match's BRIEFING tags the Inner Sphere's three drop zones on its map")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGuest::Guest ann("ANN", MCMatchRule::Supported);
    MCCampaignScreen& screen = PlanMatch(ann, InnerSphereZonesPort);
    REQUIRE_EQ(MultiPlayer()->HomeTeam, 0);
    CheckZoneTags(screen, 0, "briefing_mp_zones");

    const std::vector<MCCampaignScreen::MCZoneShown> placed = screen.ZonesShown();
    screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(ZoneTagsShown(screen).empty());
    screen.ChooseTab(MCCampaignScreen::MCTab::Briefing);
    MCTestGame::RunFrame(FlowFrame);
    CHECK_EQ(ZoneTagsShown(screen).size(), size_t{3});
    CHECK(screen.ZonesShown() == placed);
}

/// <summary>
/// The same match's BRIEFING on the Clans' side (the room's team 2, home team 1): its map tags the mission's last three
/// drop zones, F1 to F3 (<see cref="CheckZoneTags"/>), none where the other side's are.
/// </summary>
TEST_CASE_ISOLATED("game: a match's BRIEFING tags the Clans' three drop zones on its map")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr int32_t ClanZones = 3;
    MCTestGuest::Guest ann("ANN", MCMatchRule::Supported);
    MCCampaignScreen& screen = PlanMatch(ann, ClanZonesPort, ClanTeam);
    REQUIRE_EQ(MultiPlayer()->HomeTeam, 1);
    CheckZoneTags(screen, ClanZones, "briefing_mp_zones_clan");

    // The other side's zones aren't these.
    const std::vector<MCCampaignMenu::MCZoneTag>& tags = screen.ZoneMap().Tags;

    for (size_t zone = 0; zone < tags.size(); ++zone)
    {
        const MCLogistics::DropZonePosition& theirs = GlobalLog()->DropZonePositions[zone];
        const MCLogistics::DropZonePosition& ours =
            GlobalLog()->DropZonePositions[static_cast<size_t>(ClanZones) + zone];
        CHECK(theirs.X != ours.X || theirs.Y != ours.Y);
    }
}

/// <summary>
/// A match's BEGIN MISSION once pressed is said once: the ready word and its line in the chat go out once, and the
/// button is dark from then on, however often it is pressed again (1998's stayed down).
/// </summary>
TEST_CASE_ISOLATED("game: a match's BEGIN MISSION is said once, the button dark after it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGuest::Guest ann("ANN", MCMatchRule::Supported);
    MCCampaignScreen& screen = PlanMatch(ann, ReadyOncePort);
    // A drop with weight in it (a match's units are bought first; the press is what's tried here).
    CurDeployTonnage = DroppedTons;
    REQUIRE(MCDeploy::CanBegin());
    // Ann hears its line ("COMMANDER is ready") once.
    ann.Watch(MCMPMessageType::Chat);
    screen.PressBegin();
    REQUIRE(MCTestGuest::SettleUntil([&ann] { return !ann.Watched(MCMPMessageType::Chat).empty(); }));
    MCTestGuest::Settle();
    CHECK(MCDeploy::Locked());
    CHECK(!screen.CanBegin());
    CHECK_EQ(ann.Watched(MCMPMessageType::Chat).size(), size_t{1});

    screen.PressBegin();
    MCTestGuest::Settle();
    CHECK_EQ(ann.Watched(MCMPMessageType::Chat).size(), size_t{1});
    CHECK(MCCampaignMenu::Shown());
}

/// <summary>
/// A launcher's match ends as 1998's did when a player leaves it at the remade briefing: the dialog says the words that
/// end it over the briefing, and its answer ends the game (LostPlayerHandler, CancelBool's KillTheGame).
/// </summary>
TEST_CASE_ISOLATED("game: a launcher's match ends when a player leaves its remade briefing")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGuest::Guest ann("ANN", MCMatchRule::Supported);
    PlanMatch(ann, LauncherLeavePort);
    // As a launcher's command line sets it (here once the match is planned, the way in being LAN's).
    LaunchedFromLobby = true;
    ann.Leave();
    REQUIRE(MCTestGuest::SettleUntil([] { return !MCTestGuest::DialogText().empty(); }));
    CHECK(MCRmlDialogs::Shown());
    CHECK_EQ(MCTestGuest::DialogText(),
             MCRmlDialogs::AsSentence(std::format("ANN {}", LoadGameString(LauncherLeftString, LeftStringLength))));
    bool ended = false;

    try
    {
        MCRmlDialogs::Dialog()->PressMain();
        MCTestGuest::Settle();
    }
    catch (const MCGameQuit& quit)
    {
        ended = quit.ExitCode == 0;
    }

    CHECK(ended);
}

/// <summary>
/// A match's PURCHASING and 'MECH BAY (purchase_mp, mechbay_mp): the side's store as many of each as wanted (no count,
/// nothing sold out), a part sold back for the full price it cost (1998's in a match), the chat's newest lines and the
/// line being typed under the middle and right columns, a line sent there with Enter; the 'MECH BAY's lab with no
/// repairs (a match has none). Both the same on every machine at the frames' sizes, and laid out at every window the
/// players' reports name.
/// </summary>
TEST_CASE_ISOLATED(
    "game: a match's PURCHASING and 'MECH BAY stand on the chat, the store unlimited, a sale the full price")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    GRenderer = static_cast<int>(MCRendererKind::Software);
    MCTestGuest::Guest ann("ANN", MCMatchRule::Supported);
    MCCampaignScreen& screen = PlanMatch(ann, LogisticsMatchPort);
    MCLogisticsTabs& tabs = screen.Logistics();
    // The menus on the test's clock: the chat box's caret blinks on it.
    REQUIRE(MCRmlMenus::Running().has_value());
    MCRmlMenus& menus = MCRmlMenus::Running()->get();
    double clock = 0.0;
    menus.SetClock([&clock] { return clock; });
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    REQUIRE(display->RendererKind() == MCRendererKind::Software);
    HideClock(screen);
    const auto unlimited = [](const MCLogisticsTabs::MCGood& good) { return good.Note.empty() && !good.Off; };

    // PURCHASING: the side's 'Mechs and parts, no counts.
    screen.ChooseTab(MCCampaignScreen::MCTab::Purchasing);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(!tabs.StoreGoods().empty());
    CHECK(std::ranges::all_of(tabs.StoreGoods(), unlimited));
    tabs.ChooseKind(MCLogisticsTabs::MCKind::Components);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(!tabs.StoreGoods().empty());
    CHECK(std::ranges::all_of(tabs.StoreGoods(), unlimited));

    // A part bought, then sold back from the INVENTORY (the left column with COMPONENTS): all its price back.
    const int32_t points = ResourcePoints;
    const size_t owned = tabs.StockGoods().size();
    tabs.Pick(MCLogisticsTabs::MCPlace::Store, 0);
    tabs.PressDeal();
    REQUIRE(tabs.DealOpen());
    tabs.DealConfirm();
    MCTestGame::RunFrame(FlowFrame);
    CHECK(ResourcePoints < points);
    REQUIRE(!tabs.StockGoods().empty());
    CHECK(tabs.StockGoods().size() >= owned);
    tabs.Drop(MCLogisticsTabs::MCPlace::Stock, 0, MCLogisticsTabs::MCPlace::Store);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(tabs.DealOpen());
    CHECK_EQ(tabs.DealTitle(), std::string("Sell"));
    tabs.DealConfirm();
    MCTestGame::RunFrame(FlowFrame);
    CHECK_EQ(ResourcePoints, points);

    // The chat under the store and the details: a line typed there goes to the team.
    TypeLine(screen, "say-logistics", "Lasers for everyone");
    REQUIRE(!screen.ChatLines().empty());
    CHECK_EQ(screen.ChatLines().back().Text, std::string("Lasers for everyone"));
    CHECK(screen.ChatLines().back().Team);
    CHECK_EQ(ElementOf(screen, "say-logistics").GetAttribute<Rml::String>("value", "unread"), std::string());
    DrawnEach(*display, clock, "purchase_mp", {0x1309bd7c, 0xe19a1043, 0xc6b2006d});
    CheckLaidOut(menus, "purchase mp");

    // A 'Mech bought into the force for the 'MECH BAY: its lab without repairs.
    tabs.ChooseKind(MCLogisticsTabs::MCKind::Mechs);
    MCTestGame::RunFrame(FlowFrame);
    const size_t force = tabs.ForceCards().size();
    tabs.Pick(MCLogisticsTabs::MCPlace::Store, 0);
    tabs.PressDeal();
    REQUIRE(tabs.DealOpen());
    tabs.DealConfirm();
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(!tabs.SpareCards().empty());
    tabs.Send(MCLogisticsTabs::MCPlace::Spare, 0);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE_EQ(tabs.ForceCards().size(), force + 1);
    screen.ChooseTab(MCCampaignScreen::MCTab::MechBay);
    tabs.Pick(MCLogisticsTabs::MCPlace::Force, 0);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(tabs.LabUnit() != nullptr);
    CHECK(!tabs.Repairs());
    DrawnEach(*display, clock, "mechbay_mp", {0x5375b300, 0x4a033083, 0xb0548ca7});
    CheckLaidOut(menus, "mechbay mp");
}

/// <summary>
/// A match's loading screen on a guest's game (SPEC-menus.md, "Into a mission and back"): the match's room over the
/// title, the players under the map, its own lit once its load is done and the foot "Waiting for the others."; the
/// others' loads aren't told to a guest (only the host is), so theirs have no word and there's no count.
/// </summary>
TEST_CASE_ISOLATED("game: a match's loading screen on a guest lights only its own game, once loaded")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGuest::Guest ann("ANN", MCMatchRule::Supported);
    PlanMatch(ann, GuestLoadingPort);
    // This game as a guest's: no game's load is told to it (MCMultiPlayer::SendPlayerCheckIn goes to the host).
    MultiPlayer()->IsServer = false;
    MCLoadingMenu::Show();
    REQUIRE(MCLoadingMenu::Screen().has_value());
    const MCLoadingScreen& loading = MCLoadingMenu::Screen()->get();
    CHECK_EQ(loading.Eyebrow(), std::string("LAN · COMMANDER'S GAME"));
    CHECK_EQ(loading.Title(), std::string("Arena combat"));
    REQUIRE_EQ(loading.Players().size(), size_t{2});
    CHECK_EQ(loading.Players()[0].Name, std::string("COMMANDER"));
    CHECK(!loading.Players()[0].In);
    CHECK(loading.Players()[0].Word.empty());
    CHECK(loading.Count().empty());
    CHECK_EQ(loading.What(), std::string(MCLoadingMenu::LoadingText));

    MCLoadingMenu::Loaded();
    MCTestGame::RunFrame(FlowFrame);
    CHECK(loading.Players()[0].In);
    CHECK_EQ(loading.Players()[0].Word, std::string(MCLoadingMenu::InWord));
    CHECK(!loading.Players()[1].In);
    CHECK(loading.Players()[1].Word.empty());
    CHECK(loading.Count().empty());
    CHECK_EQ(loading.What(), std::string(MCLoadingMenu::WaitingForOthersText));
    MCLoadingMenu::Hide();
    MultiPlayer()->IsServer = true;
}

/// <summary>
/// A match's way in on the host (loading_mp): every player's BEGIN MISSION starts the load behind the loading screen,
/// the match's room over the title, the players under the map; this game in once its load is done ("1/2 in", "Loaded.
/// Waiting for ANN"), the battlefield entered behind it and waiting for ANN's game; the same on every machine at the
/// frames' sizes and laid out at every window. ANN's game in, the match starts behind the loading screen, which fades
/// out to the battle.
/// </summary>
TEST_CASE_ISOLATED("game: a match's loading screen lights each game as it's in, and the match starts behind it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    GRenderer = static_cast<int>(MCRendererKind::Software);
    MCTestGuest::Guest ann("ANN", MCMatchRule::Supported);
    const int32_t herNumber = LoadToTheWait(ann, HostLoadingPort);
    const uint32_t hostId = MultiPlayer()->SessionManager->MyPlayerID;
    const MCLoadingScreen& loading = MCLoadingMenu::Screen()->get();
    CHECK_EQ(loading.Eyebrow(), std::string("LAN · COMMANDER'S GAME"));
    CHECK_EQ(loading.Title(), std::string("Arena combat"));

    // The load done: this game in, the battlefield entered behind the loading screen, the match waiting for ANN's game.
    CHECK(MCLoadingMenu::Shown());
    CHECK(Scenario() != nullptr);
    CHECK(Mission()->State == MCMissionState::Scenario);
    CHECK(!MultiPlayer()->InMission);
    CHECK(GlobalLog() == nullptr);
    REQUIRE_EQ(loading.Players().size(), size_t{2});
    CHECK_EQ(loading.Players()[0], (MCLoadingScreen::MCPlayerRow{.Name = "COMMANDER",
                                                                 .Colour = loading.Players()[0].Colour,
                                                                 .In = true,
                                                                 .Word = std::string(MCLoadingMenu::InWord)}));
    CHECK_EQ(loading.Players()[1], (MCLoadingScreen::MCPlayerRow{.Name = "ANN",
                                                                 .Colour = loading.Players()[1].Colour,
                                                                 .Word = std::string(MCLoadingMenu::LoadingWord)}));
    CHECK_EQ(loading.Count(), std::string("1/2 in"));
    CHECK_EQ(loading.What(), std::string("Loaded. Waiting for ANN"));

    REQUIRE(MCRmlMenus::Running().has_value());
    MCRmlMenus& menus = MCRmlMenus::Running()->get();
    double clock = 0.0;
    menus.SetClock([&clock] { return clock; });
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    REQUIRE(display->RendererKind() == MCRendererKind::Software);
    DrawnEach(*display, clock, "loading_mp", {0x4c0de86d, 0x507ed6b9, 0x68d37528});
    CheckLaidOut(menus, "loading mp");
    CHECK(MCLoadingMenu::Shown());
    CHECK(!MultiPlayer()->InMission);

    // ANN's game in: the host starts the match, the loading screen fades out, the battle plays.
    SendCheckIn(ann, hostId, MCMPMessageType::PlayerCheckIn, herNumber);
    REQUIRE(MCTestGuest::SettleUntil([] { return MultiPlayer() != nullptr && MultiPlayer()->InMission; }));
    RunWhileChanging();
    CHECK(!MCLoadingMenu::Shown());
    CHECK(!LoadingScreenUp);
    CHECK(Scenario() != nullptr);
    CHECK(Mission()->State == MCMissionState::Scenario);
    CHECK(Turn > 1);
}

/// <summary>
/// A match whose session is lost while its loading screen waits for the others: the scenario's own wait takes the loss
/// (MCScenario::Run, as it did on the battlefield), the loading screen fades out and the game goes back to the LAN
/// games, saying so; no player is left on the loading screen.
/// </summary>
TEST_CASE_ISOLATED("game: a match whose session is lost as its loading screen waits goes back to the LAN games")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGuest::Guest ann("ANN", MCMatchRule::Supported);
    LoadToTheWait(ann, LostLoadingPort);
    REQUIRE(MCLoadingMenu::Shown());
    REQUIRE(!MultiPlayer()->InMission);

    LoseSession();
    REQUIRE(MCTestGuest::SettleUntil([] { return MCTestGuest::OnLan(); }));
    RunWhileChanging();
    CHECK(!MCLoadingMenu::Shown());
    CHECK(!LoadingScreenUp);
    CHECK(Scenario() == nullptr);
    CHECK(Mission()->State == MCMissionState::Logistics);
    CHECK_EQ(MCTestGuest::DialogText(), std::string("Lost the connection to COMMANDER's game."));
}

/// <summary>
/// A match's guest whose link to the host is lost on the remade briefing goes back to the LAN games it came from,
/// saying so over them, the remade briefing gone (the room's end's way: network/MCRoomEnd.h).
/// </summary>
TEST_CASE_ISOLATED("game: a match's remade briefing whose link is lost goes back to the LAN games, saying so")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    REQUIRE(MCCampaignMenu::Shown());
    auto game = std::make_shared<MCMultiPlayer>();
    MCGameContext::Current().SetMultiPlayer(game);
    REQUIRE_EQ(game->Start(), 0);
    game->SessionManager->CurrentConnection = MCNetProtocol::TcpIp;
    IsMPlayerGame = true;

    LoseSession();
    RunWhileChanging();

    CHECK(!MCCampaignMenu::Shown());
    CHECK(MCTestGuest::OnLan());
    CHECK_EQ(MCTestGuest::DialogText(), std::string("Lost the connection to the host's game."));
    IsMPlayerGame = false;
}

namespace
{
    /// <summary>The store's parts in PURCHASING's order (1998's: those it sorts, by their sort order).</summary>
    std::vector<std::shared_ptr<MCLogInventoryItem>> StoreParts()
    {
        std::vector<std::shared_ptr<MCLogInventoryItem>> parts;

        for (const std::shared_ptr<MCLogInventoryItem>& item : GlobalLog()->PurchaseComponents->Items)
        {
            if (item->SortOrder >= 0 && item->SortOrder < MCStore::SortOrders)
            {
                parts.push_back(item);
            }
        }

        std::ranges::stable_sort(parts, {}, &MCLogInventoryItem::SortOrder);
        return parts;
    }

    /// <summary>The spare parts in the INVENTORY's order (1998's rows).</summary>
    std::vector<std::shared_ptr<MCLogInventoryItem>> SpareParts()
    {
        std::vector<std::shared_ptr<MCLogInventoryItem>> parts;

        for (const std::shared_ptr<MCLogInventoryItem>& item : GlobalLog()->ComponentInventory->Items)
        {
            if (item->ListIndex >= 0)
            {
                parts.push_back(item);
            }
        }

        std::ranges::stable_sort(parts, {}, &MCLogInventoryItem::ListIndex);
        return parts;
    }

    /// <summary>The spare part with master id <paramref name="masterID"/>: its row in the INVENTORY.</summary>
    std::optional<size_t> SpareRow(uint8_t masterID)
    {
        const std::vector<std::shared_ptr<MCLogInventoryItem>> parts = SpareParts();
        const auto found = std::ranges::find(parts, masterID, &MCLogInventoryItem::MasterID);
        return found != parts.end() ? std::optional(static_cast<size_t>(found - parts.begin())) : std::nullopt;
    }

    int32_t SpareCount(uint8_t masterID)
    {
        const std::vector<std::shared_ptr<MCLogInventoryItem>> parts = SpareParts();
        const auto found = std::ranges::find(parts, masterID, &MCLogInventoryItem::MasterID);
        return found != parts.end() ? (*found)->Count : 0;
    }

    /// <summary>The key <paramref name="key"/> pressed with the focus on <paramref name="element"/>.</summary>
    void KeyOn(Rml::Element& element, SDL_Scancode key, int32_t times = 1)
    {
        element.Focus(true);

        for (int32_t press = 0; press < times; press++)
        {
            MCScreenInput::RealKey(key);
            MCTestGame::RunFrame(FlowFrame);
        }
    }

    /// <summary>1998's knob place for <paramref name="current"/> points of <paramref name="maximum"/> (61 pixels).</summary>
    int32_t KnobFor(int32_t current, int32_t maximum)
    {
        constexpr double Track = 61.0;
        return MCMechBay::KnobLeft + static_cast<int32_t>(static_cast<double>(current) / maximum * Track);
    }

    /// <summary>
    /// The force 'Mech of the 'MECH BAY's sliders test and pictures as it arrived: 30 armour points short, its engine two
    /// levels down (its knobs' floors there).
    /// </summary>
    constexpr int32_t ArrivedShort = 30;
    constexpr uint8_t ArrivedEngineHits = 2;
    /// <summary>The armour slider's steps in the sliders' test and the repair's picture.</summary>
    constexpr int32_t RepairSteps = 10;

    /// <summary>
    /// <paramref name="mech"/> as if it arrived <see cref="ArrivedShort"/> armour points short (from its first locations
    /// on) and its engine <see cref="ArrivedEngineHits"/> levels down, its knobs placed there (MCMechBay::PlaceKnobs).
    /// </summary>
    void ArriveDamaged(MCLogMech& mech)
    {
        int32_t missing = ArrivedShort;

        for (size_t location = 0; location < mech.Armor.size(); location++)
        {
            MCLogMech::ArmorPoints& armor = mech.Armor[location];
            const auto taken = static_cast<uint8_t>(std::min<int32_t>(armor.CurArmor, missing));
            armor.CurArmor = static_cast<uint8_t>(armor.CurArmor - taken);
            missing -= taken;
            mech.Arrived.Armor[location] = armor.CurArmor;
        }

        REQUIRE_EQ(missing, 0);
        MCMechBay::EngineOf(mech)->Hits = ArrivedEngineHits;
        mech.Arrived.EngineHits = ArrivedEngineHits;
        MCMechBay::PlaceKnobs(mech);
    }
}

/// <summary>
/// PURCHASING buys and sells by 1998's rules (logistics/MCStore): two of a store's part bought from the buy window (the
/// right button, the stepper's Right, Enter), a spare part sold back (dragged onto the store), a store 'Mech bought into
/// SPARE and sold for half its worth; a force 'Mech dragged onto the store refused with its reason; a spare into the force
/// by the right button and out again by ENTER.
/// </summary>
TEST_CASE_ISOLATED("game: PURCHASING buys and sells by 1998's rules, from the mouse and the keys")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    MCLogisticsTabs& tabs = screen.Logistics();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    constexpr int32_t Points = 200000;
    ResourcePoints = Points;
    screen.ChooseTab(MCCampaignScreen::MCTab::Purchasing);
    tabs.ChooseKind(MCLogisticsTabs::MCKind::Components);
    MCTestGame::RunFrame(FlowFrame);

    // Two of a part the store has two or more of.
    const std::vector<std::shared_ptr<MCLogInventoryItem>> store = StoreParts();
    const auto stocked = std::ranges::find_if(store, [](const auto& item) { return item->Count >= 2; });
    REQUIRE(stocked != store.end());
    const std::shared_ptr<MCLogInventoryItem> part = *stocked;
    const int32_t inStore = part->Count;
    const int32_t each = MasterComponentList[part->MasterID].ResourcePoints;
    const int32_t spares = SpareCount(part->MasterID);
    REQUIRE_EQ(tabs.StoreGoods().size(), store.size());
    ClickOn(ElementOf(screen, std::format("store{}", stocked - store.begin())), SDL_BUTTON_RIGHT);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(tabs.DealOpen());
    CHECK_EQ(tabs.DealTitle(), std::string("Buy"));
    MCScreenInput::RealKey(SDL_SCANCODE_RIGHT);
    MCTestGame::RunFrame(FlowFrame);
    CHECK_EQ(tabs.DealCountShown(), 2);
    CHECK_EQ(tabs.DealTotal(), std::format("{} RP", MCGroupedPoints(each * 2)));
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(!tabs.DealOpen());
    CHECK_EQ(ResourcePoints, Points - each * 2);
    CHECK_EQ(part->Count, inStore - 2);
    CHECK_EQ(SpareCount(part->MasterID), spares + 2);

    // One sold back, dragged from the INVENTORY (the left column with COMPONENTS) onto the store: half its price.
    const std::optional<size_t> row = SpareRow(part->MasterID);
    REQUIRE(row.has_value());
    DragTo(ElementOf(screen, std::format("stock{}", *row)), ElementOf(screen, "store0"));
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(tabs.DealOpen());
    CHECK_EQ(tabs.DealTitle(), std::string("Sell"));
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    MCTestGame::RunFrame(FlowFrame);
    CHECK_EQ(ResourcePoints, Points - each * 2 + each / 2);
    CHECK_EQ(SpareCount(part->MasterID), spares + 1);

    // A store 'Mech: into SPARE for its price; dragged back onto the store, sold for half its worth.
    tabs.ChooseKind(MCLogisticsTabs::MCKind::Mechs);
    MCTestGame::RunFrame(FlowFrame);
    const std::shared_ptr<MCPurMech> purMech = logistics->PurMechList->Mechs.front();
    MCPurMechData& data = *purMech->Variants[static_cast<size_t>(purMech->CurVariant)];
    data.NumAvailable = 2;
    const size_t sparesBefore = logistics->MechList->Mechs.size();
    const int32_t before = ResourcePoints;
    tabs.Pick(MCLogisticsTabs::MCPlace::Store, 0);
    ClickOn(ElementOf(screen, "deal"));
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(tabs.DealOpen());
    ClickOn(ElementOf(screen, "deal-confirm"));
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE_EQ(logistics->MechList->Mechs.size(), sparesBefore + 1);
    CHECK_EQ(ResourcePoints, before - data.Cost);
    CHECK_EQ(data.NumAvailable, 1);
    REQUIRE_EQ(tabs.SpareCards().size(), logistics->MechList->Mechs.size() + logistics->VehicleList->Vehicles.size());
    MCTestGame::RunFrame(FlowFrame);
    Shot("purchase_spare");

    const std::shared_ptr<MCLogMech> bought = logistics->MechList->Mechs.back();
    bought->CalcMechCost(false);
    const int32_t worth = bought->ResourcePoints;
    const int32_t afterBuy = ResourcePoints;
    DragTo(ElementOf(screen, std::format("spare{}", logistics->MechList->Mechs.size() - 1)),
           ElementOf(screen, "store0"));
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(tabs.DealOpen());
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    MCTestGame::RunFrame(FlowFrame);
    CHECK_EQ(logistics->MechList->Mechs.size(), sparesBefore);
    CHECK_EQ(ResourcePoints, afterBuy + worth / 2);

    // A force 'Mech onto the store: refused, its reason on its card.
    const size_t force = logistics->ForceMechList->Mechs.size();
    DragTo(ElementOf(screen, "force0"), ElementOf(screen, "store0"));
    MCTestGame::RunFrame(FlowFrame);
    CHECK(!tabs.DealOpen());
    CHECK_EQ(logistics->ForceMechList->Mechs.size(), force);
    REQUIRE(!tabs.ForceCards().empty());
    CHECK_EQ(tabs.ForceCards()[0].Why, std::string("In your force"));

    // Bought again, then into the force by the right button and out by ENTER.
    tabs.Pick(MCLogisticsTabs::MCPlace::Store, 0);
    tabs.PressDeal();
    tabs.DealConfirm();
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE_EQ(logistics->MechList->Mechs.size(), sparesBefore + 1);
    ClickOn(ElementOf(screen, std::format("spare{}", logistics->MechList->Mechs.size() - 1)), SDL_BUTTON_RIGHT);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE_EQ(logistics->ForceMechList->Mechs.size(), force + 1);
    CHECK_EQ(logistics->MechList->Mechs.size(), sparesBefore);
    KeyOn(ElementOf(screen, std::format("force{}", logistics->ForceMechList->Mechs.size() - 1)), SDL_SCANCODE_RETURN);
    CHECK_EQ(logistics->ForceMechList->Mechs.size(), force);
    CHECK_EQ(logistics->MechList->Mechs.size(), sparesBefore + 1);
}

/// <summary>
/// A part's last copy sold, the part still picked: its SELL offers no more (no sale of none, no points for it, the count
/// never below none).
/// </summary>
TEST_CASE_ISOLATED("game: PURCHASING sells a part's last copy, then offers no more")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    MCLogisticsTabs& tabs = screen.Logistics();
    constexpr int32_t Points = 200000;
    ResourcePoints = Points;
    screen.ChooseTab(MCCampaignScreen::MCTab::Purchasing);
    tabs.ChooseKind(MCLogisticsTabs::MCKind::Components);
    MCTestGame::RunFrame(FlowFrame);

    // One bought, then every copy owned sold, one at a time.
    const std::vector<std::shared_ptr<MCLogInventoryItem>> store = StoreParts();
    const auto stocked = std::ranges::find_if(store, [](const auto& item) { return item->Count >= 1; });
    REQUIRE(stocked != store.end());
    const uint8_t masterID = (*stocked)->MasterID;
    tabs.Pick(MCLogisticsTabs::MCPlace::Store, static_cast<size_t>(stocked - store.begin()));
    tabs.PressDeal();
    tabs.DealConfirm();
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(SpareCount(masterID) >= 1);

    while (SpareCount(masterID) > 0)
    {
        const std::optional<size_t> row = SpareRow(masterID);
        REQUIRE(row.has_value());
        tabs.Pick(MCLogisticsTabs::MCPlace::Stock, *row);
        tabs.PressDeal();
        REQUIRE(tabs.DealOpen());
        tabs.DealConfirm();
        MCTestGame::RunFrame(FlowFrame);
    }

    // SELL again: nothing to sell.
    const int32_t sold = ResourcePoints;
    tabs.PressDeal();
    MCTestGame::RunFrame(FlowFrame);
    CHECK(!tabs.DealOpen());
    CHECK_EQ(ResourcePoints, sold);
    CHECK_EQ(SpareCount(masterID), 0);
}

/// <summary>
/// REFIT ALL works only with a weapon, equipment or jammer damaged, as 1998's button (its CanRepairItems): with none, the
/// 'Mech stays in the drop; with one, it leaves it for the refit.
/// </summary>
TEST_CASE_ISOLATED("game: the 'MECH BAY's REFIT ALL does nothing with nothing damaged")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    MCLogisticsTabs& tabs = screen.Logistics();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    const std::shared_ptr<MCLogMech> mech = logistics->ForceMechList->Mechs.front();

    // Every ready unit into the drop, the worked 'Mech among them.
    for (const std::shared_ptr<MCLogPart>& unit : MCDeploy::ReadyUnits())
    {
        // A unit too heavy for what is left stays out: only the worked 'Mech must be in.
        static_cast<void>(MCDeploy::Send(*unit, std::nullopt));
    }

    REQUIRE(mech->Deployed != 0);
    screen.ChooseTab(MCCampaignScreen::MCTab::MechBay);
    tabs.Pick(MCLogisticsTabs::MCPlace::Force, 0);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(tabs.LabUnit() == mech);
    REQUIRE(!MCMechBay::ItemsDamaged(*mech));
    tabs.RefitAll();
    MCTestGame::RunFrame(FlowFrame);
    CHECK(mech->Deployed != 0);

    // A weapon damaged: REFIT ALL takes the 'Mech out of the drop to refit it.
    const auto weapon =
        std::ranges::find_if(mech->Inventory->Items, [](const auto& item)
                             { return IsWeapon(ComponentForm(item->MasterID)) && !item->Stats.empty(); });
    REQUIRE(weapon != mech->Inventory->Items.end());
    (*weapon)->Stats.front()->Hits = 1;
    REQUIRE(MCMechBay::ItemsDamaged(*mech));
    tabs.RefitAll();
    MCTestGame::RunFrame(FlowFrame);
    CHECK(mech->Deployed == 0);
}

/// <summary>
/// The 'MECH BAY's sliders repair and undo point by point as 1998's knobs (MCMechBay::MoveKnob, each move undone to where
/// the 'Mech arrived and repaired again to the knob's place): armour ten steps right repairs what ten of 1998's pixels
/// do, ten steps back refunds it all; short of points it stops where they run out; the engine a level a step; REPAIR ALL
/// the rest at 1998's prices.
/// </summary>
TEST_CASE_ISOLATED("game: the 'MECH BAY's sliders repair and undo point by point as 1998's knobs")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    MCLogisticsTabs& tabs = screen.Logistics();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    const std::shared_ptr<MCLogMech> mech = logistics->ForceMechList->Mechs.front();

    // It arrived 30 armour points short and its engine two levels down.
    ArriveDamaged(*mech);
    constexpr int32_t Points = 100000;
    ResourcePoints = Points;
    screen.ChooseTab(MCCampaignScreen::MCTab::MechBay);
    tabs.Pick(MCLogisticsTabs::MCPlace::Force, 0);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(tabs.LabUnit() == mech);

    // 1998's knob: from where it arrived, a point each 61/total pixels.
    const int32_t total = MCMechBay::Points(mech->Armor, true);
    const int32_t arrived = MCMechBay::Points(mech->Armor, false);
    const int32_t start = KnobFor(arrived, total);
    const auto scale = static_cast<float>(61.0 / total);
    const auto wanted = static_cast<int32_t>(static_cast<double>(RepairSteps) / static_cast<double>(scale));
    CHECK_EQ(tabs.ArmorRepair().Knob, start);
    KeyOn(ElementOf(screen, "armor"), SDL_SCANCODE_RIGHT, RepairSteps);
    CHECK_EQ(tabs.ArmorRepair().Knob, start + RepairSteps);
    CHECK_EQ(MCMechBay::Points(mech->Armor, false), arrived + wanted);
    CHECK_EQ(ResourcePoints, Points - wanted * logistics->ArmorCost);
    Shot("mechbay_repair");

    // Back to where it arrived: every point refunded.
    KeyOn(ElementOf(screen, "armor"), SDL_SCANCODE_LEFT, RepairSteps);
    CHECK_EQ(MCMechBay::Points(mech->Armor, false), arrived);
    CHECK_EQ(ResourcePoints, Points);
    // Not past where it arrived.
    KeyOn(ElementOf(screen, "armor"), SDL_SCANCODE_LEFT);
    CHECK_EQ(tabs.ArmorRepair().Knob, start);
    CHECK_EQ(ResourcePoints, Points);

    // Short of points: as far as they go, the knob where they ran out.
    constexpr int32_t Affordable = 3;
    ResourcePoints = logistics->ArmorCost * Affordable + 1;
    KeyOn(ElementOf(screen, "armor"), SDL_SCANCODE_RIGHT, RepairSteps);
    CHECK_EQ(MCMechBay::Points(mech->Armor, false), arrived + Affordable);
    CHECK_EQ(ResourcePoints, 1);
    CHECK_EQ(tabs.ArmorRepair().Knob,
             static_cast<int32_t>(static_cast<double>(Affordable) * static_cast<double>(scale) + start));
    KeyOn(ElementOf(screen, "armor"), SDL_SCANCODE_LEFT, RepairSteps);
    CHECK_EQ(ResourcePoints, logistics->ArmorCost * Affordable + 1);

    // The engine: a level a step, each at the engine's cost, and back.
    ResourcePoints = Points;
    KeyOn(ElementOf(screen, "engine"), SDL_SCANCODE_RIGHT);
    CHECK_EQ(MCMechBay::EngineOf(*mech)->Hits, ArrivedEngineHits - 1);
    CHECK_EQ(ResourcePoints, Points - logistics->EngineCost);
    KeyOn(ElementOf(screen, "engine"), SDL_SCANCODE_RIGHT);
    CHECK_EQ(MCMechBay::EngineOf(*mech)->Hits, 0);
    KeyOn(ElementOf(screen, "engine"), SDL_SCANCODE_LEFT, 2);
    CHECK_EQ(MCMechBay::EngineOf(*mech)->Hits, ArrivedEngineHits);
    CHECK_EQ(ResourcePoints, Points);

    // REPAIR ALL: the engine, the structure and the armour whole at their prices.
    ClickOn(ElementOf(screen, "repair-all"));
    MCTestGame::RunFrame(FlowFrame);
    CHECK_EQ(MCMechBay::Points(mech->Armor, false), total);
    CHECK_EQ(MCMechBay::EngineOf(*mech)->Hits, 0);
    CHECK_EQ(ResourcePoints, Points - ArrivedShort * logistics->ArmorCost - ArrivedEngineHits * logistics->EngineCost);
}

namespace
{
    /// <summary>The structure points the knobs' test's 'Mech arrived short, and the steps its slider takes each way.</summary>
    constexpr int32_t ArrivedStructureShort = 10;
    constexpr int32_t StructureSteps = 3;

    /// <summary>Where slider <paramref name="id"/> of <paramref name="screen"/> draws its knob: its value.</summary>
    int32_t ShownKnob(const MCCampaignScreen& screen, const std::string& id)
    {
        const std::string value = ElementOf(screen, id).GetAttribute<Rml::String>("value", "");
        REQUIRE(!value.empty());
        return static_cast<int32_t>(std::lround(std::stod(value)));
    }

    /// <summary>
    /// Slider <paramref name="id"/> moved by <paramref name="key"/> <paramref name="steps"/> times, its knob drawn where
    /// the 'MECH BAY's model has it after each (<paramref name="knob"/>).
    /// </summary>
    void StepAndCheckKnob(MCCampaignScreen& screen, const std::string& id, SDL_Scancode key, int32_t steps,
                          const std::function<int32_t()>& knob)
    {
        for (int32_t step = 0; step < steps; ++step)
        {
            KeyOn(ElementOf(screen, id), key);
            const MCTest::Scope scope(
                std::format("{} after {} steps {}", id, step + 1, key == SDL_SCANCODE_LEFT ? "left" : "right"));
            CHECK_EQ(ShownKnob(screen, id), knob());
        }
    }
}

/// <summary>
/// The 'MECH BAY's internal structure and engine sliders draw their knobs where each move leaves them, to the left as to
/// the right: after every step, each slider's value (where RmlUi draws its knob) is the model's knob. 1998's structure
/// and engine knobs, dragged left, were drawn again only after the repair (gone with the 1998 screens).
/// </summary>
TEST_CASE_ISOLATED("game: the 'MECH BAY's structure and engine sliders draw their knobs where each move leaves them")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    MCLogisticsTabs& tabs = screen.Logistics();
    const std::shared_ptr<MCLogMech> mech = GlobalLog()->ForceMechList->Mechs.front();

    // It arrived damaged as the sliders' test has it, and its internal structure short too (each location keeps a point).
    ArriveDamaged(*mech);
    int32_t missing = ArrivedStructureShort;

    for (size_t location = 0; location < mech->Internals.size(); location++)
    {
        MCLogMech::ArmorPoints& internal = mech->Internals[location];
        const auto taken = static_cast<uint8_t>(std::min<int32_t>(std::max(internal.CurArmor - 1, 0), missing));
        internal.CurArmor = static_cast<uint8_t>(internal.CurArmor - taken);
        missing -= taken;
        mech->Arrived.Internals[location] = internal.CurArmor;
    }

    REQUIRE_EQ(missing, 0);
    MCMechBay::PlaceKnobs(*mech);
    constexpr int32_t Points = 100000;
    ResourcePoints = Points;
    screen.ChooseTab(MCCampaignScreen::MCTab::MechBay);
    tabs.Pick(MCLogisticsTabs::MCPlace::Force, 0);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(tabs.LabUnit() == mech);

    // The structure: right, then back left to where it arrived.
    const auto structure = [&tabs] { return tabs.StructureRepair().Knob; };
    const int32_t structureStart = structure();
    CHECK_EQ(ShownKnob(screen, "structure"), structureStart);
    StepAndCheckKnob(screen, "structure", SDL_SCANCODE_RIGHT, StructureSteps, structure);
    CHECK(structure() > structureStart);
    StepAndCheckKnob(screen, "structure", SDL_SCANCODE_LEFT, StructureSteps, structure);
    CHECK_EQ(structure(), structureStart);

    // The engine: a level right each step, then back.
    const auto engine = [&tabs] { return tabs.EngineRepair().Knob; };
    const int32_t engineStart = engine();
    CHECK_EQ(ShownKnob(screen, "engine"), engineStart);
    StepAndCheckKnob(screen, "engine", SDL_SCANCODE_RIGHT, ArrivedEngineHits, engine);
    CHECK_EQ(engine(), engineStart + ArrivedEngineHits);
    StepAndCheckKnob(screen, "engine", SDL_SCANCODE_LEFT, ArrivedEngineHits, engine);
    CHECK_EQ(engine(), engineStart);
    CHECK_EQ(ResourcePoints, Points);
}

/// <summary>
/// The 'MECH BAY moves parts and MechWarriors as 1998's drags: a fitted part taken out by the right button goes to the
/// INVENTORY, its tons freed; ENTER on its INVENTORY card fits it back; a part too heavy is dim with its reason and
/// stays; the MechWarrior out by the right button joins the INVENTORY's MechWarriors and boards again by its card.
/// </summary>
TEST_CASE_ISOLATED("game: the 'MECH BAY fits and takes out parts and moves MechWarriors as 1998's drags")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    MCLogisticsTabs& tabs = screen.Logistics();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    const std::shared_ptr<MCLogMech> mech = logistics->ForceMechList->Mechs.front();
    screen.ChooseTab(MCCampaignScreen::MCTab::MechBay);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(tabs.LabUnit() == mech);
    REQUIRE(!tabs.FittedRows().empty());

    // The first fitted part out: to the spares, its tons freed.
    const std::string name = tabs.FittedRows()[0].Name;
    const auto item = std::ranges::find(mech->Inventory->Items, name, &MCLogInventoryItem::Name);
    REQUIRE(item != mech->Inventory->Items.end());
    const uint8_t masterID = (*item)->MasterID;
    const int32_t spares = SpareCount(masterID);
    const float used = mech->UsedTonnage;
    const size_t fitted = tabs.FittedRows().size();
    ClickOn(ElementOf(screen, "fit0"), SDL_BUTTON_RIGHT);
    MCTestGame::RunFrame(FlowFrame);
    CHECK_EQ(SpareCount(masterID), spares + 1);
    CHECK_EQ(tabs.FittedRows().size(), fitted - 1);
    CHECK(mech->UsedTonnage < used);

    // ENTER on its INVENTORY card fits it back.
    const std::optional<size_t> row = SpareRow(masterID);
    REQUIRE(row.has_value());
    REQUIRE(*row < tabs.StockGoods().size());
    CHECK(!tabs.StockGoods()[*row].Off);
    Rml::ElementList cards;
    screen.Document()->get().GetElementById("inventory")->GetElementsByTagName(cards, "div");
    const auto card = std::ranges::find_if(cards, [&row](const Rml::Element* element)
                                           { return element->GetId() == std::format("stock{}", *row); });
    REQUIRE(card != cards.end());
    KeyOn(**card, SDL_SCANCODE_RETURN);
    CHECK_EQ(SpareCount(masterID), spares);
    CHECK_EQ(tabs.FittedRows().size(), fitted);
    CHECK_EQ(mech->UsedTonnage, used);
    Shot("mechbay_parts");

    // The MechWarrior out: into the INVENTORY's MechWarriors; its card boards the 'Mech again.
    const size_t pilots = logistics->WarriorList->Warriors.size();
    REQUIRE(mech->PilotIndex >= 0);
    ClickOn(ElementOf(screen, "pilot"), SDL_BUTTON_RIGHT);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(mech->PilotIndex < 0);
    // Its card and the lab show no MechWarrior (not the list's first).
    REQUIRE(!tabs.ForceCards().empty());
    CHECK(tabs.ForceCards()[0].Who.empty());
    REQUIRE_EQ(logistics->WarriorList->Warriors.size(), pilots + 1);
    tabs.ChooseStock(MCLogisticsTabs::MCStock::MechWarriors);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE_EQ(tabs.StockGoods().size(), pilots + 1);
    CHECK(!tabs.StockGoods()[0].Off);
    Shot("mechbay_pilots");
    tabs.Send(MCLogisticsTabs::MCPlace::Stock, 0);
    MCTestGame::RunFrame(FlowFrame);
    CHECK(mech->PilotIndex >= 0);
    CHECK_EQ(logistics->WarriorList->Warriors.size(), pilots);
}

/// <summary>
/// The 'MECH BAY lists a 'Mech's fitted parts in 1998's order (its weapon list's, MCMechBay::FittedCopies): the
/// short-, medium- and long-range weapons, each by damage, lowest first, then the equipment, each copy's damage on its
/// own row. Two weapons the inventory lists the other way round (by master id, the higher first) show the lower damage
/// first.
/// </summary>
TEST_CASE_ISOLATED("game: the 'MECH BAY lists a 'Mech's weapons as 1998's, by range, each by damage, lowest first")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    MCLogisticsTabs& tabs = screen.Logistics();
    const std::shared_ptr<MCLogMech> mech = GlobalLog()->ForceMechList->Mechs.front();
    const std::vector<std::shared_ptr<MCLogInventoryItem>>& items = mech->Inventory->Items;
    const auto damage = [](uint8_t masterID) { return MasterComponentList[masterID].Damage; };
    const auto carried = [&items](uint8_t masterID)
    { return std::ranges::contains(items, masterID, &MCLogInventoryItem::MasterID); };

    // Two weapons of one range the 'Mech doesn't carry, the one of the higher master id the more damaging.
    std::optional<std::pair<uint8_t, uint8_t>> pair;

    for (int32_t higher = NumMasterComponents() - 1; higher > 0 && !pair.has_value(); --higher)
    {
        for (int32_t lower = higher - 1; lower >= 0 && !pair.has_value(); --lower)
        {
            const auto first = static_cast<uint8_t>(higher);
            const auto second = static_cast<uint8_t>(lower);

            if (IsWeapon(ComponentForm(first)) && MCMechBay::FitListOf(first) == MCMechBay::FitListOf(second) &&
                !carried(first) && !carried(second) && damage(second) < damage(first))
            {
                pair = {first, second};
            }
        }
    }

    REQUIRE(pair.has_value());
    const auto [first, second] = *pair;
    mech->CurTonnage += MCMechBay::FittedTons(first) + MCMechBay::FittedTons(second);
    REQUIRE(MCMechBay::Fit(*mech, second));
    REQUIRE(MCMechBay::Fit(*mech, first));
    const auto itemOf = [&items](uint8_t masterID)
    { return *std::ranges::find(items, masterID, &MCLogInventoryItem::MasterID); };
    const auto inventoryAt = [&items](const std::shared_ptr<MCLogInventoryItem>& item)
    { return std::ranges::find(items, item) - items.begin(); };
    REQUIRE(inventoryAt(itemOf(first)) < inventoryAt(itemOf(second)));
    REQUIRE_EQ(itemOf(second)->Stats.size(), size_t{1});
    itemOf(second)->Stats.front()->Hits = 1;

    screen.ChooseTab(MCCampaignScreen::MCTab::MechBay);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(tabs.LabUnit() == mech);

    // The rows are the fitted copies in their order; by list, each weapon list by damage, lowest first.
    const std::vector<MCLogisticsTabs::MCFitRow>& rows = tabs.FittedRows();
    const std::vector<MCMechBay::MCFittedCopy> copies = MCMechBay::FittedCopies(*mech);
    REQUIRE_EQ(rows.size(), copies.size());

    for (size_t at = 0; at < rows.size(); ++at)
    {
        CHECK_EQ(rows[at].Name, copies[at].Item->Name);
        CHECK_EQ(rows[at].State.empty(), copies[at].Copy->Hits == 0);

        if (at == 0)
        {
            continue;
        }

        const uint8_t before = copies[at - 1].Item->MasterID;
        const uint8_t now = copies[at].Item->MasterID;
        const MCMechBay::MCFitList beforeList = *MCMechBay::FitListOf(before);
        const MCMechBay::MCFitList nowList = *MCMechBay::FitListOf(now);
        CHECK(beforeList <= nowList);

        if (beforeList == nowList && nowList != MCMechBay::MCFitList::Equipment)
        {
            CHECK(damage(before) <= damage(now));
        }
    }

    // The two: the lower damage first, its copy's damage on its row.
    const auto rowOf = [&copies](uint8_t masterID)
    {
        return std::ranges::find_if(copies, [masterID](const MCMechBay::MCFittedCopy& copy)
                                    { return copy.Item->MasterID == masterID; }) -
               copies.begin();
    };

    REQUIRE(rowOf(first) < static_cast<ptrdiff_t>(rows.size()));
    REQUIRE(rowOf(second) < static_cast<ptrdiff_t>(rows.size()));
    CHECK(rowOf(second) < rowOf(first));
    CHECK(!rows[static_cast<size_t>(rowOf(second))].State.empty());
    CHECK(rows[static_cast<size_t>(rowOf(first))].State.empty());
}

/// <summary>
/// PURCHASING (the store's 'Mechs, the first picked; its parts, the first in stock picked; the buy window) and the 'MECH
/// BAY (the force's first 'Mech; then arrived damaged and its armour partly repaired) on the software renderer, the whole
/// window at 640x480, 1280x720 and 1280x1024, the hashes recorded: the Mac's and Windows' alike with the game's data.
/// With <c>--shots</c> each is saved.
/// </summary>
TEST_CASE_ISOLATED("game: PURCHASING and the 'MECH BAY draw the same on every machine")
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
    MCLogisticsTabs& tabs = screen.Logistics();
    REQUIRE(MCRmlMenus::Running().has_value());
    double clock = 0.0;
    MCRmlMenus::Running()->get().SetClock([&clock] { return clock; });
    ResourcePoints = ShotPoints;

    screen.ChooseTab(MCCampaignScreen::MCTab::Purchasing);
    tabs.Pick(MCLogisticsTabs::MCPlace::Store, 0);
    DrawnEach(*display, clock, "purchase", {0x41c12be7, 0x9d4e7376, 0x1b434715});
    tabs.ChooseKind(MCLogisticsTabs::MCKind::Components);
    tabs.Pick(MCLogisticsTabs::MCPlace::Store, FirstInStore(tabs));
    DrawnEach(*display, clock, "purchase_parts", {0xabd7c4f8, 0x328991b0, 0x58cc89da});
    tabs.PressDeal();
    REQUIRE(tabs.DealOpen());
    DrawnEach(*display, clock, "purchase_buy", {0x9ea647a9, 0xa85d2176, 0x797dbed3});
    tabs.DealCancel();

    screen.ChooseTab(MCCampaignScreen::MCTab::MechBay);
    DrawnEach(*display, clock, "mechbay", {0xedf3c776, 0xfdf470f6, 0xf39fa44c});
    ArriveDamaged(*GlobalLog()->ForceMechList->Mechs.front());
    tabs.Pick(MCLogisticsTabs::MCPlace::Force, 0);
    KeyOn(ElementOf(screen, "armor"), SDL_SCANCODE_RIGHT, RepairSteps);
    DrawnEach(*display, clock, "mechbay_repair", {0x6f67538f, 0xe21425df, 0x0156003e});
}

/// <summary>
/// PURCHASING and the 'MECH BAY at every window the players' reports name: the store's four kinds (each first good
/// picked; MECHWARRIORS and COMPONENTS with the INVENTORY on the left), the buy window, a bought 'Mech in SPARE; the 'MECH
/// BAY on the force's first 'Mech arrived damaged, the INVENTORY's MechWarriors, and a spare 'Mech: every element inside
/// the window, no two siblings overlapping, no text spilling or cut (MCRmlChecks::BoxProblems); every text at least 8
/// units high, at its contrast (MCRmlChecks::Illegible).
/// </summary>
TEST_CASE_ISOLATED("game: PURCHASING and the 'MECH BAY lay out and read at every window the players' reports name")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    MCLogisticsTabs& tabs = screen.Logistics();
    REQUIRE(MCRmlMenus::Running().has_value());
    MCRmlMenus& menus = MCRmlMenus::Running()->get();
    ResourcePoints = ShotPoints;
    screen.ChooseTab(MCCampaignScreen::MCTab::Purchasing);

    constexpr std::array<std::pair<MCLogisticsTabs::MCKind, std::string_view>, 4> Kinds = {{
        {MCLogisticsTabs::MCKind::Mechs, "purchase mechs"},
        {MCLogisticsTabs::MCKind::MechWarriors, "purchase mechwarriors"},
        {MCLogisticsTabs::MCKind::Components, "purchase components"},
        {MCLogisticsTabs::MCKind::Vehicles, "purchase vehicles"},
    }};

    for (const auto& [kind, what] : Kinds)
    {
        tabs.ChooseKind(kind);
        tabs.Pick(MCLogisticsTabs::MCPlace::Store, 0);
        MCTestGame::RunFrame(FlowFrame);
        CheckLaidOut(menus, what);
    }

    tabs.ChooseKind(MCLogisticsTabs::MCKind::Components);
    tabs.Pick(MCLogisticsTabs::MCPlace::Store, FirstInStore(tabs));
    tabs.PressDeal();
    REQUIRE(tabs.DealOpen());
    CheckLaidOut(menus, "purchase buy");
    tabs.DealCancel();

    // A 'Mech bought into SPARE (the store's first, one put in stock).
    tabs.ChooseKind(MCLogisticsTabs::MCKind::Mechs);
    const std::shared_ptr<MCPurMech> purMech = GlobalLog()->PurMechList->Mechs.front();
    purMech->Variants[static_cast<size_t>(purMech->CurVariant)]->NumAvailable = 1;
    tabs.Pick(MCLogisticsTabs::MCPlace::Store, 0);
    tabs.PressDeal();
    REQUIRE(tabs.DealOpen());
    tabs.DealConfirm();
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(!tabs.SpareCards().empty());
    CheckLaidOut(menus, "purchase spare");

    screen.ChooseTab(MCCampaignScreen::MCTab::MechBay);
    ArriveDamaged(*GlobalLog()->ForceMechList->Mechs.front());
    tabs.Pick(MCLogisticsTabs::MCPlace::Force, 0);
    MCTestGame::RunFrame(FlowFrame);
    CheckLaidOut(menus, "mechbay repair");
    tabs.ChooseStock(MCLogisticsTabs::MCStock::MechWarriors);
    MCTestGame::RunFrame(FlowFrame);
    CheckLaidOut(menus, "mechbay mechwarriors");
    tabs.Pick(MCLogisticsTabs::MCPlace::Spare, 0);
    MCTestGame::RunFrame(FlowFrame);
    CheckLaidOut(menus, "mechbay spare");
}

namespace
{
    /// <summary>The remade recording's window (the frames' 16:9 size) and the frames each step settles over.</summary>
    constexpr MCMenusPictures::MCWindowSize RecordedWindow{1280, 720};
    constexpr int32_t RecordedSettleFrames = 8;
    /// <summary>The store's stock for the recording, as the 1998 recording's (every good two).</summary>
    constexpr int32_t RecordedStock = 2;
    constexpr int32_t RecordedPoints = 100000;

    /// <summary>The mouse over <paramref name="element"/>'s middle (what's under the pointer shows in the details).</summary>
    void Hover(const Rml::Element& element)
    {
        const Rml::Vector2f at = Middle(element);
        MCScreenInput::PixelMove(at.x, at.y, FlowFrame);
    }

    /// <summary>A switch's choice <paramref name="choice"/> clicked (the tabs, the kinds, the INVENTORY's).</summary>
    void ClickChoice(const MCCampaignScreen& screen, const std::string& id, int choice)
    {
        Rml::Element& choices = ElementOf(screen, id);
        REQUIRE(choices.GetNumChildren() > choice);
        ClickOn(*choices.GetChild(choice));
    }

    /// <summary>The good named <paramref name="who"/> among <paramref name="goods"/>: its card's place.</summary>
    size_t GoodNamed(const std::vector<MCLogisticsTabs::MCGood>& goods, std::string_view who)
    {
        const auto found = std::ranges::find(goods, who, &MCLogisticsTabs::MCGood::Who);
        REQUIRE(found != goods.end());
        return static_cast<size_t>(found - goods.begin());
    }

    /// <summary>
    /// The 'MECH BAY's INVENTORY card <paramref name="index"/> (PURCHASING's INVENTORY, hidden, has the same ids, and
    /// first in the document).
    /// </summary>
    Rml::Element& InventoryCard(const MCCampaignScreen& screen, size_t index)
    {
        Rml::Element* card = ElementOf(screen, "inventory").QuerySelector(std::format("#stock{}", index));
        REQUIRE(card != nullptr);
        return *card;
    }

    /// <summary>The card of the unit <paramref name="unit"/> on the left (the force's or SPARE's): its id.</summary>
    std::string UnitCardId(const MCLogisticsTabs& tabs, const std::shared_ptr<MCLogPart>& unit)
    {
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();
        const size_t force = tabs.ForceCards().size();

        for (size_t card = 0; card < force + tabs.SpareCards().size(); card++)
        {
            const MCLogisticsTabs::MCPlace place =
                card < force ? MCLogisticsTabs::MCPlace::Force : MCLogisticsTabs::MCPlace::Spare;
            const size_t index = card < force ? card : card - force;

            if (tabs.UnitAt(place, index) == unit)
            {
                return std::format("{}{}", place == MCLogisticsTabs::MCPlace::Force ? "force" : "spare", index);
            }
        }

        FAIL("the unit has no card on the left");
        return {};
    }
}

/// <summary>
/// The logistics recording on the remade screens (SPEC-menus.md's Testing: FrameHashTests' logistics recording's
/// purchases and mech lab steps, the same steps by the remade screens' own controls: a 'Mech, a part and a vehicle bought
/// by a drag onto the left, two at once, a variant, the list scrolled, a buy too poor refused and its message closed,
/// each kind's tab, what's under the pointer; in the 'MECH BAY a second 'Mech, a weapon taken out and a laser fitted,
/// the MechWarriors, a spare 'Mech into the force and units out of it, the lab scrolled): each step's window on the
/// software renderer at 1280x720 once settled, its hash recorded, and every frame on the way folded into one more. With
/// <c>--shots</c> each step is saved.
/// </summary>
TEST_CASE_ISOLATED("game: the remade PURCHASING and 'MECH BAY record the logistics recording's steps")
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
    REQUIRE(MCMenusPictures::SetWindowPixels(*display, RecordedWindow.Width, RecordedWindow.Height));
    MCCampaignScreen& screen = Shown();
    MCLogisticsTabs& tabs = screen.Logistics();
    REQUIRE(MCRmlMenus::Running().has_value());
    double clock = 0.0;
    MCRmlMenus::Running()->get().SetClock([&clock] { return clock; });
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    std::shared_ptr<MCLogPart> boughtMech;
    // The screen laid out at the recording's window before the first click finds its tab.
    MCTestGame::RunFrame(FlowFrame);

    // Every frame on the way (the cards' slides, the buy window opening) folded into one more hash.
    uint32_t frames = MCMenusPictures::FnvBasis;
    const auto settle = [&]
    {
        for (int32_t frame = 0; frame < RecordedSettleFrames; frame++)
        {
            clock += SettleSeconds / RecordedSettleFrames;
            MCTestGame::RunFrame(FlowFrame);
            const auto window = display->ReadWindow();
            REQUIRE(window.has_value());
            frames = (frames ^ MCMenusPictures::PictureHash(*window)) * MCMenusPictures::FnvPrime;
        }
    };

    struct MCStep
    {
        std::string_view Name;
        std::function<void()> Action;
        uint32_t Expected;
    };

    const std::array<MCStep, 38> steps = {{
        {"purchase_again",
         [&]
         {
             // Enough points and stock to buy with, as the 1998 recording's.
             ResourcePoints = RecordedPoints;
             MCTestGame::StockStore(RecordedStock);
             ClickChoice(screen, "tabs", static_cast<int>(MCCampaignScreen::MCTab::Purchasing));
             CHECK(screen.Tab() == MCCampaignScreen::MCTab::Purchasing);
         },
         0x9a4baf2d},
        {"purchase_buy_drag", [&] { DragTo(ElementOf(screen, "store0"), ElementOf(screen, "units")); }, 0x595f4b77},
        {"purchase_quantity_up", [] { MCScreenInput::RealKey(SDL_SCANCODE_RIGHT); }, 0x2b6a7b1a},
        {"purchase_accept",
         [&]
         {
             ClickOn(ElementOf(screen, "deal-confirm"));
             MCTestGame::RunFrame(FlowFrame);
             REQUIRE(!logistics->MechList->Mechs.empty());
             boughtMech = logistics->MechList->Mechs.back();
         },
         0x7e021467},
        {"purchase_inventory_row", [&] { ClickOn(ElementOf(screen, UnitCardId(tabs, boughtMech))); }, 0xf7286cf7},
        {"purchase_variant",
         [&]
         {
             ClickOn(ElementOf(screen, "store0"));
             ClickChoice(screen, "variants", 1);
         },
         0x4df7563b},
        {"purchase_scroll",
         [&]
         {
             Hover(ElementOf(screen, "store0"));
             MCScreenInput::RealWheel(-1);
         },
         0x6a4b3587},
        {"purchase_too_poor",
         [&]
         {
             ResourcePoints = 0;
             DragTo(ElementOf(screen, "store0"), ElementOf(screen, "units"));
         },
         0x5e479cec},
        {"purchase_message_ok", [] { MCScreenInput::RealKey(SDL_SCANCODE_RETURN); }, 0x8cea8500},
        {"purchase_inventory_info", [&] { Hover(ElementOf(screen, "force0")); }, 0xd17a0c62},
        {"purchase_pilots_tab",
         [&] { ClickChoice(screen, "kinds", static_cast<int>(MCLogisticsTabs::MCKind::MechWarriors)); }, 0x60b3a789},
        {"purchase_components_tab",
         [&]
         {
             ResourcePoints = RecordedPoints;
             ClickChoice(screen, "kinds", static_cast<int>(MCLogisticsTabs::MCKind::Components));
         },
         0xb3c329a9},
        {"purchase_component_drag",
         [&] { DragTo(ElementOf(screen, std::format("store{}", FirstInStore(tabs))), ElementOf(screen, "owned")); },
         0x99eaf0cd},
        {"purchase_component_accept", [&] { ClickOn(ElementOf(screen, "deal-confirm")); }, 0x478e8121},
        {"purchase_vehicles_tab",
         [&] { ClickChoice(screen, "kinds", static_cast<int>(MCLogisticsTabs::MCKind::Vehicles)); }, 0xb1acbb15},
        {"purchase_vehicle_drag",
         [&] { DragTo(ElementOf(screen, std::format("store{}", FirstInStore(tabs))), ElementOf(screen, "units")); },
         0x4d94d9d5},
        {"purchase_vehicle_accept", [&] { ClickOn(ElementOf(screen, "deal-confirm")); }, 0xa60c05a4},
        {"purchase_mechs_tab", [&] { ClickChoice(screen, "kinds", static_cast<int>(MCLogisticsTabs::MCKind::Mechs)); },
         0xf256f67d},
        {"repair_again", [&] { ClickChoice(screen, "tabs", static_cast<int>(MCCampaignScreen::MCTab::MechBay)); },
         0x96e9ee43},
        {"repair_payload", [&] { ClickOn(ElementOf(screen, "fit0")); }, 0x5d3ad8af},
        {"repair_second_mech", [&] { ClickOn(ElementOf(screen, "force1")); }, 0x3e3d7c81},
        {"repair_component_info", [&] { Hover(InventoryCard(screen, 0)); }, 0x1fa15f28},
        {"repair_weapon_info", [&] { Hover(ElementOf(screen, "fit0")); }, 0x394879aa},
        {"repair_mech_info", [&] { Hover(ElementOf(screen, "force1")); }, 0x0c62fc39},
        {"repair_weapon_off", [&] { DragTo(ElementOf(screen, "fit0"), ElementOf(screen, "inventory")); }, 0xbc4f2b3f},
        {"repair_mount_laser",
         [&] { DragTo(InventoryCard(screen, GoodNamed(tabs.StockGoods(), "Laser")), ElementOf(screen, "lab")); },
         0x9b980f80},
        {"repair_pilots_tab", [&]
         { ClickChoice(screen, "stock-kinds", static_cast<int>(MCLogisticsTabs::MCStock::MechWarriors)); }, 0x84d0a547},
        {"repair_pilot_info", [&] { Hover(InventoryCard(screen, 0)); }, 0xcc124f80},
        {"repair_vehicles_tab",
         [&]
         {
             // The 1998 bay's vehicles tab: the bought vehicle, in SPARE under the force.
             REQUIRE(!logistics->VehicleList->Vehicles.empty());
             ClickOn(ElementOf(screen, UnitCardId(tabs, logistics->VehicleList->Vehicles.back())));
         },
         0xee9befa7},
        {"repair_mechs_tab", [&] { ClickOn(ElementOf(screen, UnitCardId(tabs, boughtMech))); }, 0x742b22ca},
        {"repair_mech_row_info", [&] { Hover(ElementOf(screen, UnitCardId(tabs, boughtMech))); }, 0x742b22ca},
        {"repair_mech_into_force",
         [&] { DragTo(ElementOf(screen, UnitCardId(tabs, boughtMech)), ElementOf(screen, "force0")); }, 0x6530d170},
        {"repair_select_first", [&] { ClickOn(ElementOf(screen, "force0")); }, 0x6530d170},
        {"repair_mech_out",
         [&] { DragTo(ElementOf(screen, UnitCardId(tabs, boughtMech)), ElementOf(screen, "spare-head")); }, 0x610141a2},
        {"repair_weapon_list_scroll",
         [&]
         {
             Hover(ElementOf(screen, "fit0"));
             MCScreenInput::RealWheel(-1);
         },
         0x98cbd576},
        {"repair_first_mech_out", [&] { DragTo(ElementOf(screen, "force0"), ElementOf(screen, "spare-head")); },
         0x73e8a3bb},
        {"briefing_after_repair",
         [&] { ClickChoice(screen, "tabs", static_cast<int>(MCCampaignScreen::MCTab::Briefing)); }, 0xaf7f1bc1},
        {"deploy_after_repair", [&] { ClickChoice(screen, "tabs", static_cast<int>(MCCampaignScreen::MCTab::Deploy)); },
         0xbed572a3},
    }};

    for (size_t index = 0; index < steps.size(); index++)
    {
        const MCStep& step = steps[index];
        MCTest::Scope scope(step.Name);
        step.Action();
        settle();
        const std::string name = std::format("recording{:02}_{}", index, step.Name);
        CHECK_EQ(DrawnWindow(name),
                 std::format("{} {}x{}: {:#010x}", name, RecordedWindow.Width, RecordedWindow.Height, step.Expected));
    }

    CHECK_EQ(frames, 0x56a58c29u);
}

/// <summary>
/// A message said over the briefing as its mission starts (a match's host starting it while a guest's box is up; here
/// BEGIN MISSION pressed in its place) goes with the briefing, as the original's box went with logistics: it never shows
/// over the loading screen or the battlefield, nor comes back there, and the menus' layer takes no event in the battle.
/// </summary>
TEST_CASE_ISOLATED(
    "game: a message up over the briefing as its mission starts goes with it, never over the battlefield")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
    MCTestGame::RunFrame(FlowFrame);
    ElementOf(screen, "ready0").Focus();
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(screen.CanBegin());
    ShowMenuMessage(BriefingMessage);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(MCRmlDialogs::Shown());

    screen.PressBegin();
    constexpr int32_t MostFrames = 600;
    const auto playing = [] { return Scenario() != nullptr && Mission()->State == MCMissionState::Scenario; };
    bool overLoading = false;

    for (int32_t frame = 0; frame < MostFrames && !playing(); frame++)
    {
        MCTestGame::RunFrame(FlowFrame);
        overLoading = overLoading || (LoadingScreenUp && MCRmlDialogs::Shown());
    }

    REQUIRE(playing());
    constexpr int32_t BattleFrames = 20;

    for (int32_t frame = 0; frame < BattleFrames; frame++)
    {
        MCTestGame::RunFrame(FlowFrame);
    }

    const MCOptionalRef<MCRmlMenus> menus = MCRmlMenus::Running();
    REQUIRE(menus.has_value());
    CHECK(!overLoading);
    CHECK(!MCRmlDialogs::Shown());
    CHECK(!MCRmlDialogs::Returning());
    CHECK(!menus->get().Menus().Showing());
}

/// <summary>
/// DEPLOY's BEGIN MISSION clicked: the game's cursor is where the click was, though the remade screen took the mouse's
/// moves and the click (left at the screen's corner, the mission's camera scrolled toward it from its first seconds, off
/// the player's units into the dark round the map); the mission shows its world.
/// </summary>
TEST_CASE_ISOLATED("game: a mission begun from DEPLOY shows its world, the game's cursor where BEGIN MISSION was")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    StartFirstBriefing();
    MCCampaignScreen& screen = Shown();
    screen.ChooseTab(MCCampaignScreen::MCTab::Deploy);
    MCTestGame::RunFrame(FlowFrame);
    ElementOf(screen, "ready0").Focus();
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    MCTestGame::RunFrame(FlowFrame);
    REQUIRE(screen.CanBegin());

    // The 640x480 window's pixels are the game's screen's.
    const Rml::Element& begin = ElementOf(screen, "begin");
    const Rml::Vector2f at = Middle(begin);
    ClickOn(begin);
    const MCPoint cursor = MCInput::GetCursorPos();
    CHECK_EQ(cursor.x, static_cast<int32_t>(std::floor(at.x)));
    CHECK_EQ(cursor.y, static_cast<int32_t>(std::floor(at.y)));

    constexpr int32_t MostFrames = 600;
    const auto playing = [] { return Scenario() != nullptr && Mission()->State == MCMissionState::Scenario; };

    for (int32_t frame = 0; frame < MostFrames && !playing(); frame++)
    {
        MCTestGame::RunFrame(FlowFrame);
    }

    REQUIRE(playing());
    CheckWorldShown("deploy");
}

/// <summary>
/// The tests' way into a campaign's mission (MCTestGame::StartCampaignMission: the mouse over the remade briefing, its
/// BEGIN MISSION's request): the mission shows its world, as MCTestGame::StartMission's does.
/// </summary>
TEST_CASE_ISOLATED("game: a campaign's mission started as the tests start it shows its world")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(MCTestGame::StartCampaignMission());
    CheckWorldShown("started");
}

namespace
{
    /// <summary>The walk's match's port, its own so it runs beside the other tests (--jobs).</summary>
    constexpr std::string_view MenusWalkPort = "28949";

    /// <summary>The remade menus show a screen at <paramref name="where"/>.</summary>
    void CheckMenusShown(std::string_view where)
    {
        const MCTest::Scope scope{std::string(where)};
        const MCOptionalRef<MCRmlMenus> menus = MCRmlMenus::Running();
        REQUIRE(menus.has_value());
        CHECK(menus->get().Showing());
    }
}

/// <summary>
/// Port-only (SPEC-menus.md's stage 9): the menus' ways in and out, each a remade screen (no 1998 menu screen is left to
/// make, nor its art: logistics loads only the units' icon shapes, which the remade screens draw their outlines from),
/// from the game's start, through a campaign (its four tabs; the main menu between missions, LOAD GAME, SAVE GAME,
/// Preferences and RETURN TO GAME; a mission, its results and the way back), to a match (the LAN games, the room, its
/// briefing and DEPLOY).
/// </summary>
TEST_CASE_ISOLATED("game: the remade menus walked from the start, through a campaign, to a match")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::SaveGameCleanup cleanup;
    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    RunWhileChanging();
    CheckMenusShown("the main menu");
    REQUIRE(GlobalLog() != nullptr);
    CHECK(std::ranges::none_of(GlobalLog()->MechIconShapes, &MCRegisteredBlock::Empty));

    // The campaign's four tabs.
    NewCampaign();
    RunWhileChanging();

    for (const auto& [tab, name] : {std::pair{MCCampaignScreen::MCTab::Briefing, "BRIEFING"},
                                    std::pair{MCCampaignScreen::MCTab::Purchasing, "PURCHASING"},
                                    std::pair{MCCampaignScreen::MCTab::MechBay, "'MECH BAY"},
                                    std::pair{MCCampaignScreen::MCTab::Deploy, "DEPLOY"}})
    {
        Shown().ChooseTab(tab);
        MCTestGame::RunFrame(FlowFrame);
        CheckMenusShown(name);
    }

    // The main menu between missions: LOAD GAME, SAVE GAME and Preferences, each left by Escape; RETURN TO GAME.
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();
    CheckMenusShown("the main menu between missions");

    for (const auto& [screen, name] :
         {std::pair{MCMenuScreens::Load(), "LOAD GAME"}, std::pair{MCMenuScreens::Save(), "SAVE GAME"},
          std::pair{MCMenuScreens::Preferences(), "PREFERENCES"}})
    {
        REQUIRE(MCScreenFlow::Open(screen));
        RunWhileChanging();
        CheckMenusShown(name);
        MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
        RunWhileChanging();
        REQUIRE(MCMenuScreens::Top() == MCMenuScreens::Main());
    }

    MCScreenFlow::Via("return to game", ReturnToGame)();
    RunWhileChanging();
    REQUIRE(MCCampaignMenu::Shown());
    CheckMenusShown("RETURN TO GAME");

    // A mission, its results and the way back to the next briefing.
    BeginFromDeploy();
    EndAndCloseResults(Won);
    REQUIRE(GlobalLog() != nullptr);
    REQUIRE(MCCampaignMenu::Shown());
    CheckMenusShown("the next briefing");

    // A match: from the main menu, the LAN games, the room with ANN in it, its briefing and DEPLOY.
    MCMenuScreens::LeaveCampaign();
    RunWhileChanging();
    MCTestGuest::Guest ann("ANN", MCMatchRule::Supported);
    MCCampaignScreen& match = PlanMatch(ann, MenusWalkPort);
    CheckMenusShown("the match's briefing");
    match.ChooseTab(MCCampaignScreen::MCTab::Deploy);
    MCTestGame::RunFrame(FlowFrame);
    CheckMenusShown("the match's DEPLOY");
}

#include "stdafx.h"
#include "MCTest.h"
#include "MCFakeUpdaterBackend.h"
#include "MCMenusPictures.h"
#include "MCTestGuest.h"
#include "RemadeScreens.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fakes/MCServiceScript.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCCampaignMenu.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMainMenuScreen.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCPrefsScreen.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCSaves.h"
#include "logistics/MCSavesMenu.h"
#include "logistics/MCUpdateModel.h"
#include "logistics/MCUpdateScreen.h"
#include "main/MCGamePaths.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "main/MCUpdater.h"
#include "mission/MCMission.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "platform/MCRegistry.h"
#include "ui/MCRmlChecks.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlMenus.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>

// Port-only (SPEC-menus.md's stage 7): the main menu remade, its submenus CAMPAIGNS, SOLO MISSIONS and MULTIPLAYER in
// its panel, from the keys and the mouse, drawn the same on every machine and laid out at every players' window.

namespace
{
    using MCTestGuest::ClickOn;
    using MCTestGuest::OnMainMenu;
    using MCTestGuest::Panel;

    constexpr float FlowFrame = 1.0f / 60.0f;
    constexpr int32_t MostChangeFrames = 600;
    /// <summary>Frames the menus take to lay a new window out and draw it whole.</summary>
    constexpr int32_t SettleFrames = 4;
    /// <summary>The game's names for its own "before the mission" saves (Port Arthur's), as the game writes them.</summary>
    constexpr uint32_t PortArthurBefore = 0x37b;
    constexpr int SaveNameLength = 199;
    /// <summary>The game's string of its build's version (the registry's "Version", a dot after it).</summary>
    constexpr uint32_t BuildVersionString = 0x282;
    constexpr int BuildVersionLength = 99;
    /// <summary>Another build's version, as the registry holds it after an update.</summary>
    constexpr std::string_view OtherBuildVersion = "MCX v0.0 0000.";
    /// <summary>The games service the count is asked of, and how many it says are online.</summary>
    constexpr std::string_view Service = "wss://games.test";
    constexpr std::string_view ServiceVersion = "0.6.0 (812)";
    constexpr std::string_view OnlineAnswer = R"({"online":37})";
    constexpr std::string_view OnlineLine = "37 players online";
    /// <summary>The newer version the fake updater finds.</summary>
    constexpr std::string_view NewVersion = "0.2.2 (480)";

    /// <summary>Frames while a change of screen runs, and two after: the change's last fade drawn.</summary>
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

    void SettleWindow()
    {
        for (int32_t frame = 0; frame < SettleFrames; frame++)
        {
            MCTestGame::RunFrame(FlowFrame);
        }
    }

    /// <summary>
    /// The frames at the recorded windows (MCMenusPictures::CheckWindowFrames), the mouse in the window's corner over no
    /// choice (one under it is drawn in its hover's blue).
    /// </summary>
    void CheckFrames(MCDisplay& display, std::string_view name,
                     const std::array<uint32_t, MCMenusPictures::FrameWindows.size()>& hashes)
    {
        MCScreenInput::RealMove(0, 0);
        MCMenusPictures::CheckWindowFrames(display, name, hashes, SettleWindow);
    }

    /// <summary>A key pressed and let go as the player does it, and the change it starts run out.</summary>
    void Press(SDL_Scancode key)
    {
        MCScreenInput::RealKey(key);
        RunWhileChanging();
    }

    /// <summary>A click on <paramref name="id"/> of <paramref name="panel"/>, and the change it starts run out.</summary>
    void Click(MCMainPanel panel, const char* id)
    {
        ClickOn(panel, id);
        RunWhileChanging();
    }

    /// <summary>Whether the panel shows with nothing over it (on the menus' layer's top).</summary>
    bool OnTop(MCMainPanel panel)
    {
        const std::shared_ptr<MCMainMenuScreen> top = MCRemadeTest::OnTop<MCMainMenuScreen>();
        return top != nullptr && top.get() == &Panel(panel) && !MCScreenChange::Running();
    }

    /// <summary>The flow's stack now.</summary>
    std::vector<MCScreenFlow::MCScreenId> StackNow()
    {
        const std::span<const MCScreenFlow::MCScreenId> stack = MCScreenFlow::Stack();
        return {stack.begin(), stack.end()};
    }

    /// <summary>The element with the keys' focus in the menus' context; none without one.</summary>
    std::string FocusId()
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlMenus::Running();
        REQUIRE(menus.has_value());
        const Rml::Element* focus = menus->get().Menus().Context().GetFocusElement();
        return focus != nullptr ? focus->GetId() : std::string();
    }

    /// <summary>The words a panel's element shows.</summary>
    std::string Words(MCMainPanel panel, const char* id)
    {
        return MCRemadeTest::Words(Panel(panel), id);
    }

    bool Visible(MCMainPanel panel, const char* id)
    {
        return MCRemadeTest::Visible(Panel(panel), id);
    }

    /// <summary>Whether the remade briefing shows (stage 8's), whole, the change that showed it over.</summary>
    bool OnBriefing()
    {
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();
        return logistics != nullptr && MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing) &&
               MCCampaignMenu::Shown() && MCCampaignMenu::Ready() && !MCScreenChange::Running();
    }

    /// <summary>Waits for the old briefing; when it doesn't come, what shows instead.</summary>
    void RequireBriefing()
    {
        if (MCTestGuest::SettleUntil(OnBriefing))
        {
            return;
        }

        const std::shared_ptr<MCLogistics> logistics = GlobalLog();
        FAIL(std::format("no briefing: logistics state {}, the briefing current {} and showing {}, a remade screen "
                         "showing {}, a change running {}, the mission's state {}, the flow's stack {}",
                         logistics->LogisticsState, MCMenuScreens::CampaignOn(MCMenuScreens::MCCampaignPage::Briefing),
                         MCCampaignMenu::Shown(), MCRmlFlow::Showing(), MCScreenChange::Running(),
                         std::to_underlying(Mission()->State), MCScreenFlow::StackText()));
    }

    /// <summary>
    /// The briefing's way back to the main menu (1998's MAIN MENU there): between missions, the campaign behind it.
    /// </summary>
    void BackFromBriefing()
    {
        MCMenuScreens::LeaveCampaign();
        RunWhileChanging();
        REQUIRE(OnMainMenu());
    }

    /// <summary>
    /// The original campaign started and saved as its first mission begins, then the main menu as a start of the game
    /// finds it with that save: no campaign loaded (CONTINUE, not RETURN TO GAME).
    /// </summary>
    void SaveFirstMission()
    {
        NewCampaign();
        RunWhileChanging();
        const std::string before = MCFormatPrintf(LoadGameString(PortArthurBefore, SaveNameLength).c_str(), 1, 1);
        REQUIRE_EQ(MCLogistics::SaveCampaign(before), 0);
        BackFromBriefing();
        GlobalLog()->CurrentMission = -1;
        Panel(MCMainPanel::Main).Refresh();
        SettleWindow();
    }

    /// <summary>
    /// Down pressed until <paramref name="id"/> has the focus (the first press after a change or a click only shows the
    /// ring), at most a press a choice.
    /// </summary>
    void FocusOn(const char* id)
    {
        constexpr int32_t MostPresses = 12;

        for (int32_t press = 0; press < MostPresses && FocusId() != id; press++)
        {
            Press(SDL_SCANCODE_DOWN);
        }

        REQUIRE_EQ(FocusId(), id);
    }

    /// <summary>Whether the quit question shows over the main menu, its heading the question.</summary>
    std::shared_ptr<MCRmlScreen> QuitQuestion()
    {
        std::shared_ptr<MCRmlScreen> question = MCRemadeTest::OnTop<MCRmlScreen>();

        if (question == nullptr || MCRemadeTest::Words(*question, "heading") != MCMenuScreens::QuitQuestion)
        {
            return nullptr;
        }

        return question;
    }

    /// <summary>The panel's state at every players' window: the layout and legibility checks' findings, by window.</summary>
    void CheckLayout(std::string_view what)
    {
        const MCOptionalRef<MCRmlMenus> running = MCRmlMenus::Running();
        REQUIRE(running.has_value());
        MCRmlMenus& menus = running->get();
        int width = 0;
        int height = 0;
        REQUIRE(SDL_GetWindowSizeInPixels(&MCInput::Display()->Window(), &width, &height));

        for (const MCMenusPictures::MCWindowSize& window : MCMenusPictures::PlayersWindows)
        {
            menus.SetWindow(window.Width, window.Height);
            menus.Frame();
            const float unit = MCRmlMenus::MenusUnit(window.Width, window.Height);
            const std::string where = std::format("{} {}x{}: ", what, window.Width, window.Height);
            CHECK_EQ(where + MCRmlChecks::BoxProblems(menus.Menus().Context()), where);
            CHECK_EQ(where + MCRmlChecks::Illegible(menus.Menus().Context(), unit), where);

            // The panel's foot above the legal lines' bar, as the frames draw it.
            const MCTest::Scope scope{where};
            const std::shared_ptr<MCMainMenuScreen> shown = MCRemadeTest::OnTop<MCMainMenuScreen>();
            REQUIRE(shown != nullptr);
            Rml::Element* panel = MCTestGuest::ElementOf(*shown, "menu");
            Rml::Element* legal = MCTestGuest::ElementOf(*shown, "legal");
            REQUIRE(panel != nullptr);
            REQUIRE(legal != nullptr);
            const float panelFoot =
                panel->GetAbsoluteOffset(Rml::BoxArea::Border).y + panel->GetBox().GetSize(Rml::BoxArea::Border).y;
            const float legalTop = legal->GetAbsoluteOffset(Rml::BoxArea::Border).y;
            CHECK_EQ(where + (panelFoot > legalTop
                                  ? std::format("the panel's foot at {} under the legal lines' top at {}", panelFoot,
                                                legalTop)
                                  : std::string()),
                     where);
        }

        // The window's own size again, laid out, for the clicks after.
        menus.SetWindow(width, height);
        SettleWindow();
    }
}

/// <summary>
/// The main menu, its submenus and MULTIPLAYER's two states over the painting on the software renderer, the whole
/// window at 640x480, 1280x720 and 1280x1024, the hashes recorded (the Mac's and Windows' alike with the game's data):
/// the menu with no save, with one (CONTINUE named), between missions (RETURN TO GAME leading, SAVE GAME after LOAD
/// GAME), CAMPAIGNS on a started campaign and on one not started, SOLO MISSIONS, MULTIPLAYER (INTERNET with the count
/// online, LAN), MULTIPLAYER with no network, and QUIT's question. With <c>--shots</c> each is saved.
/// </summary>
TEST_CASE_ISOLATED("game: the main menu and its submenus draw the same on every machine")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs keptPrefs;
    MCTestUpdater::MCFakeUpdater updater;
    MCServiceScript::ListScript list;
    std::string asked;
    const MCServiceScript::SetupScope service(MCServiceScript::ListOpener(list),
                                              MCServiceScript::Answering(std::string(OnlineAnswer), asked), Service,
                                              ServiceVersion);
    GRenderer = static_cast<int>(MCRendererKind::Software);
    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    REQUIRE(display->RendererKind() == MCRendererKind::Software);
    RunWhileChanging();
    MCUpdater::Found(std::string(NewVersion), "Night missions.");
    SettleWindow();
    REQUIRE(OnMainMenu());

    // No save: no CONTINUE; the version line and UPDATE at the foot.
    CHECK(!Visible(MCMainPanel::Main, "continue"));
    CHECK(!Visible(MCMainPanel::Main, "return"));
    CHECK(!Visible(MCMainPanel::Main, "save"));
    CHECK(Visible(MCMainPanel::Main, "solo"));
    CHECK_EQ(Words(MCMainPanel::Main, "version"),
             std::format("Redux HD {}", ReleaseOf(MCTestUpdater::FakeRunningVersion)));
    CHECK_EQ(Words(MCMainPanel::Main, "update"), std::format("Update to {}", ReleaseOf(NewVersion)));
    CheckFrames(*display, "main_menu", {0x2f9f433c, 0xb060ad9f, 0x4b951c48});

    // A save: CONTINUE, naming where it opens.
    SaveFirstMission();
    CHECK_EQ(Panel(MCMainPanel::Main).ContinueLine(), "Original campaign · Operation 1, mission 1");
    CHECK(Visible(MCMainPanel::Main, "continue"));
    CheckFrames(*display, "main_menu_continue", {0xb1827f10, 0xdc85e89c, 0xfaa1f6fb});

    // Between missions (from the briefing): RETURN TO GAME leads, naming the mission; SAVE GAME after LOAD GAME.
    NewCampaign();
    RunWhileChanging();
    BackFromBriefing();
    CHECK_EQ(Words(MCMainPanel::Main, "return-line"), "Original campaign · Operation 1, mission 1");
    CHECK(Visible(MCMainPanel::Main, "return"));
    CHECK(Visible(MCMainPanel::Main, "save"));
    CHECK(!Visible(MCMainPanel::Main, "continue"));
    CheckFrames(*display, "main_menu_between", {0xba0012be, 0xce77fc27, 0xfec3d819});
    GlobalLog()->CurrentMission = -1;
    Panel(MCMainPanel::Main).Refresh();
    SettleWindow();

    // CAMPAIGNS: the original (its save) picked, CONTINUE, LOAD and NEW CAMPAIGN; the expansion, not started: NEW
    // CAMPAIGN alone.
    Click(MCMainPanel::Main, "campaigns");
    REQUIRE(OnTop(MCMainPanel::Campaigns));
    const std::vector<MCMainMenuScreen::MCRow>& campaigns = Panel(MCMainPanel::Campaigns).Rows();
    REQUIRE_EQ(campaigns.size(), size_t{2});
    CHECK_EQ(campaigns[0].Title, "Original campaign");
    CHECK_EQ(campaigns[0].Line, "MechCommander · Operation 1, mission 1");
    CHECK_EQ(campaigns[1].Title, "Expansion campaign");
    CHECK_EQ(campaigns[1].Line, "Desperate Measures · Not started");
    CHECK_EQ(Panel(MCMainPanel::Campaigns).Picked(), 0);
    CHECK_EQ(Words(MCMainPanel::Campaigns, "main"), "Continue");
    CHECK(Visible(MCMainPanel::Campaigns, "load"));
    CHECK(Visible(MCMainPanel::Campaigns, "new"));
    CheckFrames(*display, "main_menu_campaigns", {0x0e5357f7, 0xf6d26c47, 0xb8d3338c});
    Panel(MCMainPanel::Campaigns).Pick(1);
    SettleWindow();
    CHECK_EQ(Words(MCMainPanel::Campaigns, "main"), "New campaign");
    CHECK(!Visible(MCMainPanel::Campaigns, "load"));
    CHECK(!Visible(MCMainPanel::Campaigns, "new"));
    CheckFrames(*display, "main_menu_campaigns_new", {0xef2b95a5, 0x900a9cd6, 0xe4b8b1d7});
    // BACK swaps the main menu in at once: no palette waits for the next frame's fade (which would darken the remade
    // menu to black for a second).
    REQUIRE(MCRemadeTest::Press(Panel(MCMainPanel::Campaigns), "back"));
    CHECK(GuiSystem().PendingPalette.empty());
    RunWhileChanging();
    REQUIRE(OnMainMenu());

    // SOLO MISSIONS: the single missions by their briefings' names, what each starts with under it.
    Click(MCMainPanel::Main, "solo");
    REQUIRE(OnTop(MCMainPanel::Solo));
    const std::vector<MCMainMenuScreen::MCRow>& solos = Panel(MCMainPanel::Solo).Rows();
    REQUIRE_EQ(solos.size(), size_t{3});
    CHECK_EQ(solos[0].Title, "Destroy All Aces");
    CHECK_EQ(solos[0].Line, "Buy 245 tons with 325,000 RP");
    CHECK_EQ(solos[1].Title, "Port Arthur Redux");
    CHECK_EQ(solos[1].Line, "Buy 990 tons with 425,000 RP");
    CHECK_EQ(solos[2].Title, "Destroy Garrison");
    CHECK_EQ(solos[2].Line, "Buy 200 tons with 185,000 RP");
    CHECK_EQ(Panel(MCMainPanel::Solo).Picked(), 0);
    CheckFrames(*display, "main_menu_solo", {0xb775c398, 0x3d58aab0, 0x5ac1a9d6});
    Press(SDL_SCANCODE_ESCAPE);
    REQUIRE(OnMainMenu());

    // MULTIPLAYER: INTERNET, the count online under it once the service answers, and LAN.
    Click(MCMainPanel::Main, "multiplayer");
    REQUIRE(MCTestGuest::SettleUntil(
        [] { return MCTestGuest::OnAccess() && Panel(MCMainPanel::Multiplayer).OnlineLine() == OnlineLine; }));
    CHECK(Visible(MCMainPanel::Multiplayer, "internet"));
    CHECK(Visible(MCMainPanel::Multiplayer, "lan"));
    CHECK(!Visible(MCMainPanel::Multiplayer, "retry"));
    CheckFrames(*display, "main_menu_multiplayer", {0x284ee93e, 0x42f947ab, 0x0eaa7a4e});

    // No network: it says so where INTERNET and LAN would be, RETRY under it.
    MCMainMenuModel::SetNetworkForTests(false);
    Click(MCMainPanel::Multiplayer, "back");
    Click(MCMainPanel::Main, "multiplayer");
    REQUIRE(OnTop(MCMainPanel::Multiplayer));
    CHECK(Panel(MCMainPanel::Multiplayer).Offline());
    CHECK(!Visible(MCMainPanel::Multiplayer, "internet"));
    CHECK(!Visible(MCMainPanel::Multiplayer, "lan"));
    CHECK(Visible(MCMainPanel::Multiplayer, "retry"));
    CheckFrames(*display, "main_menu_offline", {0x1866f4e3, 0x4bc747b2, 0x6cc46ca7});
    MCMainMenuModel::SetNetworkForTests(std::nullopt);
    Click(MCMainPanel::Multiplayer, "back");
    REQUIRE(OnMainMenu());

    // QUIT's question over the main menu.
    Click(MCMainPanel::Main, "quit");
    REQUIRE(QuitQuestion() != nullptr);
    CheckFrames(*display, "main_menu_quit", {0xaec673a3, 0x9cc00205, 0x857d2a2d});
    Press(SDL_SCANCODE_ESCAPE);
    CHECK(QuitQuestion() == nullptr);
    CHECK(OnMainMenu());
}

/// <summary>
/// The main menu and its submenus at every window the players' reports name, with the longest lines they show (a
/// campaign's save, between missions with RETURN TO GAME and SAVE GAME, an update waiting): every element inside the
/// window, no two siblings overlapping, no text spilling or cut (MCRmlChecks::BoxProblems); every text at least 8
/// units high, at its contrast (MCRmlChecks::Illegible).
/// </summary>
TEST_CASE_ISOLATED("game: the main menu and its submenus lay out and read at every window the players' reports name")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestUpdater::MCFakeUpdater updater;
    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    RunWhileChanging();
    MCUpdater::Found(std::string(NewVersion), "Night missions.");
    SaveFirstMission();
    REQUIRE(OnTop(MCMainPanel::Main));
    CheckLayout("main menu");

    NewCampaign();
    RunWhileChanging();
    BackFromBriefing();
    CheckLayout("between missions");
    GlobalLog()->CurrentMission = -1;
    Panel(MCMainPanel::Main).Refresh();
    SettleWindow();

    Click(MCMainPanel::Main, "campaigns");
    REQUIRE(OnTop(MCMainPanel::Campaigns));
    CheckLayout("campaigns");
    Panel(MCMainPanel::Campaigns).Pick(1);
    CheckLayout("campaigns, not started");
    Press(SDL_SCANCODE_ESCAPE);
    REQUIRE(OnTop(MCMainPanel::Main));
    Click(MCMainPanel::Main, "solo");
    REQUIRE(OnTop(MCMainPanel::Solo));
    CheckLayout("solo missions");
    Press(SDL_SCANCODE_ESCAPE);
    REQUIRE(OnTop(MCMainPanel::Main));
    Click(MCMainPanel::Main, "multiplayer");
    REQUIRE(OnTop(MCMainPanel::Multiplayer));
    CheckLayout("multiplayer");
    MCMainMenuModel::SetNetworkForTests(false);
    Click(MCMainPanel::Multiplayer, "retry");
    REQUIRE(OnTop(MCMainPanel::Multiplayer));
    CheckLayout("multiplayer, no network");
    MCMainMenuModel::SetNetworkForTests(std::nullopt);
}

/// <summary>
/// The main menu from the keys alone: the focus on its first choice as it shows, Up and Down along the choices, Enter
/// pressing the one with the focus; CAMPAIGNS on its picked campaign, Escape back; SOLO MISSIONS' Enter playing the
/// mission with the focus (its briefing), Escape there back to the main menu; MULTIPLAYER's LAN and Escape back to it,
/// Escape again to the main menu; Escape on the main menu asking QUIT's question, Escape there keeping the game.
/// </summary>
TEST_CASE_ISOLATED("game: the main menu and its submenus work from the keys")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    RunWhileChanging();
    REQUIRE(OnMainMenu());
    CHECK_EQ(FocusId(), "campaigns");

    // CAMPAIGNS: the focus on the picked campaign; Escape back.
    Press(SDL_SCANCODE_RETURN);
    REQUIRE(OnTop(MCMainPanel::Campaigns));
    CHECK_EQ(Panel(MCMainPanel::Campaigns).Picked(), 0);
    Press(SDL_SCANCODE_DOWN);
    CHECK_EQ(Panel(MCMainPanel::Campaigns).Picked(), 1);
    CHECK_EQ(Words(MCMainPanel::Campaigns, "main"), "New campaign");
    Press(SDL_SCANCODE_ESCAPE);
    REQUIRE(OnMainMenu());

    // Down to SOLO MISSIONS (past LOAD GAME), and Up and Down again: Enter opens it, Enter again plays the first
    // mission.
    CHECK_EQ(FocusId(), "campaigns");
    Press(SDL_SCANCODE_DOWN);
    CHECK_EQ(FocusId(), "load");
    Press(SDL_SCANCODE_DOWN);
    CHECK_EQ(FocusId(), "solo");
    Press(SDL_SCANCODE_UP);
    CHECK_EQ(FocusId(), "load");
    Press(SDL_SCANCODE_DOWN);
    CHECK_EQ(FocusId(), "solo");
    Press(SDL_SCANCODE_RETURN);
    REQUIRE(OnTop(MCMainPanel::Solo));
    Press(SDL_SCANCODE_RETURN);
    RequireBriefing();
    CHECK(Solo);
    const std::vector<MCScreenFlow::MCScreenId> briefing = {MCMenuScreens::Main(), MCMenuScreens::Campaign()};
    CHECK(StackNow() == briefing);
    // The briefing's way back: the main menu between missions, RETURN TO GAME (the single mission's) with the focus.
    BackFromBriefing();
    CHECK_EQ(FocusId(), "return");
    CHECK_EQ(Words(MCMainPanel::Main, "return-line"), "Solo mission");
    CHECK(!Visible(MCMainPanel::Main, "save"));

    // MULTIPLAYER: LAN (the focus's after INTERNET, or first without it), Escape back to MULTIPLAYER, Escape again.
    FocusOn("multiplayer");
    Press(SDL_SCANCODE_RETURN);
    REQUIRE(MCTestGuest::SettleUntil(MCTestGuest::OnAccess));

    if (FocusId() == "internet")
    {
        Press(SDL_SCANCODE_DOWN);
    }

    CHECK_EQ(FocusId(), "lan");
    Press(SDL_SCANCODE_RETURN);
    REQUIRE(MCTestGuest::SettleUntil(MCTestGuest::OnLan));
    Press(SDL_SCANCODE_ESCAPE);
    REQUIRE(MCTestGuest::SettleUntil(MCTestGuest::OnAccess));
    Press(SDL_SCANCODE_ESCAPE);
    REQUIRE(OnMainMenu());

    // Escape: QUIT's question, the focus on CANCEL; Escape keeps the game.
    Press(SDL_SCANCODE_ESCAPE);
    REQUIRE(QuitQuestion() != nullptr);
    CHECK_EQ(FocusId(), "no");
    Press(SDL_SCANCODE_ESCAPE);
    CHECK(QuitQuestion() == nullptr);
    CHECK(OnMainMenu());
    CHECK(!Mission()->GameOver);
}

/// <summary>
/// NEW CAMPAIGN's press stores the build's version (registry.cfg's, as each menu action does) without waiting for the
/// file: the press done and its change begun while the save is held, the version read back at once, the file written
/// after. Reading the file back as the press wrote it froze the press 1.2-1.3 s on the A10 (Defender scanning it).
/// </summary>
TEST_CASE_ISOLATED("game: NEW CAMPAIGN's press waits for no file, the build's version saved behind it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    RunWhileChanging();
    REQUIRE(OnMainMenu());

    // Another build's version stored, as after an update.
    MCRegistry::Write(GameRegistryKey, "Version", OtherBuildVersion);
    MCRegistry::WaitForSaves();
    REQUIRE(!CheckRegistryVersionNumber());
    const std::filesystem::path file = MCFileSystem::ResolveWrite("registry.cfg");
    // Read as text, as the store writes it (Windows' line ends read back as \n).
    const auto text = [&file]
    {
        std::ifstream in(file);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };

    const std::string before = text();
    REQUIRE(before.contains(std::format("|version={}\n", OtherBuildVersion)));

    {
        const MCRegistry::MCSavesHeld held;
        NewCampaign();
        CHECK(MCScreenChange::Running());
        CHECK(CheckRegistryVersionNumber());
        CHECK_EQ(text(), before);
    }

    MCRegistry::WaitForSaves();
    CHECK(text().contains(std::format("|version={}.\n", LoadGameString(BuildVersionString, BuildVersionLength))));
    RunWhileChanging();
    RequireBriefing();
}

/// <summary>
/// The main menu from the mouse: CONTINUE plays the latest save (its briefing); between missions RETURN TO GAME goes
/// back to it and SAVE GAME opens SAVE GAME; CAMPAIGNS' LOAD opens LOAD GAME on that campaign, whose Escape goes back to
/// CAMPAIGNS, its CONTINUE the campaign's latest save; LOAD GAME opens on every save; SOLO MISSIONS' PLAY the picked
/// mission; MULTIPLAYER's RETRY looks for the network again, INTERNET and LAN their games, BACK the main menu;
/// PREFERENCES; ORIGINAL CINEMA the intro movie (none in the tests' install: straight back); UPDATE, waiting, its screen;
/// QUIT its question, CANCEL keeping the game.
/// </summary>
TEST_CASE_ISOLATED("game: the main menu and its submenus work from the mouse")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs keptPrefs;
    MCTestUpdater::MCFakeUpdater updater;
    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    RunWhileChanging();
    SaveFirstMission();

    // CONTINUE: the save's briefing.
    Click(MCMainPanel::Main, "continue");
    RequireBriefing();
    CHECK(GlobalLog()->CurrentMission != -1);

    // Between missions: RETURN TO GAME back to the briefing; SAVE GAME, Escape back.
    BackFromBriefing();
    Click(MCMainPanel::Main, "return");
    RequireBriefing();
    BackFromBriefing();
    Click(MCMainPanel::Main, "save");
    REQUIRE(MCSavesMenu::Screen().has_value());
    CHECK(MCSavesMenu::Screen()->get().Shown());
    CHECK(MCSavesMenu::Screen()->get().Mode() == MCSavesScreen::MCMode::Save);
    Press(SDL_SCANCODE_ESCAPE);
    REQUIRE(OnMainMenu());

    // CAMPAIGNS: LOAD on the original's saves, Escape back to CAMPAIGNS; its CONTINUE the campaign's save.
    Click(MCMainPanel::Main, "campaigns");
    REQUIRE(OnTop(MCMainPanel::Campaigns));
    Click(MCMainPanel::Campaigns, "load");
    REQUIRE(MCSavesMenu::Screen().has_value());
    CHECK(MCSavesMenu::Screen()->get().Shown());
    CHECK(MCSavesMenu::Screen()->get().Campaign() == MCSaves::MCCampaign::Original);
    Press(SDL_SCANCODE_ESCAPE);
    REQUIRE(OnTop(MCMainPanel::Campaigns));
    Click(MCMainPanel::Campaigns, "main");
    RequireBriefing();
    BackFromBriefing();

    // LOAD GAME: every save; Escape back.
    Click(MCMainPanel::Main, "load");
    REQUIRE(MCSavesMenu::Screen().has_value());
    CHECK(MCSavesMenu::Screen()->get().Shown());
    CHECK(!MCSavesMenu::Screen()->get().Campaign().has_value());
    Press(SDL_SCANCODE_ESCAPE);
    REQUIRE(OnMainMenu());

    // SOLO MISSIONS: opened by a click, Escape back to the main menu; again, a click picks the second, PLAY plays it.
    Click(MCMainPanel::Main, "solo");
    REQUIRE(OnTop(MCMainPanel::Solo));
    Press(SDL_SCANCODE_ESCAPE);
    REQUIRE(OnMainMenu());
    Click(MCMainPanel::Main, "solo");
    REQUIRE(OnTop(MCMainPanel::Solo));
    Rml::ElementList rows;
    MCTestGuest::ElementOf(Panel(MCMainPanel::Solo), "rows")->GetElementsByClassName(rows, "list-row");
    std::erase_if(rows, [](Rml::Element* row) { return !row->IsVisible(); });
    REQUIRE(rows.size() >= 2);
    MCTestGuest::RealClickOn(*rows[1]);
    CHECK_EQ(Panel(MCMainPanel::Solo).Picked(), 1);
    Click(MCMainPanel::Solo, "main");
    RequireBriefing();
    CHECK(Solo);
    BackFromBriefing();

    // MULTIPLAYER with no network: RETRY finds it again; LAN; BACK.
    MCMainMenuModel::SetNetworkForTests(false);
    Click(MCMainPanel::Main, "multiplayer");
    REQUIRE(OnTop(MCMainPanel::Multiplayer));
    CHECK(Visible(MCMainPanel::Multiplayer, "retry"));
    MCMainMenuModel::SetNetworkForTests(true);
    Click(MCMainPanel::Multiplayer, "retry");
    CHECK(!Panel(MCMainPanel::Multiplayer).Offline());
    CHECK(Visible(MCMainPanel::Multiplayer, "lan"));
    MCMainMenuModel::SetNetworkForTests(std::nullopt);
    MCTestGuest::PressLan();
    REQUIRE(MCTestGuest::SettleUntil(MCTestGuest::OnLan));
    MCTestGuest::RealClickOn(*MCTestGuest::ElementOf(*MCTestGuest::Games(MCGamesWhere::Lan), "back"));
    REQUIRE(MCTestGuest::SettleUntil(MCTestGuest::OnAccess));
    Click(MCMainPanel::Multiplayer, "back");
    REQUIRE(OnMainMenu());
    CHECK(StackNow() == std::vector{MCMenuScreens::Main()});

    // PREFERENCES, and back.
    Click(MCMainPanel::Main, "preferences");
    CHECK(MCRemadeTest::OnTop<MCPrefsScreen>() != nullptr);
    Press(SDL_SCANCODE_ESCAPE);
    REQUIRE(OnMainMenu());

    // ORIGINAL CINEMA: the intro movie, none here: the main menu again.
    Click(MCMainPanel::Main, "cinema");
    REQUIRE(MCTestGuest::SettleUntil(OnMainMenu));
    CHECK(Mission()->State == MCMissionState::Logistics);

    // UPDATE, once an update waits: its screen, and back.
    CHECK(!Visible(MCMainPanel::Main, "update"));
    MCUpdater::Found(std::string(NewVersion), "Night missions.");
    SettleWindow();
    REQUIRE(Visible(MCMainPanel::Main, "update"));
    Click(MCMainPanel::Main, "update");
    CHECK(MCUpdateScreen::Shows());
    Press(SDL_SCANCODE_ESCAPE);
    REQUIRE(OnMainMenu());

    // QUIT: its question; CANCEL keeps the game.
    Click(MCMainPanel::Main, "quit");
    const std::shared_ptr<MCRmlScreen> question = QuitQuestion();
    REQUIRE(question != nullptr);
    CHECK_EQ(MCRemadeTest::Words(*question, "yes"), "Quit");
    MCTestGuest::RealClickOn(*MCTestGuest::ElementOf(*question, "no"));
    RunWhileChanging();
    CHECK(QuitQuestion() == nullptr);
    CHECK(OnMainMenu());
    CHECK(!Mission()->GameOver);
}

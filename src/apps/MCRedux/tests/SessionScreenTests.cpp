#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiSystem.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGameList.h"
#include "logistics/MCLogComboBox.h"
#include "logistics/MCLogSlider.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCLogChatWindow.h"
#include "logistics/MCSessionScreen.h"
#include "main/MCLogistics.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCDirectPlay.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCWeather.h"

using namespace MCScreenInput;

namespace
{
    /// <summary>The loopback address the game hosts on (not the default port, so a running game is left alone).</summary>
    constexpr std::string_view HostAddress = "127.0.0.1:28877";

    /// <summary>Selects the TCP/IP connection to <paramref name="address"/> ("host:port").</summary>
    uint32_t Connect(MCDirectPlay& directPlay, std::string_view address)
    {
        const auto provider = std::bit_cast<std::array<uint8_t, sizeof(_GUID)>>(MCDirectPlayGuids::DPSPGUID_TCPIP);
        std::vector<uint8_t> host(address.begin(), address.end());
        host.push_back(0);
        const std::array<DPCOMPOUNDADDRESSELEMENT, 2> elements{
            DPCOMPOUNDADDRESSELEMENT{MCDirectPlayGuids::DPAID_ServiceProvider,
                                     std::vector<uint8_t>(provider.begin(), provider.end())},
            DPCOMPOUNDADDRESSELEMENT{MCDirectPlayGuids::DPAID_INet, host}};
        return directPlay.InitializeConnection(MCDirectPlay::CreateCompoundAddress(elements), 0);
    }

    /// <summary>Runs <paramref name="call"/> while another thread pumps <paramref name="other"/> (the game is not running).</summary>
    template <class Call> auto WhilePumping(MCDirectPlay& other, Call&& call)
    {
        std::atomic<bool> stop = false;
        std::thread pumper(
            [&]
            {
                while (!stop)
                {
                    other.Pump();
                    SDL_Delay(1);
                }
            });
        auto result = call();
        stop = true;
        pumper.join();
        return result;
    }

    /// <summary>
    /// A second player in the game's session, on the test's side: it joins over the loopback and then only takes in
    /// (and drops) what it is sent, on its own thread, so the host keeps it.
    /// </summary>
    class Guest
    {
    public:
        bool Join(MCDirectPlay& host)
        {
            if (Connect(_DirectPlay, HostAddress) != DP_OK)
            {
                return false;
            }

            DPSESSIONDESC2 found{};
            bool foundOne = false;

            for (int attempt = 0; attempt < 40 && !foundOne; attempt++)
            {
                DPSESSIONDESC2 desc{};
                desc.dwSize = sizeof(desc);
                desc.guidApplication = MultiPlayerAppGuid;
                _DirectPlay.EnumSessions(
                    desc, 0,
                    [&](const DPSESSIONDESC2& session, uint32_t flags)
                    {
                        if ((flags & DPESC_TIMEDOUT) == 0)
                        {
                            found = session;
                            foundOne = true;
                        }

                        return false;
                    },
                    DPENUMSESSIONS_AVAILABLE);
                host.Pump();
                SDL_Delay(5);
            }

            if (!foundOne)
            {
                return false;
            }

            if (WhilePumping(host, [&] { return _DirectPlay.Open(found, DPOPEN_JOIN | DPOPEN_RETURNSTATUS); }) != DP_OK)
            {
                return false;
            }

            const DPNAME name{std::string("Guest"), std::nullopt};
            uint32_t id = 0;

            if (WhilePumping(host, [&] { return _DirectPlay.CreatePlayer(id, name, {}, 0); }) != DP_OK)
            {
                return false;
            }

            _Listener = std::thread(
                [this]
                {
                    while (!_Stop)
                    {
                        std::array<uint8_t, 4096> buffer{};
                        uint32_t size = 0;
                        uint32_t from = 0;
                        uint32_t to = 0;

                        while (_DirectPlay.Receive(from, to, DPRECEIVE_ALL, buffer, size) == DP_OK)
                        {
                            Keep(std::span<const uint8_t>(buffer).first(std::min<size_t>(size, buffer.size())));
                        }

                        _DirectPlay.Pump();
                        SDL_Delay(1);
                    }
                });
            return true;
        }

        /// <summary>The last match options message the host sent, if any.</summary>
        std::optional<MCMPMatchOptionsMessage> MatchOptions()
        {
            const std::lock_guard lock(_Mutex);
            return _MatchOptions;
        }

        ~Guest()
        {
            _Stop = true;

            if (_Listener.joinable())
            {
                _Listener.join();
            }
        }

    private:
        /// <summary>Keeps <paramref name="message"/> if it is the host's match options.</summary>
        void Keep(std::span<const uint8_t> message)
        {
            if (message.size() < sizeof(MCMPMatchOptionsMessage))
            {
                return;
            }

            const auto options = ReadLinkupMessage<MCMPMatchOptionsMessage>(message);

            if (options.Type() == static_cast<uint16_t>(MCMPMessageType::MatchOptions))
            {
                const std::lock_guard lock(_Mutex);
                _MatchOptions = options;
            }
        }

        MCDirectPlay _DirectPlay;
        std::thread _Listener;
        std::atomic<bool> _Stop = false;
        std::mutex _Mutex;
        std::optional<MCMPMatchOptionsMessage> _MatchOptions;
    };

    /// <summary>Runs frames for two seconds of game time, giving the network a moment each; the last frame's hash.</summary>
    uint32_t Settle()
    {
        uint32_t hash = 0;

        for (int32_t frame = 0; frame < 30; frame++)
        {
            SDL_Delay(3);
            MCTestGame::RunFrame(1.0f / 15.0f);
            hash = ScreenHash();
        }

        return hash;
    }

    /// <summary>Types <paramref name="text"/> into the text object that has the focus.</summary>
    void Type(std::string_view text)
    {
        for (const char c : text)
        {
            SendKey(static_cast<uint8_t>(c));
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }
}

/// <summary>
/// Renderer phase 3 step 4g: the multiplayer session screen, its player names, ready lights, chat window and chat
/// input draw from their state each frame. The game hosts a LAN session on the loopback, a second player joins from
/// the test, and the host moves the players onto teams, changes the points and the tech base and chats. Each step's
/// screen was recorded on the code before step 4 (when these drew into pictures at the time of each event).
/// </summary>
TEST_CASE_ISOLATED("game: the multiplayer session screen matches the pre-renderer frames")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());

    // The LAN screen as the TCP/IP button leaves it, hosting on the loopback.
    ConnectScreen();
    Settle();
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(HostAddress);
    MCSplashScreen& lanScreen = *GlobalLog()->LanScreen;
    GlobalLog()->MultiplayerScreen->ShowGuiWindow(0);
    lanScreen.ShowGuiWindow(1);
    lanScreen.ShowBlock(0);
    GlobalLog()->SetCurrentScreen(lanScreen);
    GlobalLog()->LogisticsState = 0xb;
    lanScreen.Element<MCLogTextObject>(4)->SetStringBuffer("Commander");
    lanScreen.Element<MCLogTextObject>(10)->SetStringBuffer("Test game");
    lanScreen.Element<MCLogTextObject>(11)->SetStringBuffer("6");
    CreateSession();
    Settle();
    REQUIRE(manager.CurrentSession() != nullptr);
    REQUIRE(manager.DirectPlay != nullptr);

    Guest guest;
    REQUIRE(guest.Join(*manager.DirectPlay));
    Settle();

    // Every frame presented from here on is folded into one more hash.
    uint32_t presents = 0x811c9dc5;
    MCTestGame::OnPresent = [&] { presents = (presents ^ ScreenHash()) * 0x01000193; };

    struct Step
    {
        std::string_view Name;
        std::function<void()> Action;
        uint32_t Expected;
    };

    // The cursor hasn't moved yet in the first step: at (0, 0) the original's over test (OB-129, fixed) took it as over
    // every tool button, so its frame was 0x3d56c0c0 before the fix. Port-only: every frame has the MATCH OPTIONS tab
    // behind MULTIPLAYER CHAT (3 Nov 2026; only its column differs from the frames before: 0xa6b1053f, 0x7548a837,
    // 0xf143dbb4, 0x1a793ae4, 0xa6d18671, 0x4adcfb49, 0x74b8c0d0, 0x2d2edef8, 0x983fc7f9).
    const Step steps[] = {
        {"session", [] { Go(); }, 0x0cc42d9au},
        {"first name to team 1", [] { Drag(0x15, 0x16a, 0x100, 0x13a); }, 0x034cda12u},
        {"next name to team 2", [] { Drag(0x15, 0x16a, 0x1c4, 0x13a); }, 0xb3ef257du},
        {"team 1 points up", [] { Click(0x106, 0x186); }, 0x3b17d20du},
        {"team 2 Inner Sphere", [] { Click(0x225, 0x112); }, 0xbcc94190u},
        {"hover a light", [] { SendMouse(7, 0xdc, 5, false); }, 0xec4846b8u},
        {"chat typed",
         []
         {
             Click(0x40, 0x130);
             Type("hello there");
         },
         0x19fd5db9u},
        {"chat sent", [] { SendKey(0xd); }, 0x7342d1b5u},
        {"name back off the team", [] { Drag(0x105, 0x13a, 0x40, 0x1c0); }, 0xdfb618bcu},
    };

    int32_t index = 0;

    for (const Step& step : steps)
    {
        MCTest::Scope scope(step.Name);
        step.Action();
        const uint32_t hash = Settle();
        SaveShot(std::format("session{:02} {}", index, step.Name), hash);
        CHECK_EQ(hash, step.Expected);
        index++;
    }

    MCTestGame::OnPresent = nullptr;
    // 0xa176a052 before the OB-129 fix (the first step's frames), 0x87982e74 before the control audit (2026-10-04: the
    // steps are unchanged; between them presses end at the release, and the ready lights' backing and the clock show
    // at once; 0x8a6bba0e before the MATCH OPTIONS tab).
    CHECK_EQ(presents, 0xc78ee09fu);
}

namespace
{
    /// <summary>The MATCH OPTIONS list's rows (logistics/MCMatchOptionsTab.cpp): its headings and its options.</summary>
    constexpr int32_t WeatherRow = 1;
    constexpr int32_t TimeRow = 2;
    constexpr int32_t BrakingRow = 4;
    /// <summary>A point on each tab (MCMatchOptionsTab.cpp's ChatTabArea and OptionsTabArea).</summary>
    constexpr int32_t TabX = 0xca;
    constexpr int32_t ChatTabY = 0x80;
    constexpr int32_t OptionsTabY = 0xf0;

    /// <summary>Clicks row <paramref name="row"/>'s setting box of <paramref name="list"/> <paramref name="times"/> times.</summary>
    void ClickSetting(MCLogOptionList& list, int32_t row, int32_t times)
    {
        const std::optional<std::pair<int32_t, int32_t>> centre = list.SettingCentre(row);
        REQUIRE(centre.has_value());

        for (int32_t click = 0; click < times; click++)
        {
            Click(centre->first, centre->second);
            Settle();
        }
    }
}

/// <summary>
/// Port: the session screen's MATCH OPTIONS tab, under MULTIPLAYER CHAT, brings the match's options in front in the
/// chat's place (its pictures lettered from the art's tabs); the host's changes are the match's and go to the guest at
/// once, as do the options when the screen opens; a guest's list is locked; MULTIPLAYER CHAT brings the chat back.
/// </summary>
TEST_CASE_ISOLATED("game: the session screen's MATCH OPTIONS tab sets the match's options and sends them")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    ConnectScreen();
    Settle();
    REQUIRE(MultiPlayer() != nullptr);
    MCSessionManager& manager = *MultiPlayer()->SessionManager;
    manager.ConnectTcp(HostAddress);
    MCSplashScreen& lanScreen = *GlobalLog()->LanScreen;
    GlobalLog()->MultiplayerScreen->ShowGuiWindow(0);
    lanScreen.ShowGuiWindow(1);
    lanScreen.ShowBlock(0);
    GlobalLog()->SetCurrentScreen(lanScreen);
    GlobalLog()->LogisticsState = 0xb;
    lanScreen.Element<MCLogTextObject>(4)->SetStringBuffer("Commander");
    lanScreen.Element<MCLogTextObject>(10)->SetStringBuffer("Options game");
    lanScreen.Element<MCLogTextObject>(11)->SetStringBuffer("6");
    CreateSession();
    Settle();
    REQUIRE(manager.DirectPlay != nullptr);

    Guest guest;
    REQUIRE(guest.Join(*manager.DirectPlay));
    Settle();
    Go();
    Settle();

    MCSessionScreen& screen = *GlobalLog()->SessionScreen;
    MCMultiPlayer& game = *MultiPlayer();
    REQUIRE(screen.MatchTabs[0] != nullptr);
    REQUIRE(screen.MatchTabs[1] != nullptr);
    CHECK(screen.FrontTab == MCSessionTab::Chat);
    SaveShot("match options 0 chat in front", ScreenHash());

    // Opened: the host's options went to the guest (none chosen: the mission's own, the host's PREFS).
    std::optional<MCMPMatchOptionsMessage> sent = guest.MatchOptions();
    REQUIRE(sent.has_value());
    CHECK_EQ(sent->Weather, MCMPMatchOptionsMessage::MissionOwn);
    CHECK_EQ(sent->TimeOfDay, MCMPMatchOptionsMessage::MissionOwn);
    CHECK_EQ(sent->Rules, game.WantedRules());

    Click(TabX, OptionsTabY);
    Settle();
    CHECK(screen.FrontTab == MCSessionTab::Options);
    MCLogOptionList& list = *screen.MatchOptionsList;
    CHECK(list.ShowWindow);
    CHECK(!GlobalLog()->ChatWindow->ShowWindow);
    CHECK(list.ChoiceName(WeatherRow) == "MISSION'S");
    CHECK(list.ChoiceName(TimeRow) == "MISSION'S");

    // MISSION'S, CLEAR, RAIN; MISSION'S, DAY, DUSK, NIGHT; braking the other way.
    ClickSetting(list, WeatherRow, 2);
    ClickSetting(list, TimeRow, 3);
    const bool brakingWas = (game.WantedRules() & MCMatchRule::MechBraking) != 0;
    ClickSetting(list, BrakingRow, 1);
    CHECK(list.ChoiceName(WeatherRow) == "RAIN");
    CHECK(list.ChoiceName(TimeRow) == "NIGHT");
    CHECK(game.MatchOptions.Weather == MCWeatherKind::Rain);
    CHECK(game.MatchOptions.TimeOfDay == MCTimeOfDay::Night);
    CHECK_EQ((game.WantedRules() & MCMatchRule::MechBraking) != 0, !brakingWas);
    sent = guest.MatchOptions();
    REQUIRE(sent.has_value());
    CHECK_EQ(sent->Weather, static_cast<int8_t>(MCWeatherKind::Rain));
    CHECK_EQ(sent->TimeOfDay, static_cast<int8_t>(MCTimeOfDay::Night));
    CHECK_EQ(sent->Rules, game.WantedRules());
    SaveShot("match options 1 options in front", ScreenHash());

    // New chat while the options are in front: MULTIPLAYER CHAT blinks until it is in front again.
    const size_t linesSeen = screen.ChatLinesSeen;
    GlobalLog()->ChatWindow->AddLine("%fc4Guest: %fc6ready");
    Settle();
    CHECK(GlobalLog()->ChatWindow->LinesAdded > linesSeen);

    // A guest's list: locked, saying so.
    game.IsHost = false;
    Settle();
    ClickSetting(list, WeatherRow, 1);
    CHECK(list.ChoiceName(WeatherRow) == "RAIN");
    CHECK(list.Status() == "THE HOST SETS THESE.");
    game.IsHost = true;
    Settle();

    Click(TabX, ChatTabY);
    Settle();
    CHECK(screen.FrontTab == MCSessionTab::Chat);
    CHECK(!list.ShowWindow);
    CHECK(GlobalLog()->ChatWindow->ShowWindow);
}

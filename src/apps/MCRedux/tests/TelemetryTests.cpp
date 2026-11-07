#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "MCTestGuest.h"
#include "TestGame.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSystem.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCReportsNotice.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCHdSettings.h"
#include "main/MCLogistics.h"
#include "main/MCTelemetry.h"
#include "platform/MCFileSystem.h"

// The reports (main/MCTelemetry.h), with a stand-in for the app's backend: nothing sent before the player has seen
// what the reports hold, the question where the law wants consent first (the system's own region, or the server's),
// the notice elsewhere; a match's report at its end, the missions' at the first and then a week apart; the last run's
// crash with its black box; and the main menu's notice and question (logistics/MCReportsNotice.h).

namespace
{
    using MCTelemetry::MCPrompt;

    /// <summary>What the stand-in backend was asked, and what it answers.</summary>
    struct MCBackendLog
    {
        /// <summary>Each request's path and body, and how to answer it.</summary>
        std::vector<std::pair<std::string, std::string>> Requests;
        std::vector<std::function<void(int32_t, std::string)>> Replies;
        /// <summary>Now (seconds since 1970): 15 January 2027.</summary>
        int64_t Now = 1'800'000'000;
        std::vector<std::string> Countries = {"US"};
        std::optional<MCTelemetry::MCCrash> Crash;
        /// <summary>The black box's file: it outlives a backend, as a file does a run.</summary>
        std::shared_ptr<std::vector<std::byte>> BlackBox = std::make_shared<std::vector<std::byte>>();
        int32_t PowerReads = 0;
    };

    class MCFakeBackend final : public MCTelemetry::MCBackend
    {
    public:
        explicit MCFakeBackend(std::shared_ptr<MCBackendLog> log) : _Log(std::move(log)) {}

        MCTelemetry::MCMachine Machine() override
        {
            return {.Platform = "mac",
                    .AppVersion = "0.2.1 (468)",
                    .Model = "MacBookAir10,1",
                    .Cpu = "Apple M1",
                    .PerformanceCores = 4,
                    .EfficiencyCores = 4,
                    .GpuCores = 7,
                    .MemoryGb = 8,
                    .System = "26.5"};
        }

        MCTelemetry::MCPower Power() override
        {
            _Log->PowerReads++;
            return {.Thermal = 2, .Battery = true, .LowPower = false};
        }

        std::vector<std::string> LocalCountries() override { return _Log->Countries; }

        int64_t Now() override { return _Log->Now; }

        void Request(std::string path, std::string body, std::function<void(int32_t, std::string)> done) override
        {
            _Log->Requests.emplace_back(std::move(path), std::move(body));
            _Log->Replies.push_back(std::move(done));
        }

        std::span<std::byte> MapBlackBox(size_t size) override
        {
            _Log->BlackBox->resize(size);
            return *_Log->BlackBox;
        }

        std::optional<MCTelemetry::MCCrash> Crash(int64_t) override { return _Log->Crash; }

    private:
        std::shared_ptr<MCBackendLog> _Log;
    };

    constexpr std::string_view StateFile = "reports.cfg";
    constexpr int32_t HttpOk = 200;
    constexpr int32_t ServerError = 500;
    constexpr double FrameMilliseconds = 16.0;
    constexpr int64_t SecondsPerDay = 86400;

    /// <summary>
    /// A stand-in backend for the test's length, REPORTS off and no reports.cfg at its start and end (the user folder
    /// is the tests'); <see cref="Restart"/> is the game started again, with the same black box and reports.cfg.
    /// </summary>
    class MCFakeReports
    {
    public:
        MCFakeReports()
        {
            RemoveState();
            GReports = 0;
            MCTelemetry::SetBackend(std::make_unique<MCFakeBackend>(Log));
        }

        ~MCFakeReports()
        {
            MCTelemetry::SetBackend(nullptr);
            GReports = 0;
            RemoveState();
        }

        MCFakeReports(const MCFakeReports&) = delete;
        MCFakeReports& operator=(const MCFakeReports&) = delete;

        /// <summary>The game started again: a new backend over the same black box, REPORTS as PREFS kept it.</summary>
        void Restart()
        {
            auto log = std::make_shared<MCBackendLog>(*Log);
            log->Requests.clear();
            log->Replies.clear();
            Log = log;
            MCTelemetry::SetBackend(std::make_unique<MCFakeBackend>(Log));
        }

        /// <summary>Answers request <paramref name="index"/>, and lets a frame take the reply in.</summary>
        void Reply(size_t index, int32_t status, std::string body = {})
        {
            REQUIRE(index < Log->Replies.size());
            Log->Replies[index](status, std::move(body));
            MCTelemetry::Frame(0.0);
        }

        std::shared_ptr<MCBackendLog> Log = std::make_shared<MCBackendLog>();

    private:
        static void RemoveState()
        {
            std::error_code error;
            std::filesystem::remove(MCFileSystem::UserRoot() / StateFile, error);
        }
    };

    /// <summary>Starts the reports, and has the player see the notice: REPORTS on.</summary>
    void StartReporting(MCFakeReports& reports)
    {
        MCTelemetry::Start();
        REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
        reports.Reply(0, HttpOk, R"({"country":"US","ask":false})");
        REQUIRE(MCTelemetry::Prompt() == MCPrompt::Notice);
        MCTelemetry::Answered(true);
        REQUIRE(MCTelemetry::Reporting());
        reports.Log->Requests.clear();
        reports.Log->Replies.clear();
    }

    /// <summary>Plays <paramref name="seconds"/> of 16 ms frames, every tenth one 40 ms (a hitch).</summary>
    void Play(double seconds)
    {
        constexpr int32_t hitchEvery = 10;
        constexpr double hitchMilliseconds = 40.0;
        double played = 0.0;

        for (int32_t frame = 0; played < seconds; frame++)
        {
            const double milliseconds = frame % hitchEvery == hitchEvery - 1 ? hitchMilliseconds : FrameMilliseconds;
            MCTelemetry::Frame(milliseconds);
            played += milliseconds / 1000.0;
        }
    }

    /// <summary>Plays <paramref name="seconds"/> of 16 ms frames, each throwing <paramref name="share"/> of the HD weather's pieces.</summary>
    void PlayWeather(double seconds, float share)
    {
        for (double played = 0.0; played < seconds; played += FrameMilliseconds / 1000.0)
        {
            MCTelemetry::WeatherDrawn(share);
            MCTelemetry::Frame(FrameMilliseconds);
        }
    }

    /// <summary>Keeps the tests' prefs.cfg as it was: the main menu's answer writes PREFS.</summary>
    class MCKeepPrefs
    {
    public:
        MCKeepPrefs()
        {
            std::ifstream file(_Path, std::ios::binary);
            _Existed = file.is_open();
            _Contents.assign(std::istreambuf_iterator<char>(file), {});
        }

        ~MCKeepPrefs()
        {
            if (_Existed)
            {
                std::ofstream(_Path, std::ios::binary | std::ios::trunc) << _Contents;
            }
            else
            {
                std::error_code error;
                std::filesystem::remove(_Path, error);
            }
        }

        MCKeepPrefs(const MCKeepPrefs&) = delete;
        MCKeepPrefs& operator=(const MCKeepPrefs&) = delete;

    private:
        std::filesystem::path _Path = MCFileSystem::UserRoot() / "prefs.cfg";
        bool _Existed = false;
        std::string _Contents;
    };

    /// <summary>Whether <paramref name="json"/> holds <paramref name="part"/>.</summary>
    bool Holds(std::string_view json, std::string_view part)
    {
        return json.find(part) != std::string_view::npos;
    }

    /// <summary>The copy's ID <paramref name="report"/> holds, or nothing.</summary>
    std::string IdOf(std::string_view report)
    {
        constexpr std::string_view key = R"("id":")";
        const size_t at = report.find(key);

        if (at == std::string_view::npos)
        {
            return {};
        }

        const size_t start = at + key.size();
        return std::string(report.substr(start, report.find('"', start) - start));
    }

    /// <summary>A dialog's inner panel: its edges' columns, in from the dialog's sides, and the blank inside each.</summary>
    constexpr int32_t PanelLeftEdge = 9;
    constexpr int32_t PanelRightEdge = 10;
    constexpr int32_t PanelBlank = 3;

    /// <summary>
    /// Whether <paramref name="dialog"/>'s text keeps as far from its panel's right edge as from its left: on every
    /// row of the text, the blank columns inside both edges are alike, on the screen as shown.
    /// </summary>
    bool TextInsidePanel(MCReusableDialog& dialog)
    {
        const std::vector<uint8_t> shown = MCScreenInput::ShownScreen();
        const auto width = static_cast<size_t>(ScreenPort()->Bitmap().XMax + 1);
        const auto pixel = [&](int32_t x, int32_t y)
        { return shown[static_cast<size_t>(y) * width + static_cast<size_t>(x)]; };
        const int32_t left = dialog.GlobalX() + PanelLeftEdge + 1;
        const int32_t right = dialog.GlobalX() + dialog.Width() - PanelRightEdge - PanelBlank;
        const int32_t bottom = dialog.GlobalY() + dialog.Height() - dialog.BottomPiece->Height();

        for (int32_t y = dialog.GlobalY() + dialog.TopPiece->Height(); y < bottom; y++)
        {
            for (int32_t column = 0; column < PanelBlank; column++)
            {
                if (pixel(left + column, y) != pixel(right + column, y))
                {
                    SDL_Log("The dialog's text reaches its panel's edge at (%d, %d)", right + column, y);
                    return false;
                }
            }
        }

        return true;
    }
}

TEST_CASE("reports: without a backend nothing is measured, asked or sent")
{
    MCTelemetry::SetBackend(nullptr);
    MCTelemetry::Start();
    MCTelemetry::Frame(FrameMilliseconds);
    CHECK(!MCTelemetry::Available());
    CHECK(!MCTelemetry::Reporting());
    CHECK(MCTelemetry::Prompt() == MCPrompt::None);
}

TEST_CASE("reports: the law where the player is decides the question or the notice, and nothing is sent first")
{
    // The system's own region in the EEA: the question, with nothing sent, not even the region's question.
    {
        MCFakeReports reports;
        reports.Log->Countries = {"US", "DE"};
        MCTelemetry::Start();
        CHECK(MCTelemetry::Prompt() == MCPrompt::Ask);
        CHECK(reports.Log->Requests.empty());
        CHECK(!MCTelemetry::Reporting());
    }

    // Elsewhere the server says where the request came from: the notice, or the question.
    for (const auto& [reply, prompt] : std::to_array<std::pair<std::string_view, MCPrompt>>({
             {R"({"country":"US","ask":false})", MCPrompt::Notice},
             {R"({ "country" : "BR", "ask" : false })", MCPrompt::Notice},
             {R"({"country":"GB","ask":false})", MCPrompt::NoticeWithId},
             {R"({"country":"CH","ask":false})", MCPrompt::NoticeWithId},
             {R"({"country":"KR","ask":false})", MCPrompt::Ask},
             {R"({"country":"CA","ask":true})", MCPrompt::Ask},
             {R"({"country":"XX","ask":false})", MCPrompt::Ask},
             {R"({"ask":false})", MCPrompt::Ask},
             {"not json", MCPrompt::Ask},
         }))
    {
        MCFakeReports reports;
        MCTelemetry::Start();
        REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
        CHECK_EQ(reports.Log->Requests[0].first, std::string("region"));
        CHECK(reports.Log->Requests[0].second.empty());
        CHECK(MCTelemetry::Prompt() == MCPrompt::None);
        reports.Reply(0, HttpOk, std::string(reply));
        CHECK(MCTelemetry::Prompt() == prompt);
    }

    // Only the plain notice calls the reports anonymous; the UK's and the question name their ID.
    constexpr std::string_view anonymous = "anonymous";
    CHECK(Holds(ReportsText(MCPrompt::Notice), anonymous));
    CHECK(!Holds(ReportsText(MCPrompt::NoticeWithId), anonymous));
    CHECK(!Holds(ReportsText(MCPrompt::Ask), anonymous));
    CHECK(Holds(ReportsText(MCPrompt::NoticeWithId), "random ID"));

    // No answer: nothing shows, and the next start asks again.
    MCFakeReports reports;
    MCTelemetry::Start();
    reports.Reply(0, 0);
    CHECK(MCTelemetry::Prompt() == MCPrompt::None);
    reports.Restart();
    MCTelemetry::Start();
    CHECK_EQ(reports.Log->Requests.size(), size_t{1});
}

TEST_CASE("reports: the player's answer is kept; a no is never asked again, and REPORTS chosen in HD OPTIONS is final")
{
    MCFakeReports reports;
    reports.Log->Countries = {"FR"};
    MCTelemetry::Start();
    REQUIRE(MCTelemetry::Prompt() == MCPrompt::Ask);
    MCTelemetry::Answered(false);
    CHECK(MCTelemetry::Prompt() == MCPrompt::None);
    CHECK_EQ(GReports, 0);
    CHECK(!MCTelemetry::Reporting());

    reports.Restart();
    MCTelemetry::Start();
    CHECK(MCTelemetry::Prompt() == MCPrompt::None);
    CHECK(reports.Log->Requests.empty());

    // Turned on in HD OPTIONS: reports go, with no question.
    GReports = 1;
    MCTelemetry::Chosen();
    CHECK(MCTelemetry::Reporting());
    reports.Restart();
    MCTelemetry::Start();
    CHECK(MCTelemetry::Prompt() == MCPrompt::None);
    CHECK(MCTelemetry::Reporting());

    // DEFAULTS and ORIGINAL leave the player's answer.
    const std::vector<int32_t> settings = MCHdSettings::Now();
    MCHdSettings::UseOriginal();
    CHECK_EQ(GReports, 1);
    MCHdSettings::UseDefaults();
    CHECK_EQ(GReports, 1);
    MCHdSettings::Restore(settings);
}

TEST_CASE("reports: a region reply after REPORTS was chosen in HD OPTIONS shows nothing, and changes nothing")
{
    MCFakeReports reports;
    MCTelemetry::Start();
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    MCTelemetry::Chosen();
    reports.Reply(0, HttpOk, R"({"country":"US","ask":false})");
    CHECK(MCTelemetry::Prompt() == MCPrompt::None);
    MCTelemetry::Answered(true);
    CHECK_EQ(GReports, 0);
    CHECK(!MCTelemetry::Reporting());
}

TEST_CASE("reports: a match of two minutes or more is reported at its end, with its frame times and players")
{
    MCFakeReports reports;
    StartReporting(reports);

    // A match left in its first minute counts for nothing.
    MCTelemetry::PlayLoading("mc2mp_01", true);
    MCTelemetry::PlayStarted(4, true);
    Play(60.0);
    MCTelemetry::PlayEnded();
    MCTelemetry::Frame(0.0);
    CHECK(reports.Log->Requests.empty());

    // The load's frame isn't play (a load takes a frame of its own), and a mission's path is cut to its name.
    MCTelemetry::Frame();
    MCTelemetry::PlayLoading("missions\\mc2mp_01", true);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    MCTelemetry::PlayStarted(4, true);
    MCTelemetry::Frame();
    Play(130.0);
    MCTelemetry::PlayerLost();
    MCTelemetry::PlayEnded();
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    CHECK_EQ(reports.Log->Requests[0].first, std::string("report"));
    const std::string& report = reports.Log->Requests[0].second;
    CHECK(report.starts_with(R"j({"v":1,"kind":"match","first":true,"platform":"mac","app":"0.2.1 (468)")j"));
    CHECK(Holds(report, R"("model":"MacBookAir10,1","cpu":"Apple M1","performanceCores":4,"efficiencyCores":4)"));
    CHECK(Holds(report, R"("gpuCores":7,"memoryGb":8,"system":"26.5")"));
    CHECK(Holds(report, R"("p50":16.50,"p95":40.50,"p99":40.50,"worst":40.00)"));
    CHECK(Holds(report, R"("moviesBehind":0,"weatherThinnedMinutes":0.00,"weatherLeast":1.00)"));
    CHECK(Holds(report, R"("thermal":2,"battery":true,"lowPower":false)"));
    CHECK(Holds(report, R"("mission":"mc2mp_01","players":4,"server":true,"playersLost":1)"));
    CHECK(Holds(report, R"("settings":{"HDArt":)"));
    CHECK(!Holds(report, "Reports"));
    CHECK(reports.Log->PowerReads > 0);

    // Once one is taken, the next isn't a new player's first.
    reports.Reply(0, HttpOk);
    MCTelemetry::PlayLoading("mc2mp_02", true);
    MCTelemetry::PlayStarted(2, false);
    Play(130.0);
    MCTelemetry::PlayEnded();
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{2});
    CHECK(Holds(reports.Log->Requests[1].second, R"("first":false)"));
}

TEST_CASE("reports: missions are reported at the first, then a week or more after, each report holding all since")
{
    MCFakeReports reports;
    StartReporting(reports);
    // A mission of 130 s, the HD weather thrown at <weather> in its first 30.
    const auto mission = [](float weather = 1.0f)
    {
        constexpr double weatherSeconds = 30.0;
        MCTelemetry::PlayLoading("mis0101", false);
        MCTelemetry::PlayStarted(1, false);
        PlayWeather(weatherSeconds, weather);
        Play(100.0);
        MCTelemetry::PlayEnded();
    };

    mission();
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    CHECK(Holds(reports.Log->Requests[0].second, R"("kind":"missions")"));
    CHECK(Holds(reports.Log->Requests[0].second, R"("play":{"count":1,)"));
    CHECK(!Holds(reports.Log->Requests[0].second, "mis0101"));
    CHECK(Holds(reports.Log->Requests[0].second, R"("weatherThinnedMinutes":0.00,"weatherLeast":1.00)"));
    reports.Reply(0, HttpOk);

    // Six days on: kept for the next report, through a restart, the weather thinned to keep up with it.
    constexpr float thinned = 0.6f;
    reports.Log->Now += SecondsPerDay * 6;
    mission(thinned);
    CHECK_EQ(reports.Log->Requests.size(), size_t{1});
    reports.Restart();
    MCTelemetry::Start();
    reports.Log->Now += SecondsPerDay;
    mission();
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    CHECK(Holds(reports.Log->Requests[0].second, R"("play":{"count":2,)"));
    CHECK(Holds(reports.Log->Requests[0].second, R"("weatherThinnedMinutes":0.50,"weatherLeast":0.60)"));

    // Not taken: kept for the next.
    reports.Reply(0, ServerError);
    reports.Log->Now += SecondsPerDay * 7;
    mission();
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{2});
    CHECK(Holds(reports.Log->Requests[1].second, R"("play":{"count":3,)"));
}

TEST_CASE("reports: each holds the copy's random ID, the same after a restart; REPORTS off drops it, on makes another")
{
    constexpr size_t idDigits = 32;
    MCFakeReports reports;
    StartReporting(reports);
    const auto matchId = [&reports]
    {
        MCTelemetry::PlayLoading("mc2mp_01", true);
        MCTelemetry::PlayStarted(2, true);
        Play(130.0);
        MCTelemetry::PlayEnded();
        REQUIRE(!reports.Log->Requests.empty());
        return IdOf(reports.Log->Requests.back().second);
    };

    const std::string first = matchId();
    CHECK_EQ(first.size(), idDigits);
    CHECK(std::ranges::all_of(first, [](char digit) { return std::isxdigit(digit) && !std::isupper(digit); }));

    reports.Restart();
    MCTelemetry::Start();
    CHECK_EQ(matchId(), first);

    GReports = 0;
    MCTelemetry::Chosen();
    GReports = 1;
    MCTelemetry::Chosen();
    const std::string next = matchId();
    CHECK_EQ(next.size(), idDigits);
    CHECK(next != first);
}

TEST_CASE("reports: the last run's crash is sent at the next start with its black box; a clean quit leaves none")
{
    MCFakeReports reports;
    StartReporting(reports);
    reports.Log->Crash = MCTelemetry::MCCrash{.What = "EXC_BAD_ACCESS SIGSEGV",
                                              .Frames = {"MechCommander Redux +0x1a2b MCMover::Update + 52"}};
    MCTelemetry::PlayLoading("mc2mp_03", true);
    MCTelemetry::PlayStarted(3, false);
    MCTelemetry::MessageIn(22, 64);
    constexpr int32_t updates = 3;

    for (int32_t i = 0; i < updates; i++)
    {
        MCTelemetry::MessageOut(25, 100);
        MCTelemetry::MessageIn(31, 20);
    }

    MCTelemetry::PlayerLost();
    Play(30.0);

    // The game crashed: the next start sends it.
    reports.Restart();
    MCTelemetry::Start();
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    const std::string& report = reports.Log->Requests[0].second;
    CHECK(Holds(report, R"("kind":"crash")"));
    CHECK(Holds(report, R"j("app":"0.2.1 (468)")j"));
    CHECK(Holds(report,
                R"("what":"EXC_BAD_ACCESS SIGSEGV","frames":["MechCommander Redux +0x1a2b MCMover::Update + 52"])"));
    CHECK(Holds(report, R"("phase":"match","mission":"mc2mp_03","players":3,"server":false,"playersLost":1)"));
    CHECK(Holds(report, " in 22 x1 64B\""));
    CHECK(Holds(report, " out 25 x3 300B\""));
    CHECK(Holds(report, " in 31 x3 60B\""));
    CHECK(Holds(report, " player lost\""));

    // A clean quit: no crash at the next start, whatever the system's reports hold.
    MCTelemetry::Quit();
    reports.Restart();
    MCTelemetry::Start();
    CHECK(reports.Log->Requests.empty());

    // Reports off: a crash isn't sent.
    GReports = 0;
    reports.Restart();
    MCTelemetry::Start();
    reports.Restart();
    MCTelemetry::Start();
    CHECK(reports.Log->Requests.empty());
}

TEST_CASE("reports: frame-time percentiles, and who's asked first")
{
    std::vector<uint32_t> histogram(501);
    histogram[32] = 90;
    histogram[80] = 9;
    histogram[500] = 1;
    CHECK_EQ(MCTelemetry::Percentile(histogram, 0.5), 16.5);
    CHECK_EQ(MCTelemetry::Percentile(histogram, 0.95), 40.5);
    CHECK_EQ(MCTelemetry::Percentile(histogram, 1.0), 250.5);
    CHECK_EQ(MCTelemetry::Percentile(std::vector<uint32_t>(501), 0.5), 0.0);

    const int64_t day = std::chrono::sys_days{std::chrono::year{2027} / 1 / 15}.time_since_epoch().count();
    const int64_t indiaDay = std::chrono::sys_days{std::chrono::year{2027} / 5 / 13}.time_since_epoch().count();
    CHECK(MCTelemetry::AskFirst("DE", day));
    CHECK(MCTelemetry::AskFirst("NO", day));
    CHECK(MCTelemetry::AskFirst("KR", day));
    CHECK(MCTelemetry::AskFirst("XX", day));
    CHECK(MCTelemetry::AskFirst("", day));
    CHECK(!MCTelemetry::AskFirst("US", day));
    CHECK(!MCTelemetry::AskFirst("GB", day));
    CHECK(!MCTelemetry::AskFirst("CH", day));
    CHECK(!MCTelemetry::AskFirst("IN", indiaDay - 1));
    CHECK(MCTelemetry::AskFirst("IN", indiaDay));
}

TEST_CASE_ISOLATED("game: the main menu shows the reports' notice once, and its OK turns REPORTS on")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCKeepPrefs prefs;
    MCFakeReports reports;
    REQUIRE(MCTestGame::StartLogistics());
    MCTestGame::RunFrame(1.0f / 15.0f);
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    reports.Reply(0, HttpOk, R"({"country":"US","ask":false})");
    MCTestGame::RunFrame(1.0f / 15.0f);
    MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
    REQUIRE(dialog.IsShowing());
    CHECK_EQ(dialog.Text, std::string(ReportsText(MCPrompt::Notice)));
    MCScreenInput::SaveShot("reports notice", MCScreenInput::ScreenHash());
    CHECK(TextInsidePanel(dialog));
    MCScreenInput::RealClick(dialog.OkButton->GlobalX() + dialog.OkButton->Width() / 2,
                             dialog.OkButton->GlobalY() + dialog.OkButton->Height() / 2);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(!dialog.IsShowing());
    CHECK_EQ(GReports, 1);
    CHECK(MCTelemetry::Reporting());
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(!dialog.IsShowing());
}

/// <summary>
/// A game a lobby starts never shows the main menu: its room (the session screen) shows the reports' notice instead,
/// once: not again as the room is shown again for a player who joins.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's session screen shows the reports' notice once")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCKeepPrefs prefs;
    MCFakeReports reports;
    MCTestGuest::Guest guest("ANN");
    REQUIRE(MCTestGame::StartLobby("-host -port 28899 -player BOB"));
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    reports.Reply(0, HttpOk, R"({"country":"US","ask":false})");
    MCTestGuest::Settle();
    MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->SessionScreen);
    REQUIRE(dialog.IsShowing());
    CHECK_EQ(dialog.Text, std::string(ReportsText(MCPrompt::Notice)));
    MCScreenInput::SaveShot("reports notice session screen", MCScreenInput::ScreenHash());
    MCScreenInput::RealClick(dialog.OkButton->GlobalX() + dialog.OkButton->Width() / 2,
                             dialog.OkButton->GlobalY() + dialog.OkButton->Height() / 2);
    MCTestGuest::Settle();
    CHECK(!dialog.IsShowing());
    CHECK_EQ(GReports, 1);

    REQUIRE(guest.Join(*MultiPlayer()->SessionManager->DirectPlay, "127.0.0.1:28899"));
    REQUIRE(MCTestGuest::SettleUntil([&] { return MCTestGuest::RoomShows(guest.Id()); }));
    MCTestGuest::Settle();
    CHECK(!dialog.IsShowing());
    CHECK_EQ(GReports, 1);
}

TEST_CASE_ISOLATED("game: where the law wants consent the main menu asks, and CANCEL leaves REPORTS off")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCKeepPrefs prefs;
    MCFakeReports reports;
    reports.Log->Countries = {"DE"};
    REQUIRE(MCTestGame::StartLogistics());
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(reports.Log->Requests.empty());
    MCReusableDialog& dialog = *GlobalLog()->QuestionDialog;
    REQUIRE(dialog.IsShowing());
    CHECK_EQ(dialog.Text, std::string(ReportsText(MCPrompt::Ask)));
    MCScreenInput::SaveShot("reports question", MCScreenInput::ScreenHash());
    CHECK(TextInsidePanel(dialog));
    MCScreenInput::RealClick(dialog.CancelButton->GlobalX() + dialog.CancelButton->Width() / 2,
                             dialog.CancelButton->GlobalY() + dialog.CancelButton->Height() / 2);
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(!dialog.IsShowing());
    CHECK_EQ(GReports, 0);
    CHECK(!MCTelemetry::Reporting());
    CHECK(MCTelemetry::Prompt() == MCPrompt::None);
}

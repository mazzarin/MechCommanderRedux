#include "stdafx.h"
#include "../MCMemorySystem.h"
#include "../MCTelemetryBackend.h"
#include "MCMenusPictures.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "RemadeScreens.h"
#include "ScreenInput.h"
#include "MCTestGuest.h"
#include "TestGame.h"
#include "fakes/MCFakeMemorySystem.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCPacketFile.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCMainMenuModel.h"
#include "logistics/MCMainMenuScreen.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCReportsNotice.h"
#include "logistics/MCMenuDialog.h"
#include "logistics/MCRoomScreen.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCGamePaths.h"
#include "main/MCHdSettings.h"
#include "main/MCLogistics.h"
#include "main/MCTelemetry.h"
#include "mission/MCMission.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "platform/MCMemoryRoom.h"
#include "platform/MCSdlHandle.h"
#include "platform/MCVulkanShaders.h"
#include "ui/MCRmlDialog.h"
#include "update/MCInstalled.h"
#include "update/MCUpdateDisk.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

// The reports (main/MCTelemetry.h), with a stand-in for the app's backend: nothing sent before the player has seen
// what the reports hold, the question where the law wants consent first (the system's own region, or the server's),
// the notice elsewhere; a match's report at its end, the missions' at the first and then a week apart; the last run's
// crash with its black box; a file the campaign's load refused, at once, saying why; the card the game draws on, as
// the system says it (a stand-in's, and this platform's own); the copy's content release, and the release due while it
// plays the art it has; and the main menu's notice and question (logistics/MCReportsNotice.h).

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
        /// <summary>The game's version: a later one after a restart is the game updated.</summary>
        std::string AppVersion = "0.2.1 (468)";
        std::optional<MCTelemetry::MCCrash> Crash;
        /// <summary>The computer as this platform's backend reads it (none: the stand-in MacBook Air below).</summary>
        std::optional<MCTelemetry::MCMachine> Machine;
        /// <summary>The black box's file: it outlives a backend, as a file does a run.</summary>
        std::shared_ptr<std::vector<std::byte>> BlackBox = std::make_shared<std::vector<std::byte>>();
        int32_t PowerReads = 0;
        /// <summary>The time the computer has been awake: it moves only as a test moves it.</summary>
        std::chrono::nanoseconds Awake{};
    };

    class MCFakeBackend final : public MCTelemetry::MCBackend
    {
    public:
        explicit MCFakeBackend(std::shared_ptr<MCBackendLog> log) : _Log(std::move(log)) {}

        MCTelemetry::MCMachine Machine() override
        {
            if (_Log->Machine.has_value())
            {
                return *_Log->Machine;
            }

            return {.Platform = "mac",
                    .AppVersion = _Log->AppVersion,
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

        std::chrono::nanoseconds Awake() override { return _Log->Awake; }

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

TEST_CASE("reports: play is timed by the time awake, and the time the player sees no frame isn't counted")
{
    MCFakeReports reports;
    StartReporting(reports);
    MCTelemetry::PlayLoading("mc2mp_01", true);
    MCTelemetry::PlayStarted(2, true);
    MCTelemetry::Frame();

    // 130 s of 16 ms frames by the time awake.
    constexpr std::chrono::milliseconds frame(16);
    constexpr int32_t frames = 130'000 / 16;
    constexpr int32_t asleep = 100;
    constexpr int32_t unseen = 200;

    for (int32_t i = 0; i < frames; i++)
    {
        if (i == asleep)
        {
            // The machine asleep through a frame: the wall clock runs on, the time awake doesn't.
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }

        reports.Log->Awake += frame;
        MCTelemetry::Frame();

        if (i == unseen)
        {
            // Half an hour of frames the player didn't see (the window minimized): the first seen after measures
            // nothing.
            for (int32_t hidden = 0; hidden < 100; hidden++)
            {
                reports.Log->Awake += std::chrono::seconds(18);
                MCTelemetry::Unseen();
            }

            MCTelemetry::Frame();
        }
    }

    MCTelemetry::PlayEnded();
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    const std::string& report = reports.Log->Requests[0].second;
    CHECK(Holds(report, R"("minutes":2.17,)"));
    CHECK(Holds(report, R"("p50":16.50,"p95":16.50,"p99":16.50,"worst":16.00)"));
}

TEST_CASE("reports: an internet game's link is reported as it ends, with no address, and at most 32 a run")
{
    MCFakeReports reports;
    using MCTelemetry::MCLinkEnd;
    using std::chrono::milliseconds;

    // Nothing before the player has seen what the reports hold.
    MCTelemetry::LinkEnded({});
    CHECK(reports.Log->Requests.empty());

    StartReporting(reports);
    MCTelemetry::LinkEnded({.Guest = true,
                            .Opened = true,
                            .Path = MCIceLink::Path::Relay,
                            .Setup = milliseconds(840),
                            .Open = milliseconds(90'000),
                            .Carried = {.BytesSent = 123'456, .BytesReceived = 7'890, .RoundTrip = milliseconds(64)},
                            .Here = {.Host = true, .Reflexive = true},
                            .There = {.Host = true, .Reflexive = true, .Relay = true},
                            .End = MCLinkEnd::Closed});
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    CHECK_EQ(reports.Log->Requests[0].first, std::string("report"));
    const std::string& report = reports.Log->Requests[0].second;
    CHECK(report.starts_with(R"j({"v":1,"kind":"link","first":true,"platform":"mac","app":"0.2.1 (468)")j"));
    CHECK(Holds(report, R"("model":"MacBookAir10,1")"));
    CHECK(Holds(report, R"("link":{"role":"guest","opened":true,"path":"relay","setupMs":840,"openMinutes":1.50,)"
                        R"("bytesSent":123456,"bytesReceived":7890,"rttMs":64,"here":"host,srflx",)"
                        R"("there":"host,srflx,relay","end":"closed"}})"));
    CHECK(!Holds(report, R"("play")"));

    // A link that never opened has no path and no time open; no round trip measured, none said.
    MCTelemetry::LinkEnded({.Setup = milliseconds(15'000), .End = MCLinkEnd::NoPath});
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{2});
    CHECK(Holds(reports.Log->Requests[1].second,
                R"("link":{"role":"host","opened":false,"setupMs":15000,"openMinutes":0.00,"bytesSent":0,)"
                R"("bytesReceived":0,"here":"","there":"","end":"noPath"}})"));

    // A stranger with the game's code can open link after link: the reports stop at 32 a run.
    constexpr int32_t manyLinks = 40;
    constexpr size_t mostLinks = 32;

    for (int32_t i = 0; i < manyLinks; i++)
    {
        MCTelemetry::LinkEnded({.End = MCLinkEnd::GuestLeft});
    }

    CHECK_EQ(reports.Log->Requests.size(), mostLinks);
}

TEST_CASE("reports: a refused load is reported at once, saying why and what was read of it, and the black box notes it")
{
    using MCTelemetry::MCLoadRefusal;
    MCFakeReports reports;

    // Nothing before the player has seen what the reports hold.
    MCTelemetry::LoadRefused({.Reason = MCLoadRefusal::Unopened, .Kind = "sav", .Player = true});
    CHECK(reports.Log->Requests.empty());

    StartReporting(reports);
    reports.Log->Crash = MCTelemetry::MCCrash{.What = "EXC_BAD_ACCESS SIGSEGV"};
    MCTelemetry::LoadRefused({.Reason = MCLoadRefusal::Components,
                              .Kind = "sav",
                              .Player = true,
                              .Bytes = 52'480,
                              .Packets = 31,
                              .Planet = 0,
                              .Mission = 7,
                              .Scenarios = 71,
                              .At = 37,
                              .Entries = 50});
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    CHECK_EQ(reports.Log->Requests[0].first, std::string("report"));
    const std::string& report = reports.Log->Requests[0].second;
    CHECK(report.starts_with(R"j({"v":1,"kind":"load","first":true,"platform":"mac","app":"0.2.1 (468)")j"));
    CHECK(Holds(report, R"("load":{"reason":"components","file":"sav","player":true,"bytes":52480,"packets":31,)"
                        R"("planet":0,"mission":7,"scenarios":71,"at":37,"entries":50}})"));

    // What wasn't read isn't said: a game's own file that wasn't there.
    MCTelemetry::LoadRefused({.Reason = MCLoadRefusal::Unopened, .Kind = "pkk"});
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{2});
    CHECK(Holds(reports.Log->Requests[1].second, R"("load":{"reason":"unopened","file":"pkk","player":false}})"));

    // A loop of refusals: the reports stop at 10 a run.
    constexpr int32_t manyLoads = 15;
    constexpr size_t mostLoads = 10;

    for (int32_t i = 0; i < manyLoads; i++)
    {
        MCTelemetry::LoadRefused({.Reason = MCLoadRefusal::MissionPast, .Kind = "sav", .Player = true});
    }

    CHECK_EQ(reports.Log->Requests.size(), mostLoads);

    // The run then crashed: its black box says what the loads did.
    reports.Restart();
    MCTelemetry::Start();
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    const std::string& crash = reports.Log->Requests[0].second;
    CHECK(Holds(crash, R"("kind":"crash")"));
    CHECK(Holds(crash, " load refused: components\""));
    CHECK(Holds(crash, " load refused: unopened\""));
    CHECK(Holds(crash, " load refused: missionPast\""));
}

TEST_CASE("reports: each says the card the game draws on as the system says it then, and nothing of one it can't say")
{
    constexpr uint64_t QuarterGb = uint64_t{1} << 28;
    constexpr std::string_view DeviceName = "Test GPU";
    const MCTelemetry::MCRefusedLoad load{.Reason = MCTelemetry::MCLoadRefusal::Unopened, .Kind = "pkk"};
    MCFakeReports reports;
    StartReporting(reports);
    const auto system = std::make_shared<MCFakeMemorySystem>();
    const MCFakeMemoryScope scope(system);
    const std::vector<std::pair<std::string, std::string>>& sent = reports.Log->Requests;

    {
        // A card of its own (a PC's): its memory, and the budget DXGI gives the game there.
        system->Budget = 29 * QuarterGb;
        system->OwnBytes = 32 * QuarterGb;
        const MCMemoryRoom::MCReader renderer{std::string(DeviceName)};
        MCTelemetry::LoadRefused(load);
        REQUIRE_EQ(sent.size(), size_t{1});
        CHECK(Holds(sent[0].second, R"("system":"26.5","cardMemoryGb":8.00,"gpuBudgetGb":7.25,"sharedMemory":false})"));
        CHECK_EQ(system->AskedName, std::string(DeviceName));

        // Apple silicon's: the machine's memory, Metal's working set, and no memory of the card's own.
        system->SharesMemory = true;
        system->OwnBytes.reset();
        system->Budget = 21 * QuarterGb;
        MCTelemetry::LoadRefused(load);
        REQUIRE_EQ(sent.size(), size_t{2});
        CHECK(Holds(sent[1].second, R"("system":"26.5","gpuBudgetGb":5.25,"sharedMemory":true})"));

        // A card the system can't speak for (no answer, or a budget of 0): nothing of it, never a 0.
        system->Reports = false;
        MCTelemetry::LoadRefused(load);
        system->Reports = true;
        system->Budget = 0;
        MCTelemetry::LoadRefused(load);
        REQUIRE_EQ(sent.size(), size_t{4});
        CHECK(Holds(sent[2].second, R"("system":"26.5"})"));
        CHECK(Holds(sent[3].second, R"("system":"26.5"})"));
    }

    // The renderer gone (the CPU draws): its device is asked no more.
    system->Budget = 29 * QuarterGb;
    const int reads = system->CardReads;
    MCTelemetry::LoadRefused(load);
    REQUIRE_EQ(sent.size(), size_t{5});
    CHECK((system->CardReads == reads || system->AskedName != DeviceName));
}

namespace
{
    /// <summary>The content releases of the copy in the content's tests: release 8 installed, release 9's due.</summary>
    constexpr uint64_t InstalledRelease = 8;
    constexpr uint64_t DueRelease = 9;
    /// <summary>The program the copy runs (0.7.0, say), and a later one.</summary>
    constexpr uint64_t RunningProgram = 1001;
    constexpr uint64_t LaterProgram = 1002;

    /// <summary>
    /// A copy's game folder of the test's own, for its due note; its content left out of the reports again at the
    /// test's end.
    /// </summary>
    class MCReportedCopy
    {
    public:
        explicit MCReportedCopy(std::string_view name) : _Folder("reports-content", name) {}

        ~MCReportedCopy() { MCTelemetry::SetContent(std::nullopt); }

        MCReportedCopy(const MCReportedCopy&) = delete;
        MCReportedCopy& operator=(const MCReportedCopy&) = delete;

        /// <summary>
        /// <paramref name="release"/>'s content noted due to programs from <paramref name="lowestProgram"/> on, as a
        /// game that installed the program alone leaves it (0.6.8 installing 0.7.0).
        /// </summary>
        void NoteDue(uint64_t release, uint64_t lowestProgram)
        {
            REQUIRE(MCInstalled::NoteDue(*_Disk, _Folder.Path(), {.Release = release, .LowestProgram = lowestProgram})
                        .has_value());
        }

        /// <summary>The game started on the copy with <paramref name="release"/> installed (0: none, no record).</summary>
        void Start(uint64_t release)
        {
            MCInstalled::MCRecord record;
            record.Release = release;
            MCTelemetry::SetContent(MCTelemetry::ContentOf(*_Disk, _Folder.Path(), record, RunningProgram));
        }

    private:
        MCScratchFolder _Folder;
        std::unique_ptr<MCUpdateDisk> _Disk = MakeDisk();
    };

    /// <summary>A refused load's report, which goes at once.</summary>
    const MCTelemetry::MCRefusedLoad ContentLoad{.Reason = MCTelemetry::MCLoadRefusal::Unopened, .Kind = "pkk"};
}

TEST_CASE("reports: each says the copy's content release and the release due, while the game plays the art it has and "
          "the new art downloads")
{
    MCFakeReports reports;
    StartReporting(reports);
    const std::vector<std::pair<std::string, std::string>>& sent = reports.Log->Requests;
    MCReportedCopy copy("due");

    // 0.6.8 installed this program alone, noting release 9's content due to it: the game plays release 8's art.
    copy.NoteDue(DueRelease, RunningProgram);
    copy.Start(InstalledRelease);
    MCTelemetry::LoadRefused(ContentLoad);
    REQUIRE_EQ(sent.size(), size_t{1});
    CHECK(Holds(sent[0].second, R"(},"content":{"release":8,"due":9},"load":{)"));

    // A match's report says it too.
    MCTelemetry::PlayLoading("mc2mp_01", true);
    MCTelemetry::PlayStarted(2, true);
    Play(130.0);
    MCTelemetry::PlayEnded();
    REQUIRE_EQ(sent.size(), size_t{2});
    CHECK(Holds(sent[1].second, R"("content":{"release":8,"due":9},"display":{)"));
}

TEST_CASE("reports: with no release due, each says the copy's content release alone")
{
    MCFakeReports reports;
    StartReporting(reports);
    const std::vector<std::pair<std::string, std::string>>& sent = reports.Log->Requests;
    MCReportedCopy copy("none-due");

    // No note.
    copy.Start(InstalledRelease);
    MCTelemetry::LoadRefused(ContentLoad);

    // A note for a later program, which this one isn't: it plays the files it has, and is offered the update again.
    copy.NoteDue(DueRelease, LaterProgram);
    copy.Start(InstalledRelease);
    MCTelemetry::LoadRefused(ContentLoad);

    // The release noted due installed.
    copy.NoteDue(DueRelease, RunningProgram);
    copy.Start(DueRelease);
    MCTelemetry::LoadRefused(ContentLoad);

    REQUIRE_EQ(sent.size(), size_t{3});
    CHECK(Holds(sent[0].second, R"("content":{"release":8},"load":{)"));
    CHECK(Holds(sent[1].second, R"("content":{"release":8},"load":{)"));
    CHECK(Holds(sent[2].second, R"("content":{"release":9},"load":{)"));
}

TEST_CASE("reports: a copy with no installed record says nothing of its content")
{
    MCFakeReports reports;
    StartReporting(reports);
    const std::vector<std::pair<std::string, std::string>>& sent = reports.Log->Requests;
    MCReportedCopy copy("no-record");

    // Nothing installed (a first start's copy, or a build that updates no content), a note due or not.
    copy.Start(0);
    MCTelemetry::LoadRefused(ContentLoad);
    copy.NoteDue(DueRelease, RunningProgram);
    copy.Start(0);
    MCTelemetry::LoadRefused(ContentLoad);
    REQUIRE_EQ(sent.size(), size_t{2});

    for (const std::pair<std::string, std::string>& request : sent)
    {
        CHECK(Holds(request.second, R"(},"load":{)"));
        CHECK(!Holds(request.second, "content"));
    }
}

/// <summary>
/// The personal build: a report made with this platform's real backend (MCTelemetryMac.mm, MCTelemetryWin.cpp) and
/// memory system (MCMemorySystemMac.mm, MCMemorySystemWin.cpp) says the card SDL's GPU device runs on, as the system
/// says it; the report's computer printed, a sample of what this machine sends. Skipped without a GPU, or on Windows
/// where DXGI says nothing of the device (a build machine's, drawing in software).
/// </summary>
TEST_CASE_ISOLATED("reports: a report says the card SDL's GPU device runs on, as this platform says it")
{
    constexpr std::string_view UnusedServer = "https://reports.invalid";
    constexpr std::string_view MachineKey = R"("machine":{)";

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        std::cout << std::format("  (skipped: no video: {})\n", SDL_GetError());
        return;
    }

    std::shared_ptr<SDL_GPUDevice> device(SDL_CreateGPUDevice(MCVulkanShaders::Format, false, MCVulkanShaders::Driver),
                                          MCSdlDeleter{});

    if (device == nullptr)
    {
        std::cout << std::format("  (skipped: no GPU device: {})\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return;
    }

    const char* const name =
        SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device.get()), SDL_PROP_GPU_DEVICE_NAME_STRING, "");
    REQUIRE(name != nullptr);

    {
        MCMemoryRoom::SetSystem(MCMakeMemorySystem());
        const MCMemoryRoom::MCReader renderer{std::string(name)};
        const std::optional<MCMemoryRoom::MCCard> card = MCMemoryRoom::DrawnCard();
        MCFakeReports reports;
        const auto backend = MCMakeTelemetryBackend(MCFileSystem::UserRoot(), UnusedServer);
        REQUIRE(backend != nullptr);
        reports.Log->Machine = backend->Machine();
        StartReporting(reports);
        MCTelemetry::LoadRefused({.Reason = MCTelemetry::MCLoadRefusal::Unopened, .Kind = "pkk"});
        REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
        const std::string& report = reports.Log->Requests[0].second;
        const size_t at = report.find(MachineKey);
        REQUIRE(at != std::string::npos);
        std::cout << std::format("  {}: {}\n", name, report.substr(at, report.find('}', at) - at + 1));

#ifdef _WIN32
        if (!card.has_value())
        {
            std::cout << std::format("  (skipped: DXGI says nothing of {})\n", name);
        }
#endif

#if defined(__APPLE__) && defined(__aarch64__)
        // Apple silicon: the machine's memory, Metal's working set, and nothing of a card's own.
        REQUIRE(card.has_value());
        CHECK(Holds(report, R"("sharedMemory":true})"));
        CHECK(!Holds(report, "cardMemoryGb"));
#endif

        if (card.has_value())
        {
            CHECK(card->Budget > 0);
            CHECK(Holds(report, R"("gpuBudgetGb":)"));
            CHECK(Holds(report, card->SharesMemory ? R"("sharedMemory":true)" : R"("sharedMemory":false)"));
            CHECK_EQ(Holds(report, R"("cardMemoryGb":)"), card->OwnBytes.has_value());
        }
        else
        {
            CHECK(!Holds(report, "gpuBudgetGb"));
        }
    }

    MCMemoryRoom::SetSystem(nullptr);
    device.reset();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
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

TEST_CASE("reports: the first mission after an update sends the play before it under its version, then its own")
{
    MCFakeReports reports;
    StartReporting(reports);
    constexpr double missionSeconds = 130.0;
    const auto mission = []
    {
        MCTelemetry::PlayLoading("mis0101", false);
        MCTelemetry::PlayStarted(1, false);
        Play(missionSeconds);
        MCTelemetry::PlayEnded();
    };

    const auto sent = [&](size_t index, std::string_view app, int32_t count)
    {
        const std::string& report = reports.Log->Requests[index].second;
        CHECK(Holds(report, std::format(R"("app":"{}")", app)));
        CHECK(Holds(report, std::format(R"("play":{{"count":{},)", count)));
    };

    constexpr std::string_view before = "0.2.1 (468)";
    constexpr std::string_view after = "0.2.2 (470)";

    // The first, sent; the next, a day on, kept for the week.
    mission();
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    reports.Reply(0, HttpOk);
    reports.Log->Now += SecondsPerDay;
    mission();
    CHECK_EQ(reports.Log->Requests.size(), size_t{1});

    // Updated: the game starts again as a later version, and its first mission, a day on, sends the kept one under the
    // version it was played on, then its own.
    reports.Restart();
    reports.Log->AppVersion = std::string(after);
    MCTelemetry::Start();
    reports.Log->Now += SecondsPerDay;
    mission();
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{2});
    sent(0, before, 1);
    sent(1, after, 1);

    // The old version's not taken: not kept, so it never counts as the new one's. The next mission waits for the week.
    reports.Reply(0, ServerError);
    reports.Reply(1, HttpOk);
    reports.Log->Now += SecondsPerDay;
    mission();
    CHECK_EQ(reports.Log->Requests.size(), size_t{2});
    reports.Log->Now += SecondsPerDay * 7;
    mission();
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{3});
    sent(2, after, 2);
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

namespace
{
    /// <summary>Frames for the menus to lay a new window out and draw it whole.</summary>
    void SettleMenusWindow()
    {
        constexpr int32_t SettleFrames = 4;

        for (int32_t frame = 0; frame < SettleFrames; frame++)
        {
            MCTestGame::RunFrame(1.0f / 60.0f);
        }
    }

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 6): the reports' notice remade, on top of the menus: its title and sentences,
    /// OK alone; null when it doesn't show.
    /// </summary>
    std::shared_ptr<MCRmlScreen> RemadeNotice()
    {
        std::shared_ptr<MCRmlScreen> notice = MCRemadeTest::OnTop<MCRmlScreen>();

        if (notice == nullptr || MCRemadeTest::Words(*notice, "heading") != "Reports")
        {
            return nullptr;
        }

        CHECK_EQ(MCRemadeTest::Words(*notice, "ask"),
                 std::string("MechCommander Redux sends anonymous reports of your computer, frame rates, settings and "
                             "crashes to help it run better."));
        CHECK_EQ(MCRemadeTest::Words(*notice, "kept"), std::string("Turn this off under Reports in Preferences."));
        CHECK_EQ(MCRemadeTest::Words(*notice, "yes"), std::string("OK"));
        CHECK(!MCRemadeTest::Visible(*notice, "no"));
        return notice;
    }

    /// <summary>Whether <paramref name="screen"/>'s document has the keys (the menus' focus is in it).</summary>
    bool HasKeys(const MCRmlScreen& screen)
    {
        const MCOptionalRef<Rml::ElementDocument> document = screen.Document();
        Rml::Context* context = document.has_value() ? document->get().GetContext() : nullptr;
        const Rml::Element* focus = context != nullptr ? context->GetFocusElement() : nullptr;
        return focus != nullptr && focus->GetOwnerDocument() == &document->get();
    }

    /// <summary>
    /// A lobby's game hosting on <paramref name="port"/> as BOB, the reports' region answered: its remade room with the
    /// notice over it, the keys the notice's (not the room's chat box).
    /// </summary>
    std::shared_ptr<MCRmlScreen> NoticeOverLobbyRoom(std::string_view port, MCFakeReports& reports)
    {
        REQUIRE(MCTestGame::StartLobby(std::format("-host -port {} -player BOB", port)));
        REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
        reports.Reply(0, HttpOk, R"({"country":"US","ask":false})");
        MCTestGuest::Settle();
        REQUIRE(MCMenuScreens::Top() == MCMenuScreens::Room());
        CHECK(RoomScreenShown());
        CHECK(!GlobalLog()->Message->IsShowing());
        std::shared_ptr<MCRmlScreen> notice = RemadeNotice();
        REQUIRE(notice != nullptr);
        CHECK(HasKeys(*notice));
        return notice;
    }

    /// <summary>The notice over a lobby's room answered: gone, the room on top with the keys, REPORTS on.</summary>
    void NoticeAnsweredOverRoom()
    {
        CHECK(RemadeNotice() == nullptr);
        const std::shared_ptr<MCRoomScreen> room = MCRemadeTest::OnTop<MCRoomScreen>();
        REQUIRE(room != nullptr);
        CHECK(HasKeys(*room));
        CHECK_EQ(GReports, 1);
        CHECK(MCTelemetry::Reporting());
    }
}

/// <summary>
/// Port-only (SPEC-menus.md's stage 7): over the remade main menu the notice is stage 6's remade dialog (the 1998 one
/// would show under it), drawn on the software renderer at the recorded frames' windows; its OK turns REPORTS on.
/// </summary>
TEST_CASE_ISOLATED("game: the main menu shows the reports' notice once, and its OK turns REPORTS on")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs prefs;
    MCFakeReports reports;
    GRenderer = static_cast<int>(MCRendererKind::Software);
    // The saves pinned (none): the menu under the dialog the same whatever earlier tests left.
    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    MCTestGame::RunFrame(1.0f / 15.0f);
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    reports.Reply(0, HttpOk, R"({"country":"US","ask":false})");
    MCTestGame::RunFrame(1.0f / 15.0f);
    MCMenuDialog& dialog = *GlobalLog()->Message;
    CHECK(!dialog.IsShowing());
    const std::shared_ptr<MCRmlScreen> notice = RemadeNotice();
    REQUIRE(notice != nullptr);
    MCScreenInput::SaveWindowShot("reports notice");
    // Port-only (stage 7): over the remade main menu, at the recorded frames' windows.
    MCScreenInput::RealMove(0, 0);
    MCMenusPictures::CheckWindowFrames(*MCInput::Display(), "main_menu_reports_notice",
                                       {0xfa1efc70, 0x70293fd0, 0x252f245e}, SettleMenusWindow);
    REQUIRE(MCRemadeTest::Press(*notice, "yes"));
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(MCMainMenuScreens::MainOnTop());
    CHECK_EQ(GReports, 1);
    CHECK(MCTelemetry::Reporting());
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(MCMainMenuScreens::MainOnTop());
}

/// <summary>
/// A game a lobby starts never shows the main menu: its room (the session screen) shows the reports' notice instead,
/// once: not again each frame while it shows, nor as the room is shown again for a player who joins. Over the remade
/// room (stage 4) the notice has the keys, a guest's join leaving it on top; Escape closes it, as seen.
/// </summary>
TEST_CASE_ISOLATED("game: a lobby's session screen shows the reports' notice once")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs prefs;
    MCFakeReports reports;
    MCTestGuest::Guest guest("ANN");
    const std::shared_ptr<MCRmlScreen> notice = NoticeOverLobbyRoom("28899", reports);
    MCScreenInput::SaveWindowShot("reports notice session screen");

    // A guest joins while it shows: still the one notice on top, with the keys.
    REQUIRE(guest.Join(*MultiPlayer()->SessionManager->DirectPlay, "127.0.0.1:28899"));
    REQUIRE(MCTestGuest::SettleUntil([&] { return MCTestGuest::RoomShows(guest.Id()); }));
    MCTestGuest::Settle();
    CHECK(RemadeNotice() == notice);
    CHECK(HasKeys(*notice));

    MCScreenInput::RealKey(SDL_SCANCODE_ESCAPE);
    MCTestGuest::Settle();
    NoticeAnsweredOverRoom();
    CHECK(!GlobalLog()->Message->IsShowing());
}

/// <summary>The reports' notice over a lobby's remade room: Enter, as OK.</summary>
TEST_CASE_ISOLATED("game: Enter closes the reports' notice over a lobby's room")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs prefs;
    MCFakeReports reports;
    REQUIRE(NoticeOverLobbyRoom("28892", reports) != nullptr);
    MCScreenInput::RealKey(SDL_SCANCODE_RETURN);
    MCTestGuest::Settle();
    NoticeAnsweredOverRoom();
}

/// <summary>The reports' notice over a lobby's remade room: OK clicked (the mouse reaches it too).</summary>
TEST_CASE_ISOLATED("game: OK closes the reports' notice over a lobby's room")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs prefs;
    MCFakeReports reports;
    const std::shared_ptr<MCRmlScreen> notice = NoticeOverLobbyRoom("28896", reports);
    Rml::Element* ok = MCRemadeTest::Find(*notice, "yes");
    REQUIRE(ok != nullptr);
    MCTestGuest::RealClickOn(*ok);
    MCTestGuest::Settle();
    NoticeAnsweredOverRoom();
}

TEST_CASE_ISOLATED("game: where the law wants consent the main menu asks, and CANCEL leaves REPORTS off")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs prefs;
    MCFakeReports reports;
    reports.Log->Countries = {"DE"};
    GRenderer = static_cast<int>(MCRendererKind::Software);
    // The saves pinned (none): the menu under the dialog the same whatever earlier tests left.
    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(reports.Log->Requests.empty());
    // Port-only (SPEC-menus.md's stage 6): the question remade, a dialog over the main menu, SEND with the focus.
    CHECK(!GlobalLog()->Question->IsShowing());
    const std::shared_ptr<MCRmlScreen> question = MCRemadeTest::OnTop<MCRmlScreen>();
    REQUIRE(question != nullptr);
    CHECK_EQ(MCRemadeTest::Words(*question, "heading"), std::string("Send reports?"));
    const MCOptionalRef<MCRmlMenus> menus = MCRmlMenus::Running();
    REQUIRE(menus.has_value());
    CHECK(menus->get().Menus().Context().GetFocusElement() == MCRemadeTest::Find(*question, "yes"));
    CHECK(MCRemadeTest::Visible(*question, "no"));
    CHECK_EQ(MCRemadeTest::Words(*question, "ask"),
             std::string("Send reports of your computer, frame rates, settings and crashes, under a random ID, to help "
                         "MechCommander Redux run better?"));
    CHECK_EQ(MCRemadeTest::Words(*question, "kept"), std::string("No name or IP address is kept."));
    MCScreenInput::SaveWindowShot("reports question");
    // Port-only (stage 7): over the remade main menu, at the recorded frames' windows.
    MCScreenInput::RealMove(0, 0);
    MCMenusPictures::CheckWindowFrames(*MCInput::Display(), "main_menu_reports_question",
                                       {0x64cb4c3a, 0x26198167, 0x9aad576e}, SettleMenusWindow);
    REQUIRE(MCRemadeTest::Press(*question, "no"));
    MCTestGame::RunFrame(1.0f / 15.0f);
    CHECK(MCRemadeTest::OnTop<MCRmlScreen>() != question);
    CHECK(MCMainMenuScreens::MainOnTop());
    CHECK_EQ(GReports, 0);
    CHECK(!MCTelemetry::Reporting());
    CHECK(MCTelemetry::Prompt() == MCPrompt::None);
}

namespace
{
    /// <summary>
    /// Copies save <paramref name="from"/> as <paramref name="to"/>, its settings' text (its first packet) with
    /// <paramref name="was"/> made <paramref name="becomes"/>, as a damaged save's might be.
    /// </summary>
    void CopySaveChanged(std::string_view from, std::string_view to, std::string_view was, std::string_view becomes)
    {
        MCPacketFile in;
        REQUIRE_EQ(in.Open(GamePath(SavePath, from, ".sav")), 0);
        std::vector<std::vector<uint8_t>> packets;

        for (int32_t packet = 0; packet < in.GetNumPackets(); packet++)
        {
            REQUIRE_EQ(in.SeekPacket(packet), 0);
            std::vector<uint8_t>& data = packets.emplace_back(static_cast<size_t>(in.GetPacketSize()));
            REQUIRE_EQ(in.ReadPacket(packet, data), in.GetPacketSize());
        }

        in.Close();
        std::string settings(packets[0].begin(), packets[0].end());
        const size_t at = settings.find(was);
        REQUIRE(at != std::string::npos);
        settings.replace(at, was.size(), becomes);
        packets[0].assign(settings.begin(), settings.end());
        MCPacketFile out;
        REQUIRE_EQ(out.Create(GamePath(SavePath, to, ".sav")), 0);
        out.Reserve(static_cast<int32_t>(packets.size()));

        for (size_t packet = 0; packet < packets.size(); packet++)
        {
            out.WritePacket(static_cast<int32_t>(packet), packets[packet], MCPacketStorage::Nul);
        }

        out.Close();
    }

    /// <summary>The bytes and packets of save <paramref name="name"/>, as its report gives them.</summary>
    std::string FileJson(std::string_view name)
    {
        MCPacketFile file;
        REQUIRE_EQ(file.Open(GamePath(SavePath, name, ".sav")), 0);
        return std::format(R"("bytes":{},"packets":{})", file.FileSize(), file.GetNumPackets());
    }
}

/// <summary>
/// Each file the campaign's load refuses is reported at once, saying why, with what was read of it and never its name:
/// a save naming a mission past the campaign, one naming its end, one with a component entry gone, a save that isn't
/// there, and a start file of the game's that isn't.
/// </summary>
TEST_CASE_ISOLATED("game: each refused load is reported at once, saying why and what was read of the file")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr int32_t damagedEntry = 37;
    constexpr int32_t componentEntries = 50;
    const MCTestGame::KeepPrefs prefs;
    MCFakeReports reports;
    // Its saves in a folder of its own, gone with it whatever its result: a refused one left behind would be the next
    // test's to read.
    std::optional<MCTestGame::PinnedSaves> pinned;
    REQUIRE(MCTestGame::StartLogistics([&pinned] { pinned.emplace(); }));
    MCTestGame::RunFrame(1.0f / 15.0f);
    REQUIRE_EQ(reports.Log->Requests.size(), size_t{1});
    reports.Reply(0, HttpOk, R"({"country":"US","ask":false})");
    MCTestGame::RunFrame(1.0f / 15.0f);
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    const std::shared_ptr<MCRmlScreen> notice = RemadeNotice();
    REQUIRE(notice != nullptr);
    REQUIRE(MCRemadeTest::Press(*notice, "yes"));
    MCTestGame::RunFrame(1.0f / 15.0f);
    REQUIRE(MCTelemetry::Reporting());
    NewCampaign();
    REQUIRE(MCTestGame::FinishChanges());
    const int32_t good = logistics->CurrentMission;
    REQUIRE(good >= 0);
    const auto scenarios = static_cast<int32_t>(Mission()->Scenarios.size());
    REQUIRE_EQ(logistics->SaveCampaign("p3reported"), 0);
    const std::string mission = std::format("l MissionNumber={}\r\n", good);
    CopySaveChanged("p3reported", "p3past", mission, std::format("l MissionNumber={}\r\n", scenarios + 1));
    CopySaveChanged("p3reported", "p3end", mission, std::format("l MissionNumber={}\r\n", scenarios));
    CopySaveChanged("p3reported", "p3damaged", std::format("[Componant{}]", damagedEntry), "[Xomponant]");
    reports.Log->Requests.clear();

    // Loads <name> as the player (or the game) does: refused, and its report sent at once, without the name.
    const auto refused = [&](std::string_view name, std::string_view extension, bool player)
    {
        MCTest::Scope scope(name);
        const size_t before = reports.Log->Requests.size();
        CHECK(logistics->LoadCampaign(name, extension, false, player) != 0);
        CHECK_EQ(logistics->CurrentMission, -1);
        REQUIRE_EQ(reports.Log->Requests.size(), before + 1);
        const std::string report = reports.Log->Requests.back().second;
        CHECK(Holds(report, R"("kind":"load")"));
        CHECK(!Holds(report, name));
        return report;
    };

    const std::string read = std::format(R"("planet":0,"mission":{},"scenarios":{})", good, scenarios);
    CHECK(Holds(refused("p3past", ".sav", true),
                std::format(R"("load":{{"reason":"missionPast","file":"sav","player":true,{},"planet":0,)"
                            R"("mission":{},"scenarios":{}}}}})",
                            FileJson("p3past"), scenarios + 1, scenarios)));
    CHECK(Holds(refused("p3end", ".sav", true),
                std::format(R"("load":{{"reason":"campaignEnd","file":"sav","player":true,{},"planet":0,)"
                            R"("mission":{},"scenarios":{}}}}})",
                            FileJson("p3end"), scenarios, scenarios)));
    CHECK(Holds(refused("p3damaged", ".sav", true),
                std::format(R"("load":{{"reason":"components","file":"sav","player":true,{},{},"at":{},)"
                            R"("entries":{}}}}})",
                            FileJson("p3damaged"), read, damagedEntry, componentEntries)));

    {
        MCTest::ExpectAssert missing("campaign file NOT Valid", 2);
        CHECK(Holds(refused("p3missing", ".sav", true), R"("load":{"reason":"unopened","file":"sav","player":true}})"));
        CHECK(
            Holds(refused("p3missing", ".pkk", false), R"("load":{"reason":"unopened","file":"pkk","player":false}})"));
    }

    // The good save loads, and says nothing.
    const size_t reported = reports.Log->Requests.size();
    CHECK_EQ(logistics->LoadCampaign("p3reported", ".sav", false, true), 0);
    CHECK_EQ(logistics->CurrentMission, good);
    CHECK_EQ(reports.Log->Requests.size(), reported);
}

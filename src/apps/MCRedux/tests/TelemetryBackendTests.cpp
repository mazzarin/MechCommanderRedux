#include "stdafx.h"
#include "../MCTelemetryBackend.h"
#include "MCTest.h"
#include "MCVersion.h"
#include "platform/MCFileSystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <future>

// Port-only (the personal build): this platform's real reports backend (MCTelemetryMac.mm, MCTelemetryWin.cpp), where
// TelemetryTests.cpp has a stand-in: what it reads of the computer, the black box it keeps, and, on Windows, the crash
// record MCCrashTrace writes. With --reports-server <address> (the fork's Windows build passes the reports Worker's),
// what reaches the server too.

namespace
{
    /// <summary>An address the backend is made with when the test sends nothing (never contacted).</summary>
    constexpr std::string_view UnusedServer = "https://reports.invalid";
    /// <summary>The option naming a server to send to, and the one a crashing run of this test gets.</summary>
    constexpr std::string_view ServerOption = "reports-server";
    constexpr std::string_view CrashFolderOption = "reports-crash-folder";
#ifdef _WIN32
    constexpr std::string_view ThisPlatform = "windows";
#else
    constexpr std::string_view ThisPlatform = "mac";
#endif
    /// <summary>A black box's bytes, and the pattern written into it.</summary>
    constexpr size_t BoxBytes = 4096;
    constexpr uint8_t BoxPatternStep = 7;
    /// <summary>Seconds the backend's clock may differ from the test's.</summary>
    constexpr int64_t ClockSlackSeconds = 5;
    /// <summary>The system's thermal states run 0 (nominal) to 3 (critical).</summary>
    constexpr int32_t MaxThermal = 3;
    constexpr size_t CountryLetters = 2;
    /// <summary>How long a request may take: the backends wait 20 s a step.</summary>
    constexpr std::chrono::seconds AnswerWait{60};
    constexpr int32_t HttpOk = 200;
    constexpr int32_t HttpRefused = 400;
    /// <summary>JSON the server parses but keeps nothing of: not a report of its version.</summary>
    constexpr std::string_view NotAReport = R"({"v":0,"kind":"backend test"})";
    constexpr std::string_view RefusedReply = "Not a report this server reads";

    /// <summary>A folder of the test's own under the temporary folder, emptied before and after.</summary>
    class MCScratchFolder
    {
    public:
        explicit MCScratchFolder(std::string_view name) : Path(std::filesystem::temp_directory_path() / name)
        {
            std::error_code error;
            std::filesystem::remove_all(Path, error);
            std::filesystem::create_directories(Path, error);
        }

        ~MCScratchFolder()
        {
            std::error_code error;
            std::filesystem::remove_all(Path, error);
        }

        MCScratchFolder(const MCScratchFolder&) = delete;
        MCScratchFolder& operator=(const MCScratchFolder&) = delete;

        const std::filesystem::path Path;
    };

    bool IsCountry(std::string_view code)
    {
        return code.size() == CountryLetters &&
               std::ranges::all_of(code, [](char letter) { return letter >= 'A' && letter <= 'Z'; });
    }

    uint8_t BoxByte(size_t at)
    {
        return static_cast<uint8_t>(at * BoxPatternStep + 1);
    }

    /// <summary>The backend's answer to a request: its status (0: none in time) and reply.</summary>
    std::pair<int32_t, std::string> Ask(MCTelemetry::MCBackend& backend, std::string path, std::string body)
    {
        // Shared: a late answer, after the wait gave up, still has somewhere to go.
        auto answer = std::make_shared<std::promise<std::pair<int32_t, std::string>>>();
        std::future<std::pair<int32_t, std::string>> answered = answer->get_future();
        backend.Request(std::move(path), std::move(body),
                        [answer](int32_t status, std::string reply) { answer->set_value({status, std::move(reply)}); });

        if (answered.wait_for(AnswerWait) != std::future_status::ready)
        {
            return {0, "no answer in time"};
        }

        return answered.get();
    }
}

TEST_CASE("reports' backend: this computer's model, processor, cores, memory, system, power and countries, and the "
          "black box kept from one run to the next")
{
    const MCScratchFolder folder("mc_tests_reports_backend");
    {
        const auto backend = MCMakeTelemetryBackend(folder.Path, UnusedServer);
        REQUIRE(backend != nullptr);
        const MCTelemetry::MCMachine machine = backend->Machine();
        CHECK(machine.Platform == ThisPlatform);
        CHECK(machine.AppVersion == MC_VERSION_SHOWN);
        // Windows' is the maker and the model, a space between: one of them at least.
        CHECK(machine.Model.find_first_not_of(' ') != std::string::npos);
        CHECK(!machine.Cpu.empty());
        CHECK(machine.PerformanceCores > 0);
        CHECK(machine.EfficiencyCores >= 0);
        CHECK(machine.GpuCores >= 0);
        CHECK(machine.MemoryGb > 0);
        CHECK(!machine.System.empty());

        const MCTelemetry::MCPower power = backend->Power();
        CHECK(power.Thermal >= 0);
        CHECK(power.Thermal <= MaxThermal);

        for (const std::string& country : backend->LocalCountries())
        {
            const MCTest::Scope scope(country);
            CHECK(IsCountry(country));
        }

        const int64_t now =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        CHECK(std::abs(backend->Now() - now) <= ClockSlackSeconds);
        // Nothing crashed since this run began.
        CHECK(!backend->Crash(backend->Now()).has_value());

        const std::span<std::byte> box = backend->MapBlackBox(BoxBytes);
        REQUIRE(box.size() == BoxBytes);

        for (size_t at = 0; at < box.size(); ++at)
        {
            box[at] = std::byte{BoxByte(at)};
        }
    }

    // The next run's backend finds what the last left in the box.
    const auto backend = MCMakeTelemetryBackend(folder.Path, UnusedServer);
    REQUIRE(backend != nullptr);
    const std::span<std::byte> box = backend->MapBlackBox(BoxBytes);
    REQUIRE(box.size() == BoxBytes);
    size_t kept = 0;

    for (size_t at = 0; at < box.size(); ++at)
    {
        kept += box[at] == std::byte{BoxByte(at)} ? 1 : 0;
    }

    CHECK(kept == BoxBytes);
    CHECK(MCMakeTelemetryBackend(folder.Path, "http://reports.invalid") == nullptr);
}

#ifdef _WIN32
// The Mac's crashes are the system's own reports (~/Library/Logs/DiagnosticReports), which a test's crash would add
// to: there the backend's reading of them is left to a real crash.
TEST_CASE("reports' backend: a crash leaves MCCrashTrace's record in the user folder, which the next start reads")
{
    if (const auto crashFolder = MCTest::Option(CrashFolderOption))
    {
        // The crashing run: its backend points the crash handler at the user folder, as the game's does.
        const auto backend = MCMakeTelemetryBackend(MCFileSystem::FromUtf8(*crashFolder), UnusedServer);
        std::abort();
    }

    const MCScratchFolder folder("mc_tests_reports_crash");
    const auto backend = MCMakeTelemetryBackend(folder.Path, UnusedServer);
    REQUIRE(backend != nullptr);
    const int64_t started = backend->Now();
    wchar_t* program = nullptr;
    REQUIRE(_get_wpgmptr(&program) == 0);
    REQUIRE(program != nullptr);
    // cmd.exe drops the first and last quote of a command that starts with one, so the whole is quoted again.
    const std::wstring command = std::format(L"\"\"{}\" --only \"{}\" --{} \"{}\" 2>&1\"", program,
                                             MCFileSystem::FromUtf8(MCTest::CurrentTestName()).native(),
                                             MCFileSystem::FromUtf8(CrashFolderOption).native(), folder.Path.native());
    FILE* pipe = _wpopen(command.c_str(), L"r");
    REQUIRE(pipe != nullptr);
    std::string output;
    std::array<char, BoxBytes> chunk{};

    for (size_t read = 0; (read = std::fread(chunk.data(), 1, chunk.size(), pipe)) > 0;)
    {
        output.append(chunk.data(), read);
    }

    const int status = _pclose(pipe);
    const MCTest::Scope printed("the crashing run printed:\n" + output);
    CHECK(status != 0);

    const std::optional<MCTelemetry::MCCrash> crash = backend->Crash(started);
    REQUIRE(crash.has_value());
    // abort() is raised as the CRT's failure (MCCrashTrace's 0xE0435254).
    CHECK(crash->What.starts_with("Crash: exception 0xe0435254"));
    CHECK(
        std::ranges::any_of(crash->Frames, [](const std::string& frame) { return frame.starts_with("mc_tests +0x"); }));
    CHECK(std::ranges::none_of(crash->Frames, [](const std::string& frame) { return frame.contains('\\'); }));
    CHECK(!crash->What.contains('\\'));
}
#endif

TEST_CASE("tool: --reports-server <address> checks this platform's requests reach the reports server: the region it "
          "answers, and a report it refuses")
{
    const auto server = MCTest::Option(ServerOption);

    if (!server.has_value())
    {
        return;
    }

    const MCScratchFolder folder("mc_tests_reports_server");
    const auto backend = MCMakeTelemetryBackend(folder.Path, *server);
    REQUIRE(backend != nullptr);

    // The region: nothing in the request; the country and whether to ask in the reply.
    const auto [regionStatus, region] = Ask(*backend, "region", {});
    const MCTest::Scope regionScope("the region's reply: " + region);
    CHECK(regionStatus == HttpOk);
    CHECK(region.contains(R"("country":")"));
    CHECK(region.contains(R"("ask":)"));

    // A POST the server parses as JSON (so the body arrived whole) but keeps nothing of.
    const auto [reportStatus, reply] = Ask(*backend, "report", std::string(NotAReport));
    const MCTest::Scope reportScope("the report's reply: " + reply);
    CHECK(reportStatus == HttpRefused);
    CHECK(reply.starts_with(RefusedReply));
}

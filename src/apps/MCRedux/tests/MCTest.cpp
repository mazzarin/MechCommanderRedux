#include "MCTest.h"
#include "../MCCrashTrace.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <thread>

#include "lib/MCFatal.h"
#include "platform/MCAllocator.h"
#include "platform/MCUtf8Path.h"

namespace
{
    struct RunState
    {
        std::vector<std::string> Scopes;
        std::string_view Current;
        size_t Passes = 0;
        size_t Failures = 0;
    };

    RunState& State()
    {
        static RunState state;
        return state;
    }

    /// <summary>The options given (<c>--name value</c>), by name without the dashes.</summary>
    std::vector<std::pair<std::string, std::string>>& Options()
    {
        static std::vector<std::pair<std::string, std::string>> options;
        return options;
    }

    std::string Lower(std::string_view text)
    {
        std::string lower(text);
        std::ranges::transform(lower, lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lower;
    }

    /// <summary>Whether a test runs: no filters, or its name holds any filter, ignoring case.</summary>
    bool Selected(const MCTest::TestCase& test, const std::vector<std::string>& filters)
    {
        if (filters.empty())
        {
            return true;
        }

        const std::string name = Lower(test.Name);
        return std::ranges::any_of(filters,
                                   [&](const std::string& filter) { return name.find(filter) != std::string::npos; });
    }
}

namespace MCTest
{
    std::vector<TestCase>& Registry()
    {
        static std::vector<TestCase> registry;
        return registry;
    }

    Registrar::Registrar(std::string_view name, std::function<void()> body, std::string_view file, int line,
                         bool isolated)
    {
        Registry().push_back({name, std::move(body), file, line, isolated});
    }

    Scope::Scope(std::string_view text)
    {
        State().Scopes.emplace_back(text);
    }

    Scope::~Scope()
    {
        State().Scopes.pop_back();
    }

    void ReportFailure(std::string_view file, int line, std::string_view message)
    {
        RunState& state = State();
        ++state.Failures;
        std::cout << "  " << file << "(" << line << "): " << message << "\n";

        for (const std::string& scope : state.Scopes)
        {
            std::cout << "    in " << scope << "\n";
        }
    }

    void ReportPass()
    {
        ++State().Passes;
    }
}

std::optional<std::string_view> MCTest::Option(std::string_view name)
{
    for (const auto& [key, value] : Options())
    {
        if (key == name)
        {
            return value;
        }
    }

    return std::nullopt;
}

std::string_view MCTest::CurrentTestName()
{
    return State().Current;
}

namespace
{
    /// <summary>The runner's own option (not passed on to isolated tests' processes): how many isolated tests run at
    /// once.</summary>
    constexpr std::string_view JobsOption = "--jobs";
    /// <summary>The runner's own option: the temporary folder this process runs in (a worker's).</summary>
    constexpr std::string_view TempFolderOption = "--temp-folder";

    /// <summary>The temporary folder of each worker running isolated tests at once, under the run's own: a worker's
    /// tests keep their user folder (mc_tests_user) and scratch files apart from the other workers'.</summary>
    constexpr std::string_view WorkerFolderPrefix = "mc_tests_worker_";

    /// <summary>The bytes an isolated test's output is read in at a time.</summary>
    constexpr size_t OutputChunk = 4096;

    /// <summary>A folder argument quoted: a trailing backslash would escape the closing quote, so it is doubled.</summary>
    std::string Quoted(std::string_view value)
    {
        const size_t trailing = value.size() - (value.find_last_not_of('\\') + 1);
        return std::format("\"{}{}\"", value, std::string(trailing, '\\'));
    }

    /// <summary>
    /// The command (UTF-8) running an isolated test in a process of its own: this program again, with <c>--only</c>,
    /// the test's name and the options, in <paramref name="tempFolder"/> when given, its output and errors together
    /// when <paramref name="joinErrors"/>.
    /// </summary>
    std::string IsolatedCommand(std::string_view program, const MCTest::TestCase& test,
                                const std::optional<std::filesystem::path>& tempFolder, bool joinErrors)
    {
        std::string command = std::format("\"{}\" --only \"{}\"", program, test.Name);

        for (const auto& [key, value] : Options())
        {
            command += std::format(" --{} {}", key, Quoted(value));
        }

        if (tempFolder.has_value())
        {
            command += std::format(" {} {}", TempFolderOption, Quoted(MCFileSystem::ToUtf8(*tempFolder)));
        }

#ifdef _WIN32
        // cmd.exe drops the first and last quote of a command that starts with one.
        command = "\"" + command + "\"";
#endif
        return joinErrors ? command + " 2>&1" : command;
    }

    /// <summary>Runs an isolated test in a process of its own (<see cref="IsolatedCommand"/>).</summary>
    /// <returns>Whether it passed (the child exited 0).</returns>
    bool RunIsolated(std::string_view program, const MCTest::TestCase& test)
    {
        const std::string command = IsolatedCommand(program, test, std::nullopt, false);
        std::cout << std::flush;
#ifdef _WIN32
        // The wide call keeps every character of the UTF-8 command; system would read it in the ANSI code page.
        return _wsystem(MCFileSystem::FromUtf8(command).c_str()) == 0;
#else
        return std::system(command.c_str()) == 0;
#endif
    }

    /// <summary>Runs <paramref name="command"/> (UTF-8), keeping what it prints.</summary>
    /// <returns>Whether it exited 0, and its output (or why it couldn't start).</returns>
    std::pair<bool, std::string> RunCaptured(const std::string& command)
    {
#ifdef _WIN32
        // Wide, as RunIsolated's call.
        FILE* pipe = _wpopen(MCFileSystem::FromUtf8(command).c_str(), L"r");
#else
        FILE* pipe = popen(command.c_str(), "r");
#endif

        if (pipe == nullptr)
        {
            return {false, std::format("  can't start {}: {}\n", command,
                                       std::error_code(errno, std::generic_category()).message())};
        }

        std::string output;
        std::array<char, OutputChunk> chunk{};

        for (size_t read = 0; (read = std::fread(chunk.data(), 1, chunk.size(), pipe)) > 0;)
        {
            output.append(chunk.data(), read);
        }

#ifdef _WIN32
        const int status = _pclose(pipe);
#else
        const int status = pclose(pipe);
#endif
        return {status == 0, std::move(output)};
    }

    /// <summary>
    /// Runs <paramref name="tests"/> (isolated ones), <paramref name="jobs"/> at a time, each worker's in a temporary
    /// folder of its own under this run's, printing each one's output whole as it ends.
    /// </summary>
    /// <returns>The tests that failed, or why the workers' folders can't be made.</returns>
    std::expected<std::vector<std::reference_wrapper<const MCTest::TestCase>>, std::string> RunIsolatedAtOnce(
        std::string_view program, std::span<const std::reference_wrapper<const MCTest::TestCase>> tests, int32_t jobs)
    {
        std::vector<std::filesystem::path> folders;

        for (int32_t worker = 0; worker < jobs; ++worker)
        {
            std::error_code error;
            const std::filesystem::path folder =
                std::filesystem::temp_directory_path(error) / std::format("{}{}", WorkerFolderPrefix, worker);

            if (!error)
            {
                std::filesystem::create_directories(folder, error);
            }

            if (error)
            {
                return std::unexpected(
                    std::format("can't make the folder {}: {}", MCFileSystem::ToUtf8(folder), error.message()));
            }

            folders.push_back(folder);
        }

        std::vector<std::reference_wrapper<const MCTest::TestCase>> failed;
        std::atomic<size_t> next = 0;
        std::mutex lock;
        const auto work = [&](const std::filesystem::path& folder)
        {
            for (size_t i = next++; i < tests.size(); i = next++)
            {
                const MCTest::TestCase& test = tests[i];
                const auto [passed, output] = RunCaptured(IsolatedCommand(program, test, folder, true));
                const std::scoped_lock guard(lock);
                std::cout << "[ PROC ] " << test.Name << "\n" << output << std::flush;

                if (!passed)
                {
                    failed.emplace_back(test);
                }
            }
        };

        std::vector<std::thread> workers;

        for (const std::filesystem::path& folder : folders)
        {
            workers.emplace_back(work, folder);
        }

        for (std::thread& worker : workers)
        {
            worker.join();
        }

        return failed;
    }

    /// <summary>
    /// Makes <paramref name="folder"/> (UTF-8) this process's temporary folder (what temp_directory_path gives).
    /// </summary>
    /// <returns>Whether it was set.</returns>
    bool SetTempFolder(std::string_view folder)
    {
#ifdef _WIN32
        // Wide, so a folder outside the ANSI code page keeps every character.
        const std::filesystem::path path = MCFileSystem::FromUtf8(folder);
        return _wputenv_s(L"TMP", path.c_str()) == 0 && _wputenv_s(L"TEMP", path.c_str()) == 0;
#else
        return setenv("TMPDIR", std::string(folder).c_str(), 1) == 0;
#endif
    }
}

/// <summary>
/// Runs the registered tests. Arguments are name filters (a test runs when its name holds any of them, ignoring case);
/// <c>--list</c> prints the names instead, and <c>--only &lt;name&gt;</c> runs the one test of exactly that name in this
/// process (how isolated tests are run). <c>--jobs &lt;n&gt;</c> runs the isolated tests n at a time, after the others,
/// each worker's in a temporary folder of its own (its own user folder: a test that reads what earlier ones saved there
/// may see less). Any other <c>--name value</c> is an option the tests read (<see cref="MCTest::Option"/>). Exits 0 when
/// every selected test passed.
/// </summary>
#ifdef _WIN32
// Windows hands main the command line in the ANSI code page, which can't spell every folder; wmain gets it whole.
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    if (const auto allocator = MCInitializeAllocator(); !allocator)
    {
        std::cerr << allocator.error() << "\n";
        return 1;
    }

    // A fatal error fails the test process; a message box would wait for someone to click it. A crash (or a CRT
    // assertion) prints its stack and writes mc_tests.dmp instead of showing a dialog.
    MCNoMessageBoxes = true;
    MCCrashTrace::Install();
    std::vector<std::string> arguments;

    for (int i = 0; i < argc; i++)
    {
        // A path converts the wide argument to UTF-8; on other systems it is UTF-8 already.
        arguments.push_back(MCFileSystem::ToUtf8(std::filesystem::path(argv[i])));
    }

    std::vector<std::string> filters;
    bool list = false;
    std::optional<std::string_view> only;
    int32_t jobs = 1;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = arguments[i];

        if (arg == "--list")
        {
            list = true;
        }
        else if (arg == "--only" && i + 1 < argc)
        {
            only = arguments[++i];
        }
        else if (arg == JobsOption && i + 1 < argc)
        {
            const std::string_view given = arguments[++i];
            const auto [end, error] = std::from_chars(given.data(), given.data() + given.size(), jobs);

            if (error != std::errc() || end != given.data() + given.size() || jobs < 1)
            {
                std::cerr << JobsOption << " takes a number of tests to run at once, not \"" << given << "\"\n";
                return 1;
            }
        }
        else if (arg == TempFolderOption && i + 1 < argc)
        {
            const std::string_view folder = arguments[++i];

            if (!SetTempFolder(folder))
            {
                std::cerr << "can't make " << folder << " the temporary folder\n";
                return 1;
            }
        }
        else if (arg.starts_with("--") && arg.size() > 2 && i + 1 < argc)
        {
            Options().emplace_back(std::string(arg.substr(2)), arguments[++i]);
        }
        else
        {
            filters.push_back(Lower(arg));
        }
    }

    std::vector<std::reference_wrapper<const MCTest::TestCase>> selected;

    for (const MCTest::TestCase& test : MCTest::Registry())
    {
        if (only ? test.Name == *only : Selected(test, filters))
        {
            selected.emplace_back(test);
        }
    }

    if (list)
    {
        for (const MCTest::TestCase& test : selected)
        {
            std::cout << test.Name << "\n";
        }

        return 0;
    }

    RunState& state = State();
    std::vector<std::reference_wrapper<const MCTest::TestCase>> failed;
    std::vector<std::reference_wrapper<const MCTest::TestCase>> atOnce;
    const auto runStart = std::chrono::steady_clock::now();

    for (const MCTest::TestCase& test : selected)
    {
        if (test.Isolated && !only && jobs > 1)
        {
            atOnce.emplace_back(test);
            continue;
        }

        if (test.Isolated && !only)
        {
            std::cout << "[ PROC ] " << test.Name << std::endl;

            if (!RunIsolated(arguments[0], test))
            {
                ++state.Failures;
                failed.emplace_back(test);
            }

            continue;
        }

        std::cout << "[ RUN  ] " << test.Name << std::endl;
        const size_t failuresBefore = state.Failures;
        const auto start = std::chrono::steady_clock::now();
        state.Current = test.Name;

        try
        {
            test.Body();
        }
        catch (const MCTest::RequireFailed&)
        {
        }
        catch (const std::exception& e)
        {
            MCTest::ReportFailure(test.File, test.Line, std::string("uncaught exception: ") + e.what());
        }
        catch (...)
        {
            MCTest::ReportFailure(test.File, test.Line, "uncaught exception");
        }

        state.Scopes.clear();
        state.Current = {};
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        const bool passed = state.Failures == failuresBefore;

        if (!passed)
        {
            failed.emplace_back(test);
        }

        std::cout << (passed ? "[   OK ] " : "[ FAIL ] ") << test.Name << " (" << ms << " ms)" << std::endl;
    }

    if (!atOnce.empty())
    {
        auto isolatedFailed = RunIsolatedAtOnce(arguments[0], atOnce, jobs);

        if (!isolatedFailed)
        {
            std::cerr << isolatedFailed.error() << "\n";
            return 1;
        }

        state.Failures += isolatedFailed->size();
        failed.insert(failed.end(), isolatedFailed->begin(), isolatedFailed->end());
    }

    const auto totalMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - runStart).count();
    std::cout << "\n"
              << selected.size() - failed.size() << " of " << selected.size() << " tests passed, " << state.Passes
              << " checks passed, " << state.Failures << " failed (" << totalMs << " ms)\n";

    for (const MCTest::TestCase& test : failed)
    {
        std::cout << "  FAILED: " << test.Name << "\n";
    }

    return failed.empty() ? 0 : 1;
}

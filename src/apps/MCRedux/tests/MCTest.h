#pragma once

#include <concepts>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

/// <summary>
/// The port's own small test framework: self-registering test cases, checks that report and carry on, requirements
/// that end the test, and a runner (<c>MCTest.cpp</c>) with a name filter and an exit code a build can act on.
/// </summary>
/// <remarks>
/// <para>Standard C++ only, so it builds wherever the game does. A test is</para>
/// <code>
/// TEST_CASE("tim: a 4bpp image decodes")
/// {
///     REQUIRE(tim);
///     CHECK_EQ(tim->Bpp, 4);
/// }
/// </code>
/// <para>A test that checks every entry of a table opens a <see cref="MCTest::Scope"/> per entry, so a failure names the
/// file or entry it was about.</para>
/// </remarks>
namespace MCTest
{
    /// <summary>One registered test.</summary>
    struct TestCase
    {
        /// <summary>The name given to <c>TEST_CASE</c>; the runner's filter matches against it.</summary>
        std::string_view Name;
        /// <summary>The test's body.</summary>
        std::function<void()> Body;
        /// <summary>The source file it is declared in.</summary>
        std::string_view File;
        /// <summary>The line it is declared on.</summary>
        int Line;
        /// <summary>
        /// Runs in a process of its own (<c>TEST_CASE_ISOLATED</c>): the runner starts the test program again with
        /// <c>--only</c> and the test's name, so state other tests leave behind can't reach it, nor its theirs.
        /// </summary>
        bool Isolated;
    };

    /// <summary>Every test case, in registration order.</summary>
    std::vector<TestCase>& Registry();

    /// <summary>
    /// The value of the runner's option <c>--</c><paramref name="name"/> <c>&lt;value&gt;</c> (for example
    /// <c>--game E:\mc2\MCX Original</c>), or none when it wasn't given. Isolated tests get the same options.
    /// </summary>
    std::optional<std::string_view> Option(std::string_view name);

    /// <summary>The name of the test running in this process, or an empty string between tests.</summary>
    std::string_view CurrentTestName();

    /// <summary>Adds a test to <see cref="Registry"/> from a static initialiser; <c>TEST_CASE</c> declares one.</summary>
    struct Registrar
    {
        Registrar(std::string_view name, std::function<void()> body, std::string_view file, int line,
                  bool isolated = false);
    };

    /// <summary>Thrown by a failed <c>REQUIRE</c> to end the current test; the runner catches it.</summary>
    struct RequireFailed
    {
    };

    /// <summary>
    /// A line of context printed with every failure while it is alive, innermost last, e.g. the unit a loop is
    /// checking.
    /// </summary>
    class Scope
    {
    public:
        explicit Scope(std::string_view text);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };

    /// <summary>
    /// Expects the asserts whose report holds a text while it is alive: each one is counted instead of failing the
    /// test, and when it ends the count must be the one given. Outside one, a failed <c>Assert</c> fails the test (the
    /// runner sets the game's assert hook to <see cref="ReportAssert"/>).
    /// </summary>
    /// <remarks>
    /// <code>
    /// {
    ///     MCTest::ExpectAssert expected("bad targetId", 1);
    ///     client.HandleMessage(packet);
    /// }
    /// </code>
    /// </remarks>
    class ExpectAssert
    {
    public:
        /// <param name="text">What the report holds (ignoring case), e.g. the assert's message.</param>
        /// <param name="count">How many such asserts must fail while it is alive.</param>
        /// <param name="where">Where it was opened, for the failure; callers leave it out.</param>
        ExpectAssert(std::string_view text, int32_t count,
                     const std::source_location& where = std::source_location::current());
        ~ExpectAssert();
        ExpectAssert(const ExpectAssert&) = delete;
        ExpectAssert& operator=(const ExpectAssert&) = delete;

        /// <summary>How many asserts it has taken so far.</summary>
        int32_t Seen() const { return _Seen; }

    private:
        friend void ReportAssert(std::string_view report, std::string_view file, int line);

        /// <summary>The text an expected report holds, in lower case.</summary>
        std::string _Text;
        /// <summary>How many must fail.</summary>
        int32_t _Count;
        /// <summary>How many have.</summary>
        int32_t _Seen = 0;
        /// <summary>Where it was opened.</summary>
        std::source_location _Where;
    };

    /// <summary>
    /// A failed <c>Assert</c> at <paramref name="file"/>(<paramref name="line"/>): counted by the innermost open
    /// <see cref="ExpectAssert"/> whose text the report holds, else a failure of the current test.
    /// </summary>
    void ReportAssert(std::string_view report, std::string_view file, int line);

    /// <summary>Records a failure against the current test, with the open scopes.</summary>
    /// <param name="file">Source file of the check.</param>
    /// <param name="line">Line of the check.</param>
    /// <param name="message">What failed.</param>
    void ReportFailure(std::string_view file, int line, std::string_view message);

    /// <summary>Notes one passed check, for the runner's totals.</summary>
    void ReportPass();

    /// <summary>A value as text for a failure message: <c>std::format</c>'s form when it has one.</summary>
    template <typename T> std::string Describe(const T& value)
    {
        if constexpr (std::same_as<T, uint8_t> || std::same_as<T, int8_t>)
        {
            return std::format("{}", static_cast<int>(value));
        }
        else if constexpr (std::formattable<T, char>)
        {
            return std::format("{}", value);
        }
        else
        {
            return "<value>";
        }
    }

    /// <summary><c>CHECK</c> and <c>REQUIRE</c>.</summary>
    /// <returns>Whether the condition held.</returns>
    inline bool Check(bool ok, std::string_view file, int line, std::string_view expression)
    {
        if (ok)
        {
            ReportPass();
        }
        else
        {
            ReportFailure(file, line, std::format("CHECK({}) failed", expression));
        }

        return ok;
    }

    /// <summary><c>CHECK_EQ</c> and <c>REQUIRE_EQ</c>: reports both values when they differ.</summary>
    /// <returns>Whether the values were equal.</returns>
    template <typename A, typename B>
    bool CheckEqual(const A& actual, const B& expected, std::string_view file, int line, std::string_view actualText,
                    std::string_view expectedText)
    {
        const bool ok = actual == expected;

        if (ok)
        {
            ReportPass();
        }
        else
        {
            ReportFailure(file, line,
                          std::format("CHECK_EQ({}, {}) failed: {} != {}", actualText, expectedText, Describe(actual),
                                      Describe(expected)));
        }

        return ok;
    }
}

#define OBTEST_CONCAT_INNER(a, b) a##b
#define OBTEST_CONCAT(a, b)       OBTEST_CONCAT_INNER(a, b)
#define OBTEST_CASE_IMPL(name, body)                                                                 \
    static void body();                                                                              \
    static const MCTest::Registrar OBTEST_CONCAT(body, _Registrar)(name, &body, __FILE__, __LINE__); \
    static void body()

#define OBTEST_CASE_ISOLATED_IMPL(name, body)                                                              \
    static void body();                                                                                    \
    static const MCTest::Registrar OBTEST_CONCAT(body, _Registrar)(name, &body, __FILE__, __LINE__, true); \
    static void body()

/// <summary>Declares and registers a test; the braces that follow are its body.</summary>
#define TEST_CASE(name) OBTEST_CASE_IMPL(name, OBTEST_CONCAT(MCTest_Case_, __COUNTER__))

/// <summary>
/// Declares a test that runs in a process of its own (one that brings up the whole game, say); the braces that follow
/// are its body.
/// </summary>
#define TEST_CASE_ISOLATED(name) OBTEST_CASE_ISOLATED_IMPL(name, OBTEST_CONCAT(MCTest_Case_, __COUNTER__))

/// <summary>Records a failure if <paramref name="expr"/> is false and carries on; evaluates to whether it held.</summary>
#define CHECK(expr) MCTest::Check(static_cast<bool>(expr), __FILE__, __LINE__, #expr)

/// <summary>Records a failure and ends the test if <paramref name="expr"/> is false.</summary>
#define REQUIRE(expr)                      \
    do                                     \
    {                                      \
        if (!CHECK(expr))                  \
            throw MCTest::RequireFailed{}; \
    } while (false)

/// <summary>Records a failure showing both values if they differ, and carries on.</summary>
#define CHECK_EQ(actual, expected) MCTest::CheckEqual((actual), (expected), __FILE__, __LINE__, #actual, #expected)

/// <summary>Records a failure showing both values and ends the test if they differ.</summary>
#define REQUIRE_EQ(actual, expected)       \
    do                                     \
    {                                      \
        if (!CHECK_EQ(actual, expected))   \
            throw MCTest::RequireFailed{}; \
    } while (false)

/// <summary>Records a failure with a message and carries on.</summary>
#define FAIL_CHECK(message) MCTest::ReportFailure(__FILE__, __LINE__, (message))

/// <summary>Records a failure with a message and ends the test.</summary>
#define FAIL(message)                                         \
    do                                                        \
    {                                                         \
        MCTest::ReportFailure(__FILE__, __LINE__, (message)); \
        throw MCTest::RequireFailed{};                        \
    } while (false)

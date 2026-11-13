#include "stdafx.h"
#include "MCTest.h"
#include "MCScratchFolder.h"
#include "platform/MCFrameLog.h"
#include "platform/MCNeverDestroyed.h"
#include "platform/MCRenderer.h"

TEST_CASE("port: case-insensitive compare")
{
    CHECK_EQ(MCPort::StrICmp("Data\\Art", "DATA\\art"), 0);
    CHECK(MCPort::StrICmp("abc", "abd") < 0);
    CHECK_EQ(MCPort::StrNICmp("MechCmdr1.fit", "MECHCMDR2.FIT", 8), 0);
}

/// <summary>
/// The compares and the lower-casing stop at a string's first null, as the CRT's did on a buffer: what follows it
/// doesn't count.
/// </summary>
TEST_CASE("port: string helpers end a string at its first null")
{
    CHECK_EQ(MCPort::StrICmp(std::string_view("abc\0xyz", 7), "ABC"), 0);
    CHECK(MCPort::StrICmp("ab", "abc") < 0);
    CHECK_EQ(MCPort::StrNICmp(std::string_view("ab\0x", 4), std::string_view("AB\0y", 4), 4), 0);
    std::string text("MiXeD\0KeEp", 10);
    MCPort::StrLwr(text);
    CHECK_EQ(text, std::string("mixed\0KeEp", 10));
}

TEST_CASE("port: IToA prints as _itoa: signed in base 10, the 32 bits unsigned in other bases")
{
    CHECK_EQ(MCPort::IToA(0, 10), std::string("0"));
    CHECK_EQ(MCPort::IToA(-5, 10), std::string("-5"));
    CHECK_EQ(MCPort::IToA(INT32_MIN, 10), std::string("-2147483648"));
    CHECK_EQ(MCPort::IToA(-1, 16), std::string("ffffffff"));
    CHECK_EQ(MCPort::IToA(255, 2), std::string("11111111"));
    CHECK_EQ(MCPort::IToA(INT32_MIN, 2), "1" + std::string(31, '0'));
    CHECK_EQ(MCPort::IToA(35, 36), std::string("z"));
}

TEST_CASE("port: a never-destroyed value is made from its arguments and never runs its destructor")
{
    struct Counted
    {
        explicit Counted(int& destroyed, int value) : Destroyed(destroyed), Value(value) {}
        ~Counted() { ++Destroyed; }

        int& Destroyed;
        int Value;
    };

    int destroyed = 0;

    {
        MCNeverDestroyed<Counted> held(destroyed, 7);
        CHECK_EQ((*held).Value, 7);
        (*held).Value = 8;
        CHECK_EQ((*held).Value, 8);
    }

    CHECK_EQ(destroyed, 0);
}

TEST_CASE("port: destroying a texture moves the last one into its slot and empties the caller's reference")
{
    const size_t before = MCRenderer::Textures().size();
    uint8_t pixels[4]{};
    std::weak_ptr<MCTexture> first = MCRenderer::CreateTexture(pixels, 2, 2, MCTextureUse::Static);
    std::weak_ptr<MCTexture> second = MCRenderer::CreateTexture(pixels, 2, 2, MCTextureUse::Static);
    std::weak_ptr<MCTexture> third = MCRenderer::CreateTexture(pixels, 2, 2, MCTextureUse::Static);
    REQUIRE_EQ(MCRenderer::Textures().size(), before + 3);

    MCRenderer::DestroyTexture(first);
    CHECK(first.expired());
    REQUIRE_EQ(MCRenderer::Textures().size(), before + 2);
    CHECK_EQ(third.lock()->Slot, before);
    CHECK_EQ(second.lock()->Slot, before + 1);

    for (size_t i = 0; i < MCRenderer::Textures().size(); ++i)
    {
        MCTest::Scope scope(std::format("slot {}", i));
        CHECK_EQ(MCRenderer::Textures()[i]->Slot, i);
    }

    MCRenderer::DestroyTexture(third);
    MCRenderer::DestroyTexture(second);
    CHECK_EQ(MCRenderer::Textures().size(), before);
}

namespace
{
    /// <summary>
    /// A frame log on a clock of the test's own, in a scratch folder: frames of exact lengths, closed and the system's
    /// clock back as it ends.
    /// </summary>
    class FrameLogFixture
    {
    public:
        explicit FrameLogFixture(std::string_view name) : _Folder("mc_port_tests", name)
        {
            MCFrameLog::SetClockForTests([this] { return _Ticks; });
            Opened = MCFrameLog::Open(Path());
        }

        ~FrameLogFixture()
        {
            MCFrameLog::Close();
            MCFrameLog::SetClockForTests(nullptr);
        }

        FrameLogFixture(const FrameLogFixture&) = delete;
        FrameLogFixture& operator=(const FrameLogFixture&) = delete;

        std::filesystem::path Path() const { return _Folder.Path() / "frames.txt"; }

        /// <summary>Moves the clock on by <paramref name="ms"/>.</summary>
        void Advance(double ms)
        {
            _Ticks +=
                static_cast<uint64_t>(std::llround(ms * static_cast<double>(SDL_GetPerformanceFrequency()) / 1000.0));
        }

        /// <summary>A frame of <paramref name="ms"/> on a screen of <paramref name="refreshHz"/>.</summary>
        void Frame(double ms, double refreshHz)
        {
            Advance(ms);
            MCFrameLog::NextFrame(refreshHz);
        }

        /// <summary>The log as written so far.</summary>
        std::string Text() const
        {
            std::ifstream file(Path());
            return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        }

        bool Opened = false;

    private:
        MCScratchFolder _Folder;
        /// <summary>Not 0, as a real clock never is at the start.</summary>
        uint64_t _Ticks = 1;
    };

    constexpr double Refresh240Hz = 240.0;
    constexpr double Frame240HzMs = 4.0;
}

TEST_CASE("frame log: a frame late by a refresh of a 240 Hz screen is slow, one late by less isn't")
{
    FrameLogFixture log("frame-log-refresh");
    REQUIRE(log.Opened);
    log.Frame(0.0, Refresh240Hz);

    for (int32_t i = 0; i < 40; i++)
    {
        log.Frame(Frame240HzMs, Refresh240Hz);
    }

    // Late by a refresh (4.17 ms): shown for two, where the old 6 ms margin let it go by.
    log.Frame(8.5, Refresh240Hz);
    // Late by less than most of a refresh: 1.5 times the usual, but on time for the next refresh but one.
    log.Frame(7.0, Refresh240Hz);
    // Without the screen's rate, the 6 ms margin as before.
    log.Frame(8.5, 0.0);
    log.Frame(10.5, 0.0);
    const std::string text = log.Text();
    CHECK(text.contains("frame 41 8.50 ms (usual 4.00) SLOW"));
    CHECK(!text.contains("frame 42 "));
    CHECK(!text.contains("frame 43 "));
    CHECK(text.contains("frame 44 10.50 ms (usual 4.00) SLOW"));
}

TEST_CASE("frame log: a line a second gives a slowdown that lasts with each part's time and each count a frame")
{
    FrameLogFixture log("frame-log-second");
    REQUIRE(log.Opened);
    log.Frame(0.0, Refresh240Hz);

    // A second of 20 ms frames, each 15 ms of logic and two HD frames read, then a note in one of them.
    for (int32_t i = 0; i < 50; i++)
    {
        {
            MCFrameLog::Scope part("logic");
            log.Advance(15.0);
        }

        MCFrameLog::Count("HD frames read", 2);

        if (i == 10)
        {
            MCFrameLog::Note("logistics: the purchase screen");
        }

        log.Frame(5.0, Refresh240Hz);
    }

    const std::string text = log.Text();
    CHECK(
        text.contains("-- 1.0 s: 50 frames, median 20.00 ms, p99 20.00, max 20.00; 50 over 1.5 refreshes at 240 Hz\n"));
    CHECK(text.contains("   a frame: logic 15.00\n"));
    CHECK(text.contains("   counts: HD frames read 100\n"));
    // A frame with a note is written whatever its time, with its parts and counts.
    CHECK(
        text.contains("frame 11 20.00 ms (usual 20.00)\n    logic                     15.00\n    + HD frames read: 2\n"
                      "    * logistics: the purchase screen\n"));
}

TEST_CASE("frame log: what is noted before the first frame is the first frame's, what is timed then no frame's")
{
    FrameLogFixture log("frame-log-first");
    REQUIRE(log.Opened);
    MCFrameLog::Note("display: 2560x1440 pixels at 239.97 Hz");

    {
        MCFrameLog::Scope part("before");
        log.Advance(1.0);
    }

    log.Frame(0.0, Refresh240Hz);
    log.Frame(Frame240HzMs, Refresh240Hz);
    const std::string text = log.Text();
    CHECK(text.contains("frame 1 4.00 ms (usual 0.00)\n    * display: 2560x1440 pixels at 239.97 Hz\n"));
    CHECK(!text.contains("before"));
}

TEST_CASE("frame log: a part begun in a log since opened again adds to neither")
{
    FrameLogFixture log("frame-log-stale");
    REQUIRE(log.Opened);
    log.Frame(0.0, 0.0);

    {
        MCFrameLog::Scope part("stale");
        REQUIRE(MCFrameLog::Open(log.Path()));
        log.Frame(0.0, 0.0);
    }

    MCFrameLog::Note("noted");
    log.Frame(Frame240HzMs, 0.0);
    const std::string text = log.Text();
    CHECK(text.contains("* noted"));
    CHECK(!text.contains("stale"));
}

TEST_CASE("frame log: opened again, it starts a new log; closed, it times nothing")
{
    FrameLogFixture log("frame-log-reopen");
    REQUIRE(log.Opened);
    log.Frame(0.0, 0.0);
    MCFrameLog::Note("in the first log");
    log.Frame(4.0, 0.0);
    REQUIRE(log.Text().contains("in the first log"));
    REQUIRE(MCFrameLog::Open(log.Path()));
    CHECK(!log.Text().contains("in the first log"));
    MCFrameLog::Close();
    CHECK(!MCFrameLog::Enabled());
    MCFrameLog::Note("closed");
    log.Frame(4.0, 0.0);
    CHECK(!log.Text().contains("closed"));
}

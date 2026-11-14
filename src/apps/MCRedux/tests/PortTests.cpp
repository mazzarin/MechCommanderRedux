#include "stdafx.h"
#include "MCTest.h"
#include "MCScratchFolder.h"
#include "main/MCBenchmark.h"
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

TEST_CASE("frame log: the screen's frames are timed between presents, steady where the game's loop runs unevenly")
{
    // As on the A10 with Vulkan's third image (13 Nov): the loop's frames 4 ms then 12.67 (long at 120 Hz), the screen
    // given a frame every 8.33 ms all the while; then one frame shown 25 ms after the last.
    constexpr double Refresh120Hz = 120.0;
    constexpr double QuickPresentMs = 3.5;
    constexpr double QuickRestMs = 0.5;
    constexpr double WaitingPresentMs = 7.8333333;
    constexpr double WaitingRestMs = 4.8333333;
    constexpr double LatePresentMs = 20.1666667;
    constexpr int32_t WarmPairs = 40;
    constexpr int32_t TalliedPairs = 50;
    FrameLogFixture log("frame-log-shown");
    REQUIRE(log.Opened);
    log.Frame(0.0, Refresh120Hz);

    const auto pair = [&log]
    {
        log.Advance(QuickPresentMs);
        MCFrameLog::Presented();
        log.Frame(QuickRestMs, Refresh120Hz);
        log.Advance(WaitingPresentMs);
        MCFrameLog::Presented();
        log.Frame(WaitingRestMs, Refresh120Hz);
    };

    for (int32_t i = 0; i < WarmPairs; i++)
    {
        pair();
    }

    MCFrameLog::StartTally();

    for (int32_t i = 0; i < TalliedPairs; i++)
    {
        pair();
    }

    log.Advance(LatePresentMs);
    MCFrameLog::Presented();
    log.Frame(QuickRestMs, Refresh120Hz);
    const MCFrameLog::MCTally tally = MCFrameLog::TakeTally();

    // The loop: half its frames long. The screen: one frame late, by a refresh and more.
    REQUIRE_EQ(tally.Loop.Times.size(), size_t{101});
    CHECK_EQ(tally.Loop.Long, size_t{51});
    REQUIRE(tally.TimedShown());
    REQUIRE_EQ(tally.Shown.Times.size(), size_t{101});
    CHECK_EQ(tally.Shown.Long, size_t{1});
    CHECK_EQ(tally.Shown.Slow, size_t{1});
    CHECK_EQ(&tally.Judged(), &tally.Shown);
    CHECK_EQ(std::format("{:.2f} {:.2f} {:.2f}", tally.Shown.MedianMs(), tally.Shown.P99Ms(), tally.Shown.WorstMs()),
             "8.33 8.33 25.00");

    const MCBenchmark::MCRunInfo run{
        .Build = "0.6.8 (983)", .Variant = "clear-day-hd", .Display = "a screen", .Machine = "a computer"};
    CHECK(MCBenchmark::Summary(run, tally, MCFrameLog::MCTally{})
              .contains("a computer, frames as shown: p99 8.33 ms, worst 25.00 ms, 1 frames late by a refresh, 1 over "
                        "1.5 refreshes; median 8.33 ms (120.0 fps); 101 frames in 0.9 s;"));

    // Each second's line says the screen's frames too.
    const std::string text = log.Text();
    CHECK(text.contains("; shown: "));
    CHECK(text.contains("p99 8.33, max 8.33, 0 over 1.5 refreshes\n"));
}

TEST_CASE("frame log: with vsync the screen shows a frame a refresh after the last at the soonest, so uneven "
          "hand-overs stay even")
{
    // As NVIDIA's Vulkan driver on Windows takes the frames (13 Nov, the A10): the game hands one over at the end of each
    // loop's frame, 4 ms then 12.67 alternately, and the screen shows one every 8.33 ms; then a 30 ms frame.
    constexpr double Refresh120Hz = 120.0;
    constexpr double QuickMs = 4.0;
    constexpr double WaitingMs = 12.6666667;
    constexpr double LateMs = 30.0;
    constexpr int32_t WarmPairs = 40;
    constexpr int32_t TalliedPairs = 50;
    constexpr bool VSync = true;
    FrameLogFixture log("frame-log-vsync");
    REQUIRE(log.Opened);
    MCFrameLog::NextFrame(Refresh120Hz, VSync);

    const auto frame = [&log](double ms)
    {
        log.Advance(ms);
        MCFrameLog::Presented();
        MCFrameLog::NextFrame(Refresh120Hz, VSync);
    };

    for (int32_t i = 0; i < WarmPairs; i++)
    {
        frame(QuickMs);
        frame(WaitingMs);
    }

    MCFrameLog::StartTally();

    for (int32_t i = 0; i < TalliedPairs; i++)
    {
        frame(QuickMs);
        frame(WaitingMs);
    }

    frame(LateMs);
    const MCFrameLog::MCTally tally = MCFrameLog::TakeTally();

    // The loop's and the hand-overs' frames half long; the screen's one, the late frame: every other hand-over came
    // as its refresh did, so no frame waited in the queue to cover it, and the last showed for its 30 ms.
    CHECK_EQ(tally.Loop.Long, size_t{51});
    REQUIRE_EQ(tally.Shown.Times.size(), size_t{101});
    CHECK_EQ(tally.Shown.Long, size_t{1});
    CHECK_EQ(std::format("{:.2f} {:.2f}", tally.Shown.MedianMs(), tally.Shown.WorstMs()), "8.33 30.00");

    // Without vsync the screen takes each as it's handed over.
    MCFrameLog::StartTally();
    log.Advance(QuickMs);
    MCFrameLog::Presented();
    MCFrameLog::NextFrame(Refresh120Hz, false);
    log.Advance(WaitingMs);
    MCFrameLog::Presented();
    MCFrameLog::NextFrame(Refresh120Hz, false);
    const MCFrameLog::MCTally unsynced = MCFrameLog::TakeTally();
    REQUIRE_EQ(unsynced.Shown.Times.size(), size_t{2});
    CHECK_EQ(std::format("{:.2f}", unsynced.Shown.Times[1]), "12.67");
}

TEST_CASE("benchmark: the summary heads with the hitches, the battle's and the pan's frames together, then each's own")
{
    FrameLogFixture log("benchmark-summary");
    REQUIRE(log.Opened);
    log.Frame(0.0, Refresh240Hz);

    // What is usual, before the benchmark's frames.
    for (int32_t i = 0; i < 40; i++)
    {
        log.Frame(Frame240HzMs, Refresh240Hz);
    }

    // The battle: 98 frames of 4 ms, each first using two HD frames read in 1 ms, then one late by a refresh and two
    // (12 ms) and one held for seven (30 ms).
    MCFrameLog::StartTally();

    for (int32_t i = 0; i < 98; i++)
    {
        {
            MCFrameLog::Scope part("hd.read");
            log.Advance(1.0);
        }

        MCFrameLog::Count("HD frames first used", 2);
        log.Frame(Frame240HzMs - 1.0, Refresh240Hz);
    }

    log.Frame(12.0, Refresh240Hz);
    log.Frame(30.0, Refresh240Hz);
    const MCFrameLog::MCTally battle = MCFrameLog::TakeTally();

    // The pan: 50 frames of 4.5 ms, none late.
    MCFrameLog::StartTally();

    for (int32_t i = 0; i < 50; i++)
    {
        log.Frame(4.5, Refresh240Hz);
    }

    const MCFrameLog::MCTally pan = MCFrameLog::TakeTally();
    CHECK(MCFrameLog::TakeTally().Loop.Times.empty());
    REQUIRE_EQ(battle.Loop.Times.size(), size_t{100});
    REQUIRE_EQ(pan.Loop.Times.size(), size_t{50});
    CHECK_EQ(battle.Loop.Slow, size_t{2});
    CHECK_EQ(battle.Loop.Long, size_t{2});
    // No presenter said (MCFrameLog::Presented): the loop's frames are judged.
    CHECK(!battle.TimedShown());
    CHECK_EQ(battle.RefreshHz, Refresh240Hz);

    const MCBenchmark::MCRunInfo run{.Build = "0.6.8 (976), commit 8c9bf531",
                                     .Variant = "rain-night-hd",
                                     .Display = "3024x1890 pixels at 240.00 Hz, full screen, vsync on, gpu",
                                     .Machine = "Apple M4 Pro (10P+4E cores, 20 GPU cores), 48 GB, Mac16,8, mac 26.5"};
    const std::string summary = MCBenchmark::Summary(run, battle, pan);
    // The whole: 150 frames, the 99th percentile's the 149th (12 ms), the median 4 ms; 0.434 s and 0.225 s.
    CHECK_EQ(
        summary,
        "MCRedux benchmark 0.6.8 (976), commit 8c9bf531, canyon rain-night-hd, 3024x1890 pixels at 240.00 Hz, full "
        "screen, vsync on, gpu, Apple M4 Pro (10P+4E cores, 20 GPU cores), 48 GB, Mac16,8, mac 26.5, frames as the "
        "game's loop ran (none shown): p99 12.00 ms, "
        "worst 30.00 ms, 2 frames late by a refresh, 2 over 1.5 refreshes; median 4.00 ms (250.0 fps); 150 frames "
        "in 0.7 s; 196 HD frames first used, read in 98.0 ms. Battle: p99 30.00 ms, worst 30.00 ms, 2 frames late "
        "by a refresh, 2 over 1.5 refreshes; median 4.00 ms (250.0 fps); 100 frames in 0.4 s; 196 HD frames first "
        "used, read in 98.0 ms. Pan: p99 4.50 ms, worst 4.50 ms, 0 frames late by a refresh, 0 over 1.5 refreshes; "
        "median 4.50 ms (222.2 fps); 50 frames in 0.2 s; 0 HD frames first used, read in 0.0 ms");

    // The summary ends the log, a line of its own.
    MCFrameLog::Line(summary);
    CHECK(log.Text().ends_with(summary + "\n"));
}

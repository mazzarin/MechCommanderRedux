#include "stdafx.h"
#include "MCFakeUpdaterBackend.h"
#include "MCTest.h"
#include "logistics/MCUpdateModel.h"
#include "main/MCUpdater.h"
#include "MCVersion.h"

#include <algorithm>
#include <format>
#include <string>

// The updater (main/MCUpdater.h), with a stand-in for the Mac app's Sparkle: the check made once a start, each step
// reported in turn, the player's answers passed on, the feed's text kept to what the fonts write; the version line's
// words, and a download's size, speed and time left (the remade main menu's UPDATE and its page: UpdateScreenTests.cpp).

namespace
{
    using MCTestUpdater::MCFakeUpdater;
    using MCUpdater::MCAnswer;
    using MCUpdater::MCStage;

    constexpr std::string_view NewVersion = "0.2.2 (480)";
    constexpr std::string_view Notes = "Night missions: dusk and night lighting.\n\nThe main menu updates the game.";
}

TEST_CASE("updater: the check is made once a start, and an update waits through each step for the player's answer")
{
    CHECK(!MCUpdater::Available());
    MCUpdater::CheckAtStart();
    MCFakeUpdater updater;
    REQUIRE(MCUpdater::Available());
    CHECK_EQ(MCUpdater::RunningVersion(), std::string("0.2.1 (468)"));
    MCUpdater::CheckAtStart();
    MCUpdater::CheckAtStart();
    CHECK_EQ(updater.Log->Checks.size(), size_t{1});
    CHECK(!MCUpdater::Waiting());

    int32_t changes = 0;
    MCUpdater::SetListener([&] { changes++; });

    // Skipped: nothing waits.
    MCUpdater::Found(std::string(NewVersion), std::string(Notes));
    CHECK(MCUpdater::Status().Stage == MCStage::Found);
    CHECK_EQ(MCUpdater::Status().Version, std::string(NewVersion));
    CHECK(MCUpdater::Waiting());
    MCUpdater::Answer(MCAnswer::Skip);
    CHECK(MCUpdater::Status().Stage == MCStage::None);
    CHECK(!MCUpdater::Waiting());

    // Installed: the download, then ready, then the restart.
    MCUpdater::Found(std::string(NewVersion), std::string(Notes));
    MCUpdater::Answer(MCAnswer::Install);
    CHECK(MCUpdater::Status().Stage == MCStage::Downloading);
    MCUpdater::Downloading(512, 1024);
    CHECK_EQ(MCUpdater::Status().Received, uint64_t{512});
    CHECK_EQ(MCUpdater::Status().Expected, uint64_t{1024});
    MCUpdater::Ready();
    CHECK(MCUpdater::Status().Stage == MCStage::Ready);
    MCUpdater::Closed();
    CHECK(MCUpdater::Status().Stage == MCStage::Ready);
    MCUpdater::Answer(MCAnswer::Install);
    CHECK(MCUpdater::Status().Stage == MCStage::Installing);
    MCUpdater::Closed();
    CHECK(MCUpdater::Status().Stage == MCStage::Installing);
    REQUIRE_EQ(updater.Log->Answers.size(), size_t{3});
    CHECK(updater.Log->Answers[0] == MCAnswer::Skip);
    CHECK(updater.Log->Answers[1] == MCAnswer::Install);
    CHECK(updater.Log->Answers[2] == MCAnswer::Install);

    // An answer with no step asking for it isn't passed on.
    MCUpdater::Answer(MCAnswer::Skip);
    CHECK_EQ(updater.Log->Answers.size(), size_t{3});

    // The session ending takes away an update not downloaded. The check failing is no news; a step failing after one
    // was found stays shown until the next start.
    MCUpdater::Found(std::string(NewVersion), std::string(Notes));
    MCUpdater::Closed();
    CHECK(MCUpdater::Status().Stage == MCStage::None);
    MCUpdater::Failed("offline");
    CHECK(!MCUpdater::Waiting());
    MCUpdater::Found(std::string(NewVersion), std::string(Notes));
    MCUpdater::Answer(MCAnswer::Install);
    MCUpdater::Failed("The update is improperly signed.");
    CHECK(MCUpdater::Status().Stage == MCStage::Failed);
    CHECK(MCUpdater::Status().Retry);
    MCUpdater::Closed();
    CHECK(MCUpdater::Status().Stage == MCStage::Failed);
    CHECK(changes > 0);

    // INSTALL tries again: a fresh check, and what it finds downloads a frame later, with no second INSTALL.
    const size_t answered = updater.Log->Answers.size();
    MCUpdater::Answer(MCAnswer::Install);
    CHECK_EQ(updater.Log->Checks.size(), size_t{2});
    CHECK(MCUpdater::Status().Stage == MCStage::Downloading);
    MCUpdater::Found(std::string(NewVersion), std::string(Notes));
    CHECK(MCUpdater::Status().Stage == MCStage::Downloading);
    CHECK_EQ(updater.Log->Answers.size(), answered);
    MCUpdater::Frame();
    REQUIRE_EQ(updater.Log->Answers.size(), answered + 1);
    CHECK(updater.Log->Answers.back() == MCAnswer::Install);
    MCUpdater::Ready();
    CHECK(MCUpdater::Status().Stage == MCStage::Ready);

    // Again, and nothing is found this time: nothing is shown.
    MCUpdater::Failed("The update is improperly signed.");
    MCUpdater::Answer(MCAnswer::Install);
    CHECK(MCUpdater::Status().Stage == MCStage::Downloading);
    MCUpdater::Closed();
    CHECK(MCUpdater::Status().Stage == MCStage::None);
    MCUpdater::Frame();
    CHECK_EQ(updater.Log->Answers.size(), answered + 1);
    MCUpdater::SetListener(nullptr);
}

TEST_CASE("updater: the feed's version and notes keep only what the fonts write, and stay small")
{
    MCFakeUpdater updater;
    constexpr size_t longNotes = 10000;
    std::string notes = "Caf\xC3\xA9\tnight\r\nline two\x01";
    notes.append(longNotes, 'x');
    MCUpdater::Found("0.2.2\n (480)\x7f", notes);
    CHECK_EQ(MCUpdater::Status().Version, std::string("0.2.2 (480)"));
    const std::string& kept = MCUpdater::Status().Notes;
    CHECK(kept.starts_with("Caf night\nline two"));
    CHECK(std::ranges::all_of(kept, [](char c) { return c == '\n' || (c >= ' ' && c <= '~'); }));
    CHECK(kept.size() <= 8192);
    MCUpdater::ReleaseNotes("Linked notes\n\x02");
    CHECK_EQ(MCUpdater::Status().Notes, std::string("Linked notes\n"));
    MCUpdater::Answer(MCAnswer::Skip);
    MCUpdater::ReleaseNotes("Late notes");
    CHECK(MCUpdater::Status().Notes.empty());
}

TEST_CASE("updater: the main menu's version line names Redux HD and the release running, without its build")
{
    CHECK_EQ(VersionLineText("0.6.3 (899)"), std::string("Redux HD 0.6.3"));
    CHECK_EQ(VersionLineText("0.6.3"), std::string("Redux HD 0.6.3"));
    CHECK(VersionLineText("").empty());
}

TEST_CASE("updater: a size is shown as the setup gives it: decimal GB to a tenth from 1 GB, MB below")
{
    CHECK_EQ(MCUpdateWork::SizeText(5'500'000'000), std::string("5.5 GB"));
    CHECK_EQ(MCUpdateWork::SizeText(1'000'000'000), std::string("1.0 GB"));
    CHECK_EQ(MCUpdateWork::SizeText(999'960'000), std::string("1.0 GB"));
    CHECK_EQ(MCUpdateWork::SizeText(999'940'000), std::string("999.9 MB"));
    CHECK_EQ(MCUpdateWork::SizeText(350'200'000), std::string("350.2 MB"));
    CHECK_EQ(MCUpdateWork::SizeText(0), std::string("0.0 MB"));
    CHECK_EQ(MCUpdateWork::ProgressText(1'200'000'000, 5'500'000'000), std::string("1.2 OF 5.5 GB"));
    CHECK_EQ(MCUpdateWork::ProgressText(300'000'000, 5'500'000'000), std::string("0.3 OF 5.5 GB"));
    CHECK_EQ(MCUpdateWork::ProgressText(1'500'000, 7'000'000), std::string("1.5 OF 7.0 MB"));

    // The main menu foot's count: GB to a tenth, MB whole from 10 MB, to a tenth below; the whole not known, what's in.
    CHECK_EQ(MCUpdateWork::CountText(3'100'000'000, 7'400'000'000), std::string("3.1 of 7.4 GB"));
    CHECK_EQ(MCUpdateWork::CountText(120'400'000, 340'200'000), std::string("120 of 340 MB"));
    CHECK_EQ(MCUpdateWork::CountText(400'000, 2'500'000), std::string("0.4 of 2.5 MB"));
    CHECK_EQ(MCUpdateWork::CountText(120'400'000, 0), std::string("120 MB"));
    CHECK_EQ(MCUpdateWork::CountText(1'500'000'000, 0), std::string("1.5 GB"));
}

TEST_CASE("update work: a download's speed and time left read as the first start's window shows them")
{
    CHECK_EQ(MCUpdateWork::SpeedText(10'400'000), std::string("10.4 MB/S"));
    CHECK_EQ(MCUpdateWork::SpeedText(999'960), std::string("1.0 MB/S"));
    CHECK_EQ(MCUpdateWork::SpeedText(850'400), std::string("850 KB/S"));
    CHECK_EQ(MCUpdateWork::SpeedText(0), std::string("0 KB/S"));

    using std::chrono::minutes;
    using std::chrono::seconds;
    CHECK_EQ(MCUpdateWork::TimeLeftText(seconds(59)), std::string("LESS THAN A MINUTE LEFT"));
    CHECK_EQ(MCUpdateWork::TimeLeftText(seconds(60)), std::string("ABOUT 1 MIN LEFT"));
    CHECK_EQ(MCUpdateWork::TimeLeftText(minutes(8) + seconds(20)), std::string("ABOUT 8 MIN LEFT"));
    CHECK_EQ(MCUpdateWork::TimeLeftText(minutes(8) + seconds(40)), std::string("ABOUT 9 MIN LEFT"));
    CHECK_EQ(MCUpdateWork::TimeLeftText(minutes(59) + seconds(40)), std::string("ABOUT 1 HR LEFT"));
    CHECK_EQ(MCUpdateWork::TimeLeftText(minutes(80)), std::string("ABOUT 1 HR 20 MIN LEFT"));
}

namespace
{
    /// <summary>A download fed to a rate a second at a time (or in parts of one, as the window draws).</summary>
    struct MCRateFeed
    {
        using MCClock = MCUpdateWork::MCRate::MCClock;

        MCUpdateWork::MCRate Rate;
        MCClock::time_point Start = MCClock::now();
        std::chrono::milliseconds At{0};
        uint64_t Network = 0;
        uint64_t Left = 0;

        explicit MCRateFeed(uint64_t left) : Left(left) { Rate.Add(Start, Network, Left); }

        /// <summary>A second at <paramref name="speed"/> bytes, sampled <paramref name="draws"/> times through it.</summary>
        void Second(uint64_t speed, int draws = 1)
        {
            for (int draw = 1; draw <= draws; ++draw)
            {
                const uint64_t part = speed / static_cast<uint64_t>(draws);
                At += std::chrono::milliseconds(1000) / draws;
                Network += part;
                Left -= std::min(Left, part);
                Rate.Add(Start + At, Network, Left);
            }
        }

        double Speed() const { return Rate.BytesPerSecond().value_or(-1); }

        int64_t SecondsLeft() const { return Rate.TimeLeft() ? Rate.TimeLeft()->count() : -1; }
    };
}

TEST_CASE("update work: the speed waits for 2 seconds of samples and the time left for 5, then tells them")
{
    constexpr uint64_t PerSecond = 10'400'000;
    constexpr std::string_view Separator = " \xC2\xB7 ";
    MCRateFeed feed(PerSecond * 485);
    CHECK(!feed.Rate.BytesPerSecond().has_value());
    CHECK_EQ(feed.Rate.Text(), std::string());

    feed.Second(PerSecond);
    CHECK_EQ(feed.Rate.Text(), std::string());
    feed.Second(PerSecond);
    CHECK_EQ(feed.Rate.Text(), std::string("10.4 MB/S"));
    CHECK(!feed.Rate.TimeLeft().has_value());
    feed.Second(PerSecond, 4);
    feed.Second(PerSecond);
    feed.Second(PerSecond);
    CHECK_EQ(feed.SecondsLeft(), int64_t{480});
    CHECK_EQ(feed.Rate.Text(), std::format("10.4 MB/S{}ABOUT 8 MIN LEFT", Separator));

    // Forgotten (a check of what's here): it starts again.
    feed.Rate.Reset();
    CHECK(!feed.Rate.BytesPerSecond().has_value());
    feed.Rate.Add(feed.Start + feed.At, feed.Network, feed.Left);
    CHECK_EQ(feed.Rate.Text(), std::string());
}

TEST_CASE("update work: a download's ups and downs barely move the speed shown, and the time left just counts down")
{
    // A second at three times the other's, by turns: 10.4 MB a second on the whole, for 16 minutes.
    constexpr uint64_t PerSecond = 10'400'000;
    MCRateFeed feed(PerSecond * 960);
    int64_t shownBefore = -1;

    for (int second = 0; second < 300; ++second)
    {
        // Sampled as a window draws, 30 times a second: only whole seconds count.
        feed.Second(second % 2 == 0 ? PerSecond * 3 / 2 : PerSecond / 2, 30);

        if (second < 30)
        {
            continue;
        }

        CHECK(std::abs(feed.Speed() - PerSecond) < PerSecond * 0.15);
        const int64_t shown = feed.SecondsLeft();
        REQUIRE(shown >= 0);

        // Down a second a second, never up.
        if (shownBefore >= 0)
        {
            CHECK(shown <= shownBefore);
            CHECK(shownBefore - shown <= 2);
        }

        shownBefore = shown;
    }

    // Still about right: 660 s of 10.4 MB a second left.
    CHECK(std::abs(feed.SecondsLeft() - 660) < 60);
}

TEST_CASE("update work: a download that truly slows shows it: the speed in seconds, the time left within a minute")
{
    constexpr uint64_t PerSecond = 10'400'000;
    MCRateFeed feed(PerSecond * 600);

    for (int second = 0; second < 60; ++second)
    {
        feed.Second(PerSecond);
    }

    CHECK(std::abs(feed.SecondsLeft() - 540) <= 2);

    // Half the speed from here.
    for (int second = 0; second < 15; ++second)
    {
        feed.Second(PerSecond / 2);
    }

    CHECK(feed.Speed() < PerSecond * 0.7);

    for (int second = 0; second < 45; ++second)
    {
        feed.Second(PerSecond / 2);
    }

    // 510 s at 10.4 MB a second left, so 1,020 s at half: told within 15%.
    CHECK(std::abs(feed.Speed() - PerSecond / 2) < PerSecond * 0.05);
    CHECK(std::abs(feed.SecondsLeft() - 1020) < 1020 * 15 / 100);
}

TEST_CASE("update work: nothing coming for 10 seconds shows 0 KB/S and no time left, until bytes come again")
{
    constexpr uint64_t PerSecond = 10'400'000;
    MCRateFeed feed(PerSecond * 600);

    for (int second = 0; second < 20; ++second)
    {
        feed.Second(PerSecond);
    }

    for (int second = 0; second < 9; ++second)
    {
        feed.Second(0);
    }

    CHECK(feed.Speed() > 0);
    CHECK(feed.Rate.TimeLeft().has_value());
    feed.Second(0);
    CHECK_EQ(feed.Rate.Text(), std::string("0 KB/S"));
    CHECK(!feed.Rate.TimeLeft().has_value());

    feed.Second(PerSecond);
    CHECK(feed.Speed() > 0);
    CHECK(feed.Rate.TimeLeft().has_value());
}

/// <summary>
/// Port-only (personal): one version on every platform, from version.json and the branch's count of commits
/// (src/mac/version.cmake on the Mac, MechCommander.props on Windows): the four-part version is the release and the
/// build, and the one shown (the game, its reports, the updaters) the release with the build after it in brackets.
/// </summary>
TEST_CASE("updater: the version is version.json's release with the commit count as its build, shown as 0.2.1 (518)")
{
    constexpr std::ptrdiff_t releaseParts = 3;
    const std::string version = MC_VERSION;
    const size_t buildDot = version.rfind('.');
    REQUIRE(buildDot != std::string::npos);
    const std::string release = version.substr(0, buildDot);

    CHECK_EQ(std::ranges::count(release, '.') + 1, releaseParts);
    CHECK_EQ(version.substr(buildDot + 1), std::to_string(MC_VERSION_BUILD));
    CHECK(MC_VERSION_BUILD > 0);
    CHECK_EQ(std::string(MC_VERSION_SHOWN), std::format("{} ({})", release, MC_VERSION_BUILD));
    CHECK_EQ(std::string(MC_VERSION_SEMVER), std::format("{}+{}", release, MC_VERSION_BUILD));
}

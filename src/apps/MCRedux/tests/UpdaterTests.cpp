#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogPort.h"
#include "logistics/MCLogTextButton.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCUpdatePage.h"
#include "main/MCLogistics.h"
#include "main/MCUpdater.h"
#include "MCVersion.h"

#include <algorithm>
#include <format>
#include <string>

// The updater (main/MCUpdater.h), with a stand-in for the Mac app's Sparkle: the check made once a start, each step
// reported in turn, the player's answers passed on, the feed's text kept to what the fonts write; and the main menu's
// UPDATE button in ORIGINAL CINEMA's place (logistics/MCUpdatePage.h), grayed with nothing new and amber with a line
// under QUIT while an update waits, its page installing, skipping and restarting.

namespace
{
    using MCUpdater::MCAnswer;
    using MCUpdater::MCStage;

    /// <summary>What the stand-in backend was asked.</summary>
    struct MCBackendLog
    {
        int32_t Checks = 0;
        /// <summary>The build each check looked for (none: the backend's own newest).</summary>
        std::vector<std::optional<uint64_t>> Builds;
        std::vector<MCAnswer> Answers;
    };

    /// <summary>A stand-in for Sparkle: it counts the checks and keeps the answers; the test reports the steps.</summary>
    class MCFakeBackend final : public MCUpdater::MCBackend
    {
    public:
        explicit MCFakeBackend(std::shared_ptr<MCBackendLog> log) : _Log(std::move(log)) {}

        std::string RunningVersion() override { return "0.2.1 (468)"; }

        void Check(std::optional<uint64_t> build) override
        {
            _Log->Checks++;
            _Log->Builds.push_back(build);
        }

        void Answer(MCAnswer answer) override { _Log->Answers.push_back(answer); }

    private:
        std::shared_ptr<MCBackendLog> _Log;
    };

    /// <summary>Sets a stand-in backend for the test's length, then takes it away.</summary>
    class MCFakeUpdater
    {
    public:
        MCFakeUpdater() { MCUpdater::SetBackend(std::make_unique<MCFakeBackend>(Log)); }
        ~MCFakeUpdater() { MCUpdater::SetBackend(nullptr); }
        MCFakeUpdater(const MCFakeUpdater&) = delete;
        MCFakeUpdater& operator=(const MCFakeUpdater&) = delete;

        std::shared_ptr<MCBackendLog> Log = std::make_shared<MCBackendLog>();
    };

    constexpr std::string_view NewVersion = "0.2.2 (480)";
    constexpr std::string_view Notes = "Night missions: dusk and night lighting.\n\nThe main menu updates the game.";

    /// <summary>Runs a few frames, as a player's screen would between inputs.</summary>
    void Frames()
    {
        constexpr int32_t frames = 3;

        for (int32_t frame = 0; frame < frames; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }

    /// <summary>Clicks the middle of <paramref name="object"/> as a player does.</summary>
    void RealClickOn(MCGuiObject& object)
    {
        MCScreenInput::RealClick(object.GlobalX() + object.Width() / 2, object.GlobalY() + object.Height() / 2);
        Frames();
    }

    /// <summary>Whether <paramref name="a"/> and <paramref name="b"/> hold the same pixels.</summary>
    bool SamePixels(const MCLogPort& a, const MCLogPort& b)
    {
        return a.Width() == b.Width() && a.Height() == b.Height() && std::ranges::equal(a.Pixels(), b.Pixels());
    }
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
    CHECK_EQ(updater.Log->Checks, 1);
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
    MCUpdater::Closed();
    CHECK(MCUpdater::Status().Stage == MCStage::Failed);
    CHECK(changes > 0);
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

TEST_CASE_ISOLATED("game: without an updater the main menu keeps ORIGINAL CINEMA")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    Frames();
    const std::shared_ptr<MCLogButton> cinema = GlobalLog()->MainScreen->CinemaButton;
    REQUIRE(cinema != nullptr);
    CHECK(SamePixels(*cinema->GetUpPicture(), *LogArt("mm_xbh08.tga")));
    CHECK(!cinema->Disabled);
    CHECK(!UpdatePageShown());
}

TEST_CASE_ISOLATED("game: UPDATE takes ORIGINAL CINEMA's place, amber while an update waits, and its page installs it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCFakeUpdater updater;
    REQUIRE(MCTestGame::StartLogistics());
    Frames();
    CHECK_EQ(updater.Log->Checks, 1);
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    const std::shared_ptr<MCLogButton> cinema = logistics->MainScreen->CinemaButton;
    REQUIRE(cinema != nullptr);

    // Nothing new: UPDATE, grayed, in the menu's own lettering.
    CHECK(cinema->Disabled);
    CHECK(!SamePixels(*cinema->GetGrayPicture(), *LogArt("mm_xbn08.tga")));
    MCScreenInput::SaveShot("update none", MCScreenInput::ScreenHash());
    RealClickOn(*cinema);
    CHECK(!UpdatePageShown());

    // An update waits: amber, the line under QUIT saying which.
    CHECK(!SamePixels(*cinema->GetUpPicture(), *LogArt("mm_xbh08.tga")));
    MCUpdater::Found(std::string(NewVersion), std::string(Notes));
    Frames();
    CHECK(!cinema->Disabled);
    MCScreenInput::SaveShot("update waiting", MCScreenInput::ScreenHash());

    // The page: the versions, the notes, INSTALL and SKIP.
    RealClickOn(*cinema);
    REQUIRE(UpdatePageShown());
    CHECK_EQ(UpdatePageStatus(), std::string("A NEWER VERSION IS OUT."));
    const std::vector<std::string> lines = UpdatePageLines();
    REQUIRE_EQ(lines.size(), size_t{3});
    CHECK_EQ(lines[0], std::string("NIGHT MISSIONS: DUSK AND NIGHT LIGHTING."));
    CHECK(lines[1].empty());
    const std::shared_ptr<MCLogTextButton> install = MCScreenInput::FindTextButton(*logistics->MainScreen, "INSTALL");
    const std::shared_ptr<MCLogTextButton> skip = MCScreenInput::FindTextButton(*logistics->MainScreen, "SKIP");
    const std::shared_ptr<MCLogTextButton> restart = MCScreenInput::FindTextButton(*logistics->MainScreen, "RESTART");
    const std::shared_ptr<MCLogTextButton> done = MCScreenInput::FindTextButton(*logistics->MainScreen, "DONE");
    REQUIRE(install != nullptr);
    REQUIRE(skip != nullptr);
    REQUIRE(restart != nullptr);
    REQUIRE(done != nullptr);
    CHECK(install->IsShowing());
    CHECK(skip->IsShowing());
    CHECK(!restart->IsShowing());
    MCScreenInput::SaveShot("update page", MCScreenInput::ScreenHash());

    // A click where QUIT is under the page reaches the page, not QUIT.
    MCScreenInput::RealClick(cinema->GlobalX() + cinema->Width() / 2, cinema->GlobalY() + cinema->Height() * 3 / 2);
    Frames();
    CHECK(UpdatePageShown());
    CHECK(updater.Log->Answers.empty());

    // INSTALL: the download's progress, then RESTART in INSTALL's place.
    RealClickOn(*install);
    REQUIRE_EQ(updater.Log->Answers.size(), size_t{1});
    CHECK(updater.Log->Answers[0] == MCAnswer::Install);
    CHECK(!install->IsShowing());
    constexpr uint64_t megabyte = 1024 * 1024;
    MCUpdater::Downloading(megabyte * 3 / 2, megabyte * 5);
    Frames();
    CHECK_EQ(UpdatePageStatus(), std::string("DOWNLOADING: 1.5 OF 5.0 MB."));
    MCScreenInput::SaveShot("update downloading", MCScreenInput::ScreenHash());
    MCUpdater::Ready();
    Frames();
    CHECK(restart->IsShowing());
    CHECK(!install->IsShowing());
    CHECK_EQ(restart->GlobalX(), install->GlobalX());
    MCScreenInput::SaveShot("update ready", MCScreenInput::ScreenHash());

    // DONE leaves it waiting (quitting installs it); RESTART installs it now.
    RealClickOn(*done);
    CHECK(!UpdatePageShown());
    CHECK(!cinema->Disabled);
    MCScreenInput::SaveShot("update ready menu", MCScreenInput::ScreenHash());
    RealClickOn(*cinema);
    RealClickOn(*restart);
    REQUIRE_EQ(updater.Log->Answers.size(), size_t{2});
    CHECK(updater.Log->Answers[1] == MCAnswer::Install);
    CHECK(MCUpdater::Status().Stage == MCStage::Installing);
    HideUpdatePage();

    // SKIP: this version isn't offered again, and UPDATE grays.
    MCUpdater::Found("0.2.3 (490)", "More.");
    Frames();
    RealClickOn(*cinema);
    REQUIRE(UpdatePageShown());
    RealClickOn(*skip);
    REQUIRE_EQ(updater.Log->Answers.size(), size_t{3});
    CHECK(updater.Log->Answers[2] == MCAnswer::Skip);
    CHECK(!UpdatePageShown());
    CHECK(cinema->Disabled);
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

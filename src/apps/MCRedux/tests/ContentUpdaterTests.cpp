#include "stdafx.h"
#include "MCContentFixture.h"
#include "MCTest.h"
#include "main/MCUpdater.h"
#include "update/MCContentSwap.h"
#include "update/MCContentUpdater.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateLock.h"

#include <thread>

// The game's own updater (update/MCContentUpdater.h, main/MCUpdater.h) and its locks (update/MCUpdateLock.h): a
// release hosted in a folder as the release script lays it out, checked against the root key, planned, staged and
// armed; the program's backend (Sparkle on the Mac) held to release.json's build and armed with the content, neither
// half going in without the other; a skip kept; another copy kept from swapping under a running one, and from staging
// beside it.

namespace
{
    using namespace MCContentFixture;
    using MCUpdater::MCAnswer;
    using MCUpdater::MCStage;

    constexpr std::string_view Group = "mc_content_updater_tests";

    /// <summary>The build the tests run as, a newer one, and its program's size.</summary>
    constexpr uint64_t RunningBuild = 500;
    constexpr uint64_t NewerBuild = 600;
    constexpr uint64_t ProgramSize = 7'000'000;
    constexpr std::string_view RunningShown = "0.2.1 (500)";
    constexpr std::string_view ReleaseVersion = "0.2.2";
    constexpr std::string_view ReleaseNotes = "New missions.";

    /// <summary>How long a test waits for the updater's work, and how often it looks.</summary>
    constexpr std::chrono::seconds WorkWait{30};
    constexpr std::chrono::milliseconds LookAgain{5};

    /// <summary>
    /// Release 2 hosted as the release script lays it out (keys.json and release.json signed, the list under lists/),
    /// over a copy of release 1 that records it, and the copy's user folder.
    /// </summary>
    struct MCHostedRelease : MCRelease
    {
        explicit MCHostedRelease(std::string_view name)
            : MCRelease(Group, name), User(Group, std::string(name) + "-user"), ListText(MCContentList::Write(List))
        {
            Write(Host.Path() / "lists" / (MCSha256::Hex(MCSha256::Of(ListText)) + ".json"), ListText);
            Write(Host.Path() / "keys.json", KeyList(Root, 1, {ReleaseKey.Public}));
            Write(Game.Path() / "Redux update" / "installed.json", MCInstalled::Text(Installed));
            Publish(1, 2, RunningBuild);
        }

        /// <summary>release.json at <paramref name="issue"/>: release <paramref name="number"/> with the list, its program at <paramref name="build"/>.</summary>
        void Publish(uint64_t issue, uint64_t number, uint64_t build) const
        {
            MCReleaseFile::MCRelease release{.Number = number,
                                             .Version = std::string(ReleaseVersion),
                                             .Notes = std::string(ReleaseNotes),
                                             .List = MCSha256::Of(ListText),
                                             .ListSize = ListText.size()};
            release.Programs["mac"] = {.Build = build, .Size = ProgramSize};
            release.Programs["windows"] = {.Build = build, .Size = ProgramSize};
            const MCReleaseFile::MCReleases releases{.Format = 1, .Issue = issue, .Current = release};
            Write(Host.Path() / "release.json",
                  ReleaseKey.Envelope(MCSignedFile::ReleaseType, MCReleaseFile::ReleasesPayload(releases)));
        }

        MCContentUpdater::MCSetup Setup() const
        {
            return {.GameFolder = Game.Path(),
                    .UserFolder = User.Path(),
                    .RootKeys = {Root.Public},
                    .Platform = "mac",
                    .RunningBuild = RunningBuild,
                    .RunningVersion = std::string(RunningShown)};
        }

        /// <summary>Whether an update is armed, waiting for the next start.</summary>
        bool Armed() const { return MCContentSwap::Waiting(*MakeDisk(), Game.Path()); }

        /// <summary>What the content downloads.</summary>
        uint64_t ContentBytes() { return Plan().DownloadBytes; }

        KeyPair Root{20};
        KeyPair ReleaseKey{21};
        MCScratchFolder User;
        std::string ListText;
    };

    /// <summary>What the stand-in program backend was asked.</summary>
    struct MCProgramLog
    {
        std::vector<std::optional<uint64_t>> Checks;
        std::vector<MCAnswer> Answers;
    };

    /// <summary>A stand-in for Sparkle: it keeps what it's asked; the test reports its steps.</summary>
    class MCFakeProgram final : public MCUpdater::MCBackend
    {
    public:
        explicit MCFakeProgram(std::shared_ptr<MCProgramLog> log) : _Log(std::move(log)) {}

        std::string RunningVersion() override { return std::string(RunningShown); }

        void Check(std::optional<uint64_t> build) override { _Log->Checks.push_back(build); }

        void Answer(MCAnswer answer) override { _Log->Answers.push_back(answer); }

    private:
        std::shared_ptr<MCProgramLog> _Log;
    };

    /// <summary>
    /// The updater with the release's content and, with <paramref name="program"/>, a stand-in program backend, for the
    /// test's length; RESTART's quits counted.
    /// </summary>
    class MCUpdaterScope
    {
    public:
        MCUpdaterScope(const MCHostedRelease& release, bool program)
        {
            if (program)
            {
                MCUpdater::SetBackend(std::make_unique<MCFakeProgram>(Program));
            }

            MCUpdater::SetContent(release.Setup(),
                                  [host = release.Host.Path()] { return std::make_unique<MCFolderSource>(host); });
            MCUpdater::SetQuit([quits = Quits] { ++*quits; });
        }

        ~MCUpdaterScope()
        {
            MCUpdater::ClearContent();
            MCUpdater::SetBackend(nullptr);
            MCUpdater::SetQuit(nullptr);
        }

        MCUpdaterScope(const MCUpdaterScope&) = delete;
        MCUpdaterScope& operator=(const MCUpdaterScope&) = delete;

        std::shared_ptr<MCProgramLog> Program = std::make_shared<MCProgramLog>();
        std::shared_ptr<int32_t> Quits = std::make_shared<int32_t>(0);
    };

    /// <summary>Runs the updater's frames until <paramref name="done"/>, as the game does; false if it never is.</summary>
    bool FramesUntil(const std::function<bool()>& done)
    {
        const auto until = std::chrono::steady_clock::now() + WorkWait;

        while (!done())
        {
            if (std::chrono::steady_clock::now() > until)
            {
                return false;
            }

            MCUpdater::Frame();
            std::this_thread::sleep_for(LookAgain);
        }

        return true;
    }

    /// <summary>Runs the updater's frames until its work is done.</summary>
    bool FramesUntilIdle()
    {
        return FramesUntil([] { return !MCUpdater::Working(); });
    }

    MCStage Stage()
    {
        return MCUpdater::Status().Stage;
    }

    /// <summary>Whether a copy could take the staging lock now (none stages).</summary>
    bool StagingFree(const std::filesystem::path& game)
    {
        const std::unique_ptr<MCUpdateLock> lock = MCUpdateLock::Open(game, MCUpdateLock::StagingLock);
        return lock != nullptr && lock->TryExclusive();
    }

    /// <summary>The release found, its program the build <paramref name="build"/> when the backend is to look for it.</summary>
    void CheckUntilFound(MCUpdaterScope& scope, std::optional<uint64_t> build)
    {
        MCUpdater::CheckAtStart();

        if (build)
        {
            REQUIRE(FramesUntil([&] { return !scope.Program->Checks.empty(); }));
            CHECK(scope.Program->Checks.front() == build);
            CHECK(Stage() == MCStage::None);
            MCUpdater::Found(std::format("{}.0 ({})", ReleaseVersion, *build), "Sparkle's own notes.");
        }

        REQUIRE(FramesUntil([] { return Stage() == MCStage::Found; }));
    }
}

TEST_CASE("update lock: running copies share the game's lock and keep the swap out; one copy stages at a time")
{
    MCScratchFolder game(Group, "locks");
    const std::unique_ptr<MCUpdateLock> first = MCUpdateLock::Open(game.Path(), MCUpdateLock::GameLock);
    const std::unique_ptr<MCUpdateLock> second = MCUpdateLock::Open(game.Path(), MCUpdateLock::GameLock);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(std::filesystem::exists(game.Path() / "Redux update" / "game.lock"));

    // A running copy keeps the swap out, and another runs beside it.
    REQUIRE(first->Shared());
    CHECK(!second->TryExclusive());
    CHECK(second->Shared());
    CHECK(!first->TryExclusive());

    // Alone, a copy swaps; meanwhile no other copy swaps.
    first->Release();
    CHECK(second->TryExclusive());
    CHECK(!first->TryExclusive());
    second->Release();

    // One copy stages at a time; one that quits lets go.
    std::unique_ptr<MCUpdateLock> stagingA = MCUpdateLock::Open(game.Path(), MCUpdateLock::StagingLock);
    const std::unique_ptr<MCUpdateLock> stagingB = MCUpdateLock::Open(game.Path(), MCUpdateLock::StagingLock);
    REQUIRE(stagingA != nullptr);
    REQUIRE(stagingB != nullptr);
    CHECK(stagingA->TryExclusive());
    CHECK(!stagingB->TryExclusive());
    stagingA.reset();
    CHECK(stagingB->TryExclusive());
}

TEST_CASE("content swap at a start: another copy running holds the update for a later start; a swap stopped midway "
          "is waited for")
{
    MCRelease release(Group, "start-lock");
    const std::filesystem::path& game = release.Game.Path();
    const MCContentPlan::MCPlan plan = release.Plan();
    MCFolderSource source(release.Host.Path());
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    REQUIRE(MCContentStage::Stage(*disk, source, game, plan, release.List, GoOn).has_value());
    REQUIRE(MCContentStage::Arm(*disk, game, plan).has_value());

    const std::unique_ptr<MCUpdateLock> running = MCUpdateLock::Open(game, MCUpdateLock::GameLock);
    const std::unique_ptr<MCUpdateLock> starting = MCUpdateLock::Open(game, MCUpdateLock::GameLock);
    REQUIRE(running != nullptr);
    REQUIRE(starting != nullptr);
    REQUIRE(running->Shared());

    // Another copy runs on (past the start's deadline): the update waits, and the start plays the files as they are,
    // holding the lock shared.
    constexpr std::chrono::milliseconds shortWait{300};
    const MCContentSwap::MCResult held =
        MCContentSwap::RunAtStart(*disk, game, starting.get(), std::chrono::steady_clock::now() + shortWait);
    CHECK(held.Outcome == MCContentSwap::MCOutcome::PutBack);
    CHECK(held.Error.find("another copy") != std::string::npos);
    CHECK(MCContentSwap::Waiting(*disk, game));
    CHECK_EQ(Read(game / "data" / "changed.fit"), std::string("changed v1"));
    running->Release();
    CHECK(!running->TryExclusive());
    starting->Release();

    // A swap stopped midway, with another copy still holding the lock: waited for, then stuck past the deadline.
    REQUIRE(disk->Rename(game / "Redux update" / "ready.json", game / "Redux update" / "journal.json"));
    REQUIRE(running->Shared());
    const MCContentSwap::MCResult stuck =
        MCContentSwap::RunAtStart(*disk, game, starting.get(), std::chrono::steady_clock::now() + shortWait);
    CHECK(stuck.Outcome == MCContentSwap::MCOutcome::Stuck);
    running->Release();
    starting->Release();

    // Alone, the start finishes it, and holds the lock shared: another copy runs beside it, none swaps under it.
    CHECK(MCContentSwap::RunAtStart(*disk, game, starting.get()).Outcome == MCContentSwap::MCOutcome::Swapped);
    release.CheckIsRelease2();
    CHECK(!running->TryExclusive());
    CHECK(running->Shared());
}

TEST_CASE("content swap at a start: a copy quitting as the start begins (RESTART) is waited for, then the update "
          "swaps in")
{
    MCRelease release(Group, "start-quitting");
    const std::filesystem::path& game = release.Game.Path();
    const MCContentPlan::MCPlan plan = release.Plan();
    MCFolderSource source(release.Host.Path());
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    REQUIRE(MCContentStage::Stage(*disk, source, game, plan, release.List, GoOn).has_value());
    REQUIRE(MCContentStage::Arm(*disk, game, plan).has_value());

    const std::unique_ptr<MCUpdateLock> quitting = MCUpdateLock::Open(game, MCUpdateLock::GameLock);
    const std::unique_ptr<MCUpdateLock> starting = MCUpdateLock::Open(game, MCUpdateLock::GameLock);
    REQUIRE(quitting != nullptr);
    REQUIRE(starting != nullptr);
    REQUIRE(quitting->Shared());
    constexpr std::chrono::milliseconds closing{200};
    std::jthread ends(
        [&]
        {
            std::this_thread::sleep_for(closing);
            quitting->Release();
        });
    CHECK(MCContentSwap::RunAtStart(*disk, game, starting.get()).Outcome == MCContentSwap::MCOutcome::Swapped);
    release.CheckIsRelease2();
}

TEST_CASE("content updater: release.json checked, the content planned, staged, armed only when asked, swapped in")
{
    MCHostedRelease release("check");
    MCFolderSource source(release.Host.Path());
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    const MCContentUpdater::MCSetup setup = release.Setup();

    const auto offer = MCContentUpdater::Check(setup, *disk, source);
    REQUIRE(offer.has_value());
    REQUIRE(offer->has_value());
    const MCContentUpdater::MCOffer& found = **offer;
    CHECK_EQ(found.Release.Number, uint64_t{2});
    CHECK(!found.ProgramBuild.has_value());
    REQUIRE(found.Content != nullptr);
    CHECK_EQ(found.ContentBytes(), release.ContentBytes());
    CHECK(!found.Quiet());
    CHECK_EQ(found.ShownVersion(RunningBuild), std::string("0.2.2 (500)"));

    // update.json keeps the issues read and the copy's rollout number, the same at every check.
    const auto kept = MCReleaseFile::ReadKept(Read(release.User.Path() / "update.json"));
    REQUIRE(kept.has_value());
    CHECK_EQ(kept->KeyListIssue, uint64_t{1});
    CHECK_EQ(kept->ReleaseIssue, uint64_t{1});
    REQUIRE(kept->RolloutNumber.has_value());

    // Staged, nothing waits for the next start until it's armed (the program's half comes in first).
    REQUIRE(MCContentUpdater::Stage(setup, *disk, source, found, GoOn).has_value());
    CHECK(!release.Armed());
    REQUIRE(MCContentUpdater::Arm(setup, *disk, found).has_value());
    CHECK(release.Armed());

    // While it waits, the check offers nothing more.
    const auto waiting = MCContentUpdater::Check(setup, *disk, source);
    REQUIRE(waiting.has_value());
    CHECK(!waiting->has_value());
    CHECK(MCContentSwap::Run(*disk, release.Game.Path()).Outcome == MCContentSwap::MCOutcome::Swapped);
    release.CheckIsRelease2();

    // Installed: nothing new.
    const auto again = MCContentUpdater::Check(setup, *disk, source);
    REQUIRE(again.has_value());
    CHECK(!again->has_value());
    const auto keptAgain = MCReleaseFile::ReadKept(Read(release.User.Path() / "update.json"));
    REQUIRE(keptAgain.has_value());
    CHECK(keptAgain->RolloutNumber == kept->RolloutNumber);

    // A newer program for the same content: the program alone.
    release.Publish(2, 2, NewerBuild);
    const auto program = MCContentUpdater::Check(setup, *disk, source);
    REQUIRE(program.has_value());
    REQUIRE(program->has_value());
    CHECK((*program)->ProgramBuild == NewerBuild);
    CHECK_EQ((*program)->ProgramBytes, ProgramSize);
    CHECK((*program)->Content == nullptr);
    CHECK_EQ((*program)->ShownVersion(RunningBuild), std::string("0.2.2 (600)"));

    // An older release.json served again is refused.
    release.Publish(1, 2, RunningBuild);
    const auto older = MCContentUpdater::Check(setup, *disk, source);
    REQUIRE(!older.has_value());
    CHECK(older.error().find("older") != std::string::npos);
}

TEST_CASE("content updater: a skip is kept and a higher release offered; files already the release's go in unasked; "
          "a folder this account can't write is never offered")
{
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();

    {
        MCHostedRelease release("skip");
        MCFolderSource source(release.Host.Path());
        const MCContentUpdater::MCSetup setup = release.Setup();
        const auto offer = MCContentUpdater::Check(setup, *disk, source);
        REQUIRE(offer.has_value());
        REQUIRE(offer->has_value());
        REQUIRE(MCContentUpdater::Skip(setup, *disk, **offer).has_value());

        const auto skipped = MCContentUpdater::Check(setup, *disk, source);
        REQUIRE(skipped.has_value());
        CHECK(!skipped->has_value());

        release.Publish(2, 3, RunningBuild);
        const auto higher = MCContentUpdater::Check(setup, *disk, source);
        REQUIRE(higher.has_value());
        REQUIRE(higher->has_value());
        CHECK_EQ((*higher)->Release.Number, uint64_t{3});
    }

    {
        // A copy whose files are already release 2's, with no record: nothing to fetch or ask.
        MCHostedRelease release("quiet");
        const std::filesystem::path& game = release.Game.Path();
        Write(game / "HD" / "terrain.hda", Text(MCFilePatchFixture::PatchNew()));
        Write(game / "data" / "changed.fit", "changed v2");
        Write(game / "data" / "missions" / "new.fit", "a new mission");
        std::filesystem::remove(game / "data" / "removed.fit");
        std::filesystem::remove(game / "Redux update" / "installed.json");
        MCFolderSource source(release.Host.Path());
        const auto offer = MCContentUpdater::Check(release.Setup(), *disk, source);
        REQUIRE(offer.has_value());
        REQUIRE(offer->has_value());
        CHECK((*offer)->Quiet());
        CHECK_EQ((*offer)->ContentBytes(), uint64_t{0});
    }

#ifndef _WIN32
    {
        // Another account's copy: it plays, and isn't offered what it can't install.
        MCHostedRelease release("not-writable");
        const std::filesystem::path& game = release.Game.Path();
        std::filesystem::permissions(game, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove);
        MCFolderSource source(release.Host.Path());
        const auto offer = MCContentUpdater::Check(release.Setup(), *disk, source);
        std::filesystem::permissions(game, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
        REQUIRE(!offer.has_value());
        CHECK(offer.error().find("can't write") != std::string::npos);
    }
#endif
}

TEST_CASE("updater: content alone is checked off the main thread, staged on INSTALL, ready once armed, and RESTART "
          "starts the game again")
{
    MCHostedRelease release("updater-content");
    MCUpdaterScope scope(release, false);
    CHECK(MCUpdater::Available());
    CHECK_EQ(MCUpdater::RunningVersion(), std::string(RunningShown));
    CheckUntilFound(scope, std::nullopt);
    CHECK_EQ(MCUpdater::Status().Version, std::string("0.2.2 (500)"));
    CHECK_EQ(MCUpdater::Status().Notes, std::string(ReleaseNotes));
    CHECK_EQ(MCUpdater::Status().Expected, release.ContentBytes());

    MCUpdater::Answer(MCAnswer::Install);
    CHECK(Stage() == MCStage::Downloading);
    REQUIRE(FramesUntil([] { return Stage() != MCStage::Downloading; }));
    REQUIRE(Stage() == MCStage::Ready);
    CHECK(release.Armed());
    CHECK_EQ(MCUpdater::Status().Received, MCUpdater::Status().Expected);
    CHECK(StagingFree(release.Game.Path()));

    MCUpdater::Answer(MCAnswer::Install);
    CHECK(Stage() == MCStage::Installing);
    CHECK_EQ(*scope.Quits, 1);
    CHECK(MCUpdater::RestartWanted());
}

TEST_CASE("updater: the program held to release.json's build; the content stages first and is armed the moment the "
          "program is ready")
{
    MCHostedRelease release("updater-program");
    release.Publish(1, 2, NewerBuild);
    MCUpdaterScope scope(release, true);
    CheckUntilFound(scope, NewerBuild);

    // release.json's version and notes, and the whole download's size.
    CHECK_EQ(MCUpdater::Status().Version, std::string("0.2.2 (600)"));
    CHECK_EQ(MCUpdater::Status().Notes, std::string(ReleaseNotes));
    const uint64_t contentBytes = release.ContentBytes();
    CHECK_EQ(MCUpdater::Status().Expected, contentBytes + ProgramSize);

    // The program waits for the content to be staged.
    MCUpdater::Answer(MCAnswer::Install);
    CHECK(scope.Program->Answers.empty());
    REQUIRE(FramesUntil([&] { return !scope.Program->Answers.empty(); }));
    CHECK(scope.Program->Answers.back() == MCAnswer::Install);
    CHECK(!release.Armed());

    constexpr uint64_t programIn = 1000;
    MCUpdater::Downloading(programIn, ProgramSize);
    CHECK(Stage() == MCStage::Downloading);
    CHECK_EQ(MCUpdater::Status().Received, contentBytes + programIn);
    CHECK_EQ(MCUpdater::Status().Expected, contentBytes + ProgramSize);

    // The program's installer armed: the content is armed with it.
    MCUpdater::Ready();
    CHECK(Stage() == MCStage::Ready);
    CHECK(release.Armed());
    CHECK(StagingFree(release.Game.Path()));

    // RESTART is the program's installer's: it starts the new version, whose start swaps the content in.
    MCUpdater::Answer(MCAnswer::Install);
    CHECK(Stage() == MCStage::Installing);
    CHECK(scope.Program->Answers.back() == MCAnswer::Install);
    CHECK_EQ(*scope.Quits, 0);
    CHECK(!MCUpdater::RestartWanted());
}

TEST_CASE("updater: the program's download fails: the content stays staged, unarmed")
{
    MCHostedRelease release("program-fails");
    release.Publish(1, 2, NewerBuild);
    MCUpdaterScope scope(release, true);
    CheckUntilFound(scope, NewerBuild);
    MCUpdater::Answer(MCAnswer::Install);
    REQUIRE(FramesUntil([&] { return !scope.Program->Answers.empty(); }));
    MCUpdater::Failed("the download was cut off");
    CHECK(Stage() == MCStage::Failed);
    CHECK(MCUpdater::Status().Error.find("cut off") != std::string::npos);
    CHECK(!release.Armed());
    CHECK(StagingFree(release.Game.Path()));
}

#ifndef _WIN32
TEST_CASE("updater: the content can't be armed: the program's install is cancelled")
{
    MCHostedRelease release("arm-fails");
    release.Publish(1, 2, NewerBuild);
    MCUpdaterScope scope(release, true);
    CheckUntilFound(scope, NewerBuild);
    MCUpdater::Answer(MCAnswer::Install);
    REQUIRE(FramesUntil([&] { return !scope.Program->Answers.empty(); }));
    const std::filesystem::path update = release.Game.Path() / "Redux update";
    std::filesystem::permissions(update, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove);
    MCUpdater::Ready();
    std::filesystem::permissions(update, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
    CHECK(scope.Program->Answers.back() == MCAnswer::Skip);
    CHECK(Stage() == MCStage::Failed);
    CHECK(!release.Armed());
}
#endif

TEST_CASE("updater: a skip: kept, and the program's backend let go for now")
{
    MCHostedRelease release("skipped");
    release.Publish(1, 2, NewerBuild);

    {
        MCUpdaterScope scope(release, true);
        CheckUntilFound(scope, NewerBuild);
        MCUpdater::Answer(MCAnswer::Skip);
        CHECK(Stage() == MCStage::None);
        CHECK(scope.Program->Answers.back() == MCAnswer::Later);
        const auto kept = MCReleaseFile::ReadKept(Read(release.User.Path() / "update.json"));
        REQUIRE(kept.has_value());
        CHECK_EQ(kept->Skipped, uint64_t{2});
    }

    // The next start offers nothing, and asks the program's backend for nothing.
    MCUpdaterScope next(release, true);
    MCUpdater::CheckAtStart();
    REQUIRE(FramesUntilIdle());
    CHECK(Stage() == MCStage::None);
    CHECK(next.Program->Checks.empty());
}

TEST_CASE("updater: the program's build isn't out yet: nothing is offered")
{
    MCHostedRelease release("not-out");
    release.Publish(1, 2, NewerBuild);
    MCUpdaterScope scope(release, true);
    MCUpdater::CheckAtStart();
    REQUIRE(FramesUntil([&] { return !scope.Program->Checks.empty(); }));
    MCUpdater::Closed();
    MCUpdater::Found("0.2.2 (600)", "late");
    CHECK(Stage() == MCStage::None);
}

TEST_CASE("updater: another copy stages: INSTALL fails, saying so")
{
    MCHostedRelease release("other-copy");
    release.Publish(1, 2, NewerBuild);
    MCUpdaterScope scope(release, true);
    CheckUntilFound(scope, NewerBuild);
    const std::unique_ptr<MCUpdateLock> other = MCUpdateLock::Open(release.Game.Path(), MCUpdateLock::StagingLock);
    REQUIRE(other != nullptr);
    REQUIRE(other->TryExclusive());
    MCUpdater::Answer(MCAnswer::Install);
    CHECK(Stage() == MCStage::Failed);
    CHECK(MCUpdater::Status().Error.find("another copy") != std::string::npos);
    CHECK(scope.Program->Answers.back() == MCAnswer::Later);
    CHECK(!release.Armed());
}

TEST_CASE("updater: Sparkle installing without its ready step (an update it downloaded before) arms the content too")
{
    MCHostedRelease release("installs-at-once");
    release.Publish(1, 2, NewerBuild);
    MCUpdaterScope scope(release, true);
    CheckUntilFound(scope, NewerBuild);
    MCUpdater::Answer(MCAnswer::Install);
    REQUIRE(FramesUntil([&] { return !scope.Program->Answers.empty(); }));
    MCUpdater::Installing();
    CHECK(Stage() == MCStage::Installing);
    CHECK(release.Armed());
    CHECK(StagingFree(release.Game.Path()));
}

TEST_CASE("updater: Sparkle ending its session while the update is shown fails it rather than wait")
{
    MCHostedRelease release("session-ends");
    release.Publish(1, 2, NewerBuild);
    MCUpdaterScope scope(release, true);
    CheckUntilFound(scope, NewerBuild);
    MCUpdater::Closed();
    CHECK(Stage() == MCStage::Failed);
    MCUpdater::Answer(MCAnswer::Install);
    CHECK(Stage() == MCStage::Failed);
    CHECK(scope.Program->Answers.empty());
    CHECK(!release.Armed());
}

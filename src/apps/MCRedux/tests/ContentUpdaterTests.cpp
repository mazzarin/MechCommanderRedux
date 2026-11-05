#include "stdafx.h"
#include "MCContentFixture.h"
#include "../MCFirstStart.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCHdOptionsPanel.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCLogOptionList.h"
#include "logistics/MCLogTextButton.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCLogistics.h"
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
// beside it. The HD pack got and removed at the release installed (HD OPTIONS), an update planned for the sets chosen,
// and the first start's install into a folder an installer prepared, with its window (MCFirstStart.h).

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

    /// <summary>The sets with HD, and without.</summary>
    const MCContentUpdater::MCSets WithHd = {"game", "hd"};
    const MCContentUpdater::MCSets WithoutHd = {"game"};

    /// <summary>The HD file's bytes in release 2.</summary>
    std::string Terrain()
    {
        return Text(MCFilePatchFixture::PatchNew());
    }

    /// <summary>Stages, arms and swaps in <paramref name="offer"/>, as INSTALL and the next start do.</summary>
    void PutInPlace(const MCHostedRelease& release, const MCContentUpdater::MCOffer& offer)
    {
        MCFolderSource source(release.Host.Path());
        const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
        REQUIRE(MCContentUpdater::Stage(release.Setup(), *disk, source, offer, GoOn).has_value());
        REQUIRE(MCContentUpdater::Arm(release.Setup(), *disk, offer).has_value());
        REQUIRE(MCContentSwap::Run(*disk, release.Game.Path()).Outcome == MCContentSwap::MCOutcome::Swapped);
    }

    /// <summary>The copy brought to release 2, checked and put in place.</summary>
    void UpdateToRelease2(const MCHostedRelease& release)
    {
        MCFolderSource source(release.Host.Path());
        const auto offer = MCContentUpdater::Check(release.Setup(), *MakeDisk(), source);
        REQUIRE(offer.has_value());
        REQUIRE(offer->has_value());
        PutInPlace(release, **offer);
    }

    /// <summary><paramref name="sets"/> chosen, planned against the release installed, and put in place.</summary>
    void ChangeSets(const MCHostedRelease& release, const MCContentUpdater::MCSets& sets)
    {
        const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
        REQUIRE(MCContentUpdater::Choose(*disk, release.Game.Path(), sets).has_value());
        MCFolderSource source(release.Host.Path());
        const auto change = MCContentUpdater::CheckSets(release.Setup(), *disk, source);
        REQUIRE(change.has_value());
        REQUIRE(change->has_value());
        PutInPlace(release, **change);
    }

    /// <summary>The sets the copy's record has.</summary>
    MCContentUpdater::MCSets InstalledSets(const MCHostedRelease& release)
    {
        const auto record = MCInstalled::Read(release.Game.Path());
        REQUIRE(record.has_value());
        return record->Sets;
    }

    /// <summary>A download held until the test lets it go, or until the work is stopped.</summary>
    struct MCGate
    {
        std::atomic<bool> Open = false;
        std::atomic<bool> Reached = false;
    };

    /// <summary>The release's files hosted in a folder, each download held at its gate.</summary>
    class MCHeldSource final : public MCContentStage::MCSource
    {
    public:
        MCHeldSource(std::filesystem::path folder, std::shared_ptr<MCGate> gate)
            : _Inner(std::move(folder)), _Gate(std::move(gate))
        {
        }

        std::expected<void, std::string> Fetch(std::string_view name, const std::filesystem::path& to, uint64_t size,
                                               const std::function<bool(uint64_t)>& received) override
        {
            _Gate->Reached = true;

            while (!_Gate->Open)
            {
                if (!received(0))
                {
                    return std::unexpected("stopped");
                }

                std::this_thread::sleep_for(LookAgain);
            }

            return _Inner.Fetch(name, to, size, received);
        }

        std::expected<std::string, std::string> Read(std::string_view name, size_t most) override
        {
            return _Inner.Read(name, most);
        }

    private:
        MCFolderSource _Inner;
        std::shared_ptr<MCGate> _Gate;
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
        MCUpdaterScope(const MCHostedRelease& release, bool program, MCUpdater::MCSourceMaker source = nullptr)
        {
            if (program)
            {
                MCUpdater::SetBackend(std::make_unique<MCFakeProgram>(Program));
            }

            MCUpdater::SetContent(
                release.Setup(), source != nullptr ? std::move(source) : [host = release.Host.Path()]
                                     { return std::make_unique<MCFolderSource>(host); });
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

TEST_CASE(
    "content updater: HD removed and got again at the release installed, its size recorded; removed, the player's "
    "version of an HD file is deleted too, a file of their own left")
{
    MCHostedRelease release("sets");
    const std::filesystem::path& game = release.Game.Path();
    UpdateToRelease2(release);
    const auto record = MCInstalled::Read(game);
    REQUIRE(record.has_value());
    CHECK_EQ(record->SetBytes.at("hd"), uint64_t{Terrain().size()});
    CHECK_EQ(record->SetBytes.at("game"), uint64_t{10 + 13 + 12});

    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    MCFolderSource source(release.Host.Path());
    const MCContentUpdater::MCSetup setup = release.Setup();

    // Nothing chosen apart from what the copy has: no change.
    CHECK(!MCContentUpdater::Chosen(*disk, game).has_value());
    const auto none = MCContentUpdater::CheckSets(setup, *disk, source);
    REQUIRE(none.has_value());
    CHECK(!none->has_value());

    // HD removed: nothing downloaded, its file moved out at the swap, its emptied folder gone; a second change waits
    // for the start that puts the first in place.
    REQUIRE(MCContentUpdater::Choose(*disk, game, WithoutHd).has_value());
    CHECK(MCContentUpdater::Chosen(*disk, game) == WithoutHd);
    const auto removal = MCContentUpdater::CheckSets(setup, *disk, source);
    REQUIRE(removal.has_value());
    REQUIRE(removal->has_value());
    CHECK_EQ((*removal)->ContentBytes(), uint64_t{0});
    CHECK_EQ((*removal)->Release.Number, uint64_t{2});
    REQUIRE(MCContentUpdater::Stage(setup, *disk, source, **removal, GoOn).has_value());
    REQUIRE(MCContentUpdater::Arm(setup, *disk, **removal).has_value());
    const auto waits = MCContentUpdater::CheckSets(setup, *disk, source);
    REQUIRE(!waits.has_value());
    CHECK(waits.error().find("next start") != std::string::npos);
    REQUIRE(MCContentSwap::Run(*disk, game).Outcome == MCContentSwap::MCOutcome::Swapped);
    CHECK(!std::filesystem::exists(game / "HD"));
    CHECK_EQ(Read(game / "data" / "same.fit"), std::string("same in both"));
    CHECK(InstalledSets(release) == WithoutHd);

    // HD got again: its file fetched from its zip.
    ChangeSets(release, WithHd);
    CHECK(Read(game / "HD" / "terrain.hda") == Terrain());
    CHECK(InstalledSets(release) == WithHd);
    CHECK_EQ(MCInstalled::Read(game)->Files.size(), size_t{4});

    // HD removed deletes the player's version of an HD file too, keeping nothing (GET downloads it again); a file of
    // their own in the folder stays, and so does the folder.
    Write(game / "HD" / "terrain.hda", "my own terrain");
    Write(game / "HD" / "mine.hda", "added by the player");
    ChangeSets(release, WithoutHd);
    CHECK(!std::filesystem::exists(game / "HD" / "terrain.hda"));
    CHECK(!std::filesystem::exists(game / "Redux backup"));
    CHECK_EQ(Read(game / "HD" / "mine.hda"), std::string("added by the player"));
    CHECK(InstalledSets(release) == WithoutHd);

    // A sets file that can't be read is left aside: the copy keeps the sets its record has.
    Write(game / "Redux update" / "sets.json", "not json");
    CHECK(!MCContentUpdater::Chosen(*disk, game).has_value());
    CHECK(MCContentUpdater::Wanted(*disk, game, *MCInstalled::Read(game)) == WithoutHd);
}

TEST_CASE("content updater: an update is planned for the sets chosen")
{
    MCHostedRelease release("update-sets");
    const std::filesystem::path& game = release.Game.Path();
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    REQUIRE(MCContentUpdater::Choose(*disk, game, WithoutHd).has_value());
    MCFolderSource source(release.Host.Path());
    const auto offer = MCContentUpdater::Check(release.Setup(), *disk, source);
    REQUIRE(offer.has_value());
    REQUIRE(offer->has_value());

    // The HD file isn't patched but removed; only the update zip is downloaded.
    CHECK_EQ((*offer)->ContentBytes(), uint64_t{std::filesystem::file_size(release.Host.Path() / "update-2.zip")});
    PutInPlace(release, **offer);
    CHECK(!std::filesystem::exists(game / "HD"));
    CHECK_EQ(Read(game / "data" / "changed.fit"), std::string("changed v2"));
    CHECK(InstalledSets(release) == WithoutHd);
}

TEST_CASE("content updater: the first start installs the sets chosen into a folder an installer prepared, a cut "
          "download resumed; one copy at a time; content needing a newer program refused")
{
    MCHostedRelease release("first");
    const std::filesystem::path& game = release.Game.Path();
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    const MCContentUpdater::MCSetup setup = release.Setup();

    // An empty folder an installer prepared: its answer, and nothing else.
    std::filesystem::remove_all(game);
    std::filesystem::create_directories(game);
    CHECK(!MCContentUpdater::AwaitsInstall(*disk, game));
    REQUIRE(MCContentUpdater::Choose(*disk, game, WithHd).has_value());
    CHECK(MCContentUpdater::AwaitsInstall(*disk, game));

    // Another copy downloading it: this one waits.
    {
        const std::unique_ptr<MCUpdateLock> other = MCUpdateLock::Open(game, MCUpdateLock::StagingLock);
        REQUIRE(other != nullptr);
        REQUIRE(other->TryExclusive());
        MCFolderSource source(release.Host.Path());
        const auto held = MCContentUpdater::Install(setup, *disk, source, GoOn);
        REQUIRE(!held.has_value());
        CHECK(held.error().find("another copy") != std::string::npos);
    }

    // Cut off midway, then again: it resumes, and the swap puts release 2 in place.
    MCFolderSource cut(release.Host.Path());
    cut.CutAfter(SendBytes);
    REQUIRE(!MCContentUpdater::Install(setup, *disk, cut, GoOn).has_value());
    CHECK(MCContentUpdater::AwaitsInstall(*disk, game));
    MCFolderSource source(release.Host.Path());
    std::vector<std::pair<uint64_t, uint64_t>> progress;
    REQUIRE(MCContentUpdater::Install(setup, *disk, source,
                                      [&](uint64_t received, uint64_t expected)
                                      {
                                          progress.emplace_back(received, expected);
                                          return true;
                                      })
                .has_value());
    REQUIRE(!progress.empty());
    CHECK_EQ(progress.back().first, progress.back().second);
    CHECK(MCContentUpdater::AwaitsInstall(*disk, game));
    REQUIRE(MCContentSwap::Run(*disk, game).Outcome == MCContentSwap::MCOutcome::Swapped);
    CHECK(!MCContentUpdater::AwaitsInstall(*disk, game));
    release.CheckIsRelease2();
    CHECK(InstalledSets(release) == WithHd);

    // Without HD chosen, its files never come.
    std::filesystem::remove_all(game);
    std::filesystem::create_directories(game);
    REQUIRE(MCContentUpdater::Choose(*disk, game, WithoutHd).has_value());
    REQUIRE(MCContentUpdater::Install(setup, *disk, source, GoOn).has_value());
    REQUIRE(MCContentSwap::Run(*disk, game).Outcome == MCContentSwap::MCOutcome::Swapped);
    CHECK(!std::filesystem::exists(game / "HD"));
    CHECK_EQ(Read(game / "data" / "same.fit"), std::string("same in both"));

    // Content that needs a newer program than the one running isn't installed.
    std::filesystem::remove_all(game);
    std::filesystem::create_directories(game);
    REQUIRE(MCContentUpdater::Choose(*disk, game, WithoutHd).has_value());
    release.List.LowestProgram = NewerBuild;
    release.ListText = MCContentList::Write(release.List);
    Write(release.Host.Path() / "lists" / (MCSha256::Hex(MCSha256::Of(release.ListText)) + ".json"), release.ListText);
    release.Publish(2, 2, NewerBuild);
    const auto newer = MCContentUpdater::CheckInstall(setup, *disk, source);
    REQUIRE(!newer.has_value());
    CHECK(newer.error().find("newer than") != std::string::npos);
}

TEST_CASE("updater: HD OPTIONS' choice takes HD out at the next start, and a later start gets it back as chosen")
{
    MCHostedRelease release("updater-hd");
    const std::filesystem::path& game = release.Game.Path();
    UpdateToRelease2(release);

    {
        MCUpdaterScope scope(release, false);
        CHECK(MCUpdater::HdPack().Managed);
        CHECK(MCUpdater::HdPack().Installed);
        CHECK(MCUpdater::HdPack().Chosen);
        CHECK_EQ(MCUpdater::HdPack().Bytes, uint64_t{Terrain().size()});
        MCUpdater::CheckAtStart();
        CHECK(MCUpdater::HdPack().Hold == MCUpdater::MCHdHold::Looking);
        REQUIRE(FramesUntilIdle());
        CHECK(Stage() == MCStage::None);
        CHECK(MCUpdater::HdPack().Hold == MCUpdater::MCHdHold::None);
        CHECK(MCUpdater::HdPack().Stage == MCStage::None);

        MCUpdater::ChooseHd(false);
        CHECK(!MCUpdater::HdPack().Chosen);
        REQUIRE(FramesUntil([] { return MCUpdater::HdPack().Stage != MCStage::Downloading; }));
        CHECK(MCUpdater::HdPack().Stage == MCStage::Ready);
        CHECK(MCUpdater::HdPack().Hold == MCUpdater::MCHdHold::NextStart);
        CHECK(release.Armed());
        CHECK(StagingFree(game));

        // The UPDATE button stays as it was: this is no update.
        CHECK(Stage() == MCStage::None);
        CHECK(!MCUpdater::Waiting());

        // Chosen again this run: kept, for a later start.
        MCUpdater::ChooseHd(true);
        CHECK(MCUpdater::HdPack().Chosen);
        CHECK(!MCUpdater::Working());
        CHECK(MCContentUpdater::Chosen(*MakeDisk(), game) == WithHd);
    }

    // The next start: the swap takes HD out; the start's check finds no update, and HD comes back as chosen.
    REQUIRE(MCContentSwap::Run(*MakeDisk(), game).Outcome == MCContentSwap::MCOutcome::Swapped);
    CHECK(!std::filesystem::exists(game / "HD"));

    {
        MCUpdaterScope scope(release, false);
        CHECK(!MCUpdater::HdPack().Installed);
        CHECK(MCUpdater::HdPack().Chosen);
        MCUpdater::CheckAtStart();
        REQUIRE(FramesUntil([] { return MCUpdater::HdPack().Stage == MCStage::Ready; }));
        CHECK(MCUpdater::HdPack().Expected > 0);
        CHECK_EQ(MCUpdater::HdPack().Received, MCUpdater::HdPack().Expected);
    }

    REQUIRE(MCContentSwap::Run(*MakeDisk(), game).Outcome == MCContentSwap::MCOutcome::Swapped);
    CHECK(Read(game / "HD" / "terrain.hda") == Terrain());
    CHECK(InstalledSets(release) == WithHd);
}

TEST_CASE("updater: HD's choice changed while it downloads stops the download for the new choice")
{
    MCHostedRelease release("updater-hd-stop");
    const std::filesystem::path& game = release.Game.Path();
    UpdateToRelease2(release);
    ChangeSets(release, WithoutHd);
    const auto gate = std::make_shared<MCGate>();
    MCUpdaterScope scope(release, false,
                         [host = release.Host.Path(), gate] { return std::make_unique<MCHeldSource>(host, gate); });
    CHECK(!MCUpdater::HdPack().Installed);
    CHECK(!MCUpdater::HdPack().Chosen);

    MCUpdater::ChooseHd(true);
    REQUIRE(FramesUntil([&] { return gate->Reached.load(); }));
    CHECK(MCUpdater::HdPack().Stage == MCStage::Downloading);
    CHECK(MCUpdater::HdPack().Hold == MCUpdater::MCHdHold::None);

    MCUpdater::ChooseHd(false);
    CHECK(MCUpdater::HdPack().Stopping);
    REQUIRE(FramesUntilIdle());
    CHECK(MCUpdater::HdPack().Stage == MCStage::None);
    CHECK(!MCUpdater::HdPack().Stopping);
    CHECK(!MCUpdater::HdPack().Chosen);
    CHECK(!release.Armed());
    CHECK(StagingFree(game));
    CHECK(MCContentUpdater::Chosen(*MakeDisk(), game) == WithoutHd);
}

TEST_CASE("updater: with an update shown, HD's choice plans the update again with it; Sparkle lets its update go and "
          "finds it again")
{
    {
        MCHostedRelease release("updater-hd-update");
        MCUpdaterScope scope(release, false);
        CheckUntilFound(scope, std::nullopt);
        const uint64_t withHd = MCUpdater::Status().Expected;
        CHECK(!MCUpdater::HdPack().WithUpdate);

        MCUpdater::ChooseHd(false);
        REQUIRE(FramesUntil([] { return Stage() == MCStage::Found; }));
        CHECK(MCUpdater::Status().Expected < withHd);
        CHECK(MCUpdater::HdPack().WithUpdate);
        CHECK(MCUpdater::HdPack().Hold == MCUpdater::MCHdHold::None);

        MCUpdater::Answer(MCAnswer::Install);
        REQUIRE(FramesUntil([] { return Stage() != MCStage::Downloading; }));
        REQUIRE(Stage() == MCStage::Ready);
        CHECK(MCUpdater::HdPack().Hold == MCUpdater::MCHdHold::UpdateReady);
        REQUIRE(MCContentSwap::Run(*MakeDisk(), release.Game.Path()).Outcome == MCContentSwap::MCOutcome::Swapped);
        CHECK(!std::filesystem::exists(release.Game.Path() / "HD"));
        CHECK(InstalledSets(release) == WithoutHd);
    }

    {
        MCHostedRelease release("updater-hd-program");
        release.Publish(1, 2, NewerBuild);
        MCUpdaterScope scope(release, true);
        CheckUntilFound(scope, NewerBuild);

        // Sparkle is told to let its update go; the program is looked for again only once its session has ended, so
        // that end isn't taken for the new look's.
        MCUpdater::ChooseHd(false);
        REQUIRE(!scope.Program->Answers.empty());
        CHECK(scope.Program->Answers.back() == MCAnswer::Later);
        REQUIRE(FramesUntilIdle());
        CHECK_EQ(scope.Program->Checks.size(), size_t{1});
        MCUpdater::Closed();
        REQUIRE_EQ(scope.Program->Checks.size(), size_t{2});
        CHECK(scope.Program->Checks.back() == NewerBuild);
        MCUpdater::Found(std::format("{}.0 ({})", ReleaseVersion, NewerBuild), "Sparkle's own notes.");
        CHECK(Stage() == MCStage::Found);
        CHECK(MCUpdater::HdPack().WithUpdate);
    }
}

TEST_CASE("content updater: REPAIR gives up an update stopped partway and installs the release over the files as "
          "they are, keeping the player's file it set aside")
{
    MCHostedRelease release("repair");
    const std::filesystem::path& game = release.Game.Path();
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    MCFolderSource source(release.Host.Path());

    // The player's own mission where release 2 adds one; release 2 staged and armed.
    Write(game / "data" / "missions" / "new.fit", "the player's own mission");
    const auto offer = MCContentUpdater::Check(release.Setup(), *disk, source);
    REQUIRE(offer.has_value());
    REQUIRE(offer->has_value());
    REQUIRE(MCContentUpdater::Stage(release.Setup(), *disk, source, **offer, GoOn).has_value());
    REQUIRE(MCContentUpdater::Arm(release.Setup(), *disk, **offer).has_value());

    // Stopped partway: the old files and the player's mission set aside, the changed file moved in, the big file's
    // staged copy lost.
    const std::filesystem::path update = game / "Redux update";
    std::filesystem::rename(update / "ready.json", update / "journal.json");

    for (const std::string_view path :
         {"data/changed.fit", "HD/terrain.hda", "data/removed.fit", "data/missions/new.fit"})
    {
        const std::filesystem::path aside = update / "replaced" / path;
        std::filesystem::create_directories(aside.parent_path());
        std::filesystem::rename(game / path, aside);
    }

    std::filesystem::rename(update / "staged" / "data" / "changed.fit", game / "data" / "changed.fit");
    std::filesystem::remove(update / "staged" / "HD" / "terrain.hda");

    REQUIRE(MCContentUpdater::Repair(release.Setup(), *disk, source, GoOn).has_value());
    CHECK(MCContentSwap::Repairing(*disk, game));
    REQUIRE(MCContentSwap::Run(*disk, game).Outcome == MCContentSwap::MCOutcome::Swapped);
    CHECK(!MCContentSwap::Repairing(*disk, game));
    release.CheckIsRelease2();

    // The player's mission kept; the update's own files, and the release's already in place, recorded as placed.
    CHECK_EQ(Read(game / "Redux backup" / "data" / "missions" / "new.fit"), std::string("the player's own mission"));
    CHECK(!std::filesystem::exists(game / "Redux backup" / "data" / "changed.fit"));
    CHECK(!std::filesystem::exists(game / "Redux backup" / "HD"));
    const auto record = MCInstalled::Read(game);
    REQUIRE(record.has_value());

    for (const auto& [folded, file] : record->Files)
    {
        CHECK(file.Placed);
    }
}

TEST_CASE_ISOLATED("first start: the window shows the work's progress and ends with it; closing it stops the work")
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        std::cout << "  (skipped: no video: " << SDL_GetError() << ")\n";
        return;
    }

    // Halfway through 6 GB, saved as a shot with --shots.
    MCFirstStart::MCWindowOptions options{.Hidden = true};

    if (const std::optional<std::string_view> shots = MCTest::Option("shots"))
    {
        options.Shot = MCFileSystem::FromUtf8(*shots) / "first start window.bmp";
    }

    constexpr uint64_t Whole = 6'000'000'000;
    constexpr std::chrono::milliseconds Shown{200};
    const auto done = MCFirstStart::Run(
        "GETTING THE GAME READY",
        [&](MCFirstStart::MCProgress& progress, std::stop_token) -> std::expected<void, std::string>
        {
            progress.Set(Whole / 2, Whole);
            std::this_thread::sleep_for(Shown);
            return {};
        },
        options);

    if (!done && done.error().find("can't open") != std::string::npos)
    {
        std::cout << "  (skipped: " << done.error() << ")\n";
        SDL_Quit();
        return;
    }

    REQUIRE(done.has_value());
    CHECK(*done);

    // A failure comes back as it is.
    const auto failed = MCFirstStart::Run(
        "GETTING THE GAME READY", [](MCFirstStart::MCProgress&, std::stop_token) -> std::expected<void, std::string>
        { return std::unexpected("the host is out of reach"); }, {.Hidden = true});
    REQUIRE(!failed.has_value());
    CHECK_EQ(failed.error(), std::string("the host is out of reach"));

    // Closed (the system's quit): the work is asked to stop, and the player wasn't failed.
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    REQUIRE(SDL_PushEvent(&quit));
    bool stopped = false;
    const auto closed =
        MCFirstStart::Run("GETTING THE GAME READY",
                          [&](MCFirstStart::MCProgress&, std::stop_token stop) -> std::expected<void, std::string>
                          {
                              while (!stop.stop_requested())
                              {
                                  std::this_thread::sleep_for(LookAgain);
                              }

                              stopped = true;
                              return std::unexpected("stopped");
                          },
                          {.Hidden = true});
    REQUIRE(closed.has_value());
    CHECK(!*closed);
    CHECK(stopped);
    SDL_Quit();
}

TEST_CASE_ISOLATED("recovery screen: the question wraps its text over its buttons; Enter, the arrows and Escape press "
                   "them")
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        std::cout << "  (skipped: no video: " << SDL_GetError() << ")\n";
        return;
    }

    constexpr std::string_view Heading = "THE UPDATE STOPPED PARTWAY.";
    constexpr std::string_view Text =
        "HD/TERRAIN.HDA'S STAGED COPY IS MISSING OR THE WRONG SIZE; THEN HD/TAKEN/OUT.HDA CAN'T BE PUT BACK.\n\nCLOSE "
        "ANY PROGRAM USING THE GAME'S FOLDER, THEN TRY AGAIN. REPAIR DOWNLOADS ANY FILE THAT'S WRONG OR MISSING.";
    constexpr std::array<std::string_view, 3> Buttons = {"TRY AGAIN", "REPAIR", "QUIT"};

    // REPAIR lit, saved as a shot with --shots.
    MCFirstStart::MCWindowOptions options{.Hidden = true, .Answer = 1};

    if (const std::optional<std::string_view> shots = MCTest::Option("shots"))
    {
        options.Shot = MCFileSystem::FromUtf8(*shots) / "recovery screen.bmp";
    }

    const std::optional<size_t> shown = MCFirstStart::Ask(Heading, Text, Buttons, options);

    if (!shown)
    {
        std::cout << "  (skipped: the window can't open)\n";
        SDL_Quit();
        return;
    }

    CHECK_EQ(*shown, size_t{1});

    const auto pressed = [&](std::initializer_list<SDL_Keycode> keys)
    {
        for (const SDL_Keycode key : keys)
        {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = key;
            event.key.down = true;
            REQUIRE(SDL_PushEvent(&event));
        }

        return MCFirstStart::Ask(Heading, Text, Buttons, {.Hidden = true});
    };

    CHECK(pressed({SDLK_RETURN}) == std::optional<size_t>(0));
    CHECK(pressed({SDLK_RIGHT, SDLK_RETURN}) == std::optional<size_t>(1));
    CHECK(pressed({SDLK_LEFT, SDLK_RETURN}) == std::optional<size_t>(2));
    CHECK(pressed({SDLK_ESCAPE}) == std::optional<size_t>(2));

    // Closed: no answer.
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    REQUIRE(SDL_PushEvent(&quit));
    CHECK(!MCFirstStart::Ask(Heading, Text, Buttons, {.Hidden = true}).has_value());
    SDL_Quit();
}

namespace
{
    /// <summary>HD OPTIONS' HD PACK row: first under PACKS.</summary>
    constexpr int32_t HdPackRow = 1;

    /// <summary>Runs a few frames, as a player's screen would between inputs.</summary>
    void ScreenFrames()
    {
        constexpr int32_t frames = 3;

        for (int32_t frame = 0; frame < frames; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }

    /// <summary>
    /// The preferences' HD OPTIONS page open, once the main menu's check at the start found nothing newer (until then
    /// HD PACK waits for it); its list.
    /// </summary>
    std::shared_ptr<MCLogOptionList> OpenHdOptions()
    {
        ShowPreferences();
        ShowHdOptions();
        ScreenFrames();
        const std::shared_ptr<MCLogOptionList> list = HdOptionsList();
        REQUIRE(list != nullptr);
        REQUIRE_EQ(list->Label(HdPackRow), std::string_view("HD PACK"));
        CHECK_EQ(list->HeldReason(HdPackRow), std::string_view("LOOKING FOR AN UPDATE."));
        REQUIRE(FramesUntilIdle());
        ScreenFrames();
        CHECK(list->HeldReason(HdPackRow).empty());
        return list;
    }

    /// <summary>Whether the preferences' button <paramref name="label"/> is showing.</summary>
    bool ButtonShown(std::string_view label)
    {
        const std::shared_ptr<MCLogTextButton> button = MCScreenInput::FindTextButton(*GlobalLog()->PrefScreen, label);
        return button != nullptr && button->IsShowing();
    }

    /// <summary>The preferences' button <paramref name="label"/>, which must be showing.</summary>
    std::shared_ptr<MCLogTextButton> ShownButton(std::string_view label)
    {
        std::shared_ptr<MCLogTextButton> button = MCScreenInput::FindTextButton(*GlobalLog()->PrefScreen, label);
        REQUIRE(button != nullptr);
        CHECK(button->IsShowing());
        return button;
    }

    /// <summary>Clicks the middle of <paramref name="object"/> as a player does.</summary>
    void RealClickOn(MCGuiObject& object)
    {
        MCScreenInput::RealClick(object.GlobalX() + object.Width() / 2, object.GlobalY() + object.Height() / 2);
        ScreenFrames();
    }

    /// <summary>Runs the game's frames until the HD pack's change is <paramref name="stage"/>.</summary>
    bool ScreenFramesUntil(MCStage stage)
    {
        return FramesUntil(
            [&]
            {
                MCTestGame::RunFrame(1.0f / 15.0f);
                return MCUpdater::HdPack().Stage == stage;
            });
    }
}

TEST_CASE_ISOLATED("game: HD OPTIONS' HD PACK button gets HD with its size shown, then RESTART puts it in")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCHostedRelease release("hd-options-get");
    UpdateToRelease2(release);
    ChangeSets(release, WithoutHd);
    MCUpdaterScope scope(release, false);
    REQUIRE(MCTestGame::StartLogistics());
    const std::shared_ptr<MCLogOptionList> list = OpenHdOptions();
    CHECK(list->Description(HdPackRow).find("GB TO DOWNLOAD.") != std::string::npos);
    const std::shared_ptr<MCLogTextButton> get = ShownButton("GET");
    CHECK(!ButtonShown("RESTART"));
    MCScreenInput::SaveShot("hd options get", MCScreenInput::ScreenHash());

    // GET downloads it at once, while the game goes on; RESTART then.
    RealClickOn(*get);
    CHECK(MCUpdater::HdPack().Chosen);
    REQUIRE(ScreenFramesUntil(MCStage::Ready));
    ScreenFrames();
    CHECK(release.Armed());
    CHECK_EQ(list->Description(HdPackRow), std::string("DOWNLOADED. RESTART THE GAME TO SEE IT."));
    CHECK(!get->IsShowing());
    const std::shared_ptr<MCLogTextButton> restart = ShownButton("RESTART");
    MCScreenInput::SaveShot("hd options restart", MCScreenInput::ScreenHash());
    RealClickOn(*restart);
    CHECK_EQ(*scope.Quits, 1);
    CHECK(MCUpdater::RestartWanted());
}

TEST_CASE_ISOLATED("game: HD OPTIONS' REMOVE asks first, then RESTART takes HD out")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCHostedRelease release("hd-options-remove");
    UpdateToRelease2(release);
    MCUpdaterScope scope(release, false);
    REQUIRE(MCTestGame::StartLogistics());
    const std::shared_ptr<MCLogOptionList> list = OpenHdOptions();
    CHECK(list->Description(HdPackRow).find("GB).") != std::string::npos);
    const std::shared_ptr<MCLogTextButton> remove = ShownButton("REMOVE");

    // Asked first: CANCEL keeps it.
    MCReusableDialog& question = *GlobalLog()->QuestionDialog;
    RealClickOn(*remove);
    REQUIRE(question.IsShowing());
    MCScreenInput::SaveShot("hd options remove question", MCScreenInput::ScreenHash());
    RealClickOn(*question.CancelButton);
    CHECK(!question.IsShowing());
    CHECK(MCUpdater::HdPack().Chosen);
    CHECK(!MCUpdater::Working());

    // OK: it goes at the restart.
    RealClickOn(*remove);
    REQUIRE(question.IsShowing());
    RealClickOn(*question.OkButton);
    CHECK(!MCUpdater::HdPack().Chosen);
    REQUIRE(ScreenFramesUntil(MCStage::Ready));
    ScreenFrames();
    CHECK_EQ(list->Description(HdPackRow), std::string("RESTART THE GAME TO REMOVE IT."));
    CHECK(ButtonShown("RESTART"));
    CHECK(!ButtonShown("REMOVE"));
}

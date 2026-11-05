#include "stdafx.h"
#include "MCContentFixture.h"
#include "MCFakeUpdaterBackend.h"
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
#include "update/MCContentRemoval.h"
#include "update/MCContentSwap.h"
#include "update/MCContentUpdater.h"
#include "update/MCSetupBackend.h"
#include "update/MCSyncedFolder.h"
#include "update/MCUpdatePlace.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateLock.h"

#include <condition_variable>
#include <mutex>
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
                                               const std::function<bool(std::span<const char>)>& received) override
        {
            _Gate->Reached = true;

            while (!_Gate->Open)
            {
                if (!received({}))
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

    /// <summary>A transfer stalled on the network: its downloads wait, saying nothing, until it's cancelled.</summary>
    struct MCStall
    {
        std::mutex Mutex;
        std::condition_variable Changed;
        bool Reached = false;
        bool Cancelled = false;
    };

    /// <summary>The release's files hosted in a folder, each download stalled until the transfer is cancelled.</summary>
    class MCStalledSource final : public MCContentStage::MCSource
    {
    public:
        MCStalledSource(std::filesystem::path folder, std::shared_ptr<MCStall> stall)
            : _Inner(std::move(folder)), _Stall(std::move(stall))
        {
        }

        std::expected<void, std::string> Fetch(std::string_view, const std::filesystem::path&, uint64_t,
                                               const std::function<bool(std::span<const char>)>&) override
        {
            std::unique_lock lock(_Stall->Mutex);
            _Stall->Reached = true;
            _Stall->Changed.notify_all();

            // Never the bytes callback: only a cancel ends it (or the test's own wait, failing it).
            if (!_Stall->Changed.wait_for(lock, WorkWait, [&] { return _Stall->Cancelled; }))
            {
                return std::unexpected("the stalled download was never cancelled");
            }

            return std::unexpected("stopped");
        }

        std::expected<std::string, std::string> Read(std::string_view name, size_t most) override
        {
            return _Inner.Read(name, most);
        }

        void Cancel() override
        {
            const std::lock_guard lock(_Stall->Mutex);
            _Stall->Cancelled = true;
            _Stall->Changed.notify_all();
        }

    private:
        MCFolderSource _Inner;
        std::shared_ptr<MCStall> _Stall;
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
                MCUpdater::SetBackend(std::make_unique<MCTestUpdater::MCFakeBackend>(Program, RunningShown));
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

        std::shared_ptr<MCTestUpdater::MCBackendLog> Program = std::make_shared<MCTestUpdater::MCBackendLog>();
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
    REQUIRE(first->TryShared());
    CHECK(!second->TryExclusive());
    CHECK(second->TryShared());
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
    REQUIRE(running->TryShared());

    // Another copy runs on (past the start's deadline): the update waits, and the start plays the files as they are,
    // holding the lock shared.
    constexpr std::chrono::milliseconds shortWait{300};
    const MCContentSwap::MCResult held = MCContentSwap::RunAtStart(*disk, game, starting.get(), RunningBuild,
                                                                   std::chrono::steady_clock::now() + shortWait);
    CHECK(held.Outcome == MCContentSwap::MCOutcome::PutBack);
    CHECK(held.Error.find("another copy") != std::string::npos);
    CHECK(MCContentSwap::Waiting(*disk, game));
    CHECK_EQ(Read(game / "data" / "changed.fit"), std::string("changed v1"));
    running->Release();
    CHECK(!running->TryExclusive());
    starting->Release();

    // A swap stopped midway, with another copy still holding the lock: waited for, then stuck past the deadline.
    REQUIRE(disk->Rename(game / "Redux update" / "ready.json", game / "Redux update" / "journal.json"));
    REQUIRE(running->TryShared());
    const MCContentSwap::MCResult stuck = MCContentSwap::RunAtStart(*disk, game, starting.get(), RunningBuild,
                                                                    std::chrono::steady_clock::now() + shortWait);
    CHECK(stuck.Outcome == MCContentSwap::MCOutcome::Stuck);
    running->Release();
    starting->Release();

    // Alone, the start finishes it, and holds the lock shared: another copy runs beside it, none swaps under it.
    CHECK(MCContentSwap::RunAtStart(*disk, game, starting.get(), RunningBuild).Outcome ==
          MCContentSwap::MCOutcome::Swapped);
    release.CheckIsRelease2();
    CHECK(!running->TryExclusive());
    CHECK(running->TryShared());
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
    REQUIRE(quitting->TryShared());
    constexpr std::chrono::milliseconds closing{200};
    std::jthread ends(
        [&]
        {
            std::this_thread::sleep_for(closing);
            quitting->Release();
        });
    CHECK(MCContentSwap::RunAtStart(*disk, game, starting.get(), RunningBuild).Outcome ==
          MCContentSwap::MCOutcome::Swapped);
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

TEST_CASE("content updater: a copy with no record hashes its files once; later checks take the hashes kept while each "
          "file stays as found")
{
    MCHostedRelease release("hashes");
    const std::filesystem::path& game = release.Game.Path();
    const std::filesystem::path hashesFile = game / MCFileSystem::FromUtf8(MCContentPlan::HashesFile);
    std::filesystem::remove(game / "Redux update" / "installed.json");
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    MCFolderSource source(release.Host.Path());
    const MCContentUpdater::MCSetup setup = release.Setup();
    const auto hashedAtCheck = [&]()
    {
        const auto offer = MCContentUpdater::Check(setup, *disk, source);
        REQUIRE(offer.has_value());
        REQUIRE(offer->has_value());
        REQUIRE((*offer)->Content != nullptr);
        return (*offer)->Content->Plan.HashedFiles;
    };

    // The three listed files the copy has (release 1's) hashed at the first check, none at the next.
    CHECK_EQ(hashedAtCheck(), size_t{3});
    CHECK_EQ(hashedAtCheck(), size_t{0});

    // One whose time moved on is hashed again, alone.
    const std::filesystem::path same = game / "data" / "same.fit";
    std::filesystem::last_write_time(same, std::filesystem::last_write_time(same) + std::chrono::seconds(5));
    CHECK_EQ(hashedAtCheck(), size_t{1});
    CHECK_EQ(hashedAtCheck(), size_t{0});

    // A file gone keeps no hash.
    std::filesystem::remove(game / "data" / "changed.fit");
    CHECK_EQ(hashedAtCheck(), size_t{0});
    const auto kept = MCContentPlan::ReadHashes(Read(hashesFile));
    REQUIRE(kept.has_value());
    CHECK_EQ(kept->size(), size_t{2});
    CHECK(!kept->contains("data/changed.fit"));
    CHECK(kept->contains("data/same.fit"));

    // Hashes that can't be read start again.
    Write(hashesFile, "not json");
    CHECK_EQ(hashedAtCheck(), size_t{2});
    CHECK_EQ(hashedAtCheck(), size_t{0});
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

    // The program's installer armed: the content is armed with it, on the work's thread (the frame never waits for its
    // journal's writes), and READY only once it is; the program's ready answer waits.
    MCUpdater::Ready();
    CHECK(Stage() == MCStage::Downloading);
    CHECK(MCUpdater::Working());
    REQUIRE(FramesUntil([] { return Stage() != MCStage::Downloading; }));
    CHECK(Stage() == MCStage::Ready);
    CHECK_EQ(scope.Program->Answers.size(), size_t{1});
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
    const bool ended = FramesUntil([] { return Stage() != MCStage::Downloading; });
    std::filesystem::permissions(update, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
    REQUIRE(ended);
    CHECK(scope.Program->Answers.back() == MCAnswer::Skip);
    CHECK(Stage() == MCStage::Failed);
    CHECK(!release.Armed());
    CHECK(StagingFree(release.Game.Path()));
}

TEST_CASE("updater: quitting while the content is armed waits for it; content that can't be armed cancels the "
          "program's install")
{
    MCHostedRelease release("arm-fails-quitting");
    release.Publish(1, 2, NewerBuild);
    MCUpdaterScope scope(release, true);
    CheckUntilFound(scope, NewerBuild);
    MCUpdater::Answer(MCAnswer::Install);
    REQUIRE(FramesUntil([&] { return !scope.Program->Answers.empty(); }));
    const std::filesystem::path update = release.Game.Path() / "Redux update";
    std::filesystem::permissions(update, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove);
    MCUpdater::Ready();
    MCUpdater::Quit();
    std::filesystem::permissions(update, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
    CHECK(scope.Program->Answers.back() == MCAnswer::Skip);
    CHECK(!MCUpdater::Working());
    CHECK(!release.Armed());
    CHECK(StagingFree(release.Game.Path()));
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

TEST_CASE("updater: the program alone holds the staging lock from INSTALL until READY; another copy installing it "
          "fails INSTALL, saying so")
{
    MCHostedRelease release("program-alone");
    const std::filesystem::path& game = release.Game.Path();
    UpdateToRelease2(release);
    release.Publish(2, 2, NewerBuild);

    {
        MCUpdaterScope scope(release, true);
        CheckUntilFound(scope, NewerBuild);
        CHECK_EQ(MCUpdater::Status().Expected, ProgramSize);
        const std::unique_ptr<MCUpdateLock> other = MCUpdateLock::Open(game, MCUpdateLock::StagingLock);
        REQUIRE(other != nullptr);
        REQUIRE(other->TryExclusive());
        MCUpdater::Answer(MCAnswer::Install);
        CHECK(Stage() == MCStage::Failed);
        CHECK(MCUpdater::Status().Error.find("another copy") != std::string::npos);
        REQUIRE(!scope.Program->Answers.empty());
        CHECK(scope.Program->Answers.back() == MCAnswer::Later);
    }

    MCUpdaterScope scope(release, true);
    CheckUntilFound(scope, NewerBuild);
    MCUpdater::Answer(MCAnswer::Install);
    REQUIRE(!scope.Program->Answers.empty());
    CHECK(scope.Program->Answers.back() == MCAnswer::Install);
    CHECK(!StagingFree(game));
    MCUpdater::Ready();
    REQUIRE(FramesUntil([] { return Stage() != MCStage::Downloading; }));
    CHECK(Stage() == MCStage::Ready);
    CHECK(StagingFree(game));
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

#ifndef _WIN32
TEST_CASE("updater: Sparkle installing without its ready step with content that can't be armed: the update stays "
          "failed")
{
    MCHostedRelease release("installs-at-once-arm-fails");
    release.Publish(1, 2, NewerBuild);
    MCUpdaterScope scope(release, true);
    CheckUntilFound(scope, NewerBuild);
    MCUpdater::Answer(MCAnswer::Install);
    REQUIRE(FramesUntil([&] { return !scope.Program->Answers.empty(); }));
    const std::filesystem::path update = release.Game.Path() / "Redux update";
    std::filesystem::permissions(update, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove);
    MCUpdater::Installing();
    std::filesystem::permissions(update, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
    CHECK(Stage() == MCStage::Failed);
    CHECK(!MCUpdater::Status().Error.empty());
    CHECK(!release.Armed());
    CHECK(StagingFree(release.Game.Path()));
}
#endif

TEST_CASE("updater: Sparkle ending its session while the update is shown fails it rather than wait; INSTALL then "
          "looks again")
{
    MCHostedRelease release("session-ends");
    release.Publish(1, 2, NewerBuild);
    MCUpdaterScope scope(release, true);
    CheckUntilFound(scope, NewerBuild);
    MCUpdater::Closed();
    CHECK(Stage() == MCStage::Failed);
    CHECK(scope.Program->Answers.empty());
    CHECK(!release.Armed());

    // INSTALL: the check again, and the program's backend asked for its build again; nothing passed on to it yet.
    MCUpdater::Answer(MCAnswer::Install);
    CHECK(Stage() == MCStage::Downloading);
    REQUIRE(FramesUntil([&] { return scope.Program->Checks.size() == 2; }));
    CHECK(scope.Program->Checks.back() == NewerBuild);
    CHECK(scope.Program->Answers.empty());
    CHECK(Stage() == MCStage::Downloading);
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
    Write(game / "Redux update" / "sets.json", R"({"format": 1, "sets": ["game", 2]})");
    CHECK(!MCContentUpdater::Chosen(*disk, game).has_value());
    CHECK(MCContentUpdater::Wanted(*disk, game, *MCInstalled::Read(game)) == WithoutHd);
}

TEST_CASE("content updater: lists are kept: read again unfetched, fetched again when damaged, HD removed offline; only "
          "the installed release's and the newest stay")
{
    MCHostedRelease release("kept-lists");
    const std::filesystem::path& game = release.Game.Path();
    const std::filesystem::path lists = game / MCFileSystem::FromUtf8(MCContentUpdater::KeptListsFolder);
    const std::string listName = MCSha256::Hex(MCSha256::Of(release.ListText)) + ".json";
    const std::string hostedList = std::format("{}/{}", MCContentUpdater::ListsFolder, listName);
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    const MCContentUpdater::MCSetup setup = release.Setup();
    const auto listsKept = [&]() { return std::distance(std::filesystem::directory_iterator(lists), {}); };
    const auto fetchesAtCheck = [&]()
    {
        MCFolderSource source(release.Host.Path());
        const auto offer = MCContentUpdater::Check(setup, *disk, source);
        REQUIRE(offer.has_value());
        REQUIRE(offer->has_value());
        return std::ranges::count(source.Asked(), hostedList);
    };

    // Kept before: the installed release's list, and an older one.
    const std::string installedName = MCSha256::Hex(release.Installed.List) + ".json";
    Write(lists / installedName, "release 1's list");
    Write(lists / (MCSha256::Hex(MCSha256::Of("an older list")) + ".json"), "an older list");

    // Fetched once and kept, beside the installed release's alone; read from there while the update waits.
    CHECK_EQ(fetchesAtCheck(), 1);
    CHECK_EQ(Read(lists / listName), release.ListText);
    CHECK(std::filesystem::exists(lists / installedName));
    CHECK_EQ(listsKept(), 2);
    CHECK_EQ(fetchesAtCheck(), 0);

    // One kept that isn't what its name says is fetched again.
    Write(lists / listName, "damaged");
    CHECK_EQ(fetchesAtCheck(), 1);
    CHECK_EQ(Read(lists / listName), release.ListText);

    // Installed, HD taken out, then got again: planned with the host out of reach, from the installed release's list
    // kept (only its download needs the host).
    UpdateToRelease2(release);
    ChangeSets(release, WithoutHd);
    REQUIRE(MCContentUpdater::Choose(*disk, game, WithHd).has_value());
    MCFolderSource offline(release.Host.Path() / "out-of-reach");
    const auto addition = MCContentUpdater::CheckSets(setup, *disk, offline);
    REQUIRE(addition.has_value());
    REQUIRE(addition->has_value());
    CHECK(offline.Asked().empty());
    CHECK((*addition)->ContentBytes() > 0);
}

TEST_CASE("content updater: HD OPTIONS' REMOVE is planned from the record alone, with no host and no list kept")
{
    MCHostedRelease release("remove-offline");
    const std::filesystem::path& game = release.Game.Path();
    const std::filesystem::path lists = game / MCFileSystem::FromUtf8(MCContentUpdater::KeptListsFolder);
    UpdateToRelease2(release);
    std::filesystem::remove_all(lists);
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    const MCContentUpdater::MCSetup setup = release.Setup();

    REQUIRE(MCContentUpdater::Choose(*disk, game, WithoutHd).has_value());
    MCFolderSource offline(release.Host.Path() / "out-of-reach");
    const auto removal = MCContentUpdater::CheckSets(setup, *disk, offline);
    REQUIRE(removal.has_value());
    REQUIRE(removal->has_value());
    CHECK_EQ((*removal)->ContentBytes(), uint64_t{0});
    REQUIRE(MCContentUpdater::Stage(setup, *disk, offline, **removal, GoOn).has_value());
    REQUIRE(MCContentUpdater::Arm(setup, *disk, **removal).has_value());
    CHECK(offline.Asked().empty());
    CHECK(!std::filesystem::exists(lists));
    REQUIRE(MCContentSwap::Run(*disk, game).Outcome == MCContentSwap::MCOutcome::Swapped);
    CHECK(!std::filesystem::exists(game / "HD"));

    // The game's files stay as recorded, with the release, its list and HD's size, for GET.
    const auto record = MCInstalled::Read(game);
    REQUIRE(record.has_value());
    CHECK(record->Sets == WithoutHd);
    CHECK_EQ(record->Files.size(), size_t{3});
    CHECK(!record->Files.contains("HD/TERRAIN.HDA"));
    CHECK_EQ(record->Release, uint64_t{2});
    CHECK(record->List == MCSha256::Of(release.ListText));
    CHECK_EQ(record->SetBytes.at("hd"), uint64_t{Terrain().size()});
    CHECK_EQ(Read(game / "data" / "same.fit"), std::string("same in both"));

    // Got again, from the host.
    ChangeSets(release, WithHd);
    CHECK(Read(game / "HD" / "terrain.hda") == Terrain());
    CHECK(InstalledSets(release) == WithHd);
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

namespace
{
    /// <summary>The Windows setup a release names, as hosted, and the arguments that run it silently.</summary>
    constexpr std::string_view ProgramsAddress = "https://example.test/programs";
    constexpr std::string_view SetupName = "MechCommander-Redux-Setup-600.exe";
    constexpr std::string_view SetupArguments = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART";
    constexpr std::string_view SetupBytes = "a stand-in for the setup";
    constexpr std::string_view Blocked = "Windows blocked the update (Smart App Control, error 4551)";

    /// <summary>release.json naming <paramref name="setup"/> as Windows' program for build 600 (its hash, unless given).</summary>
    void PublishSetup(const MCHostedRelease& release, std::string_view setup,
                      std::optional<MCSha256::MCDigest> hash = std::nullopt)
    {
        Write(release.Host.Path() / "programs" / SetupName, setup);
        MCReleaseFile::MCRelease entry{.Number = 2,
                                       .Version = std::string(ReleaseVersion),
                                       .Notes = std::string(ReleaseNotes),
                                       .List = MCSha256::Of(release.ListText),
                                       .ListSize = release.ListText.size()};
        entry.Programs["windows"] = {.Build = NewerBuild,
                                     .Size = setup.size(),
                                     .Sha256 = hash.value_or(MCSha256::Of(setup)),
                                     .Url = std::format("{}/{}", ProgramsAddress, SetupName),
                                     .Silent = std::string(SetupArguments)};
        const MCReleaseFile::MCReleases releases{.Format = 1, .Issue = 1, .Current = entry};
        Write(release.Host.Path() / "release.json",
              release.ReleaseKey.Envelope(MCSignedFile::ReleaseType, MCReleaseFile::ReleasesPayload(releases)));
    }

    /// <summary>What the stand-in for Windows was asked: the setups judged and started, the quits.</summary>
    struct MCSetupLog
    {
        std::vector<std::filesystem::path> Judged;
        std::vector<std::tuple<std::filesystem::path, std::string, bool>> Started;
        int32_t Quits = 0;
        /// <summary>Why it won't run a setup (empty: it runs it).</summary>
        std::string Refusal;
    };

    MCSetupBackend::MCPlatform FakeWindows(const std::shared_ptr<MCSetupLog>& log)
    {
        return {.Runnable =
                    [log](const std::filesystem::path& setup)
                {
                    log->Judged.push_back(setup);
                    return log->Refusal;
                },
                .Start =
                    [log](const std::filesystem::path& setup, std::string_view arguments, bool restart)
                {
                    log->Started.emplace_back(setup, std::string(arguments), restart);
                    return std::string();
                },
                .Quit = [log] { log->Quits++; }};
    }

    /// <summary>The updater on Windows for the test's length: the content's, and the setup's backend.</summary>
    class MCSetupScope
    {
    public:
        explicit MCSetupScope(const MCHostedRelease& release)
        {
            MCSetupBackend::MCSourceFor programs = [host = release.Host.Path()](std::string_view folder)
                -> std::expected<std::unique_ptr<MCContentStage::MCSource>, std::string>
            {
                if (folder != ProgramsAddress)
                {
                    return std::unexpected(std::format("{} isn't hosted", folder));
                }

                return std::make_unique<MCFolderSource>(host / "programs");
            };

            MCContentUpdater::MCSetup setup = release.Setup();
            setup.Platform = "windows";
            MCUpdater::SetBackend(
                MCSetupBackend::Make(release.Game.Path(), std::string(RunningShown), programs, FakeWindows(Log)));
            MCUpdater::SetContent(setup,
                                  [host = release.Host.Path()] { return std::make_unique<MCFolderSource>(host); });
        }

        ~MCSetupScope()
        {
            MCUpdater::ClearContent();
            MCUpdater::SetBackend(nullptr);
        }

        MCSetupScope(const MCSetupScope&) = delete;
        MCSetupScope& operator=(const MCSetupScope&) = delete;

        std::shared_ptr<MCSetupLog> Log = std::make_shared<MCSetupLog>();
    };

    std::filesystem::path Downloaded(const MCHostedRelease& release)
    {
        return release.Game.Path() / "Redux update" / "program" / "setup-600.exe";
    }

    std::filesystem::path ArmedRecord(const MCHostedRelease& release)
    {
        return release.Game.Path() / "Redux update" / "program" / "armed.json";
    }

    /// <summary>The update found, then INSTALL: on to READY, or a failure.</summary>
    void InstallUntilDone()
    {
        MCUpdater::CheckAtStart();
        REQUIRE(FramesUntil([] { return Stage() == MCStage::Found; }));
        MCUpdater::Answer(MCAnswer::Install);
        REQUIRE(FramesUntil([] { return Stage() == MCStage::Ready || Stage() == MCStage::Failed; }));
    }
}

TEST_CASE("updater on Windows: the setup release.json names is downloaded, checked and armed with the content; RESTART "
          "runs it as the game ends, asking it to start the game again")
{
    MCHostedRelease release("setup-backend");
    PublishSetup(release, SetupBytes);
    MCSetupScope scope(release);
    MCUpdater::CheckAtStart();
    REQUIRE(FramesUntil([] { return Stage() == MCStage::Found; }));
    CHECK_EQ(MCUpdater::Status().Version, std::string("0.2.2 (600)"));
    CHECK_EQ(MCUpdater::Status().Expected, release.ContentBytes() + SetupBytes.size());

    MCUpdater::Answer(MCAnswer::Install);
    REQUIRE(FramesUntil([] { return Stage() == MCStage::Ready || Stage() == MCStage::Failed; }));
    REQUIRE(Stage() == MCStage::Ready);
    CHECK(release.Armed());
    CHECK_EQ(Read(Downloaded(release)), std::string(SetupBytes));
    CHECK(std::filesystem::exists(ArmedRecord(release)));
    CHECK(scope.Log->Judged == std::vector{Downloaded(release)});

    // RESTART: the game quits, and the setup starts as it ends.
    MCUpdater::Answer(MCAnswer::Install);
    CHECK(Stage() == MCStage::Installing);
    CHECK_EQ(scope.Log->Quits, 1);
    CHECK(scope.Log->Started.empty());
    MCUpdater::Quit();
    REQUIRE_EQ(scope.Log->Started.size(), size_t{1});
    CHECK(std::get<0>(scope.Log->Started.front()) == Downloaded(release));
    CHECK_EQ(std::get<1>(scope.Log->Started.front()), std::string(SetupArguments));
    CHECK(std::get<2>(scope.Log->Started.front()));
    CHECK(!std::filesystem::exists(ArmedRecord(release)));
}

TEST_CASE("updater on Windows: a setup that isn't release.json's, or that Windows won't run, fails the update, saying "
          "why; nothing is armed")
{
    {
        MCHostedRelease release("setup-not-named");
        PublishSetup(release, SetupBytes, MCSha256::Of("another setup"));
        MCSetupScope scope(release);
        InstallUntilDone();
        CHECK(Stage() == MCStage::Failed);
        CHECK(MCUpdater::Status().Error.find("isn't the one release.json names") != std::string::npos);
        CHECK(!release.Armed());
        CHECK(!std::filesystem::exists(Downloaded(release)));
        CHECK(scope.Log->Judged.empty());
        MCUpdater::Quit();
        CHECK(scope.Log->Started.empty());
    }

    MCHostedRelease release("setup-blocked");
    PublishSetup(release, SetupBytes);
    MCSetupScope scope(release);
    scope.Log->Refusal = Blocked;
    InstallUntilDone();
    CHECK(Stage() == MCStage::Failed);
    CHECK_EQ(MCUpdater::Status().Error, std::string(Blocked));
    CHECK(!release.Armed());
    CHECK(!std::filesystem::exists(ArmedRecord(release)));
    MCUpdater::Quit();
    CHECK(scope.Log->Started.empty());
}

TEST_CASE("updater on Windows: quitting with the update ready runs the setup; one armed and never run (the game ended "
          "first) runs at the next start, once, when no other copy runs; once in, its files go")
{
    MCHostedRelease release("setup-quit");
    PublishSetup(release, SetupBytes);

    {
        MCSetupScope scope(release);
        InstallUntilDone();
        REQUIRE(Stage() == MCStage::Ready);
        MCUpdater::Quit();
        REQUIRE_EQ(scope.Log->Started.size(), size_t{1});
        CHECK(!std::get<2>(scope.Log->Started.front()));
    }

    // Armed, and the game ended before it quit: the next start runs it, asking for the game again; a start after that
    // doesn't (it ran once).
    std::filesystem::remove_all(release.Game.Path() / "Redux update");
    Write(release.Game.Path() / "Redux update" / "installed.json", MCInstalled::Text(release.Installed));

    {
        MCSetupScope scope(release);
        InstallUntilDone();
        REQUIRE(Stage() == MCStage::Ready);
    }

    REQUIRE(std::filesystem::exists(ArmedRecord(release)));
    const auto log = std::make_shared<MCSetupLog>();

    // Another copy running (at its own READY, or playing on): the setup waits for it to quit.
    {
        const std::unique_ptr<MCUpdateLock> running = MCUpdateLock::Open(release.Game.Path(), MCUpdateLock::GameLock);
        REQUIRE(running != nullptr);
        REQUIRE(running->TryShared());
        CHECK(!MCSetupBackend::RunLeftOver(release.Game.Path(), RunningBuild, FakeWindows(log)));
        CHECK(log->Started.empty());
        CHECK(std::filesystem::exists(ArmedRecord(release)));
    }

    // Alone, it runs, the game's lock held exclusive until the setup has started: no copy plays meanwhile.
    MCSetupBackend::MCPlatform probing = FakeWindows(log);
    bool heldWhileStarting = false;
    probing.Start = [start = probing.Start, &heldWhileStarting, &release](const std::filesystem::path& setup,
                                                                          std::string_view arguments, bool restart)
    {
        const std::unique_ptr<MCUpdateLock> other = MCUpdateLock::Open(release.Game.Path(), MCUpdateLock::GameLock);
        heldWhileStarting = other != nullptr && !other->TryShared();
        return start(setup, arguments, restart);
    };

    CHECK(MCSetupBackend::RunLeftOver(release.Game.Path(), RunningBuild, probing));
    CHECK(heldWhileStarting);
    REQUIRE_EQ(log->Started.size(), size_t{1});
    CHECK(std::get<0>(log->Started.front()) == Downloaded(release));
    CHECK(std::get<2>(log->Started.front()));
    CHECK(!MCSetupBackend::RunLeftOver(release.Game.Path(), RunningBuild, FakeWindows(log)));
    CHECK_EQ(log->Started.size(), size_t{1});
    CHECK(std::filesystem::exists(Downloaded(release)));

    // The new build's start: the setup's folder goes.
    CHECK(!MCSetupBackend::RunLeftOver(release.Game.Path(), NewerBuild, FakeWindows(log)));
    CHECK(!std::filesystem::exists(release.Game.Path() / "Redux update" / "program"));
}

TEST_CASE("update place: every path an update installs, stages, downloads and sets aside is held to Windows' limit; "
          "a folder no client syncs passes")
{
    MCHostedRelease release("place");
    const std::filesystem::path& game = release.Game.Path();
    const MCContentPlan::MCPlan plan = release.Plan();
    CHECK(MCUpdatePlace::FitPaths(game, plan, MCUpdatePlace::WindowsFileLimit, MCUpdatePlace::WindowsFolderLimit));

    // The patch's download is the longest: a limit just under it refuses the plan, naming it.
    const std::string patch = MCFileSystem::ToUtf8(std::filesystem::absolute(game) / "Redux update" / "download" /
                                                   "patches" / "terrain-1-2.hdiff");
    const auto under = MCUpdatePlace::FitPaths(game, plan, patch.size() - 1, patch.size());
    REQUIRE(!under.has_value());
    CHECK(under.error().find("terrain-1-2.hdiff's path is too long for Windows") != std::string::npos);
    CHECK(MCUpdatePlace::FitPaths(game, plan, patch.size(), patch.size()));
    CHECK(!MCSyncedBy(game).has_value());
}

TEST_CASE("content removal: the uninstaller's removal takes what updates placed, leaves what a copy taken over had in "
          "place, its backup and the player's own, and waits for running copies")
{
    MCHostedRelease release("removal");
    const std::filesystem::path& game = release.Game.Path();
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();

    // The player's copy, taken over: one file as the release has it, one the player modded (replaced, and kept in the
    // backup), one they added.
    std::filesystem::remove_all(game);
    Write(game / "data" / "same.fit", "same in both");
    Write(game / "data" / "changed.fit", "the player's mod");
    Write(game / "data" / "mine.fit", "the player's own");
    REQUIRE(MCContentUpdater::Choose(*disk, game, WithoutHd).has_value());
    MCFolderSource source(release.Host.Path());
    REQUIRE(MCContentUpdater::Install(release.Setup(), *disk, source, GoOn).has_value());
    REQUIRE(MCContentSwap::Run(*disk, game).Outcome == MCContentSwap::MCOutcome::Swapped);
    REQUIRE(std::filesystem::exists(game / "data" / "missions" / "new.fit"));
    const auto record = MCInstalled::Read(game);
    REQUIRE(record.has_value());
    CHECK(!record->Files.at("DATA/SAME.FIT").Placed);
    CHECK(record->Files.at("DATA/MISSIONS/NEW.FIT").Placed);
    CHECK(record->Files.at("DATA/CHANGED.FIT").Placed);

    // A copy of the game running: removal waits for it, then gives up, removing nothing.
    {
        const std::unique_ptr<MCUpdateLock> running = MCUpdateLock::Open(game, MCUpdateLock::GameLock);
        REQUIRE(running != nullptr);
        REQUIRE(running->TryShared());
        const auto held = MCContentRemoval::Remove(*disk, game, std::chrono::milliseconds(200));
        REQUIRE(!held.has_value());
        CHECK(held.error().find("running") != std::string::npos);
        CHECK(std::filesystem::exists(game / "data" / "missions" / "new.fit"));
    }

    const auto removed = MCContentRemoval::Remove(*disk, game);
    REQUIRE(removed.has_value());
    CHECK_EQ(removed->Removed, size_t{2});
    CHECK_EQ(removed->Kept, size_t{1});
    CHECK_EQ(Read(game / "data" / "same.fit"), std::string("same in both"));
    CHECK(!std::filesystem::exists(game / "data" / "changed.fit"));
    CHECK_EQ(Read(game / "Redux backup" / "data" / "changed.fit"), std::string("the player's mod"));
    CHECK_EQ(Read(game / "data" / "mine.fit"), std::string("the player's own"));
    CHECK(!std::filesystem::exists(game / "data" / "missions"));
    CHECK(!std::filesystem::exists(game / "Redux update"));

    // Nothing left to remove: a second run does nothing.
    const auto again = MCContentRemoval::Remove(*disk, game);
    REQUIRE(again.has_value());
    CHECK_EQ(again->Removed, size_t{0});

    // Downloaded into an empty folder, every file goes, with the folders, and a file the player changed stays.
    std::filesystem::remove_all(game);
    REQUIRE(MCContentUpdater::Choose(*disk, game, WithHd).has_value());
    REQUIRE(MCContentUpdater::Install(release.Setup(), *disk, source, GoOn).has_value());
    REQUIRE(MCContentSwap::Run(*disk, game).Outcome == MCContentSwap::MCOutcome::Swapped);
    Write(game / "data" / "same.fit", "changed by the player");
    const auto downloaded = MCContentRemoval::Remove(*disk, game);
    REQUIRE(downloaded.has_value());
    CHECK_EQ(downloaded->Removed, size_t{3});
    CHECK_EQ(downloaded->Kept, size_t{1});
    CHECK(!std::filesystem::exists(game / "HD"));
    CHECK(!std::filesystem::exists(game / "data" / "missions"));
    CHECK_EQ(Read(game / "data" / "same.fit"), std::string("changed by the player"));
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

TEST_CASE("updater: HD's choice changed while it downloads stops the download for the new choice; back to what's "
          "installed, what it staged and downloaded goes")
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

    // What it has staged and downloaded so far.
    const std::filesystem::path staged = game / "Redux update" / "staged";
    const std::filesystem::path download = game / "Redux update" / "download";
    Write(staged / "HD" / "terrain.hda", "half the terrain");
    Write(download / "hd-2.zip", "half the zip");

    MCUpdater::ChooseHd(false);
    CHECK(MCUpdater::HdPack().Stopping);
    REQUIRE(FramesUntilIdle());
    CHECK(MCUpdater::HdPack().Stage == MCStage::None);
    CHECK(!MCUpdater::HdPack().Stopping);
    CHECK(!MCUpdater::HdPack().Chosen);
    CHECK(!release.Armed());
    CHECK(StagingFree(game));
    CHECK(MCContentUpdater::Chosen(*MakeDisk(), game) == WithoutHd);
    CHECK(!std::filesystem::exists(staged));
    CHECK(!std::filesystem::exists(download));
}

namespace
{
    /// <summary>Runs the updater's frames a while, as the game does, with no end looked for.</summary>
    void SomeFrames()
    {
        constexpr int32_t Frames = 20;

        for (int32_t frame = 0; frame < Frames; frame++)
        {
            MCUpdater::Frame();
            std::this_thread::sleep_for(LookAgain);
        }
    }

    /// <summary>
    /// A match's mission begins while <paramref name="gate"/>'s download waits: it stops, and stays stopped however long
    /// the mission plays, then goes on once it's over (the gate open by then).
    /// </summary>
    void PauseAndResume(MCGate& gate)
    {
        MCUpdater::PauseWork(true);
        REQUIRE(FramesUntilIdle());
        gate.Reached = false;
        gate.Open = true;
        SomeFrames();
        CHECK(!MCUpdater::Working());
        CHECK(!gate.Reached);
        MCUpdater::PauseWork(false);
        CHECK(MCUpdater::Working());
    }
}

TEST_CASE("updater: the program's backend failing while the content stages stops the staging before the staging lock "
          "goes")
{
    MCHostedRelease release("fails-while-staging");
    release.Publish(1, 2, NewerBuild);
    const auto gate = std::make_shared<MCGate>();
    MCUpdaterScope scope(release, true,
                         [host = release.Host.Path(), gate] { return std::make_unique<MCHeldSource>(host, gate); });
    CheckUntilFound(scope, NewerBuild);
    MCUpdater::Answer(MCAnswer::Install);
    REQUIRE(FramesUntil([&] { return gate->Reached.load(); }));
    CHECK(!StagingFree(release.Game.Path()));

    MCUpdater::Failed("Sparkle's session ended");
    CHECK(Stage() == MCStage::Failed);
    CHECK(!MCUpdater::Working());
    CHECK(StagingFree(release.Game.Path()));

    // Nothing stages on after it, nor goes in.
    gate->Open = true;
    SomeFrames();
    CHECK(Stage() == MCStage::Failed);
    CHECK(!release.Armed());
    CHECK(scope.Program->Answers.empty());
}

namespace
{
    /// <summary>The release's files hosted in a folder, every download failing as a lost connection does.</summary>
    class MCCutSource final : public MCContentStage::MCSource
    {
    public:
        explicit MCCutSource(std::filesystem::path folder) : _Inner(std::move(folder)) {}

        std::expected<void, std::string> Fetch(std::string_view, const std::filesystem::path&, uint64_t,
                                               const std::function<bool(std::span<const char>)>&) override
        {
            return std::unexpected("the connection was lost");
        }

        std::expected<std::string, std::string> Read(std::string_view name, size_t most) override
        {
            return _Inner.Read(name, most);
        }

    private:
        MCFolderSource _Inner;
    };
}

TEST_CASE("updater: after a failed download INSTALL tries again: a fresh check, then the update it finds installs")
{
    MCHostedRelease release("install-again");

    // The first staging's downloads are cut off; every other transfer works.
    const auto made = std::make_shared<int32_t>(0);
    constexpr int32_t cutTransfer = 2;
    MCUpdaterScope scope(release, false,
                         [host = release.Host.Path(), made]() -> std::unique_ptr<MCContentStage::MCSource>
                         {
                             if (++*made == cutTransfer)
                             {
                                 return std::make_unique<MCCutSource>(host);
                             }

                             return std::make_unique<MCFolderSource>(host);
                         });
    CheckUntilFound(scope, std::nullopt);
    MCUpdater::Answer(MCAnswer::Install);
    REQUIRE(FramesUntil([] { return Stage() != MCStage::Downloading; }));
    REQUIRE(Stage() == MCStage::Failed);
    CHECK(MCUpdater::Status().Retry);
    CHECK(StagingFree(release.Game.Path()));

    // INSTALL: it shows downloading while the check runs, and goes on to READY with no second INSTALL.
    MCUpdater::Answer(MCAnswer::Install);
    CHECK(Stage() == MCStage::Downloading);
    CHECK_EQ(MCUpdater::Status().Version, std::string("0.2.2 (500)"));
    REQUIRE(FramesUntil([] { return Stage() != MCStage::Downloading; }));
    CHECK(Stage() == MCStage::Ready);
    CHECK(release.Armed());
    CHECK_EQ(*made, cutTransfer + 2);
}

TEST_CASE("updater: quitting cancels a download stalled on the network and waits for its end; nothing is left running")
{
    MCHostedRelease release("stalled-quit");
    const auto stall = std::make_shared<MCStall>();
    MCUpdaterScope scope(release, false, [host = release.Host.Path(), stall]
                         { return std::make_unique<MCStalledSource>(host, stall); });
    CheckUntilFound(scope, std::nullopt);
    MCUpdater::Answer(MCAnswer::Install);

    {
        std::unique_lock lock(stall->Mutex);
        REQUIRE(stall->Changed.wait_for(lock, WorkWait, [&] { return stall->Reached; }));
    }

    // A cancel ends it at once, well before the stall's own wait.
    constexpr std::chrono::seconds prompt{5};
    const auto began = std::chrono::steady_clock::now();
    MCUpdater::Quit();
    CHECK(std::chrono::steady_clock::now() - began < prompt);
    CHECK(stall->Cancelled);
    CHECK(!MCUpdater::Working());
    CHECK(StagingFree(release.Game.Path()));
    CHECK(!release.Armed());
}

TEST_CASE("update work: a piece of work's transfer is cancelled when it's asked to stop; with none it fails, saying so")
{
    MCHostedRelease release("work-source");
    const auto stall = std::make_shared<MCStall>();
    MCUpdateWork::MCWork<void> work(
        "a test's download",
        [host = release.Host.Path(), stall](MCUpdateWork::MCContext& context)
        {
            return MCUpdateWork::WithSource<void>(
                [&] { return std::make_unique<MCStalledSource>(host, stall); }, context.Stop,
                [](MCContentStage::MCSource& source)
                { return source.Fetch("update-2.zip", {}, 1, [](std::span<const char>) { return true; }); });
        });

    {
        std::unique_lock lock(stall->Mutex);
        REQUIRE(stall->Changed.wait_for(lock, WorkWait, [&] { return stall->Reached; }));
    }

    CHECK(!work.Finished());
    work.Stop();
    const std::expected<void, std::string> stopped = work.Join();
    REQUIRE(!stopped.has_value());
    CHECK_EQ(stopped.error(), std::string("stopped"));
    CHECK(stall->Cancelled);

    // Stopped before its transfer is made, the transfer is cancelled as it's made.
    const auto early = std::make_shared<MCStall>();
    std::stop_source stop;
    stop.request_stop();
    const MCUpdateWork::MCWorkSource cancelled(
        [&] { return std::make_unique<MCStalledSource>(release.Host.Path(), early); }, stop.get_token());
    CHECK(early->Cancelled);

    // No transfer: the work fails, saying why.
    const auto none = MCUpdateWork::WithSource<void>(
        [] { return std::unique_ptr<MCContentStage::MCSource>(); }, std::stop_token(),
        [](MCContentStage::MCSource&) -> std::expected<void, std::string> { return {}; });
    REQUIRE(!none.has_value());
    CHECK_EQ(none.error(), std::string(MCUpdateWork::NoSourceError));

    // Progress reported, until the stop.
    MCUpdateWork::MCProgress progress;
    std::stop_source reporting;
    const auto report = MCUpdateWork::Reporter(progress, reporting.get_token());
    CHECK(report(3, 10));
    CHECK((progress.Get() == std::pair<uint64_t, uint64_t>{3, 10}));
    reporting.request_stop();
    CHECK(!report(4, 10));
}

TEST_CASE("updater: an update's download stops while a match's mission plays, and goes on after it")
{
    MCHostedRelease release("updater-paused");
    const auto gate = std::make_shared<MCGate>();
    MCUpdaterScope scope(release, false,
                         [host = release.Host.Path(), gate] { return std::make_unique<MCHeldSource>(host, gate); });
    CheckUntilFound(scope, std::nullopt);
    MCUpdater::Answer(MCAnswer::Install);
    REQUIRE(FramesUntil([&] { return gate->Reached.load(); }));

    PauseAndResume(*gate);
    CHECK(Stage() == MCStage::Downloading);
    REQUIRE(FramesUntil([] { return Stage() != MCStage::Downloading; }));
    CHECK(Stage() == MCStage::Ready);
    CHECK(release.Armed());
    CHECK(StagingFree(release.Game.Path()));
}

TEST_CASE("updater: the HD pack's download stops while a match's mission plays, and goes on after it")
{
    MCHostedRelease release("updater-hd-paused");
    UpdateToRelease2(release);
    ChangeSets(release, WithoutHd);
    const auto gate = std::make_shared<MCGate>();
    MCUpdaterScope scope(release, false,
                         [host = release.Host.Path(), gate] { return std::make_unique<MCHeldSource>(host, gate); });
    MCUpdater::ChooseHd(true);
    REQUIRE(FramesUntil([&] { return gate->Reached.load(); }));

    PauseAndResume(*gate);
    CHECK(MCUpdater::HdPack().Stage == MCStage::Downloading);
    REQUIRE(FramesUntilIdle());
    CHECK(MCUpdater::HdPack().Stage == MCStage::Ready);
    CHECK(release.Armed());
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

    // Offline, or the release's list neither kept nor hosted, nothing is given up: TRY AGAIN can still finish the
    // update.
    const auto unchanged = [&]()
    {
        CHECK(std::filesystem::exists(update / "journal.json"));
        CHECK(!MCContentSwap::Repairing(*disk, game));
        CHECK_EQ(Read(update / "replaced" / "data" / "missions" / "new.fit"), std::string("the player's own mission"));
        CHECK_EQ(Read(update / "replaced" / "data" / "removed.fit"), std::string("removed in v2"));
    };

    MCFolderSource offline(release.Host.Path() / "nowhere");
    REQUIRE(!MCContentUpdater::Repair(release.Setup(), *disk, offline, GoOn).has_value());
    unchanged();
    const std::filesystem::path lists = release.Host.Path() / "lists";
    const std::filesystem::path listsAway = release.Host.Path() / "lists-away";
    std::filesystem::rename(lists, listsAway);
    std::filesystem::remove_all(game / MCFileSystem::FromUtf8(MCContentUpdater::KeptListsFolder));
    REQUIRE(!MCContentUpdater::Repair(release.Setup(), *disk, source, GoOn).has_value());
    unchanged();
    std::filesystem::rename(listsAway, lists);

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

TEST_CASE("recovery screen: its text wraps between words, a word too long within it; past 12 lines the step to take "
          "still shows whole")
{
    constexpr size_t Width = 10;
    CHECK((MCFirstStart::Wrapped("ONE TWO THREE", Width) == std::vector<std::string>{"ONE TWO", "THREE"}));
    CHECK((MCFirstStart::Wrapped("TENLETTERS FITS", Width) == std::vector<std::string>{"TENLETTERS", "FITS"}));
    CHECK((MCFirstStart::Wrapped("ELEVENLETTERS", Width) == std::vector<std::string>{"ELEVENLETT", "ERS"}));
    CHECK((MCFirstStart::Wrapped("A\n\nB", Width) == std::vector<std::string>{"A", "", "B"}));

    // A problem of 20 long words: it's cut short, and the step after it shows whole.
    std::string text;

    for (int word = 0; word < 20; ++word)
    {
        text += "ABCDEFGH ";
    }

    text += "\n\nCLOSE IT, THEN TRY AGAIN.";
    const std::vector<std::string> lines = MCFirstStart::Wrapped(text, Width);
    REQUIRE_EQ(lines.size(), size_t{12});
    CHECK_EQ(lines[7], std::string("ABCDEFG..."));
    CHECK_EQ(lines[8], std::string());
    CHECK_EQ(lines[9], std::string("CLOSE IT,"));
    CHECK_EQ(lines[10], std::string("THEN TRY"));
    CHECK_EQ(lines[11], std::string("AGAIN."));

    // A step too long to show whole is cut at the end, as any text.
    const std::vector<std::string> one = MCFirstStart::Wrapped(text.substr(0, 20 * 9), Width);
    REQUIRE_EQ(one.size(), size_t{12});
    CHECK_EQ(one.back(), std::string("ABCDEFG..."));
}

TEST_CASE_ISOLATED("recovery screen: the question wraps its text over its buttons; Enter, the arrows, Escape and the "
                   "mouse press them")
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

    // Enter held from before the window opened repeats, which isn't a press.
    SDL_Event held{};
    held.type = SDL_EVENT_KEY_DOWN;
    held.key.key = SDLK_RETURN;
    held.key.down = true;
    held.key.repeat = true;
    REQUIRE(SDL_PushEvent(&held));
    CHECK(pressed({SDLK_ESCAPE}) == std::optional<size_t>(2));

    // The mouse, in the window's points: one line of text puts the buttons' middles 120 down, REPAIR's 272 across and
    // QUIT's 392. A click is a press and a release on one button.
    const auto clicked = [&](std::initializer_list<std::pair<float, float>> downUp, std::string_view text)
    {
        bool down = true;

        for (const auto& [x, y] : downUp)
        {
            SDL_Event event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down = down;
            event.button.x = x;
            event.button.y = y;
            REQUIRE(SDL_PushEvent(&event));
            down = !down;
        }

        return MCFirstStart::Ask(Heading, text, Buttons, {.Hidden = true});
    };

    constexpr float ButtonsMiddle = 120;
    constexpr float RepairMiddle = 272;
    constexpr float QuitMiddle = 392;
    CHECK(clicked({{RepairMiddle, ButtonsMiddle}, {RepairMiddle, ButtonsMiddle}}, "ONE LINE.") ==
          std::optional<size_t>(1));
    CHECK(clicked({{RepairMiddle, ButtonsMiddle},
                   {QuitMiddle, ButtonsMiddle},
                   {QuitMiddle, ButtonsMiddle},
                   {QuitMiddle, ButtonsMiddle}},
                  "ONE LINE.") == std::optional<size_t>(2));

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
    CHECK_EQ(list->Description(HdPackRow),
             std::format("THE HD ART, SOUND AND MOVIES: {} TO DOWNLOAD.", MCUpdateWork::SizeText(Terrain().size())));
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
    CHECK_EQ(list->Description(HdPackRow),
             std::format("THE HD ART, SOUND AND MOVIES ({}).", MCUpdateWork::SizeText(Terrain().size())));
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

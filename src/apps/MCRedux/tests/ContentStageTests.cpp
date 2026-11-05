#include "stdafx.h"
#include "MCContentFixture.h"
#include "MCFilePatchFixture.h"
#include "../MCHttpGet.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "lib/MCSha256.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentFolder.h"
#include "update/MCContentList.h"
#include "update/MCContentPlan.h"
#include "update/MCContentStage.h"
#include "update/MCContentSwap.h"
#include "update/MCHttpSource.h"
#include "update/MCInstalled.h"
#include "update/MCReleaseFile.h"
#include "update/MCSetupBackend.h"
#include "update/MCSignedFile.h"
#include "update/MCUpdateDisk.h"

#include <condition_variable>

namespace
{
    using MCContentPlan::MCAction;
    constexpr std::string_view Group = "mc_content_stage_tests";
    using namespace MCContentFixture;

    /// <summary>The disk, but with as much free space as the test sets.</summary>
    class MCRoomDisk final : public MCUpdateDisk
    {
    public:
        explicit MCRoomDisk(uint64_t free) : _Free(free) {}

        MCStat Stat(const std::filesystem::path& path) override { return _Disk->Stat(path); }

        std::expected<MCSha256::MCDigest, std::string> Hash(const std::filesystem::path& path) override
        {
            return _Disk->Hash(path);
        }

        std::expected<std::string, std::string> ReadText(const std::filesystem::path& path) override
        {
            return _Disk->ReadText(path);
        }

        std::expected<std::vector<std::string>, std::string> Files(const std::filesystem::path& folder) override
        {
            return _Disk->Files(folder);
        }

        bool MakeFolders(const std::filesystem::path& path) override { return _Disk->MakeFolders(path); }

        bool Rename(const std::filesystem::path& from, const std::filesystem::path& to) override
        {
            return _Disk->Rename(from, to);
        }

        bool Remove(const std::filesystem::path& path) override { return _Disk->Remove(path); }
        bool RemoveIfEmpty(const std::filesystem::path& path) override { return _Disk->RemoveIfEmpty(path); }

        bool WriteFlushed(const std::filesystem::path& path, std::string_view bytes) override
        {
            return _Disk->WriteFlushed(path, bytes);
        }

        bool FlushFile(const std::filesystem::path& path) override { return _Disk->FlushFile(path); }
        bool FlushFolder(const std::filesystem::path& path) override { return _Disk->FlushFolder(path); }
        std::expected<uint64_t, std::string> FreeBytes(const std::filesystem::path&) override { return _Free; }

    private:
        std::unique_ptr<MCUpdateDisk> _Disk = MakeDisk();
        uint64_t _Free;
    };

    /// <summary>
    /// Adds to <paramref name="release"/> another big file the copy has, <c>HD/name.hda</c>, patched as the terrain
    /// is (the same bytes, its own patch's name); it would come whole from the update zip, which doesn't hold it.
    /// </summary>
    void AddPatched(MCRelease& release, std::string_view name)
    {
        const std::string path = std::format("HD/{}.hda", name);
        const std::string patchName = std::format("patches/{}-1-2.hdiff", name);
        const std::string bigOld = Text(MCFilePatchFixture::PatchOld());
        const std::string bigNew = Text(MCFilePatchFixture::PatchNew());
        const std::string patch = Text(MCFilePatchFixture::CheckedPatch);
        Write(release.Game.Path() / MCFileSystem::FromUtf8(path), bigOld);
        release.Installed.Files[MCContentList::FoldedPath(path)] = {
            .Path = path, .Set = "hd", .Sha256 = MCSha256::Of(bigOld), .Size = bigOld.size()};
        Write(release.Host.Path() / MCFileSystem::FromUtf8(patchName), patch);
        MCContentList::MCFile file{
            .Path = path, .Set = "hd", .Size = bigNew.size(), .Sha256 = MCSha256::Of(bigNew), .Zip = "update-2.zip"};
        file.Patches.push_back({MCSha256::Of(bigOld), patchName, patch.size(), MCSha256::Of(patch)});
        release.List.Files.push_back(std::move(file));
    }

    std::filesystem::path StagedAt(const MCRelease& release, std::string_view path)
    {
        return release.Game.Path() / "Redux update" / "staged" / MCFileSystem::FromUtf8(path);
    }

    /// <summary>
    /// A host that sends nothing: a fetch ends only when cancelled (or after a long while, so a cancel not made shows
    /// as a failed check, not a test that never ends).
    /// </summary>
    class MCSilentSource final : public MCContentStage::MCSource
    {
    public:
        struct MCShared
        {
            std::mutex Lock;
            std::condition_variable Changed;
            bool Reached = false;
            bool Cancelled = false;
        };

        explicit MCSilentSource(std::shared_ptr<MCShared> shared) : _Shared(std::move(shared)) {}

        std::expected<void, std::string> Fetch(std::string_view, const std::filesystem::path&, uint64_t,
                                               const std::function<bool(std::span<const char>)>&) override
        {
            std::unique_lock lock(_Shared->Lock);
            _Shared->Reached = true;
            _Shared->Changed.notify_all();
            _Shared->Changed.wait_for(lock, SilentTime, [&] { return _Shared->Cancelled; });
            return std::unexpected(_Shared->Cancelled ? "stopped" : "the host sent nothing");
        }

        std::expected<std::string, std::string> Read(std::string_view, size_t) override
        {
            return std::unexpected("the host sent nothing");
        }

        void Cancel() override
        {
            const std::scoped_lock lock(_Shared->Lock);
            _Shared->Cancelled = true;
            _Shared->Changed.notify_all();
        }

        static constexpr std::chrono::seconds SilentTime{20};

    private:
        std::shared_ptr<MCShared> _Shared;
    };

    /// <summary>The bytes of the step at <paramref name="path"/>'s file, as listed.</summary>
    uint64_t ListedSize(const MCContentList::MCList& list, std::string_view path)
    {
        const auto file = std::ranges::find(list.Files, path, &MCContentList::MCFile::Path);
        REQUIRE(file != list.Files.end());
        return file->Size;
    }
}

TEST_CASE("content stage: the patch applied, files fetched from the zips holding them, checked, then swapped in")
{
    MCRelease release(Group, "whole");
    const MCContentPlan::MCPlan plan = release.Plan();
    CHECK(std::ranges::count(plan.Steps, MCAction::Patch, &MCContentPlan::MCStep::Action) == 1);
    CHECK(std::ranges::count(plan.Steps, MCAction::Remove, &MCContentPlan::MCStep::Action) == 1);
    MCFolderSource source(release.Host.Path());
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    uint64_t lastReceived = 0;
    uint64_t lastExpected = 0;
    const auto staged = MCContentStage::Stage(*disk, source, release.Game.Path(), plan, release.List,
                                              [&](uint64_t received, uint64_t expected)
                                              {
                                                  lastReceived = received;
                                                  lastExpected = expected;
                                                  return true;
                                              });
    REQUIRE(staged.has_value());

    // Only the update zip and the patch came down: base-1.zip holds only a file the copy has.
    CHECK((source.Asked() == std::vector<std::string>{"patches/terrain-1-2.hdiff", "update-2.zip"}));
    CHECK_EQ(lastReceived, plan.DownloadBytes);
    CHECK_EQ(lastExpected, plan.DownloadBytes);

    // Staged, not armed: the next start swaps nothing in until it is (the program's half comes in first).
    CHECK(!MCContentSwap::Waiting(*disk, release.Game.Path()));
    REQUIRE(MCContentStage::Arm(*disk, release.Game.Path(), plan).has_value());

    // Armed; nothing in place changed until the swap.
    CHECK(MCContentSwap::Waiting(*disk, release.Game.Path()));
    CHECK_EQ(Read(release.Game.Path() / "data" / "changed.fit"), std::string("changed v1"));
    CHECK(Read(release.Game.Path() / "Redux update" / "staged" / "HD" / "terrain.hda") ==
          Text(MCFilePatchFixture::PatchNew()));
    CHECK(MCContentSwap::Run(*disk, release.Game.Path()).Outcome == MCContentSwap::MCOutcome::Swapped);
    release.CheckIsRelease2();

    // Planned again, nothing's left to do.
    MCContentFolder folder(release.Game.Path());
    const auto record = MCInstalled::Read(release.Game.Path());
    REQUIRE(record.has_value());
    const auto again = MCContentPlan::Make(folder, *record, release.List, MCSha256::Of("list 2"), {"game", "hd"});
    REQUIRE(again.has_value());
    CHECK(!again->Changes());

    // A file the player deleted comes back from the same release: the repair's swap is its own, not the last one's.
    std::filesystem::remove(release.Game.Path() / "data" / "changed.fit");
    MCContentFolder repairFolder(release.Game.Path());
    const auto repair =
        MCContentPlan::Make(repairFolder, *record, release.List, MCSha256::Of("list 2"), {"game", "hd"});
    REQUIRE(repair.has_value());
    CHECK(repair->Changes());
    REQUIRE(MCContentStage::Stage(*disk, source, release.Game.Path(), *repair, release.List, GoOn).has_value());
    REQUIRE(MCContentStage::Arm(*disk, release.Game.Path(), *repair).has_value());

    // Staging again over a ready update is refused: it swaps in first.
    const auto twice = MCContentStage::Stage(*disk, source, release.Game.Path(), *repair, release.List, GoOn);
    REQUIRE(!twice.has_value());
    CHECK(twice.error().find("already staged") != std::string::npos);
    CHECK(MCContentSwap::Run(*disk, release.Game.Path()).Outcome == MCContentSwap::MCOutcome::Swapped);
    release.CheckIsRelease2();
    const auto repaired = MCInstalled::Read(release.Game.Path());
    REQUIRE(repaired.has_value());
    CHECK(repaired->Swap != record->Swap);
}

TEST_CASE("content stage: a cut-off download resumes where it stopped; files already staged aren't fetched again")
{
    MCRelease release(Group, "resume");
    const MCContentPlan::MCPlan plan = release.Plan();
    MCFolderSource source(release.Host.Path());
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    source.CutAfter(SendBytes * 3);
    const auto cut = MCContentStage::Stage(*disk, source, release.Game.Path(), plan, release.List, GoOn);
    REQUIRE(!cut.has_value());
    CHECK(cut.error().find("connection was lost") != std::string::npos);
    CHECK(!MCContentSwap::Waiting(*disk, release.Game.Path()));

    // The patch had staged before the zip was cut off: the next attempt fetches only the rest of the zip.
    const uint64_t before = source.Sent();
    REQUIRE(MCContentStage::Stage(*disk, source, release.Game.Path(), plan, release.List, GoOn).has_value());
    REQUIRE(MCContentStage::Arm(*disk, release.Game.Path(), plan).has_value());
    CHECK_EQ(source.Sent() - before, release.List.Zips[1].Size - SendBytes * 3);
    CHECK(std::ranges::count(source.Asked(), std::string("patches/terrain-1-2.hdiff")) == 1);
    CHECK(MCContentSwap::Run(*disk, release.Game.Path()).Outcome == MCContentSwap::MCOutcome::Swapped);
    release.CheckIsRelease2();
}

TEST_CASE("content stage: a patch that doesn't apply fetches the file whole; a zip not as listed is refused")
{
    MCRelease release(Group, "fallback");
    // A damaged patch the list vouches for: it downloads, and HDiffPatch's own checksum refuses it.
    std::string damaged = Text(MCFilePatchFixture::CheckedPatch);
    damaged[damaged.size() - MCFilePatchFixture::PatchDataFromEnd] ^= 0x5a;
    Write(release.Host.Path() / "patches" / "terrain-1-2.hdiff", damaged);
    release.List.Files[0].Patches[0].Sha256 = MCSha256::Of(damaged);
    const MCContentPlan::MCPlan plan = release.Plan();
    MCFolderSource source(release.Host.Path());
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    REQUIRE(MCContentStage::Stage(*disk, source, release.Game.Path(), plan, release.List, GoOn).has_value());
    REQUIRE(MCContentStage::Arm(*disk, release.Game.Path(), plan).has_value());
    CHECK(MCContentSwap::Run(*disk, release.Game.Path()).Outcome == MCContentSwap::MCOutcome::Swapped);
    release.CheckIsRelease2();

    // A zip changed on the host after the list was made: refused, nothing marked ready.
    MCRelease other(Group, "bad-zip");
    std::string zip = Read(other.Host.Path() / "update-2.zip");
    zip[zip.size() / 2] ^= 1;
    Write(other.Host.Path() / "update-2.zip", zip);
    const MCContentPlan::MCPlan otherPlan = other.Plan();
    MCFolderSource otherSource(other.Host.Path());
    const auto refused = MCContentStage::Stage(*disk, otherSource, other.Game.Path(), otherPlan, other.List, GoOn);
    REQUIRE(!refused.has_value());
    CHECK(refused.error().find("isn't as the list has it") != std::string::npos);
    CHECK(!MCContentSwap::Waiting(*disk, other.Game.Path()));
    CHECK_EQ(Read(other.Game.Path() / "data" / "changed.fit"), std::string("changed v1"));
}

TEST_CASE("content stage: a checked download resumes, hashed as it comes; one not as named goes, a stopped one stays")
{
    using MCKind = MCContentStage::MCFetchError::MCKind;
    MCRelease release(Group, "checked");
    const MCContentList::MCZip& zip = release.List.Zips[1];
    const std::string bytes = Read(release.Host.Path() / zip.Name);
    const std::filesystem::path to = release.Game.Path() / "download" / zip.Name;
    MCFolderSource source(release.Host.Path());
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    uint64_t received = 0;
    const auto count = [&](uint64_t count)
    {
        received += count;
        return true;
    };

    // Cut off, then resumed: the bytes here count, and the whole is the list's.
    Write(to, bytes.substr(0, SendBytes));
    REQUIRE(MCContentStage::FetchChecked(*disk, source, zip.Name, to, zip.Size, zip.Sha256, count).has_value());
    CHECK(Read(to) == bytes);
    CHECK_EQ(received, zip.Size);
    CHECK_EQ(source.Sent(), zip.Size - SendBytes);

    // Here whole: nothing is fetched, the bytes are checked.
    REQUIRE(MCContentStage::FetchChecked(*disk, source, zip.Name, to, zip.Size, zip.Sha256, count).has_value());
    CHECK_EQ(source.Sent(), zip.Size - SendBytes);

    // A damaged start resumed from: not as named, so it goes.
    std::string damaged = bytes.substr(0, SendBytes);
    damaged[0] ^= 1;
    Write(to, damaged);
    const auto refused = MCContentStage::FetchChecked(*disk, source, zip.Name, to, zip.Size, zip.Sha256, count);
    REQUIRE(!refused.has_value());
    CHECK(refused.error().Kind == MCKind::NotAsNamed);
    CHECK(!std::filesystem::exists(to));

    // Stopped midway: what came stays.
    const auto stopped = MCContentStage::FetchChecked(*disk, source, zip.Name, to, zip.Size, zip.Sha256,
                                                      [](uint64_t count) { return count == 0; });
    REQUIRE(!stopped.has_value());
    CHECK(stopped.error().Kind == MCKind::Stopped);
    CHECK_EQ(std::filesystem::file_size(to), uint64_t{SendBytes});
}

TEST_CASE("content stage: a stop ends staging at once: no later patch is fetched or applied, no zip fetched")
{
    MCRelease release(Group, "stop");
    AddPatched(release, "a");
    AddPatched(release, "b");
    AddPatched(release, "c");
    const MCContentPlan::MCPlan plan = release.Plan();
    REQUIRE(std::ranges::count(plan.Steps, MCAction::Patch, &MCContentPlan::MCStep::Action) == 4);
    // b's patch downloaded whole by an earlier attempt.
    Write(release.Game.Path() / "Redux update" / "download" / "patches" / "b-1-2.hdiff",
          Text(MCFilePatchFixture::CheckedPatch));
    MCFolderSource source(release.Host.Path());
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    // STOP from the start: nothing is fetched, and the patch already here isn't applied.
    const auto never = MCContentStage::Stage(*disk, source, release.Game.Path(), plan, release.List,
                                             [](uint64_t, uint64_t) { return false; });
    CHECK_EQ(never.error_or(""), std::string("stopped"));
    CHECK(source.Asked().empty());
    CHECK(!std::filesystem::exists(StagedAt(release, "HD/b.hda")));

    // STOP as the first patch's bytes arrive.
    const auto stopped = MCContentStage::Stage(*disk, source, release.Game.Path(), plan, release.List,
                                               [](uint64_t received, uint64_t) { return received == 0; });
    REQUIRE(!stopped.has_value());
    CHECK_EQ(stopped.error(), std::string("stopped"));
    CHECK((source.Asked() == std::vector<std::string>{"patches/terrain-1-2.hdiff"}));
    CHECK(!std::filesystem::exists(StagedAt(release, "HD/b.hda")));
    CHECK(std::filesystem::exists(release.Game.Path() / "Redux update" / "download" / "patches" / "b-1-2.hdiff"));

    // The next attempt does the rest, b's patch not fetched again.
    REQUIRE(MCContentStage::Stage(*disk, source, release.Game.Path(), plan, release.List, GoOn).has_value());
    CHECK(std::ranges::count(source.Asked(), std::string("patches/b-1-2.hdiff")) == 0);

    for (const std::string_view path : {"HD/terrain.hda", "HD/a.hda", "HD/b.hda", "HD/c.hda"})
    {
        CHECK(Read(StagedAt(release, path)) == Text(MCFilePatchFixture::PatchNew()));
    }
}

TEST_CASE("content stage: refused without room for what's still to stage; a resumed attempt needs room for the rest "
          "alone")
{
    MCRelease release(Group, "space");
    const MCContentPlan::MCPlan plan = release.Plan();
    MCFolderSource source(release.Host.Path());

    // A full disk: nothing is fetched.
    MCRoomDisk full(0);
    const auto refused = MCContentStage::Stage(full, source, release.Game.Path(), plan, release.List, GoOn);
    REQUIRE(!refused.has_value());
    CHECK(refused.error().find("GB free") != std::string::npos);
    CHECK(source.Asked().empty());

    // Cut off in the zip, with the patched file staged: what's left is the two small files and the zip's rest, though
    // the plan as a whole needs far more.
    MCRoomDisk roomy(std::numeric_limits<uint64_t>::max());
    source.CutAfter(SendBytes * 3);
    REQUIRE(!MCContentStage::Stage(roomy, source, release.Game.Path(), plan, release.List, GoOn).has_value());
    const uint64_t rest = ListedSize(release.List, "data/changed.fit") +
                          ListedSize(release.List, "data/missions/new.fit") + release.List.Zips[1].Size - SendBytes * 3;
    REQUIRE(rest < plan.StagedBytes);
    MCRoomDisk tight(rest);
    REQUIRE(MCContentStage::Stage(tight, source, release.Game.Path(), plan, release.List, GoOn).has_value());
    REQUIRE(MCContentStage::Arm(tight, release.Game.Path(), plan).has_value());
    CHECK(MCContentSwap::Run(tight, release.Game.Path()).Outcome == MCContentSwap::MCOutcome::Swapped);
    release.CheckIsRelease2();
}

TEST_CASE("content stage: a patch that falls back on a zip not otherwise fetched is checked for room for it")
{
    MCRelease release(Group, "space-fallback");
    // Only the big file changes, and its patch is damaged: the update zip holding it is fetched only as a fallback.
    Write(release.Game.Path() / "data" / "changed.fit", "changed v2");
    Write(release.Game.Path() / "data" / "missions" / "new.fit", "a new mission");
    std::string damaged = Text(MCFilePatchFixture::CheckedPatch);
    damaged[damaged.size() - MCFilePatchFixture::PatchDataFromEnd] ^= 0x5a;
    Write(release.Host.Path() / "patches" / "terrain-1-2.hdiff", damaged);
    release.List.Files[0].Patches[0].Sha256 = MCSha256::Of(damaged);
    const MCContentPlan::MCPlan plan = release.Plan();
    REQUIRE(plan.Zips.empty());
    MCFolderSource source(release.Host.Path());
    MCRoomDisk disk(plan.StagedBytes + damaged.size());
    const auto refused = MCContentStage::Stage(disk, source, release.Game.Path(), plan, release.List, GoOn);
    REQUIRE(!refused.has_value());
    CHECK(refused.error().find("GB free") != std::string::npos);
    CHECK((source.Asked() == std::vector<std::string>{"patches/terrain-1-2.hdiff"}));
}

TEST_CASE("content stage: a zip whose entry can't be written stays for the next attempt; one at fault goes")
{
    MCRelease release(Group, "keep-zip");
    const MCContentPlan::MCPlan plan = release.Plan();
    MCFolderSource source(release.Host.Path());
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    // A folder where an entry is unpacked to: it can't be written, as on a full disk.
    const std::filesystem::path blocked = release.Game.Path() / "Redux update" / "staged" / "data" / "changed.fit";
    std::filesystem::create_directories(blocked);
    const auto failed = MCContentStage::Stage(*disk, source, release.Game.Path(), plan, release.List, GoOn);
    REQUIRE(!failed.has_value());
    CHECK(failed.error().find("can't be written") != std::string::npos);
    CHECK(std::filesystem::exists(release.Game.Path() / "Redux update" / "download" / "update-2.zip"));

    // Written once there's room: the zip isn't fetched again.
    std::filesystem::remove(blocked);
    const size_t asked = source.Asked().size();
    REQUIRE(MCContentStage::Stage(*disk, source, release.Game.Path(), plan, release.List, GoOn).has_value());
    CHECK(source.Asked().size() == asked);
    REQUIRE(MCContentStage::Arm(*disk, release.Game.Path(), plan).has_value());
    CHECK(MCContentSwap::Run(*disk, release.Game.Path()).Outcome == MCContentSwap::MCOutcome::Swapped);
    release.CheckIsRelease2();
}

TEST_CASE("setup download: letting go of it ends its download at once, even while the host sends nothing")
{
    using namespace std::chrono_literals;
    const MCScratchFolder game(Group, "setup-stop");
    const auto shared = std::make_shared<MCSilentSource::MCShared>();
    const MCSetupBackend::MCSourceFor sources =
        [shared](std::string_view) -> std::expected<std::unique_ptr<MCContentStage::MCSource>, std::string>
    { return std::make_unique<MCSilentSource>(shared); };
    constexpr uint64_t Build = 600;
    const std::unique_ptr<MCUpdater::MCBackend> backend =
        MCSetupBackend::Make(game.Path(), "0.2.1 (500)", sources, MCSetupBackend::MCPlatform{});
    backend->Offered({.Build = Build,
                      .Size = 100,
                      .Sha256 = MCSha256::Of("a setup"),
                      .Url = "https://example.test/programs/setup-600.exe",
                      .Silent = "/VERYSILENT"});
    backend->Check(Build);
    backend->Answer(MCUpdater::MCAnswer::Install);

    {
        std::unique_lock lock(shared->Lock);
        REQUIRE(shared->Changed.wait_for(lock, 5s, [&] { return shared->Reached; }));
    }

    const auto start = std::chrono::steady_clock::now();
    backend->Answer(MCUpdater::MCAnswer::Later);
    CHECK(std::chrono::steady_clock::now() - start < 2s);
    CHECK(shared->Cancelled);
}

/// <summary>
/// Tool: <c>--content-install &lt;game folder&gt; --content-host &lt;host folder or address&gt; --root-key
/// &lt;base64&gt; [--content-sets game,hd]</c> installs or updates the content of a release the release script made
/// (tools/release/make_release.py), as the game will: from a folder, or over https (the downloads Worker) through this
/// platform's transfer, the key list and <c>release.json</c> checked against the root key, the list against its
/// hash, then planned, staged and swapped in. Prints what it did.
/// </summary>
TEST_CASE_ISOLATED("tool: --content-install installs a release's content from a host folder")
{
    const std::optional<std::string_view> gameOption = MCTest::Option("content-install");

    if (!gameOption.has_value())
    {
        return;
    }

    const std::optional<std::string_view> hostOption = MCTest::Option("content-host");
    const std::optional<std::string_view> rootOption = MCTest::Option("root-key");
    REQUIRE(hostOption.has_value());
    REQUIRE(rootOption.has_value());
    const std::filesystem::path game = MCFileSystem::FromUtf8(*gameOption);
    const std::filesystem::path host = MCFileSystem::FromUtf8(*hostOption);
    const std::optional<MCSignedFile::MCPublicKey> root = MCSignedFile::KeyFromBase64(*rootOption);
    REQUIRE(root.has_value());
    const std::array<MCSignedFile::MCPublicKey, 1> roots = {*root};
    std::set<std::string, std::less<>> sets = {"game", "hd"};

    if (const std::optional<std::string_view> chosen = MCTest::Option("content-sets"))
    {
        sets.clear();

        for (const auto part : std::views::split(*chosen, ','))
        {
            sets.emplace(std::string_view(part.begin(), part.end()));
        }
    }

    // A small file's most, here: the game's updater has its own for each.
    constexpr size_t MostBytes = 64 << 20;
    std::unique_ptr<MCContentStage::MCSource> source;

    if (hostOption->starts_with("http"))
    {
        auto made = MCHttpSource::Make(*hostOption, MCMakeHttpGet());

        if (!made)
        {
            std::cout << std::format("  {}\n", made.error());
        }

        REQUIRE(made.has_value());
        source = std::move(*made);
    }
    else
    {
        source = std::make_unique<MCFolderSource>(host);
    }

    const auto readHosted = [&](std::string_view name)
    {
        auto text = source->Read(name, MostBytes);

        if (!text)
        {
            std::cout << std::format("  {}\n", text.error());
        }

        REQUIRE(text.has_value());
        return std::move(*text);
    };

    const auto trusted = MCReleaseFile::Check(readHosted("keys.json"), readHosted("release.json"), roots, {});

    if (!trusted)
    {
        std::cout << std::format("  {}\n", trusted.error());
    }

    REQUIRE(trusted.has_value());
    const MCReleaseFile::MCRelease& release = trusted->Releases.Current;
    const std::string listText = readHosted(std::format("lists/{}.json", MCSha256::Hex(release.List)));
    REQUIRE(MCSha256::Of(listText) == release.List);
    const auto list = MCContentList::Read(listText);

    if (!list)
    {
        std::cout << std::format("  {}\n", list.error());
    }

    REQUIRE(list.has_value());
    std::filesystem::create_directories(game);
    const auto installed = MCInstalled::Read(game);
    REQUIRE(installed.has_value());
    MCContentFolder folder(game);
    const auto plan = MCContentPlan::Make(folder, *installed, *list, release.List, sets);

    if (!plan)
    {
        std::cout << std::format("  {}\n", plan.error());
    }

    REQUIRE(plan.has_value());
    const auto count = [&](MCAction action)
    { return std::ranges::count(plan->Steps, action, &MCContentPlan::MCStep::Action); };
    const auto backedUp = std::ranges::count(plan->Steps, MCContentSwap::MCKeep::Always, &MCContentPlan::MCStep::Keep);
    std::cout << std::format("  release {} ({}): {} kept, {} patched, {} fetched, {} removed, {} to Redux backup; {} "
                             "bytes to download in {} zips\n",
                             release.Number, release.Version, count(MCAction::Keep), count(MCAction::Patch),
                             count(MCAction::Fetch), count(MCAction::Remove), backedUp, plan->DownloadBytes,
                             plan->Zips.size());
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    uint64_t downloaded = 0;
    const auto stageStart = std::chrono::steady_clock::now();
    const auto staged = MCContentStage::Stage(*disk, *source, game, *plan, *list,
                                              [&](uint64_t done, uint64_t)
                                              {
                                                  downloaded = done;
                                                  return true;
                                              });
    const auto swapStart = std::chrono::steady_clock::now();

    if (!staged)
    {
        std::cout << std::format("  {}\n", staged.error());
    }

    REQUIRE(staged.has_value());
    REQUIRE(MCContentStage::Arm(*disk, game, *plan).has_value());
    const MCContentSwap::MCResult swapped = MCContentSwap::Run(*disk, game);
    const auto done = std::chrono::steady_clock::now();
    REQUIRE(swapped.Outcome == MCContentSwap::MCOutcome::Swapped || !plan->Changes());
    std::cout << std::format("  installed: {} bytes downloaded; staged in {:.2f} s, swapped in {:.3f} s\n", downloaded,
                             std::chrono::duration<double>(swapStart - stageStart).count(),
                             std::chrono::duration<double>(done - swapStart).count());
}

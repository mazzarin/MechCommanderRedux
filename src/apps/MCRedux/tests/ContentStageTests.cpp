#include "stdafx.h"
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
#include "update/MCSignedFile.h"
#include "update/MCUpdateDisk.h"

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#include "miniz.h"

namespace
{
    using MCContentPlan::MCAction;
    constexpr std::string_view Group = "mc_content_stage_tests";
    /// <summary>How much a hosted file is sent at a time.</summary>
    constexpr size_t SendBytes = 4096;

    std::string Text(std::span<const uint8_t> bytes)
    {
        return std::string(bytes.begin(), bytes.end());
    }

    void Write(const std::filesystem::path& path, std::string_view bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    std::string Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    /// <summary>A stored zip of <paramref name="entries"/> (name, bytes), as the release script makes them.</summary>
    std::string StoredZip(const std::vector<std::pair<std::string, std::string>>& entries)
    {
        mz_zip_archive zip{};
        REQUIRE(mz_zip_writer_init_heap(&zip, 0, 0));

        for (const auto& [name, bytes] : entries)
        {
            REQUIRE(mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(), MZ_NO_COMPRESSION));
        }

        void* data = nullptr;
        size_t size = 0;
        REQUIRE(mz_zip_writer_finalize_heap_archive(&zip, &data, &size));
        std::string made(static_cast<const char*>(data), size);
        mz_zip_writer_end(&zip);
        return made;
    }

    /// <summary>The release's files hosted in a folder; it can cut a download off once, after some bytes.</summary>
    class MCFolderSource final : public MCContentStage::MCSource
    {
    public:
        explicit MCFolderSource(std::filesystem::path folder) : _Folder(std::move(folder)) {}

        /// <summary>The next download stops after <paramref name="bytes"/> bytes, as a lost connection does.</summary>
        void CutAfter(uint64_t bytes) { _CutAfter = bytes; }

        /// <summary>The bytes sent in all, and the files asked for.</summary>
        uint64_t Sent() const { return _Sent; }
        const std::vector<std::string>& Asked() const { return _Asked; }

        std::expected<void, std::string> Fetch(std::string_view name, const std::filesystem::path& to, uint64_t size,
                                               const std::function<bool(uint64_t)>& received) override
        {
            _Asked.emplace_back(name);
            std::ifstream from(_Folder / MCFileSystem::FromUtf8(name), std::ios::binary);

            if (!from)
            {
                return std::unexpected("404");
            }

            std::error_code error;
            const uint64_t start = std::filesystem::exists(to) ? std::filesystem::file_size(to, error) : 0;
            from.seekg(static_cast<std::streamoff>(start));
            std::ofstream out(to, std::ios::binary | std::ios::app);
            std::vector<char> buffer(SendBytes);
            uint64_t sent = 0;

            for (uint64_t at = start; at < size;)
            {
                from.read(buffer.data(), static_cast<std::streamsize>(std::min<uint64_t>(buffer.size(), size - at)));
                const auto count = static_cast<uint64_t>(from.gcount());

                if (count == 0)
                {
                    return std::unexpected("the hosted file is short");
                }

                out.write(buffer.data(), static_cast<std::streamsize>(count));
                at += count;
                sent += count;
                _Sent += count;

                if (!received(count))
                {
                    return std::unexpected("stopped");
                }

                if (_CutAfter && sent >= *_CutAfter)
                {
                    _CutAfter.reset();
                    return std::unexpected("the connection was lost");
                }
            }

            return {};
        }

        std::expected<std::string, std::string> Read(std::string_view name, size_t most) override
        {
            _Asked.emplace_back(name);
            std::ifstream from(_Folder / MCFileSystem::FromUtf8(name), std::ios::binary);

            if (!from)
            {
                return std::unexpected("404");
            }

            std::string text((std::istreambuf_iterator<char>(from)), std::istreambuf_iterator<char>());
            return text.size() <= most ? std::expected<std::string, std::string>(std::move(text))
                                       : std::unexpected("too big");
        }

    private:
        std::filesystem::path _Folder;
        std::optional<uint64_t> _CutAfter;
        uint64_t _Sent = 0;
        std::vector<std::string> _Asked;
    };

    /// <summary>
    /// A release 2 over a copy of release 1: a big file changed in part (a patch from release 1's copy), a file
    /// changed whole, a new mission, a removed file and one that stays; the copy on disk, the zips and the patch
    /// hosted.
    /// </summary>
    struct MCRelease
    {
        explicit MCRelease(std::string_view name)
            : Game(Group, std::string(name) + "-game"), Host(Group, std::string(name) + "-host")
        {
            const std::string bigOld = Text(MCFilePatchFixture::PatchOld());
            const std::string bigNew = Text(MCFilePatchFixture::PatchNew());
            Write(Game.Path() / "HD" / "terrain.hda", bigOld);
            Write(Game.Path() / "data" / "changed.fit", "changed v1");
            Write(Game.Path() / "data" / "same.fit", "same in both");
            Write(Game.Path() / "data" / "removed.fit", "removed in v2");

            for (const auto& [path, bytes] :
                 std::vector<std::pair<std::string, std::string>>{{"HD/terrain.hda", bigOld},
                                                                  {"data/changed.fit", "changed v1"},
                                                                  {"data/same.fit", "same in both"},
                                                                  {"data/removed.fit", "removed in v2"}})
            {
                Installed.Files[MCContentList::FoldedPath(path)] = {
                    .Path = path, .Set = "game", .Sha256 = MCSha256::Of(bytes), .Size = bytes.size()};
            }

            Installed.Release = 1;
            Installed.Sets = {"game", "hd"};
            const std::string base = StoredZip({{"data/same.fit", "same in both"}});
            const std::string update = StoredZip({{"data/changed.fit", "changed v2"},
                                                  {"data/missions/new.fit", "a new mission"},
                                                  {"HD/terrain.hda", bigNew}});
            const std::string patch = Text(MCFilePatchFixture::CheckedPatch);
            Write(Host.Path() / "base-1.zip", base);
            Write(Host.Path() / "update-2.zip", update);
            Write(Host.Path() / "patches" / "terrain-1-2.hdiff", patch);
            List = {.Format = 1, .Release = 2, .LowestProgram = 1};
            List.Zips = {{"base-1.zip", base.size(), MCSha256::Of(base)},
                         {"update-2.zip", update.size(), MCSha256::Of(update)}};
            MCContentList::MCFile terrain{.Path = "HD/terrain.hda",
                                          .Set = "hd",
                                          .Size = bigNew.size(),
                                          .Sha256 = MCSha256::Of(bigNew),
                                          .Zip = "update-2.zip"};
            terrain.Patches.push_back(
                {MCSha256::Of(bigOld), "patches/terrain-1-2.hdiff", patch.size(), MCSha256::Of(patch)});
            List.Files.push_back(terrain);
            List.Files.push_back({.Path = "data/changed.fit",
                                  .Set = "game",
                                  .Size = 10,
                                  .Sha256 = MCSha256::Of("changed v2"),
                                  .Zip = "update-2.zip",
                                  .Earlier = {MCSha256::Of("changed v1")}});
            List.Files.push_back({.Path = "data/missions/new.fit",
                                  .Set = "game",
                                  .Size = 13,
                                  .Sha256 = MCSha256::Of("a new mission"),
                                  .Zip = "update-2.zip"});
            List.Files.push_back({.Path = "data/same.fit",
                                  .Set = "game",
                                  .Size = 12,
                                  .Sha256 = MCSha256::Of("same in both"),
                                  .Zip = "base-1.zip"});
        }

        MCContentPlan::MCPlan Plan()
        {
            MCContentFolder folder(Game.Path());
            auto plan = MCContentPlan::Make(folder, Installed, List, MCSha256::Of("list 2"), {"game", "hd"});
            REQUIRE(plan.has_value());
            return std::move(*plan);
        }

        /// <summary>Whether the game folder now holds release 2, byte for byte, and nothing of the update's.</summary>
        void CheckIsRelease2() const
        {
            CHECK(Read(Game.Path() / "HD" / "terrain.hda") == Text(MCFilePatchFixture::PatchNew()));
            CHECK_EQ(Read(Game.Path() / "data" / "changed.fit"), std::string("changed v2"));
            CHECK_EQ(Read(Game.Path() / "data" / "missions" / "new.fit"), std::string("a new mission"));
            CHECK_EQ(Read(Game.Path() / "data" / "same.fit"), std::string("same in both"));
            CHECK(!std::filesystem::exists(Game.Path() / "data" / "removed.fit"));
            CHECK(!std::filesystem::exists(Game.Path() / "Redux update" / "staged"));
            CHECK(!std::filesystem::exists(Game.Path() / "Redux update" / "download"));
            CHECK(!std::filesystem::exists(Game.Path() / "Redux update" / "replaced"));
            const auto record = MCInstalled::Read(Game.Path());
            REQUIRE(record.has_value());
            CHECK_EQ(record->Release, uint64_t{2});
            CHECK_EQ(record->Files.size(), size_t{4});
        }

        MCScratchFolder Game;
        MCScratchFolder Host;
        MCInstalled::MCRecord Installed;
        MCContentList::MCList List;
    };

    const std::function<bool(uint64_t, uint64_t)> GoOn = [](uint64_t, uint64_t) { return true; };
}

TEST_CASE("content stage: the patch applied, files fetched from the zips holding them, checked, then swapped in")
{
    MCRelease release("whole");
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

    // Staged and ready; nothing in place changed until the swap.
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
    MCRelease release("resume");
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
    CHECK_EQ(source.Sent() - before, release.List.Zips[1].Size - SendBytes * 3);
    CHECK(std::ranges::count(source.Asked(), std::string("patches/terrain-1-2.hdiff")) == 1);
    CHECK(MCContentSwap::Run(*disk, release.Game.Path()).Outcome == MCContentSwap::MCOutcome::Swapped);
    release.CheckIsRelease2();
}

TEST_CASE("content stage: a patch that doesn't apply fetches the file whole; a zip not as listed is refused")
{
    MCRelease release("fallback");
    // A damaged patch the list vouches for: it downloads, and HDiffPatch's own checksum refuses it.
    std::string damaged = Text(MCFilePatchFixture::CheckedPatch);
    damaged[damaged.size() - MCFilePatchFixture::PatchDataFromEnd] ^= 0x5a;
    Write(release.Host.Path() / "patches" / "terrain-1-2.hdiff", damaged);
    release.List.Files[0].Patches[0].Sha256 = MCSha256::Of(damaged);
    const MCContentPlan::MCPlan plan = release.Plan();
    MCFolderSource source(release.Host.Path());
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    REQUIRE(MCContentStage::Stage(*disk, source, release.Game.Path(), plan, release.List, GoOn).has_value());
    CHECK(MCContentSwap::Run(*disk, release.Game.Path()).Outcome == MCContentSwap::MCOutcome::Swapped);
    release.CheckIsRelease2();

    // A zip changed on the host after the list was made: refused, nothing marked ready.
    MCRelease other("bad-zip");
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
    std::cout << std::format("  release {} ({}): {} kept, {} patched, {} fetched, {} removed, {} the player's; {} "
                             "bytes to download in {} zips\n",
                             release.Number, release.Version, count(MCAction::Keep), count(MCAction::Patch),
                             count(MCAction::Fetch), count(MCAction::Remove), count(MCAction::LeaveTheirs),
                             plan->DownloadBytes, plan->Zips.size());
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
    const MCContentSwap::MCResult swapped = MCContentSwap::Run(*disk, game);
    const auto done = std::chrono::steady_clock::now();
    REQUIRE(swapped.Outcome == MCContentSwap::MCOutcome::Swapped || !plan->Changes());
    std::cout << std::format("  installed: {} bytes downloaded; staged in {:.2f} s, swapped in {:.3f} s\n", downloaded,
                             std::chrono::duration<double>(swapStart - stageStart).count(),
                             std::chrono::duration<double>(done - swapStart).count());
}

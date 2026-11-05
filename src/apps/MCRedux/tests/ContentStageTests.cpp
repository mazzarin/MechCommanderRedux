#include "stdafx.h"
#include "MCFilePatchFixture.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "lib/MCSha256.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentFolder.h"
#include "update/MCContentList.h"
#include "update/MCContentPlan.h"
#include "update/MCContentStage.h"
#include "update/MCContentSwap.h"
#include "update/MCInstalled.h"
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

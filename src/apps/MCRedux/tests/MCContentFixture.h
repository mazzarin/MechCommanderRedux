#pragma once

#include "MCFilePatchFixture.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "lib/MCSha256.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentFolder.h"
#include "update/MCContentList.h"
#include "update/MCContentPlan.h"
#include "update/MCContentStage.h"
#include "update/MCInstalled.h"
#include "update/MCReleaseFile.h"
#include "update/MCSignedFile.h"
#include "update/MCUpdateDisk.h"

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#include "miniz.h"

#ifndef SODIUM_STATIC
#define SODIUM_STATIC 1
#endif
#include <sodium/crypto_sign_ed25519.h>

// What the content updater's tests share: files written and read whole, stored zips, a host in a folder, a release 2
// over a copy of release 1, and signing keys made from a seed.

namespace MCContentFixture
{
    /// <summary>How much a hosted file is sent at a time.</summary>
    constexpr size_t SendBytes = 4096;

    /// <summary>Staging's progress, always going on.</summary>
    inline const std::function<bool(uint64_t, uint64_t)> GoOn = [](uint64_t, uint64_t) { return true; };

    inline std::string Text(std::span<const uint8_t> bytes)
    {
        return std::string(bytes.begin(), bytes.end());
    }

    inline void Write(const std::filesystem::path& path, std::string_view bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    inline std::string Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    /// <summary>A stored zip of <paramref name="entries"/> (name, bytes), as the release script makes them.</summary>
    inline std::string StoredZip(const std::vector<std::pair<std::string, std::string>>& entries)
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
                                               const std::function<bool(std::span<const char>)>& received) override
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

                if (!received({buffer.data(), static_cast<size_t>(count)}))
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
    /// The disk, but with as much free space as the test sets (none: the disk's own), what can't be looked at or read
    /// (a drive failing), and calls as files are flushed and the free space asked.
    /// </summary>
    class MCTestDisk final : public MCUpdateDisk
    {
    public:
        MCTestDisk() = default;
        explicit MCTestDisk(uint64_t free) : _Free(free) {}

        MCStat Stat(const std::filesystem::path& path) override
        {
            if (!Unseen.empty() && path == Unseen)
            {
                return {.Error = MCFileSystem::ToUtf8(path) + " can't be looked at"};
            }

            return _Disk->Stat(path);
        }

        std::expected<bool, std::string> IsLink(const std::filesystem::path& path) override
        {
            return _Disk->IsLink(path);
        }

        std::expected<MCSha256::MCDigest, std::string> Hash(const std::filesystem::path& path) override
        {
            return _Disk->Hash(path);
        }

        std::expected<std::string, std::string> ReadText(const std::filesystem::path& path) override
        {
            if (!Unread.empty() && path == Unread)
            {
                return std::unexpected(MCFileSystem::ToUtf8(path) + " can't be read (the test's disk)");
            }

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

        bool Move(const std::filesystem::path& from, const std::filesystem::path& to) override
        {
            return _Disk->Move(from, to);
        }

        bool Remove(const std::filesystem::path& path) override { return _Disk->Remove(path); }
        bool RemoveIfEmpty(const std::filesystem::path& path) override { return _Disk->RemoveIfEmpty(path); }

        bool WriteFlushed(const std::filesystem::path& path, std::string_view bytes) override
        {
            return _Disk->WriteFlushed(path, bytes);
        }

        bool FlushFile(const std::filesystem::path& path) override
        {
            if (OnFlush)
            {
                OnFlush();
            }

            return _Disk->FlushFile(path);
        }

        bool FlushFolder(const std::filesystem::path& path) override { return _Disk->FlushFolder(path); }

        std::expected<uint64_t, std::string> FreeBytes(const std::filesystem::path& path) override
        {
            if (OnFreeBytes)
            {
                OnFreeBytes();
            }

            return _Free ? std::expected<uint64_t, std::string>(*_Free) : _Disk->FreeBytes(path);
        }

        /// <summary>Called as the free space is asked (none: nothing).</summary>
        std::function<void()> OnFreeBytes;

        /// <summary>Called as each file is flushed (none: nothing).</summary>
        std::function<void()> OnFlush;

        /// <summary>What can't be looked at, and what can't be read (empty: none).</summary>
        std::filesystem::path Unseen;
        std::filesystem::path Unread;

    private:
        std::unique_ptr<MCUpdateDisk> _Disk = MakeDisk();
        std::optional<uint64_t> _Free;
    };

    /// <summary>
    /// A release 2 over a copy of release 1: a big file changed in part (a patch from release 1's copy), a file
    /// changed whole, a new mission, a removed file and one that stays; the copy on disk, the zips and the patch
    /// hosted.
    /// </summary>
    struct MCRelease
    {
        MCRelease(std::string_view group, std::string_view name)
            : Game(group, std::string(name) + "-game"), Host(group, std::string(name) + "-host")
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
                                  .Zip = "update-2.zip"});
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

    /// <summary>An Ed25519 key pair made from one seed byte, so each test's keys are the same every run.</summary>
    struct KeyPair
    {
        explicit KeyPair(uint8_t seed)
        {
            std::array<uint8_t, crypto_sign_ed25519_SEEDBYTES> bytes{};
            bytes.fill(seed);
            crypto_sign_ed25519_seed_keypair(Public.data(), Secret.data(), bytes.data());
        }

        /// <summary>The signature of <paramref name="message"/>.</summary>
        std::vector<uint8_t> Sign(std::string_view message) const
        {
            std::vector<uint8_t> signature(crypto_sign_ed25519_BYTES);
            crypto_sign_ed25519_detached(signature.data(), nullptr,
                                         reinterpret_cast<const unsigned char*>(message.data()), message.size(),
                                         Secret.data());
            return signature;
        }

        /// <summary>An envelope of <paramref name="payload"/> as <paramref name="type"/>, signed by this key.</summary>
        std::string Envelope(std::string_view type, std::string_view payload) const
        {
            const std::array signatures = {Sign(MCSignedFile::SignedBytes(type, payload))};
            return MCSignedFile::Envelope(type, payload, signatures);
        }

        MCSignedFile::MCPublicKey Public{};
        std::array<uint8_t, crypto_sign_ed25519_SECRETKEYBYTES> Secret{};
    };

    /// <summary>The key list naming <paramref name="releaseKeys"/>, signed by <paramref name="root"/>.</summary>
    inline std::string KeyList(const KeyPair& root, uint64_t issue, std::vector<MCSignedFile::MCPublicKey> releaseKeys,
                               uint64_t lowestRelease = 0)
    {
        MCReleaseFile::MCKeyList keys{
            .Format = 1, .Issue = issue, .ReleaseKeys = std::move(releaseKeys), .LowestRelease = lowestRelease};
        return root.Envelope(MCSignedFile::KeyListType, MCReleaseFile::KeyListPayload(keys));
    }
}

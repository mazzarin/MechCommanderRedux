#include "stdafx.h"
#include "MCContentFixture.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "lib/MCOptionalRef.h"
#include "lib/MCSha256.h"
#include "platform/MCFileSystem.h"
#include "update/MCProgramSwap.h"
#include "update/MCReleaseFile.h"
#include "update/MCUpdateDisk.h"

// The Windows program's update put in place by the game itself (update/MCProgramSwap.h), on this machine's disk in a
// scratch folder: the program's files renamed aside and the staged ones into their places, the game's other names
// made links to the new program, and a swap stopped at any step ending all old or all new at the next start.

namespace
{
    using namespace MCContentFixture;
    using MCProgramSwap::MCOutcome;

    constexpr std::string_view Group = "mc_program_swap_tests";
    constexpr uint64_t OldBuild = 500;
    constexpr uint64_t NewBuild = 600;
    constexpr std::string_view NewVersion = "0.6.1 (600)";
    constexpr std::string_view ZipName = "program-600.zip";
    constexpr std::string_view Blocked = "Windows blocked the update (Smart App Control, error 4551)";

    constexpr std::string_view Game = "MCRedux.exe";
    constexpr std::string_view Sdl = "SDL3.dll";
    constexpr std::string_view Editor = "MCEditor.exe";
    constexpr std::string_view Readme = "MechCommander Redux HD README.txt";
    constexpr std::string_view Licences = "MechCommander Redux HD licences.txt";

    /// <summary>The game's other names: the original's, links and a copy of the game, and one the player's own.</summary>
    constexpr std::string_view Original = "MCX.EXE";
    constexpr std::string_view LinkedResolution = "mcx1024x768.exe";
    constexpr std::string_view CopiedResolution = "MCX1920x1080.EXE";
    constexpr std::string_view OwnResolution = "MCX800x600.EXE";
    constexpr std::string_view OwnResolutionBytes = "the original at 800x600";

    /// <summary>Each program file's old bytes (none: the folder lacks it) and new ones.</summary>
    struct MCFileBytes
    {
        std::string_view Name;
        std::optional<std::string_view> Old;
        std::string_view New;
    };

    // The README is the same in both: it's never moved.
    constexpr std::array<MCFileBytes, 5> Files{{{Game, "the game, build 500", "the game, build 600"},
                                                {Sdl, "SDL, build 500", "SDL, build 600"},
                                                {Editor, "the editor, build 500", "the editor, build 600"},
                                                {Readme, "the README", "the README"},
                                                {Licences, std::nullopt, "the licences"}}};

    /// <summary>The new files' zip, as the release script makes it.</summary>
    std::string ZipBytes()
    {
        std::vector<std::pair<std::string, std::string>> entries;

        for (const MCFileBytes& file : Files)
        {
            entries.emplace_back(file.Name, file.New);
        }

        return StoredZip(entries);
    }

    /// <summary>Build 600's program as release.json names it, at setup level 1.</summary>
    MCReleaseFile::MCProgram Program()
    {
        MCReleaseFile::MCProgram program{.Build = NewBuild, .SetupLevel = 1};

        for (const MCFileBytes& file : Files)
        {
            program.Files.push_back(
                {.Name = std::string(file.Name), .Size = file.New.size(), .Sha256 = MCSha256::Of(file.New)});
        }

        const std::string zip = ZipBytes();
        program.Zip = MCReleaseFile::MCHostedFile{
            .Url = std::format("https://example.test/{}", ZipName), .Size = zip.size(), .Sha256 = MCSha256::Of(zip)};
        return program;
    }

    /// <summary>
    /// A copy of build 500 with build 600's files staged: MCX.EXE and a resolution's name links to the game, another
    /// resolution's a copy of it, one the player's own program.
    /// </summary>
    class MCCopy
    {
    public:
        explicit MCCopy(std::string_view name) : _Folder(Group, name)
        {
            for (const MCFileBytes& file : Files)
            {
                if (file.Old)
                {
                    Write(At(file.Name), *file.Old);
                }

                Write(Staged(file.Name), file.New);
            }

            std::filesystem::create_hard_link(At(Game), At(Original));
            std::filesystem::create_hard_link(At(Game), At(LinkedResolution));
            Write(At(CopiedResolution), *Files[0].Old);
            Write(At(OwnResolution), OwnResolutionBytes);
        }

        const std::filesystem::path& Path() const { return _Folder.Path(); }

        std::filesystem::path At(std::string_view name) const { return Path() / MCFileSystem::FromUtf8(name); }

        std::filesystem::path Staged(std::string_view name) const
        {
            return Path() / MCFileSystem::FromUtf8(MCProgramSwap::StagedFolder) / std::to_string(NewBuild) /
                   MCFileSystem::FromUtf8(name);
        }

        std::filesystem::path Journal() const { return Path() / MCFileSystem::FromUtf8(MCProgramSwap::JournalFile); }

        /// <summary>Why the folder isn't wholly build <paramref name="build"/>'s program, or empty.</summary>
        std::string NotWhole(uint64_t build) const
        {
            const bool fresh = build == NewBuild;

            for (const MCFileBytes& file : Files)
            {
                const std::optional<std::string_view> wanted = fresh ? std::optional(file.New) : file.Old;

                if (wanted ? !std::filesystem::exists(At(file.Name)) || Read(At(file.Name)) != *wanted
                           : std::filesystem::exists(At(file.Name)))
                {
                    return std::format("{} isn't build {}'s", file.Name, build);
                }
            }

            const std::string_view game = fresh ? Files[0].New : *Files[0].Old;

            for (const std::string_view name : {Original, LinkedResolution, CopiedResolution})
            {
                if (Read(At(name)) != game)
                {
                    return std::format("{} isn't build {}'s game", name, build);
                }
            }

            // The links stay links: one file with the game.
            for (const std::string_view name : {Original, LinkedResolution})
            {
                std::error_code error;

                if (!std::filesystem::equivalent(At(Game), At(name), error))
                {
                    return std::format("{} isn't a link to {}", name, Game);
                }
            }

            if (Read(At(OwnResolution)) != OwnResolutionBytes)
            {
                return std::format("the player's own {} changed", OwnResolution);
            }

            return std::filesystem::exists(Journal()) ? "the journal is still there" : "";
        }

    private:
        MCScratchFolder _Folder;
    };

    /// <summary>What the stand-in for Windows was asked: the programs judged, the version shown.</summary>
    struct MCWindowsLog
    {
        std::vector<std::filesystem::path> Judged;
        std::vector<std::string> Shown;
        /// <summary>Why it won't run a program (empty: it runs it).</summary>
        std::string Refusal;
    };

    MCProgramSwap::MCPlatform FakeWindows(const std::shared_ptr<MCWindowsLog>& log)
    {
        return {.Runnable =
                    [log](const std::filesystem::path& program)
                {
                    log->Judged.push_back(program);
                    return log->Refusal;
                },
                .ShowVersion =
                    [log](const std::filesystem::path&, std::string_view version)
                {
                    log->Shown.emplace_back(version);
                    return std::string();
                }};
    }

    /// <summary>The program stopping: thrown by the disk at the step a test picks.</summary>
    struct MCStopped
    {
    };

    /// <summary>
    /// The disk, stopping the program at the <see cref="StopAt"/>th change (a move, deletion, write, made folder or
    /// flush): what was done before stays, as a program's death leaves it (a power cut's lost writes are the content
    /// swap's tests', on a disk in memory). Its flushes are counted but not made, to keep the test fast. A file held
    /// open can't be moved.
    /// </summary>
    class MCStoppingDisk final : public MCUpdateDisk
    {
    public:
        std::optional<int> StopAt;
        int Changes = 0;
        std::filesystem::path Held;
        /// <summary>Whether the journal was put in its place before the stop.</summary>
        bool JournalWritten = false;
        std::filesystem::path JournalPath;

        MCStat Stat(const std::filesystem::path& path) override { return _Disk->Stat(path); }
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
            return _Disk->ReadText(path);
        }

        std::expected<std::vector<std::string>, std::string> Files(const std::filesystem::path& folder) override
        {
            return _Disk->Files(folder);
        }

        bool MakeFolders(const std::filesystem::path& path) override { return Step() && _Disk->MakeFolders(path); }

        bool Rename(const std::filesystem::path& from, const std::filesystem::path& to) override
        {
            const bool renamed = Step() && _Disk->Rename(from, to);
            JournalWritten = JournalWritten || (renamed && to == JournalPath);
            return renamed;
        }

        bool Move(const std::filesystem::path& from, const std::filesystem::path& to) override
        {
            return Step() && from != Held && to != Held && _Disk->Move(from, to);
        }

        bool Remove(const std::filesystem::path& path) override { return Step() && _Disk->Remove(path); }
        bool RemoveIfEmpty(const std::filesystem::path& path) override { return Step() && _Disk->RemoveIfEmpty(path); }

        bool WriteFlushed(const std::filesystem::path& path, std::string_view bytes) override
        {
            return Step() && _Disk->WriteFlushed(path, bytes);
        }

        bool FlushFile(const std::filesystem::path&) override { return Step(); }
        bool FlushFolder(const std::filesystem::path&) override { return Step(); }

        std::expected<uint64_t, std::string> FreeBytes(const std::filesystem::path& path) override
        {
            return _Disk->FreeBytes(path);
        }

    private:
        bool Step()
        {
            if (++Changes == StopAt)
            {
                throw MCStopped{};
            }

            return true;
        }

        std::unique_ptr<MCUpdateDisk> _Disk = MakeDisk();
    };

    /// <summary>A start of build <paramref name="running"/> in <paramref name="copy"/> (on the disk unless one is given).</summary>
    MCProgramSwap::MCResult RecoverAt(const MCCopy& copy, uint64_t running,
                                      MCOptionalRef<MCUpdateDisk> disk = std::nullopt)
    {
        const std::unique_ptr<MCUpdateDisk> made = disk ? nullptr : MakeDisk();
        return MCProgramSwap::Recover(disk ? disk->get() : *made, copy.Path(), running,
                                      FakeWindows(std::make_shared<MCWindowsLog>()));
    }
}

TEST_CASE("program swap: the staged files go in, the game last; the game's other names that were it are made the new "
          "one, links staying links; an unchanged file stays; the next start clears what was set aside")
{
    MCCopy copy("swap");
    const auto log = std::make_shared<MCWindowsLog>();
    const auto swapped = MCProgramSwap::Swap(*MakeDisk(), copy.Path(), Program(), NewVersion, FakeWindows(log));
    REQUIRE_EQ(swapped.error_or(""), std::string());
    CHECK_EQ(copy.NotWhole(NewBuild), std::string());

    // Windows was asked about the new editor and game, as staged; the Apps list shows the new version.
    CHECK((log->Judged == std::vector{copy.Staged(Editor), copy.Staged(Game)}));
    CHECK(log->Shown == std::vector{std::string(NewVersion)});

    // The old files are set aside, the unchanged README never moved; the staged folder is gone.
    const std::filesystem::path old = copy.Path() / MCFileSystem::FromUtf8(MCProgramSwap::OldFolder) / "600";
    CHECK_EQ(Read(old / Game), std::string(*Files[0].Old));
    CHECK_EQ(Read(old / Original), std::string(*Files[0].Old));
    CHECK(!std::filesystem::exists(old / Readme));
    CHECK(!std::filesystem::exists(old / OwnResolution));
    CHECK(!std::filesystem::exists(copy.Staged(Game).parent_path()));

    // The new build's start: nothing under way, and what was set aside goes with the swap's folder.
    CHECK(RecoverAt(copy, NewBuild).Outcome == MCOutcome::Nothing);
    CHECK(!std::filesystem::exists(copy.Path() / MCFileSystem::FromUtf8(MCProgramSwap::SwapFolder)));
    CHECK_EQ(copy.NotWhole(NewBuild), std::string());
}

TEST_CASE("program swap: Windows refusing a new program, or a staged file changed since staging, changes nothing")
{
    {
        MCCopy copy("refused");
        const auto log = std::make_shared<MCWindowsLog>();
        log->Refusal = Blocked;
        const auto swapped = MCProgramSwap::Swap(*MakeDisk(), copy.Path(), Program(), NewVersion, FakeWindows(log));
        REQUIRE(!swapped.has_value());
        CHECK_EQ(swapped.error(), std::string(Blocked));
        CHECK_EQ(copy.NotWhole(OldBuild), std::string());
        CHECK(log->Shown.empty());
    }

    MCCopy copy("damaged");
    Write(copy.Staged(Sdl), "SDL, changed");
    const auto swapped = MCProgramSwap::Swap(*MakeDisk(), copy.Path(), Program(), NewVersion,
                                             FakeWindows(std::make_shared<MCWindowsLog>()));
    REQUIRE(!swapped.has_value());
    CHECK(swapped.error().find("SDL3.dll isn't staged as release.json names it") != std::string::npos);
    CHECK_EQ(copy.NotWhole(OldBuild), std::string());
}

TEST_CASE("program swap: a file held open puts the old files back at once, the new ones staged again for a later try")
{
    MCCopy copy("held");
    MCStoppingDisk disk;
    disk.Held = copy.At(Game);
    const auto swapped =
        MCProgramSwap::Swap(disk, copy.Path(), Program(), NewVersion, FakeWindows(std::make_shared<MCWindowsLog>()));
    REQUIRE(!swapped.has_value());
    CHECK_EQ(swapped.error(), std::string("MCRedux.exe is in use"));
    CHECK_EQ(copy.NotWhole(OldBuild), std::string());

    for (const MCFileBytes& file : Files)
    {
        CHECK_EQ(Read(copy.Staged(file.Name)), std::string(file.New));
    }

    // Let go: it goes in.
    disk.Held.clear();
    REQUIRE(MCProgramSwap::Swap(disk, copy.Path(), Program(), NewVersion, FakeWindows(std::make_shared<MCWindowsLog>()))
                .has_value());
    CHECK_EQ(copy.NotWhole(NewBuild), std::string());
}

TEST_CASE("program swap: stopped at any step, the next start ends it all old or all new: new once the journal is "
          "written, unless a staged file is gone by then")
{
    int stops = 0;

    for (int stopAt = 1;; ++stopAt)
    {
        for (const bool loseStaged : {false, true})
        {
            MCCopy copy(std::format("stop-{}-{}", stopAt, loseStaged));
            MCStoppingDisk disk;
            disk.StopAt = stopAt;
            disk.JournalPath = copy.Journal();

            try
            {
                const auto swapped = MCProgramSwap::Swap(disk, copy.Path(), Program(), NewVersion,
                                                         FakeWindows(std::make_shared<MCWindowsLog>()));
                REQUIRE_EQ(swapped.error_or(""), std::string());
                CHECK_EQ(copy.NotWhole(NewBuild), std::string());
                CHECK(stops > 10);
                return;
            }
            catch (const MCStopped&)
            {
            }

            stops += loseStaged ? 0 : 1;

            // The game not yet moved, its staged copy lost (a disk's cleaner, a failing drive): back to the old.
            const bool lost = loseStaged && std::filesystem::exists(copy.Staged(Game));

            if (lost)
            {
                std::filesystem::remove(copy.Staged(Game));
            }

            // The old program's start (by MCX.EXE, or by MCRedux.exe while it's still the old one): with the journal,
            // forward, or back once a staged file is lost; without it, nothing, as the swap had either not begun or
            // committed.
            const bool underWay = std::filesystem::exists(copy.Journal());
            const bool fresh = disk.JournalWritten && !lost;
            MCStoppingDisk starting;
            const std::string stopped =
                std::format("stopped at step {}{}", stopAt, lost ? ", the staged game then lost" : "");
            const MCOutcome wanted = !underWay ? MCOutcome::Nothing : lost ? MCOutcome::PutBack : MCOutcome::Finished;

            if (const MCProgramSwap::MCResult recovered = RecoverAt(copy, OldBuild, starting);
                recovered.Outcome != wanted)
            {
                FAIL_CHECK(std::format("{}: the next start's outcome is {}, not {} ({})", stopped,
                                       static_cast<int>(recovered.Outcome), static_cast<int>(wanted), recovered.Error));
            }

            if (const std::string mixed = copy.NotWhole(fresh ? NewBuild : OldBuild); !mixed.empty())
            {
                FAIL_CHECK(std::format("{}: after the next start, {}", stopped, mixed));
            }

            // A later start finds nothing under way.
            if (RecoverAt(copy, fresh ? NewBuild : OldBuild, starting).Outcome != MCOutcome::Nothing ||
                !copy.NotWhole(fresh ? NewBuild : OldBuild).empty())
            {
                FAIL_CHECK(std::format("{}: the start after that found something left", stopped));
            }
        }
    }
}

TEST_CASE("program swap: staged from its zip, each file checked; the files staged already and right aren't fetched "
          "again; other builds' staged files go; a zip not as release.json names it stages nothing")
{
    MCScratchFolder game(Group, "stage");
    MCScratchFolder host(Group, "stage-host");
    Write(host.Path() / ZipName, ZipBytes());
    const std::filesystem::path staged = game.Path() / MCFileSystem::FromUtf8(MCProgramSwap::StagedFolder);
    Write(staged / "580" / "MCRedux.exe", "an update given up");
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    uint64_t received = 0;
    const auto counting = [&](uint64_t bytes)
    {
        received += bytes;
        return true;
    };

    MCFolderSource source(host.Path());
    REQUIRE(MCProgramSwap::Stage(*disk, source, ZipName, game.Path(), Program(), counting).has_value());
    CHECK_EQ(received, Program().Zip->Size);

    for (const MCFileBytes& file : Files)
    {
        CHECK_EQ(Read(staged / "600" / file.Name), std::string(file.New));
    }

    CHECK(!std::filesystem::exists(staged / "580"));
    CHECK(
        !std::filesystem::exists(game.Path() / MCFileSystem::FromUtf8(MCProgramSwap::SwapFolder) / "program-600.zip"));

    // Staged already: nothing is fetched.
    CHECK(MCProgramSwap::Stage(*disk, source, ZipName, game.Path(), Program(), counting).has_value());
    CHECK_EQ(source.Asked().size(), size_t{1});

    // One file damaged: it's unpacked again.
    Write(staged / "600" / "SDL3.dll", "SDL, changed");
    CHECK(MCProgramSwap::Stage(*disk, source, ZipName, game.Path(), Program(), counting).has_value());
    CHECK_EQ(Read(staged / "600" / "SDL3.dll"), std::string("SDL, build 600"));

    // A zip release.json doesn't name.
    MCScratchFolder other(Group, "stage-other");
    MCReleaseFile::MCProgram program = Program();
    program.Zip->Sha256 = MCSha256::Of("another zip");
    MCFolderSource otherSource(host.Path());
    const auto refused = MCProgramSwap::Stage(*disk, otherSource, ZipName, other.Path(), program, counting);
    REQUIRE(!refused.has_value());
    CHECK_EQ(refused.error(), std::string("the program's download isn't the one release.json names"));
    CHECK(!std::filesystem::exists(other.Path() / MCFileSystem::FromUtf8(MCProgramSwap::StagedFolder) / "600" / Game));
}

TEST_CASE("program swap: the setup's level is the copy's record's (0 without one); a release swaps only at it, with "
          "its zip and the game among its files")
{
    MCScratchFolder game(Group, "level");
    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    CHECK_EQ(MCProgramSwap::SetupLevel(*disk, game.Path()), uint64_t{0});
    const std::filesystem::path record = game.Path() / MCFileSystem::FromUtf8(MCProgramSwap::SetupFile);
    Write(record, R"({"level": 3})");
    CHECK_EQ(MCProgramSwap::SetupLevel(*disk, game.Path()), uint64_t{3});
    Write(record, "not JSON");
    CHECK_EQ(MCProgramSwap::SetupLevel(*disk, game.Path()), uint64_t{0});

    MCReleaseFile::MCProgram program = Program();
    CHECK(MCProgramSwap::Swaps(1, program));
    CHECK(!MCProgramSwap::Swaps(0, program));
    CHECK(!MCProgramSwap::Swaps(2, program));
    program.SetupLevel = 0;
    CHECK(!MCProgramSwap::Swaps(0, program));
    program = Program();
    program.Zip.reset();
    CHECK(!MCProgramSwap::Swaps(1, program));
    program = Program();
    std::erase_if(program.Files, [](const MCReleaseFile::MCProgramFile& file) { return file.Name == Game; });
    CHECK(!MCProgramSwap::Swaps(1, program));
}

TEST_CASE("program swap: a journal that can't be read, or that names a file outside the game folder's top, leaves "
          "every file as it is")
{
    for (const std::string_view journal :
         {std::string_view("not JSON"),
          std::string_view(R"({"build": 600, "version": "0.6.1", "files": [{"name": "../MCRedux.exe", "size": 1, )"
                           R"("sha256": "0000000000000000000000000000000000000000000000000000000000000000", )"
                           R"("had": true}], "names": []})")})
    {
        MCCopy copy("unreadable");
        Write(copy.Journal(), journal);
        const MCProgramSwap::MCResult recovered = RecoverAt(copy, OldBuild);
        CHECK(recovered.Outcome == MCOutcome::Stuck);
        CHECK(std::filesystem::exists(copy.Journal()));
        std::filesystem::remove(copy.Journal());
        CHECK_EQ(copy.NotWhole(OldBuild), std::string());
    }
}

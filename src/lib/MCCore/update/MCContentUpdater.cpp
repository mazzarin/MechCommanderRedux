#include "stdafx.h"
#include "update/MCContentUpdater.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentFolder.h"
#include "update/MCContentSwap.h"
#include "update/MCInstalled.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateJson.h"
#include "update/MCUpdateLock.h"
#include "update/MCUpdatePlace.h"

#include <random>

namespace
{
    using namespace MCContentUpdater;

    /// <summary>A file made and deleted in a folder to see whether this account can write there.</summary>
    constexpr std::string_view WriteProbe = ".redux-write-test";

    /// <summary>The HD pack's folder in the game folder: its being there means the copy has HD.</summary>
    constexpr std::string_view HdFolder = "HD";

    /// <summary>The chosen sets' file: its format, and its sets.</summary>
    constexpr std::string_view FormatKey = "format";
    constexpr std::string_view SetsKey = "sets";
    constexpr uint64_t ChosenFormat = 1;

    std::filesystem::path KeptPath(const MCSetup& setup)
    {
        return setup.UserFolder / MCFileSystem::FromUtf8(MCReleaseFile::KeptFile);
    }

    /// <summary><c>update.json</c> as kept: none yet is new, and one that can't be read starts again (logged).</summary>
    MCReleaseFile::MCKept ReadKept(const MCSetup& setup, MCUpdateDisk& disk)
    {
        const std::filesystem::path path = KeptPath(setup);
        const auto kept = MCUpdateJson::ReadFile(disk, path, MCReleaseFile::ReadKept);

        if (!kept)
        {
            SDL_Log("Updates: %s can't be read (%s): it starts again", MCFileSystem::ToUtf8(path).c_str(),
                    kept.error().c_str());
            return {};
        }

        return kept->value_or(MCReleaseFile::MCKept{});
    }

    std::expected<void, std::string> WriteKept(const MCSetup& setup, MCUpdateDisk& disk,
                                               const MCReleaseFile::MCKept& kept)
    {
        const std::filesystem::path path = KeptPath(setup);

        if (!WriteDurably(disk, path, MCReleaseFile::KeptText(kept)))
        {
            return std::unexpected(std::format("{} can't be written", MCFileSystem::ToUtf8(path)));
        }

        return {};
    }

    /// <summary>The content list a plan reads: its hash (its name on the host), its size when known, its release.</summary>
    struct MCListRef
    {
        MCSha256::MCDigest Hash{};
        std::optional<uint64_t> Size;
        uint64_t Release = 0;
    };

    /// <summary>A list's file name, on the host and kept: its hash.</summary>
    std::string ListFileName(const MCSha256::MCDigest& hash)
    {
        return std::format("{}.json", MCSha256::Hex(hash));
    }

    std::filesystem::path KeptListsPath(const MCSetup& setup)
    {
        return setup.GameFolder / MCFileSystem::FromUtf8(KeptListsFolder);
    }

    /// <summary>Whether <paramref name="text"/> is the list <paramref name="list"/> names: its hash, and its size when known.</summary>
    bool IsList(std::string_view text, const MCListRef& list)
    {
        return (!list.Size || text.size() == *list.Size) && MCSha256::Of(text) == list.Hash;
    }

    /// <summary>
    /// The list kept from an earlier fetch; none when none is, or when the one kept isn't the list its name says
    /// (logged: it's fetched again).
    /// </summary>
    std::optional<std::string> KeptList(const MCSetup& setup, MCUpdateDisk& disk, const MCListRef& list)
    {
        const std::filesystem::path path = KeptListsPath(setup) / MCFileSystem::FromUtf8(ListFileName(list.Hash));
        const auto kept = MCUpdateJson::ReadFile(
            disk, path,
            [&](std::string_view text) -> std::expected<std::string, std::string>
            {
                if (!IsList(text, list))
                {
                    return std::unexpected(std::format("{} isn't the list its name says", MCFileSystem::ToUtf8(path)));
                }

                return std::string(text);
            });

        if (!kept)
        {
            SDL_Log("Updates: %s: it's fetched again", kept.error().c_str());
            return std::nullopt;
        }

        return *kept;
    }

    /// <summary>
    /// Keeps <paramref name="text"/>, the list just fetched, for later checks, and deletes every other list kept but
    /// the installed release's. What fails is logged: a later check fetches the list again.
    /// </summary>
    void KeepList(const MCSetup& setup, MCUpdateDisk& disk, const MCListRef& list, std::string_view text)
    {
        const std::filesystem::path folder = KeptListsPath(setup);
        const std::string name = ListFileName(list.Hash);

        if (!disk.MakeFolders(folder) || !WriteDurably(disk, folder / MCFileSystem::FromUtf8(name), text))
        {
            SDL_Log("Updates: release %llu's list can't be kept in %s: it's fetched again at the next check",
                    static_cast<unsigned long long>(list.Release), MCFileSystem::ToUtf8(folder).c_str());
            return;
        }

        const auto installed = MCInstalled::Read(setup.GameFolder);
        const auto files = disk.Files(folder);

        if (!installed || !files)
        {
            SDL_Log("Updates: the lists kept stay as they are (%s)",
                    (!installed ? installed.error() : files.error()).c_str());
            return;
        }

        const std::string installedName = ListFileName(installed->List);

        for (const std::string& file : *files)
        {
            if (file != name && file != installedName && !disk.Remove(folder / MCFileSystem::FromUtf8(file)))
            {
                SDL_Log("Updates: the list kept as %s stays", file.c_str());
            }
        }
    }

    /// <summary>
    /// The list of <paramref name="list"/>'s release held to its hash (and size): the one kept, else fetched from
    /// <paramref name="source"/> and kept; for a program of build <paramref name="build"/>; its plan still to make
    /// (<see cref="PlanFor"/>).
    /// </summary>
    /// <returns>The content, its plan empty; none for a list in a format this game can't read; or why there's none.</returns>
    std::expected<std::shared_ptr<MCContent>, std::string> FetchList(const MCSetup& setup, MCUpdateDisk& disk,
                                                                     MCContentStage::MCSource& source,
                                                                     const MCListRef& list, uint64_t build)
    {
        std::optional<std::string> text = KeptList(setup, disk, list);

        if (!text)
        {
            const std::string name = std::format("{}/{}", ListsFolder, ListFileName(list.Hash));
            auto fetched = source.Read(name, MostListBytes);

            if (!fetched)
            {
                return std::unexpected(fetched.error());
            }

            if (!IsList(*fetched, list))
            {
                return std::unexpected(std::format("{} isn't the list release {} names", name, list.Release));
            }

            KeepList(setup, disk, list, *fetched);
            text = std::move(*fetched);
        }

        auto read = MCContentList::Read(*text);

        if (!read)
        {
            return std::unexpected(read.error());
        }

        if (read->Format > MCContentList::KnownFormat)
        {
            SDL_Log("Updates: release %llu's list is format %llu, newer than this game reads",
                    static_cast<unsigned long long>(list.Release), static_cast<unsigned long long>(read->Format));
            return nullptr;
        }

        if (read->LowestProgram > build)
        {
            return std::unexpected(std::format("release {}'s content needs build {} of the program, newer than {}",
                                               list.Release, read->LowestProgram, build));
        }

        // The plan points into the list, so both stay together where they're made.
        auto content = std::make_shared<MCContent>();
        content->List = std::move(*read);
        return content;
    }

    std::filesystem::path HashesPath(const MCSetup& setup)
    {
        return setup.GameFolder / MCFileSystem::FromUtf8(MCContentPlan::HashesFile);
    }

    /// <summary>The hashes the last plan kept: none yet, or ones that can't be read (logged), are none.</summary>
    MCContentPlan::MCHashes ReadHashes(const MCSetup& setup, MCUpdateDisk& disk)
    {
        const auto hashes = MCUpdateJson::ReadFile(disk, HashesPath(setup), MCContentPlan::ReadHashes);

        if (!hashes)
        {
            SDL_Log("Updates: the files' hashes kept can't be read (%s): the files are hashed again",
                    hashes.error().c_str());
            return {};
        }

        return hashes->value_or(MCContentPlan::MCHashes{});
    }

    /// <summary>
    /// Plans <paramref name="content"/>'s list for the copy: <paramref name="sets"/> against what
    /// <paramref name="installed"/> records, and the hashes the last plan kept; this plan's are kept in their place.
    /// </summary>
    std::expected<void, std::string> PlanFor(const MCSetup& setup, MCUpdateDisk& disk, MCContent& content,
                                             const MCListRef& list, const MCInstalled::MCRecord& installed,
                                             const MCSets& sets)
    {
        const MCContentPlan::MCHashes hashes = ReadHashes(setup, disk);
        MCContentFolder folder(setup.GameFolder);
        auto plan = MCContentPlan::Make(folder, installed, content.List, list.Hash, sets, hashes);

        if (!plan)
        {
            return std::unexpected(plan.error());
        }

        if (plan->HashedFiles != 0)
        {
            SDL_Log("Updates: the plan for release %llu hashed %zu of its files",
                    static_cast<unsigned long long>(list.Release), plan->HashedFiles);
        }

        // The plan stands without them: the next one hashes those files again.
        if (const std::filesystem::path path = HashesPath(setup);
            plan->Hashes != hashes && (!disk.MakeFolders(path.parent_path()) ||
                                       !WriteDurably(disk, path, MCContentPlan::HashesText(plan->Hashes))))
        {
            SDL_Log("Updates: %s can't be written: the next plan hashes its files again",
                    MCFileSystem::ToUtf8(path).c_str());
        }

        content.Plan = std::move(*plan);
        return {};
    }

    /// <summary>
    /// The content of <paramref name="list"/>'s release planned for the copy (<see cref="FetchList"/>, then
    /// <see cref="PlanFor"/>).
    /// </summary>
    /// <returns>The content; none for a list in a format this game can't read; or why there's none.</returns>
    std::expected<std::shared_ptr<const MCContent>, std::string> PlanContent(const MCSetup& setup, MCUpdateDisk& disk,
                                                                             MCContentStage::MCSource& source,
                                                                             const MCListRef& list,
                                                                             const MCInstalled::MCRecord& installed,
                                                                             const MCSets& sets, uint64_t build)
    {
        auto content = FetchList(setup, disk, source, list, build);

        if (!content)
        {
            return std::unexpected(content.error());
        }

        if (*content == nullptr)
        {
            return nullptr;
        }

        if (const auto planned = PlanFor(setup, disk, **content, list, installed, sets); !planned)
        {
            return std::unexpected(planned.error());
        }

        return std::shared_ptr<const MCContent>(std::move(*content));
    }

    /// <summary>The release this copy takes, and what the game keeps of the files read.</summary>
    struct MCReleaseFor
    {
        MCReleaseFile::MCRelease Release;
        MCReleaseFile::MCKept Kept;
    };

    /// <summary>
    /// The key list and <c>release.json</c> read from <paramref name="source"/> and checked, kept in
    /// <c>update.json</c>, and the release this copy's rollout number gives it.
    /// </summary>
    std::expected<MCReleaseFor, std::string> ReleaseFor(const MCSetup& setup, MCUpdateDisk& disk,
                                                        MCContentStage::MCSource& source)
    {
        const MCReleaseFile::MCKept before = ReadKept(setup, disk);
        MCReleaseFile::MCKept kept = before;

        if (!kept.RolloutNumber)
        {
            std::random_device random;
            kept.RolloutNumber = static_cast<uint32_t>(random());
        }

        const auto keyList = source.Read(KeyListName, MostSignedBytes);

        if (!keyList)
        {
            return std::unexpected(keyList.error());
        }

        const auto releases = source.Read(ReleasesName, MostSignedBytes);

        if (!releases)
        {
            return std::unexpected(releases.error());
        }

        const auto trusted = MCReleaseFile::Check(*keyList, *releases, setup.RootKeys, kept);

        if (!trusted)
        {
            return std::unexpected(trusted.error());
        }

        kept = MCReleaseFile::Keep(kept, *trusted);

        if (kept != before)
        {
            // The check goes on without it: the next start reads the files again.
            if (const auto written = WriteKept(setup, disk, kept); !written)
            {
                SDL_Log("Updates: %s", written.error().c_str());
            }
        }

        return MCReleaseFor{MCReleaseFile::For(trusted->Releases, *kept.RolloutNumber), kept};
    }

    std::filesystem::path ChosenPath(const std::filesystem::path& gameFolder)
    {
        return gameFolder / MCFileSystem::FromUtf8(ChosenFile);
    }
}

uint64_t MCContentUpdater::MCOffer::ContentBytes() const
{
    return Content != nullptr ? Content->Plan.DownloadBytes : 0;
}

bool MCContentUpdater::MCOffer::Quiet() const
{
    return !ProgramBuild.has_value() && Content != nullptr && !Content->Plan.Changes();
}

std::string MCContentUpdater::MCOffer::ShownVersion(uint64_t runningBuild) const
{
    return std::format("{} ({})", Release.Version, ProgramBuild.value_or(runningBuild));
}

std::expected<std::optional<MCOffer>, std::string> MCContentUpdater::Check(const MCSetup& setup, MCUpdateDisk& disk,
                                                                           MCContentStage::MCSource& source)
{
    const auto chosen = ReleaseFor(setup, disk, source);

    if (!chosen)
    {
        return std::unexpected(chosen.error());
    }

    const MCReleaseFile::MCRelease& release = chosen->Release;

    if (release.Number == chosen->Kept.Skipped)
    {
        SDL_Log("Updates: release %llu was skipped", static_cast<unsigned long long>(release.Number));
        return std::nullopt;
    }

    MCOffer offer{.Release = release};

    if (const auto program = release.Programs.find(setup.Platform);
        program != release.Programs.end() && program->second.Build > setup.RunningBuild)
    {
        offer.ProgramBuild = program->second.Build;
        offer.ProgramBytes = program->second.Size;
    }

    const auto installed = MCInstalled::Read(setup.GameFolder);

    if (!installed)
    {
        return std::unexpected(installed.error());
    }

    const bool newContent = release.Number > installed->Release;

    if (!newContent && !offer.ProgramBuild)
    {
        return std::nullopt;
    }

    if (MCContentSwap::Waiting(disk, setup.GameFolder))
    {
        SDL_Log("Updates: an update already waits for the next start");
        return std::nullopt;
    }

    if (const auto writable = Writable(setup.GameFolder); !writable)
    {
        return std::unexpected(writable.error());
    }

    if (newContent)
    {
        auto content =
            PlanContent(setup, disk, source, {release.List, release.ListSize, release.Number}, *installed,
                        Wanted(disk, setup.GameFolder, *installed), offer.ProgramBuild.value_or(setup.RunningBuild));

        if (!content)
        {
            return std::unexpected(content.error());
        }

        if (*content == nullptr)
        {
            SDL_Log("Updates: release %llu's program comes alone", static_cast<unsigned long long>(release.Number));
        }

        offer.Content = std::move(*content);
    }

    // A list this game can't read, and no program to read it: nothing here can take the release.
    if (offer.Content == nullptr && !offer.ProgramBuild)
    {
        return std::nullopt;
    }

    return offer;
}

std::expected<std::optional<MCOffer>, std::string> MCContentUpdater::CheckSets(const MCSetup& setup, MCUpdateDisk& disk,
                                                                               MCContentStage::MCSource& source)
{
    const auto installed = MCInstalled::Read(setup.GameFolder);

    if (!installed)
    {
        return std::unexpected(installed.error());
    }

    const MCSets sets = Wanted(disk, setup.GameFolder, *installed);

    if (sets == installed->Sets)
    {
        return std::nullopt;
    }

    if (installed->Release == 0)
    {
        return std::unexpected("no release is installed yet");
    }

    if (MCContentSwap::Waiting(disk, setup.GameFolder))
    {
        return std::unexpected("an update waits for the game's next start");
    }

    if (const auto writable = Writable(setup.GameFolder); !writable)
    {
        return std::unexpected(writable.error());
    }

    MCOffer offer{.Release = {.Number = installed->Release, .List = installed->List}};

    // Sets taken out alone (HD OPTIONS' REMOVE): planned from the record, so neither the host nor a list is needed.
    if (std::ranges::includes(installed->Sets, sets))
    {
        MCContentFolder folder(setup.GameFolder);
        auto removal = MCContentPlan::Removal(folder, *installed, sets, ReadHashes(setup, disk));

        if (!removal)
        {
            return std::unexpected(removal.error());
        }

        auto content = std::make_shared<MCContent>();
        content->List.Release = installed->Release;
        content->Plan = std::move(*removal);
        offer.Content = std::move(content);
        return offer;
    }

    auto content = PlanContent(setup, disk, source, {installed->List, std::nullopt, installed->Release}, *installed,
                               sets, setup.RunningBuild);

    if (!content)
    {
        return std::unexpected(content.error());
    }

    if (*content == nullptr)
    {
        return std::unexpected("the installed release's list is in a newer format than this game reads");
    }

    offer.Content = std::move(*content);
    return offer;
}

namespace
{
    /// <summary>A step run once everything the install needs from the host is in hand, before it plans.</summary>
    using MCBeforePlan = std::function<std::expected<void, std::string>()>;

    /// <summary>
    /// The first start's install (<see cref="MCContentUpdater::CheckInstall"/>): the release the copy takes and its list
    /// fetched and checked, then <paramref name="beforePlan"/> (none: nothing), then the plan from the files as they are.
    /// </summary>
    std::expected<MCOffer, std::string> CheckRelease(const MCSetup& setup, MCUpdateDisk& disk,
                                                     MCContentStage::MCSource& source, const MCBeforePlan& beforePlan)
    {
        const auto chosen = ReleaseFor(setup, disk, source);

        if (!chosen)
        {
            return std::unexpected(chosen.error());
        }

        if (const auto writable = Writable(setup.GameFolder); !writable)
        {
            return std::unexpected(writable.error());
        }

        const MCReleaseFile::MCRelease& release = chosen->Release;
        const MCListRef list{release.List, release.ListSize, release.Number};
        auto content = FetchList(setup, disk, source, list, setup.RunningBuild);

        if (!content)
        {
            return std::unexpected(content.error());
        }

        if (*content == nullptr)
        {
            return std::unexpected("the game's files are in a newer format than this version reads: download the "
                                   "newest installer");
        }

        if (beforePlan)
        {
            if (const auto done = beforePlan(); !done)
            {
                return std::unexpected(done.error());
            }
        }

        const auto installed = MCInstalled::Read(setup.GameFolder);

        if (!installed)
        {
            return std::unexpected(installed.error());
        }

        if (const auto planned =
                PlanFor(setup, disk, **content, list, *installed, Wanted(disk, setup.GameFolder, *installed));
            !planned)
        {
            return std::unexpected(planned.error());
        }

        MCOffer offer{.Release = release};
        offer.Content = std::move(*content);
        return offer;
    }

    /// <summary>
    /// The first start's install or a repair, under the staging lock: checked (<see cref="CheckRelease"/>), staged and
    /// armed.
    /// </summary>
    std::expected<void, std::string> InstallRelease(const MCSetup& setup, MCUpdateDisk& disk,
                                                    MCContentStage::MCSource& source,
                                                    const std::function<bool(uint64_t, uint64_t)>& progress,
                                                    const MCBeforePlan& beforePlan)
    {
        const std::unique_ptr<MCUpdateLock> staging = MCUpdateLock::Open(setup.GameFolder, MCUpdateLock::StagingLock);

        if (staging == nullptr || !staging->TryExclusive())
        {
            return std::unexpected("another copy of the game is downloading it");
        }

        const auto offer = CheckRelease(setup, disk, source, beforePlan);

        if (!offer)
        {
            return std::unexpected(offer.error());
        }

        SDL_Log("Updates: installing release %llu (%llu bytes to download)",
                static_cast<unsigned long long>(offer->Release.Number),
                static_cast<unsigned long long>(offer->ContentBytes()));

        if (const auto staged = MCContentUpdater::Stage(setup, disk, source, *offer, progress); !staged)
        {
            return staged;
        }

        return MCContentUpdater::Arm(setup, disk, *offer);
    }
}

std::expected<MCOffer, std::string> MCContentUpdater::CheckInstall(const MCSetup& setup, MCUpdateDisk& disk,
                                                                   MCContentStage::MCSource& source)
{
    return CheckRelease(setup, disk, source, {});
}

std::expected<void, std::string> MCContentUpdater::Stage(const MCSetup& setup, MCUpdateDisk& disk,
                                                         MCContentStage::MCSource& source, const MCOffer& offer,
                                                         const std::function<bool(uint64_t, uint64_t)>& progress)
{
    if (offer.Content == nullptr)
    {
        return {};
    }

    if (const auto placed = MCUpdatePlace::Check(setup.GameFolder, offer.Content->Plan); !placed)
    {
        return placed;
    }

    return MCContentStage::Stage(disk, source, setup.GameFolder, offer.Content->Plan, offer.Content->List, progress);
}

std::expected<void, std::string> MCContentUpdater::Install(const MCSetup& setup, MCUpdateDisk& disk,
                                                           MCContentStage::MCSource& source,
                                                           const std::function<bool(uint64_t, uint64_t)>& progress)
{
    return InstallRelease(setup, disk, source, progress, {});
}

std::expected<void, std::string> MCContentUpdater::Repair(const MCSetup& setup, MCUpdateDisk& disk,
                                                          MCContentStage::MCSource& source,
                                                          const std::function<bool(uint64_t, uint64_t)>& progress)
{
    // Given up only once the release and its list are in hand: offline, the update stopped partway stays as it is,
    // and TRY AGAIN can still finish it.
    return InstallRelease(setup, disk, source, progress,
                          [&]() -> std::expected<void, std::string>
                          {
                              if (const auto given = MCContentSwap::Abandon(disk, setup.GameFolder); !given)
                              {
                                  return given;
                              }

                              SDL_Log("Updates: the update stopped partway is given up; the repair installs the "
                                      "release");
                              return {};
                          });
}

std::expected<void, std::string> MCContentUpdater::Arm(const MCSetup& setup, MCUpdateDisk& disk, const MCOffer& offer)
{
    if (offer.Content == nullptr)
    {
        if (!offer.ProgramBuild)
        {
            return {};
        }

        // The program alone (a list this game can't read): its first start fetches the release's content. The list's
        // own lowest program isn't known here; the build armed, the release's, takes its content.
        return MCInstalled::NoteDue(disk, setup.GameFolder,
                                    {.Release = offer.Release.Number, .LowestProgram = *offer.ProgramBuild});
    }

    return MCContentStage::Arm(disk, setup.GameFolder, offer.Content->Plan);
}

std::expected<void, std::string> MCContentUpdater::Skip(const MCSetup& setup, MCUpdateDisk& disk, const MCOffer& offer)
{
    MCReleaseFile::MCKept kept = ReadKept(setup, disk);
    kept.Skipped = offer.Release.Number;
    return WriteKept(setup, disk, kept);
}

MCContentUpdater::MCSets MCContentUpdater::SetsFound(const std::filesystem::path& gameFolder)
{
    MCSets sets = {std::string(MCContentList::GameSet)};
    const std::filesystem::path hd = gameFolder / MCFileSystem::FromUtf8(HdFolder);
    std::error_code error;

    if (std::filesystem::is_directory(hd, error) && !std::filesystem::is_symlink(hd, error))
    {
        sets.emplace(MCContentList::HdSet);
    }

    return sets;
}

std::optional<MCContentUpdater::MCSets> MCContentUpdater::Chosen(MCUpdateDisk& disk,
                                                                 const std::filesystem::path& gameFolder)
{
    const auto sets = MCUpdateJson::ReadFile(disk, ChosenPath(gameFolder),
                                             [](std::string_view text)
                                             {
                                                 return MCUpdateJson::Read(
                                                     ChosenFile,
                                                     [&]()
                                                     {
                                                         const std::vector<std::string> chosen =
                                                             MCUpdateJson::Strings(MCUpdateJson::Object(text), SetsKey);
                                                         MCSets read(chosen.begin(), chosen.end());
                                                         read.emplace(MCContentList::GameSet);
                                                         return read;
                                                     });
                                             });

    if (!sets)
    {
        SDL_Log("Updates: the sets chosen can't be read (%s): the copy keeps those it has", sets.error().c_str());
        return std::nullopt;
    }

    return *sets;
}

std::expected<void, std::string> MCContentUpdater::Choose(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                                          const MCSets& sets)
{
    MCSets kept = sets;
    kept.emplace(MCContentList::GameSet);
    const std::filesystem::path path = ChosenPath(gameFolder);
    const std::string text = MCUpdateJson::MCJson{{FormatKey, ChosenFormat}, {SetsKey, kept}}.dump();

    if (!disk.MakeFolders(path.parent_path()) || !WriteDurably(disk, path, text))
    {
        return std::unexpected(std::format("{} can't be written", MCFileSystem::ToUtf8(path)));
    }

    return {};
}

MCContentUpdater::MCSets MCContentUpdater::Wanted(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                                  const MCInstalled::MCRecord& installed)
{
    MCSets sets = Chosen(disk, gameFolder).value_or(installed.Sets.empty() ? SetsFound(gameFolder) : installed.Sets);
    sets.emplace(MCContentList::GameSet);
    return sets;
}

bool MCContentUpdater::AwaitsInstall(MCUpdateDisk& disk, const std::filesystem::path& gameFolder)
{
    if (!disk.Stat(ChosenPath(gameFolder)).Exists)
    {
        return false;
    }

    const auto installed = MCInstalled::Read(gameFolder);

    if (!installed)
    {
        SDL_Log("Updates: %s", installed.error().c_str());
        return false;
    }

    return installed->Release == 0;
}

std::expected<void, std::string> MCContentUpdater::Writable(const std::filesystem::path& gameFolder)
{
    const std::filesystem::path update = gameFolder / MCFileSystem::FromUtf8(MCContentSwap::UpdateFolder);
    std::error_code error;
    std::filesystem::create_directories(update, error);

    for (const std::filesystem::path& folder : {gameFolder, update})
    {
        const std::filesystem::path probe = folder / MCFileSystem::FromUtf8(WriteProbe);
        bool written = false;

        {
            std::ofstream out(probe, std::ios::binary | std::ios::trunc);
            written = static_cast<bool>(out);
        }

        std::filesystem::remove(probe, error);

        if (!written)
        {
            return std::unexpected(std::format("this account can't write to {}", MCFileSystem::ToUtf8(folder)));
        }
    }

    return {};
}

#include "stdafx.h"
#include "update/MCContentUpdater.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentFolder.h"
#include "update/MCContentSwap.h"
#include "update/MCInstalled.h"
#include "update/MCUpdateDisk.h"

#include <random>

namespace
{
    using namespace MCContentUpdater;

    /// <summary>A file made and deleted in a folder to see whether this account can write there.</summary>
    constexpr std::string_view WriteProbe = ".redux-write-test";

    /// <summary>The HD pack's folder in the game folder: its being there means the copy has HD.</summary>
    constexpr std::string_view HdFolder = "HD";

    std::filesystem::path KeptPath(const MCSetup& setup)
    {
        return setup.UserFolder / MCFileSystem::FromUtf8(MCReleaseFile::KeptFile);
    }

    /// <summary><c>update.json</c> as kept: none yet is new, and one that can't be read starts again (logged).</summary>
    MCReleaseFile::MCKept ReadKept(const MCSetup& setup, MCUpdateDisk& disk)
    {
        const std::filesystem::path path = KeptPath(setup);

        if (!disk.Stat(path).Exists)
        {
            return {};
        }

        const auto text = disk.ReadText(path);
        const auto kept = text ? MCReleaseFile::ReadKept(*text)
                               : std::expected<MCReleaseFile::MCKept, std::string>(std::unexpected(text.error()));

        if (!kept)
        {
            SDL_Log("Updates: %s can't be read (%s): it starts again", MCFileSystem::ToUtf8(path).c_str(),
                    kept.error().c_str());
            return {};
        }

        return *kept;
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

    /// <summary>
    /// <paramref name="release"/>'s content planned for the copy: its list fetched and held to the hash and size
    /// <c>release.json</c> gives, then the plan against what <paramref name="installed"/> records.
    /// </summary>
    /// <returns>The content; none for a list in a format this game can't read; or why there's none.</returns>
    std::expected<std::shared_ptr<const MCContent>, std::string> PlanContent(const MCSetup& setup,
                                                                             MCContentStage::MCSource& source,
                                                                             const MCReleaseFile::MCRelease& release,
                                                                             const MCInstalled::MCRecord& installed,
                                                                             uint64_t build)
    {
        const std::string name = std::format("{}/{}.json", ListsFolder, MCSha256::Hex(release.List));
        const auto text = source.Read(name, MostListBytes);

        if (!text)
        {
            return std::unexpected(text.error());
        }

        if (text->size() != release.ListSize || MCSha256::Of(*text) != release.List)
        {
            return std::unexpected(std::format("{} isn't the list release.json names", name));
        }

        auto list = MCContentList::Read(*text);

        if (!list)
        {
            return std::unexpected(list.error());
        }

        if (list->Format > MCContentList::KnownFormat)
        {
            SDL_Log("Updates: release %llu's list is format %llu, newer than this game reads: the program comes alone",
                    static_cast<unsigned long long>(release.Number), static_cast<unsigned long long>(list->Format));
            return nullptr;
        }

        if (list->LowestProgram > build)
        {
            return std::unexpected(std::format("release {}'s content needs build {}, and the release's program is {}",
                                               release.Number, list->LowestProgram, build));
        }

        // The plan points into the list, so both stay together where they're made.
        auto content = std::make_shared<MCContent>();
        content->List = std::move(*list);
        const std::set<std::string, std::less<>> sets =
            installed.Sets.empty() ? SetsFound(setup.GameFolder) : installed.Sets;
        MCContentFolder folder(setup.GameFolder);
        auto plan = MCContentPlan::Make(folder, installed, content->List, release.List, sets);

        if (!plan)
        {
            return std::unexpected(plan.error());
        }

        content->Plan = std::move(*plan);
        return content;
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

    const MCReleaseFile::MCRelease& release = MCReleaseFile::For(trusted->Releases, *kept.RolloutNumber);

    if (release.Number == kept.Skipped)
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
        auto content = PlanContent(setup, source, release, *installed, offer.ProgramBuild.value_or(setup.RunningBuild));

        if (!content)
        {
            return std::unexpected(content.error());
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

std::expected<void, std::string> MCContentUpdater::Stage(const MCSetup& setup, MCUpdateDisk& disk,
                                                         MCContentStage::MCSource& source, const MCOffer& offer,
                                                         const std::function<bool(uint64_t, uint64_t)>& progress)
{
    if (offer.Content == nullptr)
    {
        return {};
    }

    return MCContentStage::Stage(disk, source, setup.GameFolder, offer.Content->Plan, offer.Content->List, progress);
}

std::expected<void, std::string> MCContentUpdater::Arm(const MCSetup& setup, MCUpdateDisk& disk, const MCOffer& offer)
{
    if (offer.Content == nullptr)
    {
        return {};
    }

    return MCContentStage::Arm(disk, setup.GameFolder, offer.Content->Plan);
}

std::expected<void, std::string> MCContentUpdater::Skip(const MCSetup& setup, MCUpdateDisk& disk, const MCOffer& offer)
{
    MCReleaseFile::MCKept kept = ReadKept(setup, disk);
    kept.Skipped = offer.Release.Number;
    return WriteKept(setup, disk, kept);
}

std::set<std::string, std::less<>> MCContentUpdater::SetsFound(const std::filesystem::path& gameFolder)
{
    std::set<std::string, std::less<>> sets = {std::string(MCContentList::GameSet)};
    const std::filesystem::path hd = gameFolder / MCFileSystem::FromUtf8(HdFolder);
    std::error_code error;

    if (std::filesystem::is_directory(hd, error) && !std::filesystem::is_symlink(hd, error))
    {
        sets.emplace(MCContentList::HdSet);
    }

    return sets;
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

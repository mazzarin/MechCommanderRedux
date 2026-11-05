#include "stdafx.h"
#include "update/MCUpdatePlace.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentList.h"
#include "update/MCContentStage.h"
#include "update/MCContentSwap.h"
#include "update/MCSyncedFolder.h"

namespace
{
    /// <summary>The bytes that start a UTF-8 sequence of four (a code point past the first plane).</summary>
    constexpr uint8_t FourByteLead = 0xF0;
    constexpr uint8_t ContinuationMask = 0xC0;
    constexpr uint8_t Continuation = 0x80;

    /// <summary><paramref name="path"/>'s length as Windows counts it: UTF-16 units.</summary>
    size_t Utf16Length(const std::filesystem::path& path)
    {
        size_t units = 0;

        for (const char c : MCFileSystem::ToUtf8(path))
        {
            const auto byte = static_cast<uint8_t>(c);

            // Each code point is one unit, and one past the first plane two (a four-byte sequence in UTF-8).
            if ((byte & ContinuationMask) != Continuation)
            {
                units += byte >= FourByteLead ? 2 : 1;
            }
        }

        return units;
    }
}

std::expected<void, std::string> MCUpdatePlace::FitPaths(const std::filesystem::path& gameFolder,
                                                         const MCContentPlan::MCPlan& plan, size_t fileLimit,
                                                         size_t folderLimit)
{
    std::error_code error;
    const std::filesystem::path game = std::filesystem::absolute(gameFolder, error);
    const auto in = [&](std::string_view folder, std::string_view path)
    { return game / MCFileSystem::FromUtf8(folder) / MCFileSystem::FromUtf8(path); };
    std::vector<std::filesystem::path> paths;

    for (const MCContentPlan::MCStep& step : plan.Steps)
    {
        const bool fetched =
            step.Action == MCContentPlan::MCAction::Fetch || step.Action == MCContentPlan::MCAction::Patch;

        if (fetched)
        {
            paths.push_back(game / MCFileSystem::FromUtf8(step.OnDisk));
            paths.push_back(in(MCContentSwap::StagedFolder, step.OnDisk));
        }

        // A file replaced or removed is set aside, then kept in the backup.
        if ((fetched && step.Found) || step.Action == MCContentPlan::MCAction::Remove)
        {
            paths.push_back(in(MCContentSwap::ReplacedFolder, step.OnDisk));
            paths.push_back(in(MCContentList::BackupFolder, step.OnDisk));
        }

        if (step.Patch != nullptr)
        {
            paths.push_back(in(MCContentStage::DownloadFolder, step.Patch->Name));
        }
    }

    for (const MCContentList::MCZip* zip : plan.Zips)
    {
        paths.push_back(in(MCContentStage::DownloadFolder, zip->Name));
    }

    const std::filesystem::path* longest = nullptr;
    size_t longestLength = 0;

    for (const std::filesystem::path& path : paths)
    {
        const size_t length = Utf16Length(path);

        if ((length > fileLimit || Utf16Length(path.parent_path()) > folderLimit) && length > longestLength)
        {
            longest = &path;
            longestLength = length;
        }
    }

    if (longest != nullptr)
    {
        return std::unexpected(std::format("{}'s path is too long for Windows ({} characters, at most {}): install "
                                           "the game in a folder with a shorter path",
                                           MCFileSystem::ToUtf8(*longest), longestLength, fileLimit));
    }

    return {};
}

std::expected<void, std::string> MCUpdatePlace::Check(const std::filesystem::path& gameFolder,
                                                      const MCContentPlan::MCPlan& plan)
{
#ifdef _WIN32
    if (const std::optional<std::string> sync = MCSyncedBy(gameFolder))
    {
        return std::unexpected(std::format("{} syncs {}: install the game in a folder outside it", *sync,
                                           MCFileSystem::ToUtf8(gameFolder)));
    }

    return FitPaths(gameFolder, plan, WindowsFileLimit, WindowsFolderLimit);
#else
    static_cast<void>(gameFolder);
    static_cast<void>(plan);
    return {};
#endif
}

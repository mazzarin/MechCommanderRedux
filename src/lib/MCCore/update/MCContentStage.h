#pragma once

#include "update/MCContentPlan.h"
#include "update/MCContentSwap.h"

class MCUpdateDisk;

/// <summary>
/// Port-only: staging an update (SPEC-install-and-updates.md's Applying an update: Plan and Patched files), on a thread
/// of its own while the game plays. Each patched file is written anew in <c>Redux update/staged/</c> by applying its
/// patch to the copy on disk (lib/MCFilePatch.h, HDiffPatch); one whose patch fails, or whose result isn't the list's
/// hash, is fetched whole instead. Only the zips holding a fetched file are downloaded (into <c>Redux
/// update/download/</c>, resuming where a cut-off download stopped), each checked against the list, and only those
/// entries unpacked. Every staged file is hash-checked and flushed; then the journal-to-be is written
/// (update/MCContentSwap.h), and the swap happens at the next start. Stopped midway, the next attempt keeps every
/// staged file that's already right.
/// </summary>
namespace MCContentStage
{
    /// <summary>Where downloads go while staging, in the game folder.</summary>
    constexpr std::string_view DownloadFolder = "Redux update/download";

    /// <summary>Fetches the release's files from where they're hosted (the downloads Worker; a folder in the tests).</summary>
    class MCSource
    {
    public:
        virtual ~MCSource() = default;

        /// <summary>
        /// Appends to <paramref name="to"/> the bytes of the hosted file <paramref name="name"/> from <paramref
        /// name="to"/>'s size on, up to <paramref name="size"/> bytes in all, so a cut-off download resumes.
        /// <paramref name="received"/> gets each count of bytes that arrives, and returns false to stop.
        /// </summary>
        /// <returns>Nothing, or what failed.</returns>
        virtual std::expected<void, std::string> Fetch(std::string_view name, const std::filesystem::path& to,
                                                       uint64_t size,
                                                       const std::function<bool(uint64_t)>& received) = 0;
    };

    /// <summary>The moves the swap makes for <paramref name="plan"/>.</summary>
    std::vector<MCContentSwap::MCMove> Moves(const MCContentPlan::MCPlan& plan);

    /// <summary>
    /// Stages <paramref name="plan"/> (made against <paramref name="list"/>) in <paramref name="gameFolder"/> from
    /// <paramref name="source"/>, then marks it ready. <paramref name="progress"/> gets the bytes downloaded so far and
    /// expected, and returns false to stop.
    /// </summary>
    /// <returns>Nothing, or what failed (not enough space, a download, a file that doesn't check out).</returns>
    std::expected<void, std::string> Stage(MCUpdateDisk& disk, MCSource& source,
                                           const std::filesystem::path& gameFolder, const MCContentPlan::MCPlan& plan,
                                           const MCContentList::MCList& list,
                                           const std::function<bool(uint64_t, uint64_t)>& progress);
}

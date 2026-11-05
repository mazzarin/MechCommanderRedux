#pragma once

#include "lib/MCSha256.h"
#include "update/MCContentPlan.h"
#include "update/MCContentSwap.h"

class MCUpdateDisk;

/// <summary>
/// Port-only: staging an update (SPEC-install-and-updates.md's Applying an update: Plan and Patched files), on a thread
/// of its own while the game plays. Each patched file is written anew in <c>Redux update/staged/</c> by applying its
/// patch to the copy on disk (lib/MCFilePatch.h, HDiffPatch); one whose patch fails, or whose result isn't the list's
/// hash, is fetched whole instead. Only the zips holding a fetched file are downloaded (into <c>Redux
/// update/download/</c>, resuming where a cut-off download stopped), each checked against the list as it arrives, and
/// only those entries unpacked, each checked as it's written. Every staged file is flushed. Arming it, once the program's half of the update is
/// in too, writes the journal-to-be (update/MCContentSwap.h), and the swap happens at the next start. Stopped midway,
/// or staged and never armed, the next attempt keeps every staged file that's already right.
/// </summary>
namespace MCContentStage
{
    /// <summary>Where downloads go while staging, in the game folder.</summary>
    constexpr std::string_view DownloadFolder = MCContentList::InUpdateFolder<"download">;

    /// <summary>Fetches the release's files from where they're hosted (the downloads Worker; a folder in the tests).</summary>
    class MCSource
    {
    public:
        virtual ~MCSource() = default;

        /// <summary>
        /// Appends to <paramref name="to"/> the bytes of the hosted file <paramref name="name"/> from <paramref
        /// name="to"/>'s size on, up to <paramref name="size"/> bytes in all, so a cut-off download resumes.
        /// <paramref name="received"/> gets each piece once it's written, in order (so it can be hashed as it comes),
        /// and returns false to stop.
        /// </summary>
        /// <returns>Nothing, or what failed.</returns>
        virtual std::expected<void, std::string> Fetch(std::string_view name, const std::filesystem::path& to,
                                                       uint64_t size,
                                                       const std::function<bool(std::span<const char>)>& received) = 0;

        /// <summary>
        /// The hosted file <paramref name="name"/> whole (the key list, <c>release.json</c>, a list), refused past
        /// <paramref name="most"/> bytes.
        /// </summary>
        /// <returns>Its bytes, or what failed.</returns>
        virtual std::expected<std::string, std::string> Read(std::string_view name, size_t most) = 0;

        /// <summary>
        /// From any thread: ends the fetch or read under way at once, and fails every later one, with "stopped" (a
        /// work's stop calls it), and those of the sources made from it (<see cref="Another"/>). Here it does nothing
        /// (the fetch stops at its next bytes, as received says).
        /// </summary>
        virtual void Cancel() {}

        /// <summary>
        /// Another source to the same host with a transfer of its own, so files are fetched several at once (staging's
        /// patches, from other threads); this one's Cancel reaches it too. Here none: files are fetched one at a time.
        /// </summary>
        virtual std::unique_ptr<MCSource> Another() { return nullptr; }
    };

    /// <summary>Why a checked download (<see cref="FetchChecked"/>) failed.</summary>
    struct MCFetchError
    {
        enum class MCKind : uint8_t
        {
            /// <summary>It didn't download (no connection, the host's refusal, a write that failed).</summary>
            Download,
            /// <summary>It came, but isn't the size and hash it was to be: it's deleted.</summary>
            NotAsNamed,
            /// <summary>Stopped: what's in stays for the next try.</summary>
            Stopped,
        };

        MCKind Kind = MCKind::Download;
        std::string Reason;
    };

    /// <summary>
    /// Downloads the hosted file <paramref name="name"/> into <paramref name="to"/> from <paramref name="source"/>,
    /// resuming a cut-off download (one longer than <paramref name="size"/> starts again), and checks it's <paramref
    /// name="size"/> bytes of SHA-256 <paramref name="sha256"/>: the bytes are hashed as they arrive, those already
    /// here once first. One that isn't is deleted. <paramref name="received"/> gets the bytes already here, then each
    /// count that arrives (0: asked whether to go on), and returns false to stop.
    /// </summary>
    /// <returns>Nothing, or what failed.</returns>
    std::expected<void, MCFetchError> FetchChecked(MCUpdateDisk& disk, MCSource& source, std::string_view name,
                                                   const std::filesystem::path& to, uint64_t size,
                                                   const MCSha256::MCDigest& sha256,
                                                   const std::function<bool(uint64_t)>& received);

    /// <summary>The moves the swap makes for <paramref name="plan"/>.</summary>
    std::vector<MCContentSwap::MCMove> Moves(const MCContentPlan::MCPlan& plan);

    /// <summary>
    /// Stages <paramref name="plan"/> (made against <paramref name="list"/>) in <paramref name="gameFolder"/> from
    /// <paramref name="source"/>, each staged file and its folder flushed (not yet armed: <see cref="Arm"/>).
    /// <paramref name="progress"/> gets the bytes downloaded so far and expected, on this thread, and returns false to
    /// stop. The patches are fetched ahead from the source's others (MCSource::Another), on threads of their own; the
    /// disk is used from those threads too.
    /// </summary>
    /// <returns>Nothing, or what failed (not enough space, a download, a file that doesn't check out).</returns>
    std::expected<void, std::string> Stage(MCUpdateDisk& disk, MCSource& source,
                                           const std::filesystem::path& gameFolder, const MCContentPlan::MCPlan& plan,
                                           const MCContentList::MCList& list,
                                           const std::function<bool(uint64_t, uint64_t)>& progress);

    /// <summary>Arms the staged <paramref name="plan"/>: the next start swaps it in.</summary>
    /// <returns>Nothing, or what failed.</returns>
    std::expected<void, std::string> Arm(MCUpdateDisk& disk, const std::filesystem::path& gameFolder,
                                         const MCContentPlan::MCPlan& plan);
}

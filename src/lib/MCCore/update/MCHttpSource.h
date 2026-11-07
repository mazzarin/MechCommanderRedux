#pragma once

#include "update/MCContentStage.h"
#include "update/MCHttpTransfer.h"

/// <summary>
/// Port-only: the release's host over HTTP (SPEC-install-and-updates.md's Hosting: the downloads Worker), its files
/// fetched by name under one address and a cut-off download resumed with a byte range, and the mirrors release.json
/// names (its Mirrors): each file under lists/, zips/ and patches/ starts from a host picked at random, ours among
/// them, and one that fails there goes on from the next, by range. The rules are here; the transfer is the platform's
/// (<c>MCGet</c>: NSURLSession on the Mac, WinHTTP on Windows), so both follow them alike (update/MCHttpTransfer.h).
/// Only https, but for http to this machine (the tests' server): what arrives is checked against the signed files
/// whatever carries it, and https keeps what a player fetches private. A failure that may pass (a lost connection, a
/// timeout, the host's 5xx, 408 or 429) goes to the next host at once, and once every host has failed it is tried
/// again, waiting longer each time and resuming where the bytes stopped, through an outage of a couple of minutes
/// (<see cref="MCRetries"/>); any other (a missing file, one not as the list has it) leaves that host out for the file,
/// and fails it once every host is out.
/// </summary>
namespace MCHttpSource
{
    /// <summary>The first wait to try again, the longest, and the outage the waits ride out (their sum).</summary>
    constexpr std::chrono::milliseconds FirstRetryWait = std::chrono::seconds(1);
    constexpr std::chrono::milliseconds LongestRetryWait = std::chrono::seconds(30);
    constexpr std::chrono::milliseconds OutageRiddenOut = std::chrono::minutes(2);

    /// <summary>
    /// How a source tries again after failures that may pass, each a round of the hosts that all failed: waits doubling
    /// from <see cref="First"/> up to <see cref="Longest"/>, until they add up to <see cref="RideOut"/>. A try that
    /// brought bytes starts them over, so a long download survives many cuts.
    /// </summary>
    struct MCRetries
    {
        std::chrono::milliseconds First = FirstRetryWait;
        std::chrono::milliseconds Longest = LongestRetryWait;
        std::chrono::milliseconds RideOut = OutageRiddenOut;
    };

    /// <summary>The waits before each try again after a run of failures, in order: then the source gives up.</summary>
    std::vector<std::chrono::milliseconds> RetryWaits(const MCRetries& retries = {});

    /// <summary>The first byte a part's Content-Range names ("bytes 100-199/1000": 100), or none.</summary>
    std::optional<uint64_t> RangeFirst(std::string_view contentRange);

    /// <summary>
    /// The scheme and host of <paramref name="url"/> ("https", "example.com"), when it has them, and nothing before its
    /// path that could name another host: a user part ('@'), a backslash, a space or a control.
    /// </summary>
    std::optional<std::pair<std::string, std::string>> SchemeAndHost(std::string_view url);

    /// <summary>Makes a platform's transfer (MCMakeHttpGet).</summary>
    using MCGetMaker = std::function<std::unique_ptr<MCGet>()>;

    /// <summary>
    /// Picks the host a file starts from: given how many it may come from (ours first, then the mirrors in
    /// release.json's order, less those passed over for it), the place of one (wrapped round when past them).
    /// </summary>
    using MCPick = std::function<size_t(size_t hosts)>;

    /// <summary>The folders a mirror serves, at the paths ours does: a file under one may come from any host.</summary>
    constexpr std::array<std::string_view, 3> MirroredFolders = {"lists/", "zips/", "patches/"};

    /// <summary>Whether the hosted file <paramref name="name"/> may come from a mirror (it's under MirroredFolders).</summary>
    bool Mirrored(std::string_view name);

    /// <summary>
    /// <paramref name="address"/> as the folder of a source's files, without a trailing '/'; or why it can't be one:
    /// not a folder's address (a query, a fragment, a user part), or not https, nor http to this machine.
    /// </summary>
    std::expected<std::string, std::string> FolderAddress(std::string_view address);

    /// <summary>
    /// The source whose files are under <paramref name="address"/> ("https://host/folder"), fetched through
    /// transfers <paramref name="makeGet"/> makes: one for it, and one for each source made from it (Another), each
    /// trying again as <paramref name="retries"/> says. Once given mirrors (UseMirrors: those FolderAddress refuses
    /// left out, logged), <paramref name="pick"/> picks the host each file starts from (none: at random).
    /// </summary>
    /// <returns>The source, or why the address can't be one (not https, nor http to this machine).</returns>
    std::expected<std::unique_ptr<MCContentStage::MCSource>, std::string> Make(std::string_view address,
                                                                               MCGetMaker makeGet,
                                                                               const MCRetries& retries = {},
                                                                               MCPick pick = {});
}

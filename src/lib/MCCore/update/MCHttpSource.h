#pragma once

#include "update/MCContentStage.h"
#include "update/MCHttpTransfer.h"

/// <summary>
/// Port-only: the release's host over HTTP (SPEC-install-and-updates.md's Hosting: the downloads Worker), its files
/// fetched by name under one address and a cut-off download resumed with a byte range. The rules are here; the
/// transfer is the platform's (<c>MCGet</c>: NSURLSession on the Mac, WinHTTP on Windows), so both follow them
/// alike (update/MCHttpTransfer.h). Only https, but for http to this machine (the tests' server): what arrives is checked against the signed
/// files whatever carries it, and https keeps what a player fetches private. A failure that may pass (a lost
/// connection, a timeout, the host's 5xx, 408 or 429) is tried again, waiting longer each time and resuming where the
/// bytes stopped, through an outage of a couple of minutes (<see cref="MCRetries"/>); any other (a missing file, one
/// not as the list has it) isn't.
/// </summary>
namespace MCHttpSource
{
    /// <summary>The first wait to try again, the longest, and the outage the waits ride out (their sum).</summary>
    constexpr std::chrono::milliseconds FirstRetryWait = std::chrono::seconds(1);
    constexpr std::chrono::milliseconds LongestRetryWait = std::chrono::seconds(30);
    constexpr std::chrono::milliseconds OutageRiddenOut = std::chrono::minutes(2);

    /// <summary>
    /// How a source tries again after failures that may pass: waits doubling from <see cref="First"/> up to
    /// <see cref="Longest"/>, until they add up to <see cref="RideOut"/>. A try that brought bytes starts them over, so
    /// a long download survives many cuts.
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
    /// The source whose files are under <paramref name="address"/> ("https://host/folder"), fetched through
    /// transfers <paramref name="makeGet"/> makes: one for it, and one for each source made from it (Another), each
    /// trying again as <paramref name="retries"/> says.
    /// </summary>
    /// <returns>The source, or why the address can't be one (not https, nor http to this machine).</returns>
    std::expected<std::unique_ptr<MCContentStage::MCSource>, std::string> Make(std::string_view address,
                                                                               MCGetMaker makeGet,
                                                                               const MCRetries& retries = {});
}

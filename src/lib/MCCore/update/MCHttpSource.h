#pragma once

#include "update/MCContentStage.h"
#include "update/MCHttpTransfer.h"

/// <summary>
/// Port-only: the release's host over HTTP (SPEC-install-and-updates.md's Hosting: the downloads Worker), its files
/// fetched by name under one address and a cut-off download resumed with a byte range. The rules are here; the
/// transfer is the platform's (<c>MCGet</c>: NSURLSession on the Mac, WinHTTP on Windows), so both follow them
/// alike (update/MCHttpTransfer.h). Only https, but for http to this machine (the tests' server): what arrives is checked against the signed
/// files whatever carries it, and https keeps what a player fetches private.
/// </summary>
namespace MCHttpSource
{
    /// <summary>The first byte a part's Content-Range names ("bytes 100-199/1000": 100), or none.</summary>
    std::optional<uint64_t> RangeFirst(std::string_view contentRange);

    /// <summary>The scheme and host of <paramref name="url"/> ("https", "example.com"), when it has them.</summary>
    std::optional<std::pair<std::string, std::string>> SchemeAndHost(std::string_view url);

    /// <summary>
    /// The source whose files are under <paramref name="address"/> ("https://host/folder"), fetched through
    /// <paramref name="get"/>.
    /// </summary>
    /// <returns>The source, or why the address can't be one (not https, nor http to this machine).</returns>
    std::expected<std::unique_ptr<MCContentStage::MCSource>, std::string> Make(std::string_view address,
                                                                               std::unique_ptr<MCGet> get);
}

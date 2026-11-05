#pragma once

/// <summary>
/// Port-only: an update's patch for a content file (an HD bundle, a movie...), which turns one exact old file into the
/// new one so a release that changes part of a big file needn't ship it whole. A patch is HDiffPatch's window diff
/// (<c>hdiffz -WD -c-no -C-xxh128</c>, src/lib/HDiffPatch-5.1.3), carrying xxh128 checksums of the old file, of the
/// new one and of itself: a patch applied to any other old file, or damaged, fails. The updater checks the result's
/// SHA-256 against the release's list too.
/// </summary>
namespace MCFilePatch
{
    /// <summary>
    /// Writes to <paramref name="out"/> the file <paramref name="old"/> becomes with <paramref name="patch"/> applied,
    /// streaming, whatever the files' sizes: in the patch's window and step (2 MB and 256 KB as hdiffz makes them; at
    /// most 256 MB each) and 1 MB more. A patch without checksums, or compressed, is refused. <paramref name="goOn"/>,
    /// when given, is asked before each piece read (the old file's checksum reads it whole first) or written, and
    /// returns false to stop. With <paramref name="newSize"/>, a patch making a file of another size is refused before
    /// anything is written. <paramref name="out"/> is made anew: whatever is there goes first (a link itself, never what
    /// it points to), and it's never written through a link.
    /// </summary>
    /// <returns>
    /// Nothing, or what failed (<see cref="StoppedError"/>: stopped); nothing is then left at <paramref name="out"/>.
    /// </returns>
    std::expected<void, std::string> Apply(const std::filesystem::path& old, const std::filesystem::path& patch,
                                           const std::filesystem::path& out, const std::function<bool()>& goOn = {},
                                           std::optional<uint64_t> newSize = std::nullopt);

    /// <summary>What a patch ends with when its <c>goOn</c> says to stop.</summary>
    constexpr std::string_view StoppedError = "stopped";
}

#pragma once

/// <summary>
/// Port-only: the HD art pack's bundles (its FastFiles, <c>X.hda</c>: sprite/MCHdArt.h) as updates bring them. A release
/// that changes some of a bundle's entries ships a patch for it rather than the whole bundle again: a FastFile of the
/// entries changed or added, as they are stored, and a zero-size entry for each one deleted. A copy applies its patches
/// while it stages the update, writing the whole bundle anew for the update to swap in, so the game always reads one
/// file per bundle.
/// </summary>
/// <remarks>
/// Every bundle is written in one layout, the same on every system (<see cref="Apply"/>), so the bundle a copy makes is
/// byte for byte the release's: the release script writes its bundles and patches with these functions too, and the
/// content list's hash checks the result. A zero-size entry marks its name deleted wherever it lies (no HD frame is
/// empty).
/// </remarks>
namespace MCBundlePatch
{
    /// <summary>
    /// Writes to <paramref name="out"/> the bundle that <paramref name="base"/> becomes with <paramref name="patches"/>
    /// applied in turn: each name's entry from the last patch holding it, a zero-size one deleting it, else from the
    /// base. The layout: one entry of each name, ignoring case (a file's first, as the game reads it), the directory in
    /// name order (MCFileSystem::NameLess), the entries' bytes as their sources store them, in the same order, each
    /// right after the one before; an entry whose bytes and sizes are an earlier one's shares that one's. With no
    /// patches, it is the base in that layout.
    /// </summary>
    /// <returns>Nothing, or what failed (nothing is then left at <paramref name="out"/>).</returns>
    std::expected<void, std::string> Apply(const std::filesystem::path& base,
                                           std::span<const std::filesystem::path> patches,
                                           const std::filesystem::path& out);

    /// <summary>
    /// Writes to <paramref name="out"/>, in Apply's layout, the patch that turns <paramref name="from"/> into
    /// <paramref name="to"/>: each of <paramref name="to"/>'s entries that <paramref name="from"/> lacks, or holds with
    /// other bytes, sizes or spelling, and a zero-size entry for each name only <paramref name="from"/> has. Applying it
    /// to <paramref name="from"/> writes the bundle Apply writes for <paramref name="to"/>.
    /// </summary>
    /// <returns>Nothing, or what failed (nothing is then left at <paramref name="out"/>).</returns>
    std::expected<void, std::string> Make(const std::filesystem::path& from, const std::filesystem::path& to,
                                          const std::filesystem::path& out);
}

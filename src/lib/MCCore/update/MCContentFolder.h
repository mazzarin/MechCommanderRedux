#pragma once

/// <summary>
/// Port-only: the game folder as updates see it. A list names a file as the release spells it ("data/missions/..."),
/// but a player's copy may spell its folders otherwise (a CD install's "DATA"), and the Mac's disk ignores case as
/// Windows' does. So a listed path is found part by part, case aside (ASCII), each folder listed once; a new file goes
/// in the folders as they're spelled on disk. Updates never follow a link: one along a path is reported, never used.
/// A folder on the way that can't be listed is an error, never taken as empty: a file in it isn't missing.
/// </summary>
class MCContentFolder
{
public:
    /// <summary>What's at a listed path.</summary>
    enum class MCKind : uint8_t
    {
        /// <summary>Nothing (the file, or a folder on its way, isn't there).</summary>
        Missing,
        /// <summary>A file, at <see cref="MCFound::Path"/>.</summary>
        File,
        /// <summary>A folder where the file should be, or a file where a folder should be.</summary>
        Blocked,
        /// <summary>A link (or anything but a file or folder) on the way.</summary>
        Link,
    };

    struct MCFound
    {
        MCKind Kind = MCKind::Missing;
        /// <summary>A file: its path as spelled on disk, relative to the folder, '/' apart.</summary>
        std::string Path;
    };

    explicit MCContentFolder(std::filesystem::path root);

    /// <summary>What's at <paramref name="path"/> (a listed path), case aside; or why a folder on its way can't be listed.</summary>
    std::expected<MCFound, std::string> Find(std::string_view path);

    /// <summary>
    /// Where a new file listed as <paramref name="path"/> goes: the folders on its way spelled as they are on disk,
    /// the rest as listed; or why a folder on its way can't be listed.
    /// </summary>
    std::expected<std::string, std::string> Place(std::string_view path);

    /// <summary>The disk path of <paramref name="relative"/> (as spelled on disk, or placed).</summary>
    std::filesystem::path Full(std::string_view relative) const;

    const std::filesystem::path& Root() const { return _Root; }

    /// <summary>Forgets what was listed (after files were added or removed).</summary>
    void Forget() { _Listings.clear(); }

private:
    struct MCEntry
    {
        std::string Name;
        MCKind Kind = MCKind::Missing;
        bool Folder = false;
    };

    /// <summary>A folder's entries by their folded names.</summary>
    using MCListing = std::map<std::string, MCEntry, std::less<>>;

    /// <summary>
    /// The entries of <paramref name="folder"/> (as spelled on disk; empty is the root), none when it isn't there; or
    /// why it can't be listed. Only a whole listing is kept.
    /// </summary>
    std::expected<const MCListing*, std::string> Listing(const std::string& folder);

    std::filesystem::path _Root;
    std::map<std::string, MCListing, std::less<>> _Listings;
};

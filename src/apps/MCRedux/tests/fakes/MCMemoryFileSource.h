#pragma once

#include "lib/MCOptionalRef.h"
#include "platform/MCServices.h"

/// <summary>
/// Game files held in memory, so a test can hand the game exactly the files it needs. Paths are looked up as the
/// FastFiles look up theirs (case and <c>\</c> versus <c>/</c> ignored, a leading <c>.\</c> dropped). As on disk, user
/// files (<see cref="AddUserFile"/>) overlay the install's (<see cref="AddFile"/>).
/// </summary>
/// <remarks>
/// <c>File::open</c> reads these through <see cref="FindImage"/>. What the game writes goes to a scratch folder of
/// its own (deleted with the source), which then overlays both; code that opens a resolved path itself (the movies,
/// streamed sounds) sees only that folder.
/// </remarks>
class MCMemoryFileSource final : public MCFileSource
{
public:
    MCMemoryFileSource();
    ~MCMemoryFileSource() override;
    MCMemoryFileSource(const MCMemoryFileSource&) = delete;
    MCMemoryFileSource& operator=(const MCMemoryFileSource&) = delete;

    /// <summary>Adds (or replaces) an install file.</summary>
    void AddFile(std::string_view gamePath, std::vector<uint8_t> bytes);

    /// <summary>Adds (or replaces) an install file holding <paramref name="text"/>.</summary>
    void AddFile(std::string_view gamePath, std::string_view text);

    /// <summary>Adds (or replaces) a user file, which hides an install file of the same name.</summary>
    void AddUserFile(std::string_view gamePath, std::vector<uint8_t> bytes);

    /// <summary>The number of files in memory (both layers).</summary>
    size_t Count() const { return _Install.size() + _User.size(); }

    /// <summary>The scratch folder the game's writes go to.</summary>
    const std::filesystem::path& ScratchFolder() const { return _Scratch; }

    /// <summary>The key a game path is filed under: upper case, <c>\</c> separators, no leading <c>.\</c>.</summary>
    static std::string Key(std::string_view gamePath);

    void SetGameRoot(const std::filesystem::path& root) override;
    const std::filesystem::path& GameRoot() const override;
    void SetUserRoot(const std::filesystem::path& root) override;
    const std::filesystem::path& UserRoot() const override;
    /// <summary>Kept as set, never looked in: the files are in memory (a test of the launcher's folder uses the disk).</summary>
    void SetLauncherRoot(const std::filesystem::path& root) override;
    const std::filesystem::path& LauncherRoot() const override;
    std::filesystem::path Resolve(std::string_view gamePath) override;
    std::filesystem::path ResolveWrite(std::string_view gamePath, bool copyExisting) override;
    bool MakeDirectory(std::string_view gamePath) override;
    bool RemoveDirectory(std::string_view gamePath) override;
    std::vector<std::string> FindFiles(std::string_view gamePattern) override;
    std::vector<MCFoundFile> FindFileSources(std::string_view gamePattern) override;
    bool Exists(std::string_view gamePath) override;
    bool RemoveFile(std::string_view gamePath) override;
    bool RemoveSave(std::string_view gamePath) override;
    bool RenameFile(std::string_view fromGamePath, std::string_view toGamePath) override;
    bool CopyFile(std::string_view fromGamePath, std::string_view toGamePath) override;
    std::optional<std::span<const uint8_t>> FindImage(std::string_view gamePath) override;

private:
    /// <summary>A file in memory: its name as it was added, and its bytes.</summary>
    struct Entry
    {
        std::string Name;
        std::vector<uint8_t> Bytes;
    };

    /// <summary>A file of either layer by key, user first, if any.</summary>
    MCOptionalRef<const Entry> Find(const std::string& key) const;

    /// <summary>The scratch folder's copy of <paramref name="gamePath"/>.</summary>
    std::filesystem::path ScratchPath(std::string_view gamePath) const;

    /// <summary>The bytes of <paramref name="gamePath"/>: the scratch copy if there is one, else memory's.</summary>
    std::optional<std::vector<uint8_t>> ReadAny(std::string_view gamePath);

    /// <summary>Install files by <see cref="Key"/>.</summary>
    std::map<std::string, Entry> _Install;
    /// <summary>User files by <see cref="Key"/>.</summary>
    std::map<std::string, Entry> _User;
    std::filesystem::path _Scratch;
    std::filesystem::path _GameRoot;
    std::filesystem::path _LauncherRoot;
};

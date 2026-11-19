#pragma once

#include "platform/MCSocket.h"

class MCAudio;
class MCSoundBuffer;

// The port services: everything the game asks of the machine (time, dice, files, sound output, the network) behind
// an interface, each with the real implementation here and a fake in the tests (tests/fakes). Game code reaches them
// through MCGameContext::Current() (main/MCGameContext.h), mostly by way of the old free functions (MCPort::Milliseconds,
// MCPort::Rand, MCFileSystem::Resolve, MCSocket::Send, ...), which forward to the current context.

/// <summary>The clocks: the tick counts the game times itself by and the date and time of day.</summary>
class MCClock
{
public:
    virtual ~MCClock() = default;

    /// <summary>Milliseconds since the port started (<c>timeGetTime</c> / <c>GetTickCount</c>).</summary>
    virtual uint32_t Milliseconds() = 0;

    /// <summary>The high-resolution counter (<c>QueryPerformanceCounter</c>).</summary>
    virtual int64_t PerformanceCounter() = 0;

    /// <summary>Counts per second of <see cref="PerformanceCounter"/> (<c>QueryPerformanceFrequency</c>).</summary>
    virtual int64_t PerformanceFrequency() = 0;

    /// <summary>The current UTC date and time as a Win32 FILETIME (100 ns ticks since 1601-01-01).</summary>
    virtual uint64_t UtcFileTime() = 0;

    /// <summary>The current local date and time as a FILETIME.</summary>
    virtual uint64_t LocalFileTime() = 0;

    /// <summary>Called by the display on every present; a clock that waits for nothing may move on here.</summary>
    virtual void Presented() {}
};

/// <summary>The machine's clocks (SDL's tick counters and the system clock).</summary>
class MCSystemClock final : public MCClock
{
public:
    uint32_t Milliseconds() override;
    int64_t PerformanceCounter() override;
    int64_t PerformanceFrequency() override;
    uint64_t UtcFileTime() override;
    uint64_t LocalFileTime() override;
};

/// <summary>The dice: the original's <c>rand</c> and <c>srand</c>, which every random roll of the game goes through.</summary>
class MCRandom
{
public:
    virtual ~MCRandom() = default;

    /// <summary>The next value, in [0, 0x7fff] (<c>rand</c>).</summary>
    virtual int32_t Next() = 0;

    /// <summary>Restarts the sequence at <paramref name="seed"/> (<c>srand</c>).</summary>
    virtual void Seed(uint32_t seed) = 0;

    /// <summary>The generator's state, which decides every later <see cref="Next"/> (hashed by the state tests).</summary>
    virtual uint32_t State() const = 0;
};

/// <summary>
/// MSVC's CRT <c>rand</c>, the same on every platform: a linear congruential generator per thread, starting at 1.
/// As the CRT's, the state belongs to the thread, so every instance on a thread shares it.
/// </summary>
class MCCrtRandom final : public MCRandom
{
public:
    /// <summary>One step of the CRT's generator: the new state, and the value <c>rand</c> returns for it.</summary>
    static constexpr uint32_t Step(uint32_t state) { return state * 214013u + 2531011u; }

    /// <summary>The value <c>rand</c> returns once its state is <paramref name="state"/>.</summary>
    static constexpr int32_t Value(uint32_t state) { return static_cast<int32_t>((state >> 16) & 0x7fff); }

    int32_t Next() override;
    void Seed(uint32_t seed) override;
    uint32_t State() const override;
};

/// <summary>A file a folder listing found, and which of the folders have it.</summary>
struct MCFoundFile
{
    /// <summary>The name, as the first folder that has it spells it.</summary>
    std::string Name;
    /// <summary>Whether the user folder has it.</summary>
    bool InUserFolder = false;
    /// <summary>Port-only: whether a launcher's folder has it, as a map or mod (MCFileSystem::SetLauncherRoot).</summary>
    bool InLauncherFolder = false;
    /// <summary>Whether the game's folder (the install) has it.</summary>
    bool InGameFolder = false;
};

/// <summary>
/// Where the game's files come from: the paths the original spells (relative DOS paths, any case) mapped to files.
/// The methods are those of <c>MCFileSystem</c> (platform/MCFileSystem.h), which forwards to the current source.
/// </summary>
class MCFileSource
{
public:
    virtual ~MCFileSource() = default;

    /// <summary>Sets the folder of the retail install.</summary>
    virtual void SetGameRoot(const std::filesystem::path& root) = 0;

    /// <summary>The folder of the retail install.</summary>
    virtual const std::filesystem::path& GameRoot() const = 0;

    /// <summary>Sets the writable folder that overlays the install.</summary>
    virtual void SetUserRoot(const std::filesystem::path& root) = 0;

    /// <summary>The writable folder.</summary>
    virtual const std::filesystem::path& UserRoot() const = 0;

    /// <summary>Sets the launcher's folder whose maps and mods play beside the install (empty: none).</summary>
    virtual void SetLauncherRoot(const std::filesystem::path& root) = 0;

    /// <summary>The launcher's folder, or empty.</summary>
    virtual const std::filesystem::path& LauncherRoot() const = 0;

    /// <summary>Maps a game path to the file on disk, for reading (it may not exist).</summary>
    virtual std::filesystem::path Resolve(std::string_view gamePath) = 0;

    /// <summary>Maps a game path to where the game may write it.</summary>
    virtual std::filesystem::path ResolveWrite(std::string_view gamePath, bool copyExisting) = 0;

    /// <summary>Creates a folder.</summary>
    virtual bool MakeDirectory(std::string_view gamePath) = 0;

    /// <summary>Deletes an empty folder.</summary>
    virtual bool RemoveDirectory(std::string_view gamePath) = 0;

    /// <summary>
    /// The names of the files in a folder that match a DOS wildcard, in name order (MCFileSystem::NameLess).
    /// </summary>
    virtual std::vector<std::string> FindFiles(std::string_view gamePattern) = 0;

    /// <summary>The same files as <see cref="FindFiles"/>, each with the folders that have it.</summary>
    virtual std::vector<MCFoundFile> FindFileSources(std::string_view gamePattern) = 0;

    /// <summary>Whether a loose file (not one inside a FastFile) exists.</summary>
    virtual bool Exists(std::string_view gamePath) = 0;

    /// <summary>Deletes a loose file.</summary>
    virtual bool RemoveFile(std::string_view gamePath) = 0;

    /// <summary>Deletes a save game, the install's copy included.</summary>
    virtual bool RemoveSave(std::string_view gamePath) = 0;

    /// <summary>Renames a loose file, replacing an existing target.</summary>
    virtual bool RenameFile(std::string_view fromGamePath, std::string_view toGamePath) = 0;

    /// <summary>Copies a loose file over another.</summary>
    virtual bool CopyFile(std::string_view fromGamePath, std::string_view toGamePath) = 0;

    /// <summary>
    /// The contents of a file the source holds in memory rather than on disk. <c>File::open</c> asks for one when the
    /// path isn't on disk, before it looks in the FastFiles. The disk has none.
    /// </summary>
    virtual std::optional<std::span<const uint8_t>> FindImage([[maybe_unused]] std::string_view gamePath)
    {
        return std::nullopt;
    }
};

/// <summary>
/// The game's files on disk: the retail install, overlaid by a writable user folder (see platform/MCFileSystem.h).
/// </summary>
class MCDiskFileSource final : public MCFileSource
{
public:
    /// <summary>A source whose game root is the current folder, without a user folder.</summary>
    MCDiskFileSource();

    void SetGameRoot(const std::filesystem::path& root) override;
    const std::filesystem::path& GameRoot() const override;
    void SetUserRoot(const std::filesystem::path& root) override;
    const std::filesystem::path& UserRoot() const override;
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

private:
    /// <summary>Whether <paramref name="gamePath"/> is looked up in the user folder first.</summary>
    bool HasOverlay(std::string_view gamePath) const;

    /// <summary>
    /// The launcher's copy of <paramref name="gamePath"/>, when it's a map or mod (MCFileSystem::IsLauncherContent) the
    /// launcher's folder holds; else none.
    /// </summary>
    std::optional<std::filesystem::path> InLauncherFolder(std::string_view gamePath) const;

    std::filesystem::path _Root;
    std::filesystem::path _UserRoot;
    /// <summary>A launcher's folder, read for its maps and mods only and never written (empty: none).</summary>
    std::filesystem::path _LauncherRoot;
};

/// <summary>
/// The sound output: it opens the mixer the sound renderer plays through, and hears of every buffer that starts or
/// stops (the mixer tells it, through a <c>weak_ptr</c>: the context holds the device by <c>shared_ptr</c>).
/// </summary>
class MCAudioDevice : public std::enable_shared_from_this<MCAudioDevice>
{
public:
    virtual ~MCAudioDevice() = default;

    /// <summary>Opens the mixer; it tells this device of its buffers' plays and stops while it lives.</summary>
    virtual std::unique_ptr<MCAudio> OpenMixer() = 0;

    /// <summary>A buffer of a mixer this device opened started to play.</summary>
    virtual void BufferPlayed([[maybe_unused]] const MCSoundBuffer& buffer, [[maybe_unused]] bool looping) {}

    /// <summary>A buffer of a mixer this device opened was stopped.</summary>
    virtual void BufferStopped([[maybe_unused]] const MCSoundBuffer& buffer) {}
};

/// <summary>
/// The machine's default playback device through SDL or, when there is none, SDL's dummy driver, which plays nowhere
/// at a device's rate.
/// </summary>
class MCSdlAudioDevice final : public MCAudioDevice
{
public:
    std::unique_ptr<MCAudio> OpenMixer() override;
};

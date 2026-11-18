#pragma once

// Port-only (the workspace's SPEC-first-use.md, its guard): what the game's thread reads, unpacks or waits on for the
// first time while a mission plays, counted. The rule is that it does none of it: everything play uses is read and
// unpacked under the loading bar or by HD art's readers ahead. Each place that reads from the disk, unpacks, decodes a
// sound or waits on a reader notes itself here (MCFirstUseScope); only the outermost of nested notes counts (an HD
// entry's read holds its disk read and its unpacking). Counting costs a flag's read where nothing is counted.
//
// Only standard headers: MCDiskFile.cpp includes this and is built without the precompiled header on Windows.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/// <summary>What the game's thread did in play that the rule says it never does.</summary>
enum class MCFirstUseKind : uint8_t
{
    /// <summary>A file opened for reading.</summary>
    Open,
    /// <summary>Bytes read from a file (the system asked; a buffered file's own buffer isn't a read).</summary>
    Read,
    /// <summary>Packed bytes unpacked: the original's LZ, the HD pack's deflate or zstd.</summary>
    Unpack,
    /// <summary>A sound decoded, or waited for while its decoder makes it.</summary>
    Sound,
    /// <summary>A wait on HD art's readers: a frame they were reading, an archive they were opening.</summary>
    Wait,
    /// <summary>A movie opened, or its first frame waited for.</summary>
    Movie,
};

/// <summary>One thing the game's thread did in play (see <see cref="MCFirstUseKind"/>).</summary>
struct MCFirstUse
{
    MCFirstUseKind Kind = MCFirstUseKind::Read;
    /// <summary>What it was: a file's name, an archive's entry, a sound's.</summary>
    std::string What;
    /// <summary>How long it took the game's thread, in milliseconds.</summary>
    double Ms = 0.0;
    /// <summary>The bytes read or made.</summary>
    uint64_t Bytes = 0;
    /// <summary>The game frame it was in (MCFirstUses::NextFrame's count since play started).</summary>
    int64_t Frame = 0;
};

namespace MCFirstUses
{
    /// <summary>The most uses kept to be listed; past it they're counted alone (Dropped).</summary>
    inline constexpr size_t MostKept = 20000;

    /// <summary>
    /// Play has started (the mission's load is over): from now the calling thread, the game's, has its first uses
    /// counted, until <see cref="PlayEnded"/>.
    /// </summary>
    void PlayStarted();

    /// <summary>Play has ended (the scenario stops): nothing more is counted.</summary>
    void PlayEnded();

    /// <summary>Whether the calling thread's first uses are being counted (the game's, in play).</summary>
    bool Counting();

    /// <summary>A frame of play has started (the frame log's count, for the uses' frames).</summary>
    void NextFrame();

    /// <summary>Notes <paramref name="use"/> (the game's thread, in play: see <see cref="Counting"/>).</summary>
    void Note(MCFirstUse use);

    /// <summary>The uses noted since play started or the last take (up to MostKept), and they're forgotten.</summary>
    std::vector<MCFirstUse> Take();

    /// <summary>How many uses went unkept since the last take, past MostKept.</summary>
    size_t Dropped();

    /// <summary>The kind's name, as the guard's lines give it.</summary>
    std::string_view KindName(MCFirstUseKind kind);

    /// <summary>A file's name for a use: its folder's name and its own, as UTF-8.</summary>
    std::string FileName(const std::filesystem::path& path);
}

/// <summary>
/// What the code around a use is doing, named into each use noted while it lives (a packet's read, a type's load):
/// "context: what", outer contexts first. Naming costs nothing unless a use is noted.
/// </summary>
class MCFirstUseContext
{
public:
    template <typename Name>
    explicit MCFirstUseContext(const Name& name)
        : _Name(&name)
        , _Call([](const void* named) { return std::string(static_cast<const Name*>(named)->operator()()); })
    {
        Enter();
    }

    /// <summary>The name is kept by reference: a temporary would be gone before a use is named.</summary>
    template <typename Name> explicit MCFirstUseContext(const Name&& name) = delete;

    ~MCFirstUseContext();
    MCFirstUseContext(const MCFirstUseContext&) = delete;
    MCFirstUseContext& operator=(const MCFirstUseContext&) = delete;

    /// <summary>The names of the contexts the calling thread is in, outer first ("a: b: "), or empty.</summary>
    static std::string Names();

private:
    void Enter();

    const void* _Name;
    std::string (*_Call)(const void*);
    MCFirstUseContext* _Outer = nullptr;
};

/// <summary>
/// Notes one use of a kind when it ends, if the game's thread is counted in play and no use is being noted around it
/// (the outermost counts). Name it (What, Bytes) once Counting says it will be noted.
/// </summary>
class MCFirstUseScope
{
public:
    explicit MCFirstUseScope(MCFirstUseKind kind);

    /// <summary>
    /// A use named by <paramref name="name"/> (called only when it will be noted, so naming costs nothing otherwise) of
    /// <paramref name="bytes"/> bytes.
    /// </summary>
    template <typename Name>
    MCFirstUseScope(MCFirstUseKind kind, const Name& name, uint64_t bytes) : MCFirstUseScope(kind)
    {
        if (_Counting)
        {
            What = std::string(name());
            Bytes = bytes;
        }
    }

    ~MCFirstUseScope();
    MCFirstUseScope(const MCFirstUseScope&) = delete;
    MCFirstUseScope& operator=(const MCFirstUseScope&) = delete;

    /// <summary>Whether this use is noted when it ends (so naming it is worth its cost).</summary>
    bool Counting() const { return _Counting; }

    /// <summary>What it is, its bytes and its kind, as noted when it ends (each may change before).</summary>
    std::string What;
    uint64_t Bytes = 0;
    MCFirstUseKind Kind;

private:
    bool _Counting = false;
    std::chrono::steady_clock::time_point _Started;
};

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Port-only (SPEC-memory.md's "The ledger"): every byte the game holds belongs to a named holder, with its side (the
// CPU's memory or the GPU's) and three figures: its payload (the bytes it needs), what it has allocated for them, and
// what it has retired but not yet freed (a GPU texture let go of while a frame being drawn still holds it). A holder's
// figures are the sum of its shares (MCShare), each counted while it lives, so a share can't be left counted after its
// bytes are gone. The frame log writes the ledger each second beside the system's own figure for the game
// (MCMemoryRoom::MCSystem::Process) and the plan's room (half the machine's memory; a card's room on its own memory).
// It counts; it decides nothing.

namespace MCMemoryLedger
{
    /// <summary>Which memory a holder's bytes are in.</summary>
    enum class MCSide : uint8_t
    {
        /// <summary>The machine's memory, as the CPU allocates it.</summary>
        Cpu,
        /// <summary>The GPU's (on shared memory, the machine's too).</summary>
        Gpu,
    };

    /// <summary>The holders, by what they hold (NameOf names each in the frame log).</summary>
    enum class MCHolder : uint8_t
    {
        /// <summary>MCHdArt's frames' records kept in memory (each frame's size and flags, and its R8G8 indices).</summary>
        HdFrames,
        /// <summary>
        /// MCHdArt's frames' colours, unpacked (BC7), held until the GPU has their pictures (SPEC-memory.md's one copy of
        /// a frame's colours).
        /// </summary>
        HdColors,
        /// <summary>MCHdArt's frames its cache let go of that something else still holds (a texture's picture, a draw's).</summary>
        HdFramesLetGo,
        /// <summary>MCHdArt's frames and pictures read ahead, waiting to be taken.</summary>
        HdReadAhead,
        /// <summary>The HD pack's streams held as stored (MCFastFile::HoldStream).</summary>
        HdHold,
        /// <summary>The HD pack's unpacked streams kept for their neighbours' reads (MCFastFile).</summary>
        HdKeptStreams,
        /// <summary>The archives' tables as read into memory (MCFastFile's entries, names and placements).</summary>
        ArchiveTables,
        /// <summary>HD audio's decoded sounds, the kept ones and those within its budget.</summary>
        HdSounds,
        /// <summary>The GPU's HD pictures: their pages and textures (payload: the pictures placed in them).</summary>
        GpuHdPictures,
        /// <summary>The GPU's original shapes' atlas pages.</summary>
        GpuShapes,
        /// <summary>The GPU's original pictures (R8) and their copies.</summary>
        GpuPictures,
        /// <summary>The GPU's HD ground: the tiles' colour and relief atlases.</summary>
        GpuHdGround,
        /// <summary>The GPU's original ground: the tiles' R8G8 atlas and the terrain's buffers.</summary>
        GpuGround,
        /// <summary>The screen's targets: the surfaces drawn into, the kept ground, the presenter's frames.</summary>
        GpuTargets,
        /// <summary>The movies' textures.</summary>
        GpuMovies,
        /// <summary>The upload and download buffers the CPU writes or reads.</summary>
        GpuTransfers,
        /// <summary>The rest of the GPU's: the tables, palettes, blanks and the draws' buffer.</summary>
        GpuOther,
        Count,
    };

    inline constexpr size_t HolderCount = static_cast<size_t>(MCHolder::Count);

    /// <summary>A holder's figures, or a change to them (bytes; a change may be negative).</summary>
    struct MCFigures
    {
        int64_t Payload = 0;
        int64_t Allocated = 0;
        int64_t Retired = 0;

        bool operator==(const MCFigures&) const = default;
    };

    /// <summary>The holder's name in the frame log ("hd frames").</summary>
    std::string_view NameOf(MCHolder holder);

    /// <summary>The memory the holder's bytes are in.</summary>
    MCSide SideOf(MCHolder holder);

    /// <summary>
    /// Whether the holder's payload is counted apart from its allocations (the HD pictures': the pictures placed in its
    /// pages), so a GPU object of it adds to its allocations alone; for every other holder an allocation is payload.
    /// </summary>
    bool PayloadApart(MCHolder holder);

    /// <summary>Adds <paramref name="change"/> to the holder's figures (any thread). Prefer an MCShare.</summary>
    void Add(MCHolder holder, const MCFigures& change);

    /// <summary>The holder's figures now (any thread).</summary>
    MCFigures Of(MCHolder holder);

    /// <summary>The figures of every holder of <paramref name="side"/> now, summed (any thread).</summary>
    MCFigures OfSide(MCSide side);

    /// <summary>
    /// A part of a holder's figures, counted from Set until Set again (replacing them) or its end (taking them away).
    /// Owned by one thread at a time; moved, never copied.
    /// </summary>
    class MCShare
    {
    public:
        MCShare() = default;
        explicit MCShare(MCHolder holder) : _Holder(holder) {}
        ~MCShare();
        MCShare(MCShare&& other) noexcept;
        MCShare& operator=(MCShare&& other) noexcept;
        MCShare(const MCShare&) = delete;
        MCShare& operator=(const MCShare&) = delete;

        /// <summary>Counts <paramref name="figures"/> in place of what this share counted.</summary>
        void Set(const MCFigures& figures);

        /// <summary>Counts <paramref name="bytes"/> both as payload and as allocated (a holder that allocates what it needs).</summary>
        void SetBytes(size_t bytes);

        /// <summary>What this share counts.</summary>
        const MCFigures& Figures() const { return _Figures; }

    private:
        std::optional<MCHolder> _Holder;
        MCFigures _Figures;
    };

    /// <summary>What the system says the game holds (MCMemoryRoom::MCSystem::Process); 0 where it can't say.</summary>
    struct MCProcessFigures
    {
        /// <summary>
        /// The system's figure for the game's own memory: on macOS its physical footprint (which takes in Metal's memory:
        /// never add the GPU's allocations to it); on Windows its private bytes (the GPU's mapped pages among them).
        /// </summary>
        uint64_t Footprint = 0;
        /// <summary>Windows: what the game uses on the GPU's own memory and in the machine's (DXGI's local and non-local).</summary>
        uint64_t GpuLocal = 0;
        uint64_t GpuNonLocal = 0;
        /// <summary>
        /// Windows: what of the private bytes is the GPU's memory mapped for the CPU (its driver's write-combined pages),
        /// which DXGI's figures count already: taken out before the CPU's side is set against them.
        /// </summary>
        uint64_t GpuMapped = 0;
        /// <summary>
        /// What of the system's figure is the heap every CPU holder allocates from: on macOS its regions' pages (dirty
        /// or compressed, the footprint's), on Windows what mimalloc has committed (private bytes all).
        /// </summary>
        uint64_t Heap = 0;
    };

    /// <summary>The plan's room as the frame log shows it (SPEC-memory.md's "The ceiling").</summary>
    struct MCRoom
    {
        /// <summary>The machine's memory (MCMemoryRoom::TotalBytes: -hdmemory's when given).</summary>
        uint64_t MachineBytes = 0;
        /// <summary>Whether the GPU's bytes come out of the machine's memory (Apple silicon, integrated graphics).</summary>
        bool Shared = true;
        /// <summary>A card's own memory the driver gives the game (none: shared memory, or the system can't say).</summary>
        std::optional<uint64_t> CardBytes;
    };

    /// <summary>The most the game holds of <paramref name="room"/>'s machine: half its memory.</summary>
    uint64_t Ceiling(const MCRoom& room);

    /// <summary>The most the game's GPU side holds of <paramref name="room"/>'s card: 90% of what its driver gives it.</summary>
    uint64_t CardCeiling(uint64_t cardBytes);

    /// <summary>
    /// The ledger's line for the frame log: each holder with any bytes (payload, allocated, retired, in MB), each side's
    /// sum, the room against the ceilings, and the system's figures, the heap's among them.
    /// </summary>
    std::string Line(const MCRoom& room, const MCProcessFigures& process);
}

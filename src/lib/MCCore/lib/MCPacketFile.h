#pragma once

#include "lib/MCFile.h"

/// <summary>How a packet is stored (the top 3 bits of its offset in the packet table).</summary>
enum class MCPacketStorage : uint8_t
{
    /// <summary>The bytes as they are.</summary>
    Raw = 0,
    /// <summary>Raw, by another name (no packed file uses it; it reads as raw).</summary>
    Fwf = 1,
    /// <summary>LZ-packed, after its unpacked size.</summary>
    Lzd = 2,
    /// <summary>Huffman (never read or written by the game).</summary>
    Hf = 3,
    /// <summary>zlib (never read or written by the game).</summary>
    Zlib = 4,
    /// <summary>
    /// An empty packet when read; when written, "pack it if that makes it smaller" (stored raw or LZD).
    /// </summary>
    Nul = 7
};

/// <summary>Port-only: where a packet lies in its file, as stored (<see cref="MCPacketFile::StoredPacket"/>).</summary>
struct MCStoredPacket
{
    /// <summary>The packet's number.</summary>
    int32_t Packet = 0;
    /// <summary>Where its bytes start in the file.</summary>
    uint32_t Offset = 0;
    /// <summary>Its bytes: to the next packet's start, or the file's end.</summary>
    uint32_t Size = 0;
    /// <summary>How it's stored: raw, LZ or Nul.</summary>
    MCPacketStorage Storage = MCPacketStorage::Raw;
};

/// <summary>Port-only: what <see cref="MCPacketFile::ReadPackets"/> read.</summary>
struct MCPacketBatch
{
    /// <summary>
    /// Each packet asked for, in the order asked: its bytes as <see cref="MCPacketFile::ReadPacket"/> gives them (none
    /// for an empty packet), or nothing (std::nullopt) where the caller must read it itself.
    /// </summary>
    std::vector<std::optional<std::vector<uint8_t>>> Packets;
    /// <summary>The reads of the file made.</summary>
    size_t Reads = 0;
    /// <summary>The bytes the largest of them read.</summary>
    size_t MostReadBytes = 0;
};

/// <summary>
/// A file of numbered packets (PAK, MPK, ...): a header, a table of packet offsets whose top 3 bits give each
/// packet's storage, then the packets, each raw or LZ-packed (preceded by its unpacked size).
/// </summary>
/// <remarks>
/// Original source: <c>lib\packet.cpp</c>. Layout: <c>int32 checksumOrMagic</c> (0xFEEDFACE when unchecked),
/// <c>int32 firstPacketOffset</c> (so the table has <c>firstPacketOffset / 4 - 2</c> entries), the table, then the
/// data.
/// </remarks>
class MCPacketFile : public MCFile
{
public:
    /// <summary>The first word of a packet file whose checksum isn't checked.</summary>
    static constexpr int32_t PacketFileMagic = static_cast<int32_t>(0xFEEDFACE);

    MCPacketFile() = default;
    ~MCPacketFile() override;

    int32_t Open(std::string_view fileName, MCFileMode mode = MCFileMode::Read) override;
    int32_t Open(MCFile& parent, uint32_t length) override;
    int32_t Create(std::string_view fileName) override;

    /// <summary>Finishes the packet table (when writing) and closes.</summary>
    void Close() override;

    MCFileClass GetFileClass() const override { return MCFileClass::Packet; }

    /// <summary>
    /// Reads (and unpacks) packet <paramref name="packet"/> into <paramref name="buffer"/>; -1 means the current one.
    /// </summary>
    /// <returns>Its size, or 0 on failure (a packed packet that doesn't unpack to its full size included).</returns>
    int32_t ReadPacket(int32_t packet, std::span<uint8_t> buffer);

    /// <summary>Makes <paramref name="packet"/> current and moves to its data.</summary>
    int32_t SeekPacket(int32_t packet);

    /// <summary>
    /// Port-only: packets <paramref name="first"/> to <paramref name="first"/> + <paramref name="count"/> - 1 as stored
    /// (none unpacked), in one read: each packet's storage and stored size (4 bytes each), then their bytes. For a hash of
    /// what they hold. Empty when the packets aren't all in the file, in order.
    /// </summary>
    std::vector<uint8_t> ReadStoredPackets(int32_t first, int32_t count);

    /// <summary>Port-only: the most reads <see cref="ReadPackets"/> has under way at once (the disk's queue).</summary>
    static constexpr size_t BatchReadsAtOnce = 4;

    /// <summary>
    /// Port-only: where packet <paramref name="packet"/> lies and how it's stored, checked as SeekPacket checks it
    /// (issue #25).
    /// </summary>
    /// <returns>
    /// None for a packet not in the table, one starting at or before the table's start (SeekPacket fails it), ending
    /// before it starts or past the file's end, or stored other than raw, LZ or Nul (SeekPacket then keeps the size the
    /// packet sought before it gave); and for a file not open to read.
    /// </returns>
    std::optional<MCStoredPacket> StoredPacket(int32_t packet) const;

    /// <summary>
    /// Port-only: what ReadPacket gives for the packet <paramref name="stored"/>, from its stored bytes
    /// <paramref name="bytes"/>: a raw packet's bytes, an LZ packet unpacked to exactly the size it gives, nothing for a
    /// Nul packet (whatever its bytes) or one of no bytes.
    /// </summary>
    /// <returns>
    /// None (std::nullopt) where ReadPacket's result isn't that: an LZ packet too short to hold its size, one whose size
    /// SeekPacket refuses (issue #25, <see cref="SetMostUnpacked"/>) or that doesn't unpack to it; and bytes not the
    /// packet's size.
    /// </returns>
    std::optional<std::vector<uint8_t>> UnpackStored(const MCStoredPacket& stored,
                                                     std::span<const uint8_t> bytes) const;

    /// <summary>
    /// Port-only: reads the packets <paramref name="packets"/> as ReadPacket does, together: in the order they lie in the
    /// file, each run of neighbours within MCDiskFile::RunGapBytes of each other in one read of at most
    /// MCDiskFile::RunMostBytes (a bigger packet read alone; a Nul packet's bytes not read), up to
    /// <see cref="BatchReadsAtOnce"/> reads at once, each thread on its own reader (<see cref="OpenReader"/>), and each
    /// packet unpacked on the thread that read it. A packet asked for twice is read once. The file's position and
    /// current packet stay as they were.
    /// </summary>
    /// <returns>
    /// Each packet's bytes, or none where the caller must read it itself: StoredPacket or UnpackStored refuses it, its
    /// read came up short, or the file can't be read so (a child, a file written).
    /// </returns>
    MCPacketBatch ReadPackets(std::span<const int32_t> packets) const;

    /// <summary>
    /// Port-only: refuses (SeekPacket) an LZ packet that claims to unpack to more than <paramref name="bytes"/>, for a
    /// file whose packets are known to be small (a save's). Otherwise only more than its LZ data can make is refused
    /// (4096 bytes for each), so a packet of a few hundred kilobytes could claim 2 GiB. Kept until the file closes.
    /// </summary>
    void SetMostUnpacked(int32_t bytes) { _MostUnpacked = bytes; }

    int32_t GetNumPackets() const { return _NumPackets; }
    int32_t GetCurrentPacket() const { return _CurrentPacket; }
    /// <summary>The current packet's unpacked size.</summary>
    int32_t GetPacketSize() const { return _PacketUnpackedSize; }
    /// <summary>The current packet's stored size.</summary>
    int32_t GetPackedPacketSize() const { return _PacketSize; }
    /// <summary>The current packet's storage.</summary>
    MCPacketStorage GetStorageType() const { return _PacketType; }

    /// <summary>
    /// Starts a new file of <paramref name="count"/> packets: writes the header and an empty table.
    /// <paramref name="useCheckSum"/> makes <see cref="Close"/> write a checksum instead of the magic.
    /// </summary>
    void Reserve(int32_t count, bool useCheckSum = false);

    /// <summary>
    /// Appends packet <paramref name="packet"/> stored as <paramref name="storage"/> (<see cref="MCPacketStorage::Lzd"/>
    /// packs it, <see cref="MCPacketStorage::Nul"/> packs it if that makes it smaller, anything else stores it raw).
    /// </summary>
    /// <returns>The number of data bytes written.</returns>
    int32_t WritePacket(int32_t packet, std::span<const uint8_t> data, MCPacketStorage storage);

protected:
    /// <summary>Forgets the packet table and the current packet.</summary>
    void Clear();
    /// <summary>Resolves the table's forward references and writes the checksum, if the file was written.</summary>
    void AtClose();
    /// <summary>Checks the header and reads the packet table.</summary>
    int32_t AfterOpen();
    /// <summary>The sum of every byte after the first word.</summary>
    int32_t CheckSumFile();
    /// <summary>
    /// The offset of packet <paramref name="packet"/>, and its storage in <paramref name="storage"/>; -1 past the end
    /// (the storage left as it was).
    /// </summary>
    int32_t ReadPacketOffset(int32_t packet, MCPacketStorage& storage) const;
    /// <summary>The offset of packet <paramref name="packet"/>; -1 past the end.</summary>
    int32_t ReadPacketOffset(int32_t packet) const;
    /// <summary>
    /// Issue #25: the most an LZ packet of <paramref name="storedSize"/> bytes may unpack to (what its LZ data can make,
    /// each 12-bit code a string of at most 4096 bytes, or less, <see cref="SetMostUnpacked"/>).
    /// </summary>
    int64_t MostUnpacked(int64_t storedSize) const;

    int32_t _NumPackets = 0;
    int32_t _CurrentPacket = -1;
    /// <summary>The current packet's stored size.</summary>
    int32_t _PacketSize = 0;
    /// <summary>The current packet's offset.</summary>
    int32_t _PacketBase = 0;
    MCPacketStorage _PacketType = MCPacketStorage::Raw;
    int32_t _PacketUnpackedSize = 0;
    /// <summary>The packet table, read into memory (empty when the file is written through the disk table).</summary>
    std::vector<uint32_t> _SeekTable;
    /// <summary>Whether the header holds a checksum.</summary>
    bool _UsesCheckSum = false;
    /// <summary>Port-only: the most an LZ packet may unpack to (SetMostUnpacked; 0: what its LZ data can).</summary>
    int32_t _MostUnpacked = 0;
};

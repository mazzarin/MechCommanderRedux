#include "stdafx.h"
#include "lib/MCPacketFile.h"
#include "lib/MCFatal.h"
#include "lib/MCLz.h"
#include "lib/MCWorkers.h"

namespace
{
    constexpr uint32_t TypeShift = 29;
    constexpr uint32_t OffsetMask = 0x1fffffff;
    /// <summary>The storage bits of a table entry whose packet hasn't been written yet.</summary>
    constexpr uint32_t PendingMark = 0xe0000000;
    constexpr int32_t PacketOutOfRange = static_cast<int32_t>(0xBADF0004);
    constexpr int32_t BadPacketVersion = static_cast<int32_t>(0xBADF0004);
    constexpr int32_t PacketWrongStorage = static_cast<int32_t>(0xBADF000B);
    /// <summary>An LZ packet's unpacked size, before its LZ data.</summary>
    constexpr int64_t UnpackedSizeBytes = sizeof(int32_t);
    /// <summary>Issue #25: the most a byte of LZ data unpacks to (a 12-bit code's string is at most 4096 bytes).</summary>
    constexpr int64_t MostUnpackedPerByte = 4096;

    /// <summary>The bytes a packet's entry in ReadStoredPackets' table takes.</summary>
    constexpr size_t StoredEntryBytes = 4;

    /// <summary>
    /// A packet's entry in ReadStoredPackets' table, at the start of <paramref name="into"/>: its storage, then its stored
    /// size's three low bytes, lowest first.
    /// </summary>
    void WriteStoredEntry(std::span<uint8_t> into, MCPacketStorage storage, uint32_t size)
    {
        into[0] = static_cast<uint8_t>(storage);
        into[1] = static_cast<uint8_t>(size);
        into[2] = static_cast<uint8_t>(size >> 8);
        into[3] = static_cast<uint8_t>(size >> 16);
    }

    /// <summary>The table as the bytes a file holds.</summary>
    std::span<uint8_t> TableBytes(std::vector<uint32_t>& table)
    {
        return std::span(reinterpret_cast<uint8_t*>(table.data()), table.size() * sizeof(uint32_t));
    }
}

MCPacketFile::~MCPacketFile()
{
    Close();
}

void MCPacketFile::Clear()
{
    _CurrentPacket = -1;
    _MostUnpacked = 0;
    _NumPackets = 0;
    _PacketBase = 0;
    _PacketSize = 0;
    _SeekTable.clear();
    _Held.clear();
}

void MCPacketFile::AtClose()
{
    if (IsOpen() && _FileMode != MCFileMode::Read)
    {
        // Packets left "pending" (never written) point at the next written packet, or at the end.
        // (A written file always has its table in memory: Reserve made it.)
        uint32_t next = GetLength();

        for (_CurrentPacket = _NumPackets - 1; _CurrentPacket >= 0; --_CurrentPacket)
        {
            uint32_t& entry = _SeekTable[static_cast<size_t>(_CurrentPacket)];

            if ((entry & PendingMark) == PendingMark)
            {
                entry = next + PendingMark;
            }
            else
            {
                next = entry & OffsetMask;
            }
        }

        Seek(8);
        Write(TableBytes(_SeekTable));

        if (_UsesCheckSum)
        {
            const int32_t sum = CheckSumFile();
            Seek(0);
            WriteLong(sum);
        }
    }

    Clear();
}

int32_t MCPacketFile::CheckSumFile()
{
    const uint32_t saved = _LogicalPosition;
    Seek(4);
    const uint32_t size = FileSize();
    std::vector<uint8_t> bytes(size);
    Read(bytes);
    int32_t sum = 0;

    for (uint32_t i = 0; i + 4 < size; ++i)
    {
        sum += bytes[i];
    }

    Seek(static_cast<int32_t>(saved));
    return sum;
}

int32_t MCPacketFile::AfterOpen()
{
    if (_NumPackets == 0 && GetLength() >= 12)
    {
        const int32_t firstWord = ReadLong();

        if ((firstWord != PacketFileMagic || _UsesCheckSum) && CheckSumFile() != firstWord)
        {
            return BadPacketVersion;
        }

        const uint32_t firstPacket = static_cast<uint32_t>(ReadLong());

        // Issue #25: the table must lie in the file (the original allocated whatever the word said).
        if (firstPacket > GetLength())
        {
            return BadPacketVersion;
        }

        _NumPackets = static_cast<int32_t>(firstPacket / 4) - 2;
    }

    _CurrentPacket = -1;

    if (_FileMode == MCFileMode::Read && _NumPackets > 0 && _SeekTable.empty())
    {
        _SeekTable.assign(static_cast<size_t>(_NumPackets), 0);
        Seek(8);
        Read(TableBytes(_SeekTable));
    }

    return NO_ERR;
}

int32_t MCPacketFile::Open(std::string_view fileName, MCFileMode mode)
{
    int32_t result = MCFile::Open(fileName, mode);

    if (result == NO_ERR)
    {
        result = AfterOpen();
    }

    return result;
}

int32_t MCPacketFile::Open(MCFile& parent, uint32_t length)
{
    int32_t result = MCFile::Open(parent, length);

    if (result == NO_ERR)
    {
        result = AfterOpen();
    }

    return result;
}

int32_t MCPacketFile::Create(std::string_view fileName)
{
    int32_t result = MCFile::Create(fileName);

    if (result == NO_ERR)
    {
        result = AfterOpen();
    }

    return result;
}

void MCPacketFile::Close()
{
    AtClose();
    MCFile::Close();
}

int32_t MCPacketFile::ReadPacketOffset(int32_t packet, MCPacketStorage& storage) const
{
    uint32_t offset = 0xffffffff;

    if (packet < _NumPackets)
    {
        if (!_SeekTable.empty())
        {
            offset = _SeekTable[static_cast<size_t>(packet)];
        }

        storage = static_cast<MCPacketStorage>(offset >> TypeShift);
        offset &= OffsetMask;
    }

    return static_cast<int32_t>(offset);
}

int32_t MCPacketFile::ReadPacketOffset(int32_t packet) const
{
    MCPacketStorage storage = MCPacketStorage::Raw;
    return ReadPacketOffset(packet, storage);
}

int64_t MCPacketFile::MostUnpacked(int64_t storedSize) const
{
    const int64_t lzMost = std::max<int64_t>(storedSize - UnpackedSizeBytes, 0) * MostUnpackedPerByte;
    return _MostUnpacked > 0 ? std::min<int64_t>(lzMost, _MostUnpacked) : lzMost;
}

int32_t MCPacketFile::ReadPacket(int32_t packet, std::span<uint8_t> buffer)
{
    CloseIfOrphaned();

    if (packet != -1 && packet != _CurrentPacket && SeekPacket(packet) != NO_ERR)
    {
        return 0;
    }

    // Port-only (HoldPackets): what the read below gives, from what's held, the position left as it leaves it.
    if (const auto held = _Held.find(_CurrentPacket);
        held != _Held.end() && held->second.Stored.Storage != MCPacketStorage::Nul)
    {
        const std::vector<uint8_t>& bytes = held->second.Bytes;
        const size_t count = std::min(buffer.size(), bytes.size());
        std::copy_n(bytes.begin(), count, buffer.begin());

        if (held->second.Stored.Storage == MCPacketStorage::Raw)
        {
            Seek(_PacketBase + static_cast<int32_t>(count));
            return static_cast<int32_t>(count);
        }

        Seek(_PacketBase + _PacketSize);
        return count == bytes.size() ? static_cast<int32_t>(count) : 0;
    }

    switch (_PacketType)
    {
        case MCPacketStorage::Raw:
        case MCPacketStorage::Fwf:
        {
            Seek(_PacketBase);
            return Read(buffer.first(std::min(buffer.size(), static_cast<size_t>(std::max(_PacketSize, 0)))));
        }
        case MCPacketStorage::Lzd:
        {
            Seek(_PacketBase + 4);
            std::vector<uint8_t> packed(static_cast<size_t>(std::max(_PacketSize - 4, 0)));
            packed.resize(static_cast<size_t>(std::max(Read(packed), 0)));
            const int32_t unpacked = LZDecomp(
                buffer.first(std::min(buffer.size(), static_cast<size_t>(std::max(_PacketUnpackedSize, 0)))), packed);
            return unpacked == _PacketUnpackedSize ? unpacked : 0;
        }
        default:
        {
            return 0;
        }
    }
}

int32_t MCPacketFile::SeekPacket(int32_t packet)
{
    CloseIfOrphaned();

    if (packet < 0)
    {
        return PacketOutOfRange;
    }

    // Port-only (HoldPackets): as the seek below leaves the file, from what's held, reading nothing.
    if (const auto held = _Held.find(packet); held != _Held.end())
    {
        const MCStoredPacket& stored = held->second.Stored;
        _CurrentPacket = packet;
        _PacketType = stored.Storage;
        _PacketBase = static_cast<int32_t>(stored.Offset);
        _PacketSize = static_cast<int32_t>(stored.Size);
        _PacketUnpackedSize =
            stored.Storage == MCPacketStorage::Nul ? 0 : static_cast<int32_t>(held->second.Bytes.size());
        Seek(_PacketBase + (stored.Storage == MCPacketStorage::Lzd ? static_cast<int32_t>(UnpackedSizeBytes) : 0));
        return NO_ERR;
    }

    const int32_t offset = ReadPacketOffset(packet, _PacketType);
    _CurrentPacket = packet;
    const int32_t next = packet + 1 == _NumPackets ? static_cast<int32_t>(GetLength()) : ReadPacketOffset(packet + 1);
    _PacketSize = next - offset;
    _PacketBase = offset;
    Seek(offset);

    switch (_PacketType)
    {
        case MCPacketStorage::Raw:
            _PacketUnpackedSize = _PacketSize;
            break;
        case MCPacketStorage::Lzd:
            _PacketUnpackedSize = ReadLong();
            break;
        case MCPacketStorage::Nul:
            _PacketUnpackedSize = 0;
            break;
        default:
            return PacketWrongStorage;
    }

    // Issue #25: a read file's table and sizes come from the file. A packet outside the file, or one that claims to
    // unpack to more than its LZ data can (each 12-bit code gives at most a 4096-byte string), or than the file is held
    // to (SetMostUnpacked), is refused before anyone allocates its size.
    if (_FileMode == MCFileMode::Read)
    {
        const auto length = static_cast<int64_t>(GetLength());
        const int64_t maxUnpacked = MostUnpacked(_PacketSize);

        if (offset < 0 || next < offset || next > length ||
            (_PacketType == MCPacketStorage::Lzd && (_PacketUnpackedSize < 0 || _PacketUnpackedSize > maxUnpacked)))
        {
            _PacketSize = 0;
            _PacketUnpackedSize = 0;
            return PacketOutOfRange;
        }
    }

    return offset > 0 ? NO_ERR : PacketOutOfRange;
}

std::vector<uint8_t> MCPacketFile::ReadStoredPackets(int32_t first, int32_t count)
{
    CloseIfOrphaned();
    _CurrentPacket = -1;

    if (first < 0 || count <= 0 || first > _NumPackets - count || _SeekTable.empty())
    {
        return {};
    }

    const auto length = static_cast<int64_t>(GetLength());
    std::vector<uint8_t> stored;
    int64_t start = -1;
    int64_t end = -1;

    for (int32_t packet = first; packet < first + count; ++packet)
    {
        MCPacketStorage storage = MCPacketStorage::Raw;
        const int64_t offset = ReadPacketOffset(packet, storage);
        const int64_t next = packet + 1 == _NumPackets ? length : ReadPacketOffset(packet + 1);

        if (offset <= 0 || next < offset || next > length || (end >= 0 && offset != end))
        {
            return {};
        }

        start = start < 0 ? offset : start;
        end = next;
        stored.resize(stored.size() + StoredEntryBytes);
        WriteStoredEntry(std::span(stored).last(StoredEntryBytes), storage, static_cast<uint32_t>(next - offset));
    }

    const size_t table = stored.size();
    stored.resize(table + static_cast<size_t>(end - start));
    Seek(static_cast<int32_t>(start));
    const int32_t read = Read(std::span(stored).subspan(table));
    return read == end - start ? stored : std::vector<uint8_t>{};
}

std::optional<MCStoredPacket> MCPacketFile::StoredPacket(int32_t packet) const
{
    if (_FileMode != MCFileMode::Read || !IsOpen() || packet < 0 || packet >= _NumPackets || _SeekTable.empty())
    {
        return std::nullopt;
    }

    MCPacketStorage storage = MCPacketStorage::Raw;
    const int64_t offset = ReadPacketOffset(packet, storage);
    const int64_t length = _IsChild ? _PhysicalLength : _Length;
    const int64_t next = packet + 1 == _NumPackets ? length : ReadPacketOffset(packet + 1);

    // SeekPacket's checks; and its success, which a packet at 0 fails (ReadTile then reads the header as it).
    if (offset <= 0 || next < offset || next > length)
    {
        return std::nullopt;
    }

    switch (storage)
    {
        case MCPacketStorage::Raw:
        case MCPacketStorage::Lzd:
        case MCPacketStorage::Nul:
            return MCStoredPacket{.Packet = packet,
                                  .Offset = static_cast<uint32_t>(offset),
                                  .Size = static_cast<uint32_t>(next - offset),
                                  .Storage = storage};
        default:
            return std::nullopt;
    }
}

std::optional<std::vector<uint8_t>> MCPacketFile::UnpackStored(const MCStoredPacket& stored,
                                                               std::span<const uint8_t> bytes) const
{
    if (stored.Storage == MCPacketStorage::Nul)
    {
        return std::vector<uint8_t>{};
    }

    if (bytes.size() != stored.Size)
    {
        return std::nullopt;
    }

    switch (stored.Storage)
    {
        case MCPacketStorage::Raw:
            return std::vector<uint8_t>(bytes.begin(), bytes.end());
        case MCPacketStorage::Lzd:
        {
            if (std::cmp_less(bytes.size(), UnpackedSizeBytes))
            {
                return std::nullopt;
            }

            int32_t size = 0;
            std::memcpy(&size, bytes.data(), sizeof(size));

            if (size < 0 || size > MostUnpacked(stored.Size))
            {
                return std::nullopt;
            }

            std::vector<uint8_t> unpacked(static_cast<size_t>(size));

            if (LZDecomp(unpacked, bytes.subspan(UnpackedSizeBytes)) != size)
            {
                return std::nullopt;
            }

            return unpacked;
        }
        default:
            return std::nullopt;
    }
}

MCPacketBatch MCPacketFile::ReadPackets(std::span<const int32_t> packets) const
{
    const std::array<MCPacketsWanted, 1> one{MCPacketsWanted{*this, packets}};
    return std::move(ReadPackets(one).front());
}

std::vector<MCPacketBatch> MCPacketFile::ReadPackets(std::span<const MCPacketsWanted> files)
{
    std::vector<MCPacketBatch> batches(files.size());

    /// <summary>
    /// A packet to read: where it lies, where in its root (the file on disk or in memory its file's bytes lie in, an
    /// index into roots) it starts, its file and where its result goes (wanted as stored: a run of Stored, At its bytes'
    /// place in that run's result), and whether it's the one before it again.
    /// </summary>
    struct MCWanted
    {
        MCStoredPacket Stored;
        uint64_t Start = 0;
        size_t Root = 0;
        size_t File = 0;
        size_t Asked = 0;
        bool AsStored = false;
        size_t At = 0;
        bool Again = false;
    };

    // Each root once, with the first file of it (a reader of it is opened through that file).
    std::vector<std::pair<MCAddressKey, size_t>> roots;
    std::vector<MCWanted> wanted;

    for (size_t at = 0; at < files.size(); ++at)
    {
        const MCPacketFile& file = files[at].File;
        batches[at].Packets.resize(files[at].Packets.size());
        batches[at].Stored.resize(files[at].Stored.size());
        const MCAddressKey key = file.RootKey();

        if (key == MCAddressKey())
        {
            continue;
        }

        auto root = std::ranges::find(roots, key, &std::pair<MCAddressKey, size_t>::first);

        if (root == roots.end())
        {
            roots.emplace_back(key, at);
            root = std::prev(roots.end());
        }

        const auto rootIndex = static_cast<size_t>(root - roots.begin());
        const uint64_t base = file.RootOffset();

        for (size_t asked = 0; asked < files[at].Packets.size(); ++asked)
        {
            const std::optional<MCStoredPacket> stored = file.StoredPacket(files[at].Packets[asked]);

            // A Nul packet reads as empty, whatever its bytes: none are read for it.
            if (stored.has_value() && stored->Storage == MCPacketStorage::Nul)
            {
                batches[at].Packets[asked] = file.UnpackStored(*stored, {});
            }
            else if (stored.has_value())
            {
                wanted.push_back(MCWanted{
                    .Stored = *stored, .Start = base + stored->Offset, .Root = rootIndex, .File = at, .Asked = asked});
            }
        }

        // A run as stored: its table, then its packets' bytes (a Nul packet's too) where each lands, read with the
        // rest; none for a run ReadStoredPackets would refuse (its packets not all in the file, in order).
        for (size_t asked = 0; asked < files[at].Stored.size(); ++asked)
        {
            const MCStoredRun& run = files[at].Stored[asked];
            std::vector<MCStoredPacket> packets;

            for (int32_t packet = run.First; run.Count > 0 && packet - run.First < run.Count; ++packet)
            {
                const std::optional<MCStoredPacket> stored = file.StoredPacket(packet);

                if (!stored.has_value() ||
                    (!packets.empty() && stored->Offset != packets.back().Offset + packets.back().Size))
                {
                    packets.clear();
                    break;
                }

                packets.push_back(*stored);
            }

            if (packets.empty())
            {
                continue;
            }

            const size_t table = packets.size() * StoredEntryBytes;
            std::vector<uint8_t>& result = batches[at].Stored[asked].emplace(
                table + packets.back().Offset + packets.back().Size - packets.front().Offset);

            for (size_t k = 0; k < packets.size(); ++k)
            {
                const MCStoredPacket& packet = packets[k];
                WriteStoredEntry(std::span(result).subspan(k * StoredEntryBytes), packet.Storage, packet.Size);
                wanted.push_back(MCWanted{.Stored = packet,
                                          .Start = base + packet.Offset,
                                          .Root = rootIndex,
                                          .File = at,
                                          .Asked = asked,
                                          .AsStored = true,
                                          .At = table + packet.Offset - packets.front().Offset});
            }
        }
    }

    std::ranges::sort(wanted, {},
                      [](const MCWanted& packet)
                      {
                          return std::tuple(packet.Root, packet.Start, packet.File, packet.Stored.Packet,
                                            packet.AsStored, packet.Asked, packet.At);
                      });

    for (size_t k = 1; k < wanted.size(); ++k)
    {
        wanted[k].Again = wanted[k].File == wanted[k - 1].File &&
                          wanted[k].Stored.Packet == wanted[k - 1].Stored.Packet &&
                          wanted[k].AsStored == wanted[k - 1].AsStored;
    }

    /// <summary>One read: wanted's packets from First, Count of them, of root Root, the bytes from Start to End there.</summary>
    struct MCRun
    {
        size_t First = 0;
        size_t Count = 0;
        size_t Root = 0;
        uint64_t Start = 0;
        uint64_t End = 0;
    };

    std::vector<MCRun> runs;

    for (size_t k = 0; k < wanted.size(); ++k)
    {
        const uint64_t start = wanted[k].Start;
        const uint64_t end = start + wanted[k].Stored.Size;

        if (!runs.empty() && runs.back().Root == wanted[k].Root)
        {
            MCRun& run = runs.back();
            const uint64_t joined = std::max(run.End, end);

            // A packet within the read already (another's, wanted again or as stored) adds no bytes, whatever its size.
            if (end <= run.End ||
                (start <= run.End + MCDiskFile::RunGapBytes && joined - run.Start <= MCDiskFile::RunMostBytes))
            {
                run.End = joined;
                ++run.Count;
                continue;
            }
        }

        runs.push_back(MCRun{.First = k, .Count = 1, .Root = wanted[k].Root, .Start = start, .End = end});
    }

    if (runs.empty())
    {
        return batches;
    }

    // Each thread's reads and largest read, by the file of each read's first packet; and which packets' bytes came.
    const size_t threads = std::min(runs.size(), BatchReadsAtOnce);
    std::vector<std::vector<size_t>> reads(threads, std::vector<size_t>(files.size(), 0));
    std::vector<std::vector<size_t>> mostBytes(threads, std::vector<size_t>(files.size(), 0));
    std::vector<uint8_t> landed(wanted.size(), 0);
    std::atomic<size_t> nextRun{0};
    // The reads wait on the disk, not the cores: as many threads as reads at once, whatever the machine.
    MCWorkers workers(threads - 1);
    workers.Run(threads,
                [&](size_t thread)
                {
                    // This thread's reader of each root, opened as it first reads it (none: the caller's).
                    std::vector<std::optional<MCFileReader>> readers(roots.size());
                    std::vector<bool> opened(roots.size(), false);
                    std::vector<uint8_t> bytes;

                    for (size_t r = nextRun.fetch_add(1, std::memory_order_relaxed); r < runs.size();
                         r = nextRun.fetch_add(1, std::memory_order_relaxed))
                    {
                        const MCRun& run = runs[r];

                        if (!opened[run.Root])
                        {
                            opened[run.Root] = true;
                            readers[run.Root] = files[roots[run.Root].second].File.get().OpenRootReader();
                        }

                        if (!readers[run.Root].has_value())
                        {
                            continue;
                        }

                        bytes.resize(static_cast<size_t>(run.End - run.Start));
                        size_t got = 0;
                        const size_t first = wanted[run.First].File;

                        if (!bytes.empty())
                        {
                            got = readers[run.Root]->ReadAt(static_cast<uint32_t>(run.Start), bytes);
                            ++reads[thread][first];
                            mostBytes[thread][first] = std::max(mostBytes[thread][first], bytes.size());
                        }

                        for (size_t k = run.First; k < run.First + run.Count; ++k)
                        {
                            const MCWanted& packet = wanted[k];
                            const uint64_t from = packet.Start - run.Start;

                            if (packet.Again || from + packet.Stored.Size > got)
                            {
                                continue;
                            }

                            const std::span<const uint8_t> read =
                                std::span(bytes).subspan(static_cast<size_t>(from), packet.Stored.Size);

                            // Each run as stored is its own result, its packets' places apart: one thread writes each.
                            if (packet.AsStored)
                            {
                                std::ranges::copy(read, std::next(batches[packet.File].Stored[packet.Asked]->begin(),
                                                                  static_cast<std::ptrdiff_t>(packet.At)));
                                landed[k] = 1;
                            }
                            else
                            {
                                batches[packet.File].Packets[packet.Asked] =
                                    files[packet.File].File.get().UnpackStored(packet.Stored, read);
                            }
                        }
                    }
                });

    for (size_t k = 1; k < wanted.size(); ++k)
    {
        const MCWanted& packet = wanted[k];
        const MCWanted& before = wanted[k - 1];

        if (packet.Again && packet.AsStored && landed[k - 1] != 0)
        {
            const std::vector<uint8_t>& from = *batches[before.File].Stored[before.Asked];
            const auto first = std::next(from.begin(), static_cast<std::ptrdiff_t>(before.At));
            std::copy(
                first, std::next(first, static_cast<std::ptrdiff_t>(packet.Stored.Size)),
                std::next(batches[packet.File].Stored[packet.Asked]->begin(), static_cast<std::ptrdiff_t>(packet.At)));
            landed[k] = 1;
        }
        else if (packet.Again && !packet.AsStored)
        {
            batches[packet.File].Packets[packet.Asked] = batches[before.File].Packets[before.Asked];
        }
    }

    // A run as stored whose packets didn't all come is the caller's to read.
    for (size_t k = 0; k < wanted.size(); ++k)
    {
        if (wanted[k].AsStored && landed[k] == 0)
        {
            batches[wanted[k].File].Stored[wanted[k].Asked].reset();
        }
    }

    for (size_t at = 0; at < files.size(); ++at)
    {
        for (size_t thread = 0; thread < threads; ++thread)
        {
            batches[at].Reads += reads[thread][at];
            batches[at].MostReadBytes = std::max(batches[at].MostReadBytes, mostBytes[thread][at]);
        }
    }

    return batches;
}

MCPacketsHeld MCPacketFile::HoldPackets(std::span<const int32_t> packets)
{
    MCPacketBatch batch = ReadPackets(packets);
    MCPacketsHeld held{.Reads = batch.Reads};

    for (size_t at = 0; at < packets.size(); ++at)
    {
        std::optional<MCStoredPacket> stored = StoredPacket(packets[at]);

        if (batch.Packets[at].has_value() && stored.has_value() && !_Held.contains(packets[at]))
        {
            held.Bytes += batch.Packets[at]->size();
            _Held.emplace(packets[at], MCHeldPacket{*stored, std::move(*batch.Packets[at])});
            ++held.Packets;
        }
    }

    return held;
}

void MCPacketFile::Reserve(int32_t count, bool useCheckSum)
{
    if (_NumPackets != 0)
    {
        return;
    }

    _UsesCheckSum = useCheckSum;
    _NumPackets = count;
    const int32_t firstPacket = count * 4 + 8;
    WriteLong(PacketFileMagic);
    WriteLong(firstPacket);

    for (int32_t i = 0; i < count; ++i)
    {
        WriteLong(static_cast<int32_t>(static_cast<uint32_t>(firstPacket) + PendingMark));
    }

    _SeekTable.assign(static_cast<size_t>(std::max(_NumPackets, 0)), 0);
    Seek(8);
    Read(TableBytes(_SeekTable));
}

int32_t MCPacketFile::WritePacket(int32_t packet, std::span<const uint8_t> data, MCPacketStorage storage)
{
    if (packet < 0 || packet >= _NumPackets)
    {
        return 0;
    }

    const int32_t size = static_cast<int32_t>(data.size());
    const uint32_t end = GetLength();
    _PacketBase = static_cast<int32_t>(end);
    _CurrentPacket = packet;
    _PacketUnpackedSize = size;
    _PacketSize = size;
    _PacketType = MCPacketStorage::Raw;
    std::vector<uint8_t> packed;

    if (storage == MCPacketStorage::Nul || storage == MCPacketStorage::Lzd)
    {
        packed = LZCompress(data);
        std::vector<uint8_t> check(data.size() + 16);

        if (LZDecomp(check, packed) != size)
        {
            Fatal(-1, " Holy Dogshit, Private Pyle ");
        }

        if (storage == MCPacketStorage::Lzd || static_cast<int32_t>(packed.size()) < size)
        {
            _PacketType = MCPacketStorage::Lzd;
            _PacketSize = static_cast<int32_t>(packed.size());
        }
    }
    else
    {
        _PacketType = storage;
    }

    Seek(static_cast<int32_t>(end));
    int32_t written;

    if (_PacketType == MCPacketStorage::Lzd)
    {
        WriteLong(_PacketUnpackedSize);
        written = Write(packed);
    }
    else
    {
        written = Write(data);
    }

    const uint32_t entry = static_cast<uint32_t>(_PacketType) << TypeShift | static_cast<uint32_t>(_PacketBase);

    _SeekTable[static_cast<size_t>(packet)] = entry;

    // The packets after it point at the new end until they are written (the last one isn't moved; Close points
    // every packet still pending at the next written one).
    const uint32_t newEnd = GetLength();

    for (int32_t i = packet + 1; i < _NumPackets - 1; ++i)
    {
        _SeekTable[static_cast<size_t>(i)] = newEnd + PendingMark;
    }

    return written;
}

#include "stdafx.h"
#include "lib/MCFastFile.h"
#include "lib/MCFirstUse.h"
#include "lib/MCInflate.h"
#include "lib/MCLz.h"
#include "lib/MCZstd.h"
#include "platform/MCFileSystem.h"

// xxHash, header-only: its functions inline in this file alone.
#define XXH_INLINE_ALL
#include "xxhash.h"

namespace
{
    /// <summary>The name in the case the index keys use.</summary>
    std::string IndexKey(std::string_view name)
    {
        std::string key(name);

        for (char& c : key)
        {
            if (c >= 'a' && c <= 'z')
            {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }

        return key;
    }

    /// <summary>Port-only: a version 2 archive's magic, its first 4 bytes (SPEC-pack-v2.md's header).</summary>
    constexpr std::array<uint8_t, 4> Version2Magic = {'M', 'C', 'H', '2'};
    constexpr uint16_t Version2Format = 2;
    constexpr size_t Version2HeaderBytes = 64;
    constexpr size_t FolderRowBytes = 16;
    constexpr size_t StreamRowBytes = 24;
    /// <summary>Port-only: where a stream's row holds its kind (MCFastFile::StreamKind).</summary>
    constexpr size_t StreamRowKindAt = 17;
    /// <summary>Port-only: where a stream's row holds the number of the dictionary it was packed against (0: none).</summary>
    constexpr size_t StreamRowDictionaryAt = 18;
    constexpr size_t EntryRowBytes = 20;
    constexpr size_t RecordHeadBytes = 8;

    /// <summary>Port-only: a stream's codec (SPEC-pack-v2.md's streams).</summary>
    enum class Codec : uint8_t
    {
        Stored = 0,
        Deflate = 1,
        Zstd = 2,
    };

    /// <summary>
    /// Port-only: the most bytes a version 2 table or stream may unpack to (a malformed pack can't make the game
    /// allocate more); the builder cuts a packet's stream well below it.
    /// </summary>
    constexpr uint32_t MostUnpackedBytes = 256u << 20;
    /// <summary>The most bytes of a version 2 archive's names read (the menus' 1,478: about 45 KB, 19 Nov).</summary>
    constexpr uint64_t MostNamesBytes = 64u << 20;

    /// <summary>A little-endian value at <paramref name="at"/> of <paramref name="bytes"/>, which must hold it.</summary>
    template <typename T> T ValueAt(std::span<const uint8_t> bytes, size_t at)
    {
        T value{};
        std::memcpy(&value, bytes.data() + at, sizeof(T));
        return value;
    }

    /// <summary>
    /// Port-only: <paramref name="stored"/> unpacked by <paramref name="codec"/> to <paramref name="size"/> bytes,
    /// against <paramref name="dictionary"/> (Zstandard's alone; the table's checks keep it from any other codec).
    /// </summary>
    std::expected<std::vector<uint8_t>, std::string> UnpackStream(
        uint8_t codec, std::span<const uint8_t> stored, size_t size,
        MCOptionalRef<const MCZstd::Dictionary> dictionary = std::nullopt)
    {
        switch (static_cast<Codec>(codec))
        {
            case Codec::Stored:
            {
                if (stored.size() != size)
                {
                    return std::unexpected(std::format("{} bytes stored where {} are said", stored.size(), size));
                }

                return std::vector<uint8_t>(stored.begin(), stored.end());
            }
            case Codec::Deflate:
                return MCInflate::Inflate(stored, size);
            case Codec::Zstd:
                return MCZstd::Unpack(stored, size, dictionary);
        }

        return std::unexpected(std::format("codec {} isn't one this build reads", codec));
    }

    /// <summary>Port-only: an unpacked stream kept a while, by its archive's number and its own.</summary>
    struct KeptStream
    {
        uint64_t Archive = 0;
        uint32_t Stream = 0;
        std::shared_ptr<const std::vector<uint8_t>> Bytes;
    };

    /// <summary>Port-only: the unpacked streams kept, the latest first, and their bytes in all.</summary>
    struct KeptStreams
    {
        std::mutex Lock;
        std::list<KeptStream> Kept;
        size_t Bytes = 0;
        /// <summary>The most Bytes kept (MCFastFile::KeepStreamsUpTo), the latest stream kept whatever its size.</summary>
        size_t Most = MCFastFile::KeptStreamsLeast;
        /// <summary>Bytes, in the ledger.</summary>
        MCMemoryLedger::MCShare Ledger{MCMemoryLedger::MCHolder::HdKeptStreams};
    };

    KeptStreams& TheKeptStreams()
    {
        static KeptStreams kept;
        return kept;
    }

    /// <summary>Lets go of the oldest streams kept past their most, the latest kept whatever its size. With kept's lock held.</summary>
    void TrimKept(KeptStreams& kept)
    {
        while (kept.Bytes > kept.Most && kept.Kept.size() > 1)
        {
            kept.Bytes -= kept.Kept.back().Bytes->size();
            kept.Kept.pop_back();
        }

        kept.Ledger.SetBytes(kept.Bytes);
    }

    /// <summary>A run held: its bytes, and when an entry in it was last asked for (HeldStreams::Uses).</summary>
    struct HeldRun
    {
        std::shared_ptr<const std::vector<uint8_t>> Bytes;
        uint64_t LastUse = 0;
    };

    /// <summary>An archive's runs held, each by where it starts in the archive (MCFastFile::HoldPieces).</summary>
    using HeldRuns = std::map<uint64_t, HeldRun>;

    /// <summary>
    /// Port-only (the hold, SPEC-memory.md's fill): the runs of the archives' stored bytes held, by their archive's own
    /// number (MCFastFile::_Serial), and their bytes in all.
    /// </summary>
    struct HeldStreams
    {
        std::mutex Lock;
        std::unordered_map<uint64_t, HeldRuns> Runs;
        size_t Bytes = 0;
        /// <summary>Counts the asks for an entry held (MCFastFile::HeldOf), the runs' clock for giving back the oldest.</summary>
        uint64_t Uses = 0;
        /// <summary>Bytes, in the ledger.</summary>
        MCMemoryLedger::MCShare Ledger{MCMemoryLedger::MCHolder::HdHold};
    };

    HeldStreams& TheHeldStreams()
    {
        static HeldStreams held;
        return held;
    }

    /// <summary>
    /// Port-only (the hold): keeps <paramref name="run"/>, an archive's stored bytes from <paramref name="start"/>, among
    /// archive <paramref name="serial"/>'s runs held (a run already held there stays).
    /// </summary>
    void KeepRun(uint64_t serial, uint64_t start, std::vector<uint8_t>&& run)
    {
        auto bytes = std::make_shared<const std::vector<uint8_t>>(std::move(run));
        HeldStreams& held = TheHeldStreams();
        std::scoped_lock lock(held.Lock);

        if (held.Runs[serial].try_emplace(start, HeldRun{bytes, ++held.Uses}).second)
        {
            held.Bytes += bytes->size();
            held.Ledger.SetBytes(held.Bytes);
        }
    }

    /// <summary>Port-only: each archive's own number (KeptStream::Archive, HeldStreams::Runs).</summary>
    std::atomic<uint64_t> NextSerial{1};

    /// <summary>Port-only: the index of <paramref name="key"/> in sorted <paramref name="keys"/>, if there.</summary>
    std::optional<size_t> KeyIndex(std::span<const uint64_t> keys, uint64_t key)
    {
        const auto found = std::ranges::lower_bound(keys, key);
        return found != keys.end() && *found == key ? std::optional<size_t>(static_cast<size_t>(found - keys.begin()))
                                                    : std::nullopt;
    }

    /// <summary><paramref name="entry"/>'s stored bytes, read through <paramref name="file"/>.</summary>
    std::vector<uint8_t> StoredBytes(const MCFileEntry& entry, MCDiskFile& file)
    {
        std::vector<uint8_t> stored(static_cast<size_t>(std::max(entry.Size, 0)));
        stored.resize(file.ReadAt(static_cast<uint32_t>(entry.Offset), stored));
        return stored;
    }
}

std::expected<std::unique_ptr<MCFastFile>, std::string> MCFastFile::Create(
    std::string_view fileName, std::shared_ptr<const MCZstd::Dictionaries> dictionaries)
{
    return CreateAt(MCFileSystem::Resolve(fileName), fileName, std::move(dictionaries));
}

std::expected<std::unique_ptr<MCFastFile>, std::string> MCFastFile::CreateAt(
    const std::filesystem::path& path, std::string_view fileName,
    std::shared_ptr<const MCZstd::Dictionaries> dictionaries)
{
    auto fastFile = std::make_unique<MCFastFile>(Key());
    fastFile->_Dictionaries = std::move(dictionaries);
    fastFile->_Path = path;
    fastFile->_Handle = MCDiskFile::Open(fastFile->_Path, MCDiskFileMode::Read, MCDiskFileReads::Direct);

    if (!fastFile->_Handle.has_value())
    {
        return std::unexpected(std::format("{}: not found", fileName));
    }

    MCDiskFile& file = *fastFile->_Handle;
    std::array<uint8_t, sizeof(int32_t)> countBytes{};

    if (file.Read(countBytes) != countBytes.size())
    {
        return std::unexpected(std::format("{}: no directory", fileName));
    }

    if (countBytes == Version2Magic)
    {
        if (std::expected<void, std::string> read = fastFile->ReadVersion2(fileName); !read.has_value())
        {
            return std::unexpected(std::move(read.error()));
        }

        fastFile->CountTables();
        return fastFile;
    }

    // Issue #25: the directory must lie in the file (the original allocated whatever the count said).
    const int32_t count = std::bit_cast<int32_t>(countBytes);
    const uint32_t length = file.Length();
    fastFile->_Length = length;
    std::error_code timeError;
    fastFile->_Written = std::filesystem::last_write_time(fastFile->_Path, timeError);

    if (count < 0 || static_cast<uint64_t>(count) * sizeof(MCFileEntry) > length - std::min<uint64_t>(length, 4))
    {
        return std::unexpected(std::format("{}: a directory of {} entries", fileName, count));
    }

    // The directory in one read (an HD archive has thousands of entries). The original ignores the byte count; a
    // short directory leaves the rest zeroed.
    std::vector<MCFileEntry>& entries = fastFile->_Entries;
    entries.assign(static_cast<size_t>(count), MCFileEntry{});
    file.Read(std::span(reinterpret_cast<uint8_t*>(entries.data()), entries.size() * sizeof(MCFileEntry)));
    fastFile->_Index.reserve(entries.size());

    for (size_t i = 0; i < entries.size(); ++i)
    {
        MCFileEntry& entry = entries[i];
        entry.Name.back() = 0;

        // Its stored bytes lie in the archive, and it unpacks to no more than its LZ data can (each 12-bit code
        // gives at most a 4096-byte string); readers allocate the unpacked size.
        const int64_t stored = entry.Size;
        const int64_t unpacked = entry.RealSize;

        if (entry.Offset < 0 || stored < 0 || entry.Offset + stored > static_cast<int64_t>(length) || unpacked < 0 ||
            unpacked > (stored == unpacked ? stored : stored * 4096))
        {
            return std::unexpected(
                std::format("{}: entry {} ({}) lies outside the archive", fileName, i, entry.GetName()));
        }

        fastFile->_Index.try_emplace(IndexKey(entry.GetName()), static_cast<int32_t>(i));
    }

    // Port-only (the hold): its entries in the order their bytes lie, the longest first where two start together (an
    // entry stored once for two names), and a number of its own for what is held of it.
    std::vector<int32_t>& byOffset = fastFile->_ByOffset;
    byOffset.resize(entries.size());
    std::iota(byOffset.begin(), byOffset.end(), 0);
    std::ranges::sort(byOffset,
                      [&entries](int32_t a, int32_t b)
                      {
                          const MCFileEntry& first = entries[static_cast<size_t>(a)];
                          const MCFileEntry& second = entries[static_cast<size_t>(b)];
                          return first.Offset != second.Offset ? first.Offset < second.Offset
                                                               : first.Size > second.Size;
                      });
    fastFile->_Serial = NextSerial.fetch_add(1);
    fastFile->CountTables();
    return fastFile;
}

std::expected<void, std::string> MCFastFile::ReadVersion2(std::string_view fileName)
{
    MCDiskFile& file = *_Handle;
    const uint32_t length = file.Length();
    _Length = length;
    std::error_code timeError;
    _Written = std::filesystem::last_write_time(_Path, timeError);
    std::array<uint8_t, Version2HeaderBytes> head{};

    if (file.ReadAt(0, head) != head.size())
    {
        return std::unexpected(std::format("{}: a version 2 header cut short", fileName));
    }

    const std::span<const uint8_t> header = head;
    const auto format = ValueAt<uint16_t>(header, 4);
    const auto tableStored = ValueAt<uint32_t>(header, 8);
    const auto tableUnpacked = ValueAt<uint32_t>(header, 12);
    const auto tableCodec = ValueAt<uint8_t>(header, 16);
    const auto folders = ValueAt<uint32_t>(header, 20);
    const auto streams = ValueAt<uint32_t>(header, 24);
    const auto entries = ValueAt<uint32_t>(header, 28);
    const auto factsBytes = ValueAt<uint32_t>(header, 32);
    const auto dataAt = ValueAt<uint64_t>(header, 40);
    const auto tableHash = ValueAt<uint64_t>(header, 48);
    const auto namesAt = ValueAt<uint64_t>(header, 56);

    if (format != Version2Format)
    {
        return std::unexpected(
            std::format("{}: archive format {}, this build reads {}", fileName, format, Version2Format));
    }

    const uint64_t rows = uint64_t{folders} * FolderRowBytes + uint64_t{streams} * StreamRowBytes +
                          uint64_t{entries} * EntryRowBytes + factsBytes;

    if (tableUnpacked > MostUnpackedBytes || rows != tableUnpacked ||
        Version2HeaderBytes + uint64_t{tableStored} > length || dataAt < Version2HeaderBytes + uint64_t{tableStored} ||
        dataAt > length)
    {
        return std::unexpected(
            std::format("{}: a table of {} bytes ({} stored) that doesn't fit its counts or the file", fileName,
                        tableUnpacked, tableStored));
    }

    std::vector<uint8_t> stored(tableStored);

    if (file.ReadAt(static_cast<uint32_t>(Version2HeaderBytes), stored) != stored.size())
    {
        return std::unexpected(std::format("{}: its table cut short", fileName));
    }

    std::expected<std::vector<uint8_t>, std::string> unpacked = UnpackStream(tableCodec, stored, tableUnpacked);

    if (!unpacked.has_value())
    {
        return std::unexpected(std::format("{}: its table: {}", fileName, unpacked.error()));
    }

    const std::span<const uint8_t> table = *unpacked;

    if (XXH3_64bits(table.data(), table.size()) != tableHash)
    {
        return std::unexpected(std::format("{}: its table doesn't match its hash", fileName));
    }

    size_t at = 0;
    _FolderKeys.reserve(folders);
    _FolderFacts.reserve(folders);

    for (uint32_t i = 0; i < folders; ++i, at += FolderRowBytes)
    {
        const auto start = ValueAt<uint32_t>(table, at + 8);
        const auto bytes = ValueAt<uint32_t>(table, at + 12);

        if (uint64_t{start} + bytes > factsBytes)
        {
            return std::unexpected(std::format("{}: folder {}'s facts lie outside the facts", fileName, i));
        }

        _FolderKeys.push_back(ValueAt<uint64_t>(table, at));
        _FolderFacts.emplace_back(start, bytes);
    }

    _Streams.reserve(streams);

    for (uint32_t i = 0; i < streams; ++i, at += StreamRowBytes)
    {
        const Stream stream{.At = ValueAt<uint64_t>(table, at),
                            .Stored = ValueAt<uint32_t>(table, at + 8),
                            .Unpacked = ValueAt<uint32_t>(table, at + 12),
                            .Codec = ValueAt<uint8_t>(table, at + 16),
                            .Kind = static_cast<StreamKind>(ValueAt<uint8_t>(table, at + StreamRowKindAt)),
                            .Dictionary = ValueAt<uint16_t>(table, at + StreamRowDictionaryAt)};

        // Within the file, and within the entries' 32-bit offsets (MCFileEntry), as a version 1 archive is.
        if (stream.At < dataAt || stream.At + stream.Stored > length || stream.Unpacked > MostUnpackedBytes ||
            stream.At + stream.Stored > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
        {
            return std::unexpected(std::format("{}: stream {} lies outside the data", fileName, i));
        }

        // A dictionary is Zstandard's alone, and must be one the pack's dicts.hdd holds: without it the stream can't
        // be unpacked, so the archive is refused whole here rather than an entry at a time later.
        if (stream.Dictionary != 0 && static_cast<Codec>(stream.Codec) != Codec::Zstd)
        {
            return std::unexpected(std::format("{}: stream {} names dictionary {} with codec {}, which takes none",
                                               fileName, i, stream.Dictionary, stream.Codec));
        }

        if (stream.Dictionary != 0 && (_Dictionaries == nullptr || !_Dictionaries->Find(stream.Dictionary)))
        {
            return std::unexpected(
                std::format("{}: stream {} is packed against dictionary {}, which the pack's dicts.hdd lacks", fileName,
                            i, stream.Dictionary));
        }

        _Streams.push_back(stream);
    }

    _EntryKeys.reserve(entries);
    _Placed.reserve(entries);

    for (uint32_t i = 0; i < entries; ++i, at += EntryRowBytes)
    {
        const Placed placed{.Stream = ValueAt<uint32_t>(table, at + 8),
                            .At = ValueAt<uint32_t>(table, at + 12),
                            .Bytes = ValueAt<uint32_t>(table, at + 16)};

        if (placed.Stream >= _Streams.size() || uint64_t{placed.At} + placed.Bytes > _Streams[placed.Stream].Unpacked)
        {
            return std::unexpected(std::format("{}: entry {} lies outside its stream", fileName, i));
        }

        _EntryKeys.push_back(ValueAt<uint64_t>(table, at));
        _Placed.push_back(placed);
    }

    if (!std::ranges::is_sorted(_EntryKeys) || !std::ranges::is_sorted(_FolderKeys))
    {
        return std::unexpected(std::format("{}: its keys aren't in order", fileName));
    }

    _Facts.assign(table.begin() + static_cast<ptrdiff_t>(at), table.end());
    _NamesAt = namesAt;
    _Version2 = true;
    _Serial = NextSerial.fetch_add(1);
    return {};
}

std::expected<std::vector<std::string>, std::string> MCFastFile::Names() const
{
    const std::string name = MCFileSystem::ToUtf8(_Path);
    std::vector<std::string> names;

    if (!_Version2)
    {
        names.reserve(_Entries.size());

        for (const MCFileEntry& entry : _Entries)
        {
            names.emplace_back(entry.GetName());
        }

        return names;
    }

    if (_NamesAt == 0)
    {
        return std::unexpected(std::format("{} keeps no names", name));
    }

    if (_NamesAt < Version2HeaderBytes || _NamesAt >= _Length || _Length - _NamesAt > MostNamesBytes)
    {
        return std::unexpected(std::format("{}: its names at {} don't fit its {} bytes", name, _NamesAt, _Length));
    }

    std::optional<MCDiskFile> file = OpenAnother();
    std::vector<uint8_t> bytes(static_cast<size_t>(_Length - _NamesAt));

    if (!file.has_value() || file->ReadAt(static_cast<uint32_t>(_NamesAt), bytes) != bytes.size())
    {
        return std::unexpected(std::format("{}: its names can't be read", name));
    }

    // Each zero-terminated, one an entry.
    names.reserve(_Placed.size());
    size_t start = 0;

    for (size_t at = 0; at < bytes.size(); ++at)
    {
        if (bytes[at] == 0)
        {
            names.emplace_back(reinterpret_cast<const char*>(bytes.data() + start), at - start);
            start = at + 1;
        }
    }

    if (start != bytes.size() || names.size() != _Placed.size())
    {
        return std::unexpected(std::format("{}: {} names for {} entries{}", name, names.size(), _Placed.size(),
                                           start != bytes.size() ? ", the last unterminated" : ""));
    }

    return names;
}

uint64_t MCFastFile::PathKey(std::string_view path)
{
    std::string lower(path);

    for (char& c : lower)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }

    return XXH3_64bits(lower.data(), lower.size());
}

bool MCFastFile::HasFolder(std::string_view path) const
{
    return _Version2 && KeyIndex(_FolderKeys, PathKey(path)).has_value();
}

std::vector<std::pair<uint16_t, std::span<const uint8_t>>> MCFastFile::Facts(std::string_view path) const
{
    std::vector<std::pair<uint16_t, std::span<const uint8_t>>> records;
    const std::optional<size_t> folder = _Version2 ? KeyIndex(_FolderKeys, PathKey(path)) : std::nullopt;

    if (!folder.has_value())
    {
        return records;
    }

    const auto [start, bytes] = _FolderFacts[*folder];
    const std::span<const uint8_t> facts = std::span<const uint8_t>(_Facts).subspan(start, bytes);

    for (size_t at = 0; at + RecordHeadBytes <= facts.size();)
    {
        const auto type = ValueAt<uint16_t>(facts, at);
        const auto size = ValueAt<uint32_t>(facts, at + 4);

        if (size > facts.size() - at - RecordHeadBytes)
        {
            break;
        }

        records.emplace_back(type, facts.subspan(at + RecordHeadBytes, size));
        at += RecordHeadBytes + size;
    }

    return records;
}

std::expected<std::vector<uint8_t>, std::string> MCFastFile::Unpack(int32_t index,
                                                                    std::span<const uint8_t> stored) const
{
    std::expected<std::shared_ptr<const std::vector<uint8_t>>, std::string> stream = StreamOf(index, stored);

    if (!stream.has_value())
    {
        return std::unexpected(std::move(stream.error()));
    }

    const std::span<const uint8_t> bytes = EntryIn(index, **stream);
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

std::expected<std::shared_ptr<const std::vector<uint8_t>>, std::string> MCFastFile::StreamOf(
    int32_t index, std::span<const uint8_t> stored) const
{
    const MCFileEntry entry = EntryAt(index);

    if (stored.size() != static_cast<size_t>(std::max(entry.Size, 0)))
    {
        return std::unexpected(std::format("{} of {} stored bytes read", stored.size(), entry.Size));
    }

    if (!_Version2)
    {
        if (entry.Size == entry.RealSize)
        {
            return std::make_shared<const std::vector<uint8_t>>(stored.begin(), stored.end());
        }

        std::expected<std::vector<uint8_t>, std::string> inflated =
            MCInflate::Inflate(stored, static_cast<size_t>(std::max(entry.RealSize, 0)));

        if (!inflated.has_value())
        {
            return std::unexpected(std::move(inflated.error()));
        }

        return std::make_shared<const std::vector<uint8_t>>(std::move(*inflated));
    }

    if (std::shared_ptr<const std::vector<uint8_t>> kept = KeptStreamOf(index); kept != nullptr)
    {
        return kept;
    }

    const Placed& placed = _Placed[static_cast<size_t>(index)];
    const Stream& stream = _Streams[placed.Stream];
    // The table's read checked the dictionary is there.
    const MCOptionalRef<const MCZstd::Dictionary> dictionary =
        stream.Dictionary != 0 ? _Dictionaries->Find(stream.Dictionary) : std::nullopt;
    std::expected<std::vector<uint8_t>, std::string> unpacked =
        UnpackStream(stream.Codec, stored, stream.Unpacked, dictionary);

    if (!unpacked.has_value())
    {
        return std::unexpected(std::format("stream {}: {}", placed.Stream, unpacked.error()));
    }

    auto bytes = std::make_shared<const std::vector<uint8_t>>(std::move(*unpacked));
    KeptStreams& kept = TheKeptStreams();
    std::scoped_lock lock(kept.Lock);
    kept.Kept.push_front(KeptStream{_Serial, placed.Stream, bytes});
    kept.Bytes += bytes->size();
    TrimKept(kept);
    return bytes;
}

std::shared_ptr<const std::vector<uint8_t>> MCFastFile::KeptStreamOf(int32_t index) const
{
    if (!_Version2)
    {
        return nullptr;
    }

    const uint32_t stream = _Placed.at(static_cast<size_t>(index)).Stream;
    KeptStreams& kept = TheKeptStreams();
    std::scoped_lock lock(kept.Lock);
    const auto found = std::ranges::find_if(kept.Kept, [&](const KeptStream& one)
                                            { return one.Archive == _Serial && one.Stream == stream; });

    if (found == kept.Kept.end())
    {
        return nullptr;
    }

    kept.Kept.splice(kept.Kept.begin(), kept.Kept, found);
    return found->Bytes;
}

MCFastFile::StreamFacts MCFastFile::StreamAt(uint32_t stream) const
{
    const Stream& place = _Streams.at(stream);
    return StreamFacts{.At = place.At, .Stored = place.Stored, .Kind = place.Kind};
}

bool MCFastFile::HoldStream(uint32_t stream, std::vector<uint8_t>&& stored) const
{
    if (!_Version2 || stream >= _Streams.size() || stored.size() != _Streams[stream].Stored)
    {
        return false;
    }

    KeepRun(_Serial, _Streams[stream].At, std::move(stored));
    return true;
}

MCFileEntry MCFastFile::EntryAt(int32_t index) const
{
    if (!_Version2)
    {
        return _Entries.at(static_cast<size_t>(index));
    }

    // Its stream's place, so a read of neighbouring entries reads the stream once, and its own bytes.
    const Placed& placed = _Placed.at(static_cast<size_t>(index));
    const Stream& stream = _Streams.at(placed.Stream);
    return MCFileEntry{.Offset = static_cast<int32_t>(stream.At),
                       .Size = static_cast<int32_t>(stream.Stored),
                       .RealSize = static_cast<int32_t>(placed.Bytes),
                       .Name = {}};
}

uint32_t MCFastFile::HoldPieceCount() const
{
    return _Version2 ? static_cast<uint32_t>(_Streams.size()) : static_cast<uint32_t>(_ByOffset.size());
}

MCFastFile::HoldPiece MCFastFile::HoldPieceAt(uint32_t piece) const
{
    if (_Version2)
    {
        const Stream& stream = _Streams.at(piece);
        return HoldPiece{.At = stream.At,
                         .Stored = stream.Stored,
                         .Kept = stream.Kind == StreamKind::Packet || stream.Kind == StreamKind::Heads};
    }

    const MCFileEntry& entry = _Entries.at(static_cast<size_t>(_ByOffset.at(piece)));
    return HoldPiece{.At = static_cast<uint64_t>(entry.Offset),
                     .Stored = static_cast<uint32_t>(std::max(entry.Size, 0)),
                     .Kept = true};
}

bool MCFastFile::HoldPieces(uint32_t first, uint32_t end, uint64_t start, std::vector<uint8_t>&& run) const
{
    if (first >= end || end > HoldPieceCount())
    {
        return false;
    }

    // Every piece within the run's bytes.
    for (uint32_t piece = first; piece < end; ++piece)
    {
        const HoldPiece facts = HoldPieceAt(piece);

        if (facts.At < start || facts.At + facts.Stored > start + run.size())
        {
            return false;
        }
    }

    KeepRun(_Serial, start, std::move(run));
    return true;
}

std::optional<MCFastFile::Held> MCFastFile::HeldOf(int32_t index) const
{
    // A version 2 entry's Offset and Size are its stream's (ReadVersion2).
    const MCFileEntry entry = EntryAt(index);
    HeldStreams& held = TheHeldStreams();
    std::scoped_lock lock(held.Lock);
    const auto runs = held.Runs.find(_Serial);

    if (runs == held.Runs.end())
    {
        return std::nullopt;
    }

    // The run starting at or before the entry, if the entry lies whole in it.
    const auto offset = static_cast<uint64_t>(entry.Offset);
    const auto size = static_cast<uint64_t>(std::max(entry.Size, 0));
    auto run = runs->second.upper_bound(offset);

    if (run == runs->second.begin())
    {
        return std::nullopt;
    }

    --run;
    const std::vector<uint8_t>& bytes = *run->second.Bytes;

    if (offset + size > run->first + bytes.size())
    {
        return std::nullopt;
    }

    run->second.LastUse = ++held.Uses;
    return Held{run->second.Bytes, std::span<const uint8_t>(bytes).subspan(offset - run->first, size)};
}

size_t MCFastFile::GiveBackHeld(size_t bytes)
{
    // Freed outside the lock.
    std::vector<std::shared_ptr<const std::vector<uint8_t>>> going;
    HeldStreams& held = TheHeldStreams();
    std::scoped_lock lock(held.Lock);
    // Every run's last use and place, the least recently used first.
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> runs;

    for (const auto& [serial, archive] : held.Runs)
    {
        for (const auto& [start, run] : archive)
        {
            runs.emplace_back(run.LastUse, serial, start);
        }
    }

    std::ranges::sort(runs);
    size_t freed = 0;

    for (const auto& [use, serial, start] : runs)
    {
        if (freed >= bytes)
        {
            break;
        }

        HeldRuns& archive = held.Runs.at(serial);
        const auto run = archive.find(start);
        freed += run->second.Bytes->size();
        going.push_back(std::move(run->second.Bytes));
        archive.erase(run);

        if (archive.empty())
        {
            held.Runs.erase(serial);
        }
    }

    held.Bytes -= freed;
    held.Ledger.SetBytes(held.Bytes);
    return freed;
}

void MCFastFile::LetGoHeld()
{
    // Freed outside the lock: a mission's hold is up to a couple of GB in thousands of streams.
    std::unordered_map<uint64_t, HeldRuns> going;
    HeldStreams& held = TheHeldStreams();

    {
        std::scoped_lock lock(held.Lock);
        going.swap(held.Runs);
        held.Bytes = 0;
        held.Ledger.SetBytes(0);
    }
}

void MCFastFile::KeepStreamsUpTo(size_t bytes)
{
    KeptStreams& kept = TheKeptStreams();
    std::scoped_lock lock(kept.Lock);
    kept.Most = bytes;
    TrimKept(kept);
}

size_t MCFastFile::KeptStreamsUpTo()
{
    KeptStreams& kept = TheKeptStreams();
    std::scoped_lock lock(kept.Lock);
    return kept.Most;
}

size_t MCFastFile::KeptStreamBytes()
{
    KeptStreams& kept = TheKeptStreams();
    std::scoped_lock lock(kept.Lock);
    return kept.Bytes;
}

size_t MCFastFile::HeldBytes()
{
    HeldStreams& held = TheHeldStreams();
    std::scoped_lock lock(held.Lock);
    return held.Bytes;
}

void MCFastFile::CountTables()
{
    const auto bytesOf = [](const auto& vector)
    {
        using Value = typename std::remove_cvref_t<decltype(vector)>::value_type;
        return std::pair{vector.size() * sizeof(Value), vector.capacity() * sizeof(Value)};
    };

    size_t payload = 0;
    size_t allocated = 0;
    const auto add = [&](std::pair<size_t, size_t> bytes)
    {
        payload += bytes.first;
        allocated += bytes.second;
    };

    add(bytesOf(_Entries));
    add(bytesOf(_Streams));
    add(bytesOf(_EntryKeys));
    add(bytesOf(_Placed));
    add(bytesOf(_FolderKeys));
    add(bytesOf(_FolderFacts));
    add(bytesOf(_Facts));
    add(bytesOf(_ByOffset));

    // The index by name (version 1's): a node a name (its key, its number, the link and the hash) and the buckets; a
    // name longer than the string's own room is allocated apart. The allocator's rounding isn't known.
    constexpr size_t NodeBytes = sizeof(std::pair<const std::string, int32_t>) + sizeof(void*) + sizeof(size_t);
    const size_t inlineRoom = std::string().capacity();
    size_t index = _Index.size() * NodeBytes + _Index.bucket_count() * sizeof(void*);

    for (const auto& [name, entry] : _Index)
    {
        index += name.capacity() > inlineRoom ? name.capacity() + 1 : 0;
    }

    _Ledger.Set(
        {.Payload = static_cast<int64_t>(payload + index), .Allocated = static_cast<int64_t>(allocated + index)});
}

std::span<const uint8_t> MCFastFile::EntryIn(int32_t index, const std::vector<uint8_t>& stream) const
{
    if (!_Version2)
    {
        return stream;
    }

    const Placed& placed = _Placed.at(static_cast<size_t>(index));
    return std::span<const uint8_t>(stream).subspan(placed.At, placed.Bytes);
}

std::optional<int32_t> MCFastFile::Find(std::string_view gamePath) const
{
    if (_Version2)
    {
        const std::optional<size_t> found = KeyIndex(_EntryKeys, PathKey(gamePath));
        return found.has_value() ? std::optional<int32_t>(static_cast<int32_t>(*found)) : std::nullopt;
    }

    if (const auto found = _Index.find(IndexKey(gamePath)); found != _Index.end())
    {
        return found->second;
    }

    return std::nullopt;
}

std::optional<MCDiskFile> MCFastFile::OpenAnother() const
{
    // A file changed since (another written in its place) would be read by the old directory's offsets.
    std::error_code error;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(_Path, error);

    if (error || written != _Written)
    {
        return std::nullopt;
    }

    std::optional<MCDiskFile> file = MCDiskFile::Open(_Path, MCDiskFileMode::Read, MCDiskFileReads::Direct);

    if (file.has_value() && file->Length() != _Length)
    {
        file.reset();
    }

    return file;
}

MCOptionalRef<MCDiskFile> MCFastFile::File()
{
    if (!_Handle.has_value())
    {
        // Released: opened again only if it is the file the directory was read from.
        _Handle = OpenAnother();
    }

    return _Handle.has_value() ? MCOptionalRef<MCDiskFile>(*_Handle) : std::nullopt;
}

std::vector<uint8_t> MCFastFile::ReadStored(int32_t index)
{
    const MCFileEntry entry = EntryAt(index);

    // Port-only: bytes held (HoldPieces) are taken from memory, the file not opened again.
    if (const std::optional<Held> held = HeldOf(index); held.has_value())
    {
        return std::vector<uint8_t>(held->Stored.begin(), held->Stored.end());
    }

    const MCOptionalRef<MCDiskFile> opened = File();
    return opened ? StoredBytes(entry, *opened) : std::vector<uint8_t>();
}

std::vector<uint8_t> MCFastFile::ReadStored(int32_t index, MCDiskFile& file) const
{
    if (const std::optional<Held> held = HeldOf(index); held.has_value())
    {
        return std::vector<uint8_t>(held->Stored.begin(), held->Stored.end());
    }

    return StoredBytes(EntryAt(index), file);
}

int32_t MCFastFile::ReadEntry(int32_t index, std::span<uint8_t> data)
{
    const MCFileEntry entry = EntryAt(index);
    const MCOptionalRef<MCDiskFile> opened = File();

    if (!opened)
    {
        return 0;
    }

    MCDiskFile& file = *opened;
    data = data.first(std::min(data.size(), static_cast<size_t>(std::max(entry.RealSize, 0))));
    const auto offset = static_cast<uint32_t>(entry.Offset);
    // Port-only (lib/MCFirstUse.h): the entry a read or unpack below is for.
    const auto name = [&entry] { return std::string(entry.GetName()); };
    const MCFirstUseContext context(name);

    if (entry.Size == entry.RealSize)
    {
        return static_cast<int32_t>(file.ReadAt(offset, data));
    }

    // Packed: read the whole stored entry and unpack it (no more than the archive holds past the entry's start).
    const uint32_t stored = file.Length() - std::min(file.Length(), offset);
    std::vector<uint8_t> packed(std::min<size_t>(static_cast<size_t>(std::max(entry.Size, 0)), stored));
    packed.resize(file.ReadAt(offset, packed));
    const int32_t unpacked = LZDecomp(data, packed);
    return unpacked == entry.RealSize ? unpacked : 0;
}

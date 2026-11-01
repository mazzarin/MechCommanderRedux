#include "stdafx.h"
#include "platform/MCSmacker.h"
#include "platform/MCAudio.h"
#include "platform/MCFileSystem.h"
#include "platform/MCNeverDestroyed.h"

// Everything here is written from the public description of the format.

namespace
{
    constexpr size_t HeaderSize = 0x68;

    /// <summary>The little-endian 32-bit value at <paramref name="at"/>.</summary>
    uint32_t ReadU32(std::span<const uint8_t> bytes, size_t at)
    {
        return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8) |
               (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
    }

    /// <summary>The deepest an 8-bit tree may go (its codes are at most this long).</summary>
    constexpr int MaxSmallDepth = 32;
    /// <summary>The deepest a 16-bit tree may go.</summary>
    constexpr int MaxBigDepth = 500;
    /// <summary>A sanity bound on a tree's node count, far above anything a real file needs.</summary>
    constexpr size_t MaxNodes = size_t{1} << 21;
    /// <summary>Bits resolved by one table lookup when decoding.</summary>
    constexpr int TableBits = 12;
}

/// <summary>
/// Reads Smacker's bitstreams: bits are taken from each byte starting at its least significant bit, and a multi-bit
/// field has its first bit read as its least significant. Past the end it reads zeros and remembers the overrun.
/// </summary>
class MCSmacker::MCBitReader
{
public:
    explicit MCBitReader(std::span<const uint8_t> data) : _Data(data), _Bits(data.size() * 8) {}

    /// <summary>The next <paramref name="count"/> (at most 24) bits without consuming them.</summary>
    uint32_t Peek(int count) const
    {
        const size_t byte = _Position >> 3;
        uint32_t window = 0;

        for (size_t i = 0; i < 4; ++i)
        {
            if (byte + i < _Data.size())
            {
                window |= static_cast<uint32_t>(_Data[byte + i]) << (8 * i);
            }
        }

        window >>= (_Position & 7);
        return window & ((1u << count) - 1u);
    }

    void Skip(int count) { _Position += static_cast<size_t>(count); }

    uint32_t Read(int count)
    {
        const uint32_t value = Peek(count);
        Skip(count);
        return value;
    }

    uint32_t ReadBit() { return Read(1); }

    /// <summary>Whether more bits were read than the data holds.</summary>
    bool Overrun() const { return _Position > _Bits; }

    size_t Position() const { return _Position; }

private:
    std::span<const uint8_t> _Data;
    size_t _Bits;
    size_t _Position = 0;
};

/// <summary>
/// A Smacker Huffman tree: an 8-bit one (leaves are bytes) or a 16-bit one (leaves are built from two 8-bit trees,
/// and three escape values make a leaf read the tree's three-entry cache instead of a constant).
/// </summary>
class MCSmacker::MCHuffman
{
public:
    /// <summary>A tree that decodes 0 without reading any bit (a tree the file marks absent).</summary>
    static MCHuffman Empty()
    {
        MCHuffman tree;
        tree._Nodes.push_back({{-1, -1}, 0});
        tree._Values.push_back(0);
        tree._CacheSlot.push_back(-1);
        tree.BuildTable();
        return tree;
    }

    /// <summary>Reads an 8-bit tree: its presence bit, the tree, and the closing bit.</summary>
    static std::expected<MCHuffman, std::string> ReadSmall(MCBitReader& reader)
    {
        if (reader.ReadBit() == 0)
        {
            return Empty();
        }

        MCHuffman tree;
        auto root = tree.ReadSmallNode(reader, 0);

        if (!root)
        {
            return std::unexpected(root.error());
        }

        reader.Skip(1);

        if (reader.Overrun())
        {
            return std::unexpected(std::string("8-bit tree runs past its data"));
        }

        tree.BuildTable();
        return tree;
    }

    /// <summary>
    /// Reads a 16-bit tree (after its presence bit): the low-byte and high-byte trees, the three escapes, the tree
    /// and its closing bit.
    /// </summary>
    static std::expected<MCHuffman, std::string> ReadBig(MCBitReader& reader)
    {
        auto low = ReadSmall(reader);

        if (!low)
        {
            return std::unexpected("low-byte tree: " + low.error());
        }

        auto high = ReadSmall(reader);

        if (!high)
        {
            return std::unexpected("high-byte tree: " + high.error());
        }

        MCHuffman tree;
        tree._Big = true;

        for (uint16_t& escape : tree._Escapes)
        {
            escape = static_cast<uint16_t>(reader.Read(16));
        }

        auto root = tree.ReadBigNode(reader, *low, *high, 0);

        if (!root)
        {
            return std::unexpected(root.error());
        }

        reader.Skip(1);

        if (reader.Overrun())
        {
            return std::unexpected(std::string("16-bit tree runs past its data"));
        }

        tree.BuildTable();
        return tree;
    }

    /// <summary>Clears the cache, as every video frame starts.</summary>
    void ResetCache() { _Cache = {0, 0, 0}; }

    /// <summary>Decodes one value, updating the cache of a 16-bit tree.</summary>
    uint32_t Decode(MCBitReader& reader)
    {
        int32_t leaf;
        const Node& root = _Nodes[0];

        if (root.Leaf >= 0)
        {
            leaf = root.Leaf;
        }
        else
        {
            const TableEntry& entry = _Table[reader.Peek(_TableBits)];

            if (entry.IsLeaf)
            {
                reader.Skip(entry.Length);
                leaf = entry.Target;
            }
            else
            {
                reader.Skip(_TableBits);
                int32_t node = entry.Target;

                while (_Nodes[static_cast<size_t>(node)].Leaf < 0)
                {
                    node = _Nodes[static_cast<size_t>(node)].Child[reader.ReadBit()];
                }

                leaf = _Nodes[static_cast<size_t>(node)].Leaf;
            }
        }

        const int8_t slot = _CacheSlot[static_cast<size_t>(leaf)];
        const uint16_t value = slot >= 0 ? _Cache[static_cast<size_t>(slot)] : _Values[static_cast<size_t>(leaf)];

        if (_Big && _Cache[0] != value)
        {
            _Cache[2] = _Cache[1];
            _Cache[1] = _Cache[0];
            _Cache[0] = value;
        }

        return value;
    }

private:
    struct Node
    {
        /// <summary>The nodes a 0 bit and a 1 bit lead to, for an inner node.</summary>
        int32_t Child[2];
        /// <summary>The leaf index for a leaf, -1 for an inner node.</summary>
        int32_t Leaf;
    };

    struct TableEntry
    {
        /// <summary>A leaf index, or the inner node to go on walking from.</summary>
        int32_t Target = 0;
        uint8_t Length = 0;
        bool IsLeaf = false;
    };

    int32_t AddLeaf(uint16_t value, int8_t slot)
    {
        const int32_t leaf = static_cast<int32_t>(_Values.size());
        _Values.push_back(value);
        _CacheSlot.push_back(slot);
        _Nodes.push_back({{-1, -1}, leaf});
        return static_cast<int32_t>(_Nodes.size() - 1);
    }

    std::expected<int32_t, std::string> ReadSmallNode(MCBitReader& reader, int depth)
    {
        if (depth > MaxSmallDepth)
        {
            return std::unexpected(std::string("8-bit tree is too deep"));
        }

        if (_Nodes.size() > MaxNodes || reader.Overrun())
        {
            return std::unexpected(std::string("8-bit tree runs past its data"));
        }

        if (reader.ReadBit() == 0)
        {
            return AddLeaf(static_cast<uint16_t>(reader.Read(8)), -1);
        }

        const int32_t node = static_cast<int32_t>(_Nodes.size());
        _Nodes.push_back({{-1, -1}, -1});
        auto zero = ReadSmallNode(reader, depth + 1);

        if (!zero)
        {
            return zero;
        }

        auto one = ReadSmallNode(reader, depth + 1);

        if (!one)
        {
            return one;
        }

        _Nodes[static_cast<size_t>(node)].Child[0] = *zero;
        _Nodes[static_cast<size_t>(node)].Child[1] = *one;
        return node;
    }

    std::expected<int32_t, std::string> ReadBigNode(MCBitReader& reader, MCHuffman& low, MCHuffman& high, int depth)
    {
        if (depth > MaxBigDepth)
        {
            return std::unexpected(std::string("16-bit tree is too deep"));
        }

        if (_Nodes.size() > MaxNodes || reader.Overrun())
        {
            return std::unexpected(std::string("16-bit tree runs past its data"));
        }

        if (reader.ReadBit() == 0)
        {
            const uint32_t lo = low.Decode(reader);
            const uint32_t hi = high.Decode(reader);
            const uint16_t value = static_cast<uint16_t>(lo | (hi << 8));
            int8_t slot = -1;

            if (value == _Escapes[0])
            {
                slot = 0;
            }
            else if (value == _Escapes[1])
            {
                slot = 1;
            }
            else if (value == _Escapes[2])
            {
                slot = 2;
            }

            return AddLeaf(slot >= 0 ? uint16_t{0} : value, slot);
        }

        const int32_t node = static_cast<int32_t>(_Nodes.size());
        _Nodes.push_back({{-1, -1}, -1});
        auto zero = ReadBigNode(reader, low, high, depth + 1);

        if (!zero)
        {
            return zero;
        }

        auto one = ReadBigNode(reader, low, high, depth + 1);

        if (!one)
        {
            return one;
        }

        _Nodes[static_cast<size_t>(node)].Child[0] = *zero;
        _Nodes[static_cast<size_t>(node)].Child[1] = *one;
        return node;
    }

    /// <summary>Fills the lookup table that resolves the first <see cref="TableBits"/> bits of a code at once.</summary>
    void BuildTable()
    {
        if (_Nodes[0].Leaf >= 0)
        {
            _TableBits = 0;
            _Table.assign(1, TableEntry{_Nodes[0].Leaf, 0, true});
            return;
        }

        _TableBits = TableBits;
        _Table.assign(size_t{1} << _TableBits, TableEntry{});
        Fill(0, 0, 0);
    }

    void Fill(int32_t nodeIndex, uint32_t code, int depth)
    {
        const Node& node = _Nodes[static_cast<size_t>(nodeIndex)];

        if (node.Leaf >= 0)
        {
            for (size_t i = code; i < _Table.size(); i += size_t{1} << depth)
            {
                _Table[i] = {node.Leaf, static_cast<uint8_t>(depth), true};
            }

            return;
        }

        if (depth == _TableBits)
        {
            _Table[code] = {nodeIndex, static_cast<uint8_t>(depth), false};
            return;
        }

        Fill(node.Child[0], code, depth + 1);
        Fill(node.Child[1], code | (1u << depth), depth + 1);
    }

    std::vector<Node> _Nodes;
    std::vector<uint16_t> _Values;
    std::vector<int8_t> _CacheSlot;
    std::vector<TableEntry> _Table;
    int _TableBits = 0;
    bool _Big = false;
    std::array<uint16_t, 3> _Escapes{};
    std::array<uint16_t, 3> _Cache{};
};

// ---------------------------------------------------------------------------------------------------------------
// MCSmacker

MCSmacker::MCSmacker(Key)
{
}

MCSmacker::~MCSmacker() = default;

std::expected<std::unique_ptr<MCSmacker>, std::string> MCSmacker::Open(const std::filesystem::path& path)
{
    auto smacker = std::make_unique<MCSmacker>(Key{});
    smacker->_File.open(path, std::ios::binary);

    if (!smacker->_File)
    {
        return std::unexpected(std::format("can't open {}", MCFileSystem::ToUtf8(path)));
    }

    smacker->_File.seekg(0, std::ios::end);
    smacker->_FileSize = static_cast<uint64_t>(smacker->_File.tellg());
    smacker->_File.seekg(0);

    if (auto result = smacker->ReadHeader(); !result)
    {
        return std::unexpected(std::format("{}: {}", MCFileSystem::ToUtf8(path.filename()), result.error()));
    }

    return smacker;
}

std::expected<std::unique_ptr<MCSmacker>, std::string> MCSmacker::OpenMemory(std::vector<uint8_t> data)
{
    auto smacker = std::make_unique<MCSmacker>(Key{});
    smacker->_Memory = std::move(data);
    smacker->_InMemory = true;
    smacker->_FileSize = smacker->_Memory.size();

    if (auto result = smacker->ReadHeader(); !result)
    {
        return std::unexpected(result.error());
    }

    return smacker;
}

bool MCSmacker::ReadAt(uint64_t offset, std::span<uint8_t> dest)
{
    const size_t count = dest.size();

    if (offset > _FileSize || count > _FileSize - offset)
    {
        return false;
    }

    if (_InMemory)
    {
        std::ranges::copy(std::span(_Memory).subspan(static_cast<size_t>(offset), count), dest.begin());
        return true;
    }

    _File.clear();
    _File.seekg(static_cast<std::streamoff>(offset));
    _File.read(reinterpret_cast<char*>(dest.data()), static_cast<std::streamsize>(count));
    return static_cast<size_t>(_File.gcount()) == count;
}

std::expected<void, std::string> MCSmacker::ReadHeader()
{
    std::array<uint8_t, HeaderSize> header{};

    if (!ReadAt(0, header))
    {
        return std::unexpected(std::string("too short for a Smacker header"));
    }

    if (std::memcmp(header.data(), "SMK2", 4) == 0)
    {
        _Version = 2;
    }
    else if (std::memcmp(header.data(), "SMK4", 4) == 0)
    {
        _Version = 4;
    }
    else
    {
        return std::unexpected(std::string("not a Smacker file (no SMK2/SMK4 signature)"));
    }

    _Width = ReadU32(header, 0x04);
    _Height = ReadU32(header, 0x08);
    _Frames = ReadU32(header, 0x0c);
    const int32_t rate = static_cast<int32_t>(ReadU32(header, 0x10));
    _Flags = ReadU32(header, 0x14);

    if (_Width == 0 || _Height == 0 || _Width > 4096 || _Height > 4096)
    {
        return std::unexpected(std::format("bad size {}x{}", _Width, _Height));
    }

    if (_Frames == 0 || _Frames > 1000000)
    {
        return std::unexpected(std::format("bad frame count {}", _Frames));
    }

    if (rate > 0)
    {
        _MicrosecondsPerFrame = static_cast<uint32_t>(rate) * 1000u;
    }
    else if (rate < 0)
    {
        _MicrosecondsPerFrame = static_cast<uint32_t>(-static_cast<int64_t>(rate)) * 10u;
    }
    else
    {
        _MicrosecondsPerFrame = 100000;
    }

    if (_MicrosecondsPerFrame == 0)
    {
        _MicrosecondsPerFrame = 100000;
    }

    for (int t = 0; t < MaxTracks; ++t)
    {
        const uint32_t word = ReadU32(header, 0x48 + static_cast<size_t>(t) * 4);
        MCTrackInfo& track = _Tracks[static_cast<size_t>(t)];
        track.Compressed = (word & 0x80000000u) != 0;
        track.Present = (word & 0x40000000u) != 0;
        track.Is16Bit = (word & 0x20000000u) != 0;
        track.Stereo = (word & 0x10000000u) != 0;
        track.Codec = (word >> 26) & 3u;
        track.Rate = word & 0x00ffffffu;
        track.LargestChunk = ReadU32(header, 0x18 + static_cast<size_t>(t) * 4);
    }

    const uint32_t treesSize = ReadU32(header, 0x34);

    _StoredFrames = _Frames + ((_Flags & FlagRingFrame) ? 1u : 0u);
    std::vector<uint8_t> tables(static_cast<size_t>(_StoredFrames) * 5);

    if (!ReadAt(HeaderSize, tables))
    {
        return std::unexpected(std::string("the frame tables are cut short"));
    }

    _FrameSizes.resize(_StoredFrames);
    _FrameTypes.resize(_StoredFrames);
    _FrameOffsets.resize(_StoredFrames);

    for (uint32_t i = 0; i < _StoredFrames; ++i)
    {
        _FrameSizes[i] = ReadU32(tables, static_cast<size_t>(i) * 4);
        _FrameTypes[i] = tables[static_cast<size_t>(_StoredFrames) * 4 + i];
    }

    const uint64_t treesOffset = HeaderSize + tables.size();

    // Issue #25: the trees must lie in the file before their size is allocated.
    if (treesOffset + treesSize > _FileSize)
    {
        return std::unexpected(
            std::format("the Huffman trees need {} bytes, the file has {}", treesOffset + treesSize, _FileSize));
    }

    std::vector<uint8_t> trees(treesSize);

    if (!ReadAt(treesOffset, trees))
    {
        return std::unexpected(std::string("the Huffman trees are cut short"));
    }

    MCBitReader reader(trees);
    constexpr std::array<std::string_view, 4> names = {"MMap", "MClr", "Full", "Type"};
    const std::array<std::reference_wrapper<std::unique_ptr<MCHuffman>>, 4> slots = {std::ref(_MMap), std::ref(_MClr),
                                                                                     std::ref(_Full), std::ref(_Type)};

    for (int i = 0; i < 4; ++i)
    {
        if (reader.ReadBit() == 0)
        {
            slots[i].get() = std::make_unique<MCHuffman>(MCHuffman::Empty());
            continue;
        }

        auto tree = MCHuffman::ReadBig(reader);

        if (!tree)
        {
            return std::unexpected(std::format("{} tree: {}", names[i], tree.error()));
        }

        slots[i].get() = std::make_unique<MCHuffman>(std::move(*tree));
    }

    uint64_t offset = treesOffset + treesSize;

    for (uint32_t i = 0; i < _StoredFrames; ++i)
    {
        _FrameOffsets[i] = offset;
        offset += _FrameSizes[i] & ~3u;
    }

    if (offset > _FileSize)
    {
        return std::unexpected(std::format("the frames need {} bytes, the file has {}", offset, _FileSize));
    }

    Rewind();
    return {};
}

void MCSmacker::Rewind()
{
    _Current = -1;
    _Next = 0;
    _Pixels.assign(static_cast<size_t>(_Width) * _Height, 0);
    _Palette.fill(0);
    _PaletteChanged = false;

    for (auto& audio : _Audio)
    {
        audio.clear();
    }
}

bool MCSmacker::IsKeyFrame() const
{
    if (_Current < 0)
    {
        return false;
    }

    const uint32_t stored = _Next == 0 ? _StoredFrames - 1 : _Next - 1;
    return (_FrameSizes[stored] & 1u) != 0;
}

std::expected<void, std::string> MCSmacker::SeekFrame(uint32_t frame)
{
    if (frame >= _Frames)
    {
        return std::unexpected(std::format("frame {} of {}", frame, _Frames));
    }

    if (_Current < 0 || static_cast<uint32_t>(_Current) >= frame)
    {
        Rewind();
    }
    while (_Current != static_cast<int32_t>(frame))
    {
        if (auto result = DecodeNextFrame(); !result)
        {
            return result;
        }
    }

    return {};
}

std::expected<void, std::string> MCSmacker::DecodeNextFrame()
{
    if (_Next >= _StoredFrames)
    {
        _Next = (_Flags & FlagRingFrame) ? 1u : 0u;
    }

    const uint32_t index = _Next++;
    _Current = static_cast<int32_t>(index % _Frames);
    _PaletteChanged = false;

    for (auto& audio : _Audio)
    {
        audio.clear();
    }

    const uint32_t size = _FrameSizes[index] & ~3u;
    _FrameData.resize(size);

    if (!ReadAt(_FrameOffsets[index], _FrameData))
    {
        return std::unexpected(std::format("frame {}: can't read {} bytes", index, size));
    }

    const uint8_t type = _FrameTypes[index];
    size_t position = 0;

    if (type & 1)
    {
        if (size < 1)
        {
            return std::unexpected(std::format("frame {}: no room for the palette", index));
        }

        const size_t paletteSize = static_cast<size_t>(_FrameData[0]) * 4;

        if (paletteSize == 0 || paletteSize > size)
        {
            return std::unexpected(std::format("frame {}: bad palette size {}", index, paletteSize));
        }

        if (auto result = DecodePalette(std::span<const uint8_t>(_FrameData).subspan(0, paletteSize)); !result)
        {
            return std::unexpected(std::format("frame {}: {}", index, result.error()));
        }

        position = paletteSize;
    }

    for (int t = 0; t < MaxTracks; ++t)
    {
        if ((type & (2u << t)) == 0)
        {
            continue;
        }

        if (position + 4 > size)
        {
            return std::unexpected(std::format("frame {}: audio track {} runs past the frame", index, t));
        }

        const uint32_t length = ReadU32(_FrameData, position);

        if (length < 4 || length > size - position)
        {
            return std::unexpected(std::format("frame {}: bad audio chunk length {}", index, length));
        }

        if (auto result = DecodeAudio(t, std::span<const uint8_t>(_FrameData).subspan(position + 4, length - 4));
            !result)
        {
            return std::unexpected(std::format("frame {}, track {}: {}", index, t, result.error()));
        }

        position += length;
    }

    if (auto result = DecodeVideo(std::span<const uint8_t>(_FrameData).subspan(position)); !result)
    {
        return std::unexpected(std::format("frame {}: {}", index, result.error()));
    }

    return {};
}

std::expected<void, std::string> MCSmacker::DecodePalette(std::span<const uint8_t> chunk)
{
    const std::array<uint8_t, 768> previous = _Palette;
    size_t in = 1;
    int entry = 0;

    while (entry < 256)
    {
        if (in >= chunk.size())
        {
            return std::unexpected(std::string("palette record runs past its chunk"));
        }

        const uint8_t code = chunk[in++];

        if (code & 0x80)
        {
            // Keep the next (code & 0x7f) + 1 entries as they are.
            entry += (code & 0x7f) + 1;
        }
        else if (code & 0x40)
        {
            // Copy (code & 0x3f) + 1 entries of the previous palette, from the index the next byte gives.
            if (in >= chunk.size())
            {
                return std::unexpected(std::string("palette copy runs past its chunk"));
            }

            int from = chunk[in++];
            int count = (code & 0x3f) + 1;

            if (from + count > 256)
            {
                return std::unexpected(std::string("palette copy reads past entry 255"));
            }
            while (count-- > 0 && entry < 256)
            {
                std::memcpy(&_Palette[static_cast<size_t>(entry) * 3], &previous[static_cast<size_t>(from) * 3], 3);
                ++entry;
                ++from;
            }
        }
        else
        {
            // One new colour: this byte is red, the next two green and blue, 6 bits each.
            if (in + 2 > chunk.size())
            {
                return std::unexpected(std::string("palette colour runs past its chunk"));
            }

            _Palette[static_cast<size_t>(entry) * 3 + 0] = ScalePaletteComponent(code);
            _Palette[static_cast<size_t>(entry) * 3 + 1] = ScalePaletteComponent(chunk[in++]);
            _Palette[static_cast<size_t>(entry) * 3 + 2] = ScalePaletteComponent(chunk[in++]);
            ++entry;
        }
    }

    _PaletteChanged = true;
    return {};
}

std::expected<void, std::string> MCSmacker::DecodeAudio(int track, std::span<const uint8_t> chunk)
{
    const MCTrackInfo& info = _Tracks[static_cast<size_t>(track)];
    std::vector<uint8_t>& out = _Audio[static_cast<size_t>(track)];

    if (!info.Compressed)
    {
        out.assign(chunk.begin(), chunk.end());
        return {};
    }

    if (info.Codec != 0)
    {
        return {}; // Bink audio: not in MechCommander's files, left silent.
    }

    if (chunk.size() < 4)
    {
        return std::unexpected(std::string("compressed chunk has no size"));
    }

    const uint32_t unpacked = ReadU32(chunk, 0);

    if (unpacked > (16u << 20))
    {
        return std::unexpected(std::format("unpacked size {} is absurd", unpacked));
    }

    MCBitReader reader(chunk.subspan(4));

    if (reader.ReadBit() == 0)
    {
        return {}; // The chunk says it holds no data.
    }

    const bool stereo = reader.ReadBit() != 0;
    const bool is16 = reader.ReadBit() != 0;
    const int channels = stereo ? 2 : 1;

    std::vector<MCHuffman> trees;
    const int treeCount = 1 << ((stereo ? 1 : 0) + (is16 ? 1 : 0));

    for (int i = 0; i < treeCount; ++i)
    {
        auto tree = MCHuffman::ReadSmall(reader);

        if (!tree)
        {
            return std::unexpected(std::format("tree {}: {}", i, tree.error()));
        }

        trees.push_back(std::move(*tree));
    }

    out.resize(unpacked);

    if (is16)
    {
        const uint32_t samples = unpacked / 2;

        if (samples < static_cast<uint32_t>(channels))
        {
            return std::unexpected(std::string("too short for the starting samples"));
        }

        std::array<int16_t, 2> predictor{};

        // The starting values come right channel first, each high byte first.
        for (int c = channels - 1; c >= 0; --c)
        {
            const uint32_t high = reader.Read(8);
            const uint32_t low = reader.Read(8);
            predictor[static_cast<size_t>(c)] = static_cast<int16_t>((high << 8) | low);
        }

        auto put = [&](uint32_t index, int16_t value)
        {
            out[index * 2] = static_cast<uint8_t>(value & 0xff);
            out[index * 2 + 1] = static_cast<uint8_t>((static_cast<uint16_t>(value) >> 8) & 0xff);
        };

        for (int c = 0; c < channels; ++c)
        {
            put(static_cast<uint32_t>(c), predictor[static_cast<size_t>(c)]);
        }

        for (uint32_t i = static_cast<uint32_t>(channels); i < samples; ++i)
        {
            const size_t c = stereo ? (i & 1u) : 0u;
            const uint32_t low = trees[c * 2].Decode(reader);
            const uint32_t high = trees[c * 2 + 1].Decode(reader);
            const int16_t delta = static_cast<int16_t>(low | (high << 8));
            predictor[c] = static_cast<int16_t>(predictor[c] + delta);
            put(i, predictor[c]);
        }
    }
    else
    {
        if (unpacked < static_cast<uint32_t>(channels))
        {
            return std::unexpected(std::string("too short for the starting samples"));
        }

        std::array<uint8_t, 2> predictor{};

        for (int c = channels - 1; c >= 0; --c)
        {
            predictor[static_cast<size_t>(c)] = static_cast<uint8_t>(reader.Read(8));
        }

        for (int c = 0; c < channels; ++c)
        {
            out[static_cast<size_t>(c)] = predictor[static_cast<size_t>(c)];
        }

        for (uint32_t i = static_cast<uint32_t>(channels); i < unpacked; ++i)
        {
            const size_t c = stereo ? (i & 1u) : 0u;
            const int8_t delta = static_cast<int8_t>(trees[c].Decode(reader));
            predictor[c] = static_cast<uint8_t>(predictor[c] + delta);
            out[i] = predictor[c];
        }
    }

    if (reader.Overrun())
    {
        return std::unexpected(std::string("the samples run past the chunk"));
    }

    return {};
}

uint32_t MCSmacker::BlockRun(uint32_t index)
{
    index &= 0x3f;

    if (index < 59)
    {
        return index + 1;
    }

    return 128u << (index - 59);
}

std::expected<void, std::string> MCSmacker::DecodeVideo(std::span<const uint8_t> chunk)
{
    _MMap->ResetCache();
    _MClr->ResetCache();
    _Full->ResetCache();
    _Type->ResetCache();

    MCBitReader reader(chunk);
    const uint32_t blocksWide = _Width / 4;
    const uint32_t blocks = blocksWide * (_Height / 4);
    const size_t stride = _Width;
    auto blockAt = [&](uint32_t block) { return (block / blocksWide) * stride * 4 + (block % blocksWide) * 4; };

    uint32_t block = 0;

    while (block < blocks)
    {
        const uint32_t type = _Type->Decode(reader);
        uint32_t run = BlockRun(type >> 2);

        switch (type & 3)
        {
            case 0: // Mono: two colours and a 16-bit map choosing between them.
            {
                while (run-- > 0 && block < blocks)
                {
                    const uint32_t colours = _MClr->Decode(reader);
                    uint32_t map = _MMap->Decode(reader);
                    const uint8_t high = static_cast<uint8_t>(colours >> 8);
                    const uint8_t low = static_cast<uint8_t>(colours & 0xff);
                    size_t out = blockAt(block);

                    for (int row = 0; row < 4; ++row)
                    {
                        for (int column = 0; column < 4; ++column)
                        {
                            _Pixels[out + column] = (map & (1u << column)) ? high : low;
                        }

                        map >>= 4;
                        out += stride;
                    }

                    ++block;
                }
                break;
            }
            case 1: // Full: every pixel from the Full tree, two at a time.
            {
                int mode = 0;

                if (_Version == 4)
                {
                    if (reader.ReadBit())
                    {
                        mode = 1;
                    }
                    else if (reader.ReadBit())
                    {
                        mode = 2;
                    }
                }
                while (run-- > 0 && block < blocks)
                {
                    size_t out = blockAt(block);

                    if (mode == 0)
                    {
                        for (int row = 0; row < 4; ++row)
                        {
                            const uint32_t right = _Full->Decode(reader);
                            const uint32_t left = _Full->Decode(reader);
                            _Pixels[out + 2] = static_cast<uint8_t>(right & 0xff);
                            _Pixels[out + 3] = static_cast<uint8_t>(right >> 8);
                            _Pixels[out + 0] = static_cast<uint8_t>(left & 0xff);
                            _Pixels[out + 1] = static_cast<uint8_t>(left >> 8);
                            out += stride;
                        }
                    }
                    else if (mode == 1)
                    {
                        // Double: 2x2 pixels per value, two values per pair of rows.
                        for (int pair = 0; pair < 2; ++pair)
                        {
                            const uint32_t value = _Full->Decode(reader);
                            const uint8_t a = static_cast<uint8_t>(value & 0xff);
                            const uint8_t b = static_cast<uint8_t>(value >> 8);

                            for (int row = 0; row < 2; ++row)
                            {
                                _Pixels[out + 0] = a;
                                _Pixels[out + 1] = a;
                                _Pixels[out + 2] = b;
                                _Pixels[out + 3] = b;
                                out += stride;
                            }
                        }
                    }
                    else
                    {
                        // Half: each decoded row of four shows on two rows.
                        for (int pair = 0; pair < 2; ++pair)
                        {
                            const uint32_t right = _Full->Decode(reader);
                            const uint32_t left = _Full->Decode(reader);

                            for (int row = 0; row < 2; ++row)
                            {
                                _Pixels[out + 0] = static_cast<uint8_t>(left & 0xff);
                                _Pixels[out + 1] = static_cast<uint8_t>(left >> 8);
                                _Pixels[out + 2] = static_cast<uint8_t>(right & 0xff);
                                _Pixels[out + 3] = static_cast<uint8_t>(right >> 8);
                                out += stride;
                            }
                        }
                    }

                    ++block;
                }
                break;
            }

            case 2: // Void: the block keeps the previous frame's pixels.
                block = static_cast<uint32_t>(std::min<uint64_t>(blocks, static_cast<uint64_t>(block) + run));
                break;
            case 3: // Solid: one colour, the type code's high byte.
            {
                const uint8_t colour = static_cast<uint8_t>(type >> 8);

                while (run-- > 0 && block < blocks)
                {
                    size_t out = blockAt(block);

                    for (int row = 0; row < 4; ++row)
                    {
                        std::fill_n(_Pixels.begin() + static_cast<ptrdiff_t>(out), 4, colour);
                        out += stride;
                    }

                    ++block;
                }
                break;
            }
        }

        if (reader.Overrun())
        {
            return std::unexpected(std::format("video data ends at block {} of {}", block, blocks));
        }
    }

    return {};
}

void MCSmacker::CopyTo(std::span<uint8_t> dest, int pitch, int maxWidth, int maxHeight, std::span<const uint8_t> remap,
                       int gapColor) const
{
    if (dest.empty())
    {
        return;
    }

    const int width = std::min(Width(), maxWidth);

    if (width <= 0)
    {
        return;
    }

    const bool doubled = (_Flags & FlagYDoubled) != 0;
    const bool interlaced = !doubled && (_Flags & FlagYInterlaced) != 0;

    for (int y = 0; y < StoredHeight(); ++y)
    {
        const std::span<const uint8_t> source =
            std::span<const uint8_t>(_Pixels).subspan(static_cast<size_t>(y) * _Width, _Width);
        const int firstRow = (doubled || interlaced) ? y * 2 : y;
        const int rows = doubled ? 2 : 1;

        for (int r = 0; r < rows; ++r)
        {
            const int row = firstRow + r;

            if (row >= maxHeight)
            {
                return;
            }

            const std::span<uint8_t> target =
                dest.subspan(static_cast<size_t>(row) * pitch, static_cast<size_t>(width));

            if (!remap.empty())
            {
                for (int x = 0; x < width; ++x)
                {
                    target[x] = remap[source[x]];
                }
            }
            else
            {
                std::ranges::copy(source.first(static_cast<size_t>(width)), target.begin());
            }
        }

        if (interlaced && gapColor >= 0 && firstRow + 1 < maxHeight)
        {
            std::ranges::fill(dest.subspan(static_cast<size_t>(firstRow + 1) * pitch, static_cast<size_t>(width)),
                              static_cast<uint8_t>(gapColor));
        }
    }
}

void MCSmacker::BuildRemap(std::span<const uint8_t, 768> source, std::span<const uint8_t> target,
                           std::span<uint8_t, 256> remap)
{
    const int targetCount = static_cast<int>(std::clamp<size_t>(target.size() / 3, 1, 256));

    for (int i = 0; i < 256; ++i)
    {
        const int r = source[i * 3];
        const int g = source[i * 3 + 1];
        const int b = source[i * 3 + 2];
        int best = 0;
        int bestDistance = INT_MAX;

        for (int j = 0; j < targetCount; ++j)
        {
            const int dr = r - target[j * 3];
            const int dg = g - target[j * 3 + 1];
            const int db = b - target[j * 3 + 2];
            const int distance = dr * dr + dg * dg + db * db;

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = j;

                if (distance == 0)
                {
                    break;
                }
            }
        }

        remap[i] = static_cast<uint8_t>(best);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// MCSmackerPlayer

std::expected<std::unique_ptr<MCSmackerPlayer>, std::string> MCSmackerPlayer::Open(const std::filesystem::path& path,
                                                                                   std::shared_ptr<MCAudio> audio)
{
    auto smacker = MCSmacker::Open(path);

    if (!smacker)
    {
        return std::unexpected(smacker.error());
    }

    auto player = std::make_unique<MCSmackerPlayer>(Key{});
    player->_Smacker = std::move(*smacker);

    if (audio != nullptr)
    {
        for (int t = 0; t < MCSmacker::MaxTracks; ++t)
        {
            const MCSmacker::MCTrackInfo& track = player->_Smacker->Track(t);

            if (!track.Present || track.Codec != 0)
            {
                continue;
            }

            MCSoundFormat format;
            format.Rate = track.Rate;
            format.Channels = static_cast<uint16_t>(track.Channels());
            format.Bits = track.Is16Bit ? 16 : 8;

            if (auto stream = audio->CreateStream(format))
            {
                player->_Stream = std::move(*stream);
                player->_AudioTrack = t;
            }
            break;
        }
    }

    return player;
}

MCSmackerPlayer::~MCSmackerPlayer() = default;

void MCSmackerPlayer::CopyFrame(std::span<uint8_t> buffer, int left, int top, int pitch, int height) const
{
    const size_t first = static_cast<size_t>(top) * pitch + static_cast<size_t>(left);

    if (first >= buffer.size())
    {
        return;
    }

    _Smacker->CopyTo(buffer.subspan(first), pitch, pitch - left, height,
                     _RemapValid ? std::span<const uint8_t>(_Remap) : std::span<const uint8_t>(), 0);
}

void MCSmackerPlayer::ColorRemap(std::span<const uint8_t> palette)
{
    if (palette.size() < 3)
    {
        _RemapPalette.clear();
        _RemapValid = false;
        return;
    }

    _RemapPalette.assign(palette.begin(),
                         palette.begin() + static_cast<ptrdiff_t>(std::min<size_t>(palette.size() / 3, 256) * 3));
    MCSmacker::BuildRemap(_Smacker->Palette(), _RemapPalette, _Remap);
    _RemapValid = true;
}

namespace
{
    /// <summary>
    /// The movie clock, in microseconds: the game's performance counter, so that movies follow the tests' manual clock
    /// (MCManualClock) as the game does.
    /// </summary>
    uint64_t NowMicroseconds()
    {
        const auto counter = static_cast<uint64_t>(MCPort::PerformanceCounter());
        const auto frequency = static_cast<uint64_t>(MCPort::PerformanceFrequency());
        return counter / frequency * 1000000 + counter % frequency * 1000000 / frequency;
    }
}

std::expected<void, std::string> MCSmackerPlayer::DoFrame()
{
    if (!_Started)
    {
        _Started = true;
        _StartTicks = NowMicroseconds();
        _FramesDone = 0;
    }

    std::expected<void, std::string> result;
    const int32_t wanted = static_cast<int32_t>(_FrameNum);
    const int32_t current = _Smacker->CurrentFrame();

    if (current + 1 == wanted || (current == static_cast<int32_t>(_Smacker->FrameCount()) - 1 && wanted == 0))
    {
        result = _Smacker->DecodeNextFrame();
    }
    else
    {
        result = _Smacker->SeekFrame(_FrameNum);
    }

    if (_Smacker->PaletteChanged() && _RemapValid)
    {
        MCSmacker::BuildRemap(_Smacker->Palette(), _RemapPalette, _Remap);
    }

    if (_Stream && _AudioTrack >= 0)
    {
        const std::span<const uint8_t> pcm = _Smacker->Audio(_AudioTrack);

        if (!pcm.empty())
        {
            _Stream->Queue(pcm);
        }
    }

    ++_FramesDone;
    return result;
}

void MCSmackerPlayer::NextFrame()
{
    _FrameNum = (_FrameNum + 1) % _Smacker->FrameCount();
}

uint64_t MCSmackerPlayer::ElapsedMicroseconds() const
{
    return NowMicroseconds() - _StartTicks;
}

uint64_t MCSmackerPlayer::NextFrameTime() const
{
    return _FramesDone * _Smacker->MicrosecondsPerFrame();
}

bool MCSmackerPlayer::Wait()
{
    if (!_Started)
    {
        return false;
    }

    return ElapsedMicroseconds() < NextFrameTime();
}

namespace
{
    /// <summary>The mixer movie sound goes to (SmackSoundUseDirectSound).</summary>
    std::weak_ptr<MCAudio>& SmackerAudio()
    {
        static MCNeverDestroyed<std::weak_ptr<MCAudio>> audio;
        return *audio;
    }
}

void SmackSoundUseDirectSound(std::weak_ptr<MCAudio> audio)
{
    SmackerAudio() = std::move(audio);
}

std::unique_ptr<MCSmackTag> SmackOpen(std::string_view fileName, uint32_t flags, int32_t extraBuffers)
{
    auto player = MCSmackerPlayer::Open(MCFileSystem::Resolve(fileName), SmackerAudio().lock());

    if (!player)
    {
        return nullptr;
    }

    auto movie = std::make_unique<MCSmackTag>();
    movie->Player = std::move(*player);
    movie->FileName = fileName;
    return movie;
}

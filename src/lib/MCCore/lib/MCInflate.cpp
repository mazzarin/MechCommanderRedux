#include "stdafx.h"
#include "lib/MCInflate.h"

namespace
{
    /// <summary>The longest Huffman code, and the symbol counts of the two alphabets and the code-length one.</summary>
    constexpr int32_t MaxBits = 15;
    constexpr int32_t LiteralCodes = 288;
    constexpr int32_t DistanceCodes = 30;
    constexpr int32_t LengthCodes = 19;
    constexpr int32_t EndOfBlock = 256;
    constexpr int32_t FirstLength = 257;

    /// <summary>The base length and extra bits of length symbols 257..285, and of distance symbols 0..29.</summary>
    constexpr std::array<uint16_t, 29> LengthBase = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                     31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    constexpr std::array<uint8_t, 29> LengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    constexpr std::array<uint16_t, 30> DistanceBase = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                                       33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                                       1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    constexpr std::array<uint8_t, 30> DistanceExtra = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    /// <summary>The order the code-length alphabet's lengths are stored in.</summary>
    constexpr std::array<uint8_t, LengthCodes> LengthOrder = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                              11, 4,  12, 3, 13, 2, 14, 1, 15};

    /// <summary>A canonical Huffman code: how many codes each length has, and the symbols by code.</summary>
    struct MCHuffman
    {
        std::array<uint16_t, MaxBits + 1> Count{};
        std::vector<uint16_t> Symbol;
    };

    /// <summary>The code for <paramref name="lengths"/>; false when it is over-subscribed.</summary>
    bool Build(MCHuffman& code, std::span<const uint8_t> lengths)
    {
        code.Count.fill(0);
        code.Symbol.assign(lengths.size(), 0);

        for (const uint8_t length : lengths)
        {
            code.Count[length]++;
        }

        int32_t left = 1;

        for (int32_t length = 1; length <= MaxBits; length++)
        {
            left = (left << 1) - code.Count[static_cast<size_t>(length)];

            if (left < 0)
            {
                return false;
            }
        }

        std::array<uint16_t, MaxBits + 2> offsets{};

        for (int32_t length = 1; length <= MaxBits; length++)
        {
            offsets[static_cast<size_t>(length) + 1] =
                static_cast<uint16_t>(offsets[static_cast<size_t>(length)] + code.Count[static_cast<size_t>(length)]);
        }

        for (size_t symbol = 0; symbol < lengths.size(); symbol++)
        {
            if (lengths[symbol] != 0)
            {
                code.Symbol[offsets[lengths[symbol]]++] = static_cast<uint16_t>(symbol);
            }
        }

        return true;
    }

    /// <summary>Reads a DEFLATE stream's bits, lowest first, and writes its output.</summary>
    class MCInflater
    {
    public:
        MCInflater(std::span<const uint8_t> input, size_t size) : _Input(input) { _Output.reserve(size); }

        std::expected<std::vector<uint8_t>, std::string> Run(size_t size)
        {
            bool last = false;

            while (!last)
            {
                last = Bits(1) != 0;
                const uint32_t type = Bits(2);
                std::optional<std::string> problem;

                switch (type)
                {
                    case 0:
                        problem = Stored(size);
                        break;
                    case 1:
                        problem = Fixed(size);
                        break;
                    case 2:
                        problem = Dynamic(size);
                        break;
                    default:
                        problem = "a block of type 3";
                        break;
                }

                if (_Overrun)
                {
                    return std::unexpected(std::string("the stream ends early"));
                }

                if (problem.has_value())
                {
                    return std::unexpected(*problem);
                }
            }

            if (_Output.size() != size)
            {
                return std::unexpected(std::format("it unpacks to {} bytes, not {}", _Output.size(), size));
            }

            return std::move(_Output);
        }

    private:
        uint32_t Bits(int32_t count)
        {
            while (_BitCount < count)
            {
                if (_At >= _Input.size())
                {
                    _Overrun = true;
                    return 0;
                }

                _Buffer |= static_cast<uint64_t>(_Input[_At++]) << _BitCount;
                _BitCount += 8;
            }

            const auto value = static_cast<uint32_t>(_Buffer & ((1ull << count) - 1));
            _Buffer >>= count;
            _BitCount -= count;
            return value;
        }

        /// <summary>The next symbol of <paramref name="code"/>, or -1 for a code it lacks (or no input).</summary>
        int32_t Decode(const MCHuffman& code)
        {
            int32_t value = 0;
            int32_t first = 0;
            int32_t index = 0;

            for (int32_t length = 1; length <= MaxBits; length++)
            {
                value |= static_cast<int32_t>(Bits(1));

                if (_Overrun)
                {
                    return -1;
                }

                const int32_t count = code.Count[static_cast<size_t>(length)];

                if (value - count < first)
                {
                    return code.Symbol[static_cast<size_t>(index + (value - first))];
                }

                index += count;
                first = (first + count) << 1;
                value <<= 1;
            }

            return -1;
        }

        std::optional<std::string> Stored(size_t size)
        {
            _Buffer = 0;
            _BitCount = 0;

            if (_At + 4 > _Input.size())
            {
                _Overrun = true;
                return std::nullopt;
            }

            const uint32_t length = _Input[_At] | (static_cast<uint32_t>(_Input[_At + 1]) << 8);
            const uint32_t check = _Input[_At + 2] | (static_cast<uint32_t>(_Input[_At + 3]) << 8);
            _At += 4;

            if ((length ^ 0xffffu) != check)
            {
                return "a stored block's length doesn't match its check";
            }

            if (_At + length > _Input.size() || _Output.size() + length > size)
            {
                return "a stored block runs past the stream or the size";
            }

            _Output.insert(_Output.end(), _Input.begin() + static_cast<ptrdiff_t>(_At),
                           _Input.begin() + static_cast<ptrdiff_t>(_At + length));
            _At += length;
            return std::nullopt;
        }

        std::optional<std::string> Codes(const MCHuffman& literals, const MCHuffman& distances, size_t size)
        {
            for (;;)
            {
                const int32_t symbol = Decode(literals);

                if (symbol < 0)
                {
                    return _Overrun ? std::nullopt : std::optional<std::string>("a literal code it lacks");
                }

                if (symbol < EndOfBlock)
                {
                    if (_Output.size() >= size)
                    {
                        return "it unpacks past its size";
                    }

                    _Output.push_back(static_cast<uint8_t>(symbol));
                    continue;
                }

                if (symbol == EndOfBlock)
                {
                    return std::nullopt;
                }

                const auto lengthSymbol = static_cast<size_t>(symbol - FirstLength);

                if (lengthSymbol >= LengthBase.size())
                {
                    return "a length symbol past 285";
                }

                const size_t length = LengthBase[lengthSymbol] + Bits(LengthExtra[lengthSymbol]);
                const int32_t distanceSymbol = Decode(distances);

                if (distanceSymbol < 0 || static_cast<size_t>(distanceSymbol) >= DistanceBase.size())
                {
                    return _Overrun ? std::nullopt : std::optional<std::string>("a distance code it lacks");
                }

                const size_t distance = DistanceBase[static_cast<size_t>(distanceSymbol)] +
                                        Bits(DistanceExtra[static_cast<size_t>(distanceSymbol)]);

                if (distance > _Output.size() || _Output.size() + length > size)
                {
                    return "a match reaches before the start or past the size";
                }

                const size_t from = _Output.size() - distance;

                for (size_t i = 0; i < length; i++)
                {
                    _Output.push_back(_Output[from + i]);
                }
            }
        }

        std::optional<std::string> Fixed(size_t size)
        {
            static const std::pair<MCHuffman, MCHuffman> codes = []
            {
                std::array<uint8_t, LiteralCodes> literalLengths{};
                std::fill_n(literalLengths.begin(), 144, uint8_t{8});
                std::fill_n(literalLengths.begin() + 144, 112, uint8_t{9});
                std::fill_n(literalLengths.begin() + 256, 24, uint8_t{7});
                std::fill_n(literalLengths.begin() + 280, 8, uint8_t{8});
                std::array<uint8_t, DistanceCodes> distanceLengths{};
                distanceLengths.fill(5);
                std::pair<MCHuffman, MCHuffman> built;
                Build(built.first, literalLengths);
                Build(built.second, distanceLengths);
                return built;
            }();

            return Codes(codes.first, codes.second, size);
        }

        std::optional<std::string> Dynamic(size_t size)
        {
            const uint32_t literalCount = Bits(5) + 257;
            const uint32_t distanceCount = Bits(5) + 1;
            const uint32_t lengthCount = Bits(4) + 4;

            if (literalCount > 286 || distanceCount > DistanceCodes)
            {
                return "a dynamic block with too many codes";
            }

            std::array<uint8_t, LengthCodes> lengthLengths{};

            for (uint32_t i = 0; i < lengthCount; i++)
            {
                lengthLengths[LengthOrder[i]] = static_cast<uint8_t>(Bits(3));
            }

            MCHuffman lengthCode;

            if (!Build(lengthCode, lengthLengths))
            {
                return "an over-subscribed code-length code";
            }

            std::vector<uint8_t> lengths(literalCount + distanceCount, 0);

            for (size_t i = 0; i < lengths.size();)
            {
                const int32_t symbol = Decode(lengthCode);

                if (symbol < 0)
                {
                    return _Overrun ? std::nullopt : std::optional<std::string>("a code-length code it lacks");
                }

                if (symbol < 16)
                {
                    lengths[i++] = static_cast<uint8_t>(symbol);
                    continue;
                }

                uint8_t repeated = 0;
                size_t times = 0;

                if (symbol == 16)
                {
                    if (i == 0)
                    {
                        return "a repeat with nothing before it";
                    }

                    repeated = lengths[i - 1];
                    times = 3 + Bits(2);
                }
                else
                {
                    times = symbol == 17 ? 3 + Bits(3) : 11 + Bits(7);
                }

                if (i + times > lengths.size())
                {
                    return "code lengths past the counts";
                }

                std::fill_n(lengths.begin() + static_cast<ptrdiff_t>(i), times, repeated);
                i += times;
            }

            if (lengths[EndOfBlock] == 0)
            {
                return "no end-of-block code";
            }

            MCHuffman literals;
            MCHuffman distances;

            if (!Build(literals, std::span(lengths).first(literalCount)) ||
                !Build(distances, std::span(lengths).subspan(literalCount)))
            {
                return "an over-subscribed literal or distance code";
            }

            return Codes(literals, distances, size);
        }

        std::span<const uint8_t> _Input;
        size_t _At = 0;
        uint64_t _Buffer = 0;
        int32_t _BitCount = 0;
        bool _Overrun = false;
        std::vector<uint8_t> _Output;
    };
}

std::expected<std::vector<uint8_t>, std::string> MCInflate::Inflate(std::span<const uint8_t> deflated, size_t size)
{
    MCInflater inflater(deflated, size);
    return inflater.Run(size);
}

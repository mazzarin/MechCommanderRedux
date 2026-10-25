#include "stdafx.h"
#include "sprite/MCBc7.h"

namespace
{
    constexpr int32_t Channels = 4;
    constexpr int32_t BlockPixels = 16;
    constexpr int32_t Levels = 16;
    /// <summary>Mode 6's interpolation weights (out of 64) for its 4-bit indices.</summary>
    constexpr std::array<int32_t, Levels> Weights = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
    constexpr int32_t WeightTotal = 64;
    /// <summary>Mode 6's field widths: the mode (six 0 bits, then a 1), endpoints, p-bits and indices.</summary>
    constexpr int32_t ModeBits = 7;
    constexpr uint32_t Mode6 = 1u << 6;
    constexpr int32_t EndpointBits = 7;
    constexpr int32_t EndpointMax = (1 << EndpointBits) - 1;
    constexpr int32_t IndexBits = 4;
    /// <summary>The anchor (pixel 0) index leaves its top bit out: it is always under half.</summary>
    constexpr int32_t AnchorBits = IndexBits - 1;
    constexpr int32_t AnchorLimit = Levels / 2;
    /// <summary>Least-squares passes after the principal-axis guess.</summary>
    constexpr int32_t RefinePasses = 2;
    /// <summary>Power-iteration steps for the principal axis.</summary>
    constexpr int32_t AxisSteps = 8;
    /// <summary>What another mode decodes as: opaque magenta.</summary>
    constexpr std::array<uint8_t, Channels> Unsupported = {255, 0, 255, 255};

    using Pixel = std::array<float, Channels>;
    using Color = std::array<int32_t, Channels>;

    /// <summary>A block's endpoints: 7-bit values a channel, and their p-bits (the 8-bit value's low bit).</summary>
    struct Quantized
    {
        std::array<Color, 2> Ends{};
        std::array<int32_t, 2> PBits{};

        int32_t Value(int32_t end, int32_t channel) const { return (Ends[end][channel] << 1) | PBits[end]; }
    };

    /// <summary>A block as encoded: its endpoints, its pixels' indices and their squared error.</summary>
    struct Candidate
    {
        Quantized Ends;
        std::array<uint8_t, BlockPixels> Indices{};
        int64_t Error = std::numeric_limits<int64_t>::max();
    };

    int32_t Interpolate(int32_t e0, int32_t e1, int32_t weight)
    {
        return ((WeightTotal - weight) * e0 + weight * e1 + WeightTotal / 2) / WeightTotal;
    }

    /// <summary><paramref name="value"/> (0..255) as a 7-bit endpoint with p-bit <paramref name="pBit"/>.</summary>
    int32_t QuantizeEndpoint(float value, int32_t pBit)
    {
        return std::clamp(static_cast<int32_t>(std::lround((value - static_cast<float>(pBit)) / 2.0f)), 0, EndpointMax);
    }

    /// <summary>Each pixel's closest of the endpoints' colours, and the error of the whole block.</summary>
    void PickIndices(const std::array<Color, BlockPixels>& pixels, Candidate& candidate)
    {
        std::array<Color, Levels> palette{};

        for (int32_t level = 0; level < Levels; level++)
        {
            for (int32_t c = 0; c < Channels; c++)
            {
                palette[level][c] = Interpolate(candidate.Ends.Value(0, c), candidate.Ends.Value(1, c), Weights[level]);
            }
        }

        candidate.Error = 0;

        for (int32_t i = 0; i < BlockPixels; i++)
        {
            int64_t best = std::numeric_limits<int64_t>::max();

            for (int32_t level = 0; level < Levels; level++)
            {
                int64_t error = 0;

                for (int32_t c = 0; c < Channels; c++)
                {
                    const int64_t d = pixels[i][c] - palette[level][c];
                    error += d * d;
                }

                if (error < best)
                {
                    best = error;
                    candidate.Indices[i] = static_cast<uint8_t>(level);
                }
            }

            candidate.Error += best;
        }
    }

    /// <summary>The best of the four p-bit pairs for the endpoints <paramref name="e0"/> and <paramref name="e1"/>.</summary>
    Candidate BestQuantization(const std::array<Color, BlockPixels>& pixels, const Pixel& e0, const Pixel& e1)
    {
        Candidate best;

        for (int32_t p0 = 0; p0 < 2; p0++)
        {
            for (int32_t p1 = 0; p1 < 2; p1++)
            {
                Candidate candidate;
                candidate.Ends.PBits = {p0, p1};

                for (int32_t c = 0; c < Channels; c++)
                {
                    candidate.Ends.Ends[0][c] = QuantizeEndpoint(e0[c], p0);
                    candidate.Ends.Ends[1][c] = QuantizeEndpoint(e1[c], p1);
                }

                PickIndices(pixels, candidate);

                if (candidate.Error < best.Error)
                {
                    best = candidate;
                }
            }
        }

        return best;
    }

    /// <summary>The endpoints that fit the pixels best for <paramref name="indices"/> (least squares), if solvable.</summary>
    std::optional<std::pair<Pixel, Pixel>> FitEndpoints(const std::array<Color, BlockPixels>& pixels,
                                                        const std::array<uint8_t, BlockPixels>& indices)
    {
        float aa = 0.0f;
        float ab = 0.0f;
        float bb = 0.0f;
        Pixel ap{};
        Pixel bp{};

        for (int32_t i = 0; i < BlockPixels; i++)
        {
            const float b = static_cast<float>(Weights[indices[i]]) / WeightTotal;
            const float a = 1.0f - b;
            aa += a * a;
            ab += a * b;
            bb += b * b;

            for (int32_t c = 0; c < Channels; c++)
            {
                ap[c] += a * static_cast<float>(pixels[i][c]);
                bp[c] += b * static_cast<float>(pixels[i][c]);
            }
        }

        const float determinant = aa * bb - ab * ab;

        if (std::abs(determinant) < 1e-6f)
        {
            return std::nullopt;
        }

        Pixel e0{};
        Pixel e1{};

        for (int32_t c = 0; c < Channels; c++)
        {
            e0[c] = std::clamp((bb * ap[c] - ab * bp[c]) / determinant, 0.0f, 255.0f);
            e1[c] = std::clamp((aa * bp[c] - ab * ap[c]) / determinant, 0.0f, 255.0f);
        }

        return std::pair{e0, e1};
    }

    /// <summary>The principal-axis endpoints: the pixels' mean, out to their furthest projections each way.</summary>
    std::pair<Pixel, Pixel> AxisEndpoints(const std::array<Color, BlockPixels>& pixels)
    {
        Pixel mean{};

        for (const Color& pixel : pixels)
        {
            for (int32_t c = 0; c < Channels; c++)
            {
                mean[c] += static_cast<float>(pixel[c]) / BlockPixels;
            }
        }

        std::array<std::array<float, Channels>, Channels> covariance{};

        for (const Color& pixel : pixels)
        {
            for (int32_t r = 0; r < Channels; r++)
            {
                for (int32_t c = 0; c < Channels; c++)
                {
                    covariance[r][c] +=
                        (static_cast<float>(pixel[r]) - mean[r]) * (static_cast<float>(pixel[c]) - mean[c]);
                }
            }
        }

        Pixel axis = {1.0f, 1.0f, 1.0f, 1.0f};

        for (int32_t step = 0; step < AxisSteps; step++)
        {
            Pixel next{};
            float length = 0.0f;

            for (int32_t r = 0; r < Channels; r++)
            {
                for (int32_t c = 0; c < Channels; c++)
                {
                    next[r] += covariance[r][c] * axis[c];
                }

                length += next[r] * next[r];
            }

            if (length <= 1e-12f)
            {
                return {mean, mean};
            }

            length = std::sqrt(length);

            for (int32_t c = 0; c < Channels; c++)
            {
                axis[c] = next[c] / length;
            }
        }

        float low = std::numeric_limits<float>::max();
        float high = std::numeric_limits<float>::lowest();

        for (const Color& pixel : pixels)
        {
            float t = 0.0f;

            for (int32_t c = 0; c < Channels; c++)
            {
                t += (static_cast<float>(pixel[c]) - mean[c]) * axis[c];
            }

            low = std::min(low, t);
            high = std::max(high, t);
        }

        Pixel e0{};
        Pixel e1{};

        for (int32_t c = 0; c < Channels; c++)
        {
            e0[c] = std::clamp(mean[c] + low * axis[c], 0.0f, 255.0f);
            e1[c] = std::clamp(mean[c] + high * axis[c], 0.0f, 255.0f);
        }

        return {e0, e1};
    }

    /// <summary>
    /// For one channel: the 7-bit endpoint pair that interpolates exactly to each value at each p-bit pair and level,
    /// if any (a block of one colour is encoded exactly when one pair and level serve all its channels).
    /// </summary>
    struct MCSolidTable
    {
        struct Entry
        {
            uint8_t E0 = 0;
            uint8_t E1 = 0;
            bool Exact = false;
        };

        std::array<std::array<std::array<std::array<Entry, 256>, Levels>, 2>, 2> Entries{};

        MCSolidTable()
        {
            for (int32_t p0 = 0; p0 < 2; p0++)
            {
                for (int32_t p1 = 0; p1 < 2; p1++)
                {
                    for (int32_t level = 0; level < Levels; level++)
                    {
                        auto& values = Entries[p0][p1][level];

                        for (int32_t q0 = 0; q0 <= EndpointMax; q0++)
                        {
                            for (int32_t q1 = 0; q1 <= EndpointMax; q1++)
                            {
                                const int32_t value = Interpolate((q0 << 1) | p0, (q1 << 1) | p1, Weights[level]);

                                if (!values[value].Exact)
                                {
                                    values[value] = {static_cast<uint8_t>(q0), static_cast<uint8_t>(q1), true};
                                }
                            }
                        }
                    }
                }
            }
        }
    };

    const MCSolidTable& SolidTable()
    {
        static const MCSolidTable table;
        return table;
    }

    /// <summary>A block of the one colour <paramref name="color"/>, exactly, if a p-bit pair and level allow it.</summary>
    std::optional<Candidate> EncodeSolid(const Color& color)
    {
        const MCSolidTable& table = SolidTable();

        for (int32_t p0 = 0; p0 < 2; p0++)
        {
            for (int32_t p1 = 0; p1 < 2; p1++)
            {
                for (int32_t level = 0; level < Levels; level++)
                {
                    Candidate candidate;
                    candidate.Ends.PBits = {p0, p1};
                    bool exact = true;

                    for (int32_t c = 0; c < Channels && exact; c++)
                    {
                        const MCSolidTable::Entry& entry = table.Entries[p0][p1][level][color[c]];
                        exact = entry.Exact;
                        candidate.Ends.Ends[0][c] = entry.E0;
                        candidate.Ends.Ends[1][c] = entry.E1;
                    }

                    if (exact)
                    {
                        candidate.Indices.fill(static_cast<uint8_t>(level));
                        candidate.Error = 0;
                        return candidate;
                    }
                }
            }
        }

        return std::nullopt;
    }

    /// <summary>Writes bits into a block, lowest first.</summary>
    class MCBlockWriter
    {
    public:
        explicit MCBlockWriter(std::span<uint8_t, MCBc7::BlockBytes> block) : _Block(block)
        {
            std::ranges::fill(_Block, uint8_t{0});
        }

        void Put(uint32_t value, int32_t bits)
        {
            for (int32_t i = 0; i < bits; i++, _At++)
            {
                if ((value >> i) & 1u)
                {
                    _Block[static_cast<size_t>(_At / 8)] |= static_cast<uint8_t>(1u << (_At % 8));
                }
            }
        }

    private:
        std::span<uint8_t, MCBc7::BlockBytes> _Block;
        int32_t _At = 0;
    };

    /// <summary>Reads bits from a block, lowest first.</summary>
    class MCBlockReader
    {
    public:
        explicit MCBlockReader(std::span<const uint8_t, MCBc7::BlockBytes> block) : _Block(block) {}

        uint32_t Get(int32_t bits)
        {
            uint32_t value = 0;

            for (int32_t i = 0; i < bits; i++, _At++)
            {
                value |= static_cast<uint32_t>((_Block[static_cast<size_t>(_At / 8)] >> (_At % 8)) & 1u) << i;
            }

            return value;
        }

    private:
        std::span<const uint8_t, MCBc7::BlockBytes> _Block;
        int32_t _At = 0;
    };

    /// <summary>The 4x4 pixels of a picture's block at (<paramref name="bx"/>, <paramref name="by"/>), transparent past it.</summary>
    std::array<uint8_t, 64> BlockPixelsOf(std::span<const uint8_t> rgba, int32_t width, int32_t height, int32_t bx,
                                          int32_t by)
    {
        std::array<uint8_t, 64> out{};

        for (int32_t y = 0; y < MCBc7::BlockSide; y++)
        {
            const int32_t row = by * MCBc7::BlockSide + y;

            for (int32_t x = 0; x < MCBc7::BlockSide; x++)
            {
                const int32_t column = bx * MCBc7::BlockSide + x;

                if (row < height && column < width)
                {
                    const size_t from = (static_cast<size_t>(row) * width + column) * Channels;
                    std::copy_n(rgba.begin() + static_cast<ptrdiff_t>(from), Channels,
                                out.begin() + (y * MCBc7::BlockSide + x) * Channels);
                }
            }
        }

        return out;
    }
}

void MCBc7::EncodeBlock(std::span<const uint8_t, 64> pixels, std::span<uint8_t, BlockBytes> block)
{
    std::array<Color, BlockPixels> colors{};

    for (int32_t i = 0; i < BlockPixels; i++)
    {
        for (int32_t c = 0; c < Channels; c++)
        {
            colors[i][c] = pixels[static_cast<size_t>(i * Channels + c)];
        }
    }

    Candidate best;

    if (std::ranges::all_of(colors, [&](const Color& color) { return color == colors[0]; }))
    {
        if (std::optional<Candidate> solid = EncodeSolid(colors[0]); solid.has_value())
        {
            best = *solid;
        }
    }

    if (best.Error != 0)
    {
        const auto [first, second] = AxisEndpoints(colors);
        best = BestQuantization(colors, first, second);
    }

    for (int32_t pass = 0; pass < RefinePasses && best.Error > 0; pass++)
    {
        const auto fitted = FitEndpoints(colors, best.Indices);

        if (!fitted.has_value())
        {
            break;
        }

        const Candidate refined = BestQuantization(colors, fitted->first, fitted->second);

        if (refined.Error >= best.Error)
        {
            break;
        }

        best = refined;
    }

    // The anchor's top bit isn't stored: swap the endpoints when its index needs it.
    if (best.Indices[0] >= AnchorLimit)
    {
        std::swap(best.Ends.Ends[0], best.Ends.Ends[1]);
        std::swap(best.Ends.PBits[0], best.Ends.PBits[1]);

        for (uint8_t& index : best.Indices)
        {
            index = static_cast<uint8_t>(Levels - 1 - index);
        }
    }

    MCBlockWriter writer(block);
    writer.Put(Mode6, ModeBits);

    for (int32_t c = 0; c < Channels; c++)
    {
        writer.Put(static_cast<uint32_t>(best.Ends.Ends[0][c]), EndpointBits);
        writer.Put(static_cast<uint32_t>(best.Ends.Ends[1][c]), EndpointBits);
    }

    writer.Put(static_cast<uint32_t>(best.Ends.PBits[0]), 1);
    writer.Put(static_cast<uint32_t>(best.Ends.PBits[1]), 1);
    writer.Put(best.Indices[0], AnchorBits);

    for (int32_t i = 1; i < BlockPixels; i++)
    {
        writer.Put(best.Indices[i], IndexBits);
    }
}

void MCBc7::DecodeBlock(std::span<const uint8_t, BlockBytes> block, std::span<uint8_t, 64> pixels)
{
    MCBlockReader reader(block);

    if (reader.Get(ModeBits) != Mode6)
    {
        for (int32_t i = 0; i < BlockPixels; i++)
        {
            std::ranges::copy(Unsupported, pixels.begin() + i * Channels);
        }

        return;
    }

    Quantized ends;

    for (int32_t c = 0; c < Channels; c++)
    {
        ends.Ends[0][c] = static_cast<int32_t>(reader.Get(EndpointBits));
        ends.Ends[1][c] = static_cast<int32_t>(reader.Get(EndpointBits));
    }

    ends.PBits[0] = static_cast<int32_t>(reader.Get(1));
    ends.PBits[1] = static_cast<int32_t>(reader.Get(1));

    for (int32_t i = 0; i < BlockPixels; i++)
    {
        const auto index = static_cast<int32_t>(reader.Get(i == 0 ? AnchorBits : IndexBits));

        for (int32_t c = 0; c < Channels; c++)
        {
            pixels[static_cast<size_t>(i * Channels + c)] =
                static_cast<uint8_t>(Interpolate(ends.Value(0, c), ends.Value(1, c), Weights[index]));
        }
    }
}

std::vector<uint8_t> MCBc7::Encode(std::span<const uint8_t> rgba, int32_t width, int32_t height)
{
    const int32_t columns = Padded(width) / BlockSide;
    const int32_t rows = Padded(height) / BlockSide;
    std::vector<uint8_t> blocks(static_cast<size_t>(columns) * rows * BlockBytes);

    for (int32_t by = 0; by < rows; by++)
    {
        for (int32_t bx = 0; bx < columns; bx++)
        {
            const std::array<uint8_t, 64> pixels = BlockPixelsOf(rgba, width, height, bx, by);
            EncodeBlock(pixels, std::span<uint8_t, BlockBytes>(
                                    blocks.data() + (static_cast<size_t>(by) * columns + bx) * BlockBytes, BlockBytes));
        }
    }

    return blocks;
}

std::vector<uint8_t> MCBc7::Decode(std::span<const uint8_t> blocks, int32_t width, int32_t height)
{
    const int32_t columns = Padded(width) / BlockSide;
    const int32_t rows = Padded(height) / BlockSide;
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * Channels);

    if (blocks.size() < static_cast<size_t>(columns) * rows * BlockBytes)
    {
        return rgba;
    }

    for (int32_t by = 0; by < rows; by++)
    {
        for (int32_t bx = 0; bx < columns; bx++)
        {
            std::array<uint8_t, 64> pixels{};
            DecodeBlock(std::span<const uint8_t, BlockBytes>(
                            blocks.data() + (static_cast<size_t>(by) * columns + bx) * BlockBytes, BlockBytes),
                        pixels);

            for (int32_t y = 0; y < BlockSide; y++)
            {
                const int32_t row = by * BlockSide + y;

                for (int32_t x = 0; x < BlockSide; x++)
                {
                    const int32_t column = bx * BlockSide + x;

                    if (row < height && column < width)
                    {
                        std::copy_n(pixels.begin() + (y * BlockSide + x) * Channels, Channels,
                                    rgba.begin() +
                                        static_cast<ptrdiff_t>((static_cast<size_t>(row) * width + column) * Channels));
                    }
                }
            }
        }
    }

    return rgba;
}

#include "stdafx.h"
#include "platform/MCShapeRows.h"
#include "platform/MCSoftwareRenderer.h"

namespace
{
    /// <summary>A shape header's first and last rows (VFX's: 0x0c and 0x14).</summary>
    constexpr size_t HeaderTopAt = 0x0c;
    constexpr size_t HeaderBottomAt = 0x14;
}

size_t MCShapeRows::Extent(std::span<const uint8_t> bytes, int32_t rows, int32_t& width)
{
    size_t data = 0;
    width = 0;

    for (int32_t row = 0; row < rows; ++row)
    {
        int32_t x = 0;

        for (;;)
        {
            const uint8_t token = bytes[data++];
            const int32_t count = token >> 1;

            if (count == 0)
            {
                if ((token & 1) == 0)
                {
                    break;
                }

                x += bytes[data++];
                continue;
            }

            data += (token & 1) ? count : 1;
            x += count;
        }

        width = std::max(width, x);
    }

    return data;
}

void MCShapeRows::Decode(std::span<const uint8_t> bytes, int32_t rows, int32_t width, std::span<uint8_t> out)
{
    size_t data = 0;
    std::memset(out.data(), 0, static_cast<size_t>(width) * rows * DecodedBytes);

    for (int32_t row = 0; row < rows; ++row)
    {
        const std::span<uint8_t> line = out.subspan(static_cast<size_t>(row) * width * DecodedBytes);
        int32_t x = 0;

        for (;;)
        {
            const uint8_t token = bytes[data++];
            const int32_t count = token >> 1;

            if (count == 0)
            {
                if ((token & 1) == 0)
                {
                    break;
                }

                const int32_t skip = bytes[data++];

                for (int32_t i = 0; i < skip; ++i)
                {
                    line[(x + i) * DecodedBytes + 1] = Skipped;
                }

                x += skip;
                continue;
            }

            for (int32_t i = 0; i < count; ++i)
            {
                line[(x + i) * DecodedBytes] = (token & 1) ? bytes[data + i] : bytes[data];
                line[(x + i) * DecodedBytes + 1] = Drawn;
            }

            data += (token & 1) ? count : 1;
            x += count;
        }
    }
}

int32_t MCShapeRows::HeaderRows(std::span<const uint8_t> header)
{
    return MCRead32(header, HeaderBottomAt) - MCRead32(header, HeaderTopAt) + 1;
}

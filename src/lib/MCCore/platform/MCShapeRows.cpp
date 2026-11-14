#include "stdafx.h"
#include "platform/MCShapeRows.h"
#include "platform/MCSoftwareRenderer.h"

namespace
{
    /// <summary>A shape header's first and last rows (VFX's: 0x0c and 0x14).</summary>
    constexpr size_t HeaderTopAt = 0x0c;
    constexpr size_t HeaderBottomAt = 0x14;

    /// <summary>
    /// Byte <paramref name="at"/> of a shape's rows, 0 (a row's end) past them: a shape's rows may end before its
    /// header's do (the game's vr134.shp, its sold-out outline: a header and no rows), and nothing past them is the
    /// shape's.
    /// </summary>
    uint8_t RowByte(std::span<const uint8_t> bytes, size_t at)
    {
        return at < bytes.size() ? bytes[at] : 0;
    }
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
            const uint8_t token = RowByte(bytes, data++);
            const int32_t count = token >> 1;

            if (count == 0)
            {
                if ((token & 1) == 0)
                {
                    break;
                }

                x += RowByte(bytes, data++);
                continue;
            }

            data += (token & 1) ? count : 1;
            x += count;
        }

        width = std::max(width, x);
    }

    return std::min(data, bytes.size());
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
            const uint8_t token = RowByte(bytes, data++);
            const int32_t count = token >> 1;

            if (count == 0)
            {
                if ((token & 1) == 0)
                {
                    break;
                }

                const int32_t skip = RowByte(bytes, data++);

                for (int32_t i = 0; i < skip; ++i)
                {
                    line[(x + i) * DecodedBytes + 1] = Skipped;
                }

                x += skip;
                continue;
            }

            for (int32_t i = 0; i < count; ++i)
            {
                line[(x + i) * DecodedBytes] = RowByte(bytes, (token & 1) ? data + i : data);
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

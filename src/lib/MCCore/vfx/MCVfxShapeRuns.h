#pragma once

// Port-only: a shape's pixels as the runs it stores them in, for code that measures where a shape draws (the stopping
// art's likeness, sprite/MCMechStops) without a call a pixel.

#include "vfx/MCVfxClip.h"

/// <summary>
/// Port-only: calls <paramref name="run"/> with each run of pixels shape <paramref name="shapeNum"/> of
/// <paramref name="shapeTable"/> draws: <c>run(x, y, count, colours)</c>, the first pixel's offset from the hot spot, the
/// row, the pixels and their colours (<paramref name="count"/> of them for a run of different colours, one for a run of
/// one colour). VfxShapeVisit's pixels in its order, unclipped, skipped ones left out; nothing for a table that isn't a
/// shape table or a shape it hasn't; a row running past the table's end stops the walk.
/// </summary>
template <typename Run> void VfxShapeRuns(std::span<const uint8_t> shapeTable, int32_t shapeNum, Run&& run)
{
    // The first row's first token, after the header.
    constexpr size_t FirstToken = sizeof(MCVfxShapeHeader);

    if (!VfxIsShapeTable(shapeTable) || shapeNum < 0 || shapeNum >= VfxShapeCount(shapeTable) ||
        !MCVfxHasShape(shapeTable, shapeNum))
    {
        return;
    }

    const std::span<const uint8_t> header = MCVfxShape(shapeTable, shapeNum);
    const int32_t xMin = MCVfxRead32(header, offsetof(MCVfxShapeHeader, XMin));
    const int32_t yMin = MCVfxRead32(header, offsetof(MCVfxShapeHeader, YMin));
    const int32_t yMax = MCVfxRead32(header, offsetof(MCVfxShapeHeader, YMax));
    size_t at = FirstToken;

    for (int32_t y = yMin; y <= yMax; ++y)
    {
        int32_t x = xMin;

        for (;;)
        {
            if (at >= header.size())
            {
                return;
            }

            const uint8_t token = header[at++];
            const int32_t count = token >> 1;

            if (count == 0)
            {
                if ((token & 1) == 0)
                {
                    break;
                }

                if (at >= header.size())
                {
                    return;
                }

                x += header[at++];
                continue;
            }

            const bool literal = (token & 1) != 0;
            const size_t bytes = literal ? static_cast<size_t>(count) : 1;

            if (at + bytes > header.size())
            {
                return;
            }

            run(x, y, count, header.subspan(at, bytes));
            at += bytes;
            x += count;
        }
    }
}

#include "stdafx.h"
#include "platform/MCSoftwareRenderer.h"
#include "vfx/MCVfxClip.h"

// The software renderer's run-length shapes (the row loops of vfxa.asm's shape routines and the game's
// vfx_translatedraw.cpp), fast shapes (fastshp.cpp) and terrain tiles (vfxtile.cpp).

namespace
{
    /// <summary>A blend through the game's alpha table: <paramref name="color"/> over <paramref name="screen"/>.</summary>
    uint8_t Blend(uint8_t color, uint8_t screen)
    {
        return AlphaTable[(static_cast<uint32_t>(color) << 8) | screen];
    }

    /// <summary>
    /// Blends <paramref name="color"/> over <paramref name="p"/> (then maps it through <paramref name="xlat"/> for
    /// XlatAlpha), or records that in its op when it is see-through.
    /// </summary>
    template <MCShapeOp Op>
    void BlendPixel(uint8_t& p, uint8_t color, const std::optional<MCColorTable>& xlat, MCSeeThrough& seeThrough)
    {
        if (seeThrough.At(p))
        {
            seeThrough.Blend(p, color);

            if constexpr (Op == MCShapeOp::XlatAlpha)
            {
                seeThrough.Map(p, *xlat);
            }

            return;
        }

        p = Blend(color, p);

        if constexpr (Op == MCShapeOp::XlatAlpha)
        {
            p = (*xlat)[p];
        }
    }

    /// <summary>Steps over one encoded row (up to and past its end token); returns the bytes read.</summary>
    size_t SkipRow(std::span<const uint8_t> bytes)
    {
        size_t data = 0;

        for (;;)
        {
            const uint8_t token = bytes[data++];
            const uint32_t count = token >> 1;

            if (count != 0)
            {
                data += (token & 1) ? count : 1;
            }
            else if (token & 1)
            {
                ++data;
            }
            else
            {
                return data;
            }
        }
    }

    /// <summary>
    /// Draws one encoded row from window column <paramref name="x"/>, writing columns lo..hi of <paramref name="pixels"/>
    /// (the target's row); returns the bytes read.
    /// </summary>
    /// <remarks>
    /// VFX's own routines had separate loops for rows needing no clipping, left clipping, right clipping and both;
    /// they write the same pixels as this loop (as the Draw and Xlat ops, unclipped) for every shape whose rows stay
    /// within its bounds. Port fix: a row running past its declared bounds is clipped here too.
    /// </remarks>
    template <MCShapeOp Op>
    size_t DrawRow(std::span<const uint8_t> bytes, std::span<uint8_t> pixels, int32_t x, int32_t lo, int32_t hi,
                   const std::optional<MCColorTable>& table, MCSeeThrough& seeThrough)
    {
        constexpr bool fill = Op == MCShapeOp::Fill || Op == MCShapeOp::XlatFill;
        constexpr bool translate = Op == MCShapeOp::Xlat || Op == MCShapeOp::XlatFill;
        size_t data = 0;

        for (;;)
        {
            const uint8_t token = bytes[data++];
            const int32_t count = token >> 1;

            if (count == 0)
            {
                if ((token & 1) == 0)
                {
                    return data;
                }

                const int32_t skip = bytes[data++];

                if constexpr (fill)
                {
                    // The fills write skipped pixels as colour 0.
                    for (int32_t i = 0; i < skip; ++i)
                    {
                        const int32_t column = x + i;

                        if (column < lo)
                        {
                            continue;
                        }

                        if (column > hi)
                        {
                            break;
                        }

                        pixels[column] = 0;
                    }
                }

                x += skip;
                continue;
            }

            if (token & 1)
            {
                for (int32_t i = 0; i < count; ++i)
                {
                    const int32_t column = x + i;

                    if (column < lo)
                    {
                        continue;
                    }

                    if (column > hi)
                    {
                        break;
                    }

                    const uint8_t pixel = bytes[data + i];

                    if constexpr (Op == MCShapeOp::Alpha || Op == MCShapeOp::XlatAlpha)
                    {
                        BlendPixel<Op>(pixels[column], pixel, table, seeThrough);
                    }
                    else if constexpr (translate)
                    {
                        pixels[column] = (*table)[pixel];
                    }
                    else
                    {
                        pixels[column] = pixel;
                    }
                }

                data += count;
            }
            else
            {
                uint8_t color = bytes[data++];

                if constexpr (translate)
                {
                    color = (*table)[color];
                }

                for (int32_t i = 0; i < count; ++i)
                {
                    const int32_t column = x + i;

                    if (column < lo)
                    {
                        continue;
                    }

                    if (column > hi)
                    {
                        break;
                    }

                    if constexpr (Op == MCShapeOp::Alpha || Op == MCShapeOp::XlatAlpha)
                    {
                        // OB-114: the asm blended only a run's first pixel with its colour, the rest with colour 0.
                        BlendPixel<Op>(pixels[column], color, table, seeThrough);
                    }
                    else
                    {
                        pixels[column] = color;
                    }
                }
            }

            x += count;
        }
    }

    template <MCShapeOp Op>
    void DrawRows(MCWindow& target, const MCShapeCommand& command, std::span<const uint8_t> data)
    {
        const intptr_t stride = target.XMax + 1;
        const std::span<uint8_t> pixels = target.Bytes();
        auto row = static_cast<size_t>(command.Top * stride);
        MCSeeThrough seeThrough(target);

        for (int32_t rows = command.Rows; rows > 0; --rows, row += static_cast<size_t>(stride))
        {
            data = data.subspan(DrawRow<Op>(data, pixels.subspan(row, static_cast<size_t>(stride)), command.Left,
                                            command.Lo, command.Hi, command.Table, seeThrough));
        }
    }

    /// <summary>The four fast-shape variants, each an inline-asm loop of its own in the original.</summary>
    enum class FastMode
    {
        Plain,
        Translate,
        Alpha,
        AlphaTranslate
    };

    /// <summary>Draws one fast-shape row: skips <paramref name="leftSkip"/> pixels, then draws until <paramref name="limit"/>.</summary>
    void DrawFastRow(std::span<const uint8_t> bytes, std::span<uint8_t> pixels, int32_t startX, int32_t clipX0,
                     int32_t leftSkip, int32_t limit, const std::optional<MCColorTable>& table, FastMode mode)
    {
        size_t data = 0;
        int32_t count = 0;
        size_t dst = 0;

        if (leftSkip > 0)
        {
            // Step over the clipped-off pixels; the packet that crosses the edge is drawn from the pane's left edge.
            for (;;)
            {
                const uint32_t c = bytes[data];

                if (c >= 0x80)
                {
                    const int32_t n = static_cast<int32_t>(c) - 0x80;
                    ++data;
                    count += n;

                    if (count > leftSkip)
                    {
                        const int32_t rem = count - leftSkip;
                        count = rem;
                        data += n - rem;
                        dst = static_cast<size_t>(clipX0);

                        switch (mode)
                        {
                            case FastMode::Plain:
                            {
                                std::ranges::copy(bytes.subspan(data, static_cast<size_t>(rem)),
                                                  pixels.begin() + static_cast<ptrdiff_t>(dst));
                                data += rem;
                                dst += rem;
                                break;
                            }
                            case FastMode::Translate:
                            {
                                // OB-118: the asm advanced the pointer before the store, so these pixels landed one
                                // to the right, and didn't skip colour 255.
                                for (int32_t i = 0; i < rem; ++i, ++dst)
                                {
                                    const uint8_t color = (*table)[bytes[data++]];

                                    if (color != 0xff)
                                    {
                                        pixels[dst] = color;
                                    }
                                }
                                break;
                            }
                            case FastMode::Alpha:
                            {
                                for (int32_t i = 0; i < rem; ++i, ++dst)
                                {
                                    pixels[dst] = Blend(bytes[data++], pixels[dst]);
                                }
                                break;
                            }
                            case FastMode::AlphaTranslate:
                            {
                                for (int32_t i = 0; i < rem; ++i, ++dst)
                                {
                                    const uint8_t color = (*table)[bytes[data++]];

                                    if (color != 0xff)
                                    {
                                        pixels[dst] = Blend(color, pixels[dst]);
                                    }
                                }
                                break;
                            }
                        }
                        break;
                    }

                    data += n;
                }
                else
                {
                    count += static_cast<int32_t>(c);
                    ++data;

                    if (count > leftSkip)
                    {
                        const int32_t rem = count - leftSkip;
                        count = rem;
                        uint8_t color = bytes[data++];

                        if (mode == FastMode::Translate || mode == FastMode::AlphaTranslate)
                        {
                            color = (*table)[color];
                        }

                        dst = static_cast<size_t>(clipX0);

                        if (color == 0xff)
                        {
                            dst += rem;
                        }
                        else if (mode == FastMode::Plain || mode == FastMode::Translate)
                        {
                            std::fill_n(pixels.begin() + static_cast<ptrdiff_t>(dst), rem, color);
                            dst += rem;
                        }
                        else
                        {
                            for (int32_t i = 0; i < rem; ++i, ++dst)
                            {
                                pixels[dst] = Blend(color, pixels[dst]);
                            }
                        }
                        break;
                    }

                    ++data;
                }
            }
        }
        else
        {
            dst = static_cast<size_t>(startX);
        }

        while (count != limit)
        {
            const uint32_t c = bytes[data++];

            if (c >= 0x80)
            {
                int32_t n = static_cast<int32_t>(c) - 0x80;
                count += n;

                if (count > limit)
                {
                    n -= count - limit;
                    count = limit;
                }

                // Port fix: the plain and alpha loops were do-whiles that ran away on an empty literal (0x80).
                switch (mode)
                {
                    case FastMode::Plain:
                    {
                        for (int32_t i = 0; i < n; ++i)
                        {
                            pixels[dst++] = bytes[data++];
                        }
                        break;
                    }
                    case FastMode::Translate:
                    {
                        for (int32_t i = 0; i < n; ++i)
                        {
                            const uint8_t color = (*table)[bytes[data++]];
                            ++dst;

                            if (color != 0xff)
                            {
                                pixels[dst - 1] = color;
                            }
                        }
                        break;
                    }
                    case FastMode::Alpha:
                    {
                        for (int32_t i = 0; i < n; ++i, ++dst)
                        {
                            pixels[dst] = Blend(bytes[data++], pixels[dst]);
                        }
                        break;
                    }
                    case FastMode::AlphaTranslate:
                    {
                        for (int32_t i = 0; i < n; ++i)
                        {
                            const uint8_t color = (*table)[bytes[data++]];
                            ++dst;

                            if (color != 0xff)
                            {
                                pixels[dst - 1] = Blend(color, pixels[dst - 1]);
                            }
                        }
                        break;
                    }
                }
            }
            else
            {
                int32_t n = static_cast<int32_t>(c);
                count += n;

                if (count > limit)
                {
                    n -= count - limit;
                    count = limit;
                }

                uint8_t color = bytes[data++];

                if (mode == FastMode::Translate || mode == FastMode::AlphaTranslate)
                {
                    color = (*table)[color];
                }

                if (color == 0xff)
                {
                    dst += n;
                }
                else if (mode == FastMode::Plain || mode == FastMode::Translate)
                {
                    std::fill_n(pixels.begin() + static_cast<ptrdiff_t>(dst), n, color);
                    dst += n;
                }
                else
                {
                    for (int32_t i = 0; i < n; ++i, ++dst)
                    {
                        pixels[dst] = Blend(color, pixels[dst]);
                    }
                }
            }
        }
    }

    /// <summary>
    /// Writes <paramref name="destination"/>'s pixels of a tile span from <paramref name="source"/>: copied, translated,
    /// or the fill colour.
    /// </summary>
    void WriteSpan(std::span<uint8_t> destination, std::span<const uint8_t> source, const MCTileCommand& command)
    {
        if (command.Fill)
        {
            std::ranges::fill(destination, uint8_t{0x10});
        }
        else if (!command.Table.has_value())
        {
            std::ranges::copy(source.first(destination.size()), destination.begin());
        }
        else
        {
            const MCColorTable xlat = *command.Table;

            for (size_t i = 0; i < destination.size(); ++i)
            {
                destination[i] = xlat[source[i]];
            }
        }
    }

    /// <summary>Where tile row <paramref name="row"/> starts, from the tile's start.</summary>
    uint32_t ReadOffset(std::span<const uint8_t> tile, int32_t row)
    {
        return static_cast<uint32_t>(MCRead32(tile, 4 + static_cast<size_t>(row) * 4));
    }
}

void MCSoftwareRenderer::Shape(MCWindow& target, const MCShapeCommand& command)
{
    // Port-only: sun shadows are drawn by the hardware renderer alone.
    if (DrawingSunShadow())
    {
        return;
    }

    std::span<const uint8_t> data =
        command.ShapeTable.subspan(MCShapeOffset(command.ShapeTable, command.ShapeNum) + 0x18);

    for (int32_t i = 0; i < command.SkipRows; ++i)
    {
        data = data.subspan(SkipRow(data));
    }

    switch (command.Op)
    {
        case MCShapeOp::Draw:
            DrawRows<MCShapeOp::Draw>(target, command, data);
            break;
        case MCShapeOp::Alpha:
            DrawRows<MCShapeOp::Alpha>(target, command, data);
            break;
        case MCShapeOp::Xlat:
            DrawRows<MCShapeOp::Xlat>(target, command, data);
            break;
        case MCShapeOp::XlatAlpha:
            DrawRows<MCShapeOp::XlatAlpha>(target, command, data);
            break;
        case MCShapeOp::Fill:
            DrawRows<MCShapeOp::Fill>(target, command, data);
            break;
        case MCShapeOp::XlatFill:
            DrawRows<MCShapeOp::XlatFill>(target, command, data);
            break;
    }
}

void MCSoftwareRenderer::FastShape(MCWindow& target, const MCFastShapeCommand& command)
{
    const intptr_t stride = target.XMax + 1;
    const std::span<const uint8_t> shape = command.Shape;
    const FastMode mode = command.Alpha ? (command.Table.has_value() ? FastMode::AlphaTranslate : FastMode::Alpha)
                                        : (command.Table.has_value() ? FastMode::Translate : FastMode::Plain);
    const std::span<uint8_t> pixels = target.Bytes();
    auto row = static_cast<size_t>(command.Top * stride);

    // OB-117: the asm advanced the row-offset pointer only after reading it for the second row, so the first two rows
    // drawn both started at the first row's offset.
    for (int32_t r = command.FirstRow; r < command.EndRow; ++r)
    {
        const uint32_t offset = MCRead16(shape, 0xc + static_cast<size_t>(r) * 2);
        DrawFastRow(shape.subspan(offset), pixels.subspan(row, static_cast<size_t>(stride)), command.StartX,
                    command.ClipX0, command.LeftSkip, command.Limit, command.Table, mode);
        row += static_cast<size_t>(stride);
    }
}

void MCSoftwareRenderer::Tile(MCWindow& target, const MCTileCommand& command)
{
    const intptr_t stride = target.XMax + 1;
    const std::span<const uint8_t> tile = command.Tile;
    const std::span<uint8_t> pixels = target.Bytes();
    auto row = static_cast<size_t>(command.Top * stride);
    uint32_t offset = ReadOffset(tile, command.FirstRow);

    if (command.Unclipped)
    {
        row += static_cast<size_t>(command.Left);

        for (int32_t i = command.FirstRow; i < command.EndRow; ++i)
        {
            const uint32_t next = ReadOffset(tile, i + 1);
            const int32_t length = static_cast<int32_t>(next - offset);

            // Original behaviour: a translated span of length 1 (an x byte and no pixels) would have looped 2^32
            // times in the asm; tiles have none.
            if (length > 1)
            {
                WriteSpan(pixels.subspan(row + tile[offset], static_cast<size_t>(length - 1)), tile.subspan(offset + 1),
                          command);
            }

            offset = next;
            row += static_cast<size_t>(stride);
        }

        return;
    }

    for (int32_t i = command.FirstRow; i < command.EndRow; ++i)
    {
        const uint32_t next = ReadOffset(tile, i + 1);
        size_t source = offset;
        const int32_t first = tile[source++] + command.Left;
        int32_t count = static_cast<int32_t>(next - offset) - 1;
        const int32_t end = first + count;
        int32_t cut = 0;

        if (first < command.Lo)
        {
            cut = command.Lo - first;
            source += static_cast<size_t>(cut);
        }

        const int32_t overhang = end > command.Hi ? end - command.Hi - 1 : 0;
        count -= cut + overhang;

        if (count > 0)
        {
            WriteSpan(pixels.subspan(row + static_cast<size_t>(first + cut), static_cast<size_t>(count)),
                      tile.subspan(source), command);
        }

        offset = next;
        row += static_cast<size_t>(stride);
    }
}

#include "stdafx.h"
#include "vfx/MCVfxClip.h"

// VFX's shape routines (vfxa.asm in MCX.EXE): drawing, translating, measuring and encoding the run-length shapes the
// game's sprites, buttons and cursors are stored as.

std::array<uint8_t, 256> VfxShapeLookasideTable{};

namespace
{
    /// <summary>A shape's header fields, read from the table.</summary>
    struct MCShapeInfo
    {
        /// <summary>The shape from its header to the table's end.</summary>
        std::span<const uint8_t> Header;
        int32_t XMin = 0;
        int32_t YMin = 0;
        int32_t XMax = 0;
        int32_t YMax = 0;
    };

    MCShapeInfo ReadShape(std::span<const uint8_t> shapeTable, int32_t shapeNum)
    {
        MCShapeInfo info;
        info.Header = MCVfxShape(shapeTable, shapeNum);
        info.XMin = MCVfxRead32(info.Header, 0x08);
        info.YMin = MCVfxRead32(info.Header, 0x0c);
        info.XMax = MCVfxRead32(info.Header, 0x10);
        info.YMax = MCVfxRead32(info.Header, 0x14);
        return info;
    }

    /// <summary>
    /// The common body of VFX_shape_draw and VFX_shape_translate_draw (the latter with the lookaside table as
    /// <paramref name="xlat"/>).
    /// </summary>
    int32_t DrawShape(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                      int32_t hotY, std::optional<MCColorTable> xlat)
    {
        MCVfxClip clip;
        const int32_t status = MCVfxClipPane(pane, clip);

        if (status != 0)
        {
            return status;
        }

        hotX += clip.PaneX;
        hotY += clip.PaneY;
        const MCShapeInfo shape = ReadShape(shapeTable, shapeNum);
        const int32_t x0 = shape.XMin + hotX;
        const int32_t y0 = shape.YMin + hotY;
        const int32_t x1 = shape.XMax + hotX;
        const int32_t y1 = shape.YMax + hotY;

        if (x1 < x0 || y1 < y0)
        {
            return VfxErrBadShape;
        }

        // Cohen-Sutherland outcodes of the two corners: a side both lie beyond rejects the shape.
        if ((x0 < clip.X0 && x1 < clip.X0) || (x0 > clip.X1 && x1 > clip.X1) || (y0 < clip.Y0 && y1 < clip.Y0) ||
            (y0 > clip.Y1 && y1 > clip.Y1))
        {
            return VfxErrClipped;
        }

        // Rows above the pane are stepped over; each row drawn is clipped to the pane's columns. (The asm has separate
        // loops for rows needing no clipping, left clipping, right clipping and both, chosen from the shape's bounds;
        // they write the same pixels for every shape whose rows stay within its bounds. Port fix: a row running past
        // its declared bounds is clipped too, where the asm's unclipped loops would have written past the pane.)
        const int32_t top = std::max(y0, clip.Y0);
        const int32_t rows = std::min(y1, clip.Y1) - top + 1;

        if (rows <= 0 || shapeTable.empty())
        {
            return 0;
        }

        MCShapeCommand command;
        command.ShapeTable = shapeTable;
        command.ShapeNum = shapeNum;
        command.SkipRows = top - y0;
        command.Rows = rows;
        command.Top = top;
        command.Left = x0;
        command.Lo = clip.X0;
        command.Hi = clip.X1;
        command.ClipTop = clip.Y0;
        command.ClipBottom = clip.Y1;
        command.Op = xlat.has_value() ? MCShapeOp::Xlat : MCShapeOp::Draw;
        command.Table = xlat;
        MCRenderer::For(clip.Target()).Shape(clip.Target(), command);
        return 0;
    }
}

int32_t VfxShapeDraw(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                     int32_t hotY)
{
    return DrawShape(pane, shapeTable, shapeNum, hotX, hotY, std::nullopt);
}

void VfxShapeLookaside(MCColorTable table)
{
    std::ranges::copy(table, VfxShapeLookasideTable.begin());
    // The copy is the table draws read: registered (again) as new bytes.
    MCRenderer::RegisterData(VfxShapeLookasideTable, MCDataKind::Tables);
}

int32_t VfxShapeTranslateDraw(const MCPane& pane, std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t hotX,
                              int32_t hotY)
{
    return DrawShape(pane, shapeTable, shapeNum, hotX, hotY, MCColorTable(VfxShapeLookasideTable));
}

int32_t VfxShapeRemapColors(std::span<uint8_t> shapeTable, int32_t shapeNum)
{
    const MCShapeInfo shape = ReadShape(shapeTable, shapeNum);
    const std::span<uint8_t> header = MCVfxShape(shapeTable, shapeNum);
    // The first row's first token.
    size_t at = 0x18;

    for (int32_t rows = shape.YMax + 1 - shape.YMin; rows > 0; --rows)
    {
        for (;;)
        {
            const uint8_t token = header[at++];
            const uint32_t count = token >> 1;

            if (count == 0)
            {
                if ((token & 1) == 0)
                {
                    break;
                }

                ++at;
            }
            else if (token & 1)
            {
                for (uint32_t i = 0; i < count; ++i, ++at)
                {
                    header[at] = VfxShapeLookasideTable[header[at]];
                }
            }
            else
            {
                header[at] = VfxShapeLookasideTable[header[at]];
                ++at;
            }
        }
    }

    // The shape's pixels changed under any copy a renderer made of them.
    MCRenderer::DataChanged(header.first(at));
    return 0;
}

namespace
{
    /// <summary>
    /// The state VFX's shape encoder kept in globals between ScanLine and FlushPacket (0x007a810d..0x007a8141 in
    /// MCX.EXE), for one encoding. The asm's pointers into the pane's pixels are offsets into <see cref="Source"/>.
    /// </summary>
    struct MCShapeScanner
    {
        /// <summary>The pixels encoded (the window's).</summary>
        std::span<const uint8_t> Source;
        /// <summary>Where packets go; empty only measures (the asm's flag at 0x007a810d).</summary>
        std::span<uint8_t> Buffer;
        /// <summary>The next output byte, as an offset from <see cref="Buffer"/> (0x007a811d).</summary>
        size_t Out = 0;
        /// <summary>The first pixel of the row being encoded (0x007a8115).</summary>
        size_t Row = 0;
        /// <summary>The first pixel not yet encoded (0x007a8121).</summary>
        size_t PacketStart = 0;
        /// <summary>Where the scan stood when the flush was asked for (0x007a8119).</summary>
        size_t ScanPos = 0;
        /// <summary>Transparent pixels waiting to be written as skips (0x007a8111).</summary>
        int32_t PendingSkip = 0;

        void Put(uint8_t value)
        {
            if (Out < Buffer.size())
            {
                Buffer[Out] = value;
            }

            ++Out;
        }

        void EmitSkips()
        {
            while (PendingSkip != 0)
            {
                const int32_t count = std::min(PendingSkip, 0xff);
                PendingSkip -= count;
                Put(1);
                Put(static_cast<uint8_t>(count));
                PacketStart += static_cast<size_t>(count);
            }
        }

        /// <summary>
        /// FlushPacket: 0 starts a row, 1 writes the pixels from PacketStart to
        /// ScanPos - <paramref name="holdBack"/> as literals, 2 as runs of their first colour, 3 records them as
        /// pending skips, 4 ends the row.
        /// </summary>
        void Flush(int32_t kind, int32_t holdBack)
        {
            switch (kind)
            {
                case 0:
                {
                    PendingSkip = 0;
                    PacketStart = ScanPos;
                    break;
                }
                case 1:
                {
                    EmitSkips();
                    int32_t length = static_cast<int32_t>(ScanPos - PacketStart) - holdBack;

                    while (length != 0)
                    {
                        const int32_t count = std::min(length, 0x7f);
                        Put(static_cast<uint8_t>(count * 2 + 1));

                        for (int32_t i = 0; i < count; ++i)
                        {
                            Put(Source[PacketStart + static_cast<size_t>(i)]);
                        }

                        PacketStart += static_cast<size_t>(count);
                        length -= count;
                    }
                    break;
                }

                case 2:
                {
                    EmitSkips();
                    int32_t length = static_cast<int32_t>(ScanPos - PacketStart) - holdBack;

                    while (length != 0)
                    {
                        const int32_t count = std::min(length, 0x7f);
                        Put(static_cast<uint8_t>(count * 2));
                        Put(Source[PacketStart]);
                        PacketStart += static_cast<size_t>(count);
                        length -= count;
                    }
                    break;
                }

                case 3:
                    PendingSkip = static_cast<int32_t>(ScanPos - PacketStart) - holdBack;
                    break;
                case 4:
                    Put(0);
                    break;
                default:
                    break;
            }
        }

        /// <summary>
        /// ScanLine: encodes <paramref name="width"/> pixels from <see cref="Row"/>. Pixels
        /// of <paramref name="transparent"/> become skips (trailing ones are dropped); three or more equal pixels, or
        /// two starting a packet, become a run; anything else literals.
        /// </summary>
        void ScanLine(int32_t width, uint8_t transparent)
        {
            enum State
            {
                Literal = 1,
                Run = 2,
                Transparent = 3,
                Start = 5
            };

            size_t p = Row;
            ScanPos = p;
            Flush(0, 0);
            int32_t state = Start;
            int32_t left = width;

            if (left != 0)
            {
                uint8_t current = Source[p++];
                --left;
                // Which state to enter next; each state ends by setting it or by running out of pixels.
                int32_t next = current == transparent ? Transparent : Literal;

                while (left >= 0)
                {
                    if (next == Literal)
                    {
                        state = Literal;

                        if (left == 0)
                        {
                            break;
                        }

                        uint8_t pixel = Source[p++];
                        --left;
                        bool same = pixel == current;
                        current = pixel;

                        if (same)
                        {
                            next = Run; // two equal pixels start the packet: a run
                            continue;
                        }

                        if (current == transparent)
                        {
                            ScanPos = p;
                            Flush(Literal, 1);
                            next = Transparent;
                            continue;
                        }

                        // Collect literals until a transparent pixel or three equal ones.
                        next = 0;

                        while (next == 0)
                        {
                            if (left == 0)
                            {
                                break;
                            }

                            pixel = Source[p++];
                            --left;
                            same = pixel == current;
                            current = pixel;

                            if (current == transparent)
                            {
                                ScanPos = p;
                                Flush(Literal, 1);
                                next = Transparent;
                                break;
                            }

                            if (!same)
                            {
                                continue;
                            }

                            if (left == 0)
                            {
                                break;
                            }

                            pixel = Source[p++];
                            --left;
                            same = pixel == current;
                            current = pixel;

                            if (current == transparent)
                            {
                                ScanPos = p;
                                Flush(Literal, 1);
                                next = Transparent;
                                break;
                            }

                            if (!same)
                            {
                                continue;
                            }

                            ScanPos = p;
                            Flush(Literal, 3);
                            next = Run;
                        }

                        if (next == 0)
                        {
                            break; // out of pixels inside a literal
                        }
                    }
                    else if (next == Run)
                    {
                        state = Run;
                        bool ended = true;

                        while (left != 0)
                        {
                            const uint8_t pixel = Source[p++];
                            --left;

                            if (pixel == current)
                            {
                                continue;
                            }

                            current = pixel;
                            ScanPos = p;
                            Flush(Run, 1);
                            next = current == transparent ? Transparent : Literal;
                            ended = false;
                            break;
                        }

                        if (ended)
                        {
                            break;
                        }
                    }
                    else
                    {
                        state = Transparent;
                        bool ended = true;

                        while (left != 0)
                        {
                            const uint8_t pixel = Source[p++];
                            --left;

                            if (pixel == current)
                            {
                                continue;
                            }

                            current = pixel;
                            ScanPos = p;
                            Flush(Transparent, 1);
                            next = Literal;
                            ended = false;
                            break;
                        }

                        if (ended)
                        {
                            break;
                        }
                    }
                }
            }

            ScanPos = p;
            Flush(state, 0);
            Flush(4, 0);
        }
    };

    void Write32(std::span<uint8_t> out, size_t offset, int32_t value)
    {
        std::memcpy(out.subspan(offset, 4).data(), &value, 4);
    }
}

int32_t VfxShapeScanAsm(const MCPane& pane, uint8_t transparentColor, int32_t hotX, int32_t hotY,
                        std::span<uint8_t> buffer)
{
    MCVfxClip clip;
    const int32_t status = MCVfxClipPane(pane, clip);

    if (status != 0)
    {
        return status;
    }

    // Port: a view has no pixels to read.
    if (!clip.HasPixels())
    {
        return VfxErrBadWindow;
    }

    if (!buffer.empty())
    {
        // Bounds and origin words; the extent is the pane's (unclipped) size less one.
        const uint32_t bounds =
            (static_cast<uint32_t>(pane.X1 - pane.X0) << 16) | static_cast<uint16_t>(pane.Y1 - pane.Y0);
        const uint32_t origin = (static_cast<uint32_t>(hotX) << 16) | static_cast<uint16_t>(hotY);
        Write32(buffer, 0x00, static_cast<int32_t>(bounds));
        Write32(buffer, 0x04, static_cast<int32_t>(origin));
        Write32(buffer, 0x08, 0);
        Write32(buffer, 0x0c, 0);
        Write32(buffer, 0x10, -1);
        Write32(buffer, 0x14, -1);
    }

    hotX += clip.PaneX;
    hotY += clip.PaneY;
    const int32_t width = clip.X1 + 1 - clip.X0;

    // Pass 1: the bounding box of the non-transparent pixels.
    int32_t minX = INT32_MAX;
    int32_t minY = INT32_MAX;
    int32_t maxX = -INT32_MAX;
    int32_t maxY = -INT32_MAX;

    for (int32_t y = clip.Y0; y <= clip.Y1; ++y)
    {
        const std::span<const uint8_t> row = clip.Row(clip.X0, y);
        int32_t first = 0;

        while (first < width && row[static_cast<size_t>(first)] == transparentColor)
        {
            ++first;
        }

        if (first == width)
        {
            continue;
        }

        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
        minX = std::min(minX, clip.X0 + first);
        int32_t last = width - 1;

        while (row[static_cast<size_t>(last)] == transparentColor)
        {
            --last;
        }

        maxX = std::max(maxX, clip.X0 + last);
    }

    // (An all-transparent pane leaves the sentinels, and the asm's 32-bit arithmetic wraps: hence the unsigned sums.)
    const auto wrap = [](int32_t a, int32_t b)
    { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); };

    if (!buffer.empty())
    {
        Write32(buffer, 0x08, wrap(minX, hotX));
        Write32(buffer, 0x0c, wrap(minY, hotY));
        Write32(buffer, 0x10, wrap(maxX, hotX));
        Write32(buffer, 0x14, wrap(maxY, hotY));
    }

    // Pass 2: encode the rows of the bounding box.
    MCShapeScanner scanner;
    scanner.Source = clip.Target().Bytes();
    scanner.Buffer = buffer;
    scanner.Out = 0x18;
    const int32_t rowWidth = wrap(maxX, minX) + 1;

    for (int32_t y = minY; y <= maxY; ++y)
    {
        scanner.Row = static_cast<size_t>(y) * static_cast<size_t>(clip.Stride) + static_cast<size_t>(minX);
        scanner.ScanLine(rowWidth, transparentColor);
    }

    return static_cast<int32_t>(scanner.Out);
}

int32_t VfxShapeBounds(std::span<const uint8_t> shapeTable, int32_t shapeNum)
{
    return MCVfxRead32(MCVfxShape(shapeTable, shapeNum));
}

int32_t VfxShapeResolution(std::span<const uint8_t> shapeTable, int32_t shapeNum)
{
    const MCShapeInfo shape = ReadShape(shapeTable, shapeNum);
    const uint32_t width = static_cast<uint32_t>(shape.XMax - shape.XMin + 1);
    const uint32_t height = static_cast<uint32_t>(shape.YMax - shape.YMin + 1);
    return static_cast<int32_t>((width << 16) | (height & 0xffff));
}

int32_t VfxShapeMinxy(std::span<const uint8_t> shapeTable, int32_t shapeNum)
{
    const MCShapeInfo shape = ReadShape(shapeTable, shapeNum);
    return static_cast<int32_t>((static_cast<uint32_t>(shape.XMin) << 16) | static_cast<uint16_t>(shape.YMin));
}

int32_t VfxShapeCount(std::span<const uint8_t> shapeTable)
{
    return MCVfxRead32(shapeTable, 4);
}

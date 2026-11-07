#include "stdafx.h"
#include "platform/MCSoftwareRenderer.h"
#include "vfx/MCVfxClip.h"

// The software renderer's polygons (vfx3d.asm) and VFX_shape_transform's mapped quadrilateral (vfxa.asm).
//
// vfx3d's fillers share one scheme, reproduced here once:
//
// - Vertices are relative to the clipped pane's corner, and the polygon is clipped to 0..xmax, 0..ymax from there.
// - Each vertex gets four outcode bits (x < 0, x > xmax, y < 0, y > ymax); a bit every vertex has rejects the
//   polygon.
// - The top vertex is the one with the smallest y (the last of equals). A left edge walks the vertex list backwards
//   from it, a right edge forwards. Edge x is 16.16 with +0.5 added at the vertex (x << 16 | 0x8000), stepped by
//   dx * 65536 / dy; interpolated values (colour, u, v) start at the vertex's value + 0x8000.
// - Rows run from the top vertex's y to the bottom vertex's y inclusive (clipped to the pane). On the last row the
//   edges are only stepped, never switched. Spans are filled from the smaller edge x >> 16 to the larger inclusive.

namespace
{
    /// <summary>The clipped pane a polygon is drawn into.</summary>
    struct PolyTarget
    {
        /// <summary>The window pixel at the clipped pane's top-left corner (none when only spans are walked).</summary>
        MCOptionalRef<uint8_t> Base;
        /// <summary>The window's row length.</summary>
        int32_t Stride;
        /// <summary>The clipped pane's last column and row, relative to its corner.</summary>
        int32_t XMax;
        int32_t YMax;
    };

    /// <summary>
    /// An edge's x slope: <c>((int16)dx &lt;&lt; 32) / (dy &lt;&lt; 16)</c>, as the asm divides it (only dx's low 16 bits
    /// take part).
    /// </summary>
    int32_t EdgeXSlope(int32_t dx, int32_t dy)
    {
        const int64_t numerator = static_cast<int64_t>(static_cast<int16_t>(dx)) * 4294967296LL;
        const int32_t divisor = static_cast<int32_t>(static_cast<uint32_t>(dy) << 16);
        return static_cast<int32_t>(numerator / divisor);
    }

    /// <summary>A 16.16 value's slope over <paramref name="steps"/>: <c>(d &lt;&lt; 16) / (steps &lt;&lt; 16)</c> in 64 bits.</summary>
    int32_t ValueSlope(int32_t d, int32_t steps)
    {
        const int64_t numerator = static_cast<int64_t>(d) * 65536;
        const int32_t divisor = static_cast<int32_t>(static_cast<uint32_t>(steps) << 16);
        // Port fix: the asm's idiv faults when the quotient doesn't fit 32 bits; the port truncates.
        return static_cast<int32_t>(numerator / divisor);
    }

    /// <summary><c>imul</c> then <c>shrd 16</c>: the low 32 bits of the 64-bit product shifted right by 16.</summary>
    int32_t MulShift16(int32_t a, int32_t b)
    {
        return static_cast<int32_t>(static_cast<uint64_t>(static_cast<int64_t>(a) * b) >> 16);
    }

    /// <summary>Shifts left by 16 in 32 bits, as the asm's <c>shl reg, 16</c>.</summary>
    int32_t Shl16(int32_t a)
    {
        return static_cast<int32_t>(static_cast<uint32_t>(a) << 16);
    }

    /// <summary>One side's edge while a polygon is walked.</summary>
    template <int N> struct Edge
    {
        /// <summary>The vertex the edge starts at (0x007a9aec / 0x007a9af0), its index in the list.</summary>
        size_t Start;
        /// <summary>The vertex it runs to (0x007a9af4 / 0x007a9af8).</summary>
        size_t Next;
        /// <summary>Rows left on the edge.</summary>
        int32_t Count;
        int32_t X;
        int32_t DX;
        int32_t A[N > 0 ? N : 1];
        int32_t DA[N > 0 ? N : 1];
    };

    /// <summary>The value at byte offset <paramref name="offset"/> of a vertex (8 colour, 12 u, 16 v).</summary>
    int32_t VertexValue(const MCScreenVertex& v, int offset)
    {
        switch (offset)
        {
            case 8:
                return v.C;
            case 12:
                return v.U;
            case 16:
                return v.V;
            default:
                return v.W;
        }
    }

    /// <summary>
    /// Walks a convex polygon row by row, calling <paramref name="span"/> with the row (relative to the clipped pane's
    /// corner), the two edges, the number of rows clipped off the top and the row's index.
    /// </summary>
    /// <typeparam name="N">The number of interpolated values (0 to 2).</typeparam>
    template <int N, typename Span>
    void WalkPolygon(const PolyTarget& target, std::span<const MCScreenVertex> vertices,
                     const int (&offsets)[N > 0 ? N : 1], Span&& span)
    {
        const auto vcnt = static_cast<int32_t>(vertices.size());

        // Port fix: the asm loops forever on an empty vertex list.
        if (vcnt <= 0)
        {
            return;
        }

        uint32_t allOut = 0xf;
        int32_t minY = 0x7fff;
        int32_t maxY = -0x8000;
        std::optional<size_t> top;

        for (size_t index = 0; index < vertices.size(); ++index)
        {
            const MCScreenVertex& v = vertices[index];
            uint32_t code = 0;
            code = code << 1 | static_cast<uint32_t>(v.X) >> 31;
            code = code << 1 | static_cast<uint32_t>(target.XMax - v.X) >> 31;
            code = code << 1 | static_cast<uint32_t>(v.Y) >> 31;
            code = code << 1 | static_cast<uint32_t>(target.YMax - v.Y) >> 31;

            if (v.Y <= minY)
            {
                minY = v.Y;
                top = index;
            }

            if (v.Y >= maxY)
            {
                maxY = v.Y;
            }

            allOut &= code;
        }

        if (allOut != 0)
        {
            return;
        }

        // Port fix: every vertex above 0x7fff leaves no top vertex (the asm would read through a null pointer).
        if (!top)
        {
            return;
        }

        if (maxY == minY)
        {
            return;
        }

        auto setupEdge = [&](Edge<N>& edge, int32_t dy)
        {
            const MCScreenVertex& start = vertices[edge.Start];
            const MCScreenVertex& next = vertices[edge.Next];
            edge.Count = dy;
            edge.DX = EdgeXSlope(next.X - start.X, dy);

            for (int i = 0; i < N; ++i)
            {
                edge.DA[i] = ValueSlope(VertexValue(next, offsets[i]) - VertexValue(start, offsets[i]), dy);
            }

            edge.X = Shl16(start.X) + 0x8000;

            for (int i = 0; i < N; ++i)
            {
                edge.A[i] = VertexValue(start, offsets[i]) + 0x8000;
            }
        };

        const auto last = static_cast<size_t>(vcnt - 1);
        auto stepBack = [&](size_t v) { return v == 0 ? last : v - 1; };
        auto stepForward = [&](size_t v) { return v + 1 > last ? 0 : v + 1; };

        // The first edges: skip those wholly above the pane and the horizontal ones. The guard is the port's: the asm
        // relies on the polygon reaching y >= 0, which the outcodes guarantee, to end these loops.
        Edge<N> left{};
        Edge<N> right{};
        left.Next = *top;
        right.Next = left.Next;
        int guard = 0;

        for (;;)
        {
            if (++guard > 2 * vcnt + 2)
            {
                return;
            }

            left.Start = left.Next;
            left.Next = stepBack(left.Start);
            const int32_t y0 = vertices[left.Start].Y;
            const int32_t y1 = vertices[left.Next].Y;

            if (y0 < 0 && y1 <= 0)
            {
                continue;
            }

            const int32_t dy = y1 - y0;

            if (dy == 0)
            {
                continue;
            }

            // A first edge running upwards (a non-convex polygon) ends it. OB-127: the asm's dithered Gouraud filler
            // walked on along it with a negative row count.
            if (dy < 0)
            {
                return;
            }

            setupEdge(left, dy);
            break;
        }

        guard = 0;

        for (;;)
        {
            if (++guard > 2 * vcnt + 2)
            {
                return;
            }

            right.Start = right.Next;
            right.Next = stepForward(right.Start);
            const int32_t y0 = vertices[right.Start].Y;
            const int32_t y1 = vertices[right.Next].Y;

            if (y0 < 0 && y1 <= 0)
            {
                continue;
            }

            const int32_t dy = y1 - y0;

            if (dy == 0)
            {
                continue;
            }

            if (dy < 0)
            {
                return;
            }

            setupEdge(right, dy);
            break;
        }

        int32_t rows = target.YMax - minY;

        if (maxY - target.YMax <= 0)
        {
            rows += maxY - target.YMax;
        }

        int32_t clippedTop = 0;

        if (minY < 0)
        {
            clippedTop = -minY;
            rows -= clippedTop;
            minY = 0;

            for (Edge<N>& edge : {std::ref(left), std::ref(right)})
            {
                const int32_t n = 0 - vertices[edge.Start].Y;
                edge.Count -= n;
                edge.X += MulShift16(Shl16(n), edge.DX);

                for (int i = 0; i < N; ++i)
                {
                    edge.A[i] += MulShift16(Shl16(n), edge.DA[i]);
                }
            }
        }

        // A later edge: one running upwards ends the polygon; a horizontal one counts as one row.
        auto nextEdge = [&](Edge<N>& edge, bool backwards) -> bool
        {
            edge.Start = edge.Next;
            edge.Next = backwards ? stepBack(edge.Start) : stepForward(edge.Start);
            int32_t dy = vertices[edge.Next].Y - vertices[edge.Start].Y;

            if (dy < 0)
            {
                return false;
            }

            if (dy == 0)
            {
                dy = 1;
            }

            setupEdge(edge, dy);
            return true;
        };

        for (int32_t rowIndex = 0;; ++rowIndex)
        {
            span(minY + rowIndex, left, right, clippedTop, rowIndex);
            --rows;

            if (rows < 0)
            {
                return;
            }

            if (rows == 0)
            {
                left.X += left.DX;

                for (int i = 0; i < N; ++i)
                {
                    left.A[i] += left.DA[i];
                }

                right.X += right.DX;

                for (int i = 0; i < N; ++i)
                {
                    right.A[i] += right.DA[i];
                }

                continue;
            }

            if (--left.Count == 0)
            {
                if (!nextEdge(left, true))
                {
                    return;
                }
            }
            else
            {
                left.X += left.DX;

                for (int i = 0; i < N; ++i)
                {
                    left.A[i] += left.DA[i];
                }
            }

            if (--right.Count == 0)
            {
                if (!nextEdge(right, false))
                {
                    return;
                }
            }
            else
            {
                right.X += right.DX;

                for (int i = 0; i < N; ++i)
                {
                    right.A[i] += right.DA[i];
                }
            }
        }
    }

    /// <summary>
    /// A span's extent, clipped to 0..xmax: the pixels [<paramref name="l"/>, <paramref name="l"/> +
    /// <paramref name="count"/>), and how many were cut off its left end.
    /// </summary>
    /// <returns>False when the span is wholly outside.</returns>
    /// <remarks>
    /// The asm has an unclipped path for polygons whose vertices are all inside horizontally; it writes the same
    /// pixels whenever the spans stay inside, which such spans do, so the port always takes the clipped one.
    /// </remarks>
    bool ClipSpan(int32_t xa, int32_t xb, int32_t xmax, int32_t& l, int32_t& r, int32_t& count, int32_t& cut)
    {
        l = xa >> 16;

        if (l > xmax)
        {
            return false;
        }

        r = xb >> 16;

        if (r < 0)
        {
            return false;
        }

        count = r - l + 1;
        cut = 0;

        if (l < 0)
        {
            cut = -l;
            count -= cut;
            l = 0;
        }

        if (r > xmax)
        {
            count -= r - xmax;
        }

        return true;
    }

    /// <summary>
    /// The dithered Gouraud span shared by VFX_dithered_Gouraud_polygon and VFX_illuminate_polygon: pixel values
    /// alternate between two accumulators (colour + dither, colour + 0, swapped every row), each stepped by the slope
    /// over half the span per pixel pair.
    /// </summary>
    template <bool Add>
    void DitheredSpan(std::span<uint8_t> row, int32_t xa, int32_t xb, int32_t ca, int32_t cb, int32_t xmax,
                      int32_t ditherA, int32_t ditherB, int32_t& spanSlope)
    {
        if (!(xb > xa))
        {
            std::swap(xa, xb);
            std::swap(ca, cb);
        }

        int32_t l = xa >> 16;

        if (l > xmax)
        {
            return;
        }

        int32_t r = xb >> 16;

        if (r < 0)
        {
            return;
        }

        const int32_t n = r - l;
        int32_t count = n + 1;

        if (count != 1)
        {
            int32_t half = n >> 1;

            if (half == 0)
            {
                half = 1;
            }

            spanSlope = ValueSlope(cb - ca, half);
        }

        const int32_t slope = spanSlope;
        int32_t cut = 0;
        int32_t c = ca;
        // Where the span's first drawn pixel lies in the row.
        ptrdiff_t p = l;

        if (l < 0)
        {
            cut = -l;
            p += cut;
            count -= cut;
            c += MulShift16(static_cast<int32_t>(static_cast<uint32_t>(cut & ~1) << 15), slope);
        }

        if (r > xmax)
        {
            count -= r - xmax;
        }

        uint32_t accA = static_cast<uint32_t>(c + ditherA);
        uint32_t accB = static_cast<uint32_t>(c + ditherB);
        // The first pixel is A's unless an odd number of pixels was cut off the left, which puts B first.
        uint32_t first = accA;
        uint32_t second = accB;

        if (cut & 1)
        {
            first = accB;
            second = accA + static_cast<uint32_t>(slope);
        }

        auto pixel = [&](int32_t j) -> uint8_t
        { return static_cast<uint8_t>((j & 1) == 0 ? first >> 16 : second >> 16); };
        auto advance = [&](int32_t j)
        {
            if (j & 1)
            {
                first += static_cast<uint32_t>(slope);
                second += static_cast<uint32_t>(slope);
            }
        };

        if constexpr (!Add)
        {
            for (int32_t j = 0; j < count; ++j)
            {
                row[static_cast<size_t>(p + j)] = pixel(j);
                advance(j);
            }
        }
        else
        {
            // The asm adds the values with byte adds for a lone pixel and 16-bit adds for pairs, so a pair's low byte
            // carries into its high byte. Pairs start at the first even address, or at the first pixel when an odd
            // number was cut off the left.
            int32_t j = 0;

            const auto at = [&](int32_t k) -> uint8_t& { return row[static_cast<size_t>(p + k)]; };

            if ((cut & 1) == 0 && (reinterpret_cast<uintptr_t>(&at(0)) & 1) != 0)
            {
                at(0) = static_cast<uint8_t>(at(0) + pixel(0));
                advance(0);
                j = 1;
            }

            for (; j + 1 < count; j += 2)
            {
                const uint8_t lo = pixel(j);
                advance(j);
                const uint8_t hi = pixel(j + 1);
                advance(j + 1);
                const uint16_t word = static_cast<uint16_t>(at(j) | at(j + 1) << 8);
                const uint16_t sum = static_cast<uint16_t>(word + (lo | hi << 8));
                at(j) = static_cast<uint8_t>(sum);
                at(j + 1) = static_cast<uint8_t>(sum >> 8);
            }

            if (j < count)
            {
                at(j) = static_cast<uint8_t>(at(j) + pixel(j));
            }
        }
    }

    /// <summary>VFX_dithered_Gouraud_polygon and VFX_illuminate_polygon, which differ only in storing or adding.</summary>
    template <bool Add>
    void DitheredPolygon(const PolyTarget& target, const MCPolygonCommand& command, int32_t& spanSlope)
    {
        static const int offsets[1] = {8};
        const int32_t ditherAmount = command.DitherAmount;
        WalkPolygon<1>(target, command.Vertices, offsets,
                       [&](int32_t y, const Edge<1>& left, const Edge<1>& right, int32_t clippedTop, int32_t rowIndex)
                       {
                           const std::span<uint8_t> row(
                               &(&target.Base->get())[static_cast<intptr_t>(y) * target.Stride],
                               static_cast<size_t>(target.XMax + 1));
                           // The dither goes on A for the rows at an even distance from the top vertex.
                           const bool swapped = ((clippedTop + rowIndex) & 1) != 0;
                           const int32_t ditherA = swapped ? 0 : ditherAmount;
                           const int32_t ditherB = swapped ? ditherAmount : 0;
                           DitheredSpan<Add>(row, left.X, right.X, left.A[0], right.A[0], target.XMax, ditherA, ditherB,
                                             spanSlope);
                       });
    }

    /// <summary>
    /// The spans of VFX_map_polygon: each row's texel walk, with the step table and slopes kept between spans (a
    /// one-pixel span leaves them as they were).
    /// </summary>
    void MapSpans(const PolyTarget& target, const MCPolygonCommand& command, MCSpanState& state,
                  const std::function<void(const MCSpan&)>& emit)
    {
        if (!command.Texture.has_value())
        {
            return;
        }

        const int32_t texStride = command.Texture->get().XMax + 1;
        int32_t (&stepTable)[4] = state.MapSteps;
        int32_t& duSpan = state.MapDu;
        int32_t& dvSpan = state.MapDv;

        static const int offsets[2] = {12, 16};
        WalkPolygon<2>(
            target, command.Vertices, offsets,
            [&](int32_t y, const Edge<2>& left, const Edge<2>& right, int32_t, int32_t)
            {
                int32_t xa = left.X;
                int32_t xb = right.X;
                int32_t ua = left.A[0];
                int32_t ub = right.A[0];
                int32_t va = left.A[1];
                int32_t vb = right.A[1];

                if (!(xb > xa))
                {
                    std::swap(xa, xb);
                    std::swap(ua, ub);
                    std::swap(va, vb);
                }

                int32_t l = xa >> 16;

                if (l > target.XMax)
                {
                    return;
                }

                int32_t r = xb >> 16;

                if (r < 0)
                {
                    return;
                }

                int32_t u = ua;
                int32_t v = va;

                if (r != l)
                {
                    const int32_t n = r - l;
                    duSpan = ValueSlope(ub - ua, n);
                    // The whole texel steps of u and v, truncated towards zero, and the extra step a
                    // fraction's carry (or borrow, for a negative slope) adds.
                    int32_t uInt = static_cast<int16_t>(static_cast<uint32_t>(duSpan) >> 16);
                    int32_t uCarry = 1;

                    if (uInt < 0)
                    {
                        uCarry = -1;

                        if ((duSpan & 0xffff) != 0)
                        {
                            ++uInt;
                        }
                    }

                    uCarry += uInt;
                    dvSpan = ValueSlope(vb - va, n);
                    int32_t vInt = static_cast<int32_t>(static_cast<uint32_t>(dvSpan) >> 16) & 0xffff;
                    int32_t vCarry = texStride;

                    if (vInt & 0x8000)
                    {
                        vCarry = -vCarry;

                        if ((dvSpan & 0xffff) != 0)
                        {
                            ++vInt;
                        }
                    }

                    // OB-126: the asm's row step was a 16-bit product (imul dx; cwde).
                    const int32_t rowStep = texStride * static_cast<int16_t>(vInt);
                    stepTable[0] = uInt + rowStep;
                    stepTable[1] = stepTable[0] + vCarry;
                    stepTable[2] = uCarry + rowStep;
                    stepTable[3] = stepTable[2] + vCarry;

                    if (l < 0)
                    {
                        const int32_t cut = -l;
                        l = 0;
                        u += MulShift16(Shl16(cut), duSpan);
                        v += MulShift16(Shl16(cut), dvSpan);
                    }

                    if (r > target.XMax)
                    {
                        r = target.XMax;
                    }
                }

                MCSpan span;
                span.Y = command.OriginY + y;
                span.X0 = command.OriginX + l;
                span.X1 = command.OriginX + r;
                span.Texel =
                    static_cast<int64_t>(static_cast<uint32_t>(v) >> 16) * texStride + (static_cast<uint32_t>(u) >> 16);

                // The fractions: kept complemented for a negative slope, so that a borrow shows as a carry.
                span.UFraction = (duSpan < 0 ? ~static_cast<uint32_t>(u) : static_cast<uint32_t>(u)) & 0xffff;
                span.UStep = (duSpan < 0 ? 0u - static_cast<uint32_t>(duSpan) : static_cast<uint32_t>(duSpan)) & 0xffff;
                span.VFraction = (dvSpan < 0 ? ~static_cast<uint32_t>(v) : static_cast<uint32_t>(v)) & 0xffff;
                span.VStep = (dvSpan < 0 ? 0u - static_cast<uint32_t>(dvSpan) : static_cast<uint32_t>(dvSpan)) & 0xffff;
                span.Step0 = stepTable[0];
                span.StepU = stepTable[2] - stepTable[0];
                span.StepV = stepTable[1] - stepTable[0];
                emit(span);
            });
    }
}

void MCPolygonSpans(const MCPolygonCommand& command, MCSpanState& state, const std::function<void(const MCSpan&)>& emit)
{
    PolyTarget poly{std::nullopt, 0, command.XMax, command.YMax};
    const std::span<const MCScreenVertex> vertices = command.Vertices;

    switch (command.Kind)
    {
        case MCPolygonKind::Flat:
        {
            static const int offsets[1] = {0};
            WalkPolygon<0>(poly, vertices, offsets,
                           [&](int32_t y, const Edge<0>& left, const Edge<0>& right, int32_t, int32_t)
                           {
                               int32_t xa = left.X;
                               int32_t xb = right.X;

                               if (!(xb > xa))
                               {
                                   std::swap(xa, xb);
                               }

                               int32_t l;
                               int32_t r;
                               int32_t count;
                               int32_t cut;

                               if (!ClipSpan(xa, xb, poly.XMax, l, r, count, cut))
                               {
                                   return;
                               }

                               MCSpan span;
                               span.Y = command.OriginY + y;
                               span.X0 = command.OriginX + l;
                               span.X1 = command.OriginX + l + count - 1;
                               emit(span);
                           });
            break;
        }

        case MCPolygonKind::Gouraud:
        {
            static const int offsets[1] = {8};
            WalkPolygon<1>(poly, vertices, offsets,
                           [&](int32_t y, const Edge<1>& left, const Edge<1>& right, int32_t, int32_t)
                           {
                               int32_t xa = left.X;
                               int32_t xb = right.X;
                               int32_t ca = left.A[0];
                               int32_t cb = right.A[0];

                               if (!(xb > xa))
                               {
                                   std::swap(xa, xb);
                                   std::swap(ca, cb);
                               }

                               int32_t l = xa >> 16;

                               if (l > poly.XMax)
                               {
                                   return;
                               }

                               int32_t r = xb >> 16;

                               if (r < 0)
                               {
                                   return;
                               }

                               int32_t count = r - l + 1;

                               if (count != 1)
                               {
                                   state.SpanSlope = ValueSlope(cb - ca, r - l);
                               }

                               const int32_t slope = state.SpanSlope;
                               uint32_t c = static_cast<uint32_t>(ca);

                               if (l < 0)
                               {
                                   const int32_t cut = -l;
                                   count -= cut;
                                   l = 0;
                                   c += static_cast<uint32_t>(MulShift16(Shl16(cut), slope));
                               }

                               if (r > poly.XMax)
                               {
                                   count -= r - poly.XMax;
                               }

                               // The asm keeps the colour as an 8-bit integer and a 16-bit fraction; bits above 23
                               // never reach a pixel, so a 32-bit accumulator gives the same bytes.
                               MCSpan span;
                               span.Y = command.OriginY + y;
                               span.X0 = command.OriginX + l;
                               span.X1 = command.OriginX + l + count - 1;
                               span.Value = c;
                               span.Slope = slope;
                               emit(span);
                           });
            break;
        }

        case MCPolygonKind::Translate:
        {
            static const int offsets[1] = {0};
            WalkPolygon<0>(poly, vertices, offsets,
                           [&](int32_t y, const Edge<0>& left, const Edge<0>& right, int32_t, int32_t)
                           {
                               int32_t xa = left.X;
                               int32_t xb = right.X;

                               if (!(xb > xa))
                               {
                                   std::swap(xa, xb);
                               }

                               int32_t l = xa >> 16;

                               if (l > poly.XMax)
                               {
                                   return;
                               }

                               int32_t r = xb >> 16;

                               if (r < 0)
                               {
                                   return;
                               }

                               if (r != l)
                               {
                                   if (l < 0)
                                   {
                                       l = 0;
                                   }

                                   if (r > poly.XMax)
                                   {
                                       r = poly.XMax;
                                   }
                               }

                               MCSpan span;
                               span.Y = command.OriginY + y;
                               span.X0 = command.OriginX + l;
                               span.X1 = command.OriginX + r;
                               emit(span);
                           });
            break;
        }

        case MCPolygonKind::Map:
            MapSpans(poly, command, state, emit);
            break;
        case MCPolygonKind::DitheredGouraud:
        case MCPolygonKind::Illuminate:
            break;
    }
}

void MCSoftwareRenderer::Polygon(MCWindow& target, const MCPolygonCommand& command)
{
    const int32_t stride = target.XMax + 1;
    const std::span<uint8_t> pixels = target.Bytes();
    const auto at = [&](int32_t x, int32_t y) { return static_cast<size_t>(static_cast<intptr_t>(y) * stride + x); };

    switch (command.Kind)
    {
        case MCPolygonKind::Flat:
        {
            if (command.Vertices.empty())
            {
                return;
            }

            // Original behaviour: the asm builds a dword of the colour from (c + 0x8000) >> 16 and fills with it, so
            // colours above 255 leak their high bits into some pixels of the dword stores; the port fills with the
            // low byte.
            const uint8_t color = static_cast<uint8_t>((static_cast<uint32_t>(command.Vertices[0].C) + 0x8000) >> 16);
            MCPolygonSpans(command, _Spans,
                           [&](const MCSpan& span)
                           {
                               std::fill_n(pixels.begin() + static_cast<ptrdiff_t>(at(span.X0, span.Y)),
                                           span.X1 - span.X0 + 1, color);
                           });
            break;
        }

        case MCPolygonKind::Gouraud:
        {
            MCPolygonSpans(command, _Spans,
                           [&](const MCSpan& span)
                           {
                               size_t p = at(span.X0, span.Y);
                               uint32_t c = span.Value;

                               for (int32_t x = span.X0; x <= span.X1; ++x)
                               {
                                   pixels[p++] = static_cast<uint8_t>(c >> 16);
                                   c += static_cast<uint32_t>(span.Slope);
                               }
                           });
            break;
        }

        case MCPolygonKind::DitheredGouraud:
        case MCPolygonKind::Illuminate:
        {
            PolyTarget poly;
            poly.Stride = stride;
            poly.XMax = command.XMax;
            poly.YMax = command.YMax;
            poly.Base = pixels[at(command.OriginX, command.OriginY)];

            if (command.Kind == MCPolygonKind::DitheredGouraud)
            {
                DitheredPolygon<false>(poly, command, _Spans.SpanSlope);
            }
            else
            {
                DitheredPolygon<true>(poly, command, _Spans.SpanSlope);
            }
            break;
        }

        case MCPolygonKind::Translate:
        {
            const MCColorTable table = *command.Table;
            MCSeeThrough seeThrough(target);
            MCPolygonSpans(command, _Spans,
                           [&](const MCSpan& span)
                           {
                               for (size_t p = at(span.X0, span.Y); p <= at(span.X1, span.Y); ++p)
                               {
                                   seeThrough.Translate(pixels[p], table);
                               }
                           });
            break;
        }

        case MCPolygonKind::Map:
        {
            if (!command.Texture.has_value())
            {
                break;
            }

            const MCWindow& texture = command.Texture->get();
            NoteCpuRead(texture, "Polygon (map)");
            const std::span<const uint8_t> texels = texture.Bytes();
            const int64_t texSize = static_cast<int64_t>(texture.XMax + 1) * (texture.YMax + 1);
            const bool xlat = (command.MapFlags & VfxMapXlat) != 0;
            const bool transparent = (command.MapFlags & VfxMapTransparent) != 0;
            MCPolygonSpans(command, _Spans,
                           [&](const MCSpan& span)
                           {
                               size_t p = at(span.X0, span.Y);

                               for (int32_t j = 0; j <= span.X1 - span.X0; ++j, ++p)
                               {
                                   // Port fix: the asm reads wherever the texture coordinates point; the port skips
                                   // texels outside it.
                                   const int64_t offset = MCSpanTexel(span, j);

                                   if (offset >= 0 && offset < texSize)
                                   {
                                       uint8_t texel = texels[static_cast<size_t>(offset)];

                                       if (xlat)
                                       {
                                           texel = (*command.Table)[texel];
                                       }

                                       if (!transparent || texel != 0xff)
                                       {
                                           pixels[p] = texel;
                                       }
                                   }
                               }
                           });
            break;
        }
    }
}

namespace
{
    /// <summary>
    /// The asm's 16.16 divide: <c>(int64)numerator &lt;&lt; 16</c> by <c>(int32)(count &lt;&lt; 16)</c>, quotient kept to
    /// 32 bits. (idiv faulted on overflow in the original; the port truncates.)
    /// </summary>
    int32_t StepDivide(int32_t numerator, int32_t count)
    {
        const int64_t dividend = static_cast<int64_t>(numerator) * 65536;
        const int32_t divisor = static_cast<int32_t>(static_cast<uint32_t>(count) << 16);

        if (divisor == 0)
        {
            return 0; // Port fix: the original faulted.
        }

        return static_cast<int32_t>(dividend / divisor);
    }

    /// <summary>An edge's step: the difference of two integer coordinates, as 16.16 per row.</summary>
    int32_t EdgeStep(int32_t from, int32_t to, int32_t rows)
    {
        return StepDivide(static_cast<int32_t>(static_cast<uint32_t>(to - from) << 16), rows);
    }

    /// <summary>An integer coordinate as the 16.16 start of an edge (the pixel's centre).</summary>
    int32_t EdgeStart(int32_t value)
    {
        return static_cast<int32_t>(static_cast<uint32_t>(value) << 16) + 0x8000;
    }

    /// <summary>A 16.16 step times an integer count: <c>(step * (count &lt;&lt; 16)) &gt;&gt; 16</c>, low 32 bits.</summary>
    int32_t StepTimes(int32_t step, int32_t count)
    {
        const int64_t product = static_cast<int64_t>(step) * static_cast<int32_t>(static_cast<uint32_t>(count) << 16);
        return static_cast<int32_t>(static_cast<uint64_t>(product) >> 16);
    }

    /// <summary>One side of the quadrilateral being walked down: the 16.16 x, u, v and their per-row steps.</summary>
    struct QuadEdge
    {
        int32_t x;
        int32_t u;
        int32_t v;
        int32_t dx;
        int32_t du;
        int32_t dv;
        /// <summary>Rows left on this edge.</summary>
        int32_t count;
        /// <summary>The corner the edge starts at, and the one it runs to.</summary>
        int32_t from;
        int32_t to;
    };

    /// <summary>Sets up <paramref name="edge"/> from corner <paramref name="from"/> to <paramref name="to"/>.</summary>
    void SetupQuadEdge(const MCMapQuadVertex (&corners)[4], QuadEdge& edge, int32_t from, int32_t to, int32_t rows)
    {
        const MCMapQuadVertex& a = corners[from];
        const MCMapQuadVertex& b = corners[to];
        edge.count = rows;
        edge.dx = EdgeStep(a.X, b.X, rows);
        edge.du = EdgeStep(a.U, b.U, rows);
        edge.dv = EdgeStep(a.V, b.V, rows);
        edge.x = EdgeStart(a.X);
        edge.u = EdgeStart(a.U);
        edge.v = EdgeStart(a.V);
    }

    /// <summary>
    /// The integer part (towards zero) of a 16.16 span step, and whether the step is below zero (a carry of its
    /// fraction then steps one less).
    /// </summary>
    void SplitStep(int32_t step, int32_t& whole, bool& negative)
    {
        const uint32_t fraction = static_cast<uint32_t>(step) & 0xffff;
        int32_t integer = static_cast<int32_t>((static_cast<uint32_t>(step) >> 16) & 0xffff);
        negative = (integer & 0x8000) != 0;

        if (negative)
        {
            integer |= static_cast<int32_t>(0xffff0000);

            if (fraction != 0)
            {
                integer += 1;
            }
        }

        whole = integer;
    }
}

void MCMapQuadSpans(const MCMapQuadCommand& command, MCSpanState& state, const std::function<void(const MCSpan&)>& emit)
{
    const MCMapQuadVertex(&corners)[4] = command.Corners;
    const MCRect& clip = command.Clip;
    const int32_t textureStride = command.Texture.get().XMax + 1;

    // Outcodes, and the top (the last corner with the smallest y) and bottom.
    int32_t yTop = 0x7fff;
    int32_t yBottom = -0x8000;
    int32_t top = 0;
    uint32_t allOut = 0xf;

    for (int i = 0; i < 4; ++i)
    {
        const MCMapQuadVertex& vertex = corners[i];
        uint32_t code = 0;
        code = (code << 1) | ((vertex.X - clip.X0) < 0 ? 1u : 0u);
        code = (code << 1) | ((clip.X1 - vertex.X) < 0 ? 1u : 0u);
        code = (code << 1) | ((vertex.Y - clip.Y0) < 0 ? 1u : 0u);
        code = (code << 1) | ((clip.Y1 - vertex.Y) < 0 ? 1u : 0u);

        if (vertex.Y <= yTop)
        {
            yTop = vertex.Y;
            top = i;
        }

        if (vertex.Y >= yBottom)
        {
            yBottom = vertex.Y;
        }

        allOut &= code;
    }

    if (allOut != 0)
    {
        return;
    }

    if (yBottom == yTop)
    {
        return;
    }

    // The left side walks the corners backwards from the top, the right side forwards (which is really left is
    // settled per row). Edges wholly above the clip, and flat ones, are passed over.
    QuadEdge left;
    QuadEdge right;
    int32_t leftEnd = top;

    for (;;)
    {
        const int32_t from = leftEnd;
        const int32_t to = from == 0 ? 3 : from - 1;
        leftEnd = to;
        const int32_t fromY = corners[from].Y;
        const int32_t toY = corners[to].Y;

        if (fromY < clip.Y0 && toY <= clip.Y0)
        {
            continue;
        }

        if (toY - fromY == 0)
        {
            continue;
        }

        left.from = from;
        left.to = to;
        SetupQuadEdge(corners, left, from, to, toY - fromY);
        break;
    }

    int32_t rightEnd = top;

    for (;;)
    {
        const int32_t from = rightEnd;
        const int32_t to = from == 3 ? 0 : from + 1;
        rightEnd = to;
        const int32_t fromY = corners[from].Y;
        const int32_t toY = corners[to].Y;

        if (fromY < clip.Y0 && toY <= clip.Y0)
        {
            continue;
        }

        if (toY - fromY == 0)
        {
            continue;
        }

        right.from = from;
        right.to = to;
        SetupQuadEdge(corners, right, from, to, toY - fromY);
        break;
    }

    // Rows to draw after the first, clipped to the bottom and then the top.
    int32_t rowsLeft = clip.Y1 - yTop;

    if (yBottom - clip.Y1 <= 0)
    {
        rowsLeft += yBottom - clip.Y1;
    }

    int32_t y = yTop;

    if (clip.Y0 - yTop > 0)
    {
        rowsLeft -= clip.Y0 - yTop;
        y = clip.Y0;
        int32_t skip = clip.Y0 - corners[left.from].Y;
        left.count -= skip;
        left.x += StepTimes(left.dx, skip);
        left.u += StepTimes(left.du, skip);
        left.v += StepTimes(left.dv, skip);
        skip = clip.Y0 - corners[right.from].Y;
        right.count -= skip;
        right.x += StepTimes(right.dx, skip);
        right.u += StepTimes(right.du, skip);
        right.v += StepTimes(right.dv, skip);
    }

    // The span's steps persist between rows: a one-pixel span reuses the previous row's (as the asm's did).
    int32_t spanDu = 0;
    int32_t spanDv = 0;

    for (;; ++y)
    {
        // The span, ordered left to right.
        int32_t lx = left.x;
        int32_t lu = left.u;
        int32_t lv = left.v;
        int32_t rx = right.x;
        int32_t ru = right.u;
        int32_t rv = right.v;

        if (rx <= lx)
        {
            std::swap(lx, rx);
            std::swap(lu, ru);
            std::swap(lv, rv);
        }

        int32_t xl = lx >> 16;
        int32_t xr = rx >> 16;

        if (xl <= clip.X1 && xr >= clip.X0)
        {
            const int32_t width = xr - xl;

            if (width != 0)
            {
                spanDu = StepDivide(ru - lu, width);
                int32_t uWhole;
                bool uNegative;
                SplitStep(spanDu, uWhole, uNegative);
                const int32_t uCarry = uWhole + (uNegative ? -1 : 1);

                spanDv = StepDivide(rv - lv, width);
                int32_t vWhole;
                bool vNegative;
                SplitStep(spanDv, vWhole, vNegative);
                const int32_t vCarry = vNegative ? -textureStride : textureStride;
                // imul dx / cwde: the row offset is formed in 16 bits.
                const int32_t vOffset =
                    static_cast<int16_t>(static_cast<int16_t>(textureStride) * static_cast<int16_t>(vWhole));

                state.QuadSteps[0] = uWhole + vOffset;
                state.QuadSteps[1] = state.QuadSteps[0] + vCarry;
                state.QuadSteps[2] = uCarry + vOffset;
                state.QuadSteps[3] = state.QuadSteps[2] + vCarry;

                const int32_t clipLeft = clip.X0 - xl;

                if (clipLeft > 0)
                {
                    xl += clipLeft;
                    lu += StepTimes(spanDu, clipLeft);
                    lv += StepTimes(spanDv, clipLeft);
                }

                const int32_t clipRight = xr - clip.X1;

                if (clipRight > 0)
                {
                    xr -= clipRight;
                }
            }

            // Texel offset, and the fractional accumulators (the complement when stepping backwards).
            MCSpan span;
            span.Y = y;
            span.X0 = xl;
            span.X1 = xr;
            span.Texel = static_cast<int64_t>(static_cast<uint32_t>(lv) >> 16) * textureStride +
                         (static_cast<uint32_t>(lu) >> 16);
            span.UFraction = (spanDu < 0 ? ~static_cast<uint32_t>(lu) : static_cast<uint32_t>(lu)) & 0xffff;
            span.UStep = (spanDu < 0 ? 0u - static_cast<uint32_t>(spanDu) : static_cast<uint32_t>(spanDu)) & 0xffff;
            span.VFraction = (spanDv < 0 ? ~static_cast<uint32_t>(lv) : static_cast<uint32_t>(lv)) & 0xffff;
            span.VStep = (spanDv < 0 ? 0u - static_cast<uint32_t>(spanDv) : static_cast<uint32_t>(spanDv)) & 0xffff;
            span.Step0 = state.QuadSteps[0];
            span.StepU = state.QuadSteps[2] - state.QuadSteps[0];
            span.StepV = state.QuadSteps[1] - state.QuadSteps[0];
            emit(span);
        }

        --rowsLeft;

        if (rowsLeft < 0)
        {
            break;
        }

        if (rowsLeft == 0)
        {
            // The last row: both sides step without looking at their counts.
            left.x += left.dx;
            left.u += left.du;
            left.v += left.dv;
            right.x += right.dx;
            right.u += right.du;
            right.v += right.dv;
            continue;
        }

        if (--left.count == 0)
        {
            const int32_t from = leftEnd;
            const int32_t to = from == 0 ? 3 : from - 1;
            leftEnd = to;
            int32_t rows = corners[to].Y - corners[from].Y;

            if (static_cast<uint32_t>(rows) < 1)
            {
                rows = 1;
            }

            left.from = from;
            left.to = to;
            SetupQuadEdge(corners, left, from, to, rows);
        }
        else
        {
            left.x += left.dx;
            left.u += left.du;
            left.v += left.dv;
        }

        if (--right.count == 0)
        {
            const int32_t from = rightEnd;
            const int32_t to = from == 3 ? 0 : from + 1;
            rightEnd = to;
            int32_t rows = corners[to].Y - corners[from].Y;

            if (static_cast<uint32_t>(rows) < 1)
            {
                rows = 1;
            }

            right.from = from;
            right.to = to;
            SetupQuadEdge(corners, right, from, to, rows);
        }
        else
        {
            right.x += right.dx;
            right.u += right.du;
            right.v += right.dv;
        }
    }
}

void MCSoftwareRenderer::MapQuad(MCWindow& target, const MCMapQuadCommand& command)
{
    const MCWindow& source = command.Texture.get();
    NoteCpuRead(source, "MapQuad");
    const int32_t stride = target.XMax + 1;
    const std::span<const uint8_t> texture = source.Bytes();
    const std::span<uint8_t> pixels = target.Bytes();
    const int32_t textureStride = source.XMax + 1;
    // Port fix: texel reads outside the work buffer (the original read whatever lay there) count as transparent.
    const int64_t textureSize = static_cast<int64_t>(std::max(textureStride, 0)) * std::max(source.YMax + 1, 0);

    MCMapQuadSpans(command, _Spans,
                   [&](const MCSpan& span)
                   {
                       auto out = static_cast<size_t>(static_cast<intptr_t>(span.Y) * stride + span.X0);

                       for (int32_t j = 0; j <= span.X1 - span.X0; ++j, ++out)
                       {
                           const int64_t texel = MCSpanTexel(span, j);

                           if (texel >= 0 && texel < textureSize)
                           {
                               const uint8_t color = texture[static_cast<size_t>(texel)];

                               if (color != 0xff)
                               {
                                   pixels[out] = color;
                               }
                           }
                       }
                   });
}

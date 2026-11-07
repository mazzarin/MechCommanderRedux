#pragma once

#include "platform/MCRenderer.h"
#include "vfx/MCVfxFunctions.h"

/// <summary>
/// The see-through pixels of a draw's target: on a target with an op plane, a draw that maps the pixels under it
/// leaves the key pixels as they are and records its table in their ops (see <see cref="MCUnderlay"/>).
/// </summary>
class MCSeeThrough
{
public:
    explicit MCSeeThrough(const MCWindow& target)
        : _Ops(MCRenderer::OpPlane(target))
        , _Base(target.Pixels != nullptr ? MCAddressKey::Of(target.Pixels[0]) : MCAddressKey())
    {
    }

    /// <summary>Whether the target has see-through pixels at all.</summary>
    explicit operator bool() const { return !_Ops.empty(); }

    /// <summary>Whether pixel <paramref name="p"/> of the target is see-through.</summary>
    bool At(const uint8_t& p) const { return !_Ops.empty() && p == MCRenderer::UnderlayKey; }

    /// <summary>Maps see-through pixel <paramref name="p"/> through <paramref name="table"/>.</summary>
    void Map(uint8_t& p, MCColorTable table);

    /// <summary>Maps pixel <paramref name="p"/> through <paramref name="table"/>: a see-through one by <see cref="Map"/>.</summary>
    void Translate(uint8_t& p, MCColorTable table)
    {
        if (At(p))
        {
            Map(p, table);
        }
        else
        {
            p = table[p];
        }
    }

    /// <summary>Blends <paramref name="color"/> over see-through pixel <paramref name="p"/> (its AlphaTable row).</summary>
    void Blend(uint8_t& p, uint8_t color);

private:
    /// <summary>The target's op plane (for the one draw this lives for), and where the target's pixels start.</summary>
    std::span<uint8_t> _Ops;
    MCAddressKey _Base;
    /// <summary>The op of the table mapped last.</summary>
    MCAddressKey _LastTable;
    uint8_t _LastOp = 0;
};

/// <summary>Reads a little-endian 32-bit value at <paramref name="at"/> of <paramref name="bytes"/> (unaligned).</summary>
inline int32_t MCRead32(std::span<const uint8_t> bytes, size_t at)
{
    int32_t value;
    std::memcpy(&value, bytes.subspan(at, 4).data(), 4);
    return value;
}

/// <summary>Reads a little-endian 16-bit value at <paramref name="at"/> of <paramref name="bytes"/> (unaligned).</summary>
inline uint16_t MCRead16(std::span<const uint8_t> bytes, size_t at)
{
    uint16_t value;
    std::memcpy(&value, bytes.subspan(at, 2).data(), 2);
    return value;
}

/// <summary>Where shape <paramref name="shapeNum"/>'s header lies in a VFX shape table (its directory: 8 + 8n).</summary>
inline size_t MCShapeOffset(std::span<const uint8_t> table, int32_t shapeNum)
{
    return static_cast<uint32_t>(MCRead32(table, 8 + static_cast<size_t>(shapeNum) * 8));
}

/// <summary>Row <paramref name="color"/> of AlphaTable: what <paramref name="color"/> drawn over each colour gives.</summary>
inline MCColorTable MCAlphaRow(uint32_t color)
{
    return std::span<const uint8_t>(AlphaTable).subspan(static_cast<size_t>(color) * 256).first<256>();
}

/// <summary>
/// One row of a polygon or mapped quadrilateral as the software renderer fills it: pixels <c>X0</c>..<c>X1</c> of row
/// <c>Y</c>, in window coordinates. The hardware renderer draws the same spans.
/// </summary>
struct MCSpan
{
    int32_t Y = 0;
    int32_t X0 = 0;
    int32_t X1 = 0;
    /// <summary>Gouraud: the colour at <c>X0</c> (16.16, wrapping) and its step per pixel.</summary>
    uint32_t Value = 0;
    int32_t Slope = 0;
    /// <summary>Texel walks (mapped polygons and quadrilaterals): the texture offset at <c>X0</c>.</summary>
    int64_t Texel = 0;
    /// <summary>The 16-bit fractions of u and v and their steps per pixel (complemented when stepping back).</summary>
    uint32_t UFraction = 0;
    uint32_t UStep = 0;
    uint32_t VFraction = 0;
    uint32_t VStep = 0;
    /// <summary>The offset's step per pixel, and what a carry of the u or v fraction adds to it.</summary>
    int32_t Step0 = 0;
    int32_t StepU = 0;
    int32_t StepV = 0;
};

/// <summary>
/// The texture offset of pixel <paramref name="j"/> of a texel walk: the start, <paramref name="j"/> steps, and the
/// carries the u and v fractions have made by then (the asm stepped through a table of the four sums).
/// </summary>
inline int64_t MCSpanTexel(const MCSpan& span, int32_t j)
{
    const int64_t carriesU = (static_cast<int64_t>(span.UFraction) + static_cast<int64_t>(j) * span.UStep) >> 16;
    const int64_t carriesV = (static_cast<int64_t>(span.VFraction) + static_cast<int64_t>(j) * span.VStep) >> 16;
    return span.Texel + static_cast<int64_t>(j) * span.Step0 + carriesU * span.StepU + carriesV * span.StepV;
}

/// <summary>
/// What the polygon and quadrilateral walks keep between calls, as the asm kept it in globals: a span of one pixel
/// reuses the last slopes and step tables (to no effect on its pixel).
/// </summary>
struct MCSpanState
{
    /// <summary>The span slope the last Gouraud or dithered span computed (0x007a9b54 in MCX.EXE).</summary>
    int32_t SpanSlope = 0;
    /// <summary>VFX_map_polygon's texel step table and span slopes (0x007a9b7c..).</summary>
    int32_t MapSteps[4] = {};
    int32_t MapDu = 0;
    int32_t MapDv = 0;
    /// <summary>VFX_shape_transform's texel step table (0x007a9aac).</summary>
    int32_t QuadSteps[4] = {};
};

/// <summary>
/// The spans of a flat, Gouraud, translate or mapped polygon (nothing for the dithered kinds), in the order they are
/// filled.
/// </summary>
void MCPolygonSpans(const MCPolygonCommand& command, MCSpanState& state,
                    const std::function<void(const MCSpan&)>& emit);

/// <summary>The spans of a mapped quadrilateral (VFX_shape_transform), in the order they are filled.</summary>
void MCMapQuadSpans(const MCMapQuadCommand& command, MCSpanState& state,
                    const std::function<void(const MCSpan&)>& emit);

/// <summary>
/// The pixels of an ellipse, clipped, as runs of row <c>y</c> from <c>x0</c> to <c>x1</c>: each point of an outline
/// (a run of one), or each row of a fill, once.
/// </summary>
void MCEllipseRuns(const MCEllipseCommand& command, const std::function<void(int32_t y, int32_t x0, int32_t x1)>& emit);

/// <summary>Calls <paramref name="pixel"/>(x, y) for each pixel of a line, in order.</summary>
template <typename Pixel> void MCLinePixels(const MCLineCommand& command, Pixel&& pixel)
{
    int32_t x = command.X;
    int32_t y = command.Y;
    uint32_t fraction = command.Fraction;

    for (int32_t count = command.Count; count != 0; --count)
    {
        pixel(x, y);
        const uint32_t before = fraction;
        fraction += command.Slope;

        if (fraction < before)
        {
            x += command.MinorX;
            y += command.MinorY;
        }

        x += command.MajorX;
        y += command.MajorY;
    }
}

/// <summary>
/// The software renderer: the original's pixel loops (from vfx/*.cpp), drawing into the windows' memory. It is the
/// renderer of every window today, the headless renderer of the tests, and the reference a hardware renderer is
/// compared with.
/// </summary>
/// <remarks>
/// Defined in MCSoftwareRenderer.cpp (fills, copies, pixels, lines, ellipses, status bars, glyphs, sprites),
/// MCSoftwareRendererShapes.cpp (run-length shapes, fast shapes, tiles) and MCSoftwareRendererPolygons.cpp
/// (polygons and mapped quadrilaterals).
/// </remarks>
class MCSoftwareRenderer final : public MCRenderer
{
public:
    /// <summary>The one software renderer.</summary>
    static MCSoftwareRenderer& Instance();

    void Clear(MCWindow& target, const MCRect& rect, uint8_t color) override;
    void Hash(MCWindow& target, const MCRect& rect, uint8_t color) override;
    void Translate(MCWindow& target, const MCRect& rect, MCColorTable table) override;
    void Copy(MCWindow& target, const MCCopyCommand& command) override;
    void AlphaBlit(MCWindow& target, const MCAlphaBlitCommand& command) override;
    void ShapeBlit(MCWindow& target, const MCShapeBlitCommand& command) override;
    void Write(MCWindow& target, int32_t x, int32_t y, std::span<const uint8_t> pixels) override;
    void Pixel(MCWindow& target, int32_t x, int32_t y, uint8_t color) override;
    void Shape(MCWindow& target, const MCShapeCommand& command) override;
    void FastShape(MCWindow& target, const MCFastShapeCommand& command) override;
    void Tile(MCWindow& target, const MCTileCommand& command) override;
    void Polygon(MCWindow& target, const MCPolygonCommand& command) override;
    void MapQuad(MCWindow& target, const MCMapQuadCommand& command) override;
    void Line(MCWindow& target, const MCLineCommand& command) override;
    void Ellipse(MCWindow& target, const MCEllipseCommand& command) override;
    void StatusBar(MCWindow& target, const MCStatusBarCommand& command) override;
    void Glyph(MCWindow& target, const MCGlyphCommand& command) override;

protected:
    /// <summary>Nothing to do: every draw reads AlphaTable as it goes.</summary>
    void OnAlphaTableChanged() override {}

    /// <summary>Nothing to do: shapes are read straight from the game's memory.</summary>

private:
    /// <summary>The asm's globals the polygon and quadrilateral walks keep between calls.</summary>
    MCSpanState _Spans;
    /// <summary>The picture a shape transform renders its shape into before blending it.</summary>
    MCWindow _ShapeScratch;
    /// <summary>How many pixels <see cref="_ShapeScratch"/>'s pixels hold (it grows, never shrinks).</summary>
    size_t _ShapeScratchCapacity = 0;
};

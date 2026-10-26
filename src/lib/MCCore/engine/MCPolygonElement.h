#pragma once

#include "engine/MCElement.h"
#include "platform/MCRenderer.h"
#include "vfx/MCVfx.h"

/// <summary>
/// What a <see cref="MCPolygonElement"/> draws: up to 6 screen vertices, filled in one colour, through a fade table,
/// texture-mapped from a bitmap, or (as a special case) a status bar.
/// </summary>
/// <remarks>Original source: <c>engine\cepoly.h</c>.</remarks>
struct MCPolyElementData
{
    /// <summary>The number of vertices used (0 draws nothing).</summary>
    int32_t NumVertices = 0;
    /// <summary>The vertices, in pane coordinates (u, v are texture coordinates when mapped).</summary>
    /// <remarks>Six: the most any caller fills (they write the vertices directly).</remarks>
    std::array<MCScreenVertex, 6> Vertices{};
    /// <summary>When set a textured polygon isn't drawn.</summary>
    bool TextureMapOff = false;
    /// <summary>When set (with a fade table) a textured polygon is drawn translated instead.</summary>
    bool Translate = false;
    /// <summary>When set the element draws a status bar from vertex 0 to vertex 1 instead.</summary>
    bool StatusBar = false;
    /// <summary>The status bar's length (the last argument of <c>AGStatusBar</c>).</summary>
    int32_t BarPercent = 0;
    /// <summary>Port-only: the status bar's length before it was rounded down to <c>BarPercent</c>.</summary>
    std::optional<float> MeasuredBar;
    /// <summary>The status bar's colour.</summary>
    int32_t BarColor = 0;
    /// <summary>
    /// The picture the polygon maps, as a window of the texture's size with the texture's handle (the renderers read
    /// the texture through it); none for a flat polygon (colour in the vertices).
    /// </summary>
    std::weak_ptr<MCWindow> Texture;
    /// <summary>The fade table the polygon is translated through, if any.</summary>
    std::optional<MCColorTable> FadeTable;
};

/// <summary>A polygon (see <see cref="MCPolyElementData"/>).</summary>
/// <remarks>Original source: <c>engine\cepoly.cpp</c>.</remarks>
class MCPolygonElement : public MCElement
{
public:
    /// <summary>A copy of <paramref name="data"/> at depth <paramref name="depth"/>.</summary>
    MCPolygonElement(MCPolyElementData data, int32_t depth);

    void Draw() override;

    /// <summary>What to draw.</summary>
    MCPolyElementData Data;
};

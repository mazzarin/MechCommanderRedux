#pragma once

#include "sprite/MCInBetween.h"

/// <summary>
/// Something to draw this frame: a line, a shape, a polygon, a string... Appearances, the terrain and the interface
/// make elements with <see cref="MCElementBuffer::Make"/> and add them to the frame's draw list, which sorts them by
/// <see cref="Depth"/> and calls <see cref="Draw"/> on each.
/// </summary>
/// <remarks>Original source: <c>engine\celement.cpp</c>. An element lives until the draw list is reset, every frame.</remarks>
class MCElement
{
public:
    /// <summary>An element at depth <paramref name="depth"/>.</summary>
    explicit MCElement(int32_t depth);

    /// <summary>An element at depth <paramref name="depth"/>, rounded down to a whole number.</summary>
    explicit MCElement(float depth);

    virtual ~MCElement() = default;

    /// <summary>Draws the element into <c>GlobalPane</c>.</summary>
    virtual void Draw() = 0;

    /// <summary>The sort key.</summary>
    float Depth = 0.0f;
    /// <summary>
    /// Port-only (HD lighting): the element is a light's glow (MCLight's), drawn in a <see cref="MCRenderer::GlowScope"/>.
    /// </summary>
    bool Glow = false;
    /// <summary>
    /// Port-only (HD art): the element is an effect's frame shown part of the way toward its next, drawn in a
    /// <see cref="MCRenderer::InBetweenScope"/> (the HD art pack's in-between, where it has one).
    /// </summary>
    std::optional<MCInBetween> InBetween;
};

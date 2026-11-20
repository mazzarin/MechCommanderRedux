#pragma once

#include <RmlUi/Core/Decorator.h>

/// <summary>
/// Port-only (SPEC-menus.md, "The 1998 art"): the <c>frame-box</c> decorator, a frame cut from a 1998 picture as a
/// nine-slice whose edges and centre are repeated, never stretched: <c>frame-box(tl, t, tr, l, c, r, bl, b, br)</c>
/// names nine sprites of one <c>@spritesheet</c>, as RmlUi's own <c>tiled-box</c> does (which stretches its edges).
/// The corners are drawn whole at their size; the top and bottom edges are their sprites repeated along the width
/// between the corners, the left and right along the height, the centre both ways; the last copy of a row is cut, not
/// squeezed. A sprite's pixels land on whole window pixels: each copy starts where its 1998 place falls, rounded.
/// </summary>
/// <remarks>
/// With HD art the picture is its HD copy while RmlUi is told the 1998 size (ui/MCRmlArt), so the sprites' 1998
/// rectangles draw the same parts of the HD picture.
/// </remarks>
class MCRmlFrameInstancer final : public Rml::DecoratorInstancer
{
public:
    /// <summary>The decorator's name in a style sheet.</summary>
    static constexpr const char* Name = "frame-box";

    MCRmlFrameInstancer();

    Rml::SharedPtr<Rml::Decorator> InstanceDecorator(
        const Rml::String& name, const Rml::PropertyDictionary& properties,
        const Rml::DecoratorInstancerInterface& instancerInterface) override;

private:
    /// <summary>The nine pieces' properties, top-left to bottom-right, row by row.</summary>
    std::array<Rml::PropertyId, 9> _Pieces{};
};

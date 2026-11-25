#pragma once

namespace Rml
{
    class Context;
    class Element;
}

/// <summary>
/// Port-only (SPEC-menus.md, "Testing"): the remade menus' layout and legibility checks, read from RmlUi's own boxes
/// after a frame is laid out and drawn (tools/menus-frames measures the style frames the same way).
/// </summary>
namespace MCRmlChecks
{
    /// <summary>The face's letters span this share of its size from the highest ascender to the lowest descender.</summary>
    inline constexpr float FaceBody = 0.946f;
    /// <summary>No text under this many units of body height (Xbox Accessibility Guideline 101's 18 px at 1080p).</summary>
    inline constexpr float LeastUnits = 8.0f;
    /// <summary>The contrast a text needs against what is behind it, and a large or disabled one (XAG 102).</summary>
    inline constexpr float TextContrast = 4.5f;
    inline constexpr float LargeContrast = 3.0f;
    /// <summary>A Heading's letters, and larger, are large.</summary>
    inline constexpr float LargeUnits = 14.0f;

    /// <summary>A text shown: its words, its letters' height in units, its contrast and what it needs, where it lies.</summary>
    struct MCText
    {
        std::string Words;
        float Units = 0.0f;
        float Contrast = 0.0f;
        float Need = 0.0f;
        /// <summary>It sits on a picture, not a well.</summary>
        bool OnPicture = false;
        /// <summary>Shown inside the window.</summary>
        bool Inside = true;
        /// <summary>
        /// No line of it cut off at the side of the box that clips it (a word cut at a column's edge), but a row whose
        /// design cuts it there (class <c>cut</c>: a map description's banner row longer than the line).
        /// </summary>
        bool Whole = true;

        /// <summary>Whether it passes at <paramref name="unit"/> pixels a unit (the floor to the half pixel).</summary>
        bool Passes(float unit) const;
    };

    /// <summary>Every text the context's shown documents show, at <paramref name="unit"/> pixels a unit.</summary>
    std::vector<MCText> Texts(Rml::Context& context, float unit);

    /// <summary>
    /// What's wrong with the context's shown documents' boxes: an element shown outside the window or spilling from a
    /// parent that doesn't clip it, two siblings in the flow overlapping inside their borders (placed ones, an overlay,
    /// may; a shared border is no overlap), a text's line spilling from its element's content box (a wrapped line may
    /// take the pixel RmlUi rounds its room up by, where the element keeps it as padding; a row whose design cuts it at
    /// its edge, class <c>cut</c>, may run past it). Empty: nothing.
    /// </summary>
    std::string BoxProblems(Rml::Context& context);

    /// <summary>The texts that fail (<see cref="MCText::Passes"/>), one a line; empty: none.</summary>
    std::string Illegible(Rml::Context& context, float unit);
}

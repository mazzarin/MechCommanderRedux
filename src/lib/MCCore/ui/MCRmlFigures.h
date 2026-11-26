#pragma once

#include <string>
#include <string_view>
#include <vector>

/// <summary>
/// Port-only: one character of a number held still as it changes (the bar's RP and clock): the face's figures are
/// proportional, so the look sets each digit (<c>.figures .digit</c>, look.rcss) in a place as wide as its widest, and
/// what is beside the number stays put as its digits change.
/// </summary>
struct MCFigureGlyph
{
    std::string Text;
    bool Digit = false;

    bool operator==(const MCFigureGlyph&) const = default;
};

/// <summary>
/// <paramref name="number"/> a character each, its digits marked, for a list the document shows each in its place
/// (<c>data-for="g : clockFigures"</c>, <c>data-class-digit="g.digit"</c>).
/// </summary>
inline std::vector<MCFigureGlyph> MCHeldFigures(std::string_view number)
{
    std::vector<MCFigureGlyph> glyphs;
    glyphs.reserve(number.size());

    for (const char character : number)
    {
        glyphs.push_back(
            MCFigureGlyph{.Text = std::string(1, character), .Digit = character >= '0' && character <= '9'});
    }

    return glyphs;
}

#pragma once

#include "ui/MCRmlRecorder.h"

/// <summary>
/// Port-only (SPEC-menus.md, "The 1998 art"): the pictures a menu's documents name, by their name in the game's art:
/// <c>art:&lt;name&gt;</c> is the TGA <c>&lt;name&gt;.tga</c> under the game's <c>ArtPath</c> (<c>/</c> between folders,
/// as <c>art:logart/LSBBK01</c>), read as the logistics screens read theirs (MCLogPort) and coloured by its own colour
/// map; its key colour (magenta, where the screens' keyed copies draw nothing) transparent. With HD art, the pack's HD
/// copy (found by the 1998 picture's pixels, as the menus' HD art is) is drawn in its place, its key pixels left out as
/// the 1998 picture's are, while RmlUi is told the 1998 size: a style sheet names a picture's parts in 1998 pixels, and
/// the HD picture draws the same parts.
/// </summary>
namespace MCRmlArt
{
    /// <summary>The scheme a picture's source starts with.</summary>
    inline constexpr std::string_view Scheme = "art:";

    /// <summary>
    /// The picture <paramref name="source"/> names, with its HD copy when <paramref name="hd"/> and the pack has one;
    /// why not, for a source that isn't a picture's or one the game can't read.
    /// </summary>
    std::expected<MCRmlRecorder::MCLoaded, std::string> Load(std::string_view source, bool hd);

    /// <summary>The colours of the art picture <paramref name="name"/>'s colour map (as <c>art:</c> names it).</summary>
    std::expected<std::array<SDL_Color, 256>, std::string> Colors(std::string_view name);
}

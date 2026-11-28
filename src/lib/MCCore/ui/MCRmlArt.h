#pragma once

#include "ui/MCRmlRecorder.h"

class MCLogPart;
class MCRmlFiles;

/// <summary>
/// Port-only (SPEC-menus.md, "The 1998 art"): the pictures a menu's documents name, by their name in the game's art:
/// <c>art:&lt;name&gt;</c> is the TGA <c>&lt;name&gt;.tga</c> under the game's <c>ArtPath</c> (<c>/</c> between folders,
/// as <c>art:logart/LSBBK01</c>), read as the logistics screens read theirs (MCLogPort) and coloured by its own colour
/// map; its key colour (magenta, where the screens' keyed copies draw nothing) transparent. With HD art, the pack's HD
/// copy (found by the 1998 picture's pixels, as the menus' HD art is) is drawn in its place, its key pixels left out as
/// the 1998 picture's are, while RmlUi is told the 1998 size: a style sheet names a picture's parts in 1998 pixels, and
/// the HD picture draws the same parts.
/// <para>Any other source is the new art in the app's own <c>ui</c> folder (SPEC-menus.md, "The art": the stone, the
/// paintings' sides): a PNG at the 1998 size (<c>art/stone.png</c>), and with HD art on its HD copy in the folder's
/// <c>hd</c> folder (<c>art/hd/stone.png</c>) when there is one, reported at the 1998 file's size.</para>
/// </summary>
namespace MCRmlArt
{
    /// <summary>The scheme a picture's source starts with.</summary>
    inline constexpr std::string_view Scheme = "art:";
    /// <summary>
    /// Port-only (stage 5): a campaign mission's briefing map, <c>map:&lt;scenario&gt;</c> (<c>map:mis0203</c>): the
    /// <c>.log.tga</c> its mission file's MapFile names (MCLogistics::BriefingMapFile), read and coloured as
    /// <c>art:</c>'s pictures are, with the pack's HD copy (found by its pixels, as the briefing finds it) on HD.
    /// </summary>
    inline constexpr std::string_view MapScheme = "map:";
    /// <summary>
    /// Port-only (stage 8): a unit's outline as 1998's READY AREA tile draws it, each location coloured for its damage:
    /// <c>unit:mech/&lt;name index&gt;/&lt;states&gt;</c> or <c>unit:vehicle/...</c>, a state a location (0 to 3 its damage's
    /// fade, <c>-</c> unhurt, <c>x</c> none), from the logistics' icon shapes in the logistics' colours
    /// (<see cref="UnitSource"/> makes it).
    /// </summary>
    inline constexpr std::string_view UnitScheme = "unit:";

    /// <summary>The <c>unit:</c> source of <paramref name="part"/>'s outline as it stands.</summary>
    std::string UnitSource(const MCLogPart& part);
    /// <summary>The <c>unit:</c> source of a whole unit's outline (the store's): a mech's or a vehicle's, by name index.</summary>
    std::string WholeUnitSource(bool mech, int32_t nameIndex);
    /// <summary>
    /// Port-only (SPEC-menus.md's stage 4): a map's briefing picture by its map file, <c>terrain:&lt;map&gt;</c> (the
    /// room's and CHOOSE MAP's), its <c>&lt;map&gt;.log.tga</c> under the game's TerrainPath, read and drawn as
    /// <c>map:</c>'s.
    /// </summary>
    inline constexpr std::string_view TerrainScheme = "terrain:";

    /// <summary>
    /// The decorator that draws map file <paramref name="map"/>'s briefing picture (<c>terrain:</c>'s) at its own shape in
    /// its box, at the box's top left, as the briefing and LOAD GAME draw theirs (CHOOSE MAP's box is wider than the
    /// square pictures); <c>none</c> for no map.
    /// </summary>
    std::string TerrainPicture(std::string_view map);

    /// <summary>
    /// The picture <paramref name="source"/> names, with its HD copy when <paramref name="hd"/> and the pack has one;
    /// why not, for a source that isn't a picture's or one the game can't read.
    /// </summary>
    std::expected<MCRmlRecorder::MCLoaded, std::string> Load(std::string_view source, bool hd, const MCRmlFiles& files);

    /// <summary>The HD copy's path for the app's picture <paramref name="path"/> (<c>art/stone.png</c>: <c>art/hd/stone.png</c>).</summary>
    std::string HdPath(std::string_view path);

    /// <summary>The colours of the art picture <paramref name="name"/>'s colour map (as <c>art:</c> names it).</summary>
    std::expected<std::array<SDL_Color, 256>, std::string> Colors(std::string_view name);
}

#pragma once

/// <summary>
/// Port-only: an art mode, the art the game shows as one choice a developer switches while the game runs (the debug
/// console's <c>art</c> command; the picture tools' <c>--hd-art-modes</c>, for recordings): a pair of the player's HD
/// ART and HD LIGHTING settings (<see cref="GHdArt"/>, <see cref="GHdLighting"/>). Switching sets them for the run
/// without saving them; the next frame drawn shows the mode, and opening the preferences ends it
/// (<see cref="EndArtMode"/>).
/// </summary>
struct MCArtMode
{
    /// <summary>What the console and the tools call it.</summary>
    std::string_view Name;
    /// <summary>HD ART's setting: 0 off, 1 on.</summary>
    int HdArt = 0;
    /// <summary>HD LIGHTING's setting (<see cref="MCHdLightingSetting"/>).</summary>
    int HdLighting = 0;
};

/// <summary>
/// The art modes: <c>original</c> (the original's art), <c>hd</c> (HD art), <c>day</c> (HD art, HD lighting on, at the
/// mission's own time of day: by day in the original's missions), <c>dusk</c> and <c>night</c> (HD art, HD lighting at dusk or at night).
/// </summary>
std::span<const MCArtMode> ArtModes();

/// <summary>The art mode named <paramref name="name"/> (case blind), if there is one.</summary>
std::optional<MCArtMode> ArtModeNamed(std::string_view name);

/// <summary>
/// Shows <paramref name="mode"/>: sets HD ART and HD LIGHTING, and HD art turned on ties the shapes loaded so far to
/// the pack (<see cref="AttachHdArtToLoaded"/>).
/// </summary>
/// <returns>Nothing, or why nothing changed: the mode draws HD art and there is no pack it can draw from.</returns>
std::expected<void, std::string> ShowArtMode(const MCArtMode& mode);

/// <summary>
/// Ends the mode <see cref="ShowArtMode"/> showed, if one is: HD ART and HD LIGHTING back to the player's own, so the
/// preferences screen shows and saves theirs, never the mode's.
/// </summary>
void EndArtMode();

/// <summary>
/// Ties every shape loaded so far to the HD art pack, as loading it with HD art on would have: the sprite cache's, the
/// craters', the mech bar's icons and logistics' icons. Does nothing while HD art is off.
/// </summary>
void AttachHdArtToLoaded();

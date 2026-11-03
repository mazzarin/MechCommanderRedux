#pragma once

/// <summary>
/// Port-only: glyphs for the characters the game's fonts list but leave blank. The fonts draw ASCII and the accented
/// letters (Windows-1252 0xC0-0xFF) but nothing at 0x80-0xBF, so text holding a ´ or a § (keys a German or a Nordic
/// keyboard types, which the CONTROLS screen names) showed nothing there.
/// </summary>
namespace MCFontGlyphs
{
    /// <summary>
    /// Adds glyphs to VFX font <paramref name="font"/> (the layout in vfx/MCVfxFunctions.h), already checked to fit
    /// (VfxFontFits), for those of its blank characters it can make: ´ ¨ ° cut from its é, ë and å, ¿ its ? turned
    /// round, « and » its &lt; and &gt; doubled, and § ² º ° ½ drawn in its ink (fonts of one ink, 6 or 8 lines to the
    /// capitals: the 8- and the 10- and 12-line ones). A font without the letters to make a glyph from keeps it
    /// blank.
    /// </summary>
    void AddMissing(std::vector<uint8_t>& font);

    /// <summary>Whether VFX font <paramref name="font"/> lists <paramref name="character"/> with a glyph that draws.</summary>
    bool Draws(std::span<const uint8_t> font, uint8_t character);
}

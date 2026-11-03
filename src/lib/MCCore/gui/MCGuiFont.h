#pragma once

#include "platform/MCRegisteredBlock.h"

/// <summary>A VFX font file with its colour translation table.</summary>
/// <remarks>
/// Original source: <c>gui\afont.cpp</c> (<c>aFont</c>). The fonts the game uses are made by
/// <see cref="MCGuiSystem::Start"/> (<c>WhiteFont</c>, <c>MedRedFont</c>, ..., gui/MCGuiGlobals.h). Text is drawn up to
/// its first NUL, as the original's C strings were.
/// </remarks>
class MCGuiFont
{
public:
    /// <summary>A font with no letters: it measures 0 and draws nothing.</summary>
    MCGuiFont() = default;
    /// <summary>Lets the renderers forget the font's data and colour table.</summary>
    ~MCGuiFont();
    MCGuiFont(const MCGuiFont&) = delete;
    MCGuiFont& operator=(const MCGuiFont&) = delete;

    /// <summary>Loads font file <paramref name="fileName"/> under <c>FontPath</c>.</summary>
    /// <returns>The font, or why it couldn't be loaded (an empty file; a missing one is fatal).</returns>
    static std::expected<std::unique_ptr<MCGuiFont>, std::string> Create(std::string_view fileName);

    /// <summary>
    /// Loads font file <paramref name="fileName"/> under <c>FontPath</c> in place of the font's letters; the colour
    /// table becomes the identity. A missing file is fatal (<c>GeneralMsg</c>).
    /// </summary>
    /// <returns>0, or -2 when the file is empty.</returns>
    int32_t Load(std::string_view fileName);
    /// <summary>Lets go of the letters (the font then measures 0 and draws nothing).</summary>
    void Unload();

    /// <summary>The font's height (0 without letters).</summary>
    int32_t Height() const;
    /// <summary>The width of <paramref name="text"/> in pixels.</summary>
    int32_t Width(std::string_view text) const;
    /// <summary>The width of one character.</summary>
    int32_t Width(uint8_t c) const;
    /// <summary>Whether the font has a glyph for <paramref name="c"/> that draws (not a blank one).</summary>
    bool Draws(uint8_t c) const;
    /// <summary>Draws one character at (<paramref name="xPos"/>, <paramref name="yPos"/>).</summary>
    /// <returns>0, or -3 without letters.</returns>
    int32_t WriteChar(const MCPane& pane, int32_t xPos, int32_t yPos, char c);
    /// <summary>
    /// Draws <paramref name="text"/>, its last characters cut off until it fits <paramref name="maxWidth"/> pixels
    /// unless that is -1.
    /// </summary>
    /// <returns>0, or -3 without letters.</returns>
    int32_t WriteString(const MCPane& pane, int32_t xPos, int32_t yPos, std::string_view text, int32_t maxWidth = -1);

    /// <summary>Draws <paramref name="text"/> up to its first newline.</summary>
    /// <returns>0, or -3 without letters.</returns>
    int32_t WriteStringToNewline(const MCPane& pane, int32_t xPos, int32_t yPos, std::string_view text);

    /// <summary>
    /// How many characters of <paramref name="text"/> fit in <paramref name="maxWidth"/> pixels; with
    /// <paramref name="wordWrap"/>, breaking at a space (-1 when no break fits).
    /// </summary>
    int32_t CharactersToWidth(std::string_view text, int32_t maxWidth, bool wordWrap) const;

    /// <summary>The font file's contents (registered with the renderers while loaded).</summary>
    MCRegisteredBlock FontData;
    /// <summary>The colour translation the characters are drawn through (registered with the renderers).</summary>
    std::array<uint8_t, 256> ColorTable = {};
};

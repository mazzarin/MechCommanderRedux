#pragma once

#include "vfx/MCVfx.h"

/// <summary>A stroke (vector) font, drawn with lines and scalable. The interface writes help and map text with it.</summary>
/// <remarks>
/// Original source: <c>engine\font.cpp</c>. File format (a <c>.bin</c> of the font path, read whole): a list of
/// letters, each <c>uint8 letter, uint8 width, uint8 height</c> then strokes of 5 bytes <c>uint8 type, x0, y0, x1,
/// y1</c> ended by a type 0; the list ends with letter 0. Type 1 is a line in pixels (scaled with the letter), type 2
/// a line in 1/256ths of the letter cell. A letter of 0xFF doesn't count toward the height.
/// </remarks>
class MCFont
{
public:
    /// <summary>A font without letters (nothing is drawn, every width is 0).</summary>
    MCFont() = default;

    /// <summary>A font of <paramref name="data"/> (the file's bytes).</summary>
    explicit MCFont(std::vector<uint8_t> data);

    /// <summary>Loads <paramref name="fontName"/><c>.bin</c> from the font path.</summary>
    static std::expected<std::unique_ptr<MCFont>, std::string> Create(std::string_view fontName);

    /// <summary>
    /// The strokes of <paramref name="letter"/> (its entry to the end of the font's data), falling back from lower to
    /// upper case; empty when it has none.
    /// </summary>
    std::span<const uint8_t> FindLetter(uint8_t letter);

    /// <summary>The width of <paramref name="letter"/> (scaled).</summary>
    int32_t PrintWidth(uint8_t letter);

    /// <summary>
    /// The width of <paramref name="text"/>; with <paramref name="multiLine"/>, the widest of its lines.
    /// </summary>
    int32_t PrintWidth(std::string_view text, bool multiLine);

    /// <summary>Draws <paramref name="letter"/> at the cursor into <paramref name="pane"/>; returns its width.</summary>
    int32_t Print(uint8_t letter, MCPane& pane);

    /// <summary>Draws <paramref name="text"/> at the cursor in <paramref name="color"/> (-1 keeps the colour).</summary>
    void Print(std::string_view text, int32_t color, MCPane& pane);

    /// <summary>
    /// Moves the cursor to (<paramref name="x"/>, <paramref name="y"/>) and draws <paramref name="text"/>.
    /// </summary>
    void Print(int32_t x, int32_t y, std::string_view text, int32_t color, MCPane& pane);

    /// <summary>Draws <paramref name="text"/> up to its first newline; returns the characters drawn.</summary>
    int32_t PrintToNewline(std::string_view text, int32_t color, MCPane& pane);

    /// <summary>Moves the cursor and draws up to the first newline.</summary>
    int32_t PrintToNewline(int32_t x, int32_t y, std::string_view text, int32_t color, MCPane& pane);

    /// <summary>The cursor's x.</summary>
    int32_t CurX = 0;
    /// <summary>The cursor's y.</summary>
    int32_t CurY = 0;
    /// <summary>The colour letters are drawn in.</summary>
    int32_t Color = 0xf;
    /// <summary>The tallest letter's height.</summary>
    uint8_t FontHeight = 0;
    /// <summary>The scale applied when <see cref="Scaled"/>.</summary>
    float Scale = 2.0f;
    /// <summary>Whether sizes are scaled by <see cref="Scale"/> (rounded down).</summary>
    bool Scaled = true;
    /// <summary>
    /// Port-only: the strokes' width in window pixels where lines are drawn smooth (VfxLineDrawWide); 1 is the
    /// original's.
    /// </summary>
    float StrokeWidth = 1.0f;

private:
    /// <summary>The strokes of <paramref name="letter"/> (looked up once), or empty.</summary>
    std::span<const uint8_t> Find(uint8_t letter);

    /// <summary>A size scaled as the font is (when <see cref="Scaled"/>).</summary>
    int32_t ScaledSize(uint32_t size) const;

    /// <summary>The font file.</summary>
    std::vector<uint8_t> _Data;
    /// <summary>Each letter's offset in <see cref="_Data"/> once looked up.</summary>
    std::array<std::optional<size_t>, 256> _Letters{};
};

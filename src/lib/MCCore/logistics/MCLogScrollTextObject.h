#pragma once

#include "logistics/MCLogObject.h"

/// <summary>
/// A scrolling list of coloured text lines with a draggable scroll tab and up to four highlighted lines. Each line is
/// kept as a colour byte, the text and a newline.
/// </summary>
/// <remarks>Original source: <c>logistics\loggen.cpp</c> (<c>lScrollTextObject</c>).</remarks>
class MCLogScrollTextObject : public MCLogObject
{
public:
    /// <summary>The highlight bars a list has (each lights one line).</summary>
    static constexpr int32_t HighlightCount = 4;

    /// <summary>
    /// Places the list, makes the scroll tab, and prints <paramref name="text"/> if given (a list with no text
    /// doesn't grow its port to fit).
    /// </summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view text) override;

    /// <summary>Frees the scroll tab and the text.</summary>
    void OnDestroy() override;

    /// <summary>
    /// Draws the highlight bars and every line in its colour (a tab starts a second column, or prints as a space
    /// when the list has none).
    /// </summary>
    void Draw() override;

    /// <summary>Draws the visible part of the lines (scrolled by <see cref="FirstPixel"/>), then the children.</summary>
    void Display() override;

    /// <summary>Port: the list draws itself each frame; its port is a view as tall as all its lines.</summary>
    bool DrawsLive() override { return true; }

    void Resize(int32_t width, int32_t height) override;

    /// <summary>Appends a line of <paramref name="text"/> in <paramref name="color"/>.</summary>
    void Print(std::string_view line, uint8_t color);

    /// <summary>Appends an empty line (the port doesn't grow for it).</summary>
    void PrintBlank(uint8_t color);

    /// <summary>
    /// Appends <paramref name="text"/> word-wrapped to <paramref name="width"/> pixels (-1: the list's width): each
    /// line ends at the last space that lets it fit; a first word too long for a line takes the rest of the text.
    /// </summary>
    void PrintWrapped(std::string_view text, uint8_t color, int32_t width);

    /// <summary>Empties the list and its highlights.</summary>
    void Clear();

    /// <summary>Resizes the port to the height of all the lines (never below the visible height).</summary>
    void ResetPortSize();

    /// <summary>Converts a scroll tab position into the first visible pixel row.</summary>
    void CalcFirstPixel(int32_t tabPos);

    /// <summary>Moves the scroll tab to match <see cref="FirstPixel"/>.</summary>
    void PositionScrollTab();

    /// <summary>
    /// Scrolls for a click: <paramref name="direction"/> -1 a line up, 1 a line down, 0 a page towards
    /// <paramref name="yPos"/> (above or below the thumb).
    /// </summary>
    void ReceiveClick(int32_t direction, int32_t yPos);

    /// <summary>Port-only: the mouse wheel scrolls a line per notch, as the arrows do. Not taken when the text fits.</summary>
    bool MouseWheel(int32_t steps, int32_t xPos, int32_t yPos) override;

    /// <summary>The text of line <paramref name="line"/> (without its colour), or none; lines 0 and 1 are both the first.</summary>
    std::optional<std::string> GetTextLine(int32_t line) const;

    /// <summary>The first visible pixel row of the port.</summary>
    int32_t FirstPixel = 0;
    /// <summary>The colour of each highlight bar.</summary>
    std::array<uint8_t, HighlightCount> HighlightColor = {0xff, 0xff, 0xff, 0xff};
    /// <summary>The line under each highlight bar (-1 = none).</summary>
    std::array<int32_t, HighlightCount> HighlightLine = {-1, -1, -1, -1};
    /// <summary>
    /// The lines: a colour byte, the text, a newline. A colour of 0 ends the text as drawn (it was a C string).
    /// </summary>
    std::string Text;
    /// <summary>The draggable scroll tab (a child).</summary>
    MCGuiHandle<MCLogObject> ScrollTab;
    int32_t NumLines = 0;
    /// <summary>The list scrolls: its port grows to fit the lines.</summary>
    bool Scrolling = true;
    /// <summary>The x of the second column (text after a tab); negative = tabs print as spaces.</summary>
    int32_t TabColumn = -1;
    /// <summary>The font size column of <c>Fonts</c> (the row is the line's colour).</summary>
    int32_t FontIndex = 0;

private:
    /// <summary>The height of a line: the font's and 4 rows of gap.</summary>
    int32_t LineHeight() const;
};

/// <summary>Paints a logistics scroll tab: a filled box with a light top/left and dark bottom/right edge.</summary>
void LogPaintScrollTab(MCGuiObject& tab);

/// <summary>The scroll tab's event routine: dragging it scrolls its <see cref="MCLogScrollTextObject"/>.</summary>
void LogScrollTabHandleEvent(MCGuiObject& tab, MCGuiEvent& event);

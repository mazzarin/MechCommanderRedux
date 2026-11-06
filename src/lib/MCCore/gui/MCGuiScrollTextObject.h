#pragma once

#include "gui/MCGuiObject.h"
#include "gui/MCGuiRef.h"

/// <summary>
/// A scrolling text window: lines appended by <see cref="Print"/>, each in its colour, drawn into a port that grows
/// with the text, with a scroll thumb at the right.
/// </summary>
/// <remarks>
/// Original source: <c>gui\atextbox.cpp</c> (<c>aScrollTextObject</c>). The font is
/// <c>Fonts</c>[row][<see cref="FontIndex"/>], the row picked by the line's colour.
/// </remarks>
class MCGuiScrollTextObject : public MCGuiObject
{
public:
    /// <summary>The number of highlighted sections (the tactical map's weapon list has one per range bracket).</summary>
    static constexpr size_t NumSections = 4;

    /// <summary>
    /// The base's init with a scroll port; makes the thumb, and prints <paramref name="text"/> in colour 0x1f when
    /// given.
    /// </summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view text) override;
    /// <summary>Frees the text.</summary>
    void OnDestroy() override;
    /// <summary>Resizes the window, its port (at least as tall as the lines) and moves the thumb.</summary>
    void Resize(int32_t newWidth, int32_t newHeight) override;
    /// <summary>Draws the section highlights and the lines into the port.</summary>
    void Draw() override;
    /// <summary>Port: draws itself each frame: the whole text in a port as tall as it, scrolled by FirstPixel.</summary>
    bool DrawsLive() override { return true; }
    /// <summary>Shows the visible part of the port (from <see cref="FirstPixel"/>).</summary>
    void Display() override;

    /// <summary>Appends a line in <paramref name="color"/>, growing the port to the lines.</summary>
    virtual void Print(std::string_view line, uint8_t color);
    /// <summary>Appends a blank line (the port isn't grown).</summary>
    virtual void PrintBlank(uint8_t color);
    /// <summary>Appends <paramref name="line"/>, wrapped at spaces to <paramref name="wrapWidth"/> pixels (-1: the width).</summary>
    virtual void PrintWrapped(std::string_view line, uint8_t color, int32_t wrapWidth);
    /// <summary>Empties the text and the sections, and scrolls to the top.</summary>
    virtual void Clear();

    /// <summary>
    /// Takes <paramref name="other"/>'s text as it is now: its lines, sections, scroll position, tab stop and font,
    /// with the port grown to the lines and the thumb placed. For a view of another box's text (the mission results'
    /// debriefing shows the tactical map's salvage text).
    /// </summary>
    void CopyTextFrom(const MCGuiScrollTextObject& other);
    /// <summary>Grows the port to the lines.</summary>
    void ResetPortSize();
    /// <summary>The first visible pixel row for thumb position <paramref name="thumbY"/>.</summary>
    void CalcFirstPixel(int32_t thumbY);
    /// <summary>Sizes and places the thumb for the scroll position (hidden when the text fits).</summary>
    void PositionScrollTab();
    /// <summary>
    /// Scrolls: <paramref name="direction"/> -1 a line up, 1 a line down, 0 a page towards <paramref name="yPos"/> (a
    /// click on the track above or below the thumb).
    /// </summary>
    void ReceiveClick(int32_t direction, int32_t yPos);
    /// <summary>Port-only: the mouse wheel scrolls a line per notch, as the arrows do. Not taken when the text fits.</summary>
    bool MouseWheel(int32_t steps, int32_t xPos, int32_t yPos) override;
    /// <summary>The height of a line: the font's plus 2.</summary>
    int32_t LineHeight() const;

    /// <summary>The first pixel row of the port shown.</summary>
    int32_t FirstPixel = 0;
    /// <summary>
    /// The colours of the highlighted sections (0xff by init); the tactical map's weapon list sets them to the range
    /// colours.
    /// </summary>
    std::array<uint8_t, NumSections> SectionColors = {};
    /// <summary>The line each section highlights (-1 for none, by init and <see cref="Clear"/>).</summary>
    std::array<int32_t, NumSections> SectionStarts = {};
    /// <summary>
    /// The text: lines of (colour byte, text, newline). A line in colour 0 ends the text as drawn, as the original's
    /// C string did.
    /// </summary>
    std::string TextBuffer;
    /// <summary>The scroll thumb (a child).</summary>
    MCGuiHandle<MCGuiObject> ScrollTab;
    /// <summary>The number of lines printed.</summary>
    int32_t NumLines = 0;
    /// <summary>Set when init or a port resize failed.</summary>
    bool InitFailed = false;
    /// <summary>
    /// The x a tab in a line jumps to (only the first tab of a line counts); negative turns tabs into spaces. Never set
    /// by the object itself.
    /// </summary>
    int32_t TabStop = 0;
    /// <summary>The font column of <c>Fonts</c>.</summary>
    int32_t FontIndex = 0;
};

/// <summary>The scroll text thumb's paint routine.</summary>
void PaintScrollTab(MCGuiObject& obj);
/// <summary>The scroll text thumb's event routine: dragging it scrolls the text.</summary>
void ScrollTabEventHandler(MCGuiObject& obj, MCGuiEvent& event);

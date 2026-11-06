#pragma once

#include "logistics/MCLogObject.h"

/// <summary>
/// Port-only: a preferences list of rows of one height under a line of column titles, above a status line, scrolled by
/// the wheel, the up, down, page up and page down keys and a bar at its right: the CONTROLS list
/// (<see cref="MCLogKeyList"/>) and the HD OPTIONS list (<see cref="MCLogOptionList"/>). The list derived draws its
/// rows and passes its events through <see cref="HandleScrollMouse"/> and <see cref="ScrollByKey"/>; this draws the
/// frame, the line under the titles, the scroll bar and the status line, in the preferences panel's style.
/// </summary>
class MCLogScrollList : public MCLogObject
{
public:
    /// <summary>Destroys the object if nothing did (its cleanup lets go of the mouse).</summary>
    ~MCLogScrollList() override;

    /// <summary>Lets go of the mouse if the thumb is being dragged.</summary>
    void OnDestroy() override;

    /// <summary>The wheel scrolls the rows.</summary>
    bool MouseWheel(int32_t steps, int32_t xPos, int32_t yPos) override;

    /// <summary>Scrolls so the first row shown is <paramref name="row"/> (clamped).</summary>
    void ScrollTo(int32_t row);

    /// <summary>The first row shown.</summary>
    int32_t FirstRow() const { return _FirstRow; }

    /// <summary>The rows shown at once.</summary>
    int32_t VisibleRows();

protected:
    /// <summary>
    /// Places in the list's pixels: the column titles' top, the first row's top, the status line's height (its line
    /// included), the scroll bar's width; the text's offset in a row line, and the left edges of the headings, of the
    /// rows' names and of the text in a box at a row's right.
    /// </summary>
    static constexpr int32_t TitleTop = 4;
    static constexpr int32_t RowsTop = 16;
    static constexpr int32_t StatusHeight = 16;
    static constexpr int32_t ScrollBarWidth = 9;
    static constexpr int32_t TextOffset = 2;
    static constexpr int32_t HeadingLeft = 6;
    static constexpr int32_t NameLeft = 14;
    static constexpr int32_t BoxTextLeft = 4;

    /// <summary>
    /// The text's colours: headings (amber), rows' names (the labels' colour), column titles (grey), a box's text, the
    /// status line's hint and its news (amber); the lines between parts and around the boxes, and a box's outline under
    /// the mouse.
    /// </summary>
    static constexpr uint8_t HeadingColor = 0x31;
    static constexpr uint8_t NameColor = 0xe3;
    static constexpr uint8_t TitleColor = 0x19;
    static constexpr uint8_t BoxTextColor = 0x1d;
    static constexpr uint8_t HintColor = 0xe3;
    static constexpr uint8_t NewsColor = 0x33;
    static constexpr uint8_t LineColor = 0x14;
    static constexpr uint8_t HoverLineColor = 0xe3;

    /// <summary>A list of rows <paramref name="rowHeight"/> tall.</summary>
    explicit MCLogScrollList(int32_t rowHeight) : _RowHeight(rowHeight) {}

    /// <summary>The number of rows.</summary>
    virtual int32_t RowCount() = 0;

    /// <summary>The row under the screen y, if it is a shown row.</summary>
    std::optional<int32_t> RowAt(int32_t yPos);

    /// <summary>The top of shown row <paramref name="row"/> in the list's pixels, if it is shown.</summary>
    std::optional<int32_t> RowTop(int32_t row);

    /// <summary>The left edge of the scroll bar, in the list's pixels.</summary>
    int32_t ScrollBarLeft() { return Width() - 1 - ScrollBarWidth; }

    /// <summary>
    /// The scroll bar's part of a mouse event: a press on the bar drags the thumb or pages toward the press, and the
    /// thumb follows the mouse until the button is let go.
    /// </summary>
    /// <returns>Whether the event was the scroll bar's.</returns>
    bool HandleScrollMouse(const MCGuiEvent& event);

    /// <summary>Passes <paramref name="event"/> to the screen the list is on (Escape leaves it).</summary>
    void PassToScreen(MCGuiEvent& event);

    /// <summary>The up and down keys scroll a row, page up and page down a page.</summary>
    /// <returns>Whether <paramref name="key"/> scrolled.</returns>
    bool ScrollByKey(int32_t key);

    /// <summary>Back to the first row, the thumb let go.</summary>
    void ResetScroll();

    /// <summary>
    /// Draws the list's fill and outline, the line under the column titles, the scroll bar (the track, and the thumb
    /// showing where the rows shown are) and the status line saying <paramref name="status"/> (amber when
    /// <paramref name="news"/>); the list derived draws the titles and rows.
    /// </summary>
    void DrawFrame(std::string_view status, bool news);

    /// <summary>
    /// Writes <paramref name="text"/> at (<paramref name="xPos"/>, <paramref name="yPos"/>) in <paramref name="color"/>,
    /// cut at <paramref name="maxWidth"/>.
    /// </summary>
    /// <returns>The width written.</returns>
    int32_t Write(int32_t xPos, int32_t yPos, std::string_view text, uint8_t color, int32_t maxWidth);

    /// <summary>Draws a box's outline in <paramref name="color"/> from (<paramref name="left"/>, <paramref name="top"/>) to (<paramref name="right"/>, <paramref name="bottom"/>).</summary>
    void DrawOutline(int32_t left, int32_t top, int32_t right, int32_t bottom, uint8_t color);

private:
    /// <summary>The height of the scroll bar's track (from <see cref="RowsTop"/>), in the list's pixels.</summary>
    int32_t TrackHeight();

    /// <summary>The scroll bar's thumb: its top and height in the list's pixels.</summary>
    std::pair<int32_t, int32_t> Thumb();

    /// <summary>Whether the screen point lies on the scroll bar.</summary>
    bool OnScrollBar(int32_t xPos, int32_t yPos);

    /// <summary>The row the thumb's top at the list's y <paramref name="thumbTop"/> shows first.</summary>
    int32_t RowForThumb(int32_t thumbTop);

    /// <summary>Scrolls by the press at the scroll bar's screen y: drags the thumb, or pages toward the press.</summary>
    void PressScrollBar(int32_t yPos);

    /// <summary>Lets go of the thumb.</summary>
    void ReleaseThumb();

    /// <summary>The height of a row.</summary>
    int32_t _RowHeight;
    /// <summary>The first row shown.</summary>
    int32_t _FirstRow = 0;
    /// <summary>The thumb is being dragged: the press's offset from the thumb's top.</summary>
    std::optional<int32_t> _ThumbGrab;
};

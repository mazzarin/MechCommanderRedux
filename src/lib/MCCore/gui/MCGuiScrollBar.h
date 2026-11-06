#pragma once

#include "gui/MCGuiObject.h"
#include "gui/MCGuiRef.h"

/// <summary>The messages of a scroll bar: its parts post them to it, and it posts <see cref="Changed"/> to its parent.</summary>
namespace MCGuiScrollMessage
{
    /// <summary>The up arrow: a line up.</summary>
    inline constexpr int32_t LineUp = 0x65;
    /// <summary>The down arrow: a line down.</summary>
    inline constexpr int32_t LineDown = 0x66;
    /// <summary>The track above the thumb: a page up.</summary>
    inline constexpr int32_t PageUp = 0x67;
    /// <summary>The track below the thumb: a page down.</summary>
    inline constexpr int32_t PageDown = 0x68;
    /// <summary>The thumb was dragged: <see cref="MCGuiEvent::LParam"/> holds the new position.</summary>
    inline constexpr int32_t ThumbDragged = 0x6a;
    /// <summary>Sets the position (<see cref="MCGuiEvent::LParam"/>) without telling the parent.</summary>
    inline constexpr int32_t SetPositionQuietly = 0x6b;
    /// <summary>What the bar posts its parent when the position changed.</summary>
    inline constexpr int32_t Changed = 0x6c;
}

/// <summary>
/// An arrow or a track area of an <see cref="MCGuiScrollBar"/>: an object that posts its message to the bar while
/// pressed (at once, then after a second every 200 ms).
/// </summary>
/// <remarks>
/// The original's <c>aScrollButton</c> (its source file isn't named by the line tables) and <c>aScrollArea</c>
/// (<c>gui\ascroll.cpp</c>).
/// </remarks>
class MCGuiScrollButton : public MCGuiObject
{
public:
    /// <summary>Port: an arrow only shows its background picture.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>The message it posts (<see cref="MCGuiScrollMessage"/>).</summary>
    int32_t Message = 0;
};

/// <summary>
/// The empty track of an <see cref="MCGuiScrollBar"/> above or below the thumb: an invisible object (no port) whose
/// clicks page the bar.
/// </summary>
/// <remarks>Original source: <c>gui\ascroll.cpp</c> and <c>gui\ascroll.h</c> (<c>aScrollArea</c>).</remarks>
class MCGuiScrollArea : public MCGuiScrollButton
{
public:
    /// <summary>Places the area and makes its pane, without a port.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    /// <summary>Resizes the pane only.</summary>
    void Resize(int32_t newWidth, int32_t newHeight) override;
    /// <summary>Draws nothing.</summary>
    void Draw() override {}
    /// <summary>Displays nothing.</summary>
    void Display() override {}
    /// <summary>Draws nothing in the frame pass.</summary>
    bool DrawsLive() override { return false; }
    /// <summary>Passes the event to the event routine only.</summary>
    void HandleEvent(MCGuiEvent& event) override;
};

/// <summary>A vertical scroll bar 18 pixels wide: two arrows, the two track areas and the thumb.</summary>
/// <remarks>
/// Original source: <c>gui\ascroll.cpp</c> (<c>aScrollBar</c>). It reports to its parent with
/// <see cref="MCGuiScrollMessage::Changed"/>.
/// </remarks>
class MCGuiScrollBar : public MCGuiObject
{
public:
    /// <summary>Makes the arrows (art packets 0x27/0x28), the areas and the thumb; 18 pixels wide whatever <paramref name="width"/>.</summary>
    /// <returns>0, or a part's error.</returns>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    void Draw() override;
    /// <summary>Moves the position for its parts' messages, and tells the parent.</summary>
    void HandleEvent(MCGuiEvent& event) override;
    /// <summary>Port: the bar draws itself each frame (its thumb is placed by <see cref="ResizeAreas"/>).</summary>
    bool DrawsLive() override { return true; }

    /// <summary>Sets the range (0 hides the thumb) and lays the bar out.</summary>
    void SetScrollMax(int32_t newMax);
    /// <summary>Sets the position (clamped to 0..<see cref="ScrollMax"/>) and lays the bar out.</summary>
    void SetScrollPos(int32_t newPos);
    /// <summary>Places the thumb for the position and sizes the two areas around it.</summary>
    void ResizeAreas();

    /// <summary>The largest position (0: no thumb).</summary>
    int32_t ScrollMax = 0;
    /// <summary>The position, 0..<see cref="ScrollMax"/>.</summary>
    int32_t ScrollPos = 0;
    /// <summary>The arrows at the ends (the bar's children).</summary>
    MCGuiHandle<MCGuiScrollButton> UpButton;
    MCGuiHandle<MCGuiScrollButton> DownButton;
    /// <summary>The track above and below the thumb (the bar's children).</summary>
    MCGuiHandle<MCGuiScrollArea> UpArea;
    MCGuiHandle<MCGuiScrollArea> DownArea;
    /// <summary>The thumb, a child painted by <see cref="ScrollTabPaint"/>.</summary>
    MCGuiHandle<MCGuiObject> ScrollTab;
};

/// <summary>The arrows' and areas' event routine: posts their message to the bar, repeating while held.</summary>
void ScrollEventHandler(MCGuiObject& obj, MCGuiEvent& event);
/// <summary>The thumb's event routine: drags it.</summary>
void ScrollTabHandler(MCGuiObject& obj, MCGuiEvent& event);
/// <summary>The thumb's paint routine.</summary>
void ScrollTabPaint(MCGuiObject& obj);

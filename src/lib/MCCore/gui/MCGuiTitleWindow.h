#pragma once

#include "gui/MCGuiHolderObject.h"
#include "gui/MCGuiTitleBar.h"

// The framed windows: a title bar above, frame bars left, right and below, and a resize handle.

/// <summary>The paint routines of the camera windows' bottom, left and right frame bars.</summary>
void CameraBottomBarPaint(MCGuiObject& object);
void CameraLeftBarPaint(MCGuiObject& object);
void CameraRightBarPaint(MCGuiObject& object);
/// <summary>The paint routines of a title window's bottom, left and right frame bars.</summary>
void BottomBarPaint(MCGuiObject& object);
void LeftBarPaint(MCGuiObject& object);
void RightBarPaint(MCGuiObject& object);
/// <summary>
/// The event routine of a title window's resize handle: a press grabs the mouse, dragging resizes the window (the
/// handle's parent's parent), a release lets go.
/// </summary>
void HandleResizeButtonEvent(MCGuiObject& object, MCGuiEvent& event);

/// <summary>
/// The frame of a window: the title bar, the three frame bars and the resize handle, all the window's children
/// (the handle is the right bar's in a title window).
/// </summary>
struct MCGuiWindowFrame
{
    /// <summary>The frame bars left, below and right of the client area.</summary>
    MCGuiHandle<MCGuiObject> LeftBar;
    MCGuiHandle<MCGuiObject> BottomBar;
    MCGuiHandle<MCGuiObject> RightBar;
    /// <summary>The resize handle.</summary>
    MCGuiHandle<MCGuiObject> ResizeButton;
    /// <summary>The title bar above the client area.</summary>
    MCGuiHandle<MCGuiTitleBar> TitleBar;
};

/// <summary>
/// A framed window: a title bar above (13 pixels), frame bars left, right and below, and a resize handle in the right
/// bar's bottom corner.
/// </summary>
/// <remarks>Original source: <c>gui\awindow.cpp</c> (<c>aTitleWindow</c>).</remarks>
class MCGuiTitleWindow
    : public MCGuiObject
    , public MCGuiWindowFrame
{
public:
    /// <summary>Destroys the frame (the original's destructor calls <see cref="Destroy"/>).</summary>
    ~MCGuiTitleWindow() override;

    /// <summary>
    /// Makes the title bar (width + 12, 13 high, above the window), the three frame bars and the resize handle, then
    /// moves the window 13 pixels down to make room for the bar.
    /// </summary>
    /// <returns>0, or the first frame part's error.</returns>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    void Draw() override;
    /// <summary>
    /// Port: draws itself each frame (and so do its frame parts and title bar). Derived windows that still paint
    /// into a picture outside draw say otherwise.
    /// </summary>
    bool DrawsLive() override { return true; }
    /// <summary>Resizes the window and moves and resizes its frame.</summary>
    void Resize(int32_t newWidth, int32_t newHeight) override;

    /// <summary>Sets the title bar's text.</summary>
    void SetTitle(std::string_view newTitle);
};

/// <summary>A window with a title bar and frame whose client area is an <see cref="MCGuiHolderObject"/>'s two panes.</summary>
/// <remarks>Original source: <c>gui\awindow.cpp</c> (<c>aEmptyTitleWindow</c>).</remarks>
class MCGuiEmptyTitleWindow
    : public MCGuiHolderObject
    , public MCGuiWindowFrame
{
public:
    /// <summary>Destroys the frame (the original's destructor calls <see cref="Destroy"/>).</summary>
    ~MCGuiEmptyTitleWindow() override;

    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    void Resize(int32_t newWidth, int32_t newHeight) override;
    /// <summary>Close takes the window off the screen (the camera goes off); zoom toggles the view's zoom.</summary>
    void HandleEvent(MCGuiEvent& event) override;
    /// <summary>Sets the title bar's back colour (not the window's).</summary>
    void SetBackColor(int32_t color) override;

    /// <summary>Sets the title bar's text.</summary>
    void SetTitle(std::string_view newTitle);
};

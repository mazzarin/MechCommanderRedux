#pragma once

#include "gui/MCGuiButton.h"
#include "gui/MCGuiRef.h"

class MCGuiFont;

/// <summary>
/// A button on a title bar (the swoopy button): a button whose events go straight to <see cref="MCGuiObject"/>'s
/// handling (its event routine), not a button's.
/// </summary>
/// <remarks>Original source: <c>gui\awindow.h</c> (<c>aTitleButton</c>).</remarks>
class MCGuiTitleButton : public MCGuiButton
{
private:
    void HandleEvent(MCGuiEvent& event) override;
};

/// <summary>
/// A window's title bar: the title text, and a close, two zoom and a "swoopy" button, each of which posts its message
/// to the window (the bar's parent). Dragging the bar moves the window.
/// </summary>
/// <remarks>Original source: <c>gui\awindow.cpp</c> (<c>aTitleBar</c>).</remarks>
class MCGuiTitleBar : public MCGuiObject
{
public:
    /// <summary>Makes the four buttons; the title is <paramref name="name"/> (a space when null).</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    void Draw() override;
    /// <summary>Port: draws itself each frame from its title and buttons.</summary>
    bool DrawsLive() override { return true; }
    /// <summary>Drags the window (the parent) while the left button is held on the bar.</summary>
    void HandleEvent(MCGuiEvent& event) override;
    /// <summary>Resizes and moves the close and swoopy buttons to the right edge.</summary>
    void Resize(int32_t newWidth, int32_t newHeight) override;

    /// <summary>Points the two zoom buttons' callbacks at the window (<see cref="MCGuiEventType::ZoomIn"/>, <see cref="MCGuiEventType::ZoomOut"/>).</summary>
    void SetZoomCallbacks();
    /// <summary>Sets the title text.</summary>
    void SetTitle(std::string_view newTitle);
    /// <summary>Shows or hides the close button; showing points its callback at the window (<see cref="MCGuiEventType::Close"/>).</summary>
    void ShowCloseButton(bool show);
    /// <summary>Shows or hides the zoom button.</summary>
    void ShowZoomButton(bool show) const;
    /// <summary>Shows or hides both zoom buttons.</summary>
    void ShowZoomButtons(bool show) const;
    /// <summary>Shows or hides the swoopy button.</summary>
    void ShowSwoopyButton(bool show) const;
    /// <summary>Whether <paramref name="newWidth"/> leaves room for the shown buttons (4 pixels, plus each button).</summary>
    bool ResizeOK(int32_t newWidth) const;

    /// <summary>The title's font (the black one).</summary>
    std::shared_ptr<MCGuiFont> Font;
    /// <summary>The title text.</summary>
    std::string Title;
    /// <summary>The close button, at the right end.</summary>
    MCGuiHandle<MCGuiCloseButton> CloseButton;
    /// <summary>The zoom buttons, at the left end (hidden by default).</summary>
    MCGuiHandle<MCGuiButton> ZoomButton;
    MCGuiHandle<MCGuiButton> ZoomOutButton;
    /// <summary>The swoopy button, left of the close button (hidden by default).</summary>
    MCGuiHandle<MCGuiTitleButton> SwoopyButton;
};

/// <summary>The paint routine of a title bar: its gradient lines.</summary>
void TitleBarPaint(MCGuiObject& object);
/// <summary>The event routine of the title bar's "swoopy" button: a click flips the window's camera's swoopy flag.</summary>
void HandleSwoopyButtonEvent(MCGuiObject& object, MCGuiEvent& event);

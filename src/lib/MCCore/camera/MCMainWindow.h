#pragma once

#include "gui/MCGuiRef.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCGuiHolderObject.h"

/// <summary>The screen-filling holder of the camera panes, with the mission clock pane.</summary>
class MCMainWindow : public MCGuiHolderObject
{
public:
    ~MCMainWindow() override;
    /// <summary>Opens the holder over the whole application window.</summary>
    int32_t Init();
    /// <summary>aHolderObject::init, then makes the clock pane (40 wide, one line of lineFont).</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    /// <summary>Frees the clock pane, then aHolderObject::destroy.</summary>
    void OnDestroy() override;
    /// <summary>Follows the application window's size on resize events.</summary>
    void HandleEvent(MCGuiEvent& event) override;
    /// <summary>Draws the panes, and the mission clock once per time step when the scenario has a time limit.</summary>
    void Display() override;
    /// <summary>Activates the inactive pane's camera when tiling, then aHolderObject::SetTiled.</summary>
    void SetTiled(bool tiled) override;
    /// <summary>aHolderObject::Retile, then moves the clock pane to the active pane.</summary>
    void Retile() override;
    void SetVertical(bool on) override;
    void SetActivePane(MCGuiObject& pane) override;
    /// <summary>Zooms the active pane's view (not while paused or asked) and redraws its terrain.</summary>
    void ZoomActivePane();
    using MCGuiHolderObject::Init;

    /// <summary>The pane the mission clock is drawn in.</summary>
    MCGuiRef<MCGuiObject> ClockPane;
    /// <summary>The time the clock was last drawn (-1 by init).</summary>
    float LastClockTime = -1.0f;
};

/// <summary>
/// The main window that holds the camera panes (the camera list's; null outside a mission and before a camera makes
/// it); the caller keeps it as its pin.
/// </summary>
std::shared_ptr<MCMainWindow> MainHolder();

/// <summary>Zooms the main window's active pane.</summary>
void ToggleZoom();

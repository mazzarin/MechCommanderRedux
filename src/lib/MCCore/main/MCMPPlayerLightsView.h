#pragma once

#include "logistics/MCLogObject.h"

class MCGuiEvent;
class MCMPPlayerLights;

/// <summary>
/// A multiplayer screen's view of the ready lights (<see cref="MCMPPlayerLights"/>): their place, pictures and size.
/// Only the view on the screen the lights are on shows; it draws the lights from their state each frame, blinks them on
/// the timer, and puts a pointed-at player's name on the ticker.
/// </summary>
/// <remarks>Original source: <c>logistics.cpp</c> (<c>MPPlayerLights</c>, the GUI side of it).</remarks>
class MCMPPlayerLightsView : public MCLogObject
{
public:
    /// <summary>The blink timer's id.</summary>
    static constexpr int32_t BlinkTimer = 3;

    /// <summary>Places the view, hidden, showing <paramref name="lights"/>, and loads the pictures.</summary>
    void Init(std::weak_ptr<MCMPPlayerLights> lights);

    /// <summary>
    /// Shows the view (in front; at depth <paramref name="depth"/> when one is given), or hides it: the lights are on
    /// this view's screen or not.
    /// </summary>
    void Show(bool show, std::optional<int32_t> depth);

    /// <summary>Makes the view as wide as the lights there are.</summary>
    void FitLights();

    /// <summary>Starts the blink timer (every 500 ms).</summary>
    void StartBlinkTimer();

    /// <summary>
    /// Paints the lights: each player's numbered light, lit (<c>lsc_ph</c>) or blinking over it. Port: draws the lights
    /// from the state each frame; the parent screen draws the backing (<see cref="MCLogistics::DrawScreenChrome"/>).
    /// </summary>
    void Draw() override;

    /// <summary>Port: the lights draw themselves each frame (their port is a view).</summary>
    bool DrawsLive() override { return true; }

    /// <summary>
    /// While the view shows: the blink timer toggles the blink; pointing at a light shows the player's name on the
    /// ticker.
    /// </summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>The width of one light.</summary>
    int32_t LightWidth = 0;
    /// <summary>The unlit lights (<c>lsc_pn.tga</c>).</summary>
    std::shared_ptr<MCLogPort> LightsPort;
    /// <summary>The ready light (<c>lsc_pg.tga</c>).</summary>
    std::shared_ptr<MCLogPort> ReadyPort;
    /// <summary>The blinking light (<c>lsc_pg1.tga</c>).</summary>
    std::shared_ptr<MCLogPort> BlinkPort;

private:
    /// <summary>The lights shown (the logistics screen's).</summary>
    std::weak_ptr<MCMPPlayerLights> _Lights;
};

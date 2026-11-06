#pragma once

#include "logistics/MCLogObject.h"

class MCGuiEvent;
class MCTicker;

/// <summary>
/// A logistics screen's view of the ticker (<see cref="MCTicker"/>): where the line goes on the screen and what is
/// under it. The screen draws it each frame from the ticker's state (<see cref="DrawLine"/>). Only the line on the
/// screen the ticker is on is visible; it runs the scroll timer and passes its events on to the ticker.
/// </summary>
/// <remarks>Original source: <c>logistics\ticker.cpp</c> (<c>Ticker</c>, the GUI side of it).</remarks>
class MCTickerLine : public MCLogObject
{
public:
    /// <summary>
    /// Places the line at (<paramref name="xPos"/>, <paramref name="yPos"/>), <paramref name="width"/> wide, showing
    /// <paramref name="ticker"/>, hidden until <see cref="Show"/>.
    /// </summary>
    void Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::weak_ptr<MCTicker> ticker);

    /// <summary>
    /// Starts the scroll timer (every 75 ms). The original's ticker ran it from its start; the line showing the ticker
    /// has it, and hands it on to the next one as it is (<see cref="MCGuiSystem::HandOver"/>).
    /// </summary>
    void StartScrolling();

    /// <summary>
    /// Shows the line (in front, as the original's ticker was added to the screen last, and raised again with
    /// <paramref name="shuffle"/>, see <see cref="MCGuiObject::BringToFront"/>), or hides it.
    /// </summary>
    void Show(bool show, bool shuffle);

    /// <summary>Nothing: the screen draws the line (<see cref="DrawLine"/>).</summary>
    void Draw() override {}

    /// <summary>Passes the event on to the ticker: the scroll step timer scrolls it, anything else hides a wide text.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>Nothing: the screen draws the line (<see cref="DrawLine"/>).</summary>
    void Display() override {}

    /// <summary>Keeps a copy of <paramref name="port"/> to draw under the text.</summary>
    void SetBackPane(MCGuiPort& port);

    /// <summary>
    /// Port: draws the line from the ticker's state into <paramref name="target"/>: the back pane, then the text (a
    /// wide one scrolled, and only while it scrolls).
    /// </summary>
    void DrawLine(const MCPane& target) const;

    /// <summary>A copy of the background under the line (<see cref="SetBackPane"/>).</summary>
    std::shared_ptr<MCLogPort> BackPane;
    /// <summary>Where in the screen the line goes.</summary>
    int32_t XPos = 0;
    int32_t YPos = 0;

private:
    /// <summary>The ticker shown (the logistics screen's).</summary>
    std::weak_ptr<MCTicker> _Ticker;
};

#pragma once

class MCGuiFont;

/// <summary>
/// The scrolling message line along the top of the logistics screens (help text for whatever the mouse is over): what
/// it shows and how far it has scrolled. A text wider than the line scrolls through it after half a line of gap. Each
/// screen that shows the line has its own view of it (<see cref="MCTickerLine"/>); the text and the scroll position
/// are shared, so they stay the same from one screen to the next.
/// </summary>
/// <remarks>
/// Original source: <c>logistics\ticker.cpp</c> (<c>Ticker</c>). The original's ticker was one GUI object that the
/// logistics screens moved from screen to screen; its state is this model, owned by the logistics screen
/// (<c>MCLogistics::Ticker</c>).
/// </remarks>
class MCTicker
{
public:
    /// <summary>The scroll timer's id, and the id the line scrolls on (OB-072).</summary>
    static constexpr int32_t ScrollTimer = 4;
    static constexpr int32_t ScrollStepTimer = 7;

    /// <summary>A line <paramref name="width"/> pixels wide, measuring its text with <paramref name="font"/>.</summary>
    MCTicker(int32_t width, std::shared_ptr<MCGuiFont> font);

    /// <summary>Shows <paramref name="text"/> (an empty one clears the line); unchanged text is ignored.</summary>
    void SetString(std::string_view text);

    /// <summary>
    /// An event to the line showing it: a scroll step (<paramref name="scrollStep"/>) advances a wide text two pixels
    /// (wrapping round) and shows it; any other event hides it again (the original painted the back pane alone).
    /// </summary>
    void Advance(bool scrollStep);

    /// <summary>The text shown.</summary>
    std::string Text;
    /// <summary>How far the text has scrolled, in pixels.</summary>
    int32_t ScrollPos = 0;
    /// <summary>The font measuring the text (it is written with <c>MedWhiteFont</c>).</summary>
    std::shared_ptr<MCGuiFont> Font;
    /// <summary>The width of the text, plus half a line of gap when it scrolls; 0 = empty.</summary>
    int32_t TextWidth = 0;
    /// <summary>The line's width.</summary>
    int32_t MaxWidth = 0;
    /// <summary>A text wider than the line shows (the last event was a scroll step).</summary>
    bool ScrollShown = false;
};

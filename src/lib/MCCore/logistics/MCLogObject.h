#pragma once

#include "gui/MCGuiRef.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCOptionalRef.h"
#include "logistics/MCLogPort.h"

/// <summary>
/// The <see cref="MCGuiEvent::Data"/> of the <see cref="MCGuiEventType::Focus"/> events the logistics controls get
/// and send: the focus itself, and the notices a control sends its parent.
/// </summary>
namespace MCLogNotice
{
    /// <summary>A file pane's selection moved to a file (<see cref="MCGuiEvent::LParam"/>).</summary>
    inline constexpr int32_t FileSelected = 1;
    /// <summary>A file pane's selection went (<see cref="MCGuiEvent::LParam"/> is the file asked for).</summary>
    inline constexpr int32_t FileCleared = 2;
    /// <summary>A game list's selection moved to a game.</summary>
    inline constexpr int32_t GameSelected = 3;
    /// <summary>A game list's selected game went from the list.</summary>
    inline constexpr int32_t GameLost = 4;
    /// <summary>A text field was finished (Enter, or the focus moving on).</summary>
    inline constexpr int32_t EntryDone = 5;
    /// <summary>The control got the keyboard.</summary>
    inline constexpr int32_t FocusGained = 7;
    /// <summary>The control lost the keyboard.</summary>
    inline constexpr int32_t FocusLost = 8;
}

/// <summary>Plays the interface sample <paramref name="sampleId"/> (a click, a hover, a refusal).</summary>
void PlayLogSound(uint32_t sampleId);

/// <summary>
/// Port: the state of the places the main logistics screens share (the four screen buttons, the ticker line, the
/// resource points and the clock), which <c>Logistics::drawScreenChrome</c> draws each frame. The original painted
/// them into each screen's own picture when an event changed them.
/// </summary>
struct MCLogScreenChrome
{
    /// <summary>The screen button lit under the mouse, or -1.</summary>
    int32_t HoveredButton = -1;
    /// <summary>The briefing button's chat blink phase (lit while true; shown while the chat is unread).</summary>
    bool BlinkLit = false;

    /// <summary>Back to no button lit and the blink unlit (the screen was set up again).</summary>
    void Clear() { *this = MCLogScreenChrome{}; }
};

/// <summary>
/// The base of every logistics-screen widget: an <see cref="MCGuiObject"/> that draws into a logistics port (its own,
/// or one lent to it) and may have a still background image in a second port.
/// </summary>
/// <remarks>Original source: <c>logistics\lport.cpp</c> (<c>lObject</c>).</remarks>
class MCLogObject : public MCGuiObject
{
public:
    /// <summary>
    /// Places the object at (<paramref name="xPos"/>, <paramref name="yPos"/>) with the given size and makes its own
    /// port of that size (a view when it <see cref="MCGuiObject::DrawsLive"/>).
    /// </summary>
    /// <returns>0, or 3 when out of memory.</returns>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height);

    /// <summary>
    /// Places the object without a port of its own: it draws into ports others hand it (a pane's rows, its screen's
    /// view). The original's init took the port given and kept it, but nothing drew into it.
    /// </summary>
    void InitWithoutPort(int32_t xPos, int32_t yPos, int32_t width, int32_t height);

    /// <summary>Frees the object's own port and its background (the base's destroy does the rest).</summary>
    void OnDestroy() override;

    /// <summary>The port the object draws into, or null.</summary>
    std::shared_ptr<MCLogPort> Lport() const { return _Port; }

    /// <summary>Draws the background (or the animation when iconized) and the children into the port.</summary>
    void Draw() override;

    /// <summary>Handles a pending hide/slide, then copies the port to the screen and displays the children.</summary>
    void Display() override;

    /// <summary>Resizes the object and its port.</summary>
    void Resize(int32_t width, int32_t height) override;

    /// <summary>Fills a rectangle of the port (bitmap coordinates, inclusive) with <paramref name="color"/>.</summary>
    void FillBox(int16_t left, int16_t top, int16_t right, int16_t bottom, uint8_t color);

    /// <summary>Loads <paramref name="fileName"/> as the background port.</summary>
    int32_t SetBackground(std::string_view fileName) override;
    using MCGuiObject::SetBackground;

    /// <summary>Port: the shared places this screen shows (<see cref="MCLogScreenChrome"/>), or null for other objects.</summary>
    virtual MCOptionalRef<MCLogScreenChrome> Chrome() { return std::nullopt; }

protected:
    /// <summary>Makes <paramref name="port"/> the object's own port: drawn into, and freed with the object.</summary>
    void SetOwnPort(std::shared_ptr<MCLogPort> port);

    /// <summary>
    /// The port drawn into: the object's own or one shared with it (a ticker draws into its screen's, a scroll pane
    /// into its content); null for an object without one.
    /// </summary>
    std::shared_ptr<MCLogPort> _Port;
    /// <summary>The still background image (<see cref="SetBackground"/>).</summary>
    std::shared_ptr<MCLogPort> _BackgroundPort;
};

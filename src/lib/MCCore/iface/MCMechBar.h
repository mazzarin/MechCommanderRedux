#pragma once

#include "gui/MCGuiRef.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCFriendlyMechIcon.h"
#include "iface/MCLanceIcon.h"

/// <summary>
/// The bar along the bottom of the tactical screen: one <see cref="MCFriendlyMechIcon"/> per player mover, grouped
/// behind four <see cref="MCLanceIcon"/>s. When lances change the buttons "dance" into their new places.
/// </summary>
/// <remarks>
/// Original source: <c>iface\iface.cpp</c> (<c>aMechBar</c>). The original kept the layout fields in a virtual base
/// whose class name was lost (<c>aMechBarLayout</c> in the symbol map); they are plain fields here.
/// </remarks>
class MCMechBar : public MCGuiObject
{
public:
    /// <summary>The buttons the bar holds (kept: a player's force of twelve fills the 640-pixel bar).</summary>
    static constexpr size_t MaxButtons = 12;
    /// <summary>The lance icons (kept: the commander's four lances, F1-F4).</summary>
    static constexpr size_t NumLances = 4;

    ~MCMechBar() override = default;

    /// <summary>A bar with no bitmap of its own (it draws on its parent).</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view bitmapName) override;
    /// <summary>Removes the buttons and lance icons (<see cref="CleanUp"/>), in the original's order.</summary>
    void OnDestroy() override;
    /// <summary>Draws the children, the lance separators and each button's frame colour.</summary>
    void Display() override;
    /// <summary>Keeps the bar at the bottom of the screen and passes events to the interface.</summary>
    void HandleEvent(MCGuiEvent& event) override;
    void Resize(int32_t width, int32_t height) override;

    /// <summary>Removes (destroys) every button, reserve icon and lance icon.</summary>
    void CleanUp();

    /// <summary>
    /// Makes a button on the bar, set up by <paramref name="setup"/> (its <c>Init</c>), and appends it after the
    /// others.
    /// </summary>
    /// <returns>The button, or an empty handle (nothing made) when the bar is full.</returns>
    template <typename Setup>
        requires std::invocable<Setup&, MCFriendlyMechIcon&>
    MCGuiHandle<MCFriendlyMechIcon> AddButton(Setup&& setup)
    {
        if (_Buttons.size() >= MaxButtons)
        {
            return {};
        }

        const int32_t xPos = (SpacingX + 1) * static_cast<int32_t>(_Buttons.size()) + 2;
        MCGuiHandle<MCFriendlyMechIcon> button = CreateChild<MCFriendlyMechIcon>(
            [&](MCFriendlyMechIcon& made)
            {
                setup(made);
                made.MoveTo(xPos, 0xf, false);
            });
        _Buttons.push_back(button);
        return button;
    }

    /// <summary>
    /// Makes the icon of a mover kept off the bar, set up by <paramref name="setup"/>: a hidden child, never shown or
    /// placed, that the mission results draw the mover's picture from (<see cref="GetReserveIcon"/>).
    /// </summary>
    template <typename Setup>
        requires std::invocable<Setup&, MCFriendlyMechIcon&>
    void AddReserveIcon(Setup&& setup)
    {
        _ReserveIcons.push_back(CreateChild<MCFriendlyMechIcon>(
            [&](MCFriendlyMechIcon& made)
            {
                setup(made);
                made.ShowGuiWindow(false);
            }));
    }

    /// <summary>The buttons, in the order they were added, pinned for a walk.</summary>
    std::vector<std::shared_ptr<MCFriendlyMechIcon>> Buttons() const;
    /// <summary>The lance icons (null before <see cref="InitLances"/>), pinned for a walk.</summary>
    std::array<std::shared_ptr<MCLanceIcon>, NumLances> LanceIcons() const;
    /// <summary>Button <paramref name="index"/>, or null out of range.</summary>
    std::shared_ptr<MCFriendlyMechIcon> GetButton(size_t index) const;
    /// <summary>The button of the mover with part id <paramref name="partId"/>, or null.</summary>
    std::shared_ptr<MCFriendlyMechIcon> GetButtonFromID(int32_t partId) const;
    /// <summary>The reserve icon of the mover with part id <paramref name="partId"/>, or null.</summary>
    std::shared_ptr<MCFriendlyMechIcon> GetReserveIcon(int32_t partId) const;
    /// <summary>
    /// Sorts the buttons by lance and places them behind their lance icons; with <paramref name="animate"/> they move
    /// there over several frames (<see cref="Dance"/>).
    /// </summary>
    void PlaceButtons(bool animate);
    /// <summary>Makes the four lance icons.</summary>
    void InitLances();
    /// <summary>The icon of lance <paramref name="lanceId"/>, or null.</summary>
    std::shared_ptr<MCLanceIcon> GetLanceIconFromID(int32_t lanceId) const;

    /// <summary>Moves the buttons one frame toward their places (the dance callback, <c>DancingButtons</c>).</summary>
    void Dance();

    /// <summary>Whether the buttons are dancing into place.</summary>
    bool Dancing = false;
    /// <summary>The part id of the mover whose icon the mouse is over (-1 = none).</summary>
    int32_t HighlightId = -1;
    /// <summary>The part id of the mover whose pilot is on the video window (-1 = none).</summary>
    int32_t VideoId = -1;
    /// <summary>The gap between buttons.</summary>
    int32_t SpacingX = 4;
    int32_t SpacingY = 4;

private:
    /// <summary>The buttons, in the order they were added (children of the bar).</summary>
    std::vector<MCGuiHandle<MCFriendlyMechIcon>> _Buttons;
    /// <summary>The icons of movers kept off the bar (hidden children, <see cref="AddReserveIcon"/>).</summary>
    std::vector<MCGuiHandle<MCFriendlyMechIcon>> _ReserveIcons;
    /// <summary>The lance icons (children of the bar, made by <see cref="InitLances"/>).</summary>
    std::array<MCGuiHandle<MCLanceIcon>, NumLances> _LanceIcons;
    /// <summary>The current phase (0-2) of the dance.</summary>
    uint8_t _DanceStep = 0;
    /// <summary>Frames since the dance started.</summary>
    int16_t _DanceFrames = 0;
    /// <summary>The dance callback, live while the buttons move.</summary>
    std::shared_ptr<MCGuiCallback> _DanceCallback;
};

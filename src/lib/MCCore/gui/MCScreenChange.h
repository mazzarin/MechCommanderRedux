#pragma once

// Port-only (SPEC-menus.md's stage 1): a change of screen as one continuous sequence. The screen shown fades out while
// it stays live, the screen stays dark only while work is left, and the next screen fades in once its first frame is
// drawn: no fade hides a load or waits for one, and nothing is held on a frozen frame. The fades are the presenter's
// (MCDisplay::SetFade) over frames drawn as usual, set by the game's clock, so a frame loop's own pace shows them.
// Used on the way into a mission and back (MCMission); the changes between menus keep the palette fade
// (MCGuiSystem::FadeDownCurrentPalette) until the menus' own screen flow (stage 2).

/// <summary>Port-only: the 1998 screens' size, the frame the menus not yet remade sit in, in the window's middle.</summary>
inline constexpr int32_t MenuFrameWidth = 640;
inline constexpr int32_t MenuFrameHeight = 480;

namespace MCScreenChange
{
    /// <summary>How long the screen shown takes to fade out, and the next to fade in (SPEC-menus.md's Motion).</summary>
    inline constexpr uint32_t FadeMs = 150;
    /// <summary>The fade's level that is black (MCDisplay::SetFade).</summary>
    inline constexpr int Black = 255;

    /// <summary>Where a change is.</summary>
    enum class MCPhase
    {
        /// <summary>No change: the screen is shown as drawn.</summary>
        None,
        /// <summary>The screen shown fades out, drawn live.</summary>
        Out,
        /// <summary>Black: the fade-out shown whole; the change's work runs (<see cref="Dark"/>).</summary>
        Dark,
        /// <summary>The next screen fades in from its first frame (<see cref="FadeIn"/>).</summary>
        In
    };

    /// <summary>
    /// Starts change <paramref name="name"/> (the frame log's name for it): the screen shown fades out from the next
    /// frame, and the player's clicks and keys are held until the change is over. False, and nothing started, while a
    /// change runs or the screen can't be shown (no display).
    /// </summary>
    bool Start(std::string_view name);

    /// <summary>The change's phase now.</summary>
    MCPhase Phase();

    /// <summary>The running change's name; empty with none.</summary>
    std::string_view Name();

    /// <summary>Whether the screen is black, the fade-out shown whole: the change's work runs from here.</summary>
    bool Dark();

    /// <summary>
    /// The change's work done: the next frame drawn is the next screen's first, and it fades in from there. Nothing
    /// unless <see cref="Dark"/>.
    /// </summary>
    void FadeIn();

    /// <summary>Whether a change runs (from <see cref="Start"/> until the fade-in is over).</summary>
    bool Running();

    /// <summary>Whether the player's clicks and keys are held now (a change runs).</summary>
    bool HoldsInput();

    /// <summary>
    /// The fade the frame about to be shown takes (UpdateDisplay sets it on the display): 0 with no change, else by the
    /// game's clock from the phase's start.
    /// </summary>
    int FadeNow();

    /// <summary>
    /// A frame shown (UpdateDisplay, after its present) with the fade <see cref="FadeNow"/> gave: the fade-out shown
    /// black ends in <see cref="MCPhase::Dark"/>, the fade-in shown clear ends the change; and the frame's time is
    /// counted for the change's line in the frame log.
    /// </summary>
    void Presented(int fade);

    /// <summary>
    /// Ends a change at once, the screen shown as drawn (the game stopping, or a change whose work can't go on): its
    /// input given back.
    /// </summary>
    void Cancel();

    /// <summary>Port-only, for the tests: how many changes have ended (faded in whole) since the start.</summary>
    size_t Finished();
}

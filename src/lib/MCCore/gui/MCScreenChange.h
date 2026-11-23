#pragma once

// Port-only (SPEC-menus.md's stages 1 and 2): the one place that changes screens, with the one fade. A change is
// described (MCChange) and run here, a frame at a time (Tend): its job starts at the press and runs a piece at a time in
// each frame's budget while the screen shown stays live; the screen shown fades out live, the screen stays dark only
// while the change's work is left, and the next screen fades in once its first frame is drawn: no fade hides a load or
// waits for one, and nothing is held on a frozen frame. Before the screen is dark a change can be called off, the
// screen shown kept. The fades are the presenter's (MCDisplay::SetFade) over frames drawn as usual, set by the game's
// clock, so a frame loop's own pace shows them. Used on the way into a mission and back (MCMission); the changes between
// menus keep the palette fade (MCGuiSystem::FadeDownCurrentPalette) until the menus' own screen flow (stage 2).

/// <summary>Port-only: the 1998 screens' size, the frame the menus not yet remade sit in, in the window's middle.</summary>
inline constexpr int32_t MenuFrameWidth = 640;
inline constexpr int32_t MenuFrameHeight = 480;

namespace MCScreenChange
{
    /// <summary>How long the screen shown takes to fade out, and the next to fade in (SPEC-menus.md's Motion).</summary>
    inline constexpr uint32_t FadeMs = 150;
    /// <summary>The fade's level that is black (MCDisplay::SetFade).</summary>
    inline constexpr int Black = 255;
    /// <summary>
    /// How long a frame's pieces of a change may run (a 240 Hz refresh), so the screen stays live, or dark, only as long
    /// as their work takes; a slow piece takes a frame of its own.
    /// </summary>
    inline constexpr double FrameBudgetMs = 4.0;

    /// <summary>Where a change is.</summary>
    enum class MCPhase
    {
        /// <summary>No change: the screen is shown as drawn.</summary>
        None,
        /// <summary>
        /// The job runs, the screen shown live and its input given to it (it can call the change off), before its
        /// fade-out (<see cref="MCFadeOut::AfterJob"/>).
        /// </summary>
        Working,
        /// <summary>The screen shown fades out, drawn live.</summary>
        Out,
        /// <summary>Black: the fade-out shown whole; the change's work left runs.</summary>
        Dark,
        /// <summary>The next screen fades in from its first frame.</summary>
        In
    };

    /// <summary>When the screen shown starts to fade out.</summary>
    enum class MCFadeOut
    {
        /// <summary>At the press: the job runs on as it fades out, and what's left of it once the screen is dark.</summary>
        AtPress,
        /// <summary>Once the job is done (<see cref="MCPhase::Working"/> until then).</summary>
        AfterJob
    };

    /// <summary>A piece of a change's work, run in one frame.</summary>
    struct MCPiece
    {
        /// <summary>The frame log's part its time goes in (MCFrameLog::Scope); none when empty.</summary>
        std::string_view Part;
        std::function<void()> Run;
    };

    /// <summary>A change of screen, as <see cref="Start"/> runs it.</summary>
    struct MCChange
    {
        /// <summary>The frame log's name for it.</summary>
        std::string Name;
        /// <summary>
        /// The change it carries on from, when that was the last to end (the way into a mission's two, around the load):
        /// its line in the frame log also gives the time from that one's press (BEGIN MISSION's, to the battlefield's
        /// first whole frame). Empty for none.
        /// </summary>
        std::string After;
        /// <summary>Work started at the press, a piece at a time while the screen shown is live.</summary>
        std::vector<MCPiece> Job;
        MCFadeOut FadeOut = MCFadeOut::AtPress;
        /// <summary>
        /// Work done once the screen is dark, after the job's: the screen shown taken down, the next one made. The next
        /// screen fades in once none is left.
        /// </summary>
        std::vector<MCPiece> Dark;
        /// <summary>Undoes the job's work when the change is called off before the screen is dark (<see cref="Cancel"/>).</summary>
        std::function<void()> CalledOff;
        /// <summary>Called as the change ends, the next screen faded in whole (another change, say).</summary>
        std::function<void()> Then;
    };

    /// <summary>
    /// Starts change <paramref name="change"/> at the press: its job from the next <see cref="Tend"/>, the screen shown
    /// fading out from the next frame (or once the job is done). False, and nothing started, while a change runs or the
    /// screen can't be shown (no display).
    /// </summary>
    bool Start(MCChange change);

    /// <summary>
    /// The change's work for a frame, before the frame's logic (MCGuiSystem::RunFrameCallbacks): its pieces left in the
    /// phase, while the frame's budget lasts (<see cref="FrameBudgetMs"/>); the job done, the fade-out after it; the
    /// work done once dark, the fade-in.
    /// </summary>
    void Tend();

    /// <summary>Runs pieces <paramref name="pieces"/> at once, each in its part (a change's work done without a change).</summary>
    void RunPieces(std::span<const MCPiece> pieces);

    /// <summary>The change's phase now.</summary>
    MCPhase Phase();

    /// <summary>The running change's name; empty with none.</summary>
    std::string_view Name();

    /// <summary>Whether the screen is black, the fade-out shown whole: the change's last work runs.</summary>
    bool Dark();

    /// <summary>Whether a change runs (from <see cref="Start"/> until the fade-in is over).</summary>
    bool Running();

    /// <summary>Whether the screen fades for a change, or is dark (from the fade-out's start to the fade-in's end).</summary>
    bool Fading();

    /// <summary>Whether the player's clicks and keys are held now (<see cref="Fading"/>).</summary>
    bool HoldsInput();

    /// <summary>
    /// The fade the frame about to be shown takes (UpdateDisplay sets it on the display): 0 with no change or while its
    /// job runs live, else by the game's clock from the phase's start.
    /// </summary>
    int FadeNow();

    /// <summary>
    /// A frame shown (UpdateDisplay, after its present) with the fade <see cref="FadeNow"/> gave: the fade-out shown
    /// black ends in <see cref="MCPhase::Dark"/>, the fade-in shown clear ends the change (its Then called); and the
    /// frame's time is counted for the change's line in the frame log.
    /// </summary>
    void Presented(int fade);

    /// <summary>
    /// Ends a change at once, the screen shown as drawn (the game stopping, or the player calling it off): before the
    /// screen is dark, its CalledOff undoes its job and the screen shown stays; from dark on, its work left is dropped.
    /// Its input given back. Nothing with none running.
    /// </summary>
    void Cancel();

    /// <summary>Port-only, for the tests: how many changes have ended (faded in whole) since the start.</summary>
    size_t Finished();
}

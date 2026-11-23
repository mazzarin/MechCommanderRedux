#pragma once

#include "gui/MCScreenChange.h"
#include "lib/MCOptionalRef.h"

// Port-only (SPEC-menus.md's stage 2, "The screens' flow"): the menus' flow, over MCScreenChange's changes. Every change
// of screen goes through here, with the one fade: the player's presses, the network's events and the ways into a
// mission and back. The flow keeps the back stack: the screens walked through to the one shown, the one shown on top.
// Escape (and every screen's way back) goes back one (Back); a screen's own Leave first (a question, a session left).
// Where the way out of a mission lands sets the stack under it (Land), so Escape from there goes back as if the player
// had walked there. The stack's end is the screen at its bottom: its Leave is the end's (the main menu's QUIT question,
// a launcher's room's "Leave the game?").
//
// A screen is a registered kind (MCScreenKind): the old screens (MCLogistics', registered by logistics/MCMenuScreens)
// and the remade ones (RmlUi documents, ui/MCRmlScreen), so the two hand over to each other until the last is remade.
// The old screens' own code still hides one and shows the next: run here in the dark of a change (Change, Via), and the
// stack follows the screen that came up (the kinds' Shows says which).
//
// The stack and the kinds are kept across a mission: the menus' data outlives it (SPEC-menus.md).

namespace MCScreenFlow
{
    /// <summary>A screen's id: its place in the registry (<see cref="Register"/>).</summary>
    using MCScreenId = int32_t;

    /// <summary>No screen (the stack empty, a kind not found).</summary>
    inline constexpr MCScreenId NoScreen = -1;

    /// <summary>A screen the flow shows: one of the old screens, or a remade one.</summary>
    struct MCScreenKind
    {
        /// <summary>The frame log's and the tests' name for it ("internet").</summary>
        std::string Name;
        /// <summary>
        /// Live, from the press, a piece at a time within the frame's budget while the screen before stays shown and
        /// fades out: its document read, its pictures asked for, its first layout. Empty for none.
        /// </summary>
        std::function<std::vector<MCScreenChange::MCPiece>()> Prepare;
        /// <summary>Dark: it becomes the screen shown (the one before hidden already, by its Hide).</summary>
        std::function<void()> Show;
        /// <summary>Dark: it stops being shown (another shows, or the menus go for a mission).</summary>
        std::function<void()> Hide;
        /// <summary>Dark, asked each frame after Show: its first frame is whole (every picture in). Empty: at once.</summary>
        std::function<bool()> Ready;
        /// <summary>
        /// Escape and its way back (<see cref="Back"/>): what leaving it does, at the press. Empty: the screen below
        /// shows (<see cref="ShowBelow"/>). A Leave asks its question, or leaves its session, then calls ShowBelow, or
        /// runs the old screen's way back (which shows the screen it goes to, in a change).
        /// </summary>
        std::function<void()> Leave;
        /// <summary>
        /// Whether the screen shown is this one (the old screens: the screen logistics shows now), for the stack to
        /// follow the old code's changes. Empty: only when opened here.
        /// </summary>
        std::function<bool()> Shows;
        /// <summary>
        /// A screen that gives way to the one it leads to (LOAD GAME to the briefing, SAVE GAME back to the campaign):
        /// it leaves the stack when another opens over it, so Escape there doesn't come back to it.
        /// </summary>
        bool GivesWay = false;
    };

    /// <summary>Registers a screen; its id for <see cref="Open"/>. A kind of the same name is replaced (its id kept).</summary>
    MCScreenId Register(MCScreenKind kind);

    /// <summary>The id of the kind named <paramref name="name"/>; <see cref="NoScreen"/> when none is registered.</summary>
    MCScreenId Find(std::string_view name);

    /// <summary>The kind of <paramref name="id"/>, while registered.</summary>
    MCOptionalRef<const MCScreenKind> Kind(MCScreenId id);

    /// <summary>Forward: opens screen <paramref name="id"/> over the one shown (pushed on the stack), as a change.</summary>
    bool Open(MCScreenId id);

    /// <summary>The screen shown gives way to screen <paramref name="id"/> (the stack's top swapped), as a change.</summary>
    bool Replace(MCScreenId id);

    /// <summary>
    /// Back one: Escape on every screen, and every screen's way back. The top's Leave, at the press (or, with none, the
    /// screen below shown); at the stack's end, its bottom's Leave (the end's question). False while a change runs.
    /// </summary>
    bool Back();

    /// <summary>
    /// The screen below the one shown comes up, as a change (a Leave's last step): the top taken off the stack, hidden,
    /// and the one below shown. False at the stack's end, or while a change runs.
    /// </summary>
    bool ShowBelow();

    /// <summary>
    /// The stack the next screen to come up lands on (the way out of a mission, a rejoin): <paramref name="stack"/>,
    /// the landing's screen at its top. Taken by the next change's end (<see cref="Noted"/>) whose screen is its top.
    /// </summary>
    void Land(std::vector<MCScreenId> stack);

    /// <summary>The landing not yet taken (<see cref="Land"/>); empty with none.</summary>
    std::span<const MCScreenId> Landing();

    /// <summary>
    /// A change of screen the old code makes (it hides one screen and shows the next): <paramref name="work"/> run in the
    /// dark of a change named <paramref name="name"/>, the stack following the screen that comes up. Run at once when a
    /// change's piece calls it (it is dark already, or the change's own work) or nothing is shown (no display); queued
    /// for the running change's end when one is fading (the network's events). <paramref name="prepare"/>: work started
    /// at the press, live (the next screen's pictures).
    /// </summary>
    void Change(std::string name, std::function<void()> work, std::vector<MCScreenChange::MCPiece> prepare = {});

    /// <summary>
    /// The flow's work for a frame, after the change's (MCGuiSystem::RunFrameCallbacks): a change queued while another ran
    /// started once none runs.
    /// </summary>
    void Tend();

    /// <summary>A button's or key's action <paramref name="work"/> as a change of screen (<see cref="Change"/>).</summary>
    std::function<void()> Via(std::string name, std::function<void()> work);

    /// <summary>
    /// A change of screen just made (a change's dark work done, or the old code's at once): the stack follows the screen
    /// shown now (the kinds' Shows): taken back to it when it's in the stack, a landing taken when it's the landing's top,
    /// else pushed (over a screen that gives way, in its place).
    /// </summary>
    void Noted();

    /// <summary>Marks screen <paramref name="id"/> the one shown, the stack following it as for the old code (<see cref="Noted"/>).</summary>
    void NoteShown(MCScreenId id);

    /// <summary>The screen shown (the stack's top); <see cref="NoScreen"/> with none.</summary>
    MCScreenId Shown();

    /// <summary>The stack, from its bottom: the screen shown last.</summary>
    std::span<const MCScreenId> Stack();

    /// <summary>The stack's names, bottom first, joined by " > " (the frame log, the tests).</summary>
    std::string StackText();

    /// <summary>The stack emptied, its landing dropped and the changes queued (the game stopping, a test's start).</summary>
    void Clear();

    /// <summary>
    /// Port-only, for the tests: how many changes of screen the old code made outside a change (the screen logistics shows
    /// changed with the screen lit, not dark, and not by a slide): each was a cut, or the original's palette fade.
    /// </summary>
    size_t Unrouted();

    /// <summary>Counts a change of screen made outside a change (<see cref="Unrouted"/>), named <paramref name="what"/>.</summary>
    void NoteUnrouted(std::string_view what);
}

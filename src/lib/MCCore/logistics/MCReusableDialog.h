#pragma once

#include "logistics/MCLogDialogButton.h"

/// <summary>
/// The general logistics message dialog: word-wrapped text in a frame that grows with it, OK and optionally Cancel
/// buttons, and an optional timeout.
/// </summary>
/// <remarks>
/// Original source: <c>logistics\logdlg.cpp</c> (<c>ReusableDialog</c>). The logistics screen keeps one and reuses
/// it.
/// </remarks>
class MCReusableDialog : public MCLogObject
{
public:
    /// <summary>The timeout's timer id.</summary>
    static constexpr int32_t TimeoutTimer = 0;

    /// <summary>Loads the frame pieces (<c>dbox_top/middle/bottom</c>), centres the dialog and makes the two buttons.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;

    /// <summary>Frees the frame pieces and the text.</summary>
    void OnDestroy() override;

    /// <summary>Draws the frame and the text wrapped to the width.</summary>
    void Draw() override;

    /// <summary>Port: the dialog draws itself each frame.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>Enter presses OK (or Cancel on a one-button dialog), Escape cancels, the timeout closes it.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// Grabs the input, centres and shows the dialog, and starts the timeout timer if any. Port-only (SPEC-menus.md's
    /// stage 4): over a remade screen, said in the remade dialog instead (this one would show under it), its buttons
    /// running this one's as Enter and Escape do here, a key asked for passed to <see cref="KeyTaker"/>.
    /// </summary>
    void Activate();

    /// <summary>Port-only: shown, or said over the remade screens (<see cref="Activate"/>) while that shows.</summary>
    bool IsShowing() override;

    /// <summary>
    /// Hides the dialog and calls the callback with <paramref name="result"/>; unless <see cref="KeepCallbacks"/>
    /// is set, then clears the callbacks and the timeout.
    /// </summary>
    void Deactivate(int32_t result);

    /// <summary>
    /// Sets the text and resizes the dialog to fit it (a middle piece for each two lines). Port: a newline starts a
    /// line.
    /// </summary>
    void SetText(std::string_view newText);

    /// <summary>Shows or hides the Cancel button and places the buttons.</summary>
    void SetTwoButton(bool twoButtons);

    /// <summary>The Cancel button shows.</summary>
    bool TwoButton = false;
    /// <summary>How many middle frame pieces the text needs.</summary>
    int32_t NumMiddlePieces = 0;
    /// <summary>The text.</summary>
    std::string Text;
    /// <summary>Called by <see cref="Deactivate"/> with the result.</summary>
    std::function<void(int32_t)> Callback;
    /// <summary>Milliseconds before the dialog closes by itself; 0 = never.</summary>
    int32_t Timeout = 0;
    /// <summary>The result passed when the timeout closes the dialog.</summary>
    int32_t TimeoutResult = 0;
    /// <summary>When set, the next <see cref="Deactivate"/> keeps the callbacks (and clears this flag).</summary>
    bool KeepCallbacks = false;
    /// <summary>
    /// Port-only: when set, the dialog asks for a key. A key pressed while it shows (but Enter, Escape and a modifier
    /// alone) goes here in a binding's form (<see cref="MCKeyBindings::FromKeyEvent"/>); true closes the dialog as OK
    /// does (its callback with 0). Cleared with the callbacks. Whoever sets it gives the dialog the keyboard
    /// (<c>MCGuiSystem::SetText</c>, over a chat line's), which <see cref="Deactivate"/> gives back.
    /// </summary>
    std::function<bool(uint32_t key)> KeyTaker;
    /// <summary>The OK button (a child).</summary>
    MCGuiHandle<MCLogDialogButton> OkButton;
    /// <summary>The word a button's picture says ("Cancel" for <c>bh_cancl</c>, else "OK"), for the remade dialog.</summary>
    static std::string WordOf(const MCLogButton& button);
    /// <summary>The Cancel button (a child).</summary>
    MCGuiHandle<MCLogDialogButton> CancelButton;
    std::shared_ptr<MCLogPort> TopPiece;
    std::shared_ptr<MCLogPort> MiddlePiece;
    std::shared_ptr<MCLogPort> BottomPiece;

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 9), for the tests of the 1998 box itself: it shows as where the menus' RmlUi
    /// can't start (off: the remade dialog says it whenever the menus run).
    /// </summary>
    static bool& OldBoxOnly();

protected:
    /// <summary>Draws the frame: the top, <see cref="NumMiddlePieces"/> middle pieces and the bottom.</summary>
    void DrawFrame();
    /// <summary>
    /// The width the text wraps to: from 0xc, where it starts, to 3 pixels inside the panel's right edge (10 in from
    /// the dialog's), as far as it starts inside the left. The original's Width() - 0x14 let a full line run over the
    /// right edge.
    /// </summary>
    int32_t WrapWidth() { return Width() - 0x19; }

private:
    /// <summary>Says the dialog over the remade screens (<see cref="Activate"/>); false when none shows.</summary>
    bool SayOverRemade();

    /// <summary>
    /// Whether the remade dialog still says what <see cref="SayOverRemade"/> said there (or will again, a change of
    /// screens having hidden it).
    /// </summary>
    bool StillSaid() const;

    /// <summary>Shown the old way: grabbing the screen, centred on what the window shows.</summary>
    void ShowOld();

    /// <summary>Port-only: said over the remade screens by its last <see cref="Activate"/>, not yet answered.</summary>
    bool _SaidOverRemade = false;
    /// <summary>What it said there.</summary>
    std::string _Said;
};

/// <summary>
/// The refit confirmation dialog: a <see cref="MCReusableDialog"/> whose text is a comma-separated list, drawn one
/// item per line between two fixed strings.
/// </summary>
/// <remarks>Original source: <c>logistics\logdlg.cpp</c> (<c>RefitDialog</c>).</remarks>
class MCRefitDialog : public MCReusableDialog
{
public:
    /// <summary>Sets the list and resizes the dialog for its lines.</summary>
    void SetText(std::string_view newText);

    /// <summary>Draws the frame, the header (string 0x54), the items and the footer (string 0x62).</summary>
    void Draw() override;

    /// <summary>Writes <paramref name="text"/> word-wrapped from line <paramref name="yPos"/>.</summary>
    /// <returns>The y of the last line written.</returns>
    int32_t WrapText(std::string_view text, int32_t yPos);

    /// <summary>The number of items in the list.</summary>
    int32_t NumItems = 0;
};

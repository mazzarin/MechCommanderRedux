#pragma once

#include "logistics/MCLogDialogButton.h"

class MCMenuDialog;

/// <summary>
/// The general logistics message dialog: word-wrapped text in a frame that grows with it, OK and optionally Cancel
/// buttons. Port-only (SPEC-menus.md's stage 9, step 2): the view of a dialog's model (<see cref="MCMenuDialog"/>, which
/// holds what it says, its answers and its timeout), which says itself on the remade dialog: this no longer shows.
/// </summary>
/// <remarks>
/// Original source: <c>logistics\logdlg.cpp</c> (<c>ReusableDialog</c>). The logistics screen keeps one and reuses
/// it.
/// </remarks>
class MCReusableDialog : public MCLogObject
{
public:
    /// <summary>Loads the frame pieces (<c>dbox_top/middle/bottom</c>), centres the dialog and makes the two buttons.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;

    /// <summary>Frees the frame pieces.</summary>
    void OnDestroy() override;

    /// <summary>Draws the frame and the model's text wrapped to the width.</summary>
    void Draw() override;

    /// <summary>Port: the dialog draws itself each frame.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>Enter presses OK (or Cancel on a one-button dialog), Escape cancels, a key asked for is passed on.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>The model it shows, whose buttons its own run.</summary>
    void SetModel(std::weak_ptr<MCMenuDialog> model);

    /// <summary>Port-only: its model shown, or said over the remade screens while that shows.</summary>
    bool IsShowing() override;

    /// <summary>Closes its model with <paramref name="result"/> (a button pressed).</summary>
    void Deactivate(int32_t result);

    /// <summary>
    /// Sized to the model's text (a middle piece for each two lines; port: a newline starts a line), its buttons placed
    /// and shown as the model's are.
    /// </summary>
    virtual void Lay();

    /// <summary>Hidden, the screen let go when <paramref name="release"/>.</summary>
    void HideOld(bool release);
    /// <summary>Shown the old way.</summary>
    bool ShownOld() { return MCLogObject::IsShowing(); }

    /// <summary>How many middle frame pieces the text needs.</summary>
    int32_t NumMiddlePieces = 0;
    /// <summary>The OK button (a child).</summary>
    MCGuiHandle<MCLogDialogButton> OkButton;
    /// <summary>The Cancel button (a child).</summary>
    MCGuiHandle<MCLogDialogButton> CancelButton;
    std::shared_ptr<MCLogPort> TopPiece;
    std::shared_ptr<MCLogPort> MiddlePiece;
    std::shared_ptr<MCLogPort> BottomPiece;

protected:
    /// <summary>Draws the frame: the top, <see cref="NumMiddlePieces"/> middle pieces and the bottom.</summary>
    void DrawFrame();
    /// <summary>
    /// The width the text wraps to: from 0xc, where it starts, to 3 pixels inside the panel's right edge (10 in from
    /// the dialog's), as far as it starts inside the left. The original's Width() - 0x14 let a full line run over the
    /// right edge.
    /// </summary>
    int32_t WrapWidth() { return Width() - 0x19; }
    /// <summary>Its buttons placed for one or two (as the model's), at the bottom of the frame.</summary>
    void PlaceButtons();
    /// <summary>The model's text, or none once the model is gone.</summary>
    std::string ModelText() const;
    /// <summary>The model's buttons' pictures, whether they can be pressed and whether they show, on its own.</summary>
    void SyncButtons();

    std::weak_ptr<MCMenuDialog> _Model;
    /// <summary>The pressed pictures its buttons were given (the up ones they keep themselves).</summary>
    std::string _OkDownArt;
    std::string _CancelDownArt;
};

/// <summary>
/// The refit confirmation dialog: a <see cref="MCReusableDialog"/> whose model's text is a comma-separated list,
/// drawn one item per line between two fixed strings.
/// </summary>
/// <remarks>Original source: <c>logistics\logdlg.cpp</c> (<c>RefitDialog</c>).</remarks>
class MCRefitDialog : public MCReusableDialog
{
public:
    /// <summary>Sized for the model's list's lines.</summary>
    void Lay() override;

    /// <summary>Draws the frame, the header (string 0x54), the items and the footer (string 0x62).</summary>
    void Draw() override;

    /// <summary>Writes <paramref name="text"/> word-wrapped from line <paramref name="yPos"/>.</summary>
    /// <returns>The y of the last line written.</returns>
    int32_t WrapText(std::string_view text, int32_t yPos);

    /// <summary>The number of items in the list.</summary>
    int32_t NumItems = 0;
};

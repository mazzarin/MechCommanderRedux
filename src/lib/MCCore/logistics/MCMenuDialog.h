#pragma once

// Port-only (SPEC-menus.md's stage 9, step 2, "Models and views"): the logistics screens' message dialog, question dialog
// and refit list as models, pulled out of the 1998 box (logistics/MCReusableDialog): what one says, its buttons and what
// they run, the callback its answer goes to, its timeout. Logistics owns them (MCLogistics::Message, Question, Refit), so
// they live as long as the boxes did. Shown, one says itself on the remade dialog (MCRmlDialogs) whenever the menus run;
// where they can't start, and in the 1998 box's own tests (MCReusableDialog::OldBoxOnly), its view, the 1998 box, shows
// it. What it says, its answers and its timeout are the box's, unchanged.

#include "gui/MCGuiRef.h"

class MCGuiObject;
class MCMenuDialogTimer;
class MCReusableDialog;

class MCMenuDialog final : public std::enable_shared_from_this<MCMenuDialog>
{
public:
    /// <summary>One of its buttons: its pictures in the 1998 box (which name its word), what it runs.</summary>
    struct MCButton
    {
        /// <summary>Its pictures in the 1998 box, up and pressed (<c>bh_okay.tga</c>, <c>bg_okay.tga</c>).</summary>
        std::string UpArt;
        std::string DownArt;
        /// <summary>It can't be pressed (a message's OK during a wait the player can't end); the remade dialog has none.</summary>
        bool Disabled = false;
        /// <summary>It shows in the 1998 box (a wait hides both: ShowWaitingDialog).</summary>
        bool Shown = true;
        /// <summary>What it runs when pressed, before the dialog closes.</summary>
        std::function<void()> Exec;

        /// <summary>Its pictures, up and pressed.</summary>
        void SetPictures(std::string_view up, std::string_view down);
        /// <summary>Runs <see cref="Exec"/> (from a copy: it may set another).</summary>
        void Run() const;
        /// <summary>Whether it runs <paramref name="func"/> (the logistics screens ask whether OK exits).</summary>
        bool Runs(void (&func)()) const;
        /// <summary>The word the remade dialog says on it: "Cancel" for the 1998 cancel picture (<c>bh_cancl</c>), else "OK".</summary>
        std::string Word() const;
    };

    /// <summary>The timeout's timer id (the 1998 box's).</summary>
    static constexpr int16_t TimeoutTimer = 0;

    MCMenuDialog();
    ~MCMenuDialog();
    MCMenuDialog(const MCMenuDialog&) = delete;
    MCMenuDialog& operator=(const MCMenuDialog&) = delete;

    /// <summary>
    /// Its view where the menus can't start: the 1998 box <paramref name="view"/>, which draws what this says and passes
    /// its keys and clicks here.
    /// </summary>
    void SetView(MCGuiHandle<MCReusableDialog> view);
    /// <summary>The 1998 box that shows it where the menus can't start (none once logistics closes it).</summary>
    const MCGuiHandle<MCReusableDialog>& View() const { return _View; }

    /// <summary>Sets the text (up to its first NUL). Port: a newline starts a line. Its OK shows again (<see cref="SetTwoButton"/>).</summary>
    void SetText(std::string_view text);
    /// <summary>Its CANCEL shown or not; its OK shown again (after a wait hid it).</summary>
    void SetTwoButton(bool two);

    /// <summary>
    /// Shows it and starts its timeout, if any: said on the remade dialog whenever the menus run (its buttons running
    /// these as Enter and Escape do), else shown by its view.
    /// </summary>
    void Activate();
    /// <summary>
    /// Hides it and calls <see cref="Callback"/> with <paramref name="result"/>; unless <see cref="KeepCallbacks"/> is
    /// set (it's cleared then), then clears the callbacks, the key taker and the timeout.
    /// </summary>
    void Deactivate(int32_t result);
    /// <summary>Shown, or said on the remade dialog and not yet answered (or coming back after a change of screens).</summary>
    bool IsShowing() const;

    /// <summary>Enter: OK runs, then the dialog closes with -1 for a question, 0 for a message.</summary>
    void Enter();
    /// <summary>
    /// Escape: a question's CANCEL (OB-074 (fixed): the original ran OK's callback, so Escape on "Are you sure you want to
    /// exit?" left the game), a message's one button (OK); the dialog closes with 0.
    /// </summary>
    void Escape();
    /// <summary>
    /// A key the dialog asks for (<see cref="KeyTaker"/>), in a binding's form: true closes the dialog as OK does (its
    /// callback with 0). Nothing when it asks for none.
    /// </summary>
    void TakeKey(uint32_t key);
    /// <summary>The 1998 box asking for a key gets the keyboard (over a chat line's), which <see cref="Deactivate"/> gives back.</summary>
    void TakeKeyboard();
    /// <summary>
    /// Closes it after <paramref name="milliseconds"/> with <see cref="TimeoutResult"/>, as its timeout would (the room's
    /// waiting dialog, closing itself once everyone has the mission's file).
    /// </summary>
    void CloseIn(int32_t milliseconds);

    /// <summary>The text.</summary>
    std::string Text;
    /// <summary>Its CANCEL shows.</summary>
    bool TwoButton = false;
    MCButton Ok;
    MCButton Cancel;
    /// <summary>Called by <see cref="Deactivate"/> with the result.</summary>
    std::function<void(int32_t)> Callback;
    /// <summary>Milliseconds before it closes by itself; 0 = never.</summary>
    int32_t Timeout = 0;
    /// <summary>The result passed when the timeout closes it.</summary>
    int32_t TimeoutResult = 0;
    /// <summary>When set, the next <see cref="Deactivate"/> keeps the callbacks (and clears this flag).</summary>
    bool KeepCallbacks = false;
    /// <summary>
    /// Port-only: when set, the dialog asks for a key: a key pressed while it shows (but Enter, Escape and a modifier
    /// alone) goes here in a binding's form (<c>MCKeyBindings::FromKeyEvent</c>); true closes the dialog as OK does (its
    /// callback with 0). Cleared with the callbacks. Whoever sets it gives the dialog the keyboard
    /// (<see cref="TakeKeyboard"/>).
    /// </summary>
    std::function<bool(uint32_t key)> KeyTaker;

private:
    /// <summary>Says it on the remade dialog (<see cref="Activate"/>); false when the 1998 box is to show it.</summary>
    bool SayOverRemade();
    /// <summary>
    /// The remade dialog's button pressed: as Enter (OK, -1 for a question) or Escape on a question (CANCEL), its say
    /// over first.
    /// </summary>
    void AnswerRemade(bool ok);
    /// <summary>Whether the remade dialog still says what <see cref="SayOverRemade"/> said (or will again).</summary>
    bool StillSaid() const;
    /// <summary>Shown the old way, by its view.</summary>
    void ShowOld();
    /// <summary>Its timeout started, if it has one.</summary>
    void StartTimeout();

    MCGuiHandle<MCReusableDialog> _View;
    /// <summary>The object its timeouts' timers go to (the GUI's timers go to GUI objects).</summary>
    MCGuiRef<MCMenuDialogTimer> _Timer;
    /// <summary>Said on the remade dialog by its last <see cref="Activate"/>, not yet answered.</summary>
    bool _SaidOverRemade = false;
    /// <summary>What it said there.</summary>
    std::string _Said;
};

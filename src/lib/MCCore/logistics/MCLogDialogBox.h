#pragma once

#include "logistics/MCLogObject.h"

/// <summary>
/// The logistics message box: a picture, one or two buttons and optionally a spinner over a fill; closing it calls
/// <see cref="Callback"/> with the button pressed.
/// </summary>
/// <remarks>Original source: <c>logistics\logdlg.cpp</c> (<c>LogDialogBox</c>).</remarks>
class MCLogDialogBox : public MCLogObject
{
public:
    /// <summary>Port: the part of the box shown pressed (the original copied its pressed art over the picture).</summary>
    enum class PressedPart : int32_t
    {
        None,
        Ok,
        Cancel,
        Up,
        Down,
    };

    /// <summary>Places the box (the <c>lspcb00</c> frame is drawn into it) and hides it.</summary>
    void Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height);

    /// <summary>Frees the picture and forgets the callback.</summary>
    void OnDestroy() override;

    /// <summary>
    /// The first time after <see cref="Activate"/>, shows a frame with the cursor hidden (the original also made a
    /// darkened snapshot of the screen behind the box, which its fill then covered); then no arrow or button shows
    /// pressed any more. (The original painted the box here.)
    /// </summary>
    void DrawBackground();

    /// <summary>
    /// Port: draws the box: its fill, the frame, the spinner arrows, the buttons and the picture, then the pressed
    /// arrow or button shown over them.
    /// </summary>
    void Draw() override;

    /// <summary>Port: the box draws itself each frame.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>Whether it shows a second (cancel) button.</summary>
    void SetTwoButton(bool twoButtons) { TwoButton = twoButtons; }

    /// <summary>Whether it shows the quantity spinner.</summary>
    void SetSpinner(bool spinner) { Spinner = spinner; }

    /// <summary>Grabs the input, draws and shows the box.</summary>
    void Activate();

    /// <summary>Releases the input, hides the box and calls the callback with <paramref name="result"/>.</summary>
    void Deactivate(int32_t result);

    void SetCallback(std::function<void(int32_t)> newCallback) { Callback = std::move(newCallback); }

    /// <summary>There is a cancel button.</summary>
    bool TwoButton = true;
    /// <summary>The quantity spinner is shown.</summary>
    bool Spinner = true;
    /// <summary>Called by <see cref="Deactivate"/> with the result.</summary>
    std::function<void(int32_t)> Callback;
    /// <summary>The picture shown in the box (a copy of the picture given to <c>MCPurchaseDlg::Init</c>).</summary>
    std::shared_ptr<MCLogPort> PicturePort;
    /// <summary>Set by <see cref="Activate"/>: the next <see cref="DrawBackground"/> shows a frame first.</summary>
    bool NeedBackground = false;

    /// <summary>Port: the arrow or button held down (until the release, or the box shows afresh).</summary>
    PressedPart Pressed = PressedPart::None;

protected:
    /// <summary>Port: draws the fill, the frame, the spinner arrows, the buttons and the picture.</summary>
    void DrawBox();
    /// <summary>Port: draws the pressed arrow or button, if any.</summary>
    void DrawPressed();
};

#pragma once

/// <summary>
/// What the screen tests share: the hash of the screen as shown, screenshots, and mouse and key input sent the way the
/// game's own input code makes it.
/// </summary>
namespace MCScreenInput
{
    /// <summary>
    /// FNV-1a of the screen as shown (its pixels with the world view composited under the key, in palette indices),
    /// with its size folded in first.
    /// </summary>
    uint32_t ScreenHash();

    /// <summary>--shots &lt;folder&gt;: saves the screen as <paramref name="name"/>.bmp there and prints its hash.</summary>
    void SaveShot(const std::string& name, uint32_t hash);

    /// <summary>
    /// --shots &lt;folder&gt;: saves the picture the window shows (the presenter's composite, read back from the GPU
    /// with Vulkan, at the window's pixels) as <paramref name="name"/>.png there.
    /// </summary>
    void SaveShownShot(const std::string& name);

    /// <summary>
    /// Sends the game a mouse event at (<paramref name="x"/>, <paramref name="y"/>) as CheckMouse makes them (type 1
    /// left down, 4 left up, 7 a move), with the cursor moved there first.
    /// </summary>
    void SendMouse(int32_t type, int32_t x, int32_t y, bool leftHeld);

    /// <summary>Moves the mouse to (<paramref name="x"/>, <paramref name="y"/>) and clicks there.</summary>
    void Click(int32_t x, int32_t y);

    /// <summary>Presses at (<paramref name="x0"/>, <paramref name="y0"/>), drags to (<paramref name="x1"/>, <paramref name="y1"/>) over a few frames and lets go.</summary>
    void Drag(int32_t x0, int32_t y0, int32_t x1, int32_t y1);

    /// <summary>Sends the game a key press of character <paramref name="key"/> (an event of type 10, as typing makes).</summary>
    void SendKey(uint8_t key);

    /// <summary>
    /// Moves the mouse to (<paramref name="x"/>, <paramref name="y"/>) as play does: an SDL motion event, then frames
    /// until the game's own input code (UpdateDisplay's cursor read, CheckMouse's move event) has seen it.
    /// </summary>
    void RealMove(int32_t x, int32_t y);

    /// <summary>
    /// Presses or lets go of the left button as play does: an SDL button event, then a frame for CheckMouse to turn
    /// it into the game's event.
    /// </summary>
    void RealButton(bool down);

    /// <summary>
    /// Moves the mouse to (<paramref name="x"/>, <paramref name="y"/>) and clicks there with SDL events, one frame
    /// held: what a player's click sends through CheckMouse.
    /// </summary>
    void RealClick(int32_t x, int32_t y);

    /// <summary>
    /// Presses and lets go of the key <paramref name="scancode"/> as play does: SDL key events (the game's WM_KEYDOWN,
    /// WM_CHAR and WM_KEYUP), a frame after each, with <paramref name="mod"/> held.
    /// </summary>
    void RealKey(SDL_Scancode scancode, SDL_Keymod mod = SDL_KMOD_NONE);

    /// <summary>
    /// Turns the wheel <paramref name="notches"/> notches (positive up, away from the player) at the mouse as play
    /// does: an SDL wheel event, then a frame.
    /// </summary>
    void RealWheel(int32_t notches);
}

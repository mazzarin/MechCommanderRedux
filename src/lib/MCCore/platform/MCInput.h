#pragma once

class MCDisplay;

/// <summary>A point on the logical screen (Win32's POINT, in client coordinates of the game's screen).</summary>
struct MCPoint
{
    int32_t x = 0;
    int32_t y = 0;
};

/// <summary>A rectangle on the logical screen, right and bottom exclusive (Win32's RECT).</summary>
struct MCClipRect
{
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};

/// <summary>
/// The game's window procedure as the platform calls it: the original's WndProc (<c>0x00612080</c>, which hands
/// input to <c>translateMessage</c> @ 0x00611860) without the HWND.
/// </summary>
/// <returns>The LRESULT. For WM_CLOSE, non-zero cancels the close (see <see cref="MCInput::PumpMessages"/>).</returns>
using MCWindowProc = std::function<int32_t(uint32_t message, uint32_t wParam, int32_t lParam)>;

/// <summary>
/// The message pump and the input state: SDL's events turned into the Win32 messages MCX.EXE's window procedure saw,
/// and the Win32 input queries its code makes (<c>GetAsyncKeyState</c>, <c>GetKeyState</c>, <c>GetCursorPos</c>,
/// <c>SetCursorPos</c>, <c>ShowCursor</c>, <c>ClipCursor</c>, <c>SetCapture</c>).
/// </summary>
/// <remarks>
/// <para>Messages carry the same values Windows gave: VK codes (platform/MCWin32Defs.h) and, for keys, an lParam of
/// repeat count (bits 0-15, always 1), PC set-1 scan code (16-23), extended-key flag (24), Alt context (29), previous
/// key state (30) and transition (31). Mouse messages carry MK_ flags in wParam and the position in lParam
/// (x low word, y high word, signed), both on the logical screen: window pixels mapped through the display's
/// letterbox, so the game sees 640x480 (or whatever the screen is) whatever the window's size.</para>
/// <para>What is sent: WM_KEYDOWN/WM_KEYUP, WM_SYSKEYDOWN/WM_SYSKEYUP (Alt held without Ctrl, or F10),
/// WM_CHAR (typed text in Windows-1252, plus the control characters Windows makes for Backspace, Tab, Enter,
/// Escape and Ctrl+letter), WM_MOUSEMOVE, WM_[LRM]BUTTONDOWN/UP, WM_[LRM]BUTTONDBLCLK (the window class had
/// CS_DBLCLKS), WM_MOUSEWHEEL, WM_ACTIVATEAPP (focus gained/lost, on change only), WM_PAINT (exposed),
/// WM_CLOSE / WM_DESTROY, and whatever <see cref="PostMessage"/> queued.</para>
/// <para>Typed text comes from SDL's text input (the system's input methods, accents and press-and-hold) only while
/// a text field has the keyboard (<see cref="SetTextInput"/>). Otherwise the system's input methods stay out of the
/// game's keys, and WM_CHAR carries the character the key makes in the keyboard layout, as TranslateMessage did
/// (cheat codes are typed so).</para>
/// <para>Threads: <see cref="PumpMessages"/> and the procedure run on the main thread. The state queries
/// (<see cref="GetAsyncKeyState"/>, <see cref="GetKeyState"/>, <see cref="GetCursorPos"/>) and
/// <see cref="PostMessage"/> may be called from any thread (the original read them from its mouse timer).</para>
/// </remarks>
namespace MCInput
{
    /// <summary>
    /// Connects the input to the display whose window it reads and whose logical screen it maps to, with SDL's text
    /// input as <see cref="SetTextInput"/> last asked. Sends WM_ACTIVATEAPP(1) at once if the window already has
    /// focus. The input refers to the display while it lives; an empty one detaches it.
    /// </summary>
    void Attach(std::weak_ptr<MCDisplay> attached);

    /// <summary>The display attached, held for the caller's use, or null.</summary>
    std::shared_ptr<MCDisplay> Display();

    /// <summary>Sets the game's window procedure; every message goes to it.</summary>
    void SetWindowProc(MCWindowProc proc);

    /// <summary>
    /// Whether a text field has the keyboard (the GUI's text focus): SDL's text input runs only then. It starts or
    /// stops when the pump next finds no key waiting, not at once: SDL decides whether a key types text as it reads
    /// the key, so a change among keys already read would lose or double their characters.
    /// </summary>
    void SetTextInput(bool on);

    /// <summary>
    /// Handles every pending event and posted message (the <c>PeekMessage</c> loop of <c>aSystem::run</c>).
    /// </summary>
    /// <returns>False once the game should quit (WM_QUIT: <see cref="PostQuitMessage"/>, or a close the procedure didn't cancel).</returns>
    bool PumpMessages();

    /// <summary>
    /// Waits up to <paramref name="timeoutMs"/> for an event, then pumps (<c>WaitMessage</c>, which the original
    /// called while inactive).
    /// </summary>
    bool WaitMessage(int timeoutMs);

    /// <summary>Asks the loop to end (<c>PostQuitMessage</c>): <see cref="PumpMessages"/> returns false from now on.</summary>
    void PostQuitMessage(int exitCode = 0);

    /// <summary>Whether WM_QUIT has been posted.</summary>
    bool QuitRequested();

    /// <summary>The code given to <see cref="PostQuitMessage"/>.</summary>
    int QuitCode();

    /// <summary>Queues a message for the procedure (<c>PostMessage</c>); delivered by the next pump. WM_QUIT quits.</summary>
    void PostMessage(uint32_t message, uint32_t wParam, int32_t lParam);

    /// <summary>
    /// <c>GetAsyncKeyState</c>: bit 15 set while the key (or mouse button: VK_LBUTTON, VK_RBUTTON, VK_MBUTTON) is
    /// down; bit 0 set if it was pressed since the previous call for that key. VK_SHIFT, VK_CONTROL and VK_MENU are
    /// down when either side is.
    /// </summary>
    int16_t GetAsyncKeyState(int vk);

    /// <summary><c>GetKeyState</c>: bit 15 while down, bit 0 toggled by each press (CapsLock, NumLock, ScrollLock start as the system has them).</summary>
    int16_t GetKeyState(int vk);

    /// <summary>Whether a key is down now.</summary>
    bool IsKeyDown(int vk);

    /// <summary>
    /// The cursor on the logical screen (<c>GetCursorPos</c> followed by the original's <c>MapWindowPoints</c> to the
    /// client), clamped to the screen.
    /// </summary>
    MCPoint GetCursorPos();

    /// <summary>Where the cursor was when the message being handled was made (<c>GetMessageCursorLoc</c> @ 0x00616140).</summary>
    MCPoint GetMessagePos();

    /// <summary>Moves the cursor to a point of the logical screen (<c>SetCursorPos</c> after <c>ClientToScreen</c>).</summary>
    void SetCursorPos(int32_t x, int32_t y);

    /// <summary>
    /// <c>ShowCursor</c>: adds 1 (show) or takes 1 (hide) from the display counter and returns it; the system cursor is
    /// visible while the counter is 0 or more. The game hides it at startup and draws its own.
    /// </summary>
    int ShowCursor(bool show);

    /// <summary>
    /// The game's own cursor as a system cursor (<see cref="MCCursor"/>), or null when the game shows none. It is
    /// shown while the <see cref="ShowCursor"/> counter hides the system's arrow, letterbox bars included.
    /// </summary>
    void SetGameCursor(std::shared_ptr<SDL_Cursor> cursor);

    /// <summary>
    /// Confines the cursor to a rectangle of the logical screen (<c>ClipCursor</c>). None frees it, back to the
    /// default: the picture while full screen and focused (kept off its corners: <see cref="GuardCorners"/>),
    /// anywhere otherwise.
    /// </summary>
    void ClipCursor(std::optional<MCClipRect> rect);

    /// <summary>
    /// How far (window points) the cursor is kept from the picture's left and right sides while full screen and
    /// focused: then the picture's corners are the screen's, where macOS's hot corners (Lock Screen, Mission
    /// Control...) fire, and edge scrolling pushes the cursor into them.
    /// </summary>
    inline constexpr int CornerGuardPoints = 2;

    /// <summary>
    /// The window area the cursor may cover full screen: <paramref name="picture"/>'s, <see cref="CornerGuardPoints"/>
    /// in from its left and right sides, so it reaches no corner (all of it, if too narrow for that).
    /// </summary>
    SDL_Rect GuardCorners(SDL_Rect picture);

    /// <summary>
    /// The logical x of a window x inside a <see cref="GuardCorners"/> area: <paramref name="x"/>, its plain mapping,
    /// except at the last point the cursor reaches at either side, which is that side of the screen (from
    /// <paramref name="left"/> to <paramref name="right"/>, exclusive), so edge scrolling still works there.
    /// </summary>
    int32_t GuardedX(float windowX, const SDL_Rect& guarded, int32_t x, int32_t left, int32_t right);

    /// <summary>Recomputes the cursor's area after the picture moved in the window without a window event (a new
    /// logical size).</summary>
    void RefreshMouseArea();

    /// <summary>Keeps mouse messages coming while a button is held outside the window (<c>SetCapture</c>).</summary>
    void SetCapture();

    /// <summary>Ends <see cref="SetCapture"/> (<c>ReleaseCapture</c>).</summary>
    void ReleaseCapture();

    /// <summary>The double-click interval in milliseconds (<c>GetDoubleClickTime</c>; 500 by default).</summary>
    uint32_t GetDoubleClickTime();

    /// <summary>Sets the double-click interval.</summary>
    void SetDoubleClickTime(uint32_t milliseconds);

    /// <summary>
    /// Translates one SDL event into messages and sends them, updating the key and mouse state. <see cref="PumpMessages"/>
    /// calls this for every event; tests call it with made-up events.
    /// </summary>
    void HandleEvent(const SDL_Event& event);

    /// <summary>Forgets every key, button, counter and queued message (tests).</summary>
    void Reset();

    /// <summary>The VK code Windows gives the key at <paramref name="scancode"/> (US layout for the symbol keys; letters follow <paramref name="key"/>).</summary>
    /// <param name="scancode">The physical key.</param>
    /// <param name="key">SDL's keycode for it in the current layout, or SDLK_UNKNOWN.</param>
    /// <param name="numLock">Whether NumLock is on (the keypad gives digits or navigation keys).</param>
    /// <returns>The VK code, or 0 for a key Windows has none for.</returns>
    int VirtualKeyFromScancode(SDL_Scancode scancode, SDL_Keycode key, bool numLock);

    /// <summary>A key's PC set-1 scan code and whether Windows flags it extended (the E0 prefix).</summary>
    struct MCScanCode
    {
        uint8_t Code = 0;
        bool Extended = false;
    };

    /// <summary>The set-1 scan code of a physical key.</summary>
    MCScanCode Win32ScanCode(SDL_Scancode scancode);

    /// <summary>
    /// The Windows-1252 character the key with set-1 scan code <paramref name="code"/> (0x1xx: extended) types alone in
    /// the keyboard layout, or -1 (a key that types none, such as the keypad's, space or Enter, or one the code page
    /// lacks).
    /// </summary>
    int LayoutCharacter(uint16_t code);

    /// <summary>The lParam of a key message.</summary>
    /// <param name="scan">The key's scan code.</param>
    /// <param name="altDown">Bit 29, the context code: Alt is held.</param>
    /// <param name="previousDown">Bit 30: the key was already down (a repeat, or any key-up).</param>
    /// <param name="releasing">Bit 31: the key is being released.</param>
    int32_t MakeKeyLParam(MCScanCode scan, bool altDown, bool previousDown, bool releasing, uint16_t repeatCount = 1);

    /// <summary>The lParam of a mouse message: x in the low word, y in the high word, both signed 16-bit.</summary>
    int32_t MakePointLParam(int32_t x, int32_t y);

    /// <summary>A Unicode code point as a Windows-1252 byte, or -1 when the code page has no such character.</summary>
    int CodePointToWindows1252(uint32_t codePoint);
}

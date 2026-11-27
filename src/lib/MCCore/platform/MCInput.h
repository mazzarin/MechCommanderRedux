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
    /// Port-only: whether the game draws its own cursor now (the software cursor, PREFS SoftwareCursor, with a shape
    /// up): into its frame, or, where the frame can't carry it at the mouse (under the layer's screens, drawn over the
    /// frame, or over the bars beside it), over the whole window (MCCursor::DrawShapeOver). The system shows none then
    /// (<see cref="ShownCursor"/>).
    /// </summary>
    void SetDrawnCursor(bool drawn);

    /// <summary>Port-only (SPEC-menus.md): what the layer over the game shows (<see cref="SetEventLayer"/>).</summary>
    enum class MCLayerShows
    {
        /// <summary>No screen: the mouse is the game's.</summary>
        Nothing,
        /// <summary>Screens over the game's picture (the pause over the battle): the mouse is theirs, kept to the picture.</summary>
        OverPicture,
        /// <summary>Screens that fill the window (the remade menus, whose screen is the window): the mouse is theirs, the window's.</summary>
        Window
    };

    /// <summary>The cursor the window shows (<see cref="ShownCursor"/>).</summary>
    enum class MCShownCursor
    {
        /// <summary>No cursor.</summary>
        None,
        /// <summary>The system's arrow.</summary>
        Arrow,
        /// <summary>The game's cursor as a system cursor (<see cref="SetGameCursor"/>).</summary>
        Game
    };

    /// <summary>What sets the cursor the window shows (<see cref="ShownCursor"/>).</summary>
    struct MCCursorShowing
    {
        /// <summary>The <see cref="ShowCursor"/> counter.</summary>
        int Counter = 0;
        /// <summary>Whether the game gave its cursor as a system cursor (<see cref="SetGameCursor"/>).</summary>
        bool GameCursor = false;
        /// <summary>Port-only: whether the game draws its cursor (<see cref="SetDrawnCursor"/>).</summary>
        bool DrawnCursor = false;
        /// <summary>Port-only: what the layer over the game shows (<see cref="SetEventLayer"/>).</summary>
        MCLayerShows Layer = MCLayerShows::Nothing;
        /// <summary>Whether the mouse is over the letterbox bars, not the picture.</summary>
        bool OutsidePicture = false;
    };

    /// <summary>
    /// The cursor the window shows: the system's arrow while the <see cref="ShowCursor"/> counter says so; else the
    /// game's cursor if it gave one; else none while the game draws its own (port-only: in its frame, or over the window
    /// where the frame can't carry it), or while the layer over the game shows a screen (a cursor the game hides stays
    /// hidden); else the arrow over the letterbox bars; else none.
    /// </summary>
    MCShownCursor ShownCursor(const MCCursorShowing& showing);

    /// <summary>
    /// Confines the cursor to a rectangle of the logical screen (<c>ClipCursor</c>). None frees it, back to the
    /// default (<see cref="MouseArea"/>): while full screen and focused, the picture, or the whole window while the
    /// layer over the game fills it (<see cref="SetEventLayer"/>), kept off its corners and foot (<see cref="GuardEdges"/>);
    /// anywhere otherwise.
    /// </summary>
    void ClipCursor(std::optional<MCClipRect> rect);

    /// <summary>
    /// How far (window points) the cursor is kept from the picture's left, right and bottom sides while full screen
    /// and focused: then the picture's sides are the screen's, where macOS fires its hot corners (Lock Screen, Mission
    /// Control...) and brings up the Dock (a full-screen window's Dock hides until the cursor reaches its edge: the
    /// user's, 26 Nov, at the remade screens' foot), and edge scrolling pushes the cursor there.
    /// </summary>
    inline constexpr int EdgeGuardPoints = 2;

    /// <summary>
    /// The window area the cursor may cover full screen: <paramref name="picture"/>'s, <see cref="EdgeGuardPoints"/>
    /// in from its left, right and bottom sides, so it reaches no corner and no Dock (each way kept whole, if too
    /// small for that). The top is left: full screen, SDL hides the menu bar there.
    /// </summary>
    SDL_Rect GuardEdges(SDL_Rect picture);

    /// <summary>
    /// The logical x of a window x inside a <see cref="GuardEdges"/> area: <paramref name="x"/>, its plain mapping,
    /// except at the last point the cursor reaches at either side, which is that side of the screen (from
    /// <paramref name="left"/> to <paramref name="right"/>, exclusive), so edge scrolling still works there.
    /// </summary>
    int32_t GuardedX(float windowX, const SDL_Rect& guarded, int32_t x, int32_t left, int32_t right);

    /// <summary>
    /// The logical y of a window y inside a <see cref="GuardEdges"/> area, as <see cref="GuardedX"/>: the last point
    /// the cursor reaches at the top or bottom is that side of the screen (<paramref name="top"/> to
    /// <paramref name="bottom"/>, exclusive).
    /// </summary>
    int32_t GuardedY(float windowY, const SDL_Rect& guarded, int32_t y, int32_t top, int32_t bottom);

    /// <summary>What sets where the system mouse may go (<see cref="MouseArea"/>), in window points.</summary>
    struct MCMouseAreaState
    {
        /// <summary>The game's ClipCursor rectangle, if it set one.</summary>
        std::optional<SDL_Rect> Clip;
        /// <summary>Whether the window is full screen and has the focus.</summary>
        bool FullscreenFocused = false;
        /// <summary>Port-only (SPEC-menus.md): whether the layer over the game fills the window (<see cref="SetEventLayer"/>).</summary>
        bool LayerFillsWindow = false;
        /// <summary>The picture: the shown part of the logical screen.</summary>
        SDL_Rect Picture{};
        /// <summary>The window's size.</summary>
        SDL_Point Window{};
    };

    /// <summary>
    /// Where the system mouse may go (<c>SDL_SetWindowMouseRect</c>; none: anywhere): the game's ClipCursor rectangle if
    /// it set one; else, full screen and focused, the whole window while the layer over the game fills it (port-only:
    /// the remade menus, whose screen is the window) and the picture otherwise (the game's screen, where its cursor
    /// stops), either kept off its corners and foot (<see cref="GuardEdges"/>); else anywhere.
    /// </summary>
    std::optional<SDL_Rect> MouseArea(const MCMouseAreaState& area);

    /// <summary>The window points the picture (the shown part of the logical screen) covers whole; none without a display.</summary>
    std::optional<SDL_Rect> PictureArea();

    /// <summary>Recomputes the cursor's area after the picture moved in the window without a window event (a new
    /// logical size).</summary>
    void RefreshMouseArea();

    /// <summary>
    /// Tests: the window taken as full screen and focused, where the mouse is kept (<see cref="MouseArea"/>): a hidden
    /// window is neither, so SDL would be asked for no area. SDL takes the area (<c>SDL_GetWindowMouseRect</c>) but
    /// confines no mouse to a window without the focus.
    /// </summary>
    void TakeAsFullscreen(bool taken);

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

    /// <summary>
    /// Port-only (Redux HD): holds the player's mouse and keys off the game while <paramref name="held"/> (a benchmark's
    /// run, main/MCBenchmark.h, the same play whoever's at the computer): the game's cursor stays where it was put
    /// (<see cref="SetCursorPos"/>) wherever the mouse goes, the mouse's moves, buttons and wheel and the keys reach
    /// nothing, and the keys down are let go. Quitting and closing the window still work, and Escape calls
    /// <paramref name="escape"/>.
    /// </summary>
    void HoldInput(bool held, std::function<void()> escape = {});

    /// <summary>
    /// Port-only (SPEC-menus.md, "A pause menu"): the keys down taken as let go, before a menu over the battle takes the
    /// keys (their release would never reach the game: a scroll key held as the pause opened would scroll on after).
    /// </summary>
    void LetGoOfKeys();

    /// <summary>
    /// Port-only (SPEC-menus.md): a layer over the game that sees the mouse's and keys' events first (the remade menus,
    /// while they show): when it takes one (returns true), the game doesn't see it, but its cursor still follows a taken
    /// mouse event (<see cref="GetCursorPos"/>). None: the game sees them all. Window events (closing, resizing, focus)
    /// always reach the game too. <paramref name="shows"/> says what the layer shows (asked as each pump starts: a
    /// screen swapped for another within a frame moves no mouse; none: nothing). While its screens fill the window, the
    /// mouse may go anywhere in it, full screen too (<see cref="MouseArea"/>); while it shows any, a cursor the game
    /// draws is drawn over them (<see cref="SetDrawnCursor"/>).
    /// </summary>
    void SetEventLayer(std::function<bool(const SDL_Event&)> layer, std::function<MCLayerShows()> shows = {});

    /// <summary>
    /// Port-only: whether the layer over the game shows a screen now (<see cref="SetEventLayer"/>, asked now, not as the
    /// pump last asked): its screens are drawn over the frame, and a cursor the game drew into it would be under them
    /// (<see cref="SetDrawnCursor"/>).
    /// </summary>
    bool LayerShowsScreen();

    /// <summary>Port-only: whether the mouse is over the window's letterbox bars, not the picture, as its last event put it.</summary>
    bool OverBars();

    /// <summary>
    /// Port-only: the window pixel the mouse is over, as its last event (or the game's warp) put it, the window pixel it
    /// falls in as the menus take it; before any, where the game's cursor is. None without a display.
    /// </summary>
    std::optional<SDL_Point> MousePixel();

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

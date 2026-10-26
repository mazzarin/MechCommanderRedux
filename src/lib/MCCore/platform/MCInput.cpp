#include "stdafx.h"
#include "platform/MCInput.h"
#include "platform/MCDisplay.h"

namespace
{
    /// <summary>A message waiting in the posted queue.</summary>
    struct MCPostedMessage
    {
        uint32_t Message = 0;
        uint32_t WParam = 0;
        int32_t LParam = 0;
    };

    /// <summary>Everything the input layer keeps.</summary>
    struct MCInputState
    {
        /// <summary>The display attached (see <see cref="MCInput::Attach"/>).</summary>
        std::weak_ptr<MCDisplay> Display;
        MCWindowProc Proc;

        std::mutex QueueLock;
        std::deque<MCPostedMessage> Posted;
        std::atomic<bool> Quit = false;
        std::atomic<int> QuitCode = 0;

        /// <summary>Per VK: down now, pressed since the last GetAsyncKeyState, toggled by presses.</summary>
        std::array<std::atomic<bool>, 256> Down{};
        std::array<std::atomic<bool>, 256> Pressed{};
        std::array<std::atomic<bool>, 256> Toggled{};

        std::atomic<int32_t> CursorX = 0;
        std::atomic<int32_t> CursorY = 0;
        MCPoint MessagePos;

        int CursorCounter = 0;
        /// <summary>The game's cursor as a system cursor, or null (SetGameCursor); held while it is set.</summary>
        std::shared_ptr<SDL_Cursor> GameCursor;
        /// <summary>The game's ClipCursor rectangle (logical screen), if it set one.</summary>
        std::optional<MCClipRect> ClipRect;
        /// <summary>The window area keeping the cursor off the screen's corners (GuardCorners), if any.</summary>
        std::optional<SDL_Rect> CornerGuard;
        /// <summary>The mouse is over the window's letterbox bars, not the picture.</summary>
        bool OutsidePicture = false;
        uint32_t DoubleClickTime = 500;
        bool Active = false;
        /// <summary>Whether a text field has the keyboard (<see cref="MCInput::SetTextInput"/>).</summary>
        bool TextInput = false;

        /// <summary>The last button press, for double clicks.</summary>
        int LastClickButton = -1;
        uint64_t LastClickTime = 0;
        MCPoint LastClickPos;
    };

    MCInputState& state()
    {
        static MCInputState instance;
        return instance;
    }

    /// <summary>The first printable character: WM_CHAR below it is a control character.</summary>
    constexpr int FirstPrintable = 0x20;

    /// <summary>Whether SDL's text input runs in the attached window (keys type their text through it).</summary>
    bool textInputActive()
    {
        const std::shared_ptr<MCDisplay> display = state().Display.lock();
        return display != nullptr && SDL_TextInputActive(&display->Window());
    }

    /// <summary>
    /// Starts or stops SDL's text input in the window, as <see cref="MCInput::SetTextInput"/> asks, once no key SDL
    /// read under the old setting waits in its queue.
    /// </summary>
    void applyTextInput()
    {
        MCInputState& input = state();
        const std::shared_ptr<MCDisplay> display = input.Display.lock();

        if (display == nullptr || SDL_HasEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_TEXT_INPUT))
        {
            return;
        }

        SDL_Window* window = &display->Window();

        if (SDL_TextInputActive(window) == input.TextInput)
        {
            return;
        }

        if (!(input.TextInput ? SDL_StartTextInput(window) : SDL_StopTextInput(window)))
        {
            SDL_Log("MCInput: can't %s text input in window %u: %s", input.TextInput ? "start" : "stop",
                    SDL_GetWindowID(window), SDL_GetError());
        }
    }

    /// <summary>Sends a message to the window procedure.</summary>
    int32_t send(uint32_t message, uint32_t wParam, int32_t lParam)
    {
        MCInputState& input = state();

        if (input.Proc)
        {
            return input.Proc(message, wParam, lParam);
        }

        return 0;
    }

    /// <summary>
    /// WM_CLOSE as DefWindowProc handled it: unless the window procedure cancels it, the window is destroyed and the
    /// loop told to end.
    /// </summary>
    void close()
    {
        if (send(WM_CLOSE, 0, 0) == 0)
        {
            send(WM_DESTROY, 0, 0);
            MCInput::PostQuitMessage(0);
        }
    }

    void setKey(int vk, bool down)
    {
        if (vk <= 0 || vk > 255)
        {
            return;
        }

        MCInputState& input = state();

        if (down && !input.Down[vk])
        {
            input.Pressed[vk] = true;
            input.Toggled[vk] = !input.Toggled[vk];
        }

        input.Down[vk] = down;
    }

    /// <summary>The shown part of the logical screen (the display's view, or 640x480 without one).</summary>
    MCClipRect screenArea()
    {
        const std::shared_ptr<MCDisplay> display = state().Display.lock();

        if (display == nullptr)
        {
            return MCClipRect{0, 0, 640, 480};
        }

        const SDL_Rect view = display->View();
        return MCClipRect{view.x, view.y, view.x + view.w, view.y + view.h};
    }

    /// <summary>A window point on the logical screen, clamped to it.</summary>
    MCPoint toLogical(float windowX, float windowY)
    {
        MCPoint point;
        const std::shared_ptr<MCDisplay> display = state().Display.lock();
        float x = windowX;
        float y = windowY;

        if (display != nullptr)
        {
            display->WindowToLogical(windowX, windowY, x, y);
        }

        const MCClipRect area = screenArea();
        point.x = std::clamp(static_cast<int32_t>(std::floor(x)), area.left, area.right - 1);
        point.y = std::clamp(static_cast<int32_t>(std::floor(y)), area.top, area.bottom - 1);

        if (const std::optional<SDL_Rect>& guard = state().CornerGuard; guard.has_value())
        {
            point.x = MCInput::GuardedX(windowX, *guard, point.x, area.left, area.right);
        }

        return point;
    }

    /// <summary>The MK_ flags of the buttons and modifiers held now.</summary>
    uint32_t mouseKeyFlags()
    {
        MCInputState& input = state();
        uint32_t flags = 0;

        if (input.Down[VK_LBUTTON])
        {
            flags |= MK_LBUTTON;
        }

        if (input.Down[VK_RBUTTON])
        {
            flags |= MK_RBUTTON;
        }

        if (input.Down[VK_MBUTTON])
        {
            flags |= MK_MBUTTON;
        }

        if (input.Down[VK_SHIFT])
        {
            flags |= MK_SHIFT;
        }

        if (input.Down[VK_CONTROL])
        {
            flags |= MK_CONTROL;
        }

        return flags;
    }

    /// <summary>Moves the cursor to a logical point and makes it the message position.</summary>
    void setCursor(MCPoint point)
    {
        MCInputState& input = state();
        input.CursorX = point.x;
        input.CursorY = point.y;
        input.MessagePos = point;
    }

    /// <summary>
    /// Shows the system arrow while the game's counter says so; else the game's cursor if it gave one; else the
    /// arrow while the mouse is over the letterbox bars (where the cursor drawn into the frame can't follow it).
    /// </summary>
    void updateCursorVisibility()
    {
        MCInputState& input = state();
        SDL_Cursor* cursor = nullptr;

        if (input.CursorCounter >= 0)
        {
            cursor = SDL_GetDefaultCursor();
        }
        else if (input.GameCursor != nullptr)
        {
            cursor = input.GameCursor.get();
        }
        else if (input.OutsidePicture)
        {
            cursor = SDL_GetDefaultCursor();
        }

        if (cursor == nullptr)
        {
            SDL_HideCursor();
            return;
        }

        if (SDL_GetCursor() != cursor)
        {
            SDL_SetCursor(cursor);
        }

        if (!SDL_CursorVisible())
        {
            SDL_ShowCursor();
        }
    }

    /// <summary>
    /// Sets where the system cursor may go: the game's ClipCursor rectangle if it set one; else, full screen and
    /// focused, the picture. The original's full screen switched the monitor to the game's resolution, so the mouse
    /// never left the picture; letterboxed, the hidden cursor would wander into the bars while the game's cursor
    /// stuck at the edge. Kept to the picture, the cursor stays off its corners too (MCInput::GuardCorners).
    /// </summary>
    void applyMouseRect()
    {
        MCInputState& input = state();
        const std::shared_ptr<MCDisplay> display = input.Display.lock();
        input.CornerGuard.reset();

        if (display == nullptr)
        {
            return;
        }

        SDL_Window* window = &display->Window();
        std::optional<MCClipRect> logical = input.ClipRect;
        const bool fullscreenPicture = !logical && input.Active && display->IsFullscreen();

        if (fullscreenPicture)
        {
            logical = screenArea();
        }

        if (!logical)
        {
            SDL_SetWindowMouseRect(window, nullptr);
            return;
        }

        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        display->LogicalToWindow(static_cast<float>(logical->left), static_cast<float>(logical->top), left, top);
        display->LogicalToWindow(static_cast<float>(logical->right), static_cast<float>(logical->bottom), right,
                                 bottom);
        SDL_Rect area;
        area.x = static_cast<int>(std::ceil(left));
        area.y = static_cast<int>(std::ceil(top));
        area.w = std::max(1, static_cast<int>(std::floor(right)) - area.x);
        area.h = std::max(1, static_cast<int>(std::floor(bottom)) - area.y);

        if (fullscreenPicture)
        {
            area = MCInput::GuardCorners(area);
            input.CornerGuard = area;
        }

        SDL_SetWindowMouseRect(window, &area);
    }

    void activate(bool active)
    {
        MCInputState& input = state();

        if (input.Active == active)
        {
            return;
        }

        input.Active = active;
        applyMouseRect();

        if (!active)
        {
            // Keys released while the window had no focus never send their key-up.
            for (int vk = 0; vk < 256; vk++)
            {
                input.Down[vk] = false;
            }
        }

        send(WM_ACTIVATEAPP, active ? 1u : 0u, 0);
    }

    /// <summary>The VK of a mouse button, and its down, up and double-click messages.</summary>
    bool buttonMessages(uint8_t button, int& vk, uint32_t& downMessage, uint32_t& upMessage, uint32_t& doubleMessage)
    {
        switch (button)
        {
            case SDL_BUTTON_LEFT:
            {
                vk = VK_LBUTTON;
                downMessage = WM_LBUTTONDOWN;
                upMessage = WM_LBUTTONUP;
                doubleMessage = WM_LBUTTONDBLCLK;
                return true;
            }
            case SDL_BUTTON_RIGHT:
            {
                vk = VK_RBUTTON;
                downMessage = WM_RBUTTONDOWN;
                upMessage = WM_RBUTTONUP;
                doubleMessage = WM_RBUTTONDBLCLK;
                return true;
            }
            case SDL_BUTTON_MIDDLE:
            {
                vk = VK_MBUTTON;
                downMessage = WM_MBUTTONDOWN;
                upMessage = WM_MBUTTONUP;
                doubleMessage = WM_MBUTTONDBLCLK;
                return true;
            }
            default:
                return false;
        }
    }

    /// <summary>The generic VK and the sided one of a modifier key, or 0.</summary>
    int sidedModifier(SDL_Scancode scancode)
    {
        switch (scancode)
        {
            case SDL_SCANCODE_LSHIFT:
                return VK_LSHIFT;
            case SDL_SCANCODE_RSHIFT:
                return VK_RSHIFT;
            case SDL_SCANCODE_LCTRL:
                return VK_LCONTROL;
            case SDL_SCANCODE_RCTRL:
                return VK_RCONTROL;
            case SDL_SCANCODE_LALT:
                return VK_LMENU;
            case SDL_SCANCODE_RALT:
                return VK_RMENU;
            default:
                return 0;
        }
    }

    /// <summary>The control character Windows' TranslateMessage makes of a key, or -1.</summary>
    int controlCharacter(int vk, bool ctrl, bool alt)
    {
        switch (vk)
        {
            case VK_BACK:
                return 8;
            case VK_TAB:
                return 9;
            case VK_RETURN:
                return 13;
            case VK_ESCAPE:
                return 27;
            default:
                break;
        }

        if (ctrl && !alt && vk >= 'A' && vk <= 'Z')
        {
            return vk - 'A' + 1;
        }

        return -1;
    }

    /// <summary>
    /// The Windows-1252 character a key (<paramref name="vk"/>) makes with the modifiers held, in the keyboard layout,
    /// or -1 (a key that makes none, or one the code page lacks): TranslateMessage's WM_CHAR, for when SDL's text
    /// input is off.
    /// </summary>
    int typedCharacter(const SDL_KeyboardEvent& key, int vk)
    {
        // The keypad's keys (digits with NumLock on).
        if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        {
            return '0' + (vk - VK_NUMPAD0);
        }

        switch (vk)
        {
            case VK_MULTIPLY:
                return '*';
            case VK_ADD:
                return '+';
            case VK_SUBTRACT:
                return '-';
            case VK_DECIMAL:
                return '.';
            case VK_DIVIDE:
                return '/';
            default:
                break;
        }

        const SDL_Keycode typed = SDL_GetKeyFromScancode(key.scancode, key.mod, false);

        // Keys without a character have keycodes built from their scancode (or SDL's extended ones).
        if ((typed & (SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK)) != 0 ||
            typed < static_cast<SDL_Keycode>(FirstPrintable) || typed == SDLK_DELETE)
        {
            return -1;
        }

        return MCInput::CodePointToWindows1252(typed);
    }

    void handleKey(const SDL_KeyboardEvent& key)
    {
        MCInputState& input = state();
        bool numLock = (key.mod & SDL_KMOD_NUM) != 0;
        int vk = MCInput::VirtualKeyFromScancode(key.scancode, key.key, numLock);

        if (vk == 0)
        {
            return;
        }

        bool down = key.down;
        bool previousDown = key.repeat || !down;
        bool altDown = (key.mod & SDL_KMOD_ALT) != 0;
        bool ctrlDown = (key.mod & SDL_KMOD_CTRL) != 0;
        setKey(vk, down);
        int sided = sidedModifier(key.scancode);

        if (sided != 0)
        {
            setKey(sided, down);
        }

        bool system = (altDown && !ctrlDown) || vk == VK_F10 || vk == VK_MENU;
        uint32_t message = down ? (system ? WM_SYSKEYDOWN : WM_KEYDOWN) : (system ? WM_SYSKEYUP : WM_KEYUP);
        int32_t lParam = MCInput::MakeKeyLParam(MCInput::Win32ScanCode(key.scancode), altDown, previousDown, !down);
        MCPoint cursor{input.CursorX, input.CursorY};
        input.MessagePos = cursor;
        send(message, static_cast<uint32_t>(vk), lParam);

        if (down && !system)
        {
            int character = controlCharacter(vk, ctrlDown, altDown);

            // No text field has the keyboard: the key's own character (with it, SDL_EVENT_TEXT_INPUT brings the text).
            // Command (the Windows key) chords are the system's shortcuts, and type nothing.
            if (character < 0 && !ctrlDown && (key.mod & SDL_KMOD_GUI) == 0 && !textInputActive())
            {
                character = typedCharacter(key, vk);
            }

            if (character >= 0)
            {
                send(WM_CHAR, static_cast<uint32_t>(character), lParam);
            }
        }
    }

    /// <summary>The code points of UTF-8 text.</summary>
    std::vector<uint32_t> decodeUtf8(std::string_view text)
    {
        std::vector<uint32_t> codePoints;
        size_t at = 0;
        const auto byte = [&](size_t index) { return static_cast<uint8_t>(text[index]); };

        while (at < text.size())
        {
            uint32_t c = byte(at++);
            int extra = 0;

            if (c >= 0xf0)
            {
                c &= 0x07;
                extra = 3;
            }
            else if (c >= 0xe0)
            {
                c &= 0x0f;
                extra = 2;
            }
            else if (c >= 0xc0)
            {
                c &= 0x1f;
                extra = 1;
            }

            for (; extra > 0 && at < text.size() && (byte(at) & 0xc0) == 0x80; extra--)
            {
                c = (c << 6) | (byte(at++) & 0x3f);
            }

            codePoints.push_back(c);
        }

        return codePoints;
    }
}

namespace MCInput
{
    void Attach(std::weak_ptr<MCDisplay> attached)
    {
        MCInputState& input = state();
        input.Display = std::move(attached);
        const std::shared_ptr<MCDisplay> display = input.Display.lock();

        if (display == nullptr)
        {
            input.CornerGuard.reset();
            return;
        }

        applyTextInput();
        SDL_Keymod mod = SDL_GetModState();
        input.Toggled[VK_CAPITAL] = (mod & SDL_KMOD_CAPS) != 0;
        input.Toggled[VK_NUMLOCK] = (mod & SDL_KMOD_NUM) != 0;
        input.Toggled[VK_SCROLL] = (mod & SDL_KMOD_SCROLL) != 0;

        if (SDL_GetKeyboardFocus() == &display->Window())
        {
            activate(true);
        }

        applyMouseRect();
    }

    std::shared_ptr<MCDisplay> Display()
    {
        return state().Display.lock();
    }

    void SetWindowProc(MCWindowProc proc)
    {
        state().Proc = std::move(proc);
    }

    void SetTextInput(bool on)
    {
        state().TextInput = on;
    }

    bool PumpMessages()
    {
        MCInputState& input = state();
        std::deque<MCPostedMessage> posted;
        {
            std::scoped_lock lock(input.QueueLock);
            posted.swap(input.Posted);
        }

        for (const MCPostedMessage& message : posted)
        {
            if (message.Message == WM_CLOSE)
            {
                close();
            }
            else
            {
                send(message.Message, message.WParam, message.LParam);
            }
        }

        // Between the batches of keys SDL reads (see SetTextInput).
        applyTextInput();
        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
            HandleEvent(event);
        }

        applyTextInput();
        return !input.Quit;
    }

    bool WaitMessage(int timeoutMs)
    {
        applyTextInput();
        SDL_Event event;

        if (SDL_WaitEventTimeout(&event, timeoutMs))
        {
            HandleEvent(event);
        }

        return PumpMessages();
    }

    void PostQuitMessage(int exitCode)
    {
        MCInputState& input = state();
        input.QuitCode = exitCode;
        input.Quit = true;
    }

    bool QuitRequested()
    {
        return state().Quit;
    }

    int QuitCode()
    {
        return state().QuitCode;
    }

    void PostMessage(uint32_t message, uint32_t wParam, int32_t lParam)
    {
        if (message == WM_QUIT)
        {
            PostQuitMessage(static_cast<int>(wParam));
            return;
        }

        MCInputState& input = state();
        std::scoped_lock lock(input.QueueLock);
        input.Posted.push_back({message, wParam, lParam});
    }

    int16_t GetAsyncKeyState(int vk)
    {
        if (vk <= 0 || vk > 255)
        {
            return 0;
        }

        MCInputState& input = state();
        int16_t result = input.Down[vk] ? static_cast<int16_t>(0x8000) : 0;

        if (input.Pressed[vk].exchange(false))
        {
            result |= 1;
        }

        return result;
    }

    int16_t GetKeyState(int vk)
    {
        if (vk <= 0 || vk > 255)
        {
            return 0;
        }

        MCInputState& input = state();
        int16_t result = input.Down[vk] ? static_cast<int16_t>(0x8000) : 0;

        if (input.Toggled[vk])
        {
            result |= 1;
        }

        return result;
    }

    bool IsKeyDown(int vk)
    {
        return vk > 0 && vk <= 255 && state().Down[vk];
    }

    MCPoint GetCursorPos()
    {
        MCInputState& input = state();

        if (const std::shared_ptr<MCDisplay> display = input.Display.lock();
            display != nullptr && SDL_GetMouseFocus() == &display->Window())
        {
            float x = 0.0f;
            float y = 0.0f;
            SDL_GetMouseState(&x, &y);
            MCPoint point = toLogical(x, y);
            input.CursorX = point.x;
            input.CursorY = point.y;
        }

        return MCPoint{input.CursorX, input.CursorY};
    }

    MCPoint GetMessagePos()
    {
        return state().MessagePos;
    }

    void SetCursorPos(int32_t x, int32_t y)
    {
        MCInputState& input = state();
        const MCClipRect area = screenArea();
        MCPoint point{std::clamp(x, area.left, area.right - 1), std::clamp(y, area.top, area.bottom - 1)};
        input.CursorX = point.x;
        input.CursorY = point.y;

        // A hidden window (the headless tests) has no mouse of its own: warping would move the user's.
        if (const std::shared_ptr<MCDisplay> display = input.Display.lock();
            display != nullptr && (SDL_GetWindowFlags(&display->Window()) & SDL_WINDOW_HIDDEN) == 0)
        {
            float windowX = 0.0f;
            float windowY = 0.0f;
            display->LogicalToWindow(static_cast<float>(point.x) + 0.5f, static_cast<float>(point.y) + 0.5f, windowX,
                                     windowY);
            SDL_WarpMouseInWindow(&display->Window(), windowX, windowY);
        }
    }

    int ShowCursor(bool show)
    {
        MCInputState& input = state();
        input.CursorCounter += show ? 1 : -1;
        updateCursorVisibility();
        return input.CursorCounter;
    }

    void SetGameCursor(std::shared_ptr<SDL_Cursor> cursor)
    {
        MCInputState& input = state();

        if (input.GameCursor == cursor)
        {
            return;
        }

        input.GameCursor = std::move(cursor);
        updateCursorVisibility();
    }

    void ClipCursor(std::optional<MCClipRect> rect)
    {
        state().ClipRect = rect;
        applyMouseRect();
    }

    SDL_Rect GuardCorners(SDL_Rect picture)
    {
        if (picture.w > 2 * CornerGuardPoints)
        {
            picture.x += CornerGuardPoints;
            picture.w -= 2 * CornerGuardPoints;
        }

        return picture;
    }

    int32_t GuardedX(float windowX, const SDL_Rect& guarded, int32_t x, int32_t left, int32_t right)
    {
        // The cursor reaches the area's first point and its last (SDL's confinement holds x + w - 1).
        if (windowX < static_cast<float>(guarded.x + 1))
        {
            return left;
        }

        if (windowX >= static_cast<float>(guarded.x + guarded.w - 1))
        {
            return right - 1;
        }

        return x;
    }

    void RefreshMouseArea()
    {
        applyMouseRect();
    }

    void SetCapture()
    {
        SDL_CaptureMouse(true);
    }

    void ReleaseCapture()
    {
        SDL_CaptureMouse(false);
    }

    uint32_t GetDoubleClickTime()
    {
        return state().DoubleClickTime;
    }

    void SetDoubleClickTime(uint32_t milliseconds)
    {
        state().DoubleClickTime = milliseconds;
    }

    void HandleEvent(const SDL_Event& event)
    {
        MCInputState& input = state();

        switch (event.type)
        {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                close();
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                activate(true);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                activate(false);
                break;
            case SDL_EVENT_WINDOW_EXPOSED:
                send(WM_PAINT, 0, 0);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                // The picture moved inside the window.
                applyMouseRect();
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            {
                input.OutsidePicture = false;
                updateCursorVisibility();
                break;
            }
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                handleKey(event.key);
                break;
            case SDL_EVENT_TEXT_INPUT:
            {
                for (uint32_t codePoint : decodeUtf8(event.text.text))
                {
                    int character = CodePointToWindows1252(codePoint);

                    if (character >= FirstPrintable)
                    {
                        send(WM_CHAR, static_cast<uint32_t>(character), 1);
                    }
                }
                break;
            }
            case SDL_EVENT_MOUSE_MOTION:
            {
                if (const std::shared_ptr<MCDisplay> display = input.Display.lock())
                {
                    float x;
                    float y;
                    const bool outside = !display->WindowToLogical(event.motion.x, event.motion.y, x, y);

                    if (outside != input.OutsidePicture)
                    {
                        input.OutsidePicture = outside;
                        updateCursorVisibility();
                    }
                }

                MCPoint point = toLogical(event.motion.x, event.motion.y);
                setCursor(point);
                send(WM_MOUSEMOVE, mouseKeyFlags(), MakePointLParam(point.x, point.y));
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                int vk = 0;
                uint32_t downMessage = 0;
                uint32_t upMessage = 0;
                uint32_t doubleMessage = 0;

                if (!buttonMessages(event.button.button, vk, downMessage, upMessage, doubleMessage))
                {
                    break;
                }

                MCPoint point = toLogical(event.button.x, event.button.y);
                setCursor(point);
                setKey(vk, event.button.down);
                uint32_t message = upMessage;

                if (event.button.down)
                {
                    uint64_t now = SDL_GetTicks();
                    bool isDouble = input.LastClickButton == event.button.button &&
                                    now - input.LastClickTime <= input.DoubleClickTime &&
                                    std::abs(point.x - input.LastClickPos.x) <= 2 &&
                                    std::abs(point.y - input.LastClickPos.y) <= 2;
                    message = isDouble ? doubleMessage : downMessage;
                    input.LastClickButton = isDouble ? -1 : event.button.button;
                    input.LastClickTime = now;
                    input.LastClickPos = point;
                }

                send(message, mouseKeyFlags(), MakePointLParam(point.x, point.y));
                break;
            }

            case SDL_EVENT_MOUSE_WHEEL:
            {
                int32_t delta = static_cast<int32_t>(event.wheel.y * 120.0f);

                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                {
                    delta = -delta;
                }

                if (delta == 0)
                {
                    break;
                }

                MCPoint point = toLogical(event.wheel.mouse_x, event.wheel.mouse_y);
                uint32_t wParam = (static_cast<uint32_t>(static_cast<uint16_t>(delta)) << 16) | mouseKeyFlags();
                send(WM_MOUSEWHEEL, wParam, MakePointLParam(point.x, point.y));
                break;
            }

            default:
                break;
        }
    }

    void Reset()
    {
        MCInputState& input = state();

        for (int vk = 0; vk < 256; vk++)
        {
            input.Down[vk] = false;
            input.Pressed[vk] = false;
            input.Toggled[vk] = false;
        }

        {
            std::scoped_lock lock(input.QueueLock);
            input.Posted.clear();
        }

        input.Quit = false;
        input.QuitCode = 0;
        input.CursorX = 0;
        input.CursorY = 0;
        input.MessagePos = {};
        input.CursorCounter = 0;
        input.DoubleClickTime = 500;
        input.Active = false;
        input.TextInput = false;
        input.CornerGuard.reset();
        input.LastClickButton = -1;
        input.LastClickTime = 0;
        input.LastClickPos = {};
    }

    int VirtualKeyFromScancode(SDL_Scancode scancode, SDL_Keycode key, bool numLock)
    {
        if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
        {
            if (key >= SDLK_A && key <= SDLK_Z)
            {
                return 'A' + static_cast<int>(key - SDLK_A);
            }

            return 'A' + (scancode - SDL_SCANCODE_A);
        }

        if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
        {
            return '1' + (scancode - SDL_SCANCODE_1);
        }

        if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12)
        {
            return VK_F1 + (scancode - SDL_SCANCODE_F1);
        }

        if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9)
        {
            if (numLock)
            {
                return VK_NUMPAD1 + (scancode - SDL_SCANCODE_KP_1);
            }

            static constexpr int navigation[9] = {VK_END,   VK_DOWN, VK_NEXT, VK_LEFT, VK_CLEAR,
                                                  VK_RIGHT, VK_HOME, VK_UP,   VK_PRIOR};
            return navigation[scancode - SDL_SCANCODE_KP_1];
        }

        switch (scancode)
        {
            case SDL_SCANCODE_0:
                return '0';
            case SDL_SCANCODE_KP_0:
                return numLock ? VK_NUMPAD0 : VK_INSERT;
            case SDL_SCANCODE_KP_PERIOD:
                return numLock ? VK_DECIMAL : VK_DELETE;
            case SDL_SCANCODE_KP_DIVIDE:
                return VK_DIVIDE;
            case SDL_SCANCODE_KP_MULTIPLY:
                return VK_MULTIPLY;
            case SDL_SCANCODE_KP_MINUS:
                return VK_SUBTRACT;
            case SDL_SCANCODE_KP_PLUS:
                return VK_ADD;
            case SDL_SCANCODE_KP_ENTER:
            case SDL_SCANCODE_RETURN:
                return VK_RETURN;
            case SDL_SCANCODE_ESCAPE:
                return VK_ESCAPE;
            case SDL_SCANCODE_BACKSPACE:
                return VK_BACK;
            case SDL_SCANCODE_TAB:
                return VK_TAB;
            case SDL_SCANCODE_SPACE:
                return VK_SPACE;
            case SDL_SCANCODE_MINUS:
                return VK_OEM_MINUS;
            case SDL_SCANCODE_EQUALS:
                return VK_OEM_PLUS;
            case SDL_SCANCODE_LEFTBRACKET:
                return VK_OEM_4;
            case SDL_SCANCODE_RIGHTBRACKET:
                return VK_OEM_6;
            case SDL_SCANCODE_BACKSLASH:
                return VK_OEM_5;
            case SDL_SCANCODE_SEMICOLON:
                return VK_OEM_1;
            case SDL_SCANCODE_APOSTROPHE:
                return VK_OEM_7;
            case SDL_SCANCODE_GRAVE:
                return VK_OEM_3;
            case SDL_SCANCODE_COMMA:
                return VK_OEM_COMMA;
            case SDL_SCANCODE_PERIOD:
                return VK_OEM_PERIOD;
            case SDL_SCANCODE_SLASH:
                return VK_OEM_2;
            case SDL_SCANCODE_CAPSLOCK:
                return VK_CAPITAL;
            case SDL_SCANCODE_PRINTSCREEN:
                return VK_SNAPSHOT;
            case SDL_SCANCODE_SCROLLLOCK:
                return VK_SCROLL;
            case SDL_SCANCODE_PAUSE:
                return VK_PAUSE;
            case SDL_SCANCODE_INSERT:
                return VK_INSERT;
            case SDL_SCANCODE_HOME:
                return VK_HOME;
            case SDL_SCANCODE_PAGEUP:
                return VK_PRIOR;
            case SDL_SCANCODE_DELETE:
                return VK_DELETE;
            case SDL_SCANCODE_END:
                return VK_END;
            case SDL_SCANCODE_PAGEDOWN:
                return VK_NEXT;
            case SDL_SCANCODE_RIGHT:
                return VK_RIGHT;
            case SDL_SCANCODE_LEFT:
                return VK_LEFT;
            case SDL_SCANCODE_DOWN:
                return VK_DOWN;
            case SDL_SCANCODE_UP:
                return VK_UP;
            case SDL_SCANCODE_NUMLOCKCLEAR:
                return VK_NUMLOCK;
            case SDL_SCANCODE_LSHIFT:
            case SDL_SCANCODE_RSHIFT:
                return VK_SHIFT;
            case SDL_SCANCODE_LCTRL:
            case SDL_SCANCODE_RCTRL:
                return VK_CONTROL;
            case SDL_SCANCODE_LALT:
            case SDL_SCANCODE_RALT:
                return VK_MENU;
            case SDL_SCANCODE_LGUI:
                return VK_LWIN;
            case SDL_SCANCODE_RGUI:
                return VK_RWIN;
            case SDL_SCANCODE_APPLICATION:
                return VK_APPS;
            default:
                return 0;
        }
    }

    MCScanCode Win32ScanCode(SDL_Scancode scancode)
    {
        // Set-1 codes of the letter rows, indexed by letter.
        static constexpr uint8_t letters[26] = {0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                                0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                                0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};

        if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
        {
            return {letters[scancode - SDL_SCANCODE_A], false};
        }

        if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0)
        {
            return {static_cast<uint8_t>(0x02 + (scancode - SDL_SCANCODE_1)), false};
        }

        if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F10)
        {
            return {static_cast<uint8_t>(0x3b + (scancode - SDL_SCANCODE_F1)), false};
        }

        switch (scancode)
        {
            case SDL_SCANCODE_ESCAPE:
                return {0x01, false};
            case SDL_SCANCODE_MINUS:
                return {0x0c, false};
            case SDL_SCANCODE_EQUALS:
                return {0x0d, false};
            case SDL_SCANCODE_BACKSPACE:
                return {0x0e, false};
            case SDL_SCANCODE_TAB:
                return {0x0f, false};
            case SDL_SCANCODE_LEFTBRACKET:
                return {0x1a, false};
            case SDL_SCANCODE_RIGHTBRACKET:
                return {0x1b, false};
            case SDL_SCANCODE_RETURN:
                return {0x1c, false};
            case SDL_SCANCODE_LCTRL:
                return {0x1d, false};
            case SDL_SCANCODE_SEMICOLON:
                return {0x27, false};
            case SDL_SCANCODE_APOSTROPHE:
                return {0x28, false};
            case SDL_SCANCODE_GRAVE:
                return {0x29, false};
            case SDL_SCANCODE_LSHIFT:
                return {0x2a, false};
            case SDL_SCANCODE_BACKSLASH:
                return {0x2b, false};
            case SDL_SCANCODE_COMMA:
                return {0x33, false};
            case SDL_SCANCODE_PERIOD:
                return {0x34, false};
            case SDL_SCANCODE_SLASH:
                return {0x35, false};
            case SDL_SCANCODE_RSHIFT:
                return {0x36, false};
            case SDL_SCANCODE_KP_MULTIPLY:
                return {0x37, false};
            case SDL_SCANCODE_LALT:
                return {0x38, false};
            case SDL_SCANCODE_SPACE:
                return {0x39, false};
            case SDL_SCANCODE_CAPSLOCK:
                return {0x3a, false};
            case SDL_SCANCODE_NUMLOCKCLEAR:
                return {0x45, false};
            case SDL_SCANCODE_PAUSE:
                return {0x45, false};
            case SDL_SCANCODE_SCROLLLOCK:
                return {0x46, false};
            case SDL_SCANCODE_KP_7:
                return {0x47, false};
            case SDL_SCANCODE_KP_8:
                return {0x48, false};
            case SDL_SCANCODE_KP_9:
                return {0x49, false};
            case SDL_SCANCODE_KP_MINUS:
                return {0x4a, false};
            case SDL_SCANCODE_KP_4:
                return {0x4b, false};
            case SDL_SCANCODE_KP_5:
                return {0x4c, false};
            case SDL_SCANCODE_KP_6:
                return {0x4d, false};
            case SDL_SCANCODE_KP_PLUS:
                return {0x4e, false};
            case SDL_SCANCODE_KP_1:
                return {0x4f, false};
            case SDL_SCANCODE_KP_2:
                return {0x50, false};
            case SDL_SCANCODE_KP_3:
                return {0x51, false};
            case SDL_SCANCODE_KP_0:
                return {0x52, false};
            case SDL_SCANCODE_KP_PERIOD:
                return {0x53, false};
            case SDL_SCANCODE_F11:
                return {0x57, false};
            case SDL_SCANCODE_F12:
                return {0x58, false};
            case SDL_SCANCODE_KP_ENTER:
                return {0x1c, true};
            case SDL_SCANCODE_RCTRL:
                return {0x1d, true};
            case SDL_SCANCODE_KP_DIVIDE:
                return {0x35, true};
            case SDL_SCANCODE_PRINTSCREEN:
                return {0x37, true};
            case SDL_SCANCODE_RALT:
                return {0x38, true};
            case SDL_SCANCODE_HOME:
                return {0x47, true};
            case SDL_SCANCODE_UP:
                return {0x48, true};
            case SDL_SCANCODE_PAGEUP:
                return {0x49, true};
            case SDL_SCANCODE_LEFT:
                return {0x4b, true};
            case SDL_SCANCODE_RIGHT:
                return {0x4d, true};
            case SDL_SCANCODE_END:
                return {0x4f, true};
            case SDL_SCANCODE_DOWN:
                return {0x50, true};
            case SDL_SCANCODE_PAGEDOWN:
                return {0x51, true};
            case SDL_SCANCODE_INSERT:
                return {0x52, true};
            case SDL_SCANCODE_DELETE:
                return {0x53, true};
            case SDL_SCANCODE_LGUI:
                return {0x5b, true};
            case SDL_SCANCODE_RGUI:
                return {0x5c, true};
            case SDL_SCANCODE_APPLICATION:
                return {0x5d, true};
            default:
                return {0, false};
        }
    }

    int32_t MakeKeyLParam(MCScanCode scan, bool altDown, bool previousDown, bool releasing, uint16_t repeatCount)
    {
        uint32_t value = repeatCount;
        value |= static_cast<uint32_t>(scan.Code) << 16;

        if (scan.Extended)
        {
            value |= 1u << 24;
        }

        if (altDown)
        {
            value |= 1u << 29;
        }

        if (previousDown)
        {
            value |= 1u << 30;
        }

        if (releasing)
        {
            value |= 1u << 31;
        }

        return static_cast<int32_t>(value);
    }

    int32_t MakePointLParam(int32_t x, int32_t y)
    {
        uint32_t value = static_cast<uint16_t>(static_cast<int16_t>(x)) |
                         (static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(y))) << 16);
        return static_cast<int32_t>(value);
    }

    int CodePointToWindows1252(uint32_t codePoint)
    {
        if (codePoint < 0x80 || (codePoint >= 0xa0 && codePoint <= 0xff))
        {
            return static_cast<int>(codePoint);
        }

        // The five positions of 0x80..0x9F Windows-1252 leaves undefined map to themselves (as Windows does).
        if (codePoint == 0x81 || codePoint == 0x8d || codePoint == 0x8f || codePoint == 0x90 || codePoint == 0x9d)
        {
            return static_cast<int>(codePoint);
        }

        static constexpr uint16_t high[32] = {0x20ac, 0,      0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
                                              0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017d, 0,
                                              0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                              0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,      0x017e, 0x0178};

        for (int i = 0; i < 32; i++)
        {
            if (high[i] != 0 && high[i] == codePoint)
            {
                return 0x80 + i;
            }
        }

        return -1;
    }
}

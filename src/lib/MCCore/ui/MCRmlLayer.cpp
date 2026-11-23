#include "stdafx.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlKit.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Input.h>

namespace
{
    /// <summary>SDL's keys the menus use, as RmlUi names them (the rest are the game's).</summary>
    constexpr std::array<std::pair<SDL_Keycode, Rml::Input::KeyIdentifier>, 21> Keys = {{
        {SDLK_UP, Rml::Input::KI_UP},
        {SDLK_DOWN, Rml::Input::KI_DOWN},
        {SDLK_LEFT, Rml::Input::KI_LEFT},
        {SDLK_RIGHT, Rml::Input::KI_RIGHT},
        {SDLK_RETURN, Rml::Input::KI_RETURN},
        {SDLK_KP_ENTER, Rml::Input::KI_NUMPADENTER},
        {SDLK_ESCAPE, Rml::Input::KI_ESCAPE},
        {SDLK_TAB, Rml::Input::KI_TAB},
        {SDLK_BACKSPACE, Rml::Input::KI_BACK},
        {SDLK_DELETE, Rml::Input::KI_DELETE},
        {SDLK_HOME, Rml::Input::KI_HOME},
        {SDLK_END, Rml::Input::KI_END},
        {SDLK_PAGEUP, Rml::Input::KI_PRIOR},
        {SDLK_PAGEDOWN, Rml::Input::KI_NEXT},
        {SDLK_SPACE, Rml::Input::KI_SPACE},
        {SDLK_Q, Rml::Input::KI_Q},
        {SDLK_E, Rml::Input::KI_E},
        {SDLK_A, Rml::Input::KI_A},
        {SDLK_C, Rml::Input::KI_C},
        {SDLK_V, Rml::Input::KI_V},
        {SDLK_X, Rml::Input::KI_X},
    }};

    Rml::Input::KeyIdentifier KeyOf(SDL_Keycode key)
    {
        const auto found = std::ranges::find(Keys, key, &std::pair<SDL_Keycode, Rml::Input::KeyIdentifier>::first);
        return found != Keys.end() ? found->second : Rml::Input::KI_UNKNOWN;
    }

    int ModifiersOf(SDL_Keymod modifiers)
    {
        int state = 0;
        state |= (modifiers & SDL_KMOD_CTRL) != 0 ? Rml::Input::KM_CTRL : 0;
        state |= (modifiers & SDL_KMOD_SHIFT) != 0 ? Rml::Input::KM_SHIFT : 0;
        state |= (modifiers & SDL_KMOD_ALT) != 0 ? Rml::Input::KM_ALT : 0;
        state |= (modifiers & SDL_KMOD_GUI) != 0 ? Rml::Input::KM_META : 0;
        return state;
    }

    /// <summary>RmlUi's number for SDL's mouse button <paramref name="button"/> (left, right, middle); -1 for another.</summary>
    int ButtonOf(uint8_t button)
    {
        switch (button)
        {
            case SDL_BUTTON_LEFT:
                return 0;
            case SDL_BUTTON_RIGHT:
                return 1;
            case SDL_BUTTON_MIDDLE:
                return 2;
            default:
                return -1;
        }
    }

    /// <summary>RmlUi's pseudo-class for a focus the keys moved (the ring shows on it).</summary>
    constexpr const char* FocusVisible = "focus-visible";
    /// <summary>No modifier key held (RmlUi's modifiers are flags).</summary>
    constexpr int NoModifiers = 0;

    /// <summary>The window pixels a window point is (SDL's mouse events are in points).</summary>
    float PixelsPerPoint(SDL_WindowID id)
    {
        SDL_Window* window = SDL_GetWindowFromID(id);
        const float density = window != nullptr ? SDL_GetWindowPixelDensity(window) : 0.0f;
        return density > 0.0f ? density : 1.0f;
    }

    int Pixel(float point, float density)
    {
        return static_cast<int>(std::floor(point * density));
    }
}

MCRmlLayer::MCRmlLayer(Key, Rml::Context& context, MCKind kind, MCUnit unit, MCRmlKit& kit)
    : _Context(context), _Kind(kind), _Unit(std::move(unit)), _Kit(kit)
{
}

MCRmlLayer::~MCRmlLayer()
{
    // Each screen destroyed while the layer and its context are still here, the newest first.
    while (!_Screens.empty())
    {
        MCGuiRef<MCRmlScreen> last = std::move(_Screens.back());
        _Screens.pop_back();
        last.reset();
    }
}

void MCRmlLayer::Remove(const MCRmlScreen& screen)
{
    const auto found = std::ranges::find_if(_Screens, [&screen](const MCGuiRef<MCRmlScreen>& kept)
                                            { return kept.Pin().get() == &screen; });

    if (found != _Screens.end())
    {
        // Taken off first, then destroyed (its Destroy calls here again and finds nothing).
        MCGuiRef<MCRmlScreen> taken = std::move(*found);
        _Screens.erase(found);
        taken.reset();
    }
}

float MCRmlLayer::Unit() const
{
    return _Unit(_Width, _Height);
}

void MCRmlLayer::SetWindow(int32_t width, int32_t height)
{
    width = std::max(width, 1);
    height = std::max(height, 1);
    const float unit = _Unit(width, height);

    if (width == _Width && height == _Height && unit == _Context.GetDensityIndependentPixelRatio())
    {
        return;
    }

    _Width = width;
    _Height = height;
    _Context.SetDimensions(Rml::Vector2i(width, height));
    _Context.SetDensityIndependentPixelRatio(unit);
}

MCOptionalRef<MCRmlScreen> MCRmlLayer::Top() const
{
    for (auto shown = _ShownOrder.rbegin(); shown != _ShownOrder.rend(); ++shown)
    {
        if (const std::shared_ptr<MCRmlScreen> screen = shown->Lock(); screen != nullptr && screen->Shown())
        {
            return MCOptionalRef<MCRmlScreen>(*screen);
        }
    }

    return std::nullopt;
}

void MCRmlLayer::Shown(MCRmlScreen& screen)
{
    Hidden(screen);
    _ShownOrder.emplace_back(screen.shared_from_this());
}

void MCRmlLayer::Hidden(MCRmlScreen& screen)
{
    std::erase_if(_ShownOrder, [&screen](const MCGuiHandle<MCRmlScreen>& shown) { return !shown || shown.Is(screen); });
}

bool MCRmlLayer::TakeKey(int key, int modifiers)
{
    const MCOptionalRef<MCRmlScreen> top = Top();

    if (!top.has_value())
    {
        return false;
    }

    // Pinned for the key's length: its Back can close it (a window's CANCEL), which mustn't free it while Back runs.
    const std::shared_ptr<MCRmlScreen> screen = top->get().shared_from_this();

    switch (key)
    {
        case Rml::Input::KI_ESCAPE:
        {
            // Back one screen, or the window's CANCEL (its Back).
            if (screen->Back)
            {
                screen->Back();
            }
            else
            {
                screen->Hide();
            }

            return true;
        }
        case Rml::Input::KI_Q:
        case Rml::Input::KI_E:
        {
            if (const MCOptionalRef<Rml::ElementDocument> document = screen->Document(); document.has_value())
            {
                return _Kit.StepTab(document->get(), key == Rml::Input::KI_Q ? -1 : 1);
            }

            return false;
        }
        default:
            return screen->OnKey(key, modifiers);
    }
}

bool MCRmlLayer::HandleEvent(const SDL_Event& event)
{
    if (!Showing())
    {
        return false;
    }

    const int modifiers = ModifiersOf(SDL_GetModState());

    switch (event.type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            const float density = PixelsPerPoint(event.motion.windowID);
            _Kit.MouseMoved(_Context);
            _Context.ProcessMouseMove(Pixel(event.motion.x, density), Pixel(event.motion.y, density), modifiers);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            const float density = PixelsPerPoint(event.button.windowID);
            _Context.ProcessMouseMove(Pixel(event.button.x, density), Pixel(event.button.y, density), modifiers);

            if (const int button = ButtonOf(event.button.button); button >= 0)
            {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                {
                    _Context.ProcessMouseButtonDown(button, modifiers);
                }
                else
                {
                    _Context.ProcessMouseButtonUp(button, modifiers);
                }
            }

            return true;
        }
        case SDL_EVENT_MOUSE_WHEEL:
        {
            // SDL's wheel is away from the player (up) positive, RmlUi's down; flipped, SDL's turned round.
            const float flip = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
            _Context.ProcessMouseWheel(Rml::Vector2f(event.wheel.x * flip, -event.wheel.y * flip), modifiers);
            return true;
        }
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        {
            _Context.ProcessMouseLeave();
            return false;
        }
        case SDL_EVENT_KEY_DOWN:
        {
            const Rml::Input::KeyIdentifier key = KeyOf(event.key.key);
            const int held = ModifiersOf(event.key.mod);

            if (key == Rml::Input::KI_UNKNOWN)
            {
                return true;
            }

            // A held Escape or Enter goes back or presses once, not once a repeat (a held arrow keeps moving).
            if (event.key.repeat &&
                (key == Rml::Input::KI_ESCAPE || key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER))
            {
                return true;
            }

            // A key shows where the focus is: its ring from the first key on (the mouse's first move takes it away).
            if (Rml::Element* focus = _Context.GetFocusElement();
                focus != nullptr && focus != focus->GetOwnerDocument())
            {
                focus->SetPseudoClass(FocusVisible, true);
            }

            const bool typing = Typing();

            // Enter in a text box: done, on to the next control as Tab goes (what was typed is its value already).
            if (typing && (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER))
            {
                _Context.ProcessKeyDown(Rml::Input::KI_TAB, NoModifiers);
                _Context.ProcessKeyUp(Rml::Input::KI_TAB, NoModifiers);
                return true;
            }

            // RmlUi's first (a text box's typing, the focus's moves, Enter's press, a control's Left and Right); what
            // it leaves, the layer's and the screen's: not a letter typed in a text box. Escape is always the layer's:
            // RmlUi does nothing with it, and its text box stops every key it doesn't move out of (SPEC-menus.md's
            // stage 3: Escape closes HOST GAME's window from its name).
            const bool left = _Context.ProcessKeyDown(key, held);

            if (key == Rml::Input::KI_ESCAPE || (left && !typing))
            {
                TakeKey(key, held);
            }

            return true;
        }
        case SDL_EVENT_KEY_UP:
        {
            if (const Rml::Input::KeyIdentifier key = KeyOf(event.key.key); key != Rml::Input::KI_UNKNOWN)
            {
                _Context.ProcessKeyUp(key, ModifiersOf(event.key.mod));
            }

            return true;
        }
        case SDL_EVENT_TEXT_INPUT:
        {
            if (event.text.text != nullptr)
            {
                _Context.ProcessTextInput(Rml::String(event.text.text));
            }

            return true;
        }
        case SDL_EVENT_TEXT_EDITING:
            return true;
        default:
            return false;
    }
}

bool MCRmlLayer::Typing() const
{
    const Rml::Element* focus = _Context.GetFocusElement();
    return focus != nullptr && focus->GetTagName() == "input" &&
           focus->GetAttribute<Rml::String>("type", "text") == "text";
}

void MCRmlLayer::Update()
{
    _Kit.Update(_Context);
    _Context.Update();
}

void MCRmlLayer::Tend()
{
    // Copies: a screen's frame may show or hide screens (a window over it, the next screen).
    const std::vector<MCGuiHandle<MCRmlScreen>> shown = _ShownOrder;

    for (const MCGuiHandle<MCRmlScreen>& handle : shown)
    {
        if (const std::shared_ptr<MCRmlScreen> screen = handle.Lock(); screen != nullptr && screen->Shown())
        {
            screen->OnFrame();
        }
    }
}

void MCRmlLayer::HideAll()
{
    const std::vector<MCGuiHandle<MCRmlScreen>> shown = _ShownOrder;

    for (auto at = shown.rbegin(); at != shown.rend(); ++at)
    {
        if (const std::shared_ptr<MCRmlScreen> screen = at->Lock(); screen != nullptr)
        {
            screen->Hide();
        }
    }
}

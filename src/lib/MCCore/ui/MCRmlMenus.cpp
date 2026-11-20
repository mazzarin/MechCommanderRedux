#include "stdafx.h"
#include "ui/MCRmlMenus.h"
#include "platform/MCUtf8Path.h"
#include "ui/MCRmlArt.h"
#include "ui/MCRmlFonts.h"
#include "ui/MCRmlRecorder.h"
#include "ui/MCRmlSystem.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Input.h>

namespace
{
    /// <summary>The context's name (RmlUi keeps them by name).</summary>
    constexpr const char* ContextName = "menus";

    /// <summary>Whether RmlUi is started (it is one per program).</summary>
    bool Running = false;

    /// <summary>SDL's keys the menus use, as RmlUi names them.</summary>
    constexpr std::array<std::pair<SDL_Keycode, Rml::Input::KeyIdentifier>, 15> Keys = {{
        {SDLK_UP, Rml::Input::KI_UP},
        {SDLK_DOWN, Rml::Input::KI_DOWN},
        {SDLK_LEFT, Rml::Input::KI_LEFT},
        {SDLK_RIGHT, Rml::Input::KI_RIGHT},
        {SDLK_RETURN, Rml::Input::KI_RETURN},
        {SDLK_KP_ENTER, Rml::Input::KI_RETURN},
        {SDLK_ESCAPE, Rml::Input::KI_ESCAPE},
        {SDLK_TAB, Rml::Input::KI_TAB},
        {SDLK_BACKSPACE, Rml::Input::KI_BACK},
        {SDLK_DELETE, Rml::Input::KI_DELETE},
        {SDLK_HOME, Rml::Input::KI_HOME},
        {SDLK_END, Rml::Input::KI_END},
        {SDLK_PAGEUP, Rml::Input::KI_PRIOR},
        {SDLK_PAGEDOWN, Rml::Input::KI_NEXT},
        {SDLK_SPACE, Rml::Input::KI_SPACE},
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

MCRmlMenus::MCRmlMenus(Key)
{
}

MCRmlMenus::~MCRmlMenus()
{
    Shutdown();
}

void MCRmlMenus::Shutdown()
{
    if (_Started)
    {
        // RmlUi lets go of its documents, context, textures and font faces here, while the interfaces are still here.
        Rml::Shutdown();
        _Started = false;
        Running = false;
        _Context = nullptr;
        _Document = nullptr;
    }
}

std::expected<std::unique_ptr<MCRmlMenus>, std::string> MCRmlMenus::Create(const MCOptions& options)
{
    if (Running)
    {
        return std::unexpected("the menus' RmlUi is started already");
    }

    const std::expected<std::array<SDL_Color, 256>, std::string> colors = MCRmlArt::Colors(options.FontColors);

    if (!colors)
    {
        return std::unexpected(std::format("the fonts' colours: {}", colors.error()));
    }

    auto menus = std::make_unique<MCRmlMenus>(Key{});
    MCRmlMenus& made = *menus;
    made._Hd = options.Hd;
    made._System = std::make_unique<MCRmlSystem>();
    made._Files = std::make_unique<MCRmlFiles>(options.Folder.empty() ? MCRmlFiles::AppFolder() : options.Folder);
    made._Recorder =
        std::make_unique<MCRmlRecorder>([&made](std::string_view source) { return MCRmlArt::Load(source, made._Hd); });
    made._Fonts = std::make_unique<MCRmlFonts>(*colors, options.Hd);
    Rml::SetSystemInterface(made._System.get());
    Rml::SetFileInterface(made._Files.get());
    Rml::SetRenderInterface(made._Recorder.get());
    Rml::SetFontEngineInterface(made._Fonts.get());

    if (!Rml::Initialise())
    {
        return std::unexpected("RmlUi didn't start (its log says why)");
    }

    made._Started = true;
    Running = true;
    made._Width = std::max(options.Width, 1);
    made._Height = std::max(options.Height, 1);
    made._Context = Rml::CreateContext(ContextName, Rml::Vector2i(made._Width, made._Height));

    if (made._Context == nullptr)
    {
        return std::unexpected("RmlUi made no context");
    }

    made._Context->SetDensityIndependentPixelRatio(DpRatio(made._Width, made._Height));
    return menus;
}

float MCRmlMenus::DpRatio(int32_t width, int32_t height)
{
    return std::min(static_cast<float>(width) / static_cast<float>(ScreenWidth),
                    static_cast<float>(height) / static_cast<float>(ScreenHeight));
}

std::expected<void, std::string> MCRmlMenus::Show(std::string_view path)
{
    Rml::ElementDocument* document = _Context->LoadDocument(std::string(path));

    if (document == nullptr)
    {
        return std::unexpected(std::format("the menus' document {} can't be read from {} (the log says why)", path,
                                           MCFileSystem::ToUtf8(_Files->Folder())));
    }

    if (_Document != nullptr)
    {
        _Document->Close();
    }

    _Document = document;
    _Document->Show();
    return {};
}

MCOptionalRef<Rml::ElementDocument> MCRmlMenus::Document() const
{
    return _Document != nullptr ? MCOptionalRef<Rml::ElementDocument>(*_Document) : std::nullopt;
}

void MCRmlMenus::SetWindow(int32_t width, int32_t height)
{
    width = std::max(width, 1);
    height = std::max(height, 1);

    if (width == _Width && height == _Height)
    {
        return;
    }

    _Width = width;
    _Height = height;
    _Context->SetDimensions(Rml::Vector2i(width, height));
    _Context->SetDensityIndependentPixelRatio(DpRatio(width, height));
}

void MCRmlMenus::SetHd(bool hd)
{
    if (hd == _Hd)
    {
        return;
    }

    _Hd = hd;
    // The pictures are read again as they're next drawn, the letters made again for the new look.
    Rml::ReleaseTextures();
    _Fonts->SetSharp(hd);
}

void MCRmlMenus::SetClock(std::function<double()> clock)
{
    _System->SetClock(std::move(clock));
}

bool MCRmlMenus::HandleEvent(const SDL_Event& event)
{
    switch (event.type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            const float density = PixelsPerPoint(event.motion.windowID);
            return !_Context->ProcessMouseMove(Pixel(event.motion.x, density), Pixel(event.motion.y, density),
                                               ModifiersOf(SDL_GetModState()));
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            const int button = ButtonOf(event.button.button);

            if (button < 0)
            {
                return false;
            }

            const float density = PixelsPerPoint(event.button.windowID);
            const int modifiers = ModifiersOf(SDL_GetModState());
            _Context->ProcessMouseMove(Pixel(event.button.x, density), Pixel(event.button.y, density), modifiers);
            return event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? !_Context->ProcessMouseButtonDown(button, modifiers)
                                                             : !_Context->ProcessMouseButtonUp(button, modifiers);
        }
        case SDL_EVENT_MOUSE_WHEEL:
        {
            // SDL's wheel is away from the player (up) positive, RmlUi's down; flipped, SDL's turned round.
            const float flip = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
            return !_Context->ProcessMouseWheel(Rml::Vector2f(event.wheel.x * flip, -event.wheel.y * flip),
                                                ModifiersOf(SDL_GetModState()));
        }
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        {
            _Context->ProcessMouseLeave();
            return false;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            const Rml::Input::KeyIdentifier key = KeyOf(event.key.key);

            if (key == Rml::Input::KI_UNKNOWN)
            {
                return false;
            }

            const int modifiers = ModifiersOf(event.key.mod);
            return event.type == SDL_EVENT_KEY_DOWN ? !_Context->ProcessKeyDown(key, modifiers)
                                                    : !_Context->ProcessKeyUp(key, modifiers);
        }
        case SDL_EVENT_TEXT_INPUT:
            return event.text.text != nullptr && !_Context->ProcessTextInput(Rml::String(event.text.text));
        default:
            return false;
    }
}

std::shared_ptr<const MCUiDraws> MCRmlMenus::Frame()
{
    _Context->Update();
    _Recorder->Begin(_Width, _Height);
    _Context->Render();
    return _Recorder->Finish();
}

int32_t MCRmlMenus::CellHeight(std::string_view family)
{
    return _Fonts->CellHeight(family);
}

size_t MCRmlMenus::TexturesKept() const
{
    return _Recorder->TexturesKept();
}

size_t MCRmlMenus::GeometryKept() const
{
    return _Recorder->GeometryKept();
}

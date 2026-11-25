#include "stdafx.h"
#include "ui/MCRmlMenus.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCUtf8Path.h"
#include "ui/MCRmlArt.h"
#include "ui/MCRmlRecorder.h"
#include "ui/MCRmlSystem.h"
#include "main/MCLogisticsShared.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/StyleSheetContainer.h>

namespace
{
    /// <summary>The contexts' names (RmlUi keeps them by name).</summary>
    constexpr const char* MenusContext = "menus";
    constexpr const char* MissionContext = "mission";

    /// <summary>
    /// Whether <paramref name="event"/> is the player pressing something (a key, a character, a button, the wheel), which
    /// a change of screen holds; a key or a button let go, and the mouse's moves, pass.
    /// </summary>
    bool IsPress(const SDL_Event& event)
    {
        switch (event.type)
        {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_TEXT_INPUT:
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_WHEEL:
                return true;
            default:
                return false;
        }
    }

    /// <summary>Whether <paramref name="event"/> is the player's own: a press, a key or a button let go, the mouse moving.</summary>
    bool IsPlayerEvent(const SDL_Event& event)
    {
        return IsPress(event) || event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
               event.type == SDL_EVENT_MOUSE_MOTION;
    }

    /// <summary>Whether RmlUi is started (it is one per program).</summary>
    bool RmlStarted = false;

    /// <summary>The game's own (Start), on the game's thread.</summary>
    std::unique_ptr<MCRmlMenus>& GameMenus()
    {
        static std::unique_ptr<MCRmlMenus> menus;
        return menus;
    }

    /// <summary>The face's three files under the <c>ui</c> folder, and the weight each is (SPEC-menus.md, "Letters").</summary>
    struct MCFaceFile
    {
        std::string_view File;
        Rml::Style::FontWeight Weight;
    };

    constexpr int MediumWeight = 500;
    constexpr int SemiBoldWeight = 600;
    constexpr std::array<MCFaceFile, 3> FaceFiles = {{
        {"fonts/SairaSemiCondensed-Medium.ttf", Rml::Style::FontWeight(MediumWeight)},
        {"fonts/SairaSemiCondensed-SemiBold.ttf", Rml::Style::FontWeight(SemiBoldWeight)},
        {"fonts/SairaSemiCondensed-Bold.ttf", Rml::Style::FontWeight::Bold},
    }};
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
        // The screens first, while their documents and RmlUi are here; then RmlUi lets go of its contexts, textures
        // and font faces, while the interfaces are still here.
        _Menus.reset();
        _Mission.reset();
        Rml::Shutdown();
        _Started = false;
        RmlStarted = false;
    }
}

std::expected<std::unique_ptr<MCRmlMenus>, std::string> MCRmlMenus::Create(const MCOptions& options)
{
    if (RmlStarted)
    {
        return std::unexpected("the menus' RmlUi is started already");
    }

    auto menus = std::make_unique<MCRmlMenus>(Key{});
    MCRmlMenus& made = *menus;
    made._Hd = options.Hd;
    made._System = std::make_unique<MCRmlSystem>(options.Window);
    made._Files = std::make_unique<MCRmlFiles>(options.Folder.empty() ? MCRmlFiles::AppFolder() : options.Folder);
    made._Recorder = std::make_unique<MCRmlRecorder>([&made](std::string_view source)
                                                     { return MCRmlArt::Load(source, made._Hd, *made._Files); });
    made._Kit = std::make_unique<MCRmlKit>(options.Sound ? options.Sound : MCRmlKit::MCSound(PlayLogSound));
    Rml::SetSystemInterface(made._System.get());
    Rml::SetFileInterface(made._Files.get());
    Rml::SetRenderInterface(made._Recorder.get());

    if (!Rml::Initialise())
    {
        return std::unexpected("RmlUi didn't start (its log says why)");
    }

    made._Started = true;
    RmlStarted = true;

    for (const MCFaceFile& face : FaceFiles)
    {
        if (!Rml::LoadFontFace(std::string(face.File), std::string(FaceFamily), Rml::Style::FontStyle::Normal,
                               face.Weight))
        {
            return std::unexpected(std::format("the menus' face {} can't be read from {} (the log says why)", face.File,
                                               MCFileSystem::ToUtf8(made._Files->Folder())));
        }
    }

    made._Width = std::max(options.Width, 1);
    made._Height = std::max(options.Height, 1);
    const Rml::Vector2i size(made._Width, made._Height);
    // The mission's first, so the menus' draws go over it.
    Rml::Context* mission = Rml::CreateContext(MissionContext, size);
    Rml::Context* menusContext = Rml::CreateContext(MenusContext, size);

    if (mission == nullptr || menusContext == nullptr)
    {
        return std::unexpected("RmlUi made no context");
    }

    made._Mission =
        MCRmlLayer::Make(*mission, MCRmlLayer::MCKind::Mission,
                         options.MissionUnit ? options.MissionUnit : MCRmlLayer::MCUnit(MissionUnit), *made._Kit);
    made._Menus = MCRmlLayer::Make(*menusContext, MCRmlLayer::MCKind::Menus, MenusUnit, *made._Kit);
    made._Mission->SetWindow(made._Width, made._Height);
    made._Menus->SetWindow(made._Width, made._Height);
    return menus;
}

std::expected<void, std::string> MCRmlMenus::CheckFiles(const std::filesystem::path& folder)
{
    const std::string shown = MCFileSystem::ToUtf8(folder);
    std::error_code error;

    if (!std::filesystem::is_directory(folder, error))
    {
        return std::unexpected(std::format("the menus' folder {} isn't there", shown));
    }

    // Started as the game starts them: the face's files read into RmlUi's font engine (Create says which can't be).
    const auto checked = Create(MCOptions{.Folder = folder});

    if (!checked)
    {
        return std::unexpected(checked.error());
    }

    // The look read whole first: RmlUi takes a file it can't open, or an empty one, for a sheet with no rules.
    if (const auto look = (*checked)->_Files->ReadFile(LookFile); !look || look->empty())
    {
        return std::unexpected(std::format("the menus' look {} can't be read from {}: {}", LookFile, shown,
                                           look ? std::string("it's empty") : look.error()));
    }

    if (Rml::Factory::InstanceStyleSheetFile(std::string(LookFile)) == nullptr)
    {
        return std::unexpected(std::format(
            "the menus' look {} in {} isn't a style sheet RmlUi reads (the log says why)", LookFile, shown));
    }

    return {};
}

std::expected<std::reference_wrapper<MCRmlMenus>, std::string> MCRmlMenus::Start(const MCOptions& options)
{
    std::unique_ptr<MCRmlMenus>& menus = GameMenus();

    if (menus == nullptr)
    {
        auto made = Create(options);

        if (!made)
        {
            return std::unexpected(made.error());
        }

        menus = std::move(*made);
        // While one of the menus' screens shows, the window's mouse, keys and text are theirs (a cursor the game draws,
        // the software cursor, is drawn over them, not into its frame under them). While one shows on the menus' layer
        // (out of a mission, their screen the window), the mouse reaches the whole window full screen too. The
        // mission's layer (the pause, Preferences over the battle) keeps the battle's: its screens float over the
        // battle, which fills the window, and the battle scrolls at its edges.
        MCInput::SetEventLayer([](const SDL_Event& event)
                               { return GameMenus() != nullptr && GameMenus()->HandleEvent(event); },
                               []
                               {
                                   if (GameMenus() == nullptr || !GameMenus()->Showing())
                                   {
                                       return MCInput::MCLayerShows::Nothing;
                                   }

                                   return GameMenus()->Menus().Showing() ? MCInput::MCLayerShows::Window
                                                                         : MCInput::MCLayerShows::OverPicture;
                               });

        // Their draws over the game's picture, recorded as each frame is presented.
        if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
        {
            display->SetOverlaySource(
                [](int width, int height)
                { return GameMenus() != nullptr ? GameMenus()->FrameFor(width, height) : nullptr; });
        }
    }

    return std::ref(*menus);
}

MCOptionalRef<MCRmlMenus> MCRmlMenus::Running()
{
    return GameMenus() != nullptr ? MCOptionalRef<MCRmlMenus>(*GameMenus()) : std::nullopt;
}

void MCRmlMenus::Stop()
{
    if (GameMenus() != nullptr)
    {
        MCInput::SetEventLayer(nullptr);

        if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
        {
            display->SetOverlaySource(nullptr);
        }

        GameMenus().reset();
    }
}

float MCRmlMenus::MenusUnit(int32_t width, int32_t height)
{
    return std::min(static_cast<float>(std::max(width, 1)) / static_cast<float>(ScreenWidth),
                    static_cast<float>(std::max(height, 1)) / static_cast<float>(ScreenHeight));
}

float MCRmlMenus::MissionUnit(int32_t width, int32_t height)
{
    const std::shared_ptr<MCDisplay> display = MCInput::Display();

    if (display == nullptr)
    {
        return MenusUnit(width, height);
    }

    // The mission's screen at INTERFACE SIZE for this window, as the display sizes it: a unit is one of its lines.
    const SDL_Point screen = display->ScreenSizeFor(width, height);
    return screen.y > 0 ? static_cast<float>(height) / static_cast<float>(screen.y) : MenusUnit(width, height);
}

void MCRmlMenus::SetWindow(int32_t width, int32_t height)
{
    _Width = std::max(width, 1);
    _Height = std::max(height, 1);
    _Mission->SetWindow(_Width, _Height);
    _Menus->SetWindow(_Width, _Height);
}

void MCRmlMenus::SetHd(bool hd)
{
    if (hd == _Hd)
    {
        return;
    }

    _Hd = hd;
    // The pictures are read again as they're next drawn.
    Rml::ReleaseTextures();
    _Last.reset();
}

void MCRmlMenus::ShowLive(const std::string& source, std::shared_ptr<const MCUiTexture> picture)
{
    _Recorder->ShowLive(source, std::move(picture));
}

void MCRmlMenus::SetClock(std::function<double()> clock)
{
    _System->SetClock(std::move(clock));
}

void MCRmlMenus::HoldInput(std::function<bool()> held)
{
    _InputHeld = std::move(held);
}

void MCRmlMenus::OnPlayerInput(std::function<void()> input)
{
    _PlayerInput = std::move(input);
}

bool MCRmlMenus::HandleEvent(const SDL_Event& event)
{
    if (Showing() && _PlayerInput != nullptr && IsPlayerEvent(event))
    {
        _PlayerInput();
    }

    if (Showing() && _InputHeld != nullptr && IsPress(event) && _InputHeld())
    {
        return true;
    }

    if (_Menus->Showing())
    {
        return _Menus->HandleEvent(event);
    }

    return _Mission->HandleEvent(event);
}

std::shared_ptr<const MCUiDraws> MCRmlMenus::FrameFor(int32_t width, int32_t height)
{
    // Each frame: the mission layer's unit follows INTERFACE SIZE and UI RESOLUTION with the window kept (each layer
    // lays out again only when its size or unit changed).
    SetWindow(width, height);

    return Frame();
}

std::shared_ptr<const MCUiDraws> MCRmlMenus::Frame()
{
    // Nothing shown: nothing drawn over the game's picture.
    if (!Showing())
    {
        _Last.reset();
        return nullptr;
    }

    _Mission->Update();
    _Menus->Update();
    _Recorder->Begin(_Width, _Height);

    if (_Mission->Showing())
    {
        _Mission->Context().Render();
    }

    if (_Menus->Showing())
    {
        _Menus->Context().Render();
    }

    std::shared_ptr<const MCUiDraws> frame = _Recorder->Finish();

    // Nothing changed: the last frame's draws again, so the presenters see the same picture and keep theirs.
    if (_Last != nullptr && MCUiSameDraws(*_Last, *frame))
    {
        return _Last;
    }

    _Last = frame;
    return frame;
}

size_t MCRmlMenus::TexturesKept() const
{
    return _Recorder->TexturesKept();
}

size_t MCRmlMenus::GeometryKept() const
{
    return _Recorder->GeometryKept();
}

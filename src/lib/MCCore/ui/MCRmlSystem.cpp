#include "stdafx.h"
#include "ui/MCRmlSystem.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCUtf8Path.h"

namespace
{
    constexpr double NanosecondsPerSecond = 1e9;

    /// <summary>The folder under the app's own that holds the menus' files.</summary>
    constexpr std::string_view UiFolder = "ui";

    /// <summary>The name RmlUi's messages of <paramref name="type"/> are logged with.</summary>
    std::string_view TypeName(Rml::Log::Type type)
    {
        switch (type)
        {
            case Rml::Log::LT_ERROR:
            case Rml::Log::LT_ASSERT:
                return "error";
            case Rml::Log::LT_WARNING:
                return "warning";
            case Rml::Log::LT_INFO:
                return "info";
            default:
                return "debug";
        }
    }
}

MCRmlSystem::MCRmlSystem(MCOptionalRef<SDL_Window> window) : _Window(window)
{
}

MCRmlSystem::~MCRmlSystem() = default;

void MCRmlSystem::SetClock(MCClock clock)
{
    _Clock = std::move(clock);
}

double MCRmlSystem::GetElapsedTime()
{
    return _Clock ? _Clock() : static_cast<double>(SDL_GetTicksNS()) / NanosecondsPerSecond;
}

bool MCRmlSystem::LogMessage(Rml::Log::Type type, const Rml::String& message)
{
    // RmlUi's debug messages say what it does every frame; the rest are worth the log.
    if (type != Rml::Log::LT_DEBUG)
    {
        SDL_Log("Menus (RmlUi) %.*s: %s", static_cast<int>(TypeName(type).size()), TypeName(type).data(),
                message.c_str());
    }

    return true;
}

void MCRmlSystem::SetClipboardText(const Rml::String& text)
{
    if (!SDL_SetClipboardText(text.c_str()))
    {
        SDL_Log("Menus: can't put %zu bytes of text on the clipboard: %s", text.size(), SDL_GetError());
    }
}

void MCRmlSystem::GetClipboardText(Rml::String& text)
{
    char* clipboard = SDL_GetClipboardText();
    text = clipboard != nullptr ? clipboard : "";
    SDL_free(clipboard);
}

MCOptionalRef<SDL_Window> MCRmlSystem::TextWindow() const
{
    if (_Window.has_value())
    {
        return _Window;
    }

    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    return display != nullptr ? MCOptionalRef<SDL_Window>(display->Window()) : std::nullopt;
}

void MCRmlSystem::ActivateKeyboard(Rml::Vector2f caretPosition, float lineHeight)
{
    const MCOptionalRef<SDL_Window> window = TextWindow();

    if (!window.has_value())
    {
        return;
    }

    // The caret's place, in the window's points, for the system's input methods (their candidates beside it).
    const float density = std::max(SDL_GetWindowPixelDensity(&window->get()), 1.0f);
    const SDL_Rect area{static_cast<int>(caretPosition.x / density), static_cast<int>(caretPosition.y / density), 1,
                        std::max(static_cast<int>(lineHeight / density), 1)};
    SDL_SetTextInputArea(&window->get(), &area, 0);
    SetTextInput(window->get(), true);
}

void MCRmlSystem::DeactivateKeyboard()
{
    if (const MCOptionalRef<SDL_Window> window = TextWindow(); window.has_value())
    {
        SetTextInput(window->get(), false);
    }
}

void MCRmlSystem::SetTextInput(SDL_Window& window, bool on)
{
    if (on == _Typing)
    {
        return;
    }

    _Typing = on;

    // The game's window: the game's input keeps text input as asked (its chat asks too); another (a test's): SDL's.
    if (!_Window.has_value())
    {
        MCInput::SetTextInput(on);
    }
    else if (!(on ? SDL_StartTextInput(&window) : SDL_StopTextInput(&window)))
    {
        SDL_Log("Menus: SDL's text input didn't %s in window %u: %s", on ? "start" : "stop", SDL_GetWindowID(&window),
                SDL_GetError());
    }
}

MCRmlFiles::MCRmlFiles(std::filesystem::path folder) : _Folder(std::move(folder))
{
}

MCRmlFiles::~MCRmlFiles() = default;

std::filesystem::path MCRmlFiles::AppFolder()
{
    // SDL's base path is the program's folder, or the Mac app's resources (null when it can't tell).
    const char* base = SDL_GetBasePath();
    return base != nullptr ? MCFileSystem::FromUtf8(base) / UiFolder : std::filesystem::path(UiFolder);
}

auto MCRmlFiles::Find(Rml::FileHandle file) -> MCOptionalRef<MCOpenFile>
{
    const auto found = _Open.find(file);
    return found != _Open.end() ? MCOptionalOf(found->second.get()) : std::nullopt;
}

std::expected<std::vector<char>, std::string> MCRmlFiles::ReadFile(std::string_view path) const
{
    const std::filesystem::path relative = MCFileSystem::FromUtf8(std::string(path)).lexically_normal();

    if (path.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory() ||
        std::any_of(relative.begin(), relative.end(), [](const std::filesystem::path& part) { return part == ".."; }))
    {
        return std::unexpected(std::format("'{}' isn't a file of the menus' own", path));
    }

    const std::filesystem::path file = _Folder / relative;
    std::ifstream stream(file, std::ios::binary);

    if (!stream)
    {
        return std::unexpected(std::format("can't open {}", MCFileSystem::ToUtf8(file)));
    }

    return std::vector<char>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool MCRmlFiles::Has(std::string_view path) const
{
    const std::filesystem::path relative = MCFileSystem::FromUtf8(std::string(path)).lexically_normal();
    std::error_code error;
    return !relative.is_absolute() && std::filesystem::is_regular_file(_Folder / relative, error);
}

Rml::FileHandle MCRmlFiles::Open(const Rml::String& path)
{
    std::expected<std::vector<char>, std::string> bytes = ReadFile(path);

    if (!bytes)
    {
        SDL_Log("Menus: %s", bytes.error().c_str());
        return 0;
    }

    auto open = std::make_unique<MCOpenFile>();
    open->Bytes = std::move(*bytes);
    const auto handle = reinterpret_cast<Rml::FileHandle>(open.get());
    _Open.emplace(handle, std::move(open));
    return handle;
}

void MCRmlFiles::Close(Rml::FileHandle file)
{
    _Open.erase(file);
}

size_t MCRmlFiles::Read(void* buffer, size_t size, Rml::FileHandle file)
{
    const MCOptionalRef<MCOpenFile> found = Find(file);

    if (!found.has_value())
    {
        return 0;
    }

    MCOpenFile& open = *found;
    const size_t count = std::min(size, open.Bytes.size() - open.At);
    std::copy_n(open.Bytes.begin() + static_cast<ptrdiff_t>(open.At), count, static_cast<char*>(buffer));
    open.At += count;
    return count;
}

bool MCRmlFiles::Seek(Rml::FileHandle file, long offset, int origin)
{
    const MCOptionalRef<MCOpenFile> found = Find(file);

    if (!found.has_value())
    {
        return false;
    }

    MCOpenFile& open = *found;
    const auto size = static_cast<int64_t>(open.Bytes.size());
    const int64_t from = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? static_cast<int64_t>(open.At) : size;
    const int64_t to = from + offset;

    if (to < 0 || to > size)
    {
        return false;
    }

    open.At = static_cast<size_t>(to);
    return true;
}

size_t MCRmlFiles::Tell(Rml::FileHandle file)
{
    const MCOptionalRef<MCOpenFile> open = Find(file);
    return open.has_value() ? open->get().At : 0;
}

size_t MCRmlFiles::Length(Rml::FileHandle file)
{
    const MCOptionalRef<MCOpenFile> open = Find(file);
    return open.has_value() ? open->get().Bytes.size() : 0;
}

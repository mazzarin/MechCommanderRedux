#pragma once

#include "lib/MCOptionalRef.h"
#include "platform/MCUiDraws.h"
#include "ui/MCRmlRecorder.h"

namespace Rml
{
    class Context;
    class ElementDocument;
}

class MCRmlFiles;
class MCRmlFonts;
class MCRmlFrameInstancer;
class MCRmlRecorder;
class MCRmlSystem;

/// <summary>
/// Port-only (SPEC-menus.md, "How it's built"): the remade menus' RmlUi, owned: RmlUi started with the game's art
/// (ui/MCRmlArt), fonts (ui/MCRmlFonts), files (the app's <c>ui</c> folder) and clock, and one context covering the
/// window, its pixels its own. Lengths in <c>dp</c> are the 1998 screens' pixels: the dp ratio is the scale a 640x480
/// screen would be shown at in the window (the smaller of its width over 640 and its height over 480), so a style sheet
/// lays a screen out in the 1998 sizes and it fills the window as the original's fills it, at the window's own pixels.
/// Each <see cref="Frame"/> lays out what changed and records its draws for the presenter to draw over the game's
/// picture (MCDisplay::SetOverlay).
/// </summary>
/// <remarks>
/// RmlUi is one per program: only one of these at a time (<see cref="Create"/> refuses a second). Everything from the
/// game's thread.
/// </remarks>
class MCRmlMenus
{
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>The 1998 screen's size, whose pixels <c>dp</c> are.</summary>
    static constexpr int32_t ScreenWidth = 640;
    static constexpr int32_t ScreenHeight = 480;

    /// <summary>What a menus' RmlUi starts with.</summary>
    struct MCOptions
    {
        int32_t Width = ScreenWidth;
        int32_t Height = ScreenHeight;
        /// <summary>HD art on: the pack's pictures in place of the 1998 ones, and the letters redrawn sharp.</summary>
        bool Hd = false;
        /// <summary>The documents' folder (empty: the app's own, MCRmlFiles::AppFolder).</summary>
        std::filesystem::path Folder;
        /// <summary>The picture the fonts take their colours from (the 1998 screens' palette), as <c>art:</c> names it.</summary>
        std::string FontColors = "mm_bkgd";
    };

    explicit MCRmlMenus(Key);
    ~MCRmlMenus();
    MCRmlMenus(const MCRmlMenus&) = delete;
    MCRmlMenus& operator=(const MCRmlMenus&) = delete;

    /// <summary>RmlUi started as <paramref name="options"/> say; why not when it can't be (or one is already).</summary>
    static std::expected<std::unique_ptr<MCRmlMenus>, std::string> Create(const MCOptions& options);

    /// <summary>The dp ratio for a window of <paramref name="width"/> x <paramref name="height"/> pixels.</summary>
    static float DpRatio(int32_t width, int32_t height);

    /// <summary>
    /// Loads the document <paramref name="path"/> (under the documents' folder) and shows it, in place of the one shown;
    /// why not when it can't be read.
    /// </summary>
    std::expected<void, std::string> Show(std::string_view path);

    /// <summary>The document shown (none before <see cref="Show"/>).</summary>
    MCOptionalRef<Rml::ElementDocument> Document() const;

    /// <summary>Closes the document shown, if any (RmlUi lets go of it at the next <see cref="Frame"/>).</summary>
    void Close();

    /// <summary>
    /// Pictures whose source starts with <paramref name="scheme"/> (as <c>diagram:</c>) are made by
    /// <paramref name="loader"/> rather than read from the game's art (<c>art:</c>, ui/MCRmlArt): a screen's pictures
    /// drawn from the game's state. An empty loader takes the scheme away.
    /// </summary>
    void SetPictures(std::string scheme, MCRmlRecorder::MCLoader loader);

    /// <summary>Lays the context out again for a window of <paramref name="width"/> x <paramref name="height"/> pixels.</summary>
    void SetWindow(int32_t width, int32_t height);

    /// <summary>Turns HD art on or off: the pictures read again, the letters made again.</summary>
    void SetHd(bool hd);

    /// <summary>Reads the time from <paramref name="clock"/> (seconds; none: the game's clock): tests' frames repeat.</summary>
    void SetClock(std::function<double()> clock);

    /// <summary>
    /// Passes the window's event <paramref name="event"/> to the menus; whether they took it (the pointer over one of
    /// their elements, a key they used), so the game doesn't see it too.
    /// </summary>
    bool HandleEvent(const SDL_Event& event);

    /// <summary>Lays out what changed and records the frame's draws (none to draw: an empty list).</summary>
    std::shared_ptr<const MCUiDraws> Frame();

    /// <summary>The context (tests read its elements' boxes).</summary>
    Rml::Context& Context() const { return *_Context; }

    /// <summary>The height of the font <paramref name="family"/>'s letters in 1998 pixels (0: no such font).</summary>
    int32_t CellHeight(std::string_view family);

    /// <summary>The pictures and geometry RmlUi keeps now.</summary>
    size_t TexturesKept() const;
    size_t GeometryKept() const;

private:
    /// <summary>Shuts RmlUi down, the context with it, before the interfaces go.</summary>
    void Shutdown();

    /// <summary>The picture <paramref name="source"/> names: a scheme's of <see cref="SetPictures"/>, else the art's.</summary>
    std::expected<MCRmlRecorder::MCLoaded, std::string> LoadPicture(std::string_view source) const;

    // The interfaces outlive RmlUi (it is shut down first, in the destructor).
    std::unique_ptr<MCRmlSystem> _System;
    std::unique_ptr<MCRmlFiles> _Files;
    std::unique_ptr<MCRmlRecorder> _Recorder;
    std::unique_ptr<MCRmlFonts> _Fonts;
    /// <summary>The frame-box decorator's instancer (ui/MCRmlFrame), registered with RmlUi while it runs.</summary>
    std::unique_ptr<MCRmlFrameInstancer> _Frames;
    /// <summary>The picture makers of <see cref="SetPictures"/>, by scheme.</summary>
    std::map<std::string, MCRmlRecorder::MCLoader, std::less<>> _Pictures;
    bool _Hd = false;
    bool _Started = false;
    int32_t _Width = 0;
    int32_t _Height = 0;
    /// <summary>RmlUi's (its interface hands out raw pointers; RmlUi destroys them).</summary>
    Rml::Context* _Context = nullptr;
    Rml::ElementDocument* _Document = nullptr;
};

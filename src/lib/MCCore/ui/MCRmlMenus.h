#pragma once

#include "lib/MCOptionalRef.h"
#include "platform/MCUiDraws.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlLayer.h"

class MCRmlFiles;
class MCRmlRecorder;
class MCRmlSystem;

/// <summary>
/// Port-only (SPEC-menus.md, "How it's built"): the remade menus' RmlUi, owned: RmlUi started with the app's face
/// (Saira Semi Condensed, its three weights, on RmlUi's FreeType font engine), the game's art (ui/MCRmlArt), the app's
/// <c>ui</c> folder, the clock, the log, the clipboard and SDL's text input (ui/MCRmlSystem), the controls' kit
/// (ui/MCRmlKit), and its two layers over the window: the menus' (one unit the window over <c>min(width / 640,
/// height / 480)</c>) and the mission's (one unit the mission's INTERFACE SIZE). Each <see cref="Frame"/> lays out what
/// changed and records both layers' draws, the mission's under the menus', for the presenter to draw over the game's
/// picture (MCDisplay::SetOverlay).
/// </summary>
/// <remarks>
/// RmlUi is one per program: only one of these at a time (<see cref="Create"/> refuses a second). The game's own is
/// <see cref="Start"/>ed with its display and found by <see cref="Running"/>. Everything from the game's thread.
/// </remarks>
class MCRmlMenus
{
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>The 1998 screen's size, one unit a pixel of it.</summary>
    static constexpr int32_t ScreenWidth = 640;
    static constexpr int32_t ScreenHeight = 480;

    /// <summary>The face's family, as the look's style sheet names it, and its files under the <c>ui</c> folder.</summary>
    static constexpr std::string_view FaceFamily = "Saira";

    /// <summary>What a menus' RmlUi starts with.</summary>
    struct MCOptions
    {
        int32_t Width = ScreenWidth;
        int32_t Height = ScreenHeight;
        /// <summary>HD art on: the pack's pictures in place of the 1998 ones (and the app's art's HD copies).</summary>
        bool Hd = false;
        /// <summary>The documents' folder (empty: the app's own, MCRmlFiles::AppFolder).</summary>
        std::filesystem::path Folder;
        /// <summary>The mission layer's unit for a window (none: the mission's INTERFACE SIZE on the game's display).</summary>
        MCRmlLayer::MCUnit MissionUnit;
        /// <summary>What plays the controls' sounds (none: the game's interface sounds).</summary>
        MCRmlKit::MCSound Sound;
        /// <summary>The window SDL's text input is started on (none: the game's display's).</summary>
        MCOptionalRef<SDL_Window> Window;
    };

    explicit MCRmlMenus(Key);
    ~MCRmlMenus();
    MCRmlMenus(const MCRmlMenus&) = delete;
    MCRmlMenus& operator=(const MCRmlMenus&) = delete;

    /// <summary>RmlUi started as <paramref name="options"/> say; why not when it can't be (or one is already).</summary>
    static std::expected<std::unique_ptr<MCRmlMenus>, std::string> Create(const MCOptions& options);

    /// <summary>
    /// The game's own menus' RmlUi, started (made once, the first time a remade screen is wanted) or why not, over the
    /// game's display: its frames drawn over the game's picture as each is presented (MCDisplay::SetOverlaySource), and
    /// the window's input theirs while one of their screens shows (MCInput::SetEventLayer). It lives until
    /// <see cref="Stop"/> (the game's end).
    /// </summary>
    static std::expected<std::reference_wrapper<MCRmlMenus>, std::string> Start(const MCOptions& options);
    /// <summary>The game's own, when started.</summary>
    static MCOptionalRef<MCRmlMenus> Running();
    /// <summary>
    /// STAND-IN (menus stage 5, until menus-kit-fix's own lands in <c>menus</c>): the game's own, started over the game's
    /// display the first time a remade screen is wanted (its window's pixels, HD art as the game draws it), or why not.
    /// </summary>
    static std::expected<std::reference_wrapper<MCRmlMenus>, std::string> ForGame();
    /// <summary>Stops the game's own (its screens destroyed, RmlUi shut down).</summary>
    static void Stop();

    /// <summary>The menus' unit (the dp ratio) for a window of <paramref name="width"/> x <paramref name="height"/> pixels.</summary>
    static float MenusUnit(int32_t width, int32_t height);
    /// <summary>The mission's unit for that window: its pixels over the lines of the mission's screen at INTERFACE SIZE.</summary>
    static float MissionUnit(int32_t width, int32_t height);

    /// <summary>The menus' layer, and the mission's.</summary>
    MCRmlLayer& Menus() const { return *_Menus; }
    MCRmlLayer& Mission() const { return *_Mission; }

    /// <summary>Whether a remade screen shows on either layer (SDL's events go to RmlUi then).</summary>
    bool Showing() const { return _Menus->Showing() || _Mission->Showing(); }

    /// <summary>Both layers laid out again for a window of <paramref name="width"/> x <paramref name="height"/> pixels.</summary>
    void SetWindow(int32_t width, int32_t height);
    int32_t Width() const { return _Width; }
    int32_t Height() const { return _Height; }

    /// <summary>Turns HD art on or off: the pictures read again.</summary>
    void SetHd(bool hd);

    /// <summary>Reads the time from <paramref name="clock"/> (seconds; none: the game's clock): tests' frames repeat.</summary>
    void SetClock(std::function<double()> clock);

    /// <summary>
    /// Passes the window's event <paramref name="event"/> to the layer on top (the menus' over the mission's); whether
    /// it was theirs (a mouse, key or text event while one of their screens shows), so the game doesn't see it too.
    /// </summary>
    bool HandleEvent(const SDL_Event& event);

    /// <summary>
    /// Lays out what changed and records the frame's draws, the mission's layer under the menus' (none shown: none at
    /// all). Unchanged since the last frame: the same draws, so the presenters keep their picture.
    /// </summary>
    std::shared_ptr<const MCUiDraws> Frame();
    /// <summary><see cref="Frame"/> for a window of <paramref name="width"/> x <paramref name="height"/> pixels (laid out again when it changed).</summary>
    std::shared_ptr<const MCUiDraws> FrameFor(int32_t width, int32_t height);

    /// <summary>The kit (tests read the sounds played).</summary>
    MCRmlKit& Kit() const { return *_Kit; }

    /// <summary>The pictures and geometry RmlUi keeps now.</summary>
    size_t TexturesKept() const;
    size_t GeometryKept() const;

private:
    /// <summary>Shuts RmlUi down, its contexts with it, before the interfaces go.</summary>
    void Shutdown();

    // The interfaces outlive RmlUi (it is shut down first, in the destructor).
    std::unique_ptr<MCRmlSystem> _System;
    std::unique_ptr<MCRmlFiles> _Files;
    std::unique_ptr<MCRmlRecorder> _Recorder;
    std::unique_ptr<MCRmlKit> _Kit;
    std::unique_ptr<MCRmlLayer> _Mission;
    std::unique_ptr<MCRmlLayer> _Menus;
    bool _Hd = false;
    bool _Started = false;
    int32_t _Width = 0;
    int32_t _Height = 0;
    /// <summary>The last frame recorded, handed out again while nothing changes.</summary>
    std::shared_ptr<const MCUiDraws> _Last;
};

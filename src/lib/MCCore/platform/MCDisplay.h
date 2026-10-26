#pragma once

#include "platform/MCPresenter.h"

/// <summary>How <see cref="MCDisplay::Create"/> opens the window.</summary>
struct MCDisplayOptions
{
    /// <summary>The window title (the original's <c>appName</c>).</summary>
    std::string Title = "MechCommander Redux";
    /// <summary>The logical screen: the size of the 8-bit buffer the game draws into (PREFS "Resolution").</summary>
    int Width = 640;
    int Height = 480;
    /// <summary>Start in (desktop) fullscreen.</summary>
    bool Fullscreen = false;
    /// <summary>Let the user resize the window; the screen is scaled to fit.</summary>
    bool Resizable = true;
    /// <summary>The windowed size as a multiple of the logical size; 0 picks the largest that fits the desktop.</summary>
    int WindowScale = 0;
    /// <summary>Wait for the display's refresh in <see cref="MCDisplay::Present"/>.</summary>
    bool VSync = true;
    /// <summary>Scale only by whole multiples (sharper, with wider borders).</summary>
    bool IntegerScale = false;
    /// <summary>Fill the whole window, ignoring the aspect ratio (no bars); wins over <see cref="IntegerScale"/>.</summary>
    bool Stretch = false;
    /// <summary>Filter the scaled image linearly instead of taking the nearest pixel.</summary>
    bool LinearFilter = false;
    /// <summary>Create the window hidden (tests).</summary>
    bool Hidden = false;
    /// <summary>
    /// Give the window the display's pixel density (a Retina screen's 2 pixels a point), so a window-following screen
    /// is that many pixels; else one pixel a point.
    /// </summary>
    bool HighPixelDensity = true;
    /// <summary>
    /// The logical screen follows the window: <see cref="Width"/> x <see cref="Height"/> is the smallest it gets (and
    /// the window's minimum size), the window opens at the scale of that size <see cref="WindowScale"/> picks, and the
    /// screen starts at the window's size in pixels. The window is never resized to fit the screen.
    /// </summary>
    bool FollowWindow = false;
    /// <summary>
    /// The tallest a window-following screen gets (0: as tall as the window); see
    /// <see cref="MCDisplay::SetMaxHeight"/>.
    /// </summary>
    int MaxHeight = 0;
    /// <summary>
    /// A window-following screen is also no taller than the window in points (its pixels over the display's scale):
    /// on a Retina screen or a scaled Windows desktop, a line takes what a point does, so the UI keeps the size it has
    /// on a 1x screen; see <see cref="MCDisplay::SetFollowDisplayScale"/>.
    /// </summary>
    bool FollowDisplayScale = false;
    /// <summary>
    /// A window-following screen has this many times fewer lines than the window and the limits above give it (but
    /// no fewer than <see cref="Height"/>), so what is drawn on it at a fixed size is that much bigger; see
    /// <see cref="MCDisplay::SetInterfaceScale"/>.
    /// </summary>
    float InterfaceScale = 1.0f;
    /// <summary>The renderer asked for; Vulkan falls back to software when it can't start.</summary>
    MCRendererKind Renderer = MCRendererKind::Vulkan;
};

/// <summary>
/// The limits a window-following screen's size keeps to (<see cref="MCDisplay::WindowScreenSize"/>), as
/// <see cref="MCDisplayOptions"/> and the setters give them.
/// </summary>
struct MCScreenFit
{
    /// <summary>The smallest the screen gets.</summary>
    int MinWidth = 640;
    int MinHeight = 480;
    /// <summary>The tallest it gets (0: as tall as the window).</summary>
    int MaxHeight = 0;
    /// <summary>No taller than the window in points either.</summary>
    bool FollowDisplayScale = false;
    /// <summary>How many times fewer lines than the other limits give it (1: none).</summary>
    float InterfaceScale = 1.0f;
};

/// <summary>
/// The window and the screen the game draws into: the port's replacement for DirectDraw's primary surface, its
/// palette and <c>Flip</c> (and the GDI DIB fallback of windowed mode).
/// </summary>
/// <remarks>
/// <para>The game draws every frame into one 8-bit buffer, the VFX <c>_window</c> <see cref="Screen"/> returns,
/// <see cref="Width"/> x <see cref="Height"/> palette indices with a pitch of <see cref="Width"/>. <see cref="Present"/>
/// hands it to the presenter of the renderer in use (<see cref="MCPresenter"/>), with its op plane, its underlays
/// (<see cref="MCRenderer::Underlays"/>: the world view, drawn at 1x into a surface of its own) and the palette.
/// Vulkan (<see cref="MCVulkanPresenter"/>) composites them in a shader and scales the shown view into the window;
/// software (<see cref="MCSdlPresenter"/>) composites on the CPU (<see cref="ComposeScreen"/>) and shows the result
/// with SDL's 2D renderer. Either way the view is letterboxed into the window, nearest unless
/// <see cref="MCDisplayOptions::LinearFilter"/>.</para>
/// <para>Threads: <see cref="SetPalette"/>, <see cref="Screen"/> and the getters may be called from any thread
/// (the palette is guarded); everything else, <see cref="Present"/> included, from the thread that created the
/// display (SDL's video thread).</para>
/// </remarks>
class MCDisplay
{
    /// <summary>Only <see cref="Create"/> makes one.</summary>
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>A display with no window yet.</summary>
    explicit MCDisplay(Key) {}

    /// <summary>Opens the window and the renderer and makes the screen buffer (initialising SDL's video if needed).</summary>
    static std::expected<std::unique_ptr<MCDisplay>, std::string> Create(const MCDisplayOptions& options);

    ~MCDisplay();
    MCDisplay(const MCDisplay&) = delete;
    MCDisplay& operator=(const MCDisplay&) = delete;

    /// <summary>The SDL window.</summary>
    SDL_Window& Window() const { return *_Window; }

    /// <summary>The renderer in use (Vulkan, or software when asked for or when Vulkan couldn't start).</summary>
    MCRendererKind RendererKind() const { return _Presenter->Kind(); }

    /// <summary>The renderer in use, for logs.</summary>
    std::string RendererName() const { return _Presenter->Name(); }

    /// <summary>
    /// Sets the window's title (the original's <c>SetWindowTextA</c>), with the renderer in use after it
    /// ("... [Vulkan]", "... [Software]").
    /// </summary>
    void SetTitle(std::string_view title);

    /// <summary>Whether the logical screen follows the window (<see cref="MCDisplayOptions::FollowWindow"/>).</summary>
    bool FollowsWindow() const { return _FollowWindow; }

    /// <summary>
    /// The size a window-following screen should have now: the window's size in pixels, or, for a window taller than
    /// the most (<see cref="SetMaxHeight"/>, or its height in points with <see cref="SetFollowDisplayScale"/>), that
    /// many lines at the window's aspect; with an interface scale (<see cref="SetInterfaceScale"/>), that many times
    /// fewer lines; no smaller than the minimum (<see cref="MCDisplayOptions::Width"/> x
    /// <see cref="MCDisplayOptions::Height"/>). A minimised window keeps the current size.
    /// </summary>
    void WindowScreenSize(int& width, int& height) const;

    /// <summary>
    /// The pure form of <see cref="WindowScreenSize"/>: the screen a window <paramref name="pixelWidth"/> x
    /// <paramref name="pixelHeight"/> pixels big, at <paramref name="displayScale"/> pixels a point, gets within
    /// <paramref name="fit"/>'s limits.
    /// </summary>
    static SDL_Point FollowingScreenSize(int pixelWidth, int pixelHeight, float displayScale, const MCScreenFit& fit);

    /// <summary>
    /// Sets the tallest a window-following screen gets (0: as tall as the window), from the next
    /// <see cref="WindowScreenSize"/> on. A screen shorter than the window is shown scaled up, but its underlays (the
    /// world view) are composited at the window's pixels (<see cref="UnderlayScale"/>): only the UI drawn on the screen
    /// is coarser.
    /// </summary>
    void SetMaxHeight(int height) { _MaxHeight = std::max(height, 0); }

    /// <summary>
    /// Sets whether a window-following screen is also held to the window's height in points
    /// (<see cref="MCDisplayOptions::FollowDisplayScale"/>), from the next <see cref="WindowScreenSize"/> on.
    /// </summary>
    void SetFollowDisplayScale(bool follow) { _FollowDisplayScale = follow; }

    /// <summary>
    /// Sets how many times fewer lines a window-following screen has than it would otherwise (1: none; the interface
    /// drawn on it is that much bigger, the world view keeps the window's pixels), from the next
    /// <see cref="WindowScreenSize"/> on.
    /// </summary>
    void SetInterfaceScale(float scale) { _InterfaceScale = std::max(scale, 1.0f); }

    /// <summary>
    /// How many window pixels the underlays are composited at per screen pixel down: above 1 when the whole screen is
    /// shown scaled up (a screen held under the window's height by <see cref="SetMaxHeight"/>), else 1.
    /// </summary>
    float UnderlayScale() const;

    /// <summary>
    /// How many window pixels a screen pixel covers down when the whole screen is shown, as a running scenario shows
    /// it: <see cref="UnderlayScale"/> for the whole screen, even while only part of it is shown (the menus' corner).
    /// </summary>
    float ScreenScale() const;

    /// <summary>
    /// The largest size <see cref="WindowScreenSize"/> can return on this machine: the biggest desktop of any display,
    /// in pixels (at least the current screen).
    /// </summary>
    void LargestScreenSize(int& width, int& height) const;

    /// <summary>The logical screen's width.</summary>
    int Width() const { return _Width; }

    /// <summary>The logical screen's height.</summary>
    int Height() const { return _Height; }

    /// <summary>
    /// The screen as a VFX window. It stays the same window for the display's life; after
    /// <see cref="SetLogicalSize"/> its buffer and extents are the new ones.
    /// </summary>
    MCWindow& Screen() { return *_Screen; }

    /// <summary>The screen's pixels, <see cref="Width"/> bytes per row.</summary>
    std::span<uint8_t> Pixels() const { return _Screen->Bytes(); }

    /// <summary>
    /// Changes the logical screen (the game's mode switch, <c>aSystem::resetDirectDraw</c>). The buffer is
    /// reallocated and cleared to 0; a windowed window is resized to keep its scale, unless the screen follows the
    /// window.
    /// </summary>
    std::expected<void, std::string> SetLogicalSize(int width, int height);

    /// <summary>
    /// Shows only part of the screen: the <paramref name="width"/> x <paramref name="height"/> rectangle at
    /// (<paramref name="x"/>, <paramref name="y"/>), clamped to the screen, is what gets scaled into the window
    /// (letterboxed like the whole screen), and the mouse maps into it. The whole screen is the default and comes
    /// back after <see cref="SetLogicalSize"/>. Used to blow up the 640x480 menus the game draws in the top-left
    /// corner of a larger screen.
    /// </summary>
    /// <returns>Whether the view changed.</returns>
    bool SetView(int x, int y, int width, int height);

    /// <summary>The part of the screen that is shown (see <see cref="SetView"/>), in screen pixels.</summary>
    SDL_Rect View() const { return _View; }

    /// <summary>
    /// Sets the palette entries from <paramref name="first"/> to <paramref name="entries"/> (DirectDraw's
    /// <c>SetEntries</c> and <c>AnimatePalette</c>, GDI's <c>SetDIBColorTable</c>); those past entry 255 are left out.
    /// 8 bits per channel. Takes effect at the next <see cref="Present"/>; calling it every frame is cheap (palette
    /// animation).
    /// </summary>
    void SetPalette(int first, std::span<const MCVfxRgb> entries);

    /// <summary>As the other overload, from packed r, g, b bytes (three per entry).</summary>
    void SetPalette(int first, std::span<const uint8_t> rgb);

    /// <summary>Reads the palette entries from <paramref name="first"/> into <paramref name="out"/> (as set, without
    /// gamma); those past entry 255 are left as they are.</summary>
    void GetPalette(int first, std::span<MCVfxRgb> out) const;

    /// <summary>
    /// A display gamma applied to the palette on its way to the GPU (1 = none; above 1 brightens the midtones), and a
    /// brightness factor (1 = none). The game's own gamma steps (<c>gammaCorrectCurrentPalette</c>) edit the palette
    /// itself; this is the port's extra hook for the options screen.
    /// </summary>
    void SetGamma(float gamma, float brightness = 1.0f);

    /// <summary>The gamma set last.</summary>
    float Gamma() const { return _Gamma; }

    /// <summary>
    /// Sets the colour cycle shown over the palette (the water's): takes effect at the next <see cref="Present"/>
    /// without changing the palette.
    /// </summary>
    void SetColorCycle(const MCColorCycle& cycle);

    /// <summary>
    /// Fades the whole picture toward black: <paramref name="levels"/> (0 none .. 255 black) are taken off every shown
    /// colour's components, by the presenter (the composite shader on the GPU), whatever the surfaces hold. Takes effect
    /// at the next <see cref="Present"/>; the palette isn't changed.
    /// </summary>
    void SetFade(int levels);

    /// <summary>The fade set last.</summary>
    int Fade() const { return _Fade; }

    /// <summary>The 256 colours as they are shown: the palette through the gamma and brightness, then the cycle.</summary>
    void GetShownColors(std::span<SDL_Color, 256> out) const;

    /// <summary>
    /// How many output pixels one logical pixel covers across and down (window pixels, not points; the two differ
    /// on high-density displays). 1 before the first present.
    /// </summary>
    void PixelScale(float& scaleX, float& scaleY) const;

    /// <summary>
    /// Port-only: whether the screen is drawn at the size it is shown (<see cref="MCFrame::SharpScreen"/>): with HD art
    /// on (<see cref="GHdArt"/>), when the GPU renderer draws the frame surfaces alone (not mirror mode).
    /// </summary>
    bool SharpScreen() const;

    /// <summary>
    /// Shows the screen: uploads the pixels and the palette and presents (waiting for the display's refresh when
    /// vsync is on). A minimised window is skipped without waiting.
    /// </summary>
    std::expected<void, std::string> Present();

    /// <summary>Switches between desktop fullscreen and the window (the original's InitFullScreen / InitWindowMode).</summary>
    bool SetFullscreen(bool fullscreen);

    /// <summary>Flips <see cref="IsFullscreen"/>.</summary>
    bool ToggleFullscreen() { return SetFullscreen(!IsFullscreen()); }

    /// <summary>Whether the window is fullscreen.</summary>
    bool IsFullscreen() const;

    /// <summary>Turns waiting for the display's refresh on or off.</summary>
    void SetVSync(bool on);

    /// <summary>
    /// Writes the shown view as it is now (the screen with its underlays, at the screen's size) as an 8-bit PNG with its
    /// palette (the game's screenshot key).
    /// </summary>
    std::expected<void, std::string> SaveScreenshot(const std::filesystem::path& path) const;

    /// <summary>
    /// Writes the picture the window shows as an RGBA PNG: the frame composited by the presenter as
    /// <see cref="Present"/> does (on the GPU with Vulkan, GPU-drawn surfaces included), at the window pixels a scaled
    /// view covers, else the shown view at the screen's size; without the bars around it.
    /// </summary>
    std::expected<void, std::string> SaveShownScreenshot(const std::filesystem::path& path);

    /// <summary>
    /// The whole screen as the player sees it, at the screen's size: its pixels with the underlays' pixels in place of
    /// the key, as <see cref="Present"/> shows them (nearest). When the GPU draws the frame, read back from it (what was
    /// drawn so far is run first).
    /// </summary>
    std::vector<uint8_t> ComposeScreen() const;

    /// <summary>Whether the screen's underlays are composited by the shader (else by the CPU).</summary>
    bool CompositesOnGpu() const { return _Presenter->CompositesOnGpu(); }

    /// <summary>
    /// Draws the frame as <see cref="Present"/> would, at the screen's size, into an offscreen texture and reads it
    /// back as RGBA (tests compare it with <see cref="ComposeScreen"/>).
    /// </summary>
    std::expected<std::vector<SDL_Color>, std::string> ReadFrame();

    /// <summary>
    /// Maps a point in window coordinates (as SDL's mouse events give them) to the logical screen.
    /// </summary>
    /// <returns>Whether the point is on the shown view (not on a border). The mapped point is set either way.</returns>
    bool WindowToLogical(float windowX, float windowY, float& logicalX, float& logicalY) const;

    /// <summary>Maps a point of the logical screen to window coordinates (for warping and clipping the mouse).</summary>
    void LogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const;

    /// <summary>Maps an output point through a viewport to the logical screen (the pure form of <see cref="WindowToLogical"/>).</summary>
    static bool MapToLogical(const MCViewport& viewport, int logicalWidth, int logicalHeight, float x, float y,
                             float& logicalX, float& logicalY);

    /// <summary>
    /// Tests: called at the start of each <see cref="Present"/>, with the screen as it is about to be shown (frames
    /// drawn inside the game's own loops, such as the logistics screen wipes, are seen too).
    /// </summary>
    std::function<void()> OnPresent;

private:
    /// <summary>Resizes the window to the screen's size times its scale (windowed, not following the window).</summary>
    void ResizeWindowToScale();
    /// <summary>Makes the screen's buffer and op plane for its size.</summary>
    void MakeScreen();
    /// <summary>
    /// This frame for the presenter: the screen, its ops, its underlays and the colours as shown (marked changed when
    /// they changed since the last frame, or always with <paramref name="allColors"/>).
    /// </summary>
    MCFrame BuildFrame(bool allColors);
    /// <summary>The underlays shown on the screen now (those that lie under it and have pixels).</summary>
    std::vector<MCUnderlay> ScreenUnderlays() const;
    /// <summary>
    /// Ends the colour cycle when palette entries <paramref name="first"/>.. (<paramref name="count"/> of them) cover
    /// any it remaps: setting them replaced the animated colours. Called with the palette lock held.
    /// </summary>
    void EndCycleOver(int first, int count);
    /// <summary>Where the shown view lands in the window, in window pixels.</summary>
    MCViewport ShownViewport() const;
    /// <summary>
    /// The size the frame is composited at when the whole screen is shown scaled up (the window's pixels it covers), so
    /// the underlays keep the window's resolution, or, with <see cref="SharpScreen"/>, any view shown scaled up (the
    /// 640x480 screens'); false (and nothing set) otherwise.
    /// </summary>
    bool ScaledOutput(int& width, int& height) const;
    /// <summary>
    /// <see cref="ScaledOutput"/> for the whole screen shown, whatever part of it is shown now: the window pixels it
    /// would cover when the screen follows the window and is shown scaled up; false (and nothing set) otherwise.
    /// </summary>
    bool WholeScreenOutput(int& width, int& height) const;

    /// <summary>The window; the presenter shares it, so it outlives what draws into it.</summary>
    std::shared_ptr<SDL_Window> _Window;
    std::unique_ptr<MCPresenter> _Presenter;
    /// <summary>The screen's op plane (see <see cref="MCRenderer::SetOpPlane"/>).</summary>
    std::shared_ptr<std::vector<uint8_t>> _Ops = std::make_shared<std::vector<uint8_t>>();
    /// <summary>This frame's underlays on the screen, and the colours as shown.</summary>
    std::vector<MCUnderlay> _FrameUnderlays;
    std::array<SDL_Color, 256> _ShownColors{};
    /// <summary>The palette's colours as shown, before the cycle.</summary>
    std::array<SDL_Color, 256> _PaletteColors{};

    int _Width = 0;
    int _Height = 0;
    /// <summary>The shown part of the screen.</summary>
    SDL_Rect _View{};
    int _WindowScale = 1;
    /// <summary>The smallest a window-following screen gets.</summary>
    int _MinWidth = 0;
    int _MinHeight = 0;
    /// <summary>The tallest a window-following screen gets (0: no limit).</summary>
    int _MaxHeight = 0;
    bool _FollowDisplayScale = false;
    float _InterfaceScale = 1.0f;
    bool _FollowWindow = false;
    /// <summary>
    /// The screen's window. Its pixels are shared by the screen ports; they are reallocated only when the screen grows
    /// past <see cref="_PixelCapacity"/>, so a smaller screen keeps the pixels the ports share.
    /// </summary>
    std::shared_ptr<MCWindow> _Screen = std::make_shared<MCWindow>();
    /// <summary>How many pixels the screen's pixels hold.</summary>
    size_t _PixelCapacity = 0;

    mutable std::mutex _PaletteLock;
    std::array<MCVfxRgb, 256> _Palette{};
    bool _PaletteDirty = true;
    MCColorCycle _Cycle;
    bool _CycleDirty = false;
    float _Gamma = 1.0f;
    float _Brightness = 1.0f;
    /// <summary>The screen fade (<see cref="SetFade"/>).</summary>
    std::atomic<int> _Fade = 0;
};

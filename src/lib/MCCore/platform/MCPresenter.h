#pragma once

// The frame half of the renderer: what turns the screen the game drew (8-bit, with its op plane and underlays) into
// the picture in the window. MCDisplay owns the window, the screen buffer and the palette, and hands each frame to a
// presenter. MCVulkanPresenter composites and scales on the GPU (SDL GPU, Vulkan, HLSL shaders); MCSdlPresenter, the
// software renderer's, composites on the CPU and shows the result with SDL's 2D renderer.

#include "platform/MCRenderer.h"
#include "platform/MCUiDraws.h"

/// <summary>Which renderer draws the game (PREFS "Renderer", <c>-renderer</c>).</summary>
enum class MCRendererKind
{
    /// <summary>The GPU, through SDL GPU on Vulkan (the default; falls back to software when it can't start).</summary>
    Vulkan,
    /// <summary>The CPU, shown with SDL's 2D renderer.</summary>
    Software
};

/// <summary>The renderer kind a name stands for ("vulkan", "software", any case); empty for another name.</summary>
std::optional<MCRendererKind> MCRendererKindFromName(std::string_view name);

/// <summary>The name of a renderer kind, as <see cref="MCRendererKindFromName"/> reads it.</summary>
std::string_view MCRendererKindName(MCRendererKind kind);

/// <summary>The GPU renderer's name in the menus (the preferences' RENDERER choice, on every platform).</summary>
inline constexpr std::string_view MCGpuRendererChoice = "VULKAN";

/// <summary>The graphics API the GPU renderer runs on (SDL GPU's driver), as the window's title names it.</summary>
#if defined(__APPLE__)
inline constexpr std::string_view MCGpuApiName = "Metal";
#else
inline constexpr std::string_view MCGpuApiName = "Vulkan";
#endif

/// <summary>A rectangle in window pixels where the logical screen is shown.</summary>
struct MCViewport
{
    float X = 0.0f;
    float Y = 0.0f;
    float W = 0.0f;
    float H = 0.0f;
};

/// <summary>How the shown part of the screen is fitted into the window.</summary>
struct MCPresentation
{
    /// <summary>Scale only by whole multiples (sharper, with wider borders).</summary>
    bool IntegerScale = false;
    /// <summary>Fill the whole window, ignoring the aspect ratio; wins over <see cref="IntegerScale"/>.</summary>
    bool Stretch = false;
    /// <summary>Filter the scaled image linearly instead of taking the nearest pixel.</summary>
    bool Linear = false;
};

/// <summary>
/// A colour cycle done as an index remap (the water's, cycleColors): shown, palette index <c>First + i</c> (i
/// 0..7) takes the colour of <c>Sources[(i + Step) &amp; 7]</c>. The palette itself stays as it is, so the GPU's
/// palette texture only changes when the palette does.
/// </summary>
struct MCColorCycle
{
    /// <summary>The first index cycled.</summary>
    uint8_t First = 0;
    /// <summary>The entries the cycled indices take their colours from.</summary>
    std::array<uint8_t, 8> Sources{};
    /// <summary>The step (0..7); -1 when nothing is cycled.</summary>
    int32_t Step = -1;

    /// <summary>The palette index whose colour <paramref name="index"/> shows.</summary>
    uint8_t Remap(uint8_t index) const
    {
        const uint32_t i = static_cast<uint32_t>(index) - First;
        return Step >= 0 && i < 8 ? Sources[(i + static_cast<uint32_t>(Step)) & 7] : index;
    }

    bool operator==(const MCColorCycle&) const = default;
};

/// <summary>
/// One frame as the game left it, handed to <see cref="MCPresenter::Present"/>: it borrows the display's screen,
/// pixels and colours for that one call.
/// </summary>
struct MCFrame
{
    /// <summary>The screen as a VFX window (its pixels, <c>Width</c> bytes a row).</summary>
    std::reference_wrapper<const MCWindow> Screen;
    std::span<const uint8_t> Pixels;
    int Width = 0;
    int Height = 0;
    /// <summary>The part of the screen that is shown in the window.</summary>
    SDL_Rect View{};
    /// <summary>
    /// The size the frame is composited at, in window pixels, when the view is shown scaled up and its underlays keep
    /// the window's resolution (<see cref="MCDisplay::SetMaxHeight"/>): the screen's pixels are taken nearest, the
    /// underlays' at their own resolution (<see cref="MCComposeScaled"/>). 0 composites at the view's size, then
    /// scales the whole picture.
    /// </summary>
    int OutputWidth = 0;
    int OutputHeight = 0;
    /// <summary>
    /// Port-only, with HD art on (<see cref="MCDisplay::SharpScreen"/>): the screen is drawn at the size it is shown
    /// (as its underlays are), so the GPU renderer's HD text and art show at the output's resolution. Its palette
    /// indices are the window's as ever.
    /// </summary>
    bool SharpScreen = false;
    /// <summary>The screen's op plane (one byte a pixel, laid out as the pixels).</summary>
    std::span<const uint8_t> Ops;
    /// <summary>The screen's underlays this frame (only those that lie under the screen and have pixels).</summary>
    std::span<const MCUnderlay> Underlays;
    /// <summary>
    /// The 256 colours as shown (gamma applied, <see cref="Cycle"/> applied), and whether they changed since the last
    /// frame.
    /// </summary>
    std::span<const SDL_Color> Colors;
    bool ColorsChanged = false;
    /// <summary>
    /// The palette's colours (gamma applied) without the cycle, and whether they changed since the last frame: a GPU
    /// presenter keeps these in a texture and applies <see cref="Cycle"/> in its shader.
    /// </summary>
    std::span<const SDL_Color> PaletteColors;
    bool PaletteChanged = false;
    MCColorCycle Cycle;
    /// <summary>
    /// The screen fade (<see cref="MCDisplay::SetFade"/>): levels taken off every shown colour's components, 0 (none)
    /// to 255 (black). The presenter applies it to the finished picture; <see cref="Colors"/> don't include it.
    /// </summary>
    int Fade = 0;
    /// <summary>
    /// Port-only: the fade scales every component toward black (by <see cref="Fade"/> / 255: a change of screen's,
    /// SPEC-menus.md's stage 1, even over its length) where the original's takes the levels off each (which takes a
    /// dark screen black in its first steps).
    /// </summary>
    bool FadeScales = false;
    /// <summary>
    /// Port-only: the colour of the window around the view (<see cref="MCDisplay::SetSurround"/>), before
    /// <see cref="Fade"/>; the presenter takes the fade off it as off the picture (<see cref="MCFadedSurround"/>).
    /// </summary>
    SDL_Color Surround{0, 0, 0, SDL_ALPHA_OPAQUE};
    /// <summary>
    /// Port-only: the frame may be shown after <see cref="MCPresenter::Present"/> returns, once a thread of the
    /// presenter's has drawn it (the game's frame loop: it presents again soon, and checks for the frame meanwhile,
    /// <see cref="MCPresenter::ShowFinished"/>). Else it is shown before Present returns.
    /// </summary>
    bool Ahead = false;
    /// <summary>
    /// Port-only: the display's count of presents at this one (<see cref="MCDisplay::Presents"/>), which a recording's
    /// frames carry (<see cref="MCWindowFrame::Number"/>).
    /// </summary>
    uint64_t Number = 0;
    /// <summary>
    /// Port-only (SPEC-menus.md): the remade menus' draws, drawn over the whole window (bars included) as the last thing
    /// before the frame is shown, at the window's pixels; none: nothing over the game's picture.
    /// </summary>
    std::shared_ptr<const MCUiDraws> Overlay;

    /// <summary>Whether the frame is composited at <see cref="OutputWidth"/> x <see cref="OutputHeight"/>.</summary>
    bool ScaledOutput() const { return OutputWidth > 0 && OutputHeight > 0; }
};

/// <summary>
/// The frame's view composited at <see cref="MCFrame::OutputWidth"/> x <see cref="MCFrame::OutputHeight"/> into
/// <paramref name="pixels"/> (palette indices, the output's width a row): each output pixel shows the screen pixel under
/// its centre; where that is the key over an underlay, the underlay's pixel under the output pixel's centre, mapped
/// through the screen pixel's op table. At the view's own size this is <see cref="MCRenderer::ComposeUnderlays"/>.
/// </summary>
void MCComposeScaled(const MCFrame& frame, std::vector<uint8_t>& pixels);

/// <summary>
/// Port-only: <paramref name="color"/> through a screen fade of <paramref name="fade"/> (<see cref="MCFrame::Fade"/>):
/// the levels taken off each component, or each scaled toward black with <paramref name="scales"/>
/// (<see cref="MCFrame::FadeScales"/>). Its alpha is kept.
/// </summary>
SDL_Color MCFadedColor(SDL_Color color, int fade, bool scales);

/// <summary>
/// Port-only: <paramref name="frame"/>'s <see cref="MCFrame::Surround"/> as shown, faded as the picture is, opaque.
/// </summary>
SDL_Color MCFadedSurround(const MCFrame& frame);

/// <summary>
/// Port-only: a frame as the window showed it, the whole window (the view where it was shown, and around it), read back
/// for a recording (<see cref="MCPresenter::RecordWindow"/>).
/// </summary>
struct MCWindowFrame
{
    /// <summary>The present it was (<see cref="MCFrame::Number"/>).</summary>
    uint64_t Number = 0;
    /// <summary>The window's size in pixels.</summary>
    int Width = 0;
    int Height = 0;
    /// <summary>Its pixels, RGBA, row after row.</summary>
    std::vector<SDL_Color> Pixels;
};

/// <summary>
/// Port-only: whether a frame shown now can reach the screen, and how long the game's loop holds each frame while the
/// screen doesn't (MCGuiSystem::Run). A frame can't reach it while its window is minimized or covered (another Space
/// shown in its place), nor, on the Mac, while a full-screen window's Space slides away after another app took the
/// front: the slide holds the window's images back, and a game that keeps running behind (a multiplayer game) waited
/// the second macOS gives a wait for one, at every switch. Such a frame isn't shown (no image is taken for it) and is
/// held to a refresh of the screen, so the game neither waits nor spins. With vsync on, a screen that stops taking the
/// frames shown also stops holding them (a compositor that has stopped: the 27 Nov hang's last 48 s ran at 2,180 frames
/// a second); each is then held to <see cref="UnheldShare"/> of a refresh, so a screen that holds them again shows by
/// a wait for an image.
/// </summary>
class MCScreenPacing
{
public:
    /// <summary>Whether a frame shown in <paramref name="window"/> now reaches the screen (see the class).</summary>
    bool Reaches(SDL_Window& window);

    /// <summary>The same for a window with <paramref name="flags"/> at <paramref name="nowNs"/> (the tests').</summary>
    bool Reaches(SDL_WindowFlags flags, uint64_t nowNs);

    /// <summary>A frame shown in <paramref name="window"/> now, its image waited for <paramref name="imageWaitMs"/>.</summary>
    void Shown(SDL_Window& window, double imageWaitMs, bool vsync);

    /// <summary>The same with the screen at <paramref name="refreshHz"/>, at <paramref name="nowNs"/> (the tests').</summary>
    void Shown(double imageWaitMs, bool vsync, double refreshHz, uint64_t nowNs);

    /// <summary>How long (ms) the game's loop holds each frame now: 0 while the screen holds them.</summary>
    double HoldMs() const;

    /// <summary>A frame shown with vsync on whose image came this soon (ms) wasn't held by the screen.</summary>
    static constexpr double UnheldImageWaitMs = 0.25;
    /// <summary>How many shown in a row, each this soon, before the loop holds them.</summary>
    static constexpr int UnheldFrames = 30;
    /// <summary>The share of a refresh each is then held to.</summary>
    static constexpr double UnheldShare = 0.8;
    /// <summary>How long (ns) after another app takes the front a full-screen window's Space may still be sliding.</summary>
    static constexpr uint64_t SpaceSlideNs = 1'500'000'000;
    /// <summary>The refresh taken where the screen's isn't known.</summary>
    static constexpr double FallbackHz = 60.0;

private:
    /// <summary>The refresh of the screen <paramref name="window"/> is on.</summary>
    static double RefreshOf(SDL_Window& window);

    bool _InFront = true;
    uint64_t _LeftFrontNs = 0;
    bool _Hidden = false;
    bool _Reached = true;
    bool _Unheld = false;
    int _SoonShown = 0;
    uint64_t _LastShownNs = 0;
    double _RefreshHz = 0.0;
};

/// <summary>
/// Shows frames in the display's window. Every call is made from the thread that created the display.
/// </summary>
class MCPresenter
{
public:
    virtual ~MCPresenter() = default;

    /// <summary>
    /// Port-only: how long (ms) the game's loop holds each frame now, the screen not holding them (MCScreenPacing); 0
    /// while it does.
    /// </summary>
    double HoldFrameMs() const { return _Pacing.HoldMs(); }

    /// <summary>
    /// Port-only: the frame being drawn as the display will show it (its view, output size and sharp screen; set by
    /// the display). A GPU renderer asks for it as a frame's drawing starts, so its draws know whether the screen they
    /// land on is drawn at the size it's shown before the frame is first presented so.
    /// </summary>
    std::function<MCFrame()> FrameLayout;

    /// <summary>The kind of renderer this presenter belongs to.</summary>
    virtual MCRendererKind Kind() const = 0;

    /// <summary>A name for logs ("vulkan", "software (direct3d11)", ...).</summary>
    virtual std::string Name() const = 0;

    /// <summary>Whether the screen's underlays are composited on the GPU (else on the CPU).</summary>
    virtual bool CompositesOnGpu() const = 0;

    /// <summary>Changes how the shown part is fitted into the window.</summary>
    virtual void SetPresentation(const MCPresentation& presentation) = 0;

    /// <summary>Turns waiting for the display's refresh on or off.</summary>
    virtual void SetVSync(bool on) = 0;

    /// <summary>Shows a frame (waiting for the display's refresh when vsync is on).</summary>
    virtual std::expected<void, std::string> Present(const MCFrame& frame) = 0;

    /// <summary>
    /// Draws the frame as <see cref="Present"/> would and reads it back as RGBA: at the screen's size, or at
    /// <see cref="MCFrame::OutputWidth"/> x <see cref="MCFrame::OutputHeight"/> when the frame is scaled (tests compare
    /// the first with the CPU's composite; screenshots take the second).
    /// </summary>
    virtual std::expected<std::vector<SDL_Color>, std::string> ReadFrame(const MCFrame& frame) = 0;

    /// <summary>
    /// The screen as shown, in palette indices with the screen's layout (screenshots, tests): the frame's pixels with
    /// the underlays composited on the CPU (<see cref="MCRenderer::ComposeUnderlays"/>), or, where the GPU draws the
    /// frame, its surfaces read back and resolved by the same rule. <see cref="MCFrame::Colors"/> isn't used.
    /// </summary>
    virtual std::expected<std::vector<uint8_t>, std::string> ReadScreen(const MCFrame& frame);

    /// <summary>
    /// Port-only: the menus' draws <paramref name="overlay"/> drawn as <see cref="Present"/> draws a frame's
    /// <see cref="MCFrame::Overlay"/>, alone, over a transparent window: RGBA, premultiplied, its window's size (tests
    /// and screenshots lay it over the frame's picture).
    /// </summary>
    virtual std::expected<std::vector<SDL_Color>, std::string> ReadOverlay(const MCUiDraws& overlay) = 0;

    /// <summary>
    /// Port-only, for the tests: how many times the menus' picture was drawn again (the software renderer keeps it while
    /// the menus' draws are the same; the GPU draws them each frame, and counts nothing).
    /// </summary>
    virtual uint64_t OverlayRedraws() const { return 0; }

    /// <summary>
    /// A frame that isn't shown (the window is minimised): a GPU renderer still runs what was drawn for it, so what it
    /// recorded doesn't pile up, and waits for the GPU to finish it, so neither does what the GPU holds for it.
    /// </summary>
    virtual std::expected<void, std::string> Discard(const MCFrame&) { return {}; }

    /// <summary>
    /// Port-only: what a GPU renderer has recorded since the last frame run now, showing nothing and taking no frame
    /// (a rehearsal's uploads: gui/MCUpdateDisplay.h's RehearseScreen), <paramref name="underlays"/> the last frame's;
    /// waited for. Nothing without one, or when nothing was recorded.
    /// </summary>
    virtual std::expected<void, std::string> RunRecorded(std::span<const MCUnderlay> /*underlays*/) { return {}; }

    /// <summary>
    /// Where a view of <paramref name="viewWidth"/> x <paramref name="viewHeight"/> lands in the window now, in
    /// window pixels (not points).
    /// </summary>
    virtual MCViewport Viewport(int viewWidth, int viewHeight) const = 0;

    /// <summary>
    /// Port-only: shows a frame presented ahead (<see cref="MCFrame::Ahead"/>) if it's drawn by now; returns at once
    /// otherwise (the game checks as it goes). Its errors come with the next <see cref="Present"/>.
    /// </summary>
    virtual void ShowFinished() {}

    /// <summary>
    /// Port-only: shows a frame presented ahead, waiting for its drawing (before the game waits for something else:
    /// the window's events, a load).
    /// </summary>
    virtual void ShowPresented() {}

    /// <summary>
    /// Port-only: shows a frame presented ahead, waiting for its drawing, then waits until the GPU has done all it was
    /// given, and lets the load's own upload slots go: a mission's load, at its end (MCMission::StartScenario).
    /// </summary>
    virtual void WaitForGpu() { ShowPresented(); }

    /// <summary>
    /// Port-only: a check before the game reads the next frame's input (the window's events). When the display paces
    /// the frames, a frame presented ahead is shown now, waiting for its drawing (as <see cref="ShowPresented"/>), so
    /// the wait for the display comes before the input is read rather than after it; else as
    /// <see cref="ShowFinished"/>.
    /// </summary>
    virtual void ShowBeforeInput() {}

    /// <summary>
    /// Port-only: the worlds drawn on a grid of their own (MCDrawnGrid) under the screen of the frame presented last,
    /// in the order they're drawn (a later one over an earlier); none without a GPU renderer's.
    /// </summary>
    virtual std::span<const MCShownGrid> ShownGrids() const { return {}; }

    /// <summary>
    /// Port-only, a tool's (mc_tests' --record-window): from now on each frame shown is also read back whole, as the
    /// window shows it, and handed to <paramref name="taken"/> on the thread that shows it (the game's) once its
    /// present is done; an empty function stops. Each read waits for the GPU, so the frames take longer than in play.
    /// False when this presenter can't.
    /// </summary>
    virtual bool RecordWindow(std::function<void(MCWindowFrame&&)> /*taken*/) { return false; }

protected:
    /// <summary>Port-only: whether the frames shown reach the screen, kept by each show (MCScreenPacing).</summary>
    MCScreenPacing _Pacing;
};

/// <summary>
/// Where a view of <paramref name="viewWidth"/> x <paramref name="viewHeight"/> lands in an output of
/// <paramref name="outputWidth"/> x <paramref name="outputHeight"/> pixels: stretched over it, or letterboxed (by
/// whole multiples with <see cref="MCPresentation::IntegerScale"/>), as SDL's logical presentation places it.
/// </summary>
MCViewport MCPresentViewport(int outputWidth, int outputHeight, int viewWidth, int viewHeight,
                             const MCPresentation& presentation);

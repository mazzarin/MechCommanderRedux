#pragma once

// The frame half of the renderer: what turns the screen the game drew (8-bit, with its op plane and underlays) into
// the picture in the window. MCDisplay owns the window, the screen buffer and the palette, and hands each frame to a
// presenter. MCVulkanPresenter composites and scales on the GPU (SDL GPU, Vulkan, HLSL shaders); MCSdlPresenter, the
// software renderer's, composites on the CPU and shows the result with SDL's 2D renderer.

#include "platform/MCRenderer.h"

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
/// Shows frames in the display's window. Every call is made from the thread that created the display.
/// </summary>
class MCPresenter
{
public:
    virtual ~MCPresenter() = default;

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
    /// A frame that isn't shown (the window is minimised): a GPU renderer still runs what was drawn for it, so what it
    /// recorded doesn't pile up.
    /// </summary>
    virtual std::expected<void, std::string> Discard(const MCFrame&) { return {}; }

    /// <summary>
    /// Where a view of <paramref name="viewWidth"/> x <paramref name="viewHeight"/> lands in the window now, in
    /// window pixels (not points).
    /// </summary>
    virtual MCViewport Viewport(int viewWidth, int viewHeight) const = 0;
};

/// <summary>
/// Where a view of <paramref name="viewWidth"/> x <paramref name="viewHeight"/> lands in an output of
/// <paramref name="outputWidth"/> x <paramref name="outputHeight"/> pixels: stretched over it, or letterboxed (by
/// whole multiples with <see cref="MCPresentation::IntegerScale"/>), as SDL's logical presentation places it.
/// </summary>
MCViewport MCPresentViewport(int outputWidth, int outputHeight, int viewWidth, int viewHeight,
                             const MCPresentation& presentation);

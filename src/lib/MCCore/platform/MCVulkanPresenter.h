#pragma once

#include "platform/MCPresenter.h"
#include "platform/MCVulkanRenderer.h"

/// <summary>
/// The Vulkan renderer's presenter: an SDL GPU device (SPIR-V, Vulkan) that owns the window's swapchain. Each frame
/// it composites the screen over its underlays in palette indices into the frame texture at the screen's size
/// (quad.vshader + composite.pshader), and blits the shown view into the swapchain, letterboxed, nearest or linear.
/// </summary>
/// <remarks>
/// <para>What it composites: when its renderer draws the frame surfaces (<see cref="MCGpuDrawing::On"/>), their
/// textures as the renderer left them, and only the palette goes up (when it changed). Otherwise (the software
/// renderer draws them, or mirror mode) the screen, its op plane, the op tables in use and the underlays' surfaces are
/// uploaded each frame: R8 (palette indices), the op tables R8 256 x 256 (row 0 the identity).</para>
/// <para>The palette is RGBA 256 x 1 and the frame texture RGBA: the palette is resolved in the composite, so palette
/// fades and cycling recolour everything, as in the original.</para>
/// </remarks>
class MCVulkanPresenter final : public MCPresenter
{
    /// <summary>Only <see cref="Create"/> makes one.</summary>
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>A presenter with no device yet.</summary>
    explicit MCVulkanPresenter(Key) {}

    /// <summary>Makes the device, claims <paramref name="window"/> and loads the shaders.</summary>
    static std::expected<std::unique_ptr<MCVulkanPresenter>, std::string> Create(std::shared_ptr<SDL_Window> window,
                                                                                 bool vsync,
                                                                                 const MCPresentation& presentation);

    ~MCVulkanPresenter() override;

    MCRendererKind Kind() const override { return MCRendererKind::Vulkan; }
    std::string Name() const override;
    bool CompositesOnGpu() const override { return true; }
    void SetPresentation(const MCPresentation& presentation) override { _Presentation = presentation; }
    void SetVSync(bool on) override;
    std::expected<void, std::string> Present(const MCFrame& frame) override;
    std::expected<std::vector<SDL_Color>, std::string> ReadFrame(const MCFrame& frame) override;
    std::expected<std::vector<uint8_t>, std::string> ReadScreen(const MCFrame& frame) override;
    std::expected<void, std::string> Discard(const MCFrame& frame) override;
    MCViewport Viewport(int viewWidth, int viewHeight) const override;

private:
    /// <summary>An R8 or RGBA texture, grown as needed (only its top-left Width x Height is in use).</summary>
    struct Texture
    {
        MCGpuHandle<SDL_GPUTexture> Handle;
        /// <summary>The allocated size.</summary>
        uint32_t Width = 0;
        uint32_t Height = 0;
        /// <summary>Its mip levels.</summary>
        uint32_t Levels = 1;
    };

    /// <summary>Makes sure <paramref name="texture"/> holds at least <paramref name="width"/> x
    /// <paramref name="height"/> (exactly that with <paramref name="exact"/>), in <paramref name="levels"/> mip
    /// levels.</summary>
    std::expected<void, std::string> Ensure(Texture& texture, SDL_GPUTextureFormat format,
                                            SDL_GPUTextureUsageFlags usage, uint32_t width, uint32_t height, bool exact,
                                            uint32_t levels = 1);
    /// <summary>Whether the frame surfaces' GPU textures are composited (the renderer draws them and the screen has
    /// one).</summary>
    bool ShowsGpuSurfaces(const MCFrame& frame) const;
    /// <summary>
    /// Tells the renderer the scale the underlays are composited at (the window's pixels per screen pixel of a
    /// scaled-up view, else 1), so the world view's surface is drawn at the resolution it is shown at.
    /// </summary>
    void PlaceUnderlays(const MCFrame& frame);
    /// <summary>
    /// Runs the renderer's recorded commands on <paramref name="commands"/> when it draws, then uploads the frame's
    /// textures in a copy pass (with <paramref name="gpuSurfaces"/>, the palette only); sets
    /// <paramref name="gpuSurfaces"/> to what the composite should read.
    /// </summary>
    /// <remarks>
    /// A frame only <paramref name="read"/> keeps the underlays where the shown frames placed them: read at the
    /// screen's size, it would otherwise place them at another scale than a scaled window's (a Retina screen's), and
    /// a surface placed at a new size is made again, empty, until the game next draws all of it.
    /// </remarks>
    std::expected<void, std::string> Prepare(SDL_GPUCommandBuffer& commands, const MCFrame& frame, bool& gpuSurfaces,
                                             bool read);
    /// <summary>Uploads the frame's textures in a copy pass on <paramref name="commands"/>.</summary>
    std::expected<void, std::string> Upload(SDL_GPUCommandBuffer& commands, const MCFrame& frame, bool gpuSurfaces);
    /// <summary>Bytes to go up into a texture's top left, and (UploadParts') where they lie in the transfer buffer.</summary>
    struct UploadPart
    {
        std::span<const std::byte> Data;
        std::reference_wrapper<SDL_GPUTexture> Texture;
        uint32_t Width;
        uint32_t Height;
        uint32_t BytesPerPixel;
        uint32_t Offset;
    };

    /// <summary>
    /// Uploads <paramref name="parts"/> (none: nothing) in a copy pass on <paramref name="commands"/>, through
    /// <paramref name="buffer"/> (<paramref name="bufferSize"/> bytes; made or grown as needed).
    /// </summary>
    std::expected<void, std::string> UploadParts(SDL_GPUCommandBuffer& commands, std::span<UploadPart> parts,
                                                 MCGpuHandle<SDL_GPUTransferBuffer>& buffer, uint32_t& bufferSize);
    /// <summary>Composites the frame into the frame texture, from the uploaded textures or the GPU's surfaces.</summary>
    void Composite(SDL_GPUCommandBuffer& commands, const MCFrame& frame, bool gpuSurfaces);
    /// <summary>
    /// Port-only (HD lighting): the bloom of underlay <paramref name="index"/>'s GPU world <paramref name="world"/>
    /// (<paramref name="width"/> x <paramref name="height"/>): its bright pass into the underlay's bloom texture
    /// (_Blooms), at half its size, and that texture's mips, recorded on <paramref name="commands"/> outside any pass.
    /// None (null) without a bloom.
    /// </summary>
    SDL_GPUTexture* Bloom(SDL_GPUCommandBuffer& commands, size_t index, SDL_GPUTexture& world, uint32_t width,
                          uint32_t height);
    static void Release(Texture& texture);

    /// <summary>The window shown in (the display's).</summary>
    std::shared_ptr<SDL_Window> _Window;
    /// <summary>The device; every GPU object holds it, so it goes last.</summary>
    std::shared_ptr<SDL_GPUDevice> _Device;
    bool _Claimed = false;
    bool _VSync = true;
    MCPresentation _Presentation;
    MCGpuHandle<SDL_GPUShader> _QuadShader;
    MCGpuHandle<SDL_GPUShader> _CompositeShader;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _CompositePipeline;
    /// <summary>
    /// Port-only (HD lighting): the bloom's bright pass (Bloom) and the GPU world's field pass, each the composite's
    /// shader built for it alone (shaders/composite_bright.pshader, composite_field.pshader).
    /// </summary>
    MCGpuHandle<SDL_GPUShader> _BrightShader;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _BrightPipeline;
    MCGpuHandle<SDL_GPUShader> _FieldShader;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _FieldPipeline;
    MCGpuHandle<SDL_GPUSampler> _Nearest;
    /// <summary>Port-only (HD lighting): linear between texels and mips, for the bloom.</summary>
    MCGpuHandle<SDL_GPUSampler> _Linear;
    Texture _Screen;
    Texture _Ops;
    Texture _Tables;
    Texture _Palette;
    /// <summary>The composited frame, RGBA at the screen's size.</summary>
    Texture _Frame;
    /// <summary>The underlays' surfaces, in the order of the frame's underlays.</summary>
    std::vector<Texture> _Underlays;
    /// <summary>Port-only (HD lighting): the underlays' blooms (Bloom), in the same order.</summary>
    std::vector<Texture> _Blooms;
    /// <summary>
    /// Port-only (HD lighting): the underlays' fields (composite.pshader's Field: the bloom and the clouds' cover at the
    /// bloom's texels), in the same order.
    /// </summary>
    std::vector<Texture> _Fields;
    /// <summary>
    /// Port-only (HD lighting): the underlays' light tiles (composite.pshader's lightTileTex), in the same order, and
    /// one bound where there are none (never read; made empty).
    /// </summary>
    std::vector<Texture> _LightTiles;
    Texture _NoLightTiles;
    /// <summary>The upload buffer, and its size; the light tiles' (uploaded after the frame's textures), and its.</summary>
    MCGpuHandle<SDL_GPUTransferBuffer> _Upload;
    uint32_t _UploadSize = 0;
    MCGpuHandle<SDL_GPUTransferBuffer> _LightTileUpload;
    uint32_t _LightTileUploadSize = 0;
    /// <summary>The renderer of the frame surfaces (the renderers refer to it while it is set as the hardware one).</summary>
    std::shared_ptr<MCVulkanRenderer> _Renderer;
};

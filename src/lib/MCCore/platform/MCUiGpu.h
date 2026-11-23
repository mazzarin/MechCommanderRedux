#pragma once

#include "platform/MCSdlHandle.h"
#include "platform/MCUiDraws.h"

/// <summary>
/// Port-only (SPEC-menus.md, "How it's built"): the remade menus' draws (platform/MCUiDraws.h) drawn by the GPU, for the
/// GPU renderer's presenter (MCVulkanPresenter): over the window's swapchain image once the frame is blitted to it, as
/// the last thing before it's shown. Each draw's triangles from a storage buffer (shaders/ui.vshader), its picture's
/// texel times its colour, both premultiplied, blended over (ONE, ONE_MINUS_SRC_ALPHA; shaders/ui_picture.pshader),
/// its scissor SDL GPU's. A 1998 picture is sampled nearest, an HD one linearly.
/// </summary>
/// <remarks>
/// Threads: the game's (the presenter shows its frames there), with the command buffer the frame is shown on.
/// </remarks>
class MCUiGpu
{
    struct Key
    {
        explicit Key() = default;
    };

public:
    explicit MCUiGpu(Key) {}
    ~MCUiGpu();
    MCUiGpu(const MCUiGpu&) = delete;
    MCUiGpu& operator=(const MCUiGpu&) = delete;

    /// <summary>The shaders and samplers for <paramref name="device"/>; why not when they can't be made.</summary>
    static std::expected<std::unique_ptr<MCUiGpu>, std::string> Create(std::shared_ptr<SDL_GPUDevice> device);

    /// <summary>
    /// Draws <paramref name="draws"/> on <paramref name="commands"/> over <paramref name="target"/> (its
    /// <paramref name="format"/>, <paramref name="width"/> x <paramref name="height"/> pixels), first cleared to
    /// transparent when <paramref name="clear"/>; the pictures it hasn't drawn before uploaded first. Their colours
    /// are scaled by <paramref name="shown"/> (the frame's fade: 1 none, 0 black), their coverage kept.
    /// </summary>
    std::expected<void, std::string> Draw(SDL_GPUCommandBuffer& commands, SDL_GPUTexture& target,
                                          SDL_GPUTextureFormat format, uint32_t width, uint32_t height,
                                          const MCUiDraws& draws, bool clear, float shown = 1.0f);

    /// <summary>The pictures' textures kept (each let go of once its picture is gone).</summary>
    size_t TexturesKept() const { return _Textures.size(); }

private:
    /// <summary>The pipeline drawing on a target of <paramref name="format"/>, made the first time.</summary>
    std::expected<std::reference_wrapper<SDL_GPUGraphicsPipeline>, std::string> PipelineFor(
        SDL_GPUTextureFormat format);
    /// <summary>
    /// A texture of the <paramref name="width"/> x <paramref name="height"/> RGBA8 <paramref name="pixels"/>, uploaded on
    /// <paramref name="pass"/>; why not when it can't be made.
    /// </summary>
    std::expected<MCGpuHandle<SDL_GPUTexture>, std::string> Upload(SDL_GPUCopyPass& pass,
                                                                   std::span<const uint8_t> pixels, uint32_t width,
                                                                   uint32_t height);
    /// <summary>The vertices' buffer, grown to hold <paramref name="bytes"/>, and their transfer buffer filled.</summary>
    std::expected<void, std::string> FillVertices(SDL_GPUCopyPass& pass, std::span<const std::byte> bytes);

    struct MCKept
    {
        std::weak_ptr<const MCUiTexture> Picture;
        MCGpuHandle<SDL_GPUTexture> Texture;
    };

    std::shared_ptr<SDL_GPUDevice> _Device;
    MCGpuHandle<SDL_GPUShader> _Vertex;
    MCGpuHandle<SDL_GPUShader> _Fragment;
    MCGpuHandle<SDL_GPUSampler> _Nearest;
    MCGpuHandle<SDL_GPUSampler> _Linear;
    std::map<SDL_GPUTextureFormat, MCGpuHandle<SDL_GPUGraphicsPipeline>> _Pipelines;
    /// <summary>A white texel: an untextured draw samples it.</summary>
    MCGpuHandle<SDL_GPUTexture> _White;
    std::unordered_map<uint64_t, MCKept> _Textures;
    MCGpuHandle<SDL_GPUBuffer> _Vertices;
    MCGpuHandle<SDL_GPUTransferBuffer> _VertexTransfer;
    uint32_t _VertexCapacity = 0;
};

#pragma once

#include "platform/MCSdlHandle.h"

/// <summary>The Vulkan renderer's built-in shaders.</summary>
enum class MCShader : uint8_t
{
    Quad,
    Composite,
    Instance,
    Draw,
    Terrain,
    /// <summary>Port-only: the draws of the original's own kinds alone (shaders/draw_basic.pshader).</summary>
    DrawBasic,
    /// <summary>Port-only: the terrain layer's draws alone (shaders/draw_terrain.pshader).</summary>
    DrawTerrain,
    /// <summary>Port-only: the draws of HD art frames alone (shaders/draw_hd.pshader).</summary>
    DrawHd,
    /// <summary>Port-only: the kept ground's copy (shaders/ground.pshader).</summary>
    Ground,
    /// <summary>Port-only: the bloom's bright pass alone (shaders/composite_bright.pshader).</summary>
    CompositeBright,
    /// <summary>Port-only: the GPU world's field pass alone (shaders/composite_field.pshader).</summary>
    CompositeField,
    /// <summary>Port-only: the composite without rain (shaders/composite_dry.pshader).</summary>
    CompositeDry
};

/// <summary>A shader the Vulkan renderer uses, and the resources its bindings declare.</summary>
struct MCShaderInfo
{
    /// <summary>Which built-in shader: its name and SPIR-V (see <see cref="MCVulkanShaders::Name"/>).</summary>
    MCShader Shader;
    SDL_GPUShaderStage Stage;
    uint32_t Samplers;
    uint32_t UniformBuffers;
    uint32_t StorageBuffers = 0;
};

/// <summary>
/// Loads the Vulkan renderer's shaders. A compiled SPIR-V file named after a built-in shader replaces it: the
/// <c>shaders</c> folder beside the executable is looked in first, then the one in the game's data folder. A
/// replacement must keep the bindings the built-in's source documents.
/// </summary>
namespace MCVulkanShaders
{
#if defined(__APPLE__)
    /// <summary>
    /// The GPU driver and the built-in shaders' format. macOS has no Vulkan driver, so the GPU renderer runs on SDL
    /// GPU's Metal one, with the same shaders compiled to MSL (src/mac/tools/mc_msl.cpp).
    /// </summary>
    inline constexpr const char* Driver = "metal";
    inline constexpr SDL_GPUShaderFormat Format = SDL_GPU_SHADERFORMAT_MSL;
    /// <summary>The entry point: SPIRV-Cross renames the source's <c>main</c>, a reserved name in MSL.</summary>
    inline constexpr const char* EntryPoint = "main0";
#else
    /// <summary>The GPU driver and the built-in shaders' format.</summary>
    inline constexpr const char* Driver = "vulkan";
    inline constexpr SDL_GPUShaderFormat Format = SDL_GPU_SHADERFORMAT_SPIRV;
    /// <summary>The entry point.</summary>
    inline constexpr const char* EntryPoint = "main";
#endif

    /// <summary>The built-in shaders.</summary>
    extern const MCShaderInfo Quad;
    extern const MCShaderInfo Composite;
    extern const MCShaderInfo CompositeBright;
    extern const MCShaderInfo CompositeField;
    extern const MCShaderInfo CompositeDry;
    extern const MCShaderInfo DrawVertex;
    extern const MCShaderInfo DrawFragment;
    extern const MCShaderInfo DrawBasicFragment;
    extern const MCShaderInfo DrawTerrainFragment;
    extern const MCShaderInfo DrawHdFragment;
    extern const MCShaderInfo GroundFragment;
    extern const MCShaderInfo TerrainVertex;

    /// <summary>
    /// The shader's name: its source is <c>shaders/&lt;name&gt;.vshader</c> or <c>.pshader</c>, and a replacement is
    /// <c>&lt;name&gt;.spv</c>.
    /// </summary>
    std::string_view Name(MCShader shader);

    /// <summary>
    /// The built-in code in <see cref="Format"/>, embedded in the executable: SPIR-V compiled from the source by dxc
    /// at build time, or on macOS the MSL the Mac build makes from it.
    /// </summary>
    std::span<const unsigned char> BuiltInCode(MCShader shader);

    /// <summary>The folders searched for replacements, in order.</summary>
    std::vector<std::filesystem::path> SearchFolders();

    /// <summary>
    /// Creates <paramref name="info"/>'s shader on <paramref name="device"/>, from a replacement if there is one; the
    /// shader holds the device.
    /// </summary>
    std::expected<MCGpuHandle<SDL_GPUShader>, std::string> Load(std::shared_ptr<SDL_GPUDevice> device,
                                                                const MCShaderInfo& info);
}

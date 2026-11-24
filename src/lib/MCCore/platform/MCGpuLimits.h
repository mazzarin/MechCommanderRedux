#pragma once

/// <summary>
/// Port-only (SPEC-menus.md, "How it's built"): what SDL's GPU device can hold that SDL's GPU API doesn't say, for the
/// pictures the game makes at sizes of its own (the remade menus' art, ui/MCRmlArt).
/// </summary>
namespace MCGpuLimits
{
    /// <summary>
    /// The longest side of a 2D texture every device of a GPU driver holds: Metal's (SDL's GPU needs an Apple silicon or
    /// Mac2-family GPU, or an A9 or newer: 16384 on each), Direct3D 12's (feature level 11_0), and Vulkan's (its
    /// specification's least <c>maxImageDimension2D</c>, which SDL doesn't check).
    /// </summary>
    inline constexpr int32_t MetalLeast = 16384;
    inline constexpr int32_t Direct3D12Least = 16384;
    inline constexpr int32_t VulkanLeast = 4096;

    /// <summary>
    /// The longest side of a 2D texture <paramref name="device"/> holds: its driver's least for Metal and Direct3D 12;
    /// on Vulkan the device's own (<see cref="VulkanLargestSide"/>), or Vulkan's least when Vulkan can't say; logged.
    /// </summary>
    int32_t LargestTextureSide(SDL_GPUDevice& device);

    /// <summary>
    /// The longest side of a 2D texture (<c>maxImageDimension2D</c>) of the Vulkan device <paramref name="name"/> (as
    /// SDL's GPU names its device: SDL doesn't hand out its device, so it's found again, as MCRedux's MCMemorySystemWin
    /// finds it), the least when several have the name; read through an instance of the game's own on SDL's loader, with
    /// portability drivers listed as SDL lists them. Why not when Vulkan can't say.
    /// </summary>
    std::expected<int32_t, std::string> VulkanLargestSide(std::string_view name);
}

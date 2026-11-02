#pragma once

#include <array>
#include <cstdint>
#include <optional>

/// <summary>
/// Port-only (HD art): a frame drawn turned in the window's plane (a blown-off arm falling: MCDebris), its HD art pack
/// frame drawn through the turn by the GPU (nothing without one).
/// </summary>
struct MCSpriteTurn
{
    /// <summary>How far it is turned, clockwise on the window, in radians.</summary>
    float Radians = 0.0f;
    /// <summary>The point it turns about, in window pixels from the frame's hot spot (where it is drawn).</summary>
    float PivotX = 0.0f;
    float PivotY = 0.0f;
    /// <summary>
    /// How far above the ground under it the frame is drawn, in window pixels: its sun shadow falls that much farther
    /// along the sun's way (MCLighting::SunShadowRight and SunShadowUp), as the turned frame laid flat.
    /// </summary>
    float Lift = 0.0f;
    /// <summary>
    /// The part of the frame drawn, its original pixels from its first column and row (left, top, right, bottom,
    /// inclusive; 0 to 254); the whole frame when none.
    /// </summary>
    std::optional<std::array<uint8_t, 4>> Piece;
};

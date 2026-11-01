#pragma once

/// <summary>
/// Port-only (HD art): the frame rate the HD art pack's in-betweens bring an effect's animation up to, frames a second
/// (tools/upscale/make_object_pack.py's IN_BETWEEN_RATE).
/// </summary>
inline constexpr float InBetweenRate = 60.0f;

/// <summary>The most steps the pack divides a frame into (tools/upscale/make_object_pack.py's MAX_IN_BETWEEN_STEPS).</summary>
inline constexpr int32_t MaxInBetweenSteps = 12;

/// <summary>
/// The steps the HD art pack divides each frame of an animation played at <paramref name="frameRate"/> frames a second
/// into: the frame, then an in-between for each further step (1, none, at <see cref="InBetweenRate"/> or faster).
/// </summary>
inline int32_t InBetweenSteps(float frameRate)
{
    if (!(frameRate > 0.0f))
    {
        return 1;
    }

    return std::clamp(static_cast<int32_t>(std::ceil(InBetweenRate / frameRate)), 1, MaxInBetweenSteps);
}

/// <summary>
/// Port-only (HD art): an effect's frame drawn part of the way toward the next one its animation shows, as the HD art
/// pack's in-between <see cref="Step"/> (1 to <see cref="InBetweenSteps"/> - 1) of frame <see cref="Frame"/> toward
/// frame <see cref="Next"/> (the frame after it, or, once the animation has looped, the first after the last).
/// </summary>
struct MCInBetween
{
    int32_t Frame = 0;
    int32_t Next = 0;
    int32_t Step = 0;
};

/// <summary>
/// The in-between to draw for frame <paramref name="frame"/> of an animation played at <paramref name="frameRate"/>,
/// <paramref name="share"/> (0 to 1) of the way through the frame toward <paramref name="next"/>: none in the frame's
/// first step.
/// </summary>
inline std::optional<MCInBetween> InBetweenOf(int32_t frame, int32_t next, float share, float frameRate)
{
    const int32_t steps = InBetweenSteps(frameRate);
    const int32_t step = std::clamp(static_cast<int32_t>(std::floor(share * static_cast<float>(steps))), 0, steps - 1);

    if (step == 0)
    {
        return std::nullopt;
    }

    return MCInBetween{frame, next, step};
}

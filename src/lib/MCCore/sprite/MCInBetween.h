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
/// Port-only (HDTurns): the steps that name a mech's legs frame's turn middles in the HD art pack, frames halfway to a
/// neighbouring stored facing (tools/upscale/make_turn_middles.py, MCSpriteTree::TurnMiddle): <c>&lt;frame&gt;_turn</c>
/// halfway to the next facing (the last facing's: to its own mirror, straight back), and <c>&lt;frame&gt;_turnm</c> (the
/// first facing's only) halfway to the next facing mirrored. Above any step an effect's in-between has.
/// </summary>
inline constexpr int32_t TurnStep = 0x100;
inline constexpr int32_t TurnMirroredStep = 0x101;

/// <summary>
/// Port-only (HD art): the steps the HD art pack divides each frame of a mech's walk into toward the next frame its
/// gesture shows, the art's 15 poses a second brought to 60 (tools/upscale/make_walk_betweens.py's STEPS): the frame,
/// then <c>&lt;frame&gt;_&lt;step&gt;</c> for each further step, and over a turn middle
/// <c>&lt;frame&gt;_turn_&lt;step&gt;</c> (<see cref="TurnBetweenStep"/>).
/// </summary>
inline constexpr int32_t LegsInBetweenSteps = 4;

/// <summary>Where a turn middle's in-between keeps its step, above the turn middle's own (<see cref="TurnStep"/>).</summary>
inline constexpr int32_t TurnBetweenShift = 4;

/// <summary>
/// Port-only (HD art): the step naming in-between <paramref name="step"/> of a legs frame's turn middle
/// <paramref name="turnStep"/> (<see cref="TurnStep"/> or <see cref="TurnMirroredStep"/>) toward the next frame's.
/// </summary>
inline constexpr int32_t TurnBetweenStep(int32_t turnStep, int32_t step)
{
    return turnStep | step << TurnBetweenShift;
}

/// <summary>
/// Port-only (HDTurns): where a turn middle lies round the frame it is drawn over: the frame's first column and row
/// within the middle's bounds, and the bounds' size (original pixels; the frame's and its neighbour's together).
/// </summary>
struct MCInBetweenBounds
{
    int32_t FrameX = 0;
    int32_t FrameY = 0;
    int32_t Width = 0;
    int32_t Height = 0;
};

/// <summary>
/// Port-only (HD art): an effect's frame drawn part of the way toward the next one its animation shows, as the HD art
/// pack's in-between <see cref="Step"/> (1 to <see cref="InBetweenSteps"/> - 1) of frame <see cref="Frame"/> toward
/// frame <see cref="Next"/> (the frame after it, or, once the animation has looped, the first after the last). Or
/// (HDTurns) a mech's legs frame drawn as its turn middle: <see cref="Step"/> a turn step, <see cref="Next"/> the frame
/// itself, and <see cref="Bounds"/> where the middle lies (an effect's in-between lies over the two frames' bounds).
/// </summary>
struct MCInBetween
{
    int32_t Frame = 0;
    int32_t Next = 0;
    int32_t Step = 0;
    std::optional<MCInBetweenBounds> Bounds;
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

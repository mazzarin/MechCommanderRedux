// Port-only: the GPU's channels, what each value means in the targets and pictures that draws share, written once for
// the shaders (HLSL, which includes it as is) and the C++ that writes, reads or tests them (namespace MCGpuChannels).
// Every draw that writes a channel and every one that reads it takes its values from here; GpuChannelsTests checks the
// layout (no value means two things, each mark lies in its readers' windows).
//
// A surface's targets (MCVulkanRenderer's Surface; SV_Target0 to 2 in draw.pshader, 3 in its stands build):
// - 0, its colours: RGBA8, the picture as shown.
// - 1, its indices (Surface::Index): RGBA8.
//   - R: the original's palette index at the pixel.
//   - A: what the pixel is, as the marks below. G and B by A: over GroundMark, the HD ground's slopes in their high 7
//     bits (ReliefFlat flat) and the fog of war's edge in their low bits (G's the low bit of the count, B's the high);
//     at or under it, the window row a unit's feet stand on, FeetSteps a row and 1 more (G the high byte, B the low;
//     0 for none).
// - 2, its glints (Surface::Glints, HD weather): premultiplied RGBA8, only on passes whose draws catch light.
// - Port-only, only in a frame whose draws on the surface fold a forest's index pass into its HD frame's draw
//   (Surface::Folds; draw.pshader's stands build, DRAW_STANDS): its stands (Surface::Stands) at 2, the glints then at 3.
//   R8: the mark of what stands at the pixel, as the index target's alpha holds marks (below), or NoStand (255, as the
//   target is cleared, and the kept ground's): the index target's alpha holds it. Every draw writes the mark it writes
//   in that alpha here too (the pipelines' masks), but a fold, which writes it here alone, leaving the alpha the
//   ground's under its tree; MarkOf reads the two as one, the mark a frame without the stands holds in that alpha.
//
// The index target's alpha, by its byte (draw.pshader writes it; composite.pshader reads it, each reader by a window):
// - 0, StandingMark: something standing on the ground (a sprite, an HD frame's coverage past its original pixels).
//   Read as at or under GroundMark, under CanopyMark, and (port-only: the sun's relief, IsStanding) under
//   StandingMarkTop.
// - 2, TerrainMark: ground without a relief map (the original's tiles, an HD tile without one: the terrain shader's
//   draws) and what is laid on it (a terrain overlay: rocks, roads, rubble; a crater, a footprint: draw.pshader's
//   LaidOnGround), from draw.pshader's main. Read as at or under GroundMark, and under CanopyMark.
// - 8, 16 and 24, FoggedWaterMark for each of a water tile's corners the fog of war leaves seen (water without a relief
//   map). Read under CanopyMark, as round(a / FoggedWaterMark) up to MostFogEdge.
// - 64, SwayMark: a forest's canopy. Read two ways: within SwayMarkNear of it (IsCanopy: 49 to 79), and over CanopyMark
//   and at or under GroundMark (the snow's and the rain's: 32 to 127).
// - 133 to 184: a print under the snow, FilledMarkFrom + FilledMarkSpan for how far it has filled in (flat slopes).
//   Read over GroundMark and under FilledMarkTop (187).
// - 191 to 255: HD ground, 1 - BlockedMarkSpan for the share of it that can't be entered. Read over GroundMark, and
//   at or over FilledMarkTop.
//
// An HD art frame's index picture (MCHdArt::Frame::Indices, R8G8): R the original's index; G its marks (HasIndex and
// the values after it, below; 0 none), read as bytes through the predicates below.
//
// The surfaces' kept ground (MCVulkanRenderer's KeptGround) keeps a colours and an indices target as a surface's, and
// port-only (SPEC-pan-water.md) its weights (KeptGround::Weights), written in target slot 2 of the kept ground's pass,
// the slot a surface's own passes put its glints in (the two never meet in one pass): RGBA8, premultiplied. RGB the
// share of the colour that follows the cycled entry the kept index's red shows (the water's colour cycle:
// FirstWaterColor and the seven after it), a channel at a time, as the colour it was made from: the colour is that
// share times the entry's colour over the HD pack's colour of the entry (at least MadeWithFloor), no more than
// MaxCorrection times (draw.pshader's Corrected). So an HD tile's own index holds its HD colour; another index its HD
// colour's brightness on the entry's pack colour (the haze's tables); an original tile the entry's pack colour. A the
// colour's coverage, which the seam pass's blend takes as it takes the colours'. 0 where no cycled entry shows.
// ground.pshader's Over takes the share from the entry's colour at the kept ground's step to its colour now, clipped as
// the target clips, so a step of the cycle draws none of the kept ground again. Which entry the share follows is in its
// stands (KeptGround::Slots, the kept ground's pass's stands target: R8, the entry's index): the seam pass's
// (draw.pshader's TerrainSeamPixel), which draws the colour over the first pass's and may take a neighbouring pixel's
// index, so not always the kept index's red.
#ifndef MC_GPU_CHANNELS_HLSLI
#define MC_GPU_CHANNELS_HLSLI

#ifdef __cplusplus
#include <cmath>
#include <cstdint>

#define MC_CHANNEL inline constexpr
#define MC_CHANNEL_FUNCTION constexpr
#define MC_CHANNEL_DECODE inline

namespace MCGpuChannels
{
    using uint = uint32_t;
    using std::round;
#else
#define MC_CHANNEL static const
#define MC_CHANNEL_FUNCTION
#define MC_CHANNEL_DECODE
#endif

// A UNORM8 channel's byte, as written (k / 255) and read.
MC_CHANNEL_DECODE uint Byte(float value)
{
    return (uint)round(value * 255.0f);
}

MC_CHANNEL uint BitsPerByte = 8;

// The original's palette entries the shaders know: the water's blues and its colour-cycled entries
// (color/MCWaterCycle.h: FirstWaterColor and the seven after it), which no land tile has; the blank index, and the
// black a tile no one has seen is filled with.
MC_CHANNEL uint FirstWaterBlue = 0x58;
MC_CHANNEL uint WaterBlues = 6;
MC_CHANNEL uint FirstWaterColor = 0xd8;
MC_CHANNEL uint WaterColors = 8;
MC_CHANNEL uint BlankIndex = 0;
MC_CHANNEL uint UnseenIndex = 0x10;

// The most draw.pshader's Corrected scales an HD colour's channel by where today's colour differs from the pack's, and
// the least pack colour it divides by: the kept ground's lay (ground.pshader) scales its weights (above) as it does.
MC_CHANNEL float MaxCorrection = 4.0f;
MC_CHANNEL float MadeWithFloor = 1.0f / 255.0f;

// The palette texture's rows (MCVulkanRenderer's), 256 wide: the colours shown (gamma applied), the colours the HD art
// pack was made from; port-only, the colours before the game's brightness (PREFS Brightness) with how many times it
// brightened each in A (a byte, 0 to MaxBrightenings), and its curve, a value a column, brightened once (R), twice (G)
// and three times (B). An HD colour is corrected before the brightness, then brightened by its own lightness
// (BrightenedThrough), as the original's palette is: following a near-black entry's change, several times over, a
// lighter HD colour would glow.
MC_CHANNEL uint ShownRow = 0;
MC_CHANNEL uint MadeWithRow = 1;
MC_CHANNEL uint BeforeRow = 2;
MC_CHANNEL uint BrighteningRow = 3;
MC_CHANNEL uint PaletteRows = 4;
MC_CHANNEL uint MaxBrightenings = 3;

// The index target's alpha (the table above): its readers' thresholds, then the marks.
MC_CHANNEL float GroundMark = 0.5f;
MC_CHANNEL float CanopyMark = 0.125f;
MC_CHANNEL float StandingMark = 0.0f;
MC_CHANNEL float StandingMarkTop = 1.0f / 255.0f;
MC_CHANNEL float TerrainMark = 2.0f / 255.0f;
MC_CHANNEL float FoggedWaterMark = 8.0f / 255.0f;
MC_CHANNEL uint MostFogEdge = 3;
MC_CHANNEL float SwayMark = 64.0f / 255.0f;
MC_CHANNEL float SwayMarkNear = 16.0f / 255.0f;
MC_CHANNEL float FilledMarkFrom = 0.52f;
MC_CHANNEL float FilledMarkSpan = 0.2f;
MC_CHANNEL float FilledMarkTop = 0.735f;
MC_CHANNEL float BlockedMarkSpan = 0.25f;

// The stands target (with a fold, target 2): nothing stands, the mark is the index target's alpha's (its byte 255: a
// draw's mark of 255 is written in both, so either reads it).
MC_CHANNEL float NoStand = 1.0f;

// The mark at a pixel, from the index target's alpha <paramref name="alpha"/> and the stands target's
// <paramref name="stands"/> (NoStand without one): what stands there, else the alpha's.
MC_CHANNEL_DECODE float MarkOf(float alpha, float stands)
{
    return Byte(stands) != Byte(NoStand) ? stands : alpha;
}

// The index target's green and blue: a relief map's byte for flat ground (a byte stands for a rise of (byte - this) /
// 127 of a pixel); a unit's feet, carried in its draw's op.y from FeetShift.
MC_CHANNEL float ReliefFlat = 128.0f;
MC_CHANNEL uint FeetShift = 16;
MC_CHANNEL float FeetSteps = 4.0f;

// An HD frame's index picture's green: 255 a pixel with an index, 254 one that is also part of an object's baked shadow
// (MCHdArt::MarkBakedShadows, or the pack's marks: a forest's). A version 2 frame also has 253 and 252, its index
// spread past the original's pixels (outside a baked shadow and in one), and 251 and 250, drawn and blended (outside
// one and in one). 249 is a pixel without an index that the frame covers (its alpha at the pixel's centre over
// HdMaskAlpha: an in-between's edge), which MCVulkanRenderer::MarkHdCoverage marks in a version 1 frame on the GPU,
// with the spread marks: SpreadStep less where the frame doesn't cover a pixel, and at least CoveredNoIndex where it
// does.
MC_CHANNEL uint HasIndex = 255u;
MC_CHANNEL uint BakedShadow = 254u;
MC_CHANNEL uint Spread = 253u;
MC_CHANNEL uint SpreadShadow = 252u;
MC_CHANNEL uint DrawnBlended = 251u;
MC_CHANNEL uint DrawnBlendedShadow = 250u;
MC_CHANNEL uint CoveredNoIndex = 249u;
MC_CHANNEL float HdMaskAlpha = 0.5f;
MC_CHANNEL float SpreadStep = float(HasIndex - Spread) / 255.0f;
MC_CHANNEL float CoveredNoIndexGreen = float(CoveredNoIndex) / 255.0f;

// Whether a green gives its pixel an index: the original's red.
MC_CHANNEL_FUNCTION bool HasIndexGreen(uint green)
{
    return green >= DrawnBlendedShadow;
}

// Whether a green puts its pixel in a baked shadow.
MC_CHANNEL_FUNCTION bool InShadowGreen(uint green)
{
    return green == BakedShadow || green == SpreadShadow || green == DrawnBlendedShadow;
}

// Whether a version 2 frame's green is a pixel the original draws (drawn, or drawn and blended; in a baked shadow or
// not), not one its index only spread to. A version 1 frame's green doesn't say.
MC_CHANNEL_FUNCTION bool OriginalDrawsGreen(uint green)
{
    return green == HasIndex || green == BakedShadow || green == DrawnBlended || green == DrawnBlendedShadow;
}

// Whether a frame covers an original pixel (KindHdSmooth's mask), once MarkHdCoverage has marked a version 1 frame:
// drawn, in a baked shadow or not, or covered without an index; not spread to, and not drawn and blended (those lie
// outside a translucent frame's coverage).
MC_CHANNEL_FUNCTION bool CoversGreen(uint green)
{
    return green == HasIndex || green == BakedShadow || green == CoveredNoIndex;
}

// Whether a frame's pixel is recoloured by its index (today's colour of it). In a frame holding its blended pixels
// (HdLight: an effect's) only 255 and 254 are: a version 2 frame's drawn and blended pixel keeps the original's index
// into the blend's table (AlphaPal.ini), not a colour, and its spread is from those.
MC_CHANNEL_FUNCTION bool RecolouringGreen(uint green, bool light)
{
    return light ? green >= BakedShadow : HasIndexGreen(green);
}

#ifdef __cplusplus
} // namespace MCGpuChannels
#else
// Port-only: premultiplied colour `color` of coverage `a` brightened `times` times by the game's brightness's curve (the
// palette texture `palette`'s BrighteningRow, read with `samp`), a channel at a time, between the curve's values.
float3 BrightenedThrough(Texture2D<float4> palette, SamplerState samp, float3 color, float a, uint times)
{
    if (times == 0u || a <= 0.0)
    {
        return color;
    }

    uint width;
    uint height;
    palette.GetDimensions(width, height);
    const uint channel = min(times, MaxBrightenings) - 1u;
    const float3 at = saturate(color / a) * 255.0;
    const int3 low = int3(floor(at));
    const int3 high = min(low + 1, 255);
    const float3 f = at - float3(low);
    float3 brightened = float3(0.0, 0.0, 0.0);

    [unroll] for (int c = 0; c < 3; c++)
    {
        const float row = ((float)BrighteningRow + 0.5) / (float)height;
        const float from = palette.SampleLevel(samp, float2(((float)low[c] + 0.5) / (float)width, row), 0)[channel];
        const float to = palette.SampleLevel(samp, float2(((float)high[c] + 0.5) / (float)width, row), 0)[channel];
        brightened[c] = lerp(from, to, f[c]);
    }

    return brightened * a;
}
#endif

#undef MC_CHANNEL
#undef MC_CHANNEL_FUNCTION
#undef MC_CHANNEL_DECODE

#endif

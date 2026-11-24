#include "stdafx.h"
#include "MCTest.h"
#include "color/MCWaterCycle.h"
#include "shaders/channels.hlsli"
#include "sprite/MCHdArt.h"

// Port-only: the GPU's channels (shaders/channels.hlsli), the values the draws that share a target or a picture agree
// on: each means one thing, and each mark a draw writes lies in the windows its readers read it by.

namespace
{
    namespace Gc = MCGpuChannels;

    /// <summary>The bytes a UNORM8 channel holds.</summary>
    constexpr uint32_t Bytes = 256;
    /// <summary>A UNORM8 channel's largest value, as a float.</summary>
    constexpr float UnormMost = 255.0f;
    /// <summary>The most a print under the snow has filled in (draw.pshader's GroundFillMost).</summary>
    constexpr uint32_t FillSteps = 63;
    /// <summary>The fog of war's most seen corners counted at a water texel (composite.pshader's FoggedWaterAt).</summary>
    constexpr uint32_t FogEdges = 3;

    /// <summary>The byte a UNORM8 target keeps for <paramref name="value"/> (round to nearest).</summary>
    constexpr uint32_t Written(float value)
    {
        return static_cast<uint32_t>(value * UnormMost + 0.5f);
    }

    /// <summary>What a shader reads for byte <paramref name="byte"/>.</summary>
    constexpr float Read(uint32_t byte)
    {
        return static_cast<float>(byte) / UnormMost;
    }

    // An HD frame's index picture's green: each mark its own, none 0 (no mark).
    constexpr std::array GreenMarks = {Gc::HasIndex,     Gc::BakedShadow,        Gc::Spread,        Gc::SpreadShadow,
                                       Gc::DrawnBlended, Gc::DrawnBlendedShadow, Gc::CoveredNoIndex};

    constexpr bool AllDistinctAndMarked()
    {
        for (size_t i = 0; i < GreenMarks.size(); i++)
        {
            if (GreenMarks[i] == 0 || GreenMarks[i] >= Bytes)
            {
                return false;
            }

            for (size_t j = i + 1; j < GreenMarks.size(); j++)
            {
                if (GreenMarks[i] == GreenMarks[j])
                {
                    return false;
                }
            }
        }

        return true;
    }

    static_assert(AllDistinctAndMarked());
    // The coverage marks (MarkHdCoverage) take a drawn pixel to its spread mark by one step, in a shadow or not, and its
    // step is a whole byte.
    static_assert(Gc::HasIndex - Gc::Spread == Gc::BakedShadow - Gc::SpreadShadow);
    static_assert(Written(Gc::SpreadStep) == Gc::HasIndex - Gc::Spread);
    static_assert(Written(Gc::CoveredNoIndexGreen) == Gc::CoveredNoIndex);
    // A unit's feet fill the bits above FeetShift, two bytes (the index target's green and blue).
    static_assert(Gc::FeetShift + 2 * Gc::BitsPerByte == 32);
    // The CPU's copies are the header's.
    static_assert(Gc::FirstWaterColor == FirstWaterColor);
    static_assert(MCHdArt::ReliefFlat == static_cast<int32_t>(Gc::ReliefFlat));

    /// <summary>The index target's alpha as composite.pshader reads it (its comparisons, by the header's values).</summary>
    struct AlphaRead
    {
        bool Ground;
        bool Canopy;
        bool OverCanopy;
        bool Print;
        uint32_t FoggedWater;
        bool Standing;
    };

    AlphaRead ReadAlpha(uint32_t byte)
    {
        const float a = Read(byte);
        const bool ground = a > Gc::GroundMark;
        const auto edges = static_cast<uint32_t>(std::round(a / Gc::FoggedWaterMark));
        return {
            .Ground = ground,
            .Canopy = std::abs(a - Gc::SwayMark) < Gc::SwayMarkNear,
            .OverCanopy = !ground && a > Gc::CanopyMark,
            .Print = ground && a < Gc::FilledMarkTop,
            .FoggedWater = !ground && a < Gc::CanopyMark ? std::min(edges, Gc::MostFogEdge) : 0u,
            .Standing = a < Gc::StandingMarkTop,
        };
    }

    /// <summary>Whether <paramref name="green"/> is one of <paramref name="set"/> (the layout's table, written out).</summary>
    bool in(std::initializer_list<uint32_t> set, uint32_t green)
    {
        return std::find(set.begin(), set.end(), green) != set.end();
    }

    /// <summary>A version 1 frame's green after MarkHdCoverage's pass where the frame doesn't cover the pixel.</summary>
    uint32_t MarkedUncovered(uint32_t green)
    {
        return green - std::min(green, Gc::HasIndex - Gc::Spread);
    }

    /// <summary>The same where it does.</summary>
    uint32_t MarkedCovered(uint32_t green)
    {
        return std::max(green, Gc::CoveredNoIndex);
    }
}

TEST_CASE("gpu channels: every green of an HD frame's index picture means what the layout says, as the shaders read it")
{
    for (uint32_t green = 0; green < Bytes; green++)
    {
        const auto mark = static_cast<uint8_t>(green);
        CHECK_EQ(Gc::HasIndexGreen(green), green >= 250);
        CHECK_EQ(Gc::InShadowGreen(green), in({254, 252, 250}, green));
        CHECK_EQ(Gc::OriginalDrawsGreen(green), in({255, 254, 251, 250}, green));
        CHECK_EQ(Gc::CoversGreen(green), in({255, 254, 249}, green));
        CHECK_EQ(Gc::DrawnThroughAlphaGreen(green), in({251, 250}, green));
        CHECK_EQ(Gc::RecolouringGreen(green, true), in({255, 254}, green));
        CHECK_EQ(Gc::RecolouringGreen(green, false), green >= 250);
        CHECK_EQ(MCHdArt::HasIndexMark(mark), Gc::HasIndexGreen(green));
        CHECK_EQ(MCHdArt::InShadowMark(mark), Gc::InShadowGreen(green));
        CHECK_EQ(MCHdArt::OriginalDrawsMark(mark), Gc::OriginalDrawsGreen(green));
        CHECK_EQ(Gc::Byte(Read(green)), green);
    }
}

TEST_CASE("gpu channels: the coverage marks take a version 1 frame's greens to the mask once, and not twice")
{
    // A version 1 frame's greens: none, drawn, drawn in a baked shadow.
    for (const uint32_t green : {0u, Gc::HasIndex, Gc::BakedShadow})
    {
        const uint32_t uncovered = MarkedUncovered(green);
        const uint32_t covered = MarkedCovered(green);
        CHECK(!Gc::CoversGreen(uncovered));
        CHECK(Gc::CoversGreen(covered));
        // The marks keep what the frame's index says: whether it has one, and whether it's in a shadow.
        CHECK_EQ(Gc::HasIndexGreen(uncovered), Gc::HasIndexGreen(green));
        CHECK_EQ(Gc::InShadowGreen(uncovered), Gc::InShadowGreen(green));
        CHECK_EQ(Gc::HasIndexGreen(covered), Gc::HasIndexGreen(green));
        CHECK_EQ(Gc::InShadowGreen(covered), Gc::InShadowGreen(green));

        // A second pass would take a spread pixel to one the original draws and blends: the pass runs once an upload.
        if (green == Gc::HasIndex)
        {
            CHECK_EQ(MarkedUncovered(uncovered), Gc::DrawnBlended);
        }
    }
}

TEST_CASE("gpu channels: each mark in the index target's alpha reads as itself and nothing else")
{
    // What stands on the ground, and ground without a relief map: alike but for whether something stands there.
    const AlphaRead standing = ReadAlpha(Written(Gc::StandingMark));
    CHECK(!standing.Ground && !standing.Canopy && !standing.OverCanopy && !standing.Print && standing.Standing);
    CHECK_EQ(standing.FoggedWater, 0u);
    const AlphaRead terrain = ReadAlpha(Written(Gc::TerrainMark));
    CHECK(!terrain.Ground && !terrain.Canopy && !terrain.OverCanopy && !terrain.Print && !terrain.Standing);
    CHECK_EQ(terrain.FoggedWater, 0u);

    // Water at the fog of war's edge, a mark for each corner the fog leaves seen.
    for (uint32_t edges = 1; edges <= FogEdges; edges++)
    {
        const AlphaRead water = ReadAlpha(Written(Gc::FoggedWaterMark * static_cast<float>(edges)));
        CHECK(!water.Ground && !water.Canopy && !water.OverCanopy && !water.Print && !water.Standing);
        CHECK_EQ(water.FoggedWater, edges);
    }

    // A forest's canopy, read both ways.
    const AlphaRead canopy = ReadAlpha(Written(Gc::SwayMark));
    CHECK(!canopy.Ground && canopy.Canopy && canopy.OverCanopy && !canopy.Print && !canopy.Standing);
    CHECK_EQ(canopy.FoggedWater, 0u);

    // A print under the snow, filled in step by step, and how far it reads.
    for (uint32_t step = 0; step <= FillSteps; step++)
    {
        const float filled = static_cast<float>(step) / static_cast<float>(FillSteps);
        const uint32_t byte = Written(Gc::FilledMarkFrom + Gc::FilledMarkSpan * filled);
        const AlphaRead print = ReadAlpha(byte);
        CHECK(print.Ground && !print.Canopy && !print.OverCanopy && print.Print && !print.Standing);
        const float read = std::clamp((Read(byte) - Gc::FilledMarkFrom) / Gc::FilledMarkSpan, 0.0f, 1.0f);
        CHECK(std::abs(read - filled) <= 1.0f / (UnormMost * Gc::FilledMarkSpan));
    }

    // HD ground, every share of it that can't be entered.
    for (uint32_t share = 0; share < Bytes; share++)
    {
        const float blocked = Read(share);
        const AlphaRead ground = ReadAlpha(Written(1.0f - Gc::BlockedMarkSpan * blocked));
        CHECK(ground.Ground && !ground.Canopy && !ground.OverCanopy && !ground.Print && !ground.Standing);
        CHECK_EQ(ground.FoggedWater, 0u);
    }
}

TEST_CASE("gpu channels: the stands and the index target's alpha merge into the mark a frame without stands holds")
{
    // Every pair the layout allows: a stands byte, NoStand's or a mark (a fold's, or what any draw wrote in both), over
    // any alpha. NoStand is a byte no draw needs to tell from the alpha.
    const uint32_t noStand = Written(Gc::NoStand);
    CHECK_EQ(noStand, Bytes - 1);

    for (uint32_t stands = 0; stands < Bytes; stands++)
    {
        for (uint32_t alpha = 0; alpha < Bytes; alpha++)
        {
            CHECK_EQ(Written(Gc::MarkOf(Read(alpha), Read(stands))), stands == noStand ? alpha : stands);
        }
    }

    // A draw writes its mark in both (where its pipeline writes the alpha), so the merge is that mark, NoStand's byte
    // too (HD ground's highest); a pass without stands binds NoStand, so the alpha is read as it is.
    for (uint32_t mark = 0; mark < Bytes; mark++)
    {
        CHECK_EQ(Written(Gc::MarkOf(Read(mark), Read(mark))), mark);
        CHECK_EQ(Written(Gc::MarkOf(Read(mark), Gc::NoStand)), mark);
    }

    // A fold writes its marks (what stands, a canopy) in the stands alone, over whatever alpha the ground under it left:
    // each is told from NoStand and merges to itself, the mark the forest's index pass would have left in the alpha.
    for (const float mark : {Gc::StandingMark, Gc::SwayMark})
    {
        CHECK(Written(mark) != noStand);

        for (uint32_t alpha = 0; alpha < Bytes; alpha++)
        {
            CHECK_EQ(Written(Gc::MarkOf(Read(alpha), mark)), Written(mark));
        }
    }
}

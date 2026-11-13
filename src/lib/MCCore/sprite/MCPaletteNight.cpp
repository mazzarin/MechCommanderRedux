#include "stdafx.h"
#include "sprite/MCPaletteNight.h"
#include "color/MCPalette.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>
    /// A beam's box is taken this many degrees wider than its spread to each side (a lamp's beam, tipped down, lights
    /// a little wider near the lamp), and this many pixels wider all round (the arc's samples cut its curve short).
    /// </summary>
    constexpr float BoxMoreDegrees = 5.0f;
    constexpr float BoxMorePixels = 2.0f;
    /// <summary>The points a beam's box is taken round along the far end of its reach.</summary>
    constexpr int32_t BoxArcPoints = 5;
    /// <summary>
    /// The beams' lift is worked out once a cell this many pixels square, at its middle: its levels' steps are too
    /// faint for the cells' stairs to show, and it's a quarter of the work.
    /// </summary>
    constexpr int32_t LiftCell = 2;
    constexpr float CellMiddle = static_cast<float>(LiftCell) * 0.5f;
    /// <summary>
    /// A beam in the air, from the lamp to where its middle meets the ground, lifts at most this share of the night
    /// (the ground it lights, all of it). The rest is the shape of HD lighting's (composite.pshader's AirBeamsAt, whose
    /// constants these are): it fades out this far past where its middle meets the ground (a share of the way from
    /// its lamp), widens from this many pixels at its lamp, is full within this share of its width, and fades out as
    /// its unit faces away (its share up the screen from AirBeamHideFrom to AirBeamHidden), behind its unit.
    /// </summary>
    constexpr float AirLift = 0.75f;
    constexpr float AirBeamLength = 1.1f;
    constexpr float AirBeamLampWidth = 1.0f;
    constexpr float AirBeamFull = 0.4f;
    constexpr float AirBeamHideFrom = 0.2f;
    constexpr float AirBeamHidden = 0.7f;

    /// <summary>The beams the frame's units shine, drawn at the frame's end.</summary>
    std::vector<MCSceneLight>& Beams()
    {
        static std::vector<MCSceneLight> beams;
        return beams;
    }

    /// <summary>A beam and the box of the world (inclusive) that its light can reach.</summary>
    struct BeamBox
    {
        MCRect Box;
        std::reference_wrapper<const MCSceneLight> Beam;
        /// <summary>Its way along the ground (a unit vector, a screen height the aspect of one).</summary>
        float WayX = 0.0f;
        float WayY = 0.0f;
        /// <summary>
        /// Its beam in the air on the screen: from its lamp, its way to where its middle meets the ground (and one over
        /// that way's length squared, and its length), its spread (its half width a pixel along), and how much it
        /// shows as its unit faces away.
        /// </summary>
        float LampX = 0.0f;
        float LampY = 0.0f;
        float AirX = 0.0f;
        float AirY = 0.0f;
        float AirPerLength2 = 0.0f;
        float AirLength = 0.0f;
        float AirSpread = 0.0f;
        float AirShows = 0.0f;
    };

    /// <summary>HLSL's smoothstep: 0 at or below <paramref name="low"/>, 1 at or above <paramref name="high"/>.</summary>
    float SmoothStep(float low, float high, float x)
    {
        const float t = std::clamp((x - low) / (high - low), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    /// <summary>
    /// <paramref name="beam"/>'s box in a world <paramref name="width"/> by <paramref name="height"/> pixels: the
    /// ground ahead of its lamp within its reach and a little more than its spread (along the ground a screen height
    /// <paramref name="aspect"/> of one); none for a beam pointing nowhere or wholly off the world.
    /// </summary>
    std::optional<BeamBox> BoxOf(const MCSceneLight& beam, int32_t width, int32_t height, float aspect)
    {
        const float aimX = beam.SpotX;
        const float aimY = beam.SpotY / aspect;
        const float aim = std::hypot(aimX, aimY);

        if (aim <= 0.0f || beam.Reach <= 0.0f)
        {
            return std::nullopt;
        }

        const float wayX = aimX / aim;
        const float wayY = aimY / aim;
        const float spread = (MCSceneLights::HeadlightDegrees + BoxMoreDegrees) * std::numbers::pi_v<float> / 180.0f;
        // Its lamp (its beam in the air starts there), and the far end of its reach.
        float left = beam.X0;
        float right = beam.X0;
        float top = beam.Y0 - beam.Height;
        float bottom = beam.Y0;

        for (int32_t point = 0; point < BoxArcPoints; point++)
        {
            const float turn = spread * (2.0f * static_cast<float>(point) / (BoxArcPoints - 1) - 1.0f);
            const float x = beam.X0 + beam.Reach * (wayX * std::cos(turn) - wayY * std::sin(turn));
            const float y = beam.Y0 + beam.Reach * (wayX * std::sin(turn) + wayY * std::cos(turn)) * aspect;
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }

        const MCRect box{std::max(0, static_cast<int32_t>(std::floor(left - BoxMorePixels))),
                         std::max(0, static_cast<int32_t>(std::floor(top - BoxMorePixels))),
                         std::min(width - 1, static_cast<int32_t>(std::ceil(right + BoxMorePixels))),
                         std::min(height - 1, static_cast<int32_t>(std::ceil(bottom + BoxMorePixels)))};

        if (box.X1 < box.X0 || box.Y1 < box.Y0)
        {
            return std::nullopt;
        }

        BeamBox found{box, beam, wayX, wayY};
        found.LampX = beam.X0;
        found.LampY = beam.Y0 - beam.Height;
        found.AirX = beam.SpotX;
        found.AirY = beam.SpotY + beam.Height;
        const float airLength2 = found.AirX * found.AirX + found.AirY * found.AirY;

        if (airLength2 > 0.0f && beam.SpotCos > 0.0f)
        {
            found.AirLength = std::sqrt(airLength2);
            found.AirPerLength2 = 1.0f / airLength2;
            found.AirSpread = std::sqrt(std::max(1.0f - beam.SpotCos * beam.SpotCos, 0.0f)) / beam.SpotCos;
            found.AirShows = 1.0f - SmoothStep(AirBeamHideFrom, AirBeamHidden, -found.AirY / found.AirLength);
        }

        return found;
    }

    /// <summary>
    /// How much of its light <paramref name="box"/>'s beam in the air shows at window point (<paramref name="x"/>,
    /// <paramref name="y"/>), 0 to 1: as HD lighting's beam in the air (composite.pshader's AirBeamsAt), without the
    /// units in its way.
    /// </summary>
    float AirAt(const BeamBox& box, float x, float y)
    {
        if (box.AirShows <= 0.0f)
        {
            return 0.0f;
        }

        const float dx = x - box.LampX;
        const float dy = y - box.LampY;
        const float t = (dx * box.AirX + dy * box.AirY) * box.AirPerLength2;

        if (t <= 0.0f || t >= AirBeamLength)
        {
            return 0.0f;
        }

        // Its half width there: the beam's spread over the way from the lamp.
        const float width = AirBeamLampWidth + t * box.AirLength * box.AirSpread;
        const float offX = dx - box.AirX * t;
        const float offY = dy - box.AirY * t;
        const float off2 = offX * offX + offY * offY;

        if (off2 >= width * width)
        {
            return 0.0f;
        }

        const float across = std::sqrt(off2) / width;
        return (1.0f - t / AirBeamLength) * box.AirShows * (1.0f - SmoothStep(AirBeamFull, 1.0f, across));
    }

    /// <summary>
    /// How much of the night <paramref name="box"/>'s beam lifts at window point (<paramref name="atX"/>,
    /// <paramref name="atY"/>), 0 to 1 (along the ground a screen height <paramref name="aspect"/> of one): the
    /// ground's by its light (none at once outside its reach and spread, which <paramref name="spreadTan2"/>, the box's
    /// spread's tangent squared, bounds), or its beam in the air's.
    /// </summary>
    float LiftAt(const BeamBox& box, float atX, float atY, float aspect, float spreadTan2)
    {
        const MCSceneLight& beam = box.Beam;
        const float air = AirLift * AirAt(box, atX, atY);
        const float dx = atX - beam.X0;
        const float dy = (atY - beam.Y0) / aspect;
        const float along = dx * box.WayX + dy * box.WayY;
        const float across = dx * box.WayY - dy * box.WayX;

        if (along <= 0.0f || dx * dx + dy * dy >= beam.Reach * beam.Reach ||
            across * across > along * along * spreadTan2)
        {
            return air;
        }

        return std::max(air, MCSceneLights::GroundLight(beam, atX, atY, aspect) / MCSceneLights::HeadlightLight());
    }
}

namespace MCPaletteNight
{
    bool Shown()
    {
        return MCSceneLights::PlayedTime() == MCTimeOfDay::Night && !MCSceneLights::LightingDrawn();
    }

    std::optional<MCTableFit> NightFit()
    {
        const MCPalette& palette = GamePalette();
        const std::optional<MCColorTable> table =
            Shown() && palette.NumBitmapHazeLevels > 0 ? palette.GetHazePalette(-NightDim) : std::nullopt;

        if (!table.has_value())
        {
            return std::nullopt;
        }

        // The palette's colours are 6 bits a channel.
        constexpr int32_t SixToEightBits = 2;
        const std::span<const MCVfxRgb, MCPalette::ColorCount> colours = palette.Colors();
        return FitColorTable(
            [colours](int32_t index)
            {
                const MCVfxRgb& colour = colours[static_cast<size_t>(index)];
                return std::array<uint8_t, 3>{static_cast<uint8_t>(colour.R << SixToEightBits),
                                              static_cast<uint8_t>(colour.G << SixToEightBits),
                                              static_cast<uint8_t>(colour.B << SixToEightBits)};
            },
            *table);
    }

    int32_t DimFor(float lift)
    {
        return static_cast<int32_t>(std::lround(static_cast<float>(NightDim) * (1.0f - std::clamp(lift, 0.0f, 1.0f))));
    }

    std::vector<MCShade> Shades(int32_t width, int32_t height, std::span<const MCSceneLight> beams, float aspect)
    {
        std::vector<MCShade> shades;

        if (width <= 0 || height <= 0 || aspect <= 0.0f)
        {
            return shades;
        }

        std::vector<BeamBox> boxes;

        for (const MCSceneLight& beam : beams)
        {
            if (const std::optional<BeamBox> box = BoxOf(beam, width, height, aspect); box.has_value())
            {
                boxes.push_back(*box);
            }
        }

        const float spread = (MCSceneLights::HeadlightDegrees + BoxMoreDegrees) * std::numbers::pi_v<float> / 180.0f;
        const float spreadTan2 = std::tan(spread) * std::tan(spread);
        std::vector<uint8_t> dims(static_cast<size_t>(width));
        // The shades the row above ended (each row's runs carry on down while they keep their place and table).
        std::vector<size_t> open;
        std::vector<size_t> next;

        // The pixels the beams may reach on the rows being drawn (the rest is the night).
        int32_t lo = width;
        int32_t hi = -1;

        for (int32_t y = 0; y < height; y++)
        {
            // Each cell's lift, at its middle, for its rows.
            if (y % LiftCell == 0)
            {
                const auto crosses = [y](const BeamBox& box) { return box.Box.Y0 < y + LiftCell && box.Box.Y1 >= y; };
                lo = width;
                hi = -1;

                for (const BeamBox& box : boxes | std::views::filter(crosses))
                {
                    lo = std::min(lo, box.Box.X0 - box.Box.X0 % LiftCell);
                    hi = std::max(hi, std::min(box.Box.X1 - box.Box.X1 % LiftCell + LiftCell - 1, width - 1));
                }

                if (hi >= lo)
                {
                    std::fill(dims.begin() + lo, dims.begin() + hi + 1, static_cast<uint8_t>(NightDim));
                }

                const float atY = static_cast<float>(y) + CellMiddle;

                for (const BeamBox& box : boxes | std::views::filter(crosses))
                {
                    for (int32_t x = box.Box.X0 - box.Box.X0 % LiftCell; x <= box.Box.X1; x += LiftCell)
                    {
                        const float lift = LiftAt(box, static_cast<float>(x) + CellMiddle, atY, aspect, spreadTan2);

                        if (lift <= 0.0f)
                        {
                            continue;
                        }

                        const auto dim = static_cast<uint8_t>(DimFor(lift));

                        for (int32_t cell = x; cell < std::min(x + LiftCell, width); cell++)
                        {
                            uint8_t& at = dims[static_cast<size_t>(cell)];
                            at = std::min(at, dim);
                        }
                    }
                }
            }

            const auto dimAt = [&](int32_t x)
            { return x < lo || x > hi ? static_cast<uint8_t>(NightDim) : dims[static_cast<size_t>(x)]; };
            // The row above's shades, in their order across, and the next that may carry on into this row.
            size_t above = 0;
            const auto addRun = [&](int32_t x0, int32_t x1, uint8_t dim)
            {
                while (above < open.size() && shades[open[above]].Rect.X0 < x0)
                {
                    above++;
                }

                if (above < open.size() && shades[open[above]].Rect.X0 == x0 && shades[open[above]].Rect.X1 == x1 &&
                    shades[open[above]].Dim == dim)
                {
                    shades[open[above]].Rect.Y1 = y;
                    next.push_back(open[above]);
                }
                else
                {
                    shades.push_back(MCShade{MCRect{x0, y, x1, y}, dim});
                    next.push_back(shades.size() - 1);
                }
            };

            // The row's runs: outside the beams' pixels it holds the night throughout.
            int32_t x0 = 0;
            uint8_t run = dimAt(0);

            for (int32_t x = std::max(lo, 1); x <= std::min(hi + 1, width - 1); x++)
            {
                if (const uint8_t dim = dimAt(x); dim != run)
                {
                    addRun(x0, x - 1, run);
                    x0 = x;
                    run = dim;
                }
            }

            addRun(x0, width - 1, run);
            open.swap(next);
            next.clear();
        }

        return shades;
    }

    void Begin()
    {
        Beams().clear();
    }

    void AddBeam(const MCSceneLight& beam)
    {
        Beams().push_back(beam);
    }

    void Draw(const MCPane& pane)
    {
        const std::vector<MCSceneLight> beams = std::exchange(Beams(), {});
        const MCPalette& palette = GamePalette();

        // A palette without darkening tables (none of the game's) has nothing to dim with.
        if (!Shown() || palette.NumBitmapHazeLevels <= 0)
        {
            return;
        }

        const std::vector<MCShade> shades =
            Shades(pane.X1 - pane.X0 + 1, pane.Y1 - pane.Y0 + 1, beams, MCSceneLights::GroundAspect());

        for (const MCShade& shade : shades)
        {
            if (shade.Dim == 0)
            {
                continue;
            }

            // The palette's second haze set darkens (the first lightens, as haze).
            const std::optional<MCColorTable> table = palette.GetHazePalette(-shade.Dim);

            if (table.has_value() && VfxTranslateRect(pane, shade.Rect, *table) != 0)
            {
                return;
            }
        }
    }
} // namespace MCPaletteNight

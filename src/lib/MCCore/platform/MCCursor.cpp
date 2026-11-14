#include "stdafx.h"
#include "platform/MCCursor.h"
#include "color/MCWaterCycle.h"
#include "platform/MCDisplay.h"
#include "platform/MCFrameLog.h"
#include "platform/MCInput.h"
#include "platform/MCSdlHandle.h"

MCCursorImage MCCursorImage::Blank(int width, int height, int hotX, int hotY)
{
    MCCursorImage image;
    image.Width = std::max(width, 0);
    image.Height = std::max(height, 0);
    image.HotX = hotX;
    image.HotY = hotY;
    image.Pixels.assign(static_cast<size_t>(image.Width) * image.Height, 0);
    image.Opaque.assign(image.Pixels.size(), 0);
    return image;
}

MCCursorImage MCCursorImage::Overlay(const MCCursorImage& under, const MCCursorImage& over)
{
    // Both pictures relative to the shared hot spot.
    const int left = std::min(-under.HotX, -over.HotX);
    const int top = std::min(-under.HotY, -over.HotY);
    const int right = std::max(under.Width - under.HotX, over.Width - over.HotX);
    const int bottom = std::max(under.Height - under.HotY, over.Height - over.HotY);
    MCCursorImage result = Blank(right - left, bottom - top, -left, -top);

    for (const MCCursorImage& layer : {std::cref(under), std::cref(over)})
    {
        const int originX = result.HotX - layer.HotX;
        const int originY = result.HotY - layer.HotY;

        for (int y = 0; y < layer.Height; ++y)
        {
            for (int x = 0; x < layer.Width; ++x)
            {
                const size_t from = static_cast<size_t>(y) * layer.Width + x;

                if (layer.Opaque[from] == 0)
                {
                    continue;
                }

                const size_t to = static_cast<size_t>(originY + y) * result.Width + originX + x;
                result.Pixels[to] = layer.Pixels[from];
                result.Opaque[to] = 1;
            }
        }
    }

    return result;
}

MCCursorImage MCCursorImage::WithHotSpotInside(const MCCursorImage& image)
{
    if (image.HotX >= 0 && image.HotY >= 0 && image.HotX < image.Width && image.HotY < image.Height)
    {
        return image;
    }

    return Overlay(image, Blank(1, 1, 0, 0));
}

namespace
{
    constexpr float Full = 255.0f;
    constexpr size_t ColourChannels = 3;
    constexpr size_t PaletteBytes = 768;
    constexpr size_t ToneValues = 256;
    /// <summary>The palette entries the game's fades and brightness steps change (FirstGameColor, EndGameColor).</summary>
    constexpr size_t FirstGameEntry = 10;
    constexpr size_t EndGameEntry = 0xf6;
    /// <summary>How far (0..255) an entry may be from its curve's value and still be on it.</summary>
    constexpr float ToneSlack = 1.0f;

    bool sameColors(const std::array<SDL_Color, 256>& a, const std::array<SDL_Color, 256>& b)
    {
        return std::memcmp(a.data(), b.data(), sizeof(SDL_Color) * 256) == 0;
    }

    /// <summary>
    /// How today's palette changed every value (0..255) the pack's colours had, one curve for the three channels: the
    /// game changes its colours through one (its brightness steps, the black between screens).
    /// </summary>
    struct MCTones
    {
        /// <summary>Today's value (0..255) of each value the pack had.</summary>
        std::array<float, ToneValues> Of{};
        /// <summary>
        /// The curve is no change: today's colours are the pack's, or no one curve takes the pack's colours to
        /// today's (a screen with its own palette), which the picture's own colours then tell (MCRecolour).
        /// </summary>
        bool Unchanged = true;

        static MCTones None()
        {
            MCTones tones;

            for (size_t value = 0; value < ToneValues; ++value)
            {
                tones.Of[value] = static_cast<float>(value);
            }

            return tones;
        }

        /// <summary>Today's value (0..255) of <paramref name="value"/> (0..255), between the curve's whole values.</summary>
        float At(float value) const
        {
            const float x = std::clamp(value, 0.0f, Full);
            const size_t below = std::min(static_cast<size_t>(x), ToneValues - 2);
            return Of[below] + (Of[below + 1] - Of[below]) * (x - static_cast<float>(below));
        }

        bool operator==(const MCTones&) const = default;
    };

    /// <summary>
    /// The value at <paramref name="x"/> on the line through (x0, y0) and (x1, y1) in log-log terms (a power law, as
    /// a gamma table bends), or the straight line where a value is 0.
    /// </summary>
    float Between(float x, float x0, float y0, float x1, float y1)
    {
        if (x > 0.0f && x0 > 0.0f && y0 > 0.0f && y1 > 0.0f)
        {
            return y0 * std::exp(std::log(y1 / y0) * std::log(x / x0) / std::log(x1 / x0));
        }

        return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
    }

    /// <summary>Whether <paramref name="entry"/> is one of the water's, which the game rewrites as the water moves.</summary>
    bool Water(size_t entry)
    {
        return entry >= FirstWaterColor && entry - FirstWaterColor < WaterMagicColors.size();
    }

    /// <summary>
    /// The curve taking <paramref name="madeWith"/>'s game entries to <paramref name="colors"/>' (the water's left
    /// out): at each value the pack has, today's mean of the channels that had it; between them and past the last, a
    /// power law; from 0 to the first, the next stretch's down to 0. None when every entry is as made, or when one
    /// isn't on the curve (another palette, not a brightness step).
    /// </summary>
    MCTones FitTones(std::span<const uint8_t> madeWith, const std::array<SDL_Color, 256>& colors)
    {
        std::array<float, ToneValues> sum{};
        std::array<int, ToneValues> count{};
        bool asMade = true;

        const auto forEachSample = [&](auto&& take)
        {
            for (size_t entry = FirstGameEntry; entry < EndGameEntry; ++entry)
            {
                if (Water(entry))
                {
                    continue;
                }

                const SDL_Color now = colors[entry];
                const std::array<uint8_t, ColourChannels> today = {now.r, now.g, now.b};

                for (size_t c = 0; c < ColourChannels; ++c)
                {
                    take(madeWith[entry * ColourChannels + c], static_cast<float>(today[c]));
                }
            }
        };

        forEachSample(
            [&](uint8_t made, float today)
            {
                sum[made] += today;
                ++count[made];
                asMade = asMade && today == static_cast<float>(made);
            });

        bool onCurve = true;
        forEachSample(
            [&](uint8_t made, float today)
            { onCurve = onCurve && std::abs(today - sum[made] / static_cast<float>(count[made])) <= ToneSlack; });

        if (asMade || !onCurve)
        {
            return MCTones::None();
        }

        // The samples, rising, from 0.
        std::array<float, ToneValues + 1> xs{};
        std::array<float, ToneValues + 1> ys{};
        size_t samples = 0;

        if (count[0] == 0)
        {
            ++samples;
        }

        for (size_t value = 0; value < ToneValues; ++value)
        {
            if (count[value] > 0)
            {
                xs[samples] = static_cast<float>(value);
                ys[samples] = sum[value] / static_cast<float>(count[value]);
                ++samples;
            }
        }

        if (samples < 2)
        {
            return MCTones::None();
        }

        MCTones tones;
        tones.Unchanged = false;
        size_t k = 0;

        for (size_t value = 0; value < ToneValues; ++value)
        {
            const float x = static_cast<float>(value);

            // xs[k] <= x < xs[k + 1], or the last stretch (past the last sample).
            while (k + 2 < samples && xs[k + 1] <= x)
            {
                ++k;
            }

            float y = 0.0f;

            if (x == xs[k] || x == xs[k + 1])
            {
                y = x == xs[k] ? ys[k] : ys[k + 1];
            }
            else if (k == 0 && ys[0] <= 0.0f && samples > 2 && ys[1] > 0.0f && ys[2] > 0.0f)
            {
                y = Between(x, xs[1], ys[1], xs[2], ys[2]);
            }
            else
            {
                y = Between(x, xs[k], ys[k], xs[k + 1], ys[k + 1]);
            }

            tones.Of[value] = std::clamp(y, 0.0f, Full);
        }

        return tones;
    }

    /// <summary>
    /// <see cref="FitTones"/> for <paramref name="image"/>'s HD copy (none without one), kept for the next call with
    /// the same colours (a cursor is made at two sizes, and every shape in one palette).
    /// </summary>
    const MCTones& TonesOf(const MCCursorImage& image, const std::array<SDL_Color, 256>& colors)
    {
        static const MCTones None = MCTones::None();

        if (image.Hd == nullptr || image.Hd->MadeWith.size() != PaletteBytes)
        {
            return None;
        }

        struct MCLastTones
        {
            std::vector<uint8_t> MadeWith;
            std::array<SDL_Color, 256> Colors{};
            MCTones Tones;
        };

        thread_local MCLastTones last;

        if (last.MadeWith != image.Hd->MadeWith || !sameColors(last.Colors, colors))
        {
            last = {image.Hd->MadeWith, colors, FitTones(image.Hd->MadeWith, colors)};
        }

        return last.Tones;
    }
}

namespace
{
    /// <summary>An SDL cursor made for a picture, in the colours it uses.</summary>
    struct MCMadeCursor
    {
        /// <summary>The cursor; shared with the input layer while it is the game's cursor.</summary>
        std::shared_ptr<SDL_Cursor> Cursor;
        /// <summary>The colours of the indices the picture shows; the others are zero.</summary>
        std::array<SDL_Color, 256> Colors{};
        /// <summary>The curve its HD copy went through (<see cref="TonesOf"/>).</summary>
        MCTones Tones = MCTones::None();
    };

    struct MCCursorState
    {
        /// <summary>The game's cursor shapes (<see cref="MCCursor::Preload"/>).</summary>
        std::vector<MCCursorImage> Shapes;
        /// <summary>Each shape's cursor, at the current scale; null until made.</summary>
        std::vector<MCMadeCursor> ShapeCursors;
        /// <summary>The last other picture shown (a dragged item), remade when it changes.</summary>
        MCCursorImage OtherImage;
        MCMadeCursor Other;
        std::shared_ptr<SDL_Cursor> Shown;
        /// <summary>Cursors no longer wanted, freed once another is shown (SDL would show its arrow in between).</summary>
        std::vector<std::shared_ptr<SDL_Cursor>> Retired;
        /// <summary>
        /// Made but not yet shown over the window, where Windows builds the cursor (see warm). Recoloured cursors of
        /// shapes already shown aren't added (see ensure).
        /// </summary>
        std::vector<std::shared_ptr<SDL_Cursor>> Cold;
        float ScaleX = 0.0f;
        float ScaleY = 0.0f;
        float Density = 0.0f;
        /// <summary>The colours shown, as the last call read them.</summary>
        std::array<SDL_Color, 256> ShownColors{};
        uint64_t MadeCount = 0;
    };

    MCCursorState& state()
    {
        static MCCursorState instance;
        return instance;
    }

    /// <summary>A bitmap as an RGBA surface, or none.</summary>
    MCSdlHandle<SDL_Surface> toSurface(const MCCursorBitmap& bitmap)
    {
        MCSdlHandle<SDL_Surface> surface(SDL_CreateSurface(bitmap.Width, bitmap.Height, SDL_PIXELFORMAT_RGBA32));

        if (surface == nullptr)
        {
            return nullptr;
        }

        for (int y = 0; y < bitmap.Height; ++y)
        {
            std::memcpy(static_cast<uint8_t*>(surface->pixels) + static_cast<ptrdiff_t>(y) * surface->pitch,
                        bitmap.Rgba.data() + static_cast<size_t>(y) * bitmap.Width * 4,
                        static_cast<size_t>(bitmap.Width) * 4);
        }

        return surface;
    }

    /// <summary>
    /// <paramref name="colors"/> kept only at the indices <paramref name="image"/> shows, so a palette change it
    /// doesn't use (the water cycle) doesn't remake its cursor.
    /// </summary>
    std::array<SDL_Color, 256> usedColors(const MCCursorImage& image, const std::array<SDL_Color, 256>& colors)
    {
        std::array<SDL_Color, 256> used{};

        for (size_t i = 0; i < image.Pixels.size(); ++i)
        {
            if (image.Opaque[i] != 0)
            {
                used[image.Pixels[i]] = colors[image.Pixels[i]];
            }
        }

        return used;
    }

    /// <summary>A new SDL cursor of <paramref name="picture"/>, or none.</summary>
    std::shared_ptr<SDL_Cursor> makeCursor(const MCCursorImage& picture, const std::array<SDL_Color, 256>& colors,
                                           float scaleX, float scaleY, float density)
    {
        // The surface is the picture at 100% display scale (window points); on a high-density display the pixel
        // sized one goes with it as an alternate image, which SDL picks there.
        const MCCursorImage image = MCCursorImage::WithHotSpotInside(picture);
        const MCCursorBitmap base = MCCursor::Rasterize(image, colors, scaleX / density, scaleY / density);
        const MCSdlHandle<SDL_Surface> surface = toSurface(base);

        if (surface == nullptr)
        {
            return nullptr;
        }

        if (density != 1.0f)
        {
            // The surface takes its own reference to the alternate image.
            if (const MCSdlHandle<SDL_Surface> dense = toSurface(MCCursor::Rasterize(image, colors, scaleX, scaleY)))
            {
                SDL_AddSurfaceAlternateImage(surface.get(), dense.get());
            }
        }

        SDL_Cursor* made = SDL_CreateColorCursor(surface.get(), base.HotX, base.HotY);

        if (made == nullptr)
        {
            SDL_Log("MCCursor: SDL_CreateColorCursor failed: %s", SDL_GetError());
            return nullptr;
        }

        return std::shared_ptr<SDL_Cursor>(made, MCSdlDeleter{});
    }

    /// <summary>Retires <paramref name="made"/>'s cursor, if any, and empties it.</summary>
    void retire(MCCursorState& cursor, MCMadeCursor& made)
    {
        if (made.Cursor != nullptr)
        {
            cursor.Retired.push_back(made.Cursor);
            std::erase(cursor.Cold, made.Cursor);
        }

        made = {};
    }

    /// <summary>
    /// Makes <paramref name="made"/> show <paramref name="image"/> in <paramref name="colors"/> unless it already
    /// does; false if SDL couldn't.
    /// </summary>
    bool ensure(MCCursorState& cursor, MCMadeCursor& made, const MCCursorImage& image,
                const std::array<SDL_Color, 256>& colors)
    {
        const std::array<SDL_Color, 256> used = usedColors(image, colors);
        // The HD copy's curve too: a brightness step changes it with the colours the picture uses, but a screen's
        // palette may change the colours it is read from alone.
        MCTones tones = TonesOf(image, colors);

        if (made.Cursor != nullptr && sameColors(made.Colors, used) && made.Tones == tones)
        {
            return true;
        }

        MCFrameLog::Scope part("cursor.make");
        std::shared_ptr<SDL_Cursor> fresh = makeCursor(image, colors, cursor.ScaleX, cursor.ScaleY, cursor.Density);

        if (MCFrameLog::Enabled())
        {
            MCFrameLog::Note(std::format("cursor made, {}x{} picture{}", image.Width, image.Height,
                                         made.Cursor != nullptr ? " (remade: new colours)" : ""));
        }

        if (fresh == nullptr)
        {
            return false;
        }

        // Only a first cursor for the picture (or one replacing a cursor never shown) is warmed. Warming shows each
        // cold cursor over the window, so warming every shape recoloured by a palette change (the brightness slider
        // changes it with every mouse move) would flash the other shapes on screen; a recoloured shape builds its
        // icon when it is next shown.
        const bool wasCold = made.Cursor == nullptr || std::ranges::find(cursor.Cold, made.Cursor) != cursor.Cold.end();
        retire(cursor, made);
        made = {fresh, used, std::move(tones)};

        if (wasCold)
        {
            cursor.Cold.push_back(std::move(fresh));
        }

        ++cursor.MadeCount;
        return true;
    }

    /// <summary>
    /// Takes the display's scale and colours. A new scale drops every cursor. Nothing is made here: a shape is made
    /// again in new colours or at a new scale when it is next shown (ensure), so no frame makes more than the one on
    /// screen.
    /// </summary>
    void prepare(MCCursorState& cursor, MCDisplay& display)
    {
        display.GetShownColors(cursor.ShownColors);
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        display.PixelScale(scaleX, scaleY);
        float density = SDL_GetWindowPixelDensity(&display.Window());

        if (density <= 0.0f)
        {
            density = 1.0f;
        }

        if (cursor.ScaleX != scaleX || cursor.ScaleY != scaleY || cursor.Density != density)
        {
            if (MCFrameLog::Enabled())
            {
                MCFrameLog::Note(std::format("cursor scale {}x{} density {} -> {}x{} density {}: all dropped",
                                             cursor.ScaleX, cursor.ScaleY, cursor.Density, scaleX, scaleY, density));
            }

            for (MCMadeCursor& made : cursor.ShapeCursors)
            {
                retire(cursor, made);
            }

            retire(cursor, cursor.Other);
            cursor.ScaleX = scaleX;
            cursor.ScaleY = scaleY;
            cursor.Density = density;
        }
    }

    /// <summary>
    /// Shows each cold cursor once, then puts back the one on screen. Windows builds an SDL cursor's icon (an .ANI
    /// resource, slow) the first time it is shown over the window, so this moves that work off the switch. Only
    /// while the game's cursor is up over the window: elsewhere SDL shows its arrow and builds nothing.
    /// </summary>
    void warm(MCCursorState& cursor, SDL_Window& window)
    {
        if (cursor.Cold.empty() || SDL_GetMouseFocus() != &window || !SDL_CursorVisible() ||
            SDL_GetCursor() != cursor.Shown.get())
        {
            return;
        }

        MCFrameLog::Scope part("cursor.warm");

        if (MCFrameLog::Enabled())
        {
            MCFrameLog::Note(std::format("cursor: warmed {}", cursor.Cold.size()));
        }

        for (const std::shared_ptr<SDL_Cursor>& cold : cursor.Cold)
        {
            SDL_SetCursor(cold.get());
        }

        cursor.Cold.clear();
        SDL_SetCursor(cursor.Shown.get());
    }

    /// <summary>Puts <paramref name="shown"/> on screen, then frees the retired cursors and warms the cold ones.</summary>
    void showCursor(MCCursorState& cursor, std::shared_ptr<SDL_Cursor> shown, SDL_Window& window)
    {
        if (MCFrameLog::Enabled() && cursor.Shown != shown)
        {
            const bool cold = std::ranges::find(cursor.Cold, shown) != cursor.Cold.end();
            MCFrameLog::Note(std::format("cursor switched{} (mouse focus {}, visible {})", cold ? " to a cold one" : "",
                                         SDL_GetMouseFocus() == &window, SDL_CursorVisible()));
        }

        // The cursor shown before stays held until the new one is up, so SDL never shows its arrow in between.
        const std::shared_ptr<SDL_Cursor> before = std::exchange(cursor.Shown, shown);

        if (MCFrameLog::Scope part("cursor.set"); true)
        {
            MCInput::SetGameCursor(std::move(shown));
        }

        // The retired cursors go now, but the one on screen.
        std::erase_if(cursor.Retired,
                      [&](const std::shared_ptr<SDL_Cursor>& retired) { return retired != cursor.Shown; });

        warm(cursor, window);
    }

    /// <summary>The attached display (its window shows the cursor), or null.</summary>
    std::shared_ptr<MCDisplay> shownDisplay()
    {
        return MCInput::Display();
    }
}

namespace
{
    /// <summary>The most a colour channel is scaled by when today's colour differs from the pack's (as the GPU's).</summary>
    constexpr float MaxCorrection = 4.0f;
    /// <summary>Coverage below this (BC7's error around a frame's empty pixels) is transparent.</summary>
    constexpr float CoverageFloor = 1.5f / 255.0f;
    constexpr size_t Channels = 4;
    /// <summary>Where a pixel's centre lies across it.</summary>
    constexpr float PixelCentre = 0.5f;
    /// <summary>How far (0..255) today's colour may be from the curve's and still be as the curve has it.</summary>
    constexpr float OnCurve = 0.5f;
    /// <summary>
    /// How alike an HD colour must be to an original colour (as the pack had it) for that colour's correction to count:
    /// the spread of a Gaussian over their RGB distance, as draw.pshader's RecolourLikeness.
    /// </summary>
    constexpr float RecolourLikeness = 0.08f;
    /// <summary>The share of its nearness an original colour unlike the HD colour still counts by (RecolouredBetween's).</summary>
    constexpr float UnlikeShare = 1e-4f;
    /// <summary>
    /// Past this (the exponent of <see cref="Likeness"/>'s Gaussian), a colour's likeness is under a thousandth of
    /// <see cref="UnlikeShare"/>, so it counts as 0 and its exponential isn't worked out.
    /// </summary>
    constexpr float UnlikeExponent = 16.0f;

    /// <summary>One palette index's change from the colour the pack was made with, through the curve, to today's.</summary>
    struct MCIndexChange
    {
        /// <summary>The pack's colour, 0..1.</summary>
        std::array<float, ColourChannels> Made{};
        /// <summary>Today's colour over the curve's, channel by channel (where <see cref="Informs"/>).</summary>
        std::array<float, ColourChannels> Ratio{};
        /// <summary>
        /// Whether the curve's colour has the channel: one at 0 says nothing about how today's palette changed it (0
        /// over 0, or a black pixel turned to anything).
        /// </summary>
        std::array<bool, ColourChannels> Informs{};
    };

    /// <summary>How today's palette changed a picture's colours from the pack's, worked out once a picture.</summary>
    struct MCRecolour
    {
        /// <summary>The curve every colour of the copy goes through (<see cref="FitTones"/>).</summary>
        MCTones Tones = MCTones::None();
        std::array<MCIndexChange, 256> Changes{};
        /// <summary>The palette indices the picture shows.</summary>
        std::vector<uint8_t> Used;
        /// <summary>
        /// Whether any of them is off the curve in a channel the curve's colour has; if none is, the curve alone
        /// recolours the copy (none: it shows as made).
        /// </summary>
        bool Changed = false;
    };

    MCRecolour RecolourOf(const MCCursorImage& image, const std::array<SDL_Color, 256>& colors)
    {
        MCRecolour recolour;

        if (image.Hd == nullptr || image.Hd->MadeWith.size() != PaletteBytes)
        {
            return recolour;
        }

        recolour.Tones = TonesOf(image, colors);
        std::array<bool, 256> shows{};

        for (size_t i = 0; i < image.Pixels.size(); ++i)
        {
            shows[image.Pixels[i]] = shows[image.Pixels[i]] || image.Opaque[i] != 0;
        }

        for (size_t index = 0; index < shows.size(); ++index)
        {
            if (!shows[index])
            {
                continue;
            }

            recolour.Used.push_back(static_cast<uint8_t>(index));
            MCIndexChange& change = recolour.Changes[index];
            const SDL_Color now = colors[index];
            const std::array<uint8_t, ColourChannels> shown = {now.r, now.g, now.b};

            for (size_t c = 0; c < ColourChannels; ++c)
            {
                const uint8_t then = image.Hd->MadeWith[index * ColourChannels + c];
                const float toned = recolour.Tones.Of[then];
                change.Made[c] = static_cast<float>(then) / Full;
                change.Informs[c] = then != 0 && toned > 0.0f;

                if (change.Informs[c])
                {
                    change.Ratio[c] = std::min(static_cast<float>(shown[c]) / toned, MaxCorrection);
                    recolour.Changed = recolour.Changed || std::abs(static_cast<float>(shown[c]) - toned) > OnCurve;
                }
            }
        }

        return recolour;
    }

    /// <summary>How alike HD colour <paramref name="own"/> (0..1) is to <paramref name="change"/>'s pack colour, 0..1.</summary>
    float Likeness(const MCIndexChange& change, const std::array<float, ColourChannels>& own)
    {
        float distance = 0.0f;

        for (size_t c = 0; c < ColourChannels; ++c)
        {
            const float d = own[c] - change.Made[c];
            distance += d * d;
        }

        const float exponent = distance / (2.0f * RecolourLikeness * RecolourLikeness);
        return exponent > UnlikeExponent ? 0.0f : std::exp(-exponent);
    }

    /// <summary>Colours' changes gathered channel by channel, each by a weight, in the channels each informs.</summary>
    struct MCCorrection
    {
        std::array<float, ColourChannels> Sum{};
        std::array<float, ColourChannels> Weight{};

        void Add(const MCIndexChange& change, float weight, const std::array<bool, ColourChannels>& channels)
        {
            for (size_t c = 0; c < ColourChannels; ++c)
            {
                if (channels[c] && change.Informs[c])
                {
                    Sum[c] += weight * change.Ratio[c];
                    Weight[c] += weight;
                }
            }
        }
    };

    /// <summary>
    /// The correction of HD colour <paramref name="own"/> at (atX, atY) among the original's pixels (pixel n's centre
    /// at n), recoloured as the world's units are (draw.pshader's RecolouredBetween): today's colours of the four original
    /// pixels around over the pack's, each counted by its nearness and by how alike the HD colour is to its colour as
    /// the pack was made. A channel none of the four has (black pixels round a drawn highlight) takes the change of the
    /// picture's own colours by likeness alone; a channel the picture never has, none.
    /// </summary>
    std::array<float, ColourChannels> Correction(const MCCursorImage& image, const MCRecolour& recolour,
                                                 const std::array<float, ColourChannels>& own, float atX, float atY)
    {
        constexpr std::array<bool, ColourChannels> All = {true, true, true};
        constexpr size_t Around = 4;
        std::array<uint8_t, Around> indices{};
        std::array<float, Around> nearness{};
        size_t count = 0;
        const int originalX = static_cast<int>(std::floor(atX));
        const int originalY = static_cast<int>(std::floor(atY));
        const float fx = atX - static_cast<float>(originalX);
        const float fy = atY - static_cast<float>(originalY);

        for (int dy = 0; dy < 2; ++dy)
        {
            for (int dx = 0; dx < 2; ++dx)
            {
                const int ox = originalX + dx;
                const int oy = originalY + dy;
                const float closeness = (dx != 0 ? fx : 1.0f - fx) * (dy != 0 ? fy : 1.0f - fy);

                if (closeness <= 0.0f || ox < 0 || oy < 0 || ox >= image.Width || oy >= image.Height)
                {
                    continue;
                }

                const size_t original = static_cast<size_t>(oy) * image.Width + ox;

                if (image.Opaque[original] != 0)
                {
                    indices[count] = image.Pixels[original];
                    nearness[count] = closeness;
                    ++count;
                }
            }
        }

        // Likeness only tells colours apart: where the four are one colour, nearness alone gives the same.
        const bool mixed =
            std::ranges::any_of(std::span(indices).first(count), [&](uint8_t index) { return index != indices[0]; });
        MCCorrection gathered;

        for (size_t i = 0; i < count; ++i)
        {
            const MCIndexChange& change = recolour.Changes[indices[i]];
            gathered.Add(change, nearness[i] * (mixed ? Likeness(change, own) + UnlikeShare : 1.0f), All);
        }

        std::array<bool, ColourChannels> missing{};

        for (size_t c = 0; c < ColourChannels; ++c)
        {
            missing[c] = gathered.Weight[c] <= 0.0f;
        }

        if (std::ranges::any_of(missing, std::identity{}))
        {
            for (const uint8_t index : recolour.Used)
            {
                const MCIndexChange& change = recolour.Changes[index];
                gathered.Add(change, Likeness(change, own) + UnlikeShare, missing);
            }
        }

        std::array<float, ColourChannels> correction = {1.0f, 1.0f, 1.0f};

        for (size_t c = 0; c < ColourChannels; ++c)
        {
            if (gathered.Weight[c] > 0.0f)
            {
                correction[c] = gathered.Sum[c] / gathered.Weight[c];
            }
        }

        return correction;
    }

    /// <summary>
    /// The colour (straight, 0..255) and coverage (0..255) of <paramref name="image"/>'s HD copy at bitmap pixel
    /// (x, y) of a bitmap scaled by (scaleX, scaleY): between the four HD pixels around its centre, through
    /// <paramref name="recolour"/>'s curve, then recoloured where the picture's colours are off it
    /// (<see cref="Correction"/>). Where today's colours are the pack's the copy shows as made; with a brightness step
    /// or the black between screens, as a whole picture of the original would.
    /// </summary>
    std::array<uint8_t, Channels> HdCursorPixel(const MCCursorImage& image, const MCRecolour& recolour, float scaleX,
                                                float scaleY, int x, int y)
    {
        const MCCursorHd& hd = *image.Hd;
        const int width = image.Width * hd.Scale;
        const int height = image.Height * hd.Scale;
        const float atX = (static_cast<float>(x) + PixelCentre) / scaleX * static_cast<float>(hd.Scale) - PixelCentre;
        const float atY = (static_cast<float>(y) + PixelCentre) / scaleY * static_cast<float>(hd.Scale) - PixelCentre;
        const int baseX = static_cast<int>(std::floor(atX));
        const int baseY = static_cast<int>(std::floor(atY));
        const float fx = atX - static_cast<float>(baseX);
        const float fy = atY - static_cast<float>(baseY);
        std::array<float, Channels> sum{};

        for (int dy = 0; dy < 2; ++dy)
        {
            for (int dx = 0; dx < 2; ++dx)
            {
                const int sx = baseX + dx;
                const int sy = baseY + dy;

                if (sx < 0 || sy < 0 || sx >= width || sy >= height)
                {
                    continue;
                }

                const float weight = (dx != 0 ? fx : 1.0f - fx) * (dy != 0 ? fy : 1.0f - fy);
                const size_t at = (static_cast<size_t>(sy) * width + sx) * Channels;

                for (size_t c = 0; c < Channels; ++c)
                {
                    sum[c] += weight * static_cast<float>(hd.Rgba[at + c]) / Full;
                }
            }
        }

        const float coverage = sum[3];

        if (coverage < CoverageFloor)
        {
            return {};
        }

        std::array<float, ColourChannels> own{};

        for (size_t c = 0; c < ColourChannels; ++c)
        {
            own[c] = sum[c] / coverage;
        }

        std::array<float, ColourChannels> correction = {1.0f, 1.0f, 1.0f};

        if (recolour.Changed)
        {
            correction = Correction(image, recolour, own, (static_cast<float>(x) + PixelCentre) / scaleX - PixelCentre,
                                    (static_cast<float>(y) + PixelCentre) / scaleY - PixelCentre);
        }

        std::array<uint8_t, Channels> out{};

        for (size_t c = 0; c < ColourChannels; ++c)
        {
            const float toned = recolour.Tones.Unchanged ? own[c] : recolour.Tones.At(own[c] * Full) / Full;
            out[c] = static_cast<uint8_t>(std::lround(std::clamp(toned * correction[c], 0.0f, 1.0f) * Full));
        }

        out[3] = static_cast<uint8_t>(std::lround(std::clamp(coverage, 0.0f, 1.0f) * Full));
        return out;
    }
}

namespace MCCursor
{
    MCCursorBitmap Rasterize(const MCCursorImage& image, const std::array<SDL_Color, 256>& colors, float scaleX,
                             float scaleY)
    {
        MCCursorBitmap bitmap;

        if (image.Width <= 0 || image.Height <= 0)
        {
            return bitmap;
        }

        scaleX = std::max(scaleX, 0.01f);
        scaleY = std::max(scaleY, 0.01f);
        bitmap.Width = std::max(1, static_cast<int>(std::lround(image.Width * scaleX)));
        bitmap.Height = std::max(1, static_cast<int>(std::lround(image.Height * scaleY)));
        bitmap.HotX = std::clamp(static_cast<int>(std::floor(image.HotX * scaleX)), 0, bitmap.Width - 1);
        bitmap.HotY = std::clamp(static_cast<int>(std::floor(image.HotY * scaleY)), 0, bitmap.Height - 1);
        bitmap.Rgba.assign(static_cast<size_t>(bitmap.Width) * bitmap.Height * 4, 0);

        // Port-only: the HD copy, where the picture has one of its size.
        if (const std::shared_ptr<const MCCursorHd>& hd = image.Hd;
            hd != nullptr && hd->Scale > 0 &&
            hd->Rgba.size() == static_cast<size_t>(image.Width) * image.Height * hd->Scale * hd->Scale * Channels)
        {
            const MCRecolour recolour = RecolourOf(image, colors);

            for (int y = 0; y < bitmap.Height; ++y)
            {
                for (int x = 0; x < bitmap.Width; ++x)
                {
                    std::ranges::copy(HdCursorPixel(image, recolour, scaleX, scaleY, x, y),
                                      bitmap.Rgba.begin() + (static_cast<ptrdiff_t>(y) * bitmap.Width + x) * 4);
                }
            }

            return bitmap;
        }

        for (int y = 0; y < bitmap.Height; ++y)
        {
            const int sourceY = std::min(image.Height - 1, static_cast<int>(static_cast<float>(y) / scaleY));

            for (int x = 0; x < bitmap.Width; ++x)
            {
                const int sourceX = std::min(image.Width - 1, static_cast<int>(static_cast<float>(x) / scaleX));
                const size_t from = static_cast<size_t>(sourceY) * image.Width + sourceX;

                if (image.Opaque[from] == 0)
                {
                    continue;
                }

                const SDL_Color color = colors[image.Pixels[from]];
                const size_t to = (static_cast<size_t>(y) * bitmap.Width + x) * 4;
                bitmap.Rgba[to] = color.r;
                bitmap.Rgba[to + 1] = color.g;
                bitmap.Rgba[to + 2] = color.b;
                bitmap.Rgba[to + 3] = 255;
            }
        }

        return bitmap;
    }

    void Preload(std::vector<MCCursorImage> shapes)
    {
        MCCursorState& cursor = state();

        for (MCMadeCursor& made : cursor.ShapeCursors)
        {
            retire(cursor, made);
        }

        cursor.Shapes = std::move(shapes);
        cursor.ShapeCursors.assign(cursor.Shapes.size(), {});

        // Every shape made now, at load, in the colours shown now, so each is warmed (Windows' slow part) before play.
        if (const std::shared_ptr<MCDisplay> display = shownDisplay())
        {
            prepare(cursor, *display);

            for (size_t shape = 0; shape < cursor.Shapes.size(); ++shape)
            {
                if (cursor.Shapes[shape].Width > 0)
                {
                    ensure(cursor, cursor.ShapeCursors[shape], cursor.Shapes[shape], cursor.ShownColors);
                }
            }
        }
    }

    void ShowShape(size_t shape)
    {
        MCCursorState& cursor = state();
        const std::shared_ptr<MCDisplay> display = shownDisplay();

        if (display == nullptr || shape >= cursor.Shapes.size() || cursor.Shapes[shape].Width <= 0)
        {
            Hide();
            return;
        }

        prepare(cursor, *display);
        MCMadeCursor& made = cursor.ShapeCursors[shape];

        // Only the shape on screen follows the colours and scale; another is made again when it is next shown.
        if (!ensure(cursor, made, cursor.Shapes[shape], cursor.ShownColors))
        {
            return;
        }

        retire(cursor, cursor.Other);
        cursor.OtherImage = {};
        showCursor(cursor, made.Cursor, display->Window());
    }

    void Show(const MCCursorImage& picture)
    {
        MCCursorState& cursor = state();
        const std::shared_ptr<MCDisplay> display = shownDisplay();

        if (display == nullptr || picture.Width <= 0 || picture.Height <= 0)
        {
            Hide();
            return;
        }

        prepare(cursor, *display);

        if (!(cursor.OtherImage == picture))
        {
            retire(cursor, cursor.Other);
            cursor.OtherImage = picture;
        }

        if (!ensure(cursor, cursor.Other, picture, cursor.ShownColors))
        {
            return;
        }

        showCursor(cursor, cursor.Other.Cursor, display->Window());
    }

    void Hide()
    {
        MCInput::SetGameCursor(nullptr);
    }

    void Shutdown()
    {
        MCCursorState& cursor = state();
        MCInput::SetGameCursor(nullptr);

        for (MCMadeCursor& made : cursor.ShapeCursors)
        {
            retire(cursor, made);
        }

        retire(cursor, cursor.Other);
        const uint64_t madeCount = cursor.MadeCount;
        cursor = MCCursorState{};
        cursor.MadeCount = madeCount;
    }

    uint64_t CursorsMade()
    {
        return state().MadeCount;
    }

    size_t ColdCursors()
    {
        return state().Cold.size();
    }
}

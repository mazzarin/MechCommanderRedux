#include "stdafx.h"
#include "platform/MCCursor.h"
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
    /// <summary>An SDL cursor made for a picture, in the colours it uses.</summary>
    struct MCMadeCursor
    {
        /// <summary>The cursor; shared with the input layer while it is the game's cursor.</summary>
        std::shared_ptr<SDL_Cursor> Cursor;
        /// <summary>The colours of the indices the picture shows; the others are zero.</summary>
        std::array<SDL_Color, 256> Colors{};
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
        /// <summary>The shown colours of the previous call, to tell a settled palette from a fade.</summary>
        std::array<SDL_Color, 256> LastColors{};
        /// <summary>The colours every shape was last made in; false when a shape may be missing.</summary>
        std::array<SDL_Color, 256> ReadyColors{};
        bool Ready = false;
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

    bool sameColors(const std::array<SDL_Color, 256>& a, const std::array<SDL_Color, 256>& b)
    {
        return std::memcmp(a.data(), b.data(), sizeof(SDL_Color) * 256) == 0;
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

        if (made.Cursor != nullptr && sameColors(made.Colors, used))
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
        made = {fresh, used};

        if (wasCold)
        {
            cursor.Cold.push_back(std::move(fresh));
        }

        ++cursor.MadeCount;
        return true;
    }

    /// <summary>
    /// Takes the display's scale and colours. A new scale drops every cursor. Once the palette has settled (the
    /// same two calls running; not mid-fade) every shape gets its cursor in the colours now shown, so switching
    /// shapes in play only selects one.
    /// </summary>
    void prepare(MCCursorState& cursor, MCDisplay& display)
    {
        std::array<SDL_Color, 256> colors{};
        display.GetShownColors(colors);
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
                MCFrameLog::Note(std::format("cursor scale {}x{} density {} -> {}x{} density {}: all remade",
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
            cursor.Ready = false;
        }

        const bool settled = sameColors(colors, cursor.LastColors);
        cursor.LastColors = colors;

        if (!settled || (cursor.Ready && sameColors(colors, cursor.ReadyColors)))
        {
            return;
        }

        bool all = true;

        for (size_t i = 0; i < cursor.Shapes.size(); ++i)
        {
            if (cursor.Shapes[i].Width > 0)
            {
                all = ensure(cursor, cursor.ShapeCursors[i], cursor.Shapes[i], colors) && all;
            }
        }

        cursor.Ready = all;
        cursor.ReadyColors = colors;
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
    constexpr float Full = 255.0f;
    constexpr size_t Channels = 4;

    /// <summary>
    /// The colour (straight, 0..255) and coverage (0..255) of <paramref name="image"/>'s HD copy at bitmap pixel
    /// (x, y) of a bitmap scaled by (scaleX, scaleY): between the four HD pixels around its centre, corrected by the
    /// colour today's palette gives the original pixel under it.
    /// </summary>
    std::array<uint8_t, Channels> HdCursorPixel(const MCCursorImage& image, const std::array<SDL_Color, 256>& colors,
                                                float scaleX, float scaleY, int x, int y)
    {
        const MCCursorHd& hd = *image.Hd;
        const int width = image.Width * hd.Scale;
        const int height = image.Height * hd.Scale;
        const float atX = (static_cast<float>(x) + 0.5f) / scaleX * static_cast<float>(hd.Scale) - 0.5f;
        const float atY = (static_cast<float>(y) + 0.5f) / scaleY * static_cast<float>(hd.Scale) - 0.5f;
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

        // Today's colour of the original pixel under the centre over the one the pack was made from.
        std::array<float, 3> correction = {1.0f, 1.0f, 1.0f};
        const int originalX = std::min(image.Width - 1, static_cast<int>((static_cast<float>(x) + 0.5f) / scaleX));
        const int originalY = std::min(image.Height - 1, static_cast<int>((static_cast<float>(y) + 0.5f) / scaleY));
        const size_t original = static_cast<size_t>(originalY) * image.Width + originalX;
        constexpr size_t PaletteBytes = 768;

        if (hd.MadeWith.size() == PaletteBytes && image.Opaque[original] != 0)
        {
            const uint8_t index = image.Pixels[original];
            const SDL_Color now = colors[index];
            const std::array<uint8_t, 3> shown = {now.r, now.g, now.b};

            for (size_t c = 0; c < correction.size(); ++c)
            {
                const float then = std::max(static_cast<float>(hd.MadeWith[index * 3 + c]), 1.0f);
                correction[c] = std::clamp(static_cast<float>(shown[c]) / then, 0.0f, MaxCorrection);
            }
        }

        std::array<uint8_t, Channels> out{};

        for (size_t c = 0; c < correction.size(); ++c)
        {
            out[c] =
                static_cast<uint8_t>(std::lround(std::clamp(sum[c] / coverage * correction[c], 0.0f, 1.0f) * Full));
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
            for (int y = 0; y < bitmap.Height; ++y)
            {
                for (int x = 0; x < bitmap.Width; ++x)
                {
                    std::ranges::copy(HdCursorPixel(image, colors, scaleX, scaleY, x, y),
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
        cursor.Ready = false;
        // Made now in the colours shown now, not waiting for a settled palette: the game may show one at once.
        cursor.LastColors = {};

        if (const std::shared_ptr<MCDisplay> display = shownDisplay())
        {
            display->GetShownColors(cursor.LastColors);
            prepare(cursor, *display);
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

        // Mid-fade only the shape on screen follows the colours; all of them are remade once the palette settles.
        if (!ensure(cursor, made, cursor.Shapes[shape], cursor.LastColors))
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

        if (!ensure(cursor, cursor.Other, picture, cursor.LastColors))
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

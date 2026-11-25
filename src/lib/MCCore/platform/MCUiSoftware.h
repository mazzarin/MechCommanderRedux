#pragma once

#include "lib/MCOptionalRef.h"
#include "platform/MCSdlHandle.h"
#include "platform/MCUiDraws.h"

/// <summary>
/// Port-only (SPEC-menus.md, "How it's built"): the remade menus' draws (platform/MCUiDraws.h) drawn by the CPU, for
/// the software renderer (MCSdlPresenter): onto a picture of the window's size, transparent where nothing is drawn
/// (premultiplied RGBA8), with SDL's software renderer, which every machine runs alike. Rectangles (nearly every draw:
/// boxes, borders, pictures, letters) are drawn as SDL_RenderTexture copies of whole texels onto whole pixels (a picture
/// at another size from its scaled copy, <see cref="MCUiCopyOf"/>), the boxes of one colour by <see cref="MCUiFill"/>
/// (SDL's blend, sooner); other triangles with SDL_RenderGeometry, the untextured ones by <see cref="MCUiTriangle"/>
/// (SDL's pixels, sooner) unless SDL would take some as a rectangle. Colours are premultiplied and blended so
/// (<c>SDL_BLENDMODE_BLEND_PREMULTIPLIED</c>), a scissor is SDL's clip rectangle.
/// </summary>
/// <remarks>
/// A picture is never given to SDL as triangles: SDL takes RmlUi's two triangles of a box as a rectangle only when they
/// meet on the other diagonal, and its software textured triangle (SDL_triangle.c, SDL_SW_BlitTriangle) refuses one
/// whose texel places times its area pass 2^31, as a picture over most of a window's ("triangle area overflow"), which
/// its queue drops unsaid: the main menu's painting at 1280x720 was the stone alone.
/// <para>A frame's draws are mostly the last's (RmlUi keeps an element's geometry while it doesn't change), so only the
/// boxes where they differ are drawn again: cleared, and every draw over them drawn cut to them. That gives the pixels a
/// whole picture gives: SDL's software draws cut by a clip rectangle are its whole draws' pixels inside it (a scaled copy
/// is scaled whole, then cut; a triangle's pixels are found from their own places).</para>
/// </remarks>
class MCUiSoftware
{
public:
    MCUiSoftware();
    ~MCUiSoftware();
    MCUiSoftware(const MCUiSoftware&) = delete;
    MCUiSoftware& operator=(const MCUiSoftware&) = delete;

    /// <summary>
    /// Draws <paramref name="draws"/> onto a transparent picture of their window's size: wholly the first time, after
    /// <see cref="Forget"/> and when the window's size changes; else only where they differ from the draws drawn last
    /// (<see cref="Changed"/>).
    /// </summary>
    std::expected<void, std::string> Draw(const MCUiDraws& draws);

    /// <summary>The picture drawn last (RGBA32, premultiplied; none before the first).</summary>
    MCOptionalRef<const SDL_Surface> Picture() const { return MCOptionalOf<const SDL_Surface>(_Surface.get()); }

    /// <summary>The boxes of the picture the last <see cref="Draw"/> drew (they may overlap); none: it's as it was.</summary>
    const std::vector<SDL_Rect>& Changed() const { return _Changed; }

    /// <summary>The next <see cref="Draw"/> draws the whole picture.</summary>
    void Forget();

    /// <summary>The textures kept (tests: let go of with the pictures they copy).</summary>
    size_t TexturesKept() const { return _Textures.size(); }

private:
    /// <summary>The renderer's copy of <paramref name="texture"/>, made the first time it's drawn.</summary>
    std::expected<std::reference_wrapper<SDL_Texture>, std::string> TextureOf(const MCUiTexture& texture);
    /// <summary>
    /// The renderer's copy of <paramref name="source"/> of <paramref name="texture"/> scaled to <paramref name="width"/>
    /// x <paramref name="height"/> as SDL scales it (SDL_ScaleSurface, nearest or smooth as the picture asks), made the
    /// first time it's drawn so: copied whole texel for texel after, as SDL draws a scaled copy it cuts (scaled whole,
    /// then cut), so the same pixels, without scaling the whole each time.
    /// </summary>
    std::expected<std::reference_wrapper<SDL_Texture>, std::string> ScaledOf(
        const std::shared_ptr<const MCUiTexture>& texture, const SDL_Rect& source, int width, int height);
    /// <summary>A picture of <paramref name="width"/> x <paramref name="height"/> and its renderer, made anew.</summary>
    std::expected<void, std::string> MakePicture(int width, int height);
    /// <summary>
    /// <paramref name="box"/> cleared and every draw of <paramref name="draws"/> over it drawn again, cut to it.
    /// </summary>
    std::expected<void, std::string> DrawBox(const MCUiDraws& draws, const SDL_Rect& box);
    /// <summary>One draw cut to <paramref name="clip"/>.</summary>
    std::expected<void, std::string> DrawOne(const MCUiDraw& draw, const SDL_Rect& clip);
    /// <summary>Runs SDL's queued draws, before the picture is drawn on or read directly.</summary>
    std::expected<void, std::string> RunQueue();
    /// <summary>Lets go of the copies of pictures nothing holds any more.</summary>
    void Prune();

    struct MCKept
    {
        std::weak_ptr<const MCUiTexture> Picture;
        MCSdlHandle<SDL_Texture> Texture;
    };

    /// <summary>A scaled copy's picture, the part of it scaled and the size it's scaled to.</summary>
    struct MCScaledKey
    {
        uint64_t Picture = 0;
        SDL_Rect Source{};
        int Width = 0;
        int Height = 0;

        bool operator==(const MCScaledKey& other) const
        {
            return Picture == other.Picture && Source.x == other.Source.x && Source.y == other.Source.y &&
                   Source.w == other.Source.w && Source.h == other.Source.h && Width == other.Width &&
                   Height == other.Height;
        }
    };

    struct MCScaledHash
    {
        size_t operator()(const MCScaledKey& key) const;
    };

    MCSdlHandle<SDL_Surface> _Surface;
    MCSdlHandle<SDL_Renderer> _Renderer;
    /// <summary>A white texel: an untextured triangle is it, coloured.</summary>
    MCSdlHandle<SDL_Texture> _White;
    /// <summary>The pictures' copies, by their number; each let go of once its picture is gone.</summary>
    std::unordered_map<uint64_t, MCKept> _Textures;
    /// <summary>
    /// The pictures' scaled copies (<see cref="ScaledOf"/>); each let go of once its picture is gone, all of them once
    /// they hold more than a few pictures of the window's size.
    /// </summary>
    std::unordered_map<MCScaledKey, MCKept, MCScaledHash> _Scaled;
    size_t _ScaledBytes = 0;
    /// <summary>
    /// The draws drawn last, kept: their geometry and pictures held, so a new one never has an old one's address.
    /// </summary>
    std::vector<MCUiDraw> _Drawn;
    /// <summary>Whether the next draw is drawn whole (nothing drawn yet, or forgotten).</summary>
    bool _Whole = true;
    std::vector<SDL_Rect> _Changed;
    /// <summary>Whether SDL's renderer has draws queued that haven't run.</summary>
    bool _Queued = false;
    /// <summary>The window pixels each draw being drawn may draw on (<see cref="MCUiFootprint"/>).</summary>
    std::vector<SDL_Rect> _Footprints;
    /// <summary>A draw's pieces (scratch, kept for its room).</summary>
    std::vector<MCUiPiece> _Pieces;
    std::vector<SDL_Vertex> _Vertices;
};

/// <summary>
/// The window boxes where <paramref name="now"/>'s pixels may differ from <paramref name="before"/>'s, both over the
/// same window: where a draw not in both, in the same order, may draw (each draw's footprint: its vertices' box, cut by
/// its scissor and the window). The rest of the window has the same draws in the same order, so the same pixels.
/// </summary>
std::vector<SDL_Rect> MCUiChangedBoxes(std::span<const MCUiDraw> before, std::span<const MCUiDraw> now,
                                       const SDL_Rect& window);

/// <summary>The window pixels <paramref name="draw"/> may draw on, inside <paramref name="window"/> (empty: none).</summary>
SDL_Rect MCUiFootprint(const MCUiDraw& draw, const SDL_Rect& window);

/// <summary>
/// How the software path copies a quad's texels onto its target (<see cref="MCUiCopyOf"/>): its texture's whole texels
/// round them (<see cref="Part"/>), scaled to <see cref="Width"/> x <see cref="Height"/> (their own size: copied as they
/// are), and the target's pixels taken from that at <see cref="From"/> (in the part scaled).
/// </summary>
struct MCUiCopy
{
    SDL_Rect Part{};
    int Width = 0;
    int Height = 0;
    SDL_Rect From{};
};

/// <summary>
/// The copy of <paramref name="texels"/> (inside their texture) onto <paramref name="target"/>: whole texels are the
/// part itself, scaled to the target's size; texels cut between texels (a picture scaled to its box and cut by it, as a
/// cover decorator's) the whole texels round them, scaled at the target's pixels a texel, the cut's place in them
/// rounded to a pixel.
/// </summary>
MCUiCopy MCUiCopyOf(const SDL_FRect& texels, const SDL_Rect& target);

/// <summary>
/// The vertices the software path gives SDL_RenderGeometry for <paramref name="piece"/>'s triangles of
/// <paramref name="draw"/>, into <paramref name="vertices"/>: corners on whole pixels, colours straight (SDL's software
/// triangles blend straight alpha), the texture's places as recorded.
/// </summary>
void MCUiSdlTriangles(const MCUiDraw& draw, const MCUiPiece& piece, std::vector<SDL_Vertex>& vertices);

/// <summary>
/// Whether SDL's software renderer takes some of <paramref name="vertices"/>' triangles (given SDL_RenderGeometry
/// without indices, textured) as a rectangle, so not as triangles: SDL_SW_RenderGeometryRaw's walk and test, as they
/// stand (two in a row sharing two corners, which name an upright rectangle's in one colour; the last's third corner
/// may lie anywhere).
/// </summary>
bool MCUiSdlMakesRectangles(std::span<const SDL_Vertex> vertices);

/// <summary>
/// An untextured triangle over <paramref name="picture"/> (RGBA32), cut to <paramref name="clip"/>, as SDL's software
/// renderer draws one through a white texel blended straight (SDL_SW_BlitTriangle, SDL_BLENDMODE_BLEND): its corners
/// and colours taken as SDL takes them, the same pixels found (half-pixel fixed point, the top-left rule), each one's
/// colour between the corners' rounded down, blended as its slow path blends it. The same bytes, several times sooner.
/// </summary>
void MCUiTriangle(SDL_Surface& picture, const SDL_Rect& clip, std::span<const SDL_Vertex, 3> corners);

/// <summary>
/// A box of one colour (premultiplied RGBA) over <paramref name="box"/> of <paramref name="picture"/> (RGBA32), blended
/// as SDL's software renderer blends a texel of it (<c>SDL_BLENDMODE_BLEND_PREMULTIPLIED</c>, SDL_blit_auto.c's
/// arithmetic): each channel <c>min(255, colour + (255 - alpha) x picture / 255)</c>, rounded as SDL's MULT_DIV_255.
/// The same bytes, several times sooner.
/// </summary>
void MCUiFill(SDL_Surface& picture, const SDL_Rect& box, const std::array<uint8_t, 4>& color);

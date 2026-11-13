#pragma once

/// <summary>
/// An 8-bit cursor picture: palette indices, which of them show, and the hot spot (the pixel the mouse position is
/// on). The hot spot is always inside the picture.
/// </summary>
/// <summary>
/// Port-only: a cursor picture's HD copy (the HD art pack's frame of its shape): premultiplied RGBA at <see cref="Scale"/>
/// times the picture's size, and the colours the pack was made from (768 bytes; none: shown as made), by which today's
/// palette corrects it as the GPU corrects the pack's frames.
/// </summary>
struct MCCursorHd
{
    int Scale = 1;
    std::vector<uint8_t> Rgba;
    std::vector<uint8_t> MadeWith;
};

struct MCCursorImage
{
    int Width = 0;
    int Height = 0;
    int HotX = 0;
    int HotY = 0;
    /// <summary>Palette indices, <see cref="Width"/> per row.</summary>
    std::vector<uint8_t> Pixels;
    /// <summary>1 where the pixel shows, 0 where it is transparent; as <see cref="Pixels"/>.</summary>
    std::vector<uint8_t> Opaque;
    /// <summary>
    /// Port-only: the picture's HD copy, shown instead of its pixels (smoothly, at the size it is shown); none for a
    /// picture the pack has no frame of. A picture made from others (an overlay, a grown one) has none.
    /// </summary>
    std::shared_ptr<const MCCursorHd> Hd;

    bool operator==(const MCCursorImage&) const = default;

    /// <summary>A transparent picture of <paramref name="width"/> x <paramref name="height"/>.</summary>
    static MCCursorImage Blank(int width, int height, int hotX, int hotY);

    /// <summary>
    /// <paramref name="over"/> drawn on <paramref name="under"/> with their hot spots on the same point; the result
    /// is as large as both need.
    /// </summary>
    static MCCursorImage Overlay(const MCCursorImage& under, const MCCursorImage& over);

    /// <summary>The picture grown with transparent pixels until it holds its hot spot.</summary>
    static MCCursorImage WithHotSpotInside(const MCCursorImage& image);
};

/// <summary>A cursor picture in colour at the size it is shown: RGBA bytes, <see cref="Width"/> * 4 per row.</summary>
struct MCCursorBitmap
{
    int Width = 0;
    int Height = 0;
    int HotX = 0;
    int HotY = 0;
    std::vector<uint8_t> Rgba;
};

/// <summary>
/// The game's cursor shown as the system (hardware) cursor, so it follows the mouse at the desktop's rate instead
/// of being drawn into the frame. The picture is scaled as the screen is (nearest pixel) and coloured with the
/// display's palette.
/// </summary>
/// <remarks>Threads: the thread that created the display (as <see cref="MCDisplay::Present"/>).</remarks>
namespace MCCursor
{
    /// <summary>
    /// Sets the game's cursor shapes and makes all their SDL cursors now. They are remade only for a new screen scale
    /// or when the palette settles on new colours for them (after a fade), a millisecond's worth a frame (the shape
    /// shown at once), so switching shapes in play only selects one. Each is also shown once, unseen, the first time
    /// the mouse is over the window: Windows builds a cursor's icon then, and that is the slow part.
    /// </summary>
    void Preload(std::vector<MCCursorImage> shapes);

    /// <summary>Makes preloaded shape <paramref name="shape"/> the cursor; cheap to call every frame.</summary>
    void ShowShape(size_t shape);

    /// <summary>
    /// Makes <paramref name="image"/>, not a preloaded shape (a dragged item over the cursor), the cursor over the
    /// attached display's window. Its SDL cursor is remade whenever the picture changes; cheap while it doesn't.
    /// </summary>
    void Show(const MCCursorImage& picture);

    /// <summary>How many SDL cursors have been made (tests).</summary>
    uint64_t CursorsMade();

    /// <summary>How many made cursors Windows hasn't built yet (tests).</summary>
    size_t ColdCursors();

    /// <summary>Shows no game cursor (the game hid its own).</summary>
    void Hide();

    /// <summary>Frees the SDL cursor (before the display goes).</summary>
    void Shutdown();

    /// <summary>
    /// <paramref name="image"/> in <paramref name="colors"/> scaled by <paramref name="scaleX"/> x
    /// <paramref name="scaleY"/> (nearest pixel), transparent pixels alpha 0; or, when it has an HD copy, that copy
    /// sampled between its pixels and changed as today's colours changed the pack's. The hot spot lands on the
    /// top-left of its scaled pixel.
    /// </summary>
    /// <remarks>
    /// Port-only, the HD copy's colours: where today's palette is the pack's through one curve (the game's brightness
    /// steps, the black between screens), every colour of the copy goes through that curve; where it isn't (a
    /// screen's own palette), the copy is recoloured as the picture's own colours changed, each HD colour by the
    /// original pixels around it that it is like. The water's entries, which the game rewrites as the water moves, are
    /// left out of the curve.
    /// </remarks>
    MCCursorBitmap Rasterize(const MCCursorImage& image, const std::array<SDL_Color, 256>& colors, float scaleX,
                             float scaleY);
}

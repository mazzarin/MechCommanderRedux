#pragma once

/// <summary>
/// Port-only (SPEC-menus.md, "How it's built"): the remade menus' draws for one frame, as RmlUi made them (indexed,
/// coloured, textured triangles with premultiplied alpha and a scissor; ui/MCRmlRecorder records them), drawn over the
/// whole window as the last thing before the frame is shown: by the GPU renderer's presenter (MCVulkanPresenter) or the
/// software renderer's (MCSdlPresenter). Recorded on the game's thread and drawn after it (the GPU's frames run ahead
/// on a thread of their own), so nothing recorded changes once handed over.
/// </summary>
/// <remarks>
/// Every corner lands on a whole window pixel (<see cref="MCUiPlace"/>), so both renderers draw the same pixels.
/// </remarks>

/// <summary>A vertex: its place (window pixels, before its draw's move), premultiplied colour and texture place.</summary>
struct MCUiVertex
{
    float X = 0.0f;
    float Y = 0.0f;
    std::array<uint8_t, 4> Color{};
    float U = 0.0f;
    float V = 0.0f;
};

/// <summary>Triangles: three indices into <see cref="Vertices"/> each.</summary>
struct MCUiGeometry
{
    std::vector<MCUiVertex> Vertices;
    std::vector<int32_t> Indices;
};

/// <summary>A picture the draws sample: RGBA8, premultiplied, row after row.</summary>
struct MCUiTexture
{
    /// <summary>A number no other picture in the run has (the renderers keep their copies by it).</summary>
    uint64_t Id = 0;
    int32_t Width = 0;
    int32_t Height = 0;
    std::vector<uint8_t> Pixels;
    /// <summary>Filtered smoothly when drawn at another size (an HD picture); else its nearest pixel is taken.</summary>
    bool Smooth = false;
};

/// <summary>One draw: its triangles moved by (<see cref="X"/>, <see cref="Y"/>), coloured, or textured and coloured.</summary>
struct MCUiDraw
{
    std::shared_ptr<const MCUiGeometry> Geometry;
    float X = 0.0f;
    float Y = 0.0f;
    /// <summary>None: the vertices' colours alone.</summary>
    std::shared_ptr<const MCUiTexture> Texture;
    /// <summary>The window pixels it may draw on; none: all.</summary>
    std::optional<SDL_Rect> Scissor;
};

/// <summary>A frame's draws over a window of <see cref="Width"/> x <see cref="Height"/> pixels, in order.</summary>
struct MCUiDraws
{
    int32_t Width = 0;
    int32_t Height = 0;
    std::vector<MCUiDraw> Draws;
};

/// <summary>The window pixel edge a vertex's coordinate <paramref name="at"/> moved by <paramref name="by"/> lands on.</summary>
int32_t MCUiPlace(float at, float by);

/// <summary>
/// An axis-aligned rectangle two of a draw's triangles make, in one colour: where it lands in the window, and the texels
/// of the draw's texture it shows, when it has one and they are whole texels the right way round.
/// </summary>
struct MCUiQuad
{
    SDL_Rect Target{};
    std::optional<SDL_Rect> Source;
    std::array<uint8_t, 4> Color{};
};

/// <summary>
/// A run of a draw's triangles: one rectangle (<see cref="Quad"/>), or triangles drawn as they are (indices
/// <see cref="First"/> to <see cref="First"/> + <see cref="Count"/>).
/// </summary>
struct MCUiPiece
{
    std::optional<MCUiQuad> Quad;
    size_t First = 0;
    size_t Count = 0;
};

/// <summary>
/// The draw's triangles in order, triangles <c>2k</c> and <c>2k + 1</c> taken as a rectangle where they make one (RmlUi
/// draws boxes, borders, pictures and letters so), the rest as runs of triangles.
/// </summary>
std::vector<MCUiPiece> MCUiPieces(const MCUiDraw& draw);

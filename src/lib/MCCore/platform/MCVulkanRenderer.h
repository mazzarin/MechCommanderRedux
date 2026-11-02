#pragma once

#include "platform/MCPresenter.h"
#include "platform/MCSdlHandle.h"
#include "platform/MCSharpText.h"
#include "platform/MCSoftwareRenderer.h"

#include <bitset>
#include <unordered_set>

namespace MCHdArt
{
    struct Frame;
}

/// <summary>
/// The hardware renderer: draws the frame surfaces (the screen and the world view's surface, see
/// <see cref="MCRenderer::AddFrameSurface"/>) on the GPU, through SDL GPU on Vulkan, on the presenter's device.
/// </summary>
/// <remarks>
/// <para>Commands are recorded as they come (resolved into draws: shaders/instance.vshader's <c>Draw</c>) and run when the
/// frame is presented (<see cref="Execute"/>), in order, so the presenter composites what they drew. What a command
/// reads is taken at the time of the call, as the software renderer reads it: shapes, tiles, fast shapes and glyphs are
/// decoded into an atlas (value + state), kept by the address they're read from in a registered data block until its
/// owner says the bytes changed (<see cref="MCRenderer::RegisterData"/>); pictures are their textures
/// (<see cref="MCTexture"/>); tables get a row of the table texture, kept by their address in the same way.</para>
/// <para>A surface holds colours (premultiplied RGBA, what the composite shows) and palette indices (what draws that
/// read a surface read, see shaders/draw.pshader). Opaque draws write both, with the frame's palette; translucent and
/// table-mapped draws are alpha blended into the colours by the GPU's blend (AlphaPal.ini's colours, not AlphaTable's)
/// and never read the target. A copy within a surface reads a copy of its indices taken before it; consecutive such
/// draws that don't touch each other's pixels share one copy.</para>
/// <para>Integer arithmetic throughout: the GPU writes the software renderer's indices exactly wherever nothing was
/// blended. Mirror mode (<see cref="MCGpuDrawing::Mirror"/>) draws both and compares them every frame
/// (<see cref="Compare"/>), leaving out the pixels blended draws covered.</para>
/// </remarks>
class MCVulkanRenderer final : public MCRenderer
{
    /// <summary>Only <see cref="Create"/> makes one.</summary>
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>A renderer with no device yet.</summary>
    explicit MCVulkanRenderer(Key) {}

    /// <summary>Makes the renderer's pipelines and fixed textures on <paramref name="device"/>.</summary>
    static std::expected<std::unique_ptr<MCVulkanRenderer>, std::string> Create(std::shared_ptr<SDL_GPUDevice> device);

    ~MCVulkanRenderer() override;

    void Clear(MCWindow& target, const MCRect& rect, uint8_t color) override;
    void Hash(MCWindow& target, const MCRect& rect, uint8_t color) override;
    void Copy(MCWindow& target, const MCCopyCommand& command) override;
    void AlphaBlit(MCWindow& target, const MCAlphaBlitCommand& command) override;
    void ShapeBlit(MCWindow& target, const MCShapeBlitCommand& command) override;
    void Write(MCWindow& target, int32_t x, int32_t y, std::span<const uint8_t> pixels) override;
    void Pixel(MCWindow& target, int32_t x, int32_t y, uint8_t color) override;
    void Shape(MCWindow& target, const MCShapeCommand& command) override;
    void FastShape(MCWindow& target, const MCFastShapeCommand& command) override;
    void Tile(MCWindow& target, const MCTileCommand& command) override;
    void Polygon(MCWindow& target, const MCPolygonCommand& command) override;
    void MapQuad(MCWindow& target, const MCMapQuadCommand& command) override;
    void Line(MCWindow& target, const MCLineCommand& command) override;
    void Ellipse(MCWindow& target, const MCEllipseCommand& command) override;
    void StatusBar(MCWindow& target, const MCStatusBarCommand& command) override;
    void Shadow(MCWindow& target, const MCShadowCommand& command) override;
    void Particle(MCWindow& target, const MCParticleCommand& command) override;
    void Glyph(MCWindow& target, const MCGlyphCommand& command) override;
    /// <summary>
    /// Draws the frame's terrain tiles from the mesh in one draw (shaders/terrain.vshader), uploading the mesh and its
    /// tiles when it is new; the pass's tile commands are then left. An error when the mesh can't be held or the fog of
    /// war isn't a frame surface.
    /// </summary>
    std::expected<void, std::string> TerrainLayer(MCWindow& target, const MCTerrainFrame& frame) override;
    void EndTerrainLayer(MCWindow& target) override;

    /// <summary>
    /// The colours the next <see cref="Execute"/> draws with: the palette as shown (gamma applied, 256 entries; empty
    /// keeps the last) and the colour cycle shown over it.
    /// </summary>
    void SetColors(std::span<const SDL_Color> palette, const MCColorCycle& cycle);

    /// <summary>
    /// The output pixels per screen pixel across and down the underlays are shown at (above 1 when the screen is shown
    /// scaled up but its underlays keep the window's resolution): from the next <see cref="Execute"/> on, an underlay's
    /// surface is drawn at its rectangle's size times these.
    /// </summary>
    void SetUnderlayScale(float scaleX, float scaleY)
    {
        _UnderlayScaleX = scaleX;
        _UnderlayScaleY = scaleY;
    }

    /// <summary>
    /// Port-only (<see cref="MCFrame::SharpScreen"/>): from the next <see cref="Execute"/> on, <paramref name="screen"/>'s
    /// surface is drawn at <paramref name="scaleX"/> x <paramref name="scaleY"/> output pixels a window pixel, from
    /// its corner to the shown view's far corner (<paramref name="right"/>, <paramref name="bottom"/>, exclusive: a
    /// 640x480 screen draws only that much of the screen); scales of 1 (or no screen) draw it at its window's size.
    /// </summary>
    void SetScreenScale(const MCWindow* screen, float scaleX, float scaleY, int32_t right, int32_t bottom);

    /// <summary>
    /// Runs the commands recorded since the last call on <paramref name="commands"/>: uploads what they read, then
    /// draws into the surfaces. The kept surfaces (the fog of war the terrain reads) are drawn first, then the
    /// underlays (<paramref name="underlays"/>), then the rest. On a surface shown over an underlay the key colour is
    /// drawn transparent.
    /// </summary>
    std::expected<void, std::string> Execute(SDL_GPUCommandBuffer& commands, std::span<const MCUnderlay> underlays);

    /// <summary>
    /// Runs the commands recorded so far (<see cref="Execute"/>) in a command buffer of its own: before a read of the
    /// surfaces ahead of the frame's present, or for a frame that isn't shown.
    /// </summary>
    std::expected<void, std::string> Flush(std::span<const MCUnderlay> underlays);

    /// <summary>
    /// The colour texture of <paramref name="window"/>'s surface (premultiplied RGBA, as <see cref="Execute"/> left it)
    /// and its size in use; null when the window has no surface yet.
    /// </summary>
    std::shared_ptr<SDL_GPUTexture> SurfaceTexture(const MCWindow& window, uint32_t& width, uint32_t& height) const;

    /// <summary>
    /// Port-only (HD lighting's living water): the palette indices of <paramref name="window"/>'s surface (R8, the size
    /// of its colour texture), as <see cref="Execute"/> left them; null when the window has no surface yet.
    /// </summary>
    std::shared_ptr<SDL_GPUTexture> SurfaceIndexTexture(const MCWindow& window) const;

    /// <summary>
    /// Port-only (HD weather's drops catching the light): the light the raindrops on <paramref name="window"/>'s
    /// surface caught (premultiplied RGBA, the size of its colour texture: Surface::Glints), as <see cref="Execute"/>
    /// left it; null when the window has no surface yet.
    /// </summary>
    std::shared_ptr<SDL_GPUTexture> SurfaceGlintTexture(const MCWindow& window) const;

    /// <summary>
    /// The drawn pixels per window pixel of <paramref name="window"/>'s surface across and down (above 1 for a screen
    /// drawn at the size it's shown, <see cref="SetScreenScale"/>); 1, 1 when it has none.
    /// </summary>
    std::pair<float, float> SurfaceScale(const MCWindow& window) const;

    /// <summary>
    /// <paramref name="screen"/> in palette indices with its layout, for screenshots and tests: flushes, reads the
    /// indices of the screen's surface and its underlays' back (waiting for the GPU), and puts the world pixel under
    /// each key pixel's centre over an underlay. Blended colours aren't indices: their pixels keep the index under
    /// them. Empty when the screen has no surface yet.
    /// </summary>
    std::expected<std::vector<uint8_t>, std::string> ReadShown(const MCWindow& screen,
                                                               std::span<const MCUnderlay> underlays);

    /// <summary>The result of <see cref="Compare"/>.</summary>
    struct Comparison
    {
        /// <summary>Pixels whose index differs, over every surface (those blended draws covered left out).</summary>
        int64_t Different = 0;
        /// <summary>What differed first, for the log ("" when nothing did).</summary>
        std::string First;
    };

    /// <summary>
    /// Mirror mode: downloads every surface's indices (waiting for the GPU) and compares them with the window's own
    /// pixels, which the software renderer drew from the same commands, except where the last frame's blended draws
    /// went (the software renderer maps those pixels through AlphaTable, the GPU blends their colours).
    /// </summary>
    std::expected<Comparison, std::string> Compare(std::span<const MCUnderlay> underlays,
                                                   std::span<const SDL_Color> colors);

    /// <summary>The comparisons so far (<see cref="Compare"/> keeps them, and logs the first frames that differ).</summary>
    struct MirrorTally
    {
        int64_t Frames = 0;
        int64_t DifferentFrames = 0;
        /// <summary>The first frame that differed (its number among those compared) and what differed first in it.</summary>
        int64_t FirstFrame = -1;
        std::string First;
        Comparison Last;
    };

    const MirrorTally& Mirror() const { return _Mirror; }

    /// <summary>Starts the tally again.</summary>
    void ResetMirror() { _Mirror = MirrorTally{}; }

    /// <summary>
    /// What a frame sent to the GPU besides its draws and tables: new pictures (whole textures from their pixels),
    /// new movie frames (stream textures' locked rectangles, decoded on the CPU straight into upload memory: the one
    /// upload expected every frame), and new atlas images.
    /// </summary>
    struct UploadTally
    {
        int64_t MovieFrames = 0;
        int64_t MovieBytes = 0;
        int64_t Pictures = 0;
        int64_t PictureBytes = 0;
        int64_t AtlasImages = 0;
        int64_t AtlasBytes = 0;
    };

    /// <summary>The uploads of the last frame <see cref="Execute"/> ran.</summary>
    const UploadTally& LastFrameUploads() const { return _LastFrameUploads; }

    /// <summary>The terrain layers drawn from the mesh so far.</summary>
    int64_t TerrainLayersDrawn() const { return _TerrainLayersDrawn; }

    /// <summary>The shapes drawn from the HD art pack so far (<see cref="MCHdArt"/>).</summary>
    int64_t HdDrawn() const { return _HdDrawn; }
    /// <summary>Whether the ground is drawn with the HD art pack's colours (it has an HD terrain atlas).</summary>
    bool HdTerrain() const { return _TerrainMesh.HdAtlas.Handle != nullptr; }

    /// <summary>The commands the GPU can't draw yet that were asked of it (each logged once).</summary>
    const std::map<std::string, int64_t>& Unsupported() const { return _Unsupported; }

    /// <summary>
    /// Port-only, for the HD art audit (a test tool): one draw command and what it drew. Sharp: shown at the output's
    /// resolution (from the HD art pack, the ground's HD atlas, text redrawn sharp, a line or ellipse drawn smooth, or a
    /// mech's shadow with smooth edges), not from the original's pixels.
    /// </summary>
    struct AuditedDraw
    {
        std::string_view Command;
        /// <summary>The window drawn on, and whether its surface is drawn at more pixels than the window has.</summary>
        const MCWindow* Window = nullptr;
        bool Scaled = false;
        /// <summary>
        /// The data it drew (a shape table, a tile, a font...; to the end of its registered block) and the frame, or the
        /// window it copied from, where it has them.
        /// </summary>
        std::span<const uint8_t> Data;
        int32_t Frame = -1;
        const MCWindow* From = nullptr;
        /// <summary>The window pixels its draws covered.</summary>
        int64_t Pixels = 0;
        bool Sharp = false;
    };

    /// <summary>Port-only: called after every draw command while set (the HD art audit's).</summary>
    std::function<void(const AuditedDraw&)> Audit;

    /// <summary>
    /// Port-only: called as the renderer starts recording a frame (its first command), for the presenter to place the
    /// screen (<see cref="SetScreenScale"/>) as the display will show the frame. The draws then know a sharp screen
    /// from the frame it first is one (HD art just turned on, the game's first frame), not one frame on.
    /// </summary>
    std::function<void()> RecordingStarts;

protected:
    /// <summary>The alpha colours go up again before the next draws.</summary>
    void OnAlphaTableChanged() override { _AlphaColorsChanged = true; }
    /// <summary>Drops the images and table rows made from the changed or freed bytes (atlas places are left until it
    /// starts again).</summary>
    void OnDataChanged(MCAddressKey first, size_t size) override;

    /// <summary>The surface is let go: its commands still run, then its textures go (after the next execution).</summary>
    void OnFrameSurfaceRemoved(const MCWindow& window) override;

private:
    /// <summary>A texture and its allocated size.</summary>
    struct Texture
    {
        /// <summary>Held too by the uploads and the frame's pictures that use it until the frame has run.</summary>
        std::shared_ptr<SDL_GPUTexture> Handle;
        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    /// <summary>A frame surface on the GPU.</summary>
    struct Surface
    {
        /// <summary>The window's address (none once it is let go), and the window while it is a frame surface.</summary>
        MCAddressKey Key;
        std::weak_ptr<MCWindow> Window;
        /// <summary>
        /// The surface's colours (premultiplied RGBA) and indices, both colour targets, and the copy of the indices
        /// draws that read the surface itself read. The indices are RGBA8: R the palette index; port-only (HD
        /// lighting's relief maps), G and B the slope of the HD ground drawn there and A 1 where it is (MCHdArt's
        /// relief maps: what draws over the ground writes 0, what blends over it leaves the ground's).
        /// </summary>
        Texture Target;
        Texture Index;
        Texture Copy;
        /// <summary>
        /// Port-only (HD weather): a third colour target, the light the raindrops caught (MCParticleCommand::Glint,
        /// premultiplied RGBA), which the composite adds over the lit world. Every draw covers it as it covers the
        /// colours (an opaque one clears it), so a unit in front of a drop hides its light.
        /// </summary>
        Texture Glints;
        /// <summary>The window rectangles blended draws covered in the frame being recorded, and in the last one run.</summary>
        std::vector<MCRect> Blended;
        std::vector<MCRect> LastBlended;
        /// <summary>The size the commands recorded last saw (the window's).</summary>
        uint32_t RecordedWidth = 0;
        uint32_t RecordedHeight = 0;
        /// <summary>
        /// The part of the window the drawn size covers, from its corner: the whole window, but for a screen drawn at the
        /// size it's shown (<see cref="SetScreenScale"/>), only as far as the view's far corner.
        /// </summary>
        uint32_t MappedWidth = 0;
        uint32_t MappedHeight = 0;
        /// <summary>
        /// The size it is drawn at: the window's, or for a surface shown as an underlay, the rectangle it's shown in
        /// (in output pixels: times the underlay scale, <see cref="SetUnderlayScale"/>). Its draws are scaled into
        /// that (shaders/draw.pshader), so the zoom costs nothing and the composite shows it pixel for pixel.
        /// </summary>
        uint32_t DrawnWidth = 0;
        uint32_t DrawnHeight = 0;
        /// <summary>Execution: the part of the surface the copy holds, and what was drawn since it was taken.</summary>
        MCRect Copied{0, 0, -1, -1};
        std::vector<MCRect> DrawnSinceCopy;
    };

    /// <summary>A draw's rectangle (x0, y0, x1, y1), or where it reads from and its steps (x, y, step x, step y).</summary>
    using Quad = std::array<int32_t, 4>;

    /// <summary>What a draw reads its pixels from.</summary>
    enum class SourceKind : uint8_t
    {
        None,
        /// <summary>An atlas page (<c>SourceIndex</c>).</summary>
        Atlas,
        /// <summary>A picture's texture (<c>SourceIndex</c> into the frame's picture list).</summary>
        Picture,
        /// <summary>Another surface (<c>SourceIndex</c>).</summary>
        Surface,
        /// <summary>The target's own copy (a copy within the surface).</summary>
        TargetCopy,
        /// <summary>The frame's strip of written pixels (<see cref="Write"/>).</summary>
        Strip
    };

    /// <summary>
    /// Port-only: the draw shader's build a draw goes through (VariantOf): the one for every kind, or the one for the
    /// original's own kinds alone (shaders/draw_basic.pshader) or for HD art frames alone (draw_hd.pshader).
    /// </summary>
    enum class DrawVariant : uint8_t
    {
        Every,
        Basic,
        Hd
    };

    /// <summary>The draw variants.</summary>
    static constexpr size_t DrawVariants = 3;

    /// <summary>One recorded command: a run of draws with the same target and bindings, or a surface resize.</summary>
    struct Record
    {
        bool Resize = false;
        uint16_t SurfaceIndex = 0;
        SourceKind Source = SourceKind::None;
        uint32_t SourceIndex = 0;
        /// <summary>Whether the draws are blended into the colours (the blend pipeline), or opaque.</summary>
        bool Blend = false;
        /// <summary>Whether the draws write the indices alone (an HD art frame draws the colours).</summary>
        bool IndexOnly = false;
        /// <summary>Port-only: whether the draws are shadows (HD lighting), taking the colours' alpha down alone.</summary>
        bool Shadow = false;
        /// <summary>
        /// Port-only: whether the draws write the indices' alpha alone (HDSway: a forest's HD frame marking the sway;
        /// with IndexOnly, an HD frame's mark pass: HdMarks::Standing).
        /// </summary>
        bool Marks = false;
        /// <summary>Port-only: the draw shader's build the draws go through.</summary>
        DrawVariant Variant = DrawVariant::Every;
        /// <summary>Whether the draws read the target's copy, and the part of the target they read.</summary>
        bool ReadsCopy = false;
        MCRect Reads{0, 0, -1, -1};
        /// <summary>The part of the target drawn.</summary>
        MCRect Writes{0, 0, -1, -1};
        uint32_t First = 0;
        uint32_t Count = 0;
        /// <summary>A terrain layer (an index into the frame's terrain draws), or -1.</summary>
        int32_t TerrainDraw = -1;
    };

    /// <summary>A frame's terrain layer: where it goes, the fog it reads and the numbers that place it.</summary>
    struct TerrainDraw
    {
        uint16_t FogIndex = 0;
        MCTerrainFrame Frame;
        uint32_t HazeRows[3] = {};
        /// <summary>The mesh's cells drawn (the rows of the grid).</summary>
        uint32_t FirstCell = 0;
        uint32_t CellCount = 0;
    };

    /// <summary>The terrain mesh on the GPU: its tiles' atlas, its cells and images (shaders/terrain.vshader).</summary>
    struct TerrainMeshState
    {
        uint64_t Version = 0;
        bool Usable = false;
        Texture Atlas;
        /// <summary>
        /// The HD art pack's tiles at HdScale, in shelves across MaxHdAtlasSide-wide atlases: a big map's need the
        /// second (none without HD terrain). Each image says which, and where (shaders/terrain.vshader).
        /// </summary>
        Texture HdAtlas;
        Texture HdAtlas2;
        int32_t HdScale = 0;
        /// <summary>
        /// Port-only (HD lighting's relief maps, MCLighting::ReliefMaps): the tiles' relief maps at ReliefScale, BC5,
        /// shelved as the HD tiles are (none without them).
        /// </summary>
        Texture ReliefAtlas;
        Texture ReliefAtlas2;
        int32_t ReliefScale = 0;
        /// <summary>Whether it was made for the relief maps (ReliefMapsInUse), so turning them on or off remakes it.</summary>
        bool Relief = false;
        /// <summary>The HD art pack it was made with (HdPackInUse; 0: none), so a change of pack remakes it.</summary>
        uint64_t HdPack = 0;
        std::shared_ptr<SDL_GPUBuffer> Cells;
        std::shared_ptr<SDL_GPUBuffer> Images;
        int32_t FirstRow = 0;
        int32_t FirstCol = 0;
        int32_t Cols = 0;
        int32_t Rows = 0;
    };

    /// <summary>The surface for <paramref name="window"/> (made on first use); records a resize when its size changed.</summary>
    uint16_t SurfaceFor(const MCWindow& window);
    /// <summary>Starts a frame's recording, if this is its first command.</summary>
    void BeginRecording();
    /// <summary>
    /// Adds a draw to the frame; returns false (and draws nothing) when its rectangle is empty. With
    /// <paramref name="join"/>, the draw belongs to the same command as the one before and touches none of its pixels,
    /// so it can read the same copy of the target.
    /// </summary>
    bool Add(uint16_t surface, SourceKind source, uint32_t sourceIndex, const Quad& rect,
             const std::optional<Quad>& sourceAt, uint32_t op, uint32_t color, uint32_t before, uint32_t after,
             bool join = false);
    /// <summary>
    /// Port-only: the draw shader's build a draw with <paramref name="op"/> goes through: the basic one for the
    /// original's own kinds (a fill, hash, texture, Gouraud span or texel walk, without smooth edges), the HD one for an
    /// HD art frame (KindHd, KindHdSmooth), else the one for every kind.
    /// </summary>
    static DrawVariant VariantOf(uint32_t op);
    /// <summary>The part of <paramref name="rect"/> (window pixels) on <paramref name="surface"/>, as Add draws it.</summary>
    static MCRect Clipped(const Surface& surface, const Quad& rect);
    /// <summary>
    /// Notes a blended draw left out (it would draw nothing) over <paramref name="rect"/>, as Add notes one drawn:
    /// mirror mode leaves its pixels out of its comparison all the same.
    /// </summary>
    void NoteBlended(uint16_t surface, const Quad& rect);
    /// <summary>What a draw reads from a window: its surface, the target's copy, or a picture of its pixels.</summary>
    struct SourceRef
    {
        SourceKind Kind = SourceKind::None;
        uint32_t Index = 0;
    };

    std::optional<SourceRef> SourceFor(uint16_t target, const MCWindow& window);
    /// <summary>
    /// Adds a span of a polygon or quadrilateral (<paramref name="from"/>: the texture's width and height for a texel
    /// walk), with its values.
    /// </summary>
    bool AddSpan(uint16_t surface, const SourceRef& source, const Quad& at, const Quad& from, uint32_t op,
                 uint32_t color, uint32_t before, uint32_t after, const MCSpan& span, bool join);
    /// <summary>The frame's picture index of the picture keyed by <paramref name="key"/>; a new one is
    /// <paramref name="width"/> x <paramref name="height"/> bytes that <paramref name="fill"/> writes.</summary>
    std::optional<uint32_t> PictureForKey(uint64_t key, uint32_t width, uint32_t height,
                                          const std::function<void(std::span<uint8_t>)>& fill);
    /// <summary>
    /// The frame's picture index of <paramref name="texture"/>: its GPU copy, uploaded again first when its pixels
    /// changed. Pixels changed after a draw of this frame read the copy go into a picture for the rest of the frame.
    /// </summary>
    std::optional<uint32_t> PictureOf(MCTexture& texture);

    /// <summary>
    /// What a blit of a picture draws: its colours, only its blended pixels' (an HD art frame shows the rest), or only
    /// its opaque pixels' indices (the blend pipeline leaves the indices).
    /// </summary>
    enum class BlitPixels : uint8_t
    {
        All,
        Blended,
        OpaqueIndices,
    };

    /// <summary>
    /// Adds a blended blit (CopySprite) of a picture <paramref name="width"/> x <paramref name="height"/>, which
    /// <paramref name="picture"/> gives once the blit is known to stay inside it (else <paramref name="command"/> is
    /// noted as not supported); of its <paramref name="pixels"/>.
    /// </summary>
    void BlitPicture(MCWindow& target, const MCAlphaBlitCommand& blit, int32_t width, int32_t height,
                     std::string_view command, const std::function<std::optional<uint32_t>()>& picture,
                     bool smoothEdges = false, BlitPixels pixels = BlitPixels::All);
    /// <summary>
    /// Where a blit of a picture <paramref name="width"/> x <paramref name="height"/> goes (its rectangle) and reads
    /// from (its first pixel and steps); none when it draws nothing, or reads outside the picture (then
    /// <paramref name="command"/> is noted as not supported).
    /// </summary>
    std::optional<std::pair<Quad, Quad>> BlitPlace(const MCAlphaBlitCommand& blit, int32_t width, int32_t height,
                                                   std::string_view command);
    /// <summary>
    /// How an HD art frame's colours show: corrected by each original index's colour now over the pack's palette.rgb
    /// (a mech's, the ground's: made from the missions' palette), or as made (a menu's picture, made from its own
    /// screen's palette).
    /// </summary>
    enum class HdColours : uint8_t
    {
        Corrected,
        AsMade,
    };

    /// <summary>
    /// Port-only: where an HD art frame's edges come from: its alpha (a menu's picture, an effect's blended pixels), or
    /// the original's pixels it draws, smoothed at the output's resolution (KindHdSmooth: a sprite's or an overlay's
    /// frame, whose alpha is only their mask scaled up, steps and all).
    /// </summary>
    enum class HdEdges : uint8_t
    {
        Frame,
        Smooth,
    };

    /// <summary>
    /// Port-only: what an HD art frame's draw tells the composite besides its colours: nothing (the original's indices,
    /// which its draw writes, tell it), or that something stands on the ground wherever the frame shows, its smooth
    /// edge past the original's pixels too (a unit's or an object's frame: the index target's alpha, draw.pshader's
    /// mark pass), the indices left the original's.
    /// </summary>
    enum class HdMarks : uint8_t
    {
        None,
        Standing,
    };

    /// <summary>
    /// Port-only: whether <paramref name="surface"/> shows what is drawn from the original at the output's resolution:
    /// HD art is on and the surface is drawn at more pixels than its window has.
    /// </summary>
    bool ScaledHdArt(uint16_t surface) const;
    /// <summary>
    /// Whether <paramref name="surface"/> is drawn at more pixels than its window has: the sharp screen as placed for
    /// the frame being recorded, or a surface placed so when the last frame ran (an underlay).
    /// </summary>
    bool DrawnLarger(uint16_t surface) const;
    /// <summary>Port-only: whether <paramref name="surface"/> is the sharp screen (<see cref="SetScreenScale"/>).</summary>
    bool SharpScreenSurface(uint16_t surface) const;

    /// <summary>
    /// Port-only: adds the draw of an HD art frame of a textured polygon's picture (a tactical or briefing map's) as the
    /// polygon maps it (KindHdMap): turned and scaled, at the output's resolution. False when the frame can't be put on
    /// the GPU or the polygon's vertices lie in a line.
    /// </summary>
    bool AddHdMap(uint16_t surface, const MCHdArt::Frame& frame, const MCPolygonCommand& command);

    /// <summary>
    /// Port-only (sun shadows): adds the shadow the sun casts of frame <paramref name="shapeNum"/> of
    /// <paramref name="table"/> (its HD art pack frame; none without one), the frame's first column and row at window
    /// (<paramref name="left"/>, <paramref name="top"/>), <paramref name="mirrored"/> its first column drawn its last: each
    /// point cast MCLighting::SunShadowRight and SunShadowUp per pixel it stands above the hot spot's row, black at
    /// the frame's coverage as dark as MCLighting::SunShadow, over what lies under it (no indices), within
    /// <paramref name="clip"/> (window pixels, inclusive; the window's when none).
    /// </summary>
    void AddSunShadow(MCWindow& target, std::span<const uint8_t> table, int32_t shapeNum, int32_t left, int32_t top,
                      bool mirrored, const std::optional<Quad>& clip);

    /// <summary>
    /// Port-only (sun shadows): while the shown sun casts farther than the day's (MCSceneLights::PaintedShadowReach:
    /// dusk), adds the shadow painted into <paramref name="frame"/> (MCHdArt::Frame::PaintedShadow: a forest's, which
    /// casts none of its own) lengthened as far, its original pixels from window (<paramref name="x"/>,
    /// <paramref name="y"/>), as dark as MCLighting::SunShadow, within <paramref name="clip"/> (window pixels,
    /// inclusive). Added before the frame, it lies under it. Nothing for another frame, or by day or night.
    /// </summary>
    void AddPaintedShadow(uint16_t surface, const MCHdArt::Frame& frame, int32_t x, int32_t y, const Quad& clip);

    /// <summary>
    /// Port-only (sun shadows): adds <paramref name="frame"/> (picture <paramref name="picture"/>) as a sun shadow at
    /// <paramref name="at"/>, each window point read from the frame's original pixels at <paramref name="mapping"/>'s
    /// affine map of it (u's three terms, then v's), lengthened by <paramref name="reach"/> (draw.pshader's
    /// LengthenedShadowPixel; 0: cast as it is). Whether it was added.
    /// </summary>
    bool AddMappedSunShadow(uint16_t surface, const MCHdArt::Frame& frame, uint32_t picture, const Quad& at,
                            const std::array<float, 6>& mapping, uint32_t reach);

    /// <summary>
    /// Port-only: adds the smooth colour pass of a game-drawn picture mapped with its key transparent (the fog of war,
    /// KindSmoothMap): its drawn texels' edges rounded at the output's resolution, as a smooth-edged shape's are, from
    /// <paramref name="source"/> (the picture's surface). False when the polygon's vertices lie in a line.
    /// </summary>
    bool AddSmoothMap(uint16_t surface, SourceRef source, const MCPolygonCommand& command);

    /// <summary>
    /// Port-only: adds the colour pass of a flat or Gouraud polygon (a laser's beam) at the output's resolution
    /// (KindSmoothPolygon): its edges smooth, its colour index taken at every drawn pixel; a polygon of no area as lines,
    /// a quad with one corner turned in as two triangles. False for a polygon it can't draw so (more than four corners,
    /// one row high, one the walk doesn't fill whole, or concave otherwise).
    /// </summary>
    bool AddSmoothPolygon(uint16_t surface, const MCPolygonCommand& command);

    /// <summary>
    /// Port-only: adds a draw of kind and flags <paramref name="op"/> over a textured polygon's bounds, reading
    /// <paramref name="source"/> through the polygon's <paramref name="mapping"/> (window to texture positions);
    /// whether one was added (none when the polygon lies outside its pane).
    /// </summary>
    bool AddMapped(uint16_t surface, SourceKind source, uint32_t sourceIndex, uint32_t op, uint32_t color,
                   const std::array<float, 6>& mapping, const MCPolygonCommand& command);

    /// <summary>
    /// Adds the draw of an HD art frame over <paramref name="at"/> (blended into the colours), reading the original
    /// shape's pixel <paramref name="from"/> (its first pixel and steps, in the shape's bounds) at the frame's scale,
    /// recoloured by <paramref name="table"/> as the original's pixels would be, its colours as
    /// <paramref name="colours"/> says; a keyed copy (<paramref name="key"/>) leaves out the original's pixels of the
    /// key; its edges as <paramref name="edges"/> says; marked as <paramref name="marks"/> says (an effect's frame and a
    /// forest's mark nothing: an effect's blended pixels leave the ground under them, a forest's frame marks its sway).
    /// False when the frame can't be put on the GPU.
    /// </summary>
    bool AddHd(uint16_t surface, const MCHdArt::Frame& frame, const Quad& at, const Quad& from,
               const std::optional<MCColorTable>& table, std::optional<uint8_t> key = std::nullopt,
               HdColours colours = HdColours::Corrected, HdEdges edges = HdEdges::Frame, HdMarks marks = HdMarks::None);

    /// <summary>
    /// Port-only: the HD art pack's copy of <paramref name="window"/>'s picture (a menu's art), looked for once each
    /// time its pixels are uploaded or the pack changes; null when there is none, or when <paramref name="target"/>
    /// isn't a sharp screen.
    /// </summary>
    std::shared_ptr<const MCHdArt::Frame> HdPictureFor(uint16_t target, const MCWindow& window);

    /// <summary>Where a copy onto a sharp screen shows a frame shown over its source's pixels (OnShowHdPicture).</summary>
    struct ShownHdPart
    {
        std::shared_ptr<const MCHdArt::Frame> Frame;
        Quad At;
        Quad From;
    };

    /// <summary>
    /// The part of <paramref name="command"/>'s copy onto <paramref name="surface"/> its source's shown HD frame covers
    /// (<see cref="MCRenderer::ShowHdPicture"/>): where on the target, and where in the frame; none off a sharp screen.
    /// </summary>
    std::optional<ShownHdPart> ShownHdPartOf(uint16_t surface, const MCCopyCommand& command);
    /// <summary>Port-only: <see cref="HdPictureFor"/> whatever the target: null when there is none.</summary>
    std::shared_ptr<const MCHdArt::Frame> HdPicture(const MCWindow& window);
    /// <summary>
    /// Port-only: the HD art pack's copy of frame <paramref name="frame"/> of <paramref name="table"/> (its bounds
    /// <paramref name="width"/> x <paramref name="height"/>): from the folder the table is attached to, else, for a
    /// table nothing attached (one loaded outside the sprite, crater and icon files), the one the pack keeps under its
    /// bytes' hash (<see cref="MCHdArt::FindShape"/>); null when there is none.
    /// </summary>
    std::shared_ptr<const MCHdArt::Frame> HdShape(std::span<const uint8_t> table, int32_t frame, int32_t width,
                                                  int32_t height, int32_t step = 0);

    /// <summary>
    /// Port-only: an HD art pack's in-between, over its frame's and the next one's bounds together (a turn middle: its
    /// frame's and its neighbour's): where the frame's bounds start in the in-between's, and the in-between's size
    /// (original pixels).
    /// </summary>
    struct HdInBetweenFrame
    {
        std::shared_ptr<const MCHdArt::Frame> Frame;
        int32_t FrameX = 0;
        int32_t FrameY = 0;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    /// <summary>
    /// Port-only: the HD art pack's in-between a draw of frame <paramref name="shapeNum"/> of <paramref name="table"/>
    /// (its HD frame <paramref name="hd"/>) shows instead of the frame (<see cref="MCRenderer::DrawingInBetween"/>): an
    /// effect's, whose frame holds its blended pixels, or (HDTurns) a mech's legs' turn middle, where the draw says it
    /// lies. None when the draw isn't of the in-between's frame or the pack hasn't the in-between.
    /// </summary>
    std::optional<HdInBetweenFrame> HdInBetween(std::span<const uint8_t> table, int32_t shapeNum,
                                                const MCHdArt::Frame& hd);
    /// <summary>The frame's picture index of HD art frame <paramref name="frame"/> (uploaded when new).</summary>
    std::optional<uint32_t> HdPictureOf(const MCHdArt::Frame& frame);
    /// <summary>
    /// Port-only: how HD art frame <paramref name="frame"/> of shape table <paramref name="table"/> draws its edges: its
    /// own when it holds its blended pixels (an effect's) or its table keeps them (MCHdArt::KeepsFrameEdges), else
    /// smooth.
    /// </summary>
    static HdEdges HdEdgesOf(const MCHdArt::Frame& frame, std::span<const uint8_t> table);

    /// <summary>
    /// Port-only: draws the part from (<paramref name="sourceX"/>, <paramref name="sourceY"/>) of the glyph
    /// <paramref name="columns"/> x <paramref name="rows"/> at <paramref name="glyph"/> (rows a glyph's width apart) of
    /// <paramref name="font"/> over <paramref name="at"/> on a sharp screen, the whole glyph redrawn as strokes
    /// (<see cref="MCSharpText"/>; a two-tone glyph as a layer per byte, as its font's layers say) at the output's
    /// resolution in the colours <paramref name="table"/> gives its bytes. False when it can't be put on the GPU (the
    /// original then draws the colours as well).
    /// </summary>
    bool AddSharpGlyph(uint16_t surface, std::span<const uint8_t> font, std::span<const uint8_t> glyph, int32_t columns,
                       int32_t rows, const Quad& at, int32_t sourceX, int32_t sourceY, const MCColorTable& table);
    /// <summary>Port-only: how <paramref name="font"/>'s two-tone glyphs are layered (kept until its bytes change).</summary>
    const MCSharpText::MCFontLayers& FontLayers(std::span<const uint8_t> font);
    /// <summary>
    /// Port-only: draws line art <paramref name="columns"/> x <paramref name="rows"/> (<paramref name="art"/>, its
    /// pixels' bytes, <see cref="MCSharpText::NoInk"/> where none is drawn) over <paramref name="at"/> on a sharp screen,
    /// reading it from <paramref name="from"/> (its first pixel and steps), redrawn as smooth strokes
    /// (<see cref="MCSharpLines"/>) at the output's resolution in the colours table row <paramref name="tableRow"/> gives
    /// its bytes. False when it can't be put on the GPU.
    /// </summary>
    bool AddSharpLines(uint16_t surface, std::span<const uint8_t> art, int32_t columns, int32_t rows, const Quad& at,
                       const Quad& from, uint32_t tableRow);
    /// <summary>What a picture redrawn sharp was redrawn from (its key's space).</summary>
    enum class SharpKind : uint8_t
    {
        Glyph = 1,
        Lines,
    };

    /// <summary>The key a picture redrawn sharp is kept by: the kind, size and bytes of what it was redrawn from.</summary>
    static uint64_t SharpKey(SharpKind kind, std::span<const uint8_t> pixels, int32_t pitch, int32_t columns,
                             int32_t rows);
    /// <summary>
    /// Draws the picture kept by <paramref name="key"/> (<paramref name="redraw"/> makes it the first time: RGBA8,
    /// <see cref="MCSharpText::Scale"/> times <paramref name="columns"/> x <paramref name="rows"/>, red the byte and green
    /// the coverage) over <paramref name="at"/>, read from <paramref name="from"/>, in the colours table row
    /// <paramref name="tableRow"/> gives its bytes (shaders/draw.pshader's KindSharpGlyph).
    /// </summary>
    bool AddSharpPicture(uint16_t surface, uint64_t key, const std::function<std::vector<uint8_t>()>& redraw,
                         int32_t columns, int32_t rows, const Quad& at, const Quad& from, uint32_t tableRow);
    /// <summary>A registered table's row of the table texture, and a number no other table's bytes ever had.</summary>
    struct TableSlot
    {
        uint32_t Row = 0;
        uint64_t Id = 0;
    };

    /// <summary>
    /// The slot of the table at <paramref name="table"/> (a row taken from its bytes the first time); none when it
    /// lies outside every registered table block (noted as unregistered).
    /// </summary>
    std::optional<TableSlot> TableSlotOf(MCColorTable table);
    /// <summary>The table texture's row of <paramref name="table"/> (<see cref="TableSlotOf"/>; 0, the identity,
    /// when unregistered).</summary>
    uint32_t TableRow(MCColorTable table);
    /// <summary>Puts the alpha colours (MCAlphaColors) in the alpha texture's form into the uploads.</summary>
    void UploadAlphaColors();
    /// <summary>
    /// Puts the new table rows into the uploads, with their fits (all of them when the colours changed or the textures
    /// grew).
    /// </summary>
    std::expected<void, std::string> UploadTables();
    /// <summary>
    /// The fit of table row <paramref name="row"/> into <paramref name="fit"/>: the premultiplied colour and coverage
    /// whose blend best gives what the table does to the palette's colours (least squares over the free colours).
    /// </summary>
    void FitTable(uint32_t row, std::span<float, 4> fit) const;
    /// <summary>
    /// A new atlas place that <paramref name="decode"/> fills (width x height pixels of value + state); none when it
    /// doesn't fit a page.
    /// </summary>
    struct AtlasPlace
    {
        uint32_t Page = 0;
        int32_t X = 0;
        int32_t Y = 0;
    };

    std::optional<AtlasPlace> AtlasFor(int32_t width, int32_t height,
                                       const std::function<void(std::span<uint8_t>)>& decode);
    /// <summary>An encoded image's extent: its bytes, and the width and height it decodes to.</summary>
    struct Extent
    {
        size_t Size = 0;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    /// <summary>
    /// An image made from registered data (<see cref="MCRenderer::RegisterData"/>): its extent, its atlas place (none
    /// when it has no pixels or doesn't fit a page) or, for a shape transform's picture, its picture's number.
    /// </summary>
    struct DataImage
    {
        Extent Measured;
        std::optional<AtlasPlace> Place;
        uint64_t PictureId = 0;
        /// <summary>Mirror mode: the bytes it was made from, to catch a change nobody announced.</summary>
        std::vector<uint8_t> Bytes;
        /// <summary>
        /// Port-only: the colours of the pixels it draws, as decoded into the atlas: a translucent shape with none a
        /// translucent draw shows opaque has no index pass (Shape).
        /// </summary>
        std::bitset<256> Colors;
    };

    /// <summary>
    /// The image encoded at <paramref name="data"/> (with <paramref name="seed"/>: what else its decoding depends on),
    /// found by its address, or measured (<paramref name="measure"/> walks the encoding) and kept. None when the data
    /// lies outside every registered block, or reaches past its own (noted as unregistered, <paramref name="what"/>).
    /// </summary>
    MCOptionalRef<DataImage> ImageOf(std::span<const uint8_t> data, uint64_t seed, std::string_view what,
                                     const std::function<Extent()>& measure);

    /// <summary>
    /// <see cref="ImageOf"/>, with the image in the atlas: <paramref name="decode"/> decodes it the first time.
    /// </summary>
    MCOptionalRef<DataImage> AtlasForData(std::span<const uint8_t> data, uint64_t seed, std::string_view what,
                                          const std::function<Extent()>& measure,
                                          const std::function<void(std::span<uint8_t>, const Extent&)>& decode);
    /// <summary>The frame's picture index of <paramref name="window"/>'s pixels (uploaded when new).</summary>
    std::optional<uint32_t> PictureFor(const MCWindow& window);
    /// <summary>Queues the bytes of an upload into a rectangle of a texture: where to write them.</summary>
    std::span<uint8_t> QueueUpload(std::shared_ptr<SDL_GPUTexture> texture, uint32_t x, uint32_t y, uint32_t width,
                                   uint32_t height, uint32_t bytesPerPixel);
    /// <summary>Notes a command the GPU can't draw yet.</summary>
    void NotSupported(std::string_view command);

    std::expected<void, std::string> EnsureTexture(Texture& texture, SDL_GPUTextureFormat format,
                                                   SDL_GPUTextureUsageFlags usage, uint32_t width, uint32_t height);
    static void Release(Texture& texture);
    /// <summary>Takes the copy of a surface's <paramref name="rect"/> (window pixels; between render passes).</summary>
    static void TakeCopy(SDL_GPUCommandBuffer& commands, Surface& surface, const MCRect& rect);
    /// <summary>Sets each surface's drawn size for this frame (<paramref name="underlays"/> are drawn as shown).</summary>
    void PlaceSurfaces(std::span<const MCUnderlay> underlays);
    /// <summary>(Re)makes a surface's textures at its drawn size and clears them, as a window's new pixels start.</summary>
    std::expected<void, std::string> MakeSurfaceTextures(SDL_GPUCommandBuffer& commands, Surface& surface);
    /// <summary>Puts <paramref name="mesh"/> on the GPU (its tiles in the terrain atlas, its cells and images).</summary>
    std::expected<void, std::string> UploadTerrainMesh(const MCTerrainMesh& mesh);
    /// <summary>The HD art pack in use (MCHdArt::PackVersion), or 0 when HD art is off.</summary>
    static uint64_t HdPackInUse();
    /// <summary>
    /// Whether the terrain takes the HD art pack's relief maps (HD lighting on, MCLighting::ReliefMaps above 0).
    /// </summary>
    static bool ReliefMapsInUse();
    /// <summary>Makes one of the terrain's HD atlases (BC7, MaxHdAtlasSide wide), or says why there is none.</summary>
    std::expected<void, std::string> MakeHdTerrainAtlas(
        Texture& atlas, uint32_t height, SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM);
    /// <summary>Puts an HD terrain tile's colours into HD atlas <paramref name="atlas"/> at (x, y), a block corner.</summary>
    void UploadHdTile(Texture& atlas, const MCHdArt::Frame& frame, uint32_t x, uint32_t y);
    /// <summary>Releases the terrain mesh's textures and buffers.</summary>
    void ReleaseTerrainMesh();
    /// <summary>Queues <paramref name="size"/> bytes for an upload into a buffer: where to write them.</summary>
    std::span<uint8_t> QueueBufferUpload(std::shared_ptr<SDL_GPUBuffer> buffer, uint32_t size);
    /// <summary>Execution: draws a terrain layer in the render pass under way.</summary>
    void DrawTerrain(SDL_GPUCommandBuffer& commands, SDL_GPURenderPass& pass, const Surface& surface,
                     const TerrainDraw& draw, std::span<const std::byte> frameUniforms);
    /// <summary>Reads <paramref name="surfaces"/>' indices back (each its texture's size), waiting for the GPU.</summary>
    std::expected<std::vector<std::vector<uint8_t>>, std::string> Download(
        std::span<const std::reference_wrapper<const Surface>> surfaces);

    /// <summary>The presenter's device; every GPU object holds it.</summary>
    std::shared_ptr<SDL_GPUDevice> _Device;
    MCGpuHandle<SDL_GPUShader> _VertexShader;
    MCGpuHandle<SDL_GPUShader> _FragmentShader;
    /// <summary>
    /// Port-only: the fragment shaders of the draws of the original's own kinds alone and of HD art frames alone
    /// (DrawVariant), and of the terrain layer's.
    /// </summary>
    MCGpuHandle<SDL_GPUShader> _BasicFragmentShader;
    MCGpuHandle<SDL_GPUShader> _HdFragmentShader;
    MCGpuHandle<SDL_GPUShader> _TerrainFragmentShader;

    /// <summary>The draws' pipelines with one fragment shader.</summary>
    struct DrawPipelines
    {
        /// <summary>
        /// Opaque draws (colours and indices written), blended ones (alpha blended into the colours), and those that
        /// write the indices alone.
        /// </summary>
        MCGpuHandle<SDL_GPUGraphicsPipeline> Opaque;
        MCGpuHandle<SDL_GPUGraphicsPipeline> Blend;
        MCGpuHandle<SDL_GPUGraphicsPipeline> IndexOnly;
        /// <summary>
        /// Port-only (HDSway): a forest's draw, blended, writing the sway mark (the indices' alpha) alone.
        /// </summary>
        MCGpuHandle<SDL_GPUGraphicsPipeline> BlendMarks;
        /// <summary>Port-only: an HD frame's mark pass (HdMarks::Standing), writing the indices' alpha alone.</summary>
        MCGpuHandle<SDL_GPUGraphicsPipeline> IndexMarks;
        /// <summary>Whether every pipeline was made.</summary>
        bool Made() const;
        /// <summary>The pipeline <paramref name="record"/>'s draws go through (not a shadow's).</summary>
        SDL_GPUGraphicsPipeline* For(const Record& record) const;
    };

    /// <summary>The draws' pipelines, a set for each draw variant (DrawVariant's order).</summary>
    std::array<DrawPipelines, DrawVariants> _Pipelines;
    /// <summary>Port-only: shadows (HD lighting), which take the colours' alpha down by theirs and leave the rest.</summary>
    MCGpuHandle<SDL_GPUGraphicsPipeline> _ShadowPipeline;
    /// <summary>The terrain layer's vertex shader and pipeline (opaque, with the terrain's fragment shader).</summary>
    MCGpuHandle<SDL_GPUShader> _TerrainShader;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _TerrainPipeline;
    /// <summary>Port-only: the terrain's seam pass, blended (shaders/terrain.vshader).</summary>
    MCGpuHandle<SDL_GPUGraphicsPipeline> _TerrainSeamPipeline;
    TerrainMeshState _TerrainMesh;
    std::vector<TerrainDraw> _TerrainDraws;
    /// <summary>The surface whose tile commands a terrain layer drew (-1: none).</summary>
    int32_t _TerrainLayerSurface = -1;
    int64_t _TerrainLayersDrawn = 0;

    /// <summary>Uploads into buffers waiting for the next <see cref="Execute"/>.</summary>
    struct BufferUpload
    {
        std::shared_ptr<SDL_GPUBuffer> Buffer;
        uint32_t Size;
        size_t Offset;
    };

    std::vector<BufferUpload> _BufferUploads;
    MCGpuHandle<SDL_GPUSampler> _Nearest;
    /// <summary>A 1x1 texture bound where a draw has no source or no world.</summary>
    Texture _Blank;

    std::vector<Surface> _Surfaces;

    // The frame being recorded.
    bool _Recording = false;
    uint64_t _Frame = 0;
    std::vector<Record> _Records;
    std::vector<std::array<int32_t, 20>> _Draws;
    /// <summary>
    /// The table rows (256 bytes each, row 0 the identity) and the texture holding them. A row stays while its table
    /// does; rows dropped (the table changed or went) are free again from the next frame on (<c>_ReleasedRows</c>,
    /// the frame recorded may still read them), and new rows go up with their fits at <see cref="Execute"/>.
    /// </summary>
    std::vector<uint8_t> _Tables;
    std::map<MCAddressKey, TableSlot> _TableSlots;
    std::vector<uint32_t> _FreeRows;
    std::vector<uint32_t> _ReleasedRows;
    std::vector<uint32_t> _NewRows;
    uint64_t _LastTableId = 0;
    Texture _TableTexture;
    /// <summary>
    /// The alpha colours (RGBA32F, a column each: row 0 the kind, row 1 the premultiplied colour and coverage), whether
    /// they must go up again, and the table rows' fits (RGBA32F, a row each).
    /// </summary>
    Texture _AlphaTexture;
    bool _AlphaColorsChanged = true;
    Texture _FitTexture;
    /// <summary>The palette drawn with (RGBA8, 256 x 1), its colours, whether they changed, and the colour cycle.</summary>
    Texture _PaletteTexture;
    std::array<SDL_Color, 256> _Colors{};
    bool _ColorsChanged = true;
    /// <summary>Output pixels per screen pixel the underlays are shown at (<see cref="SetUnderlayScale"/>).</summary>
    float _UnderlayScaleX = 1.0f;
    float _UnderlayScaleY = 1.0f;
    /// <summary>The screen drawn at the size it's shown (<see cref="SetScreenScale"/>): its window, scale and extent.</summary>
    MCAddressKey _ScaledScreen;
    float _ScreenScaleX = 1.0f;
    float _ScreenScaleY = 1.0f;
    int32_t _ScreenRight = 0;
    int32_t _ScreenBottom = 0;
    MCColorCycle _Cycle;
    /// <summary>The pixels <see cref="Write"/> was given this frame: a row each (StripWidth wide), and the texture.</summary>
    std::vector<uint8_t> _StripPixels;
    uint32_t _StripRows = 0;
    Texture _Strip;

    /// <summary>
    /// Uploads waiting for the next <see cref="Execute"/>, run in order: from <c>Buffer</c> at <c>Offset</c>
    /// (<c>_Transfer</c> when null; rows <c>RowPixels</c> apart, <c>Width</c> when 0), or with <c>From</c> set, a copy of
    /// the same rectangle from that texture. A null <c>Texture</c> was dropped. An upload holds what it reads and
    /// writes until the frame has run.
    /// </summary>
    struct Upload
    {
        std::shared_ptr<SDL_GPUTexture> Texture;
        uint32_t X;
        uint32_t Y;
        uint32_t Width;
        uint32_t Height;
        uint32_t BytesPerPixel;
        size_t Offset;
        std::shared_ptr<SDL_GPUTransferBuffer> Buffer;
        uint32_t RowPixels = 0;
        std::shared_ptr<SDL_GPUTexture> From;
    };

    std::vector<Upload> _Uploads;
    std::vector<uint8_t> _Staging;
    std::shared_ptr<SDL_GPUTransferBuffer> _Transfer;
    uint32_t _TransferSize = 0;
    MCGpuHandle<SDL_GPUBuffer> _DrawBuffer;
    uint32_t _DrawBufferSize = 0;

    /// <summary>The atlas: pages of value + state (R8G8), filled in shelves.</summary>
    struct Page
    {
        Texture Texture;
        int32_t ShelfX = 0;
        int32_t ShelfY = 0;
        int32_t ShelfHeight = 0;
    };

    std::vector<Page> _Pages;

    /// <summary>The images made from registered data, by the address they're read from and their seed.</summary>
    std::map<std::pair<MCAddressKey, uint64_t>, DataImage> _DataImages;
    /// <summary>
    /// Port-only: the hashes of the shape tables nothing attached (<see cref="HdShape"/>), by the table's address, until
    /// its bytes change; none for a table outside the registered blocks.
    /// </summary>
    std::map<MCAddressKey, std::optional<uint64_t>> _ShapeHashes;
    /// <summary>The last picture number handed out (<c>DataImage::PictureId</c>).</summary>
    uint64_t _LastPictureId = 0;

    /// <summary>Pictures (windows in memory) by content, and when each was used last.</summary>
    struct Picture
    {
        /// <summary>An HD art frame's colours (Texture holds its original's indices), and the bytes both take.</summary>
        Texture HdColors;
        size_t Bytes = 0;
        Texture Texture;
        uint64_t LastFrame = 0;
    };

    std::unordered_map<uint64_t, Picture> _Pictures;
    size_t _PictureBytes = 0;
    /// <summary>
    /// The HD art frames on the GPU (their indices R8G8, their colours BC7 or RGBA8), by their frame's number, and the
    /// bytes they take.
    /// </summary>
    std::unordered_map<uint64_t, Picture> _HdPictures;
    size_t _HdPictureBytes = 0;
    /// <summary>The HD art frames the GPU couldn't take (logged once each, then drawn from the 1x shape).</summary>
    std::unordered_set<uint64_t> _HdRefused;
    /// <summary>Port-only: the glyphs redrawn sharp (RGBA8, MCSharpText), by their bytes' hash, and their bytes.</summary>
    std::unordered_map<uint64_t, Picture> _SharpGlyphs;
    size_t _SharpGlyphBytes = 0;
    /// <summary>Port-only: how each font's two-tone glyphs are layered, by the font's address, until its bytes change.</summary>
    std::map<MCAddressKey, MCSharpText::MCFontLayers> _FontLayers;
    int64_t _HdDrawn = 0;
    /// <summary>The draw command being audited (while <see cref="Audit"/> is set).</summary>
    std::optional<AuditedDraw> _Audited;

    /// <summary>Audits the draw command it lives through (when <see cref="Audit"/> is set; the outermost command's).</summary>
    class AuditScope
    {
    public:
        AuditScope(MCVulkanRenderer& renderer, std::string_view command, const MCWindow& target,
                   std::span<const uint8_t> data = {}, int32_t frame = -1, const MCWindow* from = nullptr);
        ~AuditScope();
        AuditScope(const AuditScope&) = delete;
        AuditScope& operator=(const AuditScope&) = delete;

    private:
        MCVulkanRenderer& _Renderer;
        bool _Outermost = false;
    };

    /// <summary>
    /// Drops the pictures of <paramref name="pictures"/> unused for a while, then, oldest first, those past
    /// <paramref name="budget"/> bytes (down to half of it); <paramref name="bytes"/> counts what they take.
    /// </summary>
    void TrimPictures(std::unordered_map<uint64_t, Picture>& pictures, size_t& bytes, size_t budget,
                      size_t bytesPerPixel);

    /// <summary>
    /// Drops <paramref name="it"/>'s picture from <paramref name="pictures"/>, taking what it took from
    /// <paramref name="bytes"/>.
    /// </summary>
    /// <returns>The next picture.</returns>
    std::unordered_map<uint64_t, Picture>::iterator DropPicture(std::unordered_map<uint64_t, Picture>& pictures,
                                                                std::unordered_map<uint64_t, Picture>::iterator it,
                                                                size_t& bytes, size_t bytesPerPixel);

    /// <summary>Drops the HD art frame <paramref name="id"/>'s picture now (a movie frame shown for the last time).</summary>
    void ForgetHdPicture(uint64_t id);

    /// <summary>Marks <paramref name="picture"/> used this frame; returns its frame picture index.</summary>
    uint32_t UsePicture(Picture& picture);

    /// <summary>
    /// Port-only: the picture redrawn sharp kept by <paramref name="key"/> (<see cref="AddSharpPicture"/>'s), made by
    /// <paramref name="redraw"/> the first time; null when it can't be put on the GPU.
    /// </summary>
    Picture* SharpPicture(uint64_t key, const std::function<std::vector<uint8_t>()>& redraw, int32_t columns,
                          int32_t rows);
    /// <summary>
    /// Port-only: draws <paramref name="picture"/>, redrawn sharp, over <paramref name="at"/>, read from
    /// <paramref name="from"/>, in the colours table row <paramref name="tableRow"/> gives its bytes, with
    /// <paramref name="flags"/> above the scale in the colour field (shaders/draw.pshader's KindSharpGlyph).
    /// </summary>
    void DrawSharpPicture(uint16_t surface, Picture& picture, const Quad& at, const Quad& from, uint32_t tableRow,
                          uint32_t flags);

    /// <summary>The frame's pictures in use (their textures).</summary>
    std::vector<std::shared_ptr<SDL_GPUTexture>> _FramePictures;
    /// <summary>The HD colours of the frame's pictures that are HD art frames, by their place in _FramePictures.</summary>
    std::unordered_map<uint32_t, std::shared_ptr<SDL_GPUTexture>> _FrameHdColors;
    /// <summary>Whether the GPU samples BC7 (unknown: not asked yet); else HD colours are decoded to RGBA8.</summary>
    std::optional<bool> _Bc7;
    /// <summary>The HD art pack whose colours the palette's second row holds (MCHdArt::PackVersion; 0: none).</summary>
    uint64_t _HdPaletteVersion = 0;

    /// <summary>
    /// What is kept for a texture (its <c>MCTexture::Hardware</c>): its GPU copy, and its place in
    /// <c>_FramePictures</c> during frame <c>Frame</c>. <c>Behind</c>: the copy is older than the pixels (they changed
    /// after a draw of that frame read it, and went into a picture of their own).
    /// </summary>
    struct TextureState : MCTextureHardware
    {
        Texture Copy;
        uint64_t Frame = ~0ull;
        uint32_t Index = 0;
        bool Behind = false;
        /// <summary>Mirror mode: the pixels last uploaded, to catch a change nobody announced.</summary>
        std::vector<uint8_t> Uploaded;
        /// <summary>
        /// A stream texture's upload memory (its size), and the frame and <c>_Uploads</c> entry of the stream upload
        /// queued last.
        /// </summary>
        std::shared_ptr<SDL_GPUTransferBuffer> Stream;
        uint32_t StreamSize = 0;
        uint64_t StreamFrame = ~0ull;
        size_t StreamUpload = 0;
        /// <summary>
        /// Port-only: the HD art pack's copy of the pixels as last uploaded (<see cref="MCHdArt::FindPicture"/>), and
        /// whether it was looked for since, in the pack then set (<see cref="MCHdArt::PackVersion"/>).
        /// </summary>
        std::shared_ptr<const MCHdArt::Frame> Hd;
        bool HdLooked = false;
        uint64_t HdPack = 0;
        /// <summary>
        /// Port-only: a frame shown over the pixels from (ShownHdX, ShownHdY) on (MCRenderer::ShowHdPicture: a movie's
        /// HD frame), or null.
        /// </summary>
        std::shared_ptr<const MCHdArt::Frame> ShownHd;
        int32_t ShownHdX = 0;
        int32_t ShownHdY = 0;
    };

    /// <summary>What is kept for <paramref name="texture"/> (made when missing).</summary>
    static TextureState& StateOf(MCTexture& texture);
    /// <summary>
    /// Makes <paramref name="texture"/>'s GPU copy and uploads its pixels whole into it when it has none, its pixels
    /// changed, or it is behind them.
    /// </summary>
    std::expected<void, std::string> UploadWhole(MCTexture& texture, TextureState& state);
    /// <summary>
    /// Draws of this frame that read <paramref name="state"/>'s picture keep what they read: they move to a copy of
    /// it taken now (in upload order), so uploads queued from here on reach only the draws after them.
    /// </summary>
    std::expected<void, std::string> KeepFramePicture(MCTexture& texture, TextureState& state);
    /// <summary>Maps <paramref name="texture"/>'s stream upload memory for its <c>Locked</c> rows.</summary>
    std::expected<std::span<uint8_t>, std::string> MapStream(MCTexture& texture, TextureState& state);

    /// <summary>Drops what is kept for a texture (its copy goes once the frame recorded has run).</summary>
    void OnTextureReleased(MCTexture& texture) override;
    void OnShowHdPicture(MCTexture& texture, std::shared_ptr<const MCHdArt::Frame> frame, int32_t x,
                         int32_t y) override;
    bool OnShowsHdPictures() const override { return static_cast<bool>(_ScaledScreen); }
    /// <summary>Maps the texture's own upload memory, cycled so a frame in flight is never waited on.</summary>
    std::span<uint8_t> OnLockStream(MCTexture& texture) override;
    /// <summary>Queues the locked rectangle's upload from that memory (filled from <paramref name="written"/> when
    /// not empty).</summary>
    void OnUnlockStream(MCTexture& texture, std::span<const uint8_t> written) override;

    /// <summary>Textures and transfer buffers to release after the next execution (the frame recorded may still read
    /// them).</summary>
    std::vector<Texture> _Retired;
    std::vector<std::shared_ptr<SDL_GPUTransferBuffer>> _RetiredTransfers;

    std::map<std::string, int64_t> _Unsupported;
    /// <summary>The uploads of the frame being recorded, and of the last one run.</summary>
    UploadTally _FrameUploads;
    UploadTally _LastFrameUploads;
    /// <summary>The span walks' state (as the software renderer keeps its own).</summary>
    MCSpanState _Spans;
    MirrorTally _Mirror;
    /// <summary>The surfaces dumped (-gpudump).</summary>
    std::set<MCAddressKey> _Dumped;
};

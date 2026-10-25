#pragma once

#include "platform/MCPresenter.h"
#include "platform/MCSdlHandle.h"
#include "platform/MCSoftwareRenderer.h"

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
        /// The surface's colours (premultiplied RGBA) and indices (R8), both colour targets, and the copy of the indices
        /// draws that read the surface itself read.
        /// </summary>
        Texture Target;
        Texture Index;
        Texture Copy;
        /// <summary>The window rectangles blended draws covered in the frame being recorded, and in the last one run.</summary>
        std::vector<MCRect> Blended;
        std::vector<MCRect> LastBlended;
        /// <summary>The size the commands recorded last saw (the window's).</summary>
        uint32_t RecordedWidth = 0;
        uint32_t RecordedHeight = 0;
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
        /// <summary>The HD art pack's tiles, at HdScale times the atlas's places (none without HD terrain).</summary>
        Texture HdAtlas;
        int32_t HdScale = 0;
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
    /// Adds a blended blit (CopySprite) of a picture <paramref name="width"/> x <paramref name="height"/>, which
    /// <paramref name="picture"/> gives once the blit is known to stay inside it (else <paramref name="command"/> is
    /// noted as not supported).
    /// </summary>
    void BlitPicture(MCWindow& target, const MCAlphaBlitCommand& blit, int32_t width, int32_t height,
                     std::string_view command, const std::function<std::optional<uint32_t>()>& picture,
                     bool smoothEdges = false);
    /// <summary>
    /// Where a blit of a picture <paramref name="width"/> x <paramref name="height"/> goes (its rectangle) and reads
    /// from (its first pixel and steps); none when it draws nothing, or reads outside the picture (then
    /// <paramref name="command"/> is noted as not supported).
    /// </summary>
    std::optional<std::pair<Quad, Quad>> BlitPlace(const MCAlphaBlitCommand& blit, int32_t width, int32_t height,
                                                   std::string_view command);
    /// <summary>
    /// Adds the draw of an HD art frame over <paramref name="at"/> (blended into the colours), reading the original
    /// shape's pixel <paramref name="from"/> (its first pixel and steps, in the shape's bounds) at the frame's scale,
    /// recoloured by <paramref name="table"/> as the original's pixels would be. False when the frame can't be put on
    /// the GPU.
    /// </summary>
    bool AddHd(uint16_t surface, const MCHdArt::Frame& frame, const Quad& at, const Quad& from,
               const std::optional<MCColorTable>& table);
    /// <summary>The frame's picture index of HD art frame <paramref name="frame"/> (uploaded when new).</summary>
    std::optional<uint32_t> HdPictureOf(const MCHdArt::Frame& frame);
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
    /// <summary>Makes the terrain's HD atlas (BC7), or says why there is none.</summary>
    std::expected<void, std::string> MakeHdTerrainAtlas(uint32_t width, uint32_t height);
    /// <summary>Puts an HD terrain tile's colours into the HD atlas at (x, y), a block corner.</summary>
    void UploadHdTile(const MCHdArt::Frame& frame, uint32_t x, uint32_t y);
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
    /// Opaque draws (colours and indices written), blended ones (alpha blended into the colours), and those that write
    /// the indices alone.
    /// </summary>
    MCGpuHandle<SDL_GPUGraphicsPipeline> _Opaque;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _Blend;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _IndexOnly;
    /// <summary>The terrain layer's vertex shader and pipeline (opaque, with the draw fragment shader).</summary>
    MCGpuHandle<SDL_GPUShader> _TerrainShader;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _TerrainPipeline;
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
    int64_t _HdDrawn = 0;

    /// <summary>
    /// Drops the pictures of <paramref name="pictures"/> unused for a while, then, oldest first, those past
    /// <paramref name="budget"/> bytes (down to half of it); <paramref name="bytes"/> counts what they take.
    /// </summary>
    void TrimPictures(std::unordered_map<uint64_t, Picture>& pictures, size_t& bytes, size_t budget,
                      size_t bytesPerPixel);

    /// <summary>Marks <paramref name="picture"/> used this frame; returns its frame picture index.</summary>
    uint32_t UsePicture(Picture& picture);

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

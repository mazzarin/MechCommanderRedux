#pragma once

#include "platform/MCPresenter.h"
#include "platform/MCSdlHandle.h"
#include "platform/MCSharpLines.h"
#include "platform/MCSharpText.h"
#include "platform/MCSoftwareRenderer.h"
#include "sprite/MCHdMovie.h"

#include <bitset>
#include <unordered_set>

namespace MCHdArt
{
    struct Frame;
}

// smol-atlas's (src/lib/smol-atlas-77a4e174): the places in the pages pictures share (MCVulkanRenderer's SharedPage).
struct smol_atlas_t;
struct smol_atlas_item_t;

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

    /// <summary>
    /// Port-only: a pipeline for <see cref="MakePipelines"/> to make: its name (for the log, should it fail), whether
    /// it's one of the slow ones, and the call that makes it and keeps it where it goes (false when it wasn't made).
    /// </summary>
    struct PipelineJob
    {
        std::string Name;
        bool Slow = false;
        std::function<bool()> Make;
    };

    /// <summary>
    /// Makes the renderer's pipelines and fixed textures on <paramref name="device"/>. Port-only: the pipelines of
    /// <paramref name="moreJobs"/> (the presenter's) are made with the renderer's, all at once, which empties it; it's
    /// left as it was when the renderer fails before its pipelines are made.
    /// </summary>
    static std::expected<std::unique_ptr<MCVulkanRenderer>, std::string> Create(std::shared_ptr<SDL_GPUDevice> device,
                                                                                std::vector<PipelineJob>& moreJobs);

    /// <summary>
    /// Port-only: makes <paramref name="jobs"/> on the machine's threads (this one among them), the slow ones first,
    /// and empties it; a pipeline not made is logged with SDL's error. A driver compiles each pipeline on the thread
    /// that asks for it: on one thread an empty shader cache kept the window black for 9.5 s (the A10, 13 Nov).
    /// </summary>
    static void MakePipelines(std::vector<PipelineJob>& jobs);

    ~MCVulkanRenderer() override;

    void Clear(MCWindow& target, const MCRect& rect, uint8_t color) override;
    void Hash(MCWindow& target, const MCRect& rect, uint8_t color) override;
    void Translate(MCWindow& target, const MCRect& rect, MCColorTable table) override;
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
    void Particles(MCWindow& target, std::span<const MCParticleCommand> commands) override;
    void Glyph(MCWindow& target, const MCGlyphCommand& command) override;
    /// <summary>
    /// Draws the frame's terrain tiles from the mesh in one draw (shaders/terrain.vshader), uploading the mesh and its
    /// tiles when it is new; the pass's tile commands are then left. An error when the mesh can't be held or the fog of
    /// war isn't a frame surface.
    /// </summary>
    std::expected<void, std::string> TerrainLayer(MCWindow& target, const MCTerrainFrame& frame) override;
    void EndTerrainLayer(MCWindow& target) override;
    /// <summary>
    /// Uploads <paramref name="mesh"/> and its tiles unless they're on the GPU already, as they are (the same mesh, HD
    /// art pack and relief maps); the uploads go straight to the GPU, ahead of the frame being recorded.
    /// </summary>
    std::expected<void, std::string> PrepareTerrain(const MCTerrainMesh& mesh) override;

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
    void SetScreenScale(MCOptionalRef<const MCWindow> screen, float scaleX, float scaleY, int32_t right,
                        int32_t bottom);

    /// <summary>
    /// Runs the commands recorded since the last call on <paramref name="commands"/>: uploads what they read, then
    /// draws into the surfaces. The kept surfaces (the fog of war the terrain reads) are drawn first, then the
    /// underlays (<paramref name="underlays"/>), then the rest. On a surface shown over an underlay the key colour is
    /// drawn transparent. <see cref="Seal"/>, then <see cref="Run"/>, on this thread.
    /// </summary>
    std::expected<void, std::string> Execute(SDL_GPUCommandBuffer& commands, std::span<const MCUnderlay> underlays);

    /// <summary>
    /// Port-only: a frame's commands sealed to be run (<see cref="Seal"/>): what they draw and what they read, taken
    /// whole from the recording, with what the run reads of the renderer's state and the game's copied in, so the next
    /// frame can be recorded while it runs on another thread (<see cref="Run"/>). Defined below the class.
    /// </summary>
    struct FrameRun;

    /// <summary>Port-only: destroys a sealed frame.</summary>
    struct DestroyRun
    {
        void operator()(FrameRun* frame) const;
    };

    using SealedFrame = std::unique_ptr<FrameRun, DestroyRun>;

    /// <summary>
    /// Port-only: ends the frame recorded since the last seal and hands its commands over, ready to run
    /// (<see cref="Run"/>): each surface placed as <paramref name="underlays"/> show it, the new table rows, the alpha
    /// colours and the palette queued to go up. Recording goes on into a frame of its own at once. None (null) when
    /// nothing was recorded; an error when the tables can't go up.
    /// </summary>
    std::expected<SealedFrame, std::string> Seal(std::span<const MCUnderlay> underlays);

    /// <summary>
    /// Port-only: runs sealed frame <paramref name="run"/>'s commands on <paramref name="commands"/>: uploads what
    /// they read, then draws into the surfaces. It reads only the frame and the run's own side of the renderer (the
    /// surfaces' textures, the kept ground, the upload and draw buffers; the terrain mesh, which changes only while no
    /// frame runs: <see cref="WaitForRun"/>), so it may run on another thread while the next frame is recorded: one
    /// frame at a time, in the order sealed. The frame is left empty, for <see cref="Recycle"/>.
    /// </summary>
    std::expected<void, std::string> Run(SDL_GPUCommandBuffer& commands, FrameRun& run);

    /// <summary>Port-only: takes a frame back once run, its memory kept for a frame to come (the recording's thread).</summary>
    void Recycle(SealedFrame run);

    /// <summary>
    /// Port-only: set while frames may run on another thread (the presenter's): returns once none runs. Called first by
    /// whatever touches the run's side from the recording's thread: a read-back, a flush, the terrain mesh's upload.
    /// </summary>
    std::function<void()> WaitForRun;

    /// <summary>
    /// Port-only: called every CheckpointDraws draws recorded, while set (the presenter shows a frame that ran
    /// meanwhile as soon as it's done, rather than once the frame being recorded is presented).
    /// </summary>
    std::function<void()> Checkpoint;

    /// <summary>
    /// Port-only: the draws recorded between two <see cref="Checkpoint"/> calls. Few: a draw can take tens of
    /// microseconds to record (HD art, the rain's marks), and a frame that ran waits for the next check to be shown.
    /// </summary>
    static constexpr uint32_t CheckpointDraws = 8;

    /// <summary>The address of the frame surface <paramref name="window"/> is (or lies over), or none.</summary>
    static MCAddressKey SurfaceKey(const MCWindow& window);

    /// <summary>Port-only: a surface's sizes as the last <see cref="Seal"/> placed it.</summary>
    struct SealedPlace
    {
        /// <summary>The size it is drawn at (its textures', once the frame sealed has run).</summary>
        uint32_t Width = 0;
        uint32_t Height = 0;
        /// <summary>The part of its window the drawn size covers, from its corner.</summary>
        uint32_t MappedWidth = 0;
        uint32_t MappedHeight = 0;
    };

    /// <summary>
    /// Port-only: where the last <see cref="Seal"/> placed the surface of <paramref name="key"/>; none when it has no
    /// surface, or none drawn yet.
    /// </summary>
    std::optional<SealedPlace> SealedSize(MCAddressKey key) const;

    /// <summary>
    /// Runs the commands recorded so far (<see cref="Execute"/>) in a command buffer of its own and waits for the GPU to
    /// finish them: before a read of the surfaces ahead of the frame's present, or for a frame that isn't shown.
    /// </summary>
    std::expected<void, std::string> Flush(std::span<const MCUnderlay> underlays);

    /// <summary>The frames whose commands have run (<see cref="Execute"/>), shown or not.</summary>
    uint64_t FramesRun() const { return _Frame; }

    /// <summary>
    /// The colour texture of <paramref name="window"/>'s surface (premultiplied RGBA, as <see cref="Execute"/> left it)
    /// and its size in use; null when the window has no surface yet. Waits for a frame running (<see cref="WaitForRun"/>);
    /// the key's (the run's side's, for the thread running frames) doesn't.
    /// </summary>
    std::shared_ptr<SDL_GPUTexture> SurfaceTexture(const MCWindow& window, uint32_t& width, uint32_t& height) const;
    std::shared_ptr<SDL_GPUTexture> SurfaceTexture(MCAddressKey key, uint32_t& width, uint32_t& height) const;

    /// <summary>
    /// Port-only (HD lighting's living water): the palette indices of <paramref name="window"/>'s surface (R8, the size
    /// of its colour texture), as <see cref="Execute"/> left them; null when the window has no surface yet. As
    /// <see cref="SurfaceTexture"/>, the window's waits, the key's doesn't.
    /// </summary>
    std::shared_ptr<SDL_GPUTexture> SurfaceIndexTexture(const MCWindow& window) const;
    std::shared_ptr<SDL_GPUTexture> SurfaceIndexTexture(MCAddressKey key) const;

    /// <summary>
    /// Port-only (HD weather's drops catching the light): the light the raindrops on <paramref name="window"/>'s
    /// surface caught (premultiplied RGBA, the size of its colour texture: Surface::Glints), as <see cref="Execute"/>
    /// left it; a texture of one transparent texel when its last frame's draws caught none; null when the window has
    /// no surface yet. As <see cref="SurfaceTexture"/>, the window's waits, the key's doesn't.
    /// </summary>
    std::shared_ptr<SDL_GPUTexture> SurfaceGlintTexture(const MCWindow& window) const;
    std::shared_ptr<SDL_GPUTexture> SurfaceGlintTexture(MCAddressKey key) const;

    /// <summary>
    /// The drawn pixels per window pixel of <paramref name="window"/>'s surface across and down (above 1 for a screen
    /// drawn at the size it's shown, <see cref="SetScreenScale"/>); 1, 1 when it has none. As
    /// <see cref="SurfaceTexture"/>, the window's waits, the key's doesn't.
    /// </summary>
    std::pair<float, float> SurfaceScale(const MCWindow& window) const;
    std::pair<float, float> SurfaceScale(MCAddressKey key) const;

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

    /// <summary>
    /// Port-only: the draw calls of the last frame <see cref="Execute"/> ran (each a batch of draws), once it has run
    /// (<see cref="WaitForRun"/>).
    /// </summary>
    int64_t LastFrameBatches() const
    {
        WaitForRunning();
        return _LastFrameBatches;
    }

    /// <summary>
    /// Port-only (HD weather): the render passes of the last frame <see cref="Execute"/> ran that drew into a surface's
    /// glints (Surface::Glints), those on a surface whose draws caught light.
    /// </summary>
    int64_t LastFrameGlintPasses() const
    {
        WaitForRunning();
        return _LastFrameGlintPasses;
    }

    /// <summary>
    /// Port-only (HD weather), for the tests: every surface drawn with its glints, as though its draws caught light in
    /// every frame (the frames the same either way).
    /// </summary>
    static inline bool GlintsEveryFrame = false;

    /// <summary>The terrain layers drawn from the mesh so far.</summary>
    int64_t TerrainLayersDrawn() const { return _TerrainLayersDrawn; }

    /// <summary>
    /// The ground meshes put on the GPU so far, and the times their tiles went up with them (a mesh of the tiles
    /// already up sends its cells alone).
    /// </summary>
    uint64_t TerrainUploads() const { return _TerrainUploads; }
    int64_t TerrainTileUploads() const { return _TerrainTileUploads; }

    /// <summary>
    /// A frame's uploads are staged and sent through memory kept for the next frames up to this many bytes each (the
    /// staging and the transfer buffer); a frame that sends more sends it through memory of its own, gone once the
    /// frame has run, so the burst isn't held all session.
    /// </summary>
    static constexpr size_t KeptUploadBytes = 16 * 1024 * 1024;

    /// <summary>
    /// The bytes kept for the next frames' uploads (the staging's, the frame being recorded's and the one run last's,
    /// and the transfer buffer's), once the frame sealed last has run.
    /// </summary>
    size_t UploadBytesKept() const;

    /// <summary>The shapes drawn from the HD art pack so far (<see cref="MCHdArt"/>).</summary>
    int64_t HdDrawn() const { return _HdDrawn; }

    /// <summary>
    /// Port-only (HD movies), for the tests: the colours the GPU made of movie frame <paramref name="frame"/>
    /// (<see cref="MCHdArt::ColorFormat::Yuv420"/>; RGBA8 rows, its size), read back, waiting for the GPU. The frame
    /// must have been drawn, and its frame run, and be its movie's latest.
    /// </summary>
    std::expected<std::vector<uint8_t>, std::string> MovieColors(const MCHdArt::Frame& frame);

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
        std::reference_wrapper<const MCWindow> Window;
        bool Scaled = false;
        /// <summary>
        /// The data it drew (a shape table, a tile, a font...; to the end of its registered block) and the frame, or the
        /// window it copied from, where it has them.
        /// </summary>
        std::span<const uint8_t> Data;
        int32_t Frame = -1;
        MCOptionalRef<const MCWindow> From;
        /// <summary>The window pixels its draws covered.</summary>
        int64_t Pixels = 0;
        bool Sharp = false;
        /// <summary>Whether it was line art redrawn as strokes (MCSharpLines).</summary>
        bool LineArt = false;
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
    /// <summary>
    /// Port-only: an upload too big for a frame's staging (the ground's, hundreds of megabytes with HD art), written
    /// straight into a few transfer buffers in turn, each sent up on a command buffer of its own as it fills.
    /// </summary>
    class ChunkedUpload;

    /// <summary>A texture and its allocated size.</summary>
    struct Texture
    {
        /// <summary>Held too by the uploads and the frame's pictures that use it until the frame has run.</summary>
        std::shared_ptr<SDL_GPUTexture> Handle;
        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    /// <summary>A draw's fields (shaders/instance.vshader's <c>Draw</c>).</summary>
    using DrawFields = std::array<int32_t, 24>;

    /// <summary>
    /// Port-only: where a picture lies in the textures its draws read: its pixels' corner (an HD art frame's indices'),
    /// its HD colours' corner, and its size, 0 x 0 for a picture with a texture of its own (whose size it is). Its draws
    /// carry it (shaders/instance.vshader's <c>Draw::place</c>), so pictures in one page share their bindings.
    /// </summary>
    struct PicturePlace
    {
        uint16_t X = 0;
        uint16_t Y = 0;
        uint16_t HdX = 0;
        uint16_t HdY = 0;
        uint16_t Width = 0;
        uint16_t Height = 0;

        bool operator==(const PicturePlace&) const = default;
    };

    /// <summary>Port-only: destroys a shared page's places.</summary>
    struct DestroyPlaces
    {
        void operator()(smol_atlas_t* places) const;
    };

    /// <summary>
    /// Port-only: a page pictures share (HD art frames, or sharp glyphs), so the draws of any of them share their
    /// bindings (<see cref="SameBindings"/>): its texture (the HD frames' indices, or the glyphs' pixels) and the HD
    /// frames' colours, each packed by smol-atlas (the colours in whole BC7 blocks when they're BC7), and how many
    /// pictures it holds. An emptied page lets its textures go, its slot left for the next page.
    /// </summary>
    struct SharedPage
    {
        Texture HdColors;
        Texture Texture;
        std::unique_ptr<smol_atlas_t, DestroyPlaces> Places;
        std::unique_ptr<smol_atlas_t, DestroyPlaces> HdPlaces;
        uint32_t Pictures = 0;
    };

    /// <summary>Port-only: the pages a picture may share (none: a picture with textures of its own).</summary>
    enum class PageKind : uint8_t
    {
        None,
        Hd,
        Sharp,
    };

    /// <summary>
    /// Port-only: a kind of shared page: its texture's format and side, and its HD colours' (format
    /// SDL_GPU_TEXTUREFORMAT_INVALID: none), placed in blocks of HdBlock pixels a side.
    /// </summary>
    struct SharedLayout
    {
        SDL_GPUTextureFormat Format = SDL_GPU_TEXTUREFORMAT_INVALID;
        uint32_t Side = 0;
        SDL_GPUTextureFormat HdFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        uint32_t HdSide = 0;
        uint32_t HdBlock = 1;
    };

    /// <summary>Port-only: a shared page's place let go, freed once the frame that let it go has run.</summary>
    struct RetiredPlace
    {
        PageKind Kind = PageKind::None;
        uint32_t Page = 0;
        smol_atlas_item_t* Item = nullptr;
        smol_atlas_item_t* HdItem = nullptr;
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
        /// colours (an opaque one clears it), so a unit in front of a drop hides its light. Only a frame whose draws
        /// on the surface catch light (CatchesLight) draws into it, the first made then; the others' passes have the
        /// colours and indices alone, and the composite reads no light caught.
        /// </summary>
        Texture Glints;
        /// <summary>
        /// Port-only (HD weather): whether a draw recorded on the surface this frame caught light (a glint), and
        /// whether Glints holds what the frames drawn so far left in it (not since a frame drawn without it, nor
        /// since it was made).
        /// </summary>
        bool CatchesLight = false;
        bool GlintsHeld = false;
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
        /// <summary>Port-only: the channels of the targets its pipeline writes (WrittenChannels).</summary>
        uint32_t Channels = 0;
        /// <summary>
        /// Its draws: from First, Count of them, once laid out (LayOutDraws). While recording, First is its first draw
        /// and Last its latest, each draw linked to its next by _DrawNext.
        /// </summary>
        uint32_t First = 0;
        uint32_t Last = 0;
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
        /// <summary>The mesh's cells drawn (those of the grid).</summary>
        uint32_t CellCount = 0;
        /// <summary>
        /// Port-only (KeepGround), taken as the frame is sealed: the fog's flags (1: the home side sees the vertex;
        /// FogColumns a row; none without the fog's pixels), and each haze table row's bytes as the table texture holds
        /// them (none for a row past it; GroundLooks, PlanGround).
        /// </summary>
        std::vector<uint8_t> Seen;
        int32_t FogColumns = 0;
        std::array<std::optional<std::array<uint8_t, 256>>, std::extent_v<decltype(HazeRows)>> HazeTables;
    };

    /// <summary>
    /// Port-only: a surface as a frame was sealed (<see cref="Seal"/>): its window's address (none: let go, its textures
    /// go once the frame has run), the sizes its commands saw and it is drawn at, whether its draws catch light, where
    /// it is drawn among the surfaces (0 kept, 1 shown as an underlay, 2 the rest), and whether its key is drawn
    /// transparent (it lies over an underlay).
    /// </summary>
    struct SealedSurface
    {
        MCAddressKey Key;
        uint32_t RecordedWidth = 0;
        uint32_t RecordedHeight = 0;
        uint32_t MappedWidth = 0;
        uint32_t MappedHeight = 0;
        uint32_t DrawnWidth = 0;
        uint32_t DrawnHeight = 0;
        bool CatchesLight = false;
        int32_t Rank = 2;
        bool KeyClear = false;
    };

    /// <summary>
    /// Port-only (the kept ground, KeepGround): a terrain tile as a cell draws it (terrain.vshader's Image): its box's
    /// size, its hot spot and the width its header gives; and the palette indices its pixels hold.
    /// </summary>
    struct TerrainTileShape
    {
        int32_t Width = 0;
        int32_t Height = 0;
        int32_t HotX = 0;
        int32_t HotY = 0;
        int32_t HeaderWidth = 0;
        std::bitset<256> Colors;
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
        /// <summary>Port-only: the mesh's tiles the atlases hold (a mesh of the same tiles keeps them).</summary>
        std::vector<std::vector<uint8_t>> Tiles;
        std::shared_ptr<SDL_GPUBuffer> Cells;
        std::shared_ptr<SDL_GPUBuffer> Images;
        int32_t FirstRow = 0;
        int32_t FirstCol = 0;
        int32_t Cols = 0;
        int32_t Rows = 0;
        /// <summary>
        /// Port-only (KeepGround): the cells' tiles and corner elevations as uploaded, and the tiles as they are drawn,
        /// so the kept ground can tell which cells' draws changed.
        /// </summary>
        std::vector<uint32_t> CellTiles;
        std::vector<uint32_t> CellElevations;
        std::vector<TerrainTileShape> Shapes;
        /// <summary>Port-only: which upload this is (TerrainUploads), so the kept ground knows the mesh it was drawn from.</summary>
        uint64_t Upload = 0;
    };

    /// <summary>
    /// Port-only: the ground (the terrain layer drawn over nothing) kept from frame to frame for the surface the
    /// terrain draws on first, so a frame redraws only the ground that changed: what the view scrolled onto, and the
    /// cells whose draw changed (fog of war, the water's colour cycle, a tile now dropped or drawn at the view's edges).
    /// It is in one of two pairs of textures the surface's size (colours, indices); when the view moved, the next
    /// frame's is made in the other from this one, shifted. A frame then lays it over the surface where the terrain
    /// layer would be drawn (shaders/ground.pshader): the same pixels wherever the terrain's first pass draws (all but
    /// the void past the map's ring of off-map cells, where its seams' blends over the surface may round apart). Only
    /// with HD ground, whose two passes are most of a frame's drawing.
    /// </summary>
    struct KeptGround
    {
        std::array<Texture, 2> Colors;
        std::array<Texture, 2> Indices;
        /// <summary>The pair holding the ground, and whether one does.</summary>
        size_t Current = 0;
        bool Held = false;
        /// <summary>The surface (its index and window) and the terrain mesh's upload it was drawn for.</summary>
        int32_t SurfaceIndex = -1;
        MCAddressKey SurfaceKey;
        uint64_t Upload = 0;
        /// <summary>The terrain layer drawn (the camera's place, the grid), the fog's flags (1: the home side sees the
        /// vertex; FogColumns a row) and the colour cycle's step.</summary>
        MCTerrainFrame Frame;
        std::vector<uint8_t> Seen;
        int32_t FogColumns = 0;
        int32_t CycleStep = 0;
        /// <summary>
        /// The camera's place the frame before (the origin of the last terrain layer drawn, kept or not): a view moving
        /// so its pixels can't be kept moved draws its terrain directly until it holds still.
        /// </summary>
        std::array<int32_t, 2> LastOrigin{INT32_MIN, INT32_MIN};
        /// <summary>
        /// The rest it was drawn with, as bytes (GroundLooks): the sizes, the terrain's steps and clip, the haze's
        /// tables, the palette (but the cycled colours) and the frame's uniforms (but the cycle's step).
        /// </summary>
        std::vector<uint8_t> Looks;
    };

    /// <summary>Port-only: what shaders/ground.pshader draws of the kept ground (its modes).</summary>
    enum class KeptGroundMode : int32_t
    {
        Copy,
        Clear,
        Over,
        Indices
    };

    /// <summary>Port-only (KeepGround): how a frame brings the kept ground up to date.</summary>
    struct GroundPlan
    {
        /// <summary>Whether it is drawn again whole (else from the kept one, shifted).</summary>
        bool Whole = true;
        /// <summary>How far the kept pixels moved (drawn pixels).</summary>
        std::array<int32_t, 2> Shift{};
        /// <summary>The parts drawn again (drawn pixels), when not whole.</summary>
        std::vector<MCRect> Redrawn;
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
    /// Port-only: the channels of a surface's targets that a record's pipeline writes (DrawPipelines::For's, or the
    /// shadow's), four bits a target in the targets' order, red first.
    /// </summary>
    static uint32_t WrittenChannels(bool blend, bool indexOnly, bool shadow, bool marks);
    /// <summary>
    /// Port-only: whether a draw on <paramref name="surface"/> reading <paramref name="source"/> and writing
    /// <paramref name="channels"/> over <paramref name="written"/> leaves every pixel as it was when drawn before
    /// <paramref name="later"/>, a record added before it, rather than after.
    /// </summary>
    static bool DrawsApart(const Record& later, uint16_t surface, SourceKind source, uint32_t sourceIndex,
                           uint32_t channels, const MCRect& written);
    /// <summary>Port-only: puts <paramref name="run"/>'s draws in their records' order, each record's together (Record::First).</summary>
    void LayOutDraws(FrameRun& run);
    /// <summary>Adds particle <paramref name="command"/>'s draw to <paramref name="surface"/> (Particle).</summary>
    void AddParticle(uint16_t surface, const MCParticleCommand& command);
    /// <summary>
    /// Port-only: the draw shader's build a draw with <paramref name="op"/> goes through: the basic one for the
    /// original's own kinds (a fill, hash, texture, Gouraud span or texel walk, without smooth edges), the HD one for an
    /// HD art frame (KindHd, KindHdSmooth), else the one for every kind.
    /// </summary>
    static DrawVariant VariantOf(uint32_t op);
    /// <summary>
    /// Port-only: the colour <paramref name="record"/>'s first draw (of <paramref name="run"/>'s) wipes every texel
    /// of <paramref name="surface"/> with (an opaque fill of all of its window), if it does, so a pass starting with it
    /// can start cleared to that.
    /// </summary>
    static std::optional<uint8_t> WipeColor(const FrameRun& run, const Record& record, const Surface& surface);
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
    /// Port-only: where an HD art frame's edges come from: its alpha (an effect's blended pixels), the same within a
    /// hard border (a picture's: a menu's art, a movie's frame, whose rectangle's edge is drawn whole, nothing outside it
    /// counting as undrawn, so pictures side by side or one in another's hole meet without a seam), or the original's
    /// pixels it draws, smoothed at the output's resolution (KindHdSmooth: a sprite's or an overlay's frame, whose alpha
    /// is only their mask scaled up, steps and all).
    /// </summary>
    enum class HdEdges : uint8_t
    {
        Frame,
        Picture,
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
    /// Port-only (HD art): adds frame <paramref name="shapeNum"/> of <paramref name="table"/> turned as
    /// MCRenderer::DrawingTurn says (its HD art pack frame; nothing without one), upright its first column and row at
    /// window (<paramref name="left"/>, <paramref name="top"/>), <paramref name="mirrored"/> its first column drawn its
    /// last, through <paramref name="paint"/> (a paint scheme) when given; or, while drawing a sun shadow, the shadow the
    /// sun casts of it as it lies above the ground (MCSpriteTurn::Lift). Within <paramref name="clip"/> (window pixels,
    /// inclusive; the window's when none).
    /// </summary>
    void AddTurned(MCWindow& target, std::span<const uint8_t> table, int32_t shapeNum, int32_t left, int32_t top,
                   bool mirrored, const std::optional<MCColorTable>& paint, const std::optional<Quad>& clip);

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
    /// LengthenedShadowPixel; 0: cast as it is), of the frame's <paramref name="piece"/> alone (draw.pshader's
    /// HdMapPixel; 0: all of it). Whether it was added.
    /// </summary>
    bool AddMappedSunShadow(uint16_t surface, const MCHdArt::Frame& frame, uint32_t picture, const Quad& at,
                            const std::array<float, 6>& mapping, uint32_t reach, uint32_t piece = 0);

    /// <summary>
    /// Port-only: lays the last draw added's map of the window into <paramref name="frame"/>'s original pixels
    /// (<paramref name="mapping"/>: u's three terms, then v's) where a KindHdMap draw reads it.
    /// </summary>
    void SetDrawMapping(const MCHdArt::Frame& frame, const std::array<float, 6>& mapping);

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

    /// <summary>
    /// Port-only: a copy onto sharp screen <paramref name="surface"/> over <paramref name="at"/> (window pixels) that
    /// lies wholly in the key of a keyed HD picture drawn before it since the draws last ran (_KeyedPictures) fills that
    /// part of its hole: the picture's draw carries it (at most FilledHolesMost), and where the copy meets the hole's
    /// edges they aren't softened. Bilinear filtering took the key's transparent texels into the picture's pixels beside
    /// the hole, and the transparent outside into the copy's border pixels: each covered only part of the pixels where
    /// they meet, and the black under them showed as a dark line (shaders/draw.pshader's HdColourBesideHoles).
    /// </summary>
    void FillHoles(uint16_t surface, const Quad& at);
    /// <summary>Whether every original pixel of <paramref name="frame"/> in <paramref name="rect"/> (inclusive) is
    /// <paramref name="key"/>, as a keyed copy leaves it out.</summary>
    static bool KeyedThroughout(const MCHdArt::Frame& frame, const Quad& rect, uint8_t key);

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
    /// Port-only (HD movies): <see cref="HdPictureOf"/> for a movie's frame (<see cref="MCHdArt::ColorFormat::Yuv420"/>):
    /// its movie's picture (<see cref="MoviePicture"/>), made the first time, with the frame's planes and indices
    /// uploaded into it and its colours made from them (<see cref="ConvertMovies"/>) when it holds another frame.
    /// </summary>
    std::optional<uint32_t> MoviePictureOf(const MCHdArt::Frame& frame);
    /// <summary>
    /// Port-only (HD movies): makes the colours of each movie picture given a frame in <paramref name="run"/> from
    /// its planes (shaders/movie.pshader), a render pass each, after the frame's uploads and before its draws.
    /// </summary>
    void ConvertMovies(SDL_GPUCommandBuffer& commands, FrameRun& run);
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
    /// its bytes, <paramref name="drawn"/> its parts drawn by hand. False when it can't be put on the GPU.
    /// </summary>
    bool AddSharpLines(uint16_t surface, std::span<const uint8_t> art, int32_t columns, int32_t rows, const Quad& at,
                       const Quad& from, uint32_t tableRow, std::span<const MCSharpLines::MCDrawnLoop> drawn);
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
    /// <summary>
    /// Queues the bytes of an upload into a rectangle of a texture (in the frame being recorded, or
    /// <paramref name="run"/>): where to write them.
    /// </summary>
    std::span<uint8_t> QueueUpload(std::shared_ptr<SDL_GPUTexture> texture, uint32_t x, uint32_t y, uint32_t width,
                                   uint32_t height, uint32_t bytesPerPixel);
    static std::span<uint8_t> QueueUpload(FrameRun& run, std::shared_ptr<SDL_GPUTexture> texture, uint32_t x,
                                          uint32_t y, uint32_t width, uint32_t height, uint32_t bytesPerPixel);
    /// <summary>Notes a command the GPU can't draw yet.</summary>
    void NotSupported(std::string_view command);

    std::expected<void, std::string> EnsureTexture(Texture& texture, SDL_GPUTextureFormat format,
                                                   SDL_GPUTextureUsageFlags usage, uint32_t width, uint32_t height);
    static void Release(Texture& texture);
    /// <summary>Takes the copy of a surface's <paramref name="rect"/> (window pixels; between render passes).</summary>
    static void TakeCopy(SDL_GPUCommandBuffer& commands, Surface& surface, const MCRect& rect);
    /// <summary>Sets each surface's drawn size for this frame (<paramref name="underlays"/> are drawn as shown).</summary>
    void PlaceSurfaces(std::span<const MCUnderlay> underlays);
    /// <summary>
    /// (Re)makes a surface's textures at its drawn size and clears them, as a window's new pixels start (to
    /// <paramref name="blank"/>, palette index 0's colour, and index 0).
    /// </summary>
    std::expected<void, std::string> MakeSurfaceTextures(SDL_GPUCommandBuffer& commands, Surface& surface,
                                                         SDL_Color blank);
    /// <summary>
    /// Puts <paramref name="mesh"/> on the GPU (its tiles in the terrain atlas, its cells and images; its cells alone
    /// when the tiles up are its own), through a <see cref="ChunkedUpload"/>.
    /// </summary>
    std::expected<void, std::string> UploadTerrainMesh(const MCTerrainMesh& mesh);
    /// <summary>Puts <paramref name="mesh"/>'s cells in a new cell buffer, through <paramref name="upload"/>.</summary>
    std::expected<void, std::string> UploadTerrainCells(ChunkedUpload& upload, const MCTerrainMesh& mesh);
    /// <summary>The HD art pack in use (MCHdArt::PackVersion), or 0 when HD art is off.</summary>
    static uint64_t HdPackInUse();
    /// <summary>
    /// Whether the terrain takes the HD art pack's relief maps (HD lighting on, MCLighting::ReliefMaps above 0).
    /// </summary>
    static bool ReliefMapsInUse();
    /// <summary>Makes one of the terrain's HD atlases (BC7, MaxHdAtlasSide wide), or says why there is none.</summary>
    std::expected<void, std::string> MakeHdTerrainAtlas(
        Texture& atlas, uint32_t height, SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM);
    /// <summary>
    /// Puts an HD terrain tile's colours into HD atlas <paramref name="atlas"/> at (x, y), a block corner, through
    /// <paramref name="upload"/>.
    /// </summary>
    std::expected<void, std::string> UploadHdTile(ChunkedUpload& upload, Texture& atlas, const MCHdArt::Frame& frame,
                                                  uint32_t x, uint32_t y);
    /// <summary>Releases the terrain mesh's textures and buffers.</summary>
    void ReleaseTerrainMesh();
    /// <summary>The pipelines drawing into a surface's targets, with its glints or without (defined below).</summary>
    struct SurfacePipelines;
    /// <summary>
    /// Port-only (HD weather): the pipelines for a pass on <paramref name="surface"/> this frame: with its glints when
    /// its draws catch light.
    /// </summary>
    const SurfacePipelines& PipelinesFor(const Surface& surface) const;
    /// <summary>
    /// Execution: draws a terrain layer of <paramref name="run"/> in the render pass under way, through
    /// <paramref name="pipelines"/>.
    /// </summary>
    void DrawTerrain(SDL_GPUCommandBuffer& commands, SDL_GPURenderPass& pass, const SurfacePipelines& pipelines,
                     const FrameRun& run, const Surface& surface, const TerrainDraw& draw,
                     std::span<const std::byte> frameUniforms);
    /// <summary>
    /// Port-only (KeepGround): what the kept ground depends on for <paramref name="draw"/> of <paramref name="run"/>
    /// on <paramref name="surface"/> but the camera, the fog and the colour cycle's step, as bytes:
    /// <paramref name="frameUniforms"/> are the terrain's (the step left out).
    /// </summary>
    std::vector<uint8_t> GroundLooks(const FrameRun& run, const Surface& surface, const TerrainDraw& draw,
                                     std::span<const std::byte> frameUniforms) const;
    /// <summary>
    /// Port-only (KeepGround): how to bring the kept ground up to date for <paramref name="draw"/> on surface
    /// <paramref name="surfaceIndex"/>, drawn with <paramref name="looks"/>, the fog's flags <paramref name="seen"/>
    /// (<paramref name="fogColumns"/> a row) and colour cycle <paramref name="cycle"/> (the frame's, as sealed): drawn
    /// whole, or shifted and the parts that changed drawn again.
    /// </summary>
    GroundPlan PlanGround(uint16_t surfaceIndex, const TerrainDraw& draw, std::span<const uint8_t> looks,
                          std::span<const uint8_t> seen, int32_t fogColumns, const MCColorCycle& cycle) const;
    /// <summary>
    /// Port-only (KeepGround): draws kept pair <paramref name="from"/> (none for Clear) as <paramref name="mode"/>
    /// says, each pixel from the one <paramref name="shift"/> before it (drawn pixels), at the drawn pixels showing
    /// the terrain's clip in the render pass under way on <paramref name="surface"/>'s size, through
    /// <paramref name="pipelines"/>.
    /// </summary>
    void DrawKeptGround(SDL_GPUCommandBuffer& commands, SDL_GPURenderPass& pass, const SurfacePipelines& pipelines,
                        const Surface& surface, const MCRect& clip, KeptGroundMode mode,
                        std::optional<size_t> from = std::nullopt, std::array<int32_t, 2> shift = {});
    /// <summary>Reads <paramref name="surfaces"/>' indices back (each its texture's size), waiting for the GPU.</summary>
    std::expected<std::vector<std::vector<uint8_t>>, std::string> Download(
        std::span<const std::reference_wrapper<const Surface>> surfaces);
    /// <summary>
    /// Reads <paramref name="textures"/> back whole, <paramref name="bytesPerPixel"/> bytes a pixel (each its rows one
    /// after another), waiting for the GPU.
    /// </summary>
    std::expected<std::vector<std::vector<uint8_t>>, std::string> DownloadTextures(
        std::span<const std::reference_wrapper<const Texture>> textures, uint32_t bytesPerPixel);

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

    /// <summary>
    /// The pipelines that draw into a surface's targets. Port-only (HD weather): a set for passes with its glints and
    /// one for passes with its colours and indices alone (Surface::CatchesLight).
    /// </summary>
    struct SurfacePipelines
    {
        /// <summary>The draws' pipelines, a set for each draw variant (DrawVariant's order).</summary>
        std::array<DrawPipelines, DrawVariants> Draws;
        /// <summary>Port-only: shadows (HD lighting), which take the colours' alpha down by theirs and leave the rest.</summary>
        MCGpuHandle<SDL_GPUGraphicsPipeline> Shadow;
        /// <summary>
        /// The terrain layer's (opaque, with the terrain's fragment shader) and, port-only, its seam pass's, blended
        /// (shaders/terrain.vshader).
        /// </summary>
        MCGpuHandle<SDL_GPUGraphicsPipeline> Terrain;
        MCGpuHandle<SDL_GPUGraphicsPipeline> TerrainSeam;
        /// <summary>
        /// Port-only: the kept ground's (shaders/ground.pshader): opaque (Copy, Clear), blended (Over) and the indices
        /// alone (Indices).
        /// </summary>
        MCGpuHandle<SDL_GPUGraphicsPipeline> Ground;
        MCGpuHandle<SDL_GPUGraphicsPipeline> GroundOver;
        MCGpuHandle<SDL_GPUGraphicsPipeline> GroundIndex;
        /// <summary>Whether every pipeline was made.</summary>
        bool Made() const;
    };

    /// <summary>The surfaces' pipelines: for passes without their glints, then for those with them.</summary>
    std::array<SurfacePipelines, 2> _SurfacePipelines;
    /// <summary>The terrain layer's vertex shader.</summary>
    MCGpuHandle<SDL_GPUShader> _TerrainShader;
    TerrainMeshState _TerrainMesh;
    /// <summary>Port-only: the terrain meshes uploaded (TerrainMeshState::Upload).</summary>
    uint64_t _TerrainUploads = 0;
    /// <summary>Port-only: the kept ground, and the shaders that draw it (shaders/ground.pshader).</summary>
    KeptGround _KeptGround;
    MCGpuHandle<SDL_GPUShader> _QuadShader;
    MCGpuHandle<SDL_GPUShader> _GroundShader;
    /// <summary>
    /// Port-only (HD movies): the movie conversion's shader and pipeline (quad.vshader and shaders/movie.pshader, onto
    /// one RGBA8 target, opaque).
    /// </summary>
    MCGpuHandle<SDL_GPUShader> _MovieShader;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _MoviePipeline;
    /// <summary>The surface whose tile commands a terrain layer drew (-1: none).</summary>
    int32_t _TerrainLayerSurface = -1;
    int64_t _TerrainLayersDrawn = 0;
    int64_t _TerrainTileUploads = 0;

    MCGpuHandle<SDL_GPUSampler> _Nearest;
    /// <summary>A 1x1 texture bound where a draw has no source or no world.</summary>
    Texture _Blank;

    /// <summary>
    /// The surfaces as the frames being recorded see them (the recording's side: their windows, the sizes their
    /// commands saw, the blended draws'), and port-only, as the frames run see them (the run's side, <see cref="Run"/>:
    /// their textures, the copies taken, the sizes the frame run was sealed with), in the same order: a surface keeps its
    /// place for good.
    /// </summary>
    std::vector<Surface> _Surfaces;
    std::vector<Surface> _RunSurfaces;

    // The frame being recorded.
    bool _Recording = false;
    uint64_t _Frame = 0;
    /// <summary>
    /// Port-only: the frame being recorded (its records, draws and uploads; never null), and a frame run and taken back,
    /// kept for the next seal (<see cref="Recycle"/>), so their memory is made once.
    /// </summary>
    SealedFrame _Run;
    SealedFrame _SpareRun;
    /// <summary>Port-only: draws recorded since the last <see cref="Checkpoint"/>.</summary>
    uint32_t _DrawsSinceCheckpoint = 0;
    /// <summary>
    /// Port-only (the run's side): the draws as laid out (LayOutDraws swaps them with a frame's, so neither is made
    /// again).
    /// </summary>
    std::vector<DrawFields> _LaidDraws;
    /// <summary>Port-only: waits for a frame running, when frames may run on another thread (<see cref="WaitForRun"/>).</summary>
    void WaitForRunning() const;
    /// <summary>
    /// Port-only: empties <paramref name="run"/> for a frame to come, its memory kept (a burst's staging let go,
    /// KeptUploadBytes), what it held let go.
    /// </summary>
    static void EmptyRun(FrameRun& run);
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
    /// <summary>The texture the pixels <see cref="Write"/> was given go up into (the run's side; FrameRun::StripPixels).</summary>
    Texture _Strip;

    /// <summary>
    /// An upload waiting for its frame to run (FrameRun::Uploads, run in order): from <c>Buffer</c> at <c>Offset</c>
    /// (the frame's staging when null; rows <c>RowPixels</c> apart, <c>Width</c> when 0), or with <c>From</c> set, a
    /// copy of the same rectangle from that texture. A null <c>Texture</c> was dropped. An upload holds what it reads
    /// and writes until the frame has run.
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

    /// <summary>
    /// The transfer buffer the frames' uploads (their staging, FrameRun::Staging) go up through, kept for the next
    /// frames up to <see cref="KeptUploadBytes"/> (a bigger frame's go once it has run); the run's side.
    /// </summary>
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
        /// <summary>Port-only: its place in _FramePictures when last used (UsePicture keeps it for the frame).</summary>
        uint32_t FrameIndex = UINT32_MAX;
        /// <summary>
        /// Port-only: the pages it shares (none: Texture and HdColors are its own), its page there, its place in the
        /// page's textures (which Texture and HdColors are then) and its places' items (smol-atlas's, freed by the
        /// page's places: <see cref="FreeShared"/>).
        /// </summary>
        PageKind Shares = PageKind::None;
        uint32_t Page = 0;
        PicturePlace Place;
        smol_atlas_item_t* Item = nullptr;
        smol_atlas_item_t* HdItem = nullptr;
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

    /// <summary>
    /// Port-only (HD movies): a movie's frames on the GPU, kept while it shows, each frame put in place of the one
    /// before: its planes (Y, then U and V at half the size each way; R8), uploaded as a frame comes, and made into its
    /// colours (RGBA8, the frame's size: <see cref="ConvertMovies"/>), which its draws read as they read any HD art
    /// frame's, with the frame's indices (R8G8, the original's size).
    /// </summary>
    struct MoviePicture
    {
        /// <summary>The frame's indices (Texture) and colours (HdColors), as an HD art frame's picture.</summary>
        Picture Shown;
        Texture Luma;
        Texture Blue;
        Texture Red;
        /// <summary>The frame it holds (MCHdArt::Frame::Id), and how its planes make colours.</summary>
        uint64_t Frame = 0;
        MCYuvFactors Factors;
        /// <summary>Whether its colours are made from its planes this frame (a frame was uploaded).</summary>
        bool Convert = false;
    };

    /// <summary>
    /// Port-only (HD movies): the movies' pictures, by their movies' numbers (MCHdArt::Frame::Movie), and those let go
    /// whose colours this frame's draws still wait for (<see cref="ConvertMovies"/> makes them, then drops them).
    /// </summary>
    std::unordered_map<uint64_t, MoviePicture> _HdMovies;

    /// <summary>
    /// Port-only (HD movies): a movie picture's colours made from its planes as a frame runs (<see cref="ConvertMovies"/>),
    /// taken as the frame is sealed: the textures, and how the planes make colours.
    /// </summary>
    struct MovieConversion
    {
        Texture Colors;
        Texture Luma;
        Texture Blue;
        Texture Red;
        MCYuvFactors Factors;
    };

    /// <summary>Port-only (HD movies): <see cref="ConvertMovies"/> for one picture.</summary>
    void ConvertMovie(SDL_GPUCommandBuffer& commands, const MovieConversion& movie);
    /// <summary>
    /// Port-only (HD movies): lets movie <paramref name="movie"/>'s picture go, kept till the frame runs when its draws
    /// wait for its colours.
    /// </summary>
    void ReleaseMovie(uint64_t movie);
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
                   std::span<const uint8_t> data = {}, int32_t frame = -1,
                   MCOptionalRef<const MCWindow> from = std::nullopt);
        ~AuditScope();
        AuditScope(const AuditScope&) = delete;
        AuditScope& operator=(const AuditScope&) = delete;

    private:
        MCVulkanRenderer& _Renderer;
        bool _Outermost = false;
    };

    /// <summary>
    /// Drops the pictures of <paramref name="pictures"/> unused for a while (looked for every PictureSweepFrames
    /// frames), then, oldest first, those past <paramref name="budget"/> bytes not drawn in the last frame; at most
    /// TrimmedBytesPerFrame a frame (the rest go later). <paramref name="bytes"/> counts what they take.
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
    /// <summary>
    /// Drops what the GPU keeps for <paramref name="shown"/>, a frame shown over a texture's pixels
    /// (<see cref="MCRenderer::ShowHdPicture"/>) for the last time, before <paramref name="next"/> (none: nothing more):
    /// its picture, or (port-only, HD movies) its movie's once the next isn't a frame of that movie.
    /// </summary>
    void ForgetShownHd(const MCHdArt::Frame& shown, MCOptionalRef<const MCHdArt::Frame> next = std::nullopt);

    /// <summary>Marks <paramref name="picture"/> used this frame; returns its frame picture index.</summary>
    uint32_t UsePicture(Picture& picture);

    /// <summary>
    /// Port-only: the picture redrawn sharp kept by <paramref name="key"/> (<see cref="AddSharpPicture"/>'s), made by
    /// <paramref name="redraw"/> the first time; none when it can't be put on the GPU.
    /// </summary>
    MCOptionalRef<Picture> SharpPicture(uint64_t key, const std::function<std::vector<uint8_t>()>& redraw,
                                        int32_t columns, int32_t rows);
    /// <summary>
    /// Port-only: draws <paramref name="picture"/>, redrawn sharp, over <paramref name="at"/>, read from
    /// <paramref name="from"/>, in the colours table row <paramref name="tableRow"/> gives its bytes, with
    /// <paramref name="flags"/> above the scale in the colour field (shaders/draw.pshader's KindSharpGlyph).
    /// </summary>
    void DrawSharpPicture(uint16_t surface, Picture& picture, const Quad& at, const Quad& from, uint32_t tableRow,
                          uint32_t flags);

    /// <summary>
    /// Port-only: a keyed copy of an HD picture onto a sharp screen since the draws last ran (FillHoles): its surface,
    /// its draw's place in the run's draws (_Run), the picture, where it was copied (window pixels) from which of its pixels, the key,
    /// and how many filled holes its draw carries.
    /// </summary>
    struct KeyedPicture
    {
        uint16_t Surface = 0;
        size_t Draw = 0;
        std::shared_ptr<const MCHdArt::Frame> Frame;
        Quad At{};
        Quad From{};
        uint8_t Key = 0;
        int32_t Holes = 0;
    };

    std::vector<KeyedPicture> _KeyedPictures;
    /// <summary>
    /// A picture of the frame: the texture its draws read, an HD art frame's colours (none: not one), and its place in
    /// them (port-only: a shared page's).
    /// </summary>
    struct FramePicture
    {
        std::shared_ptr<SDL_GPUTexture> Texture;
        std::shared_ptr<SDL_GPUTexture> HdColors;
        PicturePlace Place;
    };

    /// <summary>
    /// Port-only: the pages the HD art frames share and the sharp glyphs', and the places let go while the frames that
    /// read them may still run (freed as the next frame's recording starts).
    /// </summary>
    std::vector<SharedPage> _HdPages;
    std::vector<SharedPage> _SharpPages;
    std::vector<RetiredPlace> _RetiredPlaces;
    /// <summary>Port-only: the shared pages of kind <paramref name="kind"/>.</summary>
    std::vector<SharedPage>& PagesOf(PageKind kind);
    /// <summary>
    /// Port-only: puts <paramref name="picture"/> in a page of kind <paramref name="kind"/> (<paramref name="layout"/>'s;
    /// one made when none has room): <paramref name="width"/> x <paramref name="height"/> of its texture's pixels, and an
    /// HD art frame's colours <paramref name="hdWidth"/> x <paramref name="hdHeight"/> (0 x 0: none); its page's textures
    /// become its own. False, leaving it as it was, for a picture over half a page or when no page could be made.
    /// </summary>
    bool PlaceShared(PageKind kind, const SharedLayout& layout, Picture& picture, uint32_t width, uint32_t height,
                     uint32_t hdWidth, uint32_t hdHeight);
    /// <summary>
    /// Port-only: lets <paramref name="picture"/>'s place in its shared page go (freed as the next frame's recording
    /// starts, <see cref="FreeRetiredPlaces"/>).
    /// </summary>
    void FreeShared(Picture& picture);
    /// <summary>Port-only: frees the places let go (<see cref="FreeShared"/>), releasing the pages they empty.</summary>
    void FreeRetiredPlaces();
    /// <summary>
    /// Whether draws of <paramref name="run"/> from source <paramref name="a"/> and source <paramref name="b"/> of kind
    /// <paramref name="source"/> read the same textures (port-only: pictures in one shared page do).
    /// </summary>
    static bool SameBindings(const FrameRun& run, SourceKind source, uint32_t a, uint32_t b);
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
        /// A stream texture's memory its locked rows are written into (rows the texture's width apart), copied into the
        /// frame's staging as it's unlocked: never memory a frame running on another thread reads.
        /// </summary>
        std::vector<uint8_t> Stream;
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
    /// <summary>Drops what is kept for a texture (its copy goes once the frame recorded has run).</summary>
    void OnTextureReleased(MCTexture& texture) override;
    void OnShowHdPicture(MCTexture& texture, std::shared_ptr<const MCHdArt::Frame> frame, int32_t x,
                         int32_t y) override;
    bool OnShowsHdPictures() const override { return static_cast<bool>(_ScaledScreen); }
    /// <summary>The texture's own memory for its locked rows (TextureState::Stream).</summary>
    std::span<uint8_t> OnLockStream(MCTexture& texture) override;
    /// <summary>Queues the locked rectangle's upload from that memory (from <paramref name="written"/> when not
    /// empty).</summary>
    void OnUnlockStream(MCTexture& texture, std::span<const uint8_t> written) override;

    std::map<std::string, int64_t> _Unsupported;
    /// <summary>The uploads of the frame being recorded, and of the last one run.</summary>
    UploadTally _FrameUploads;
    /// <summary>Port-only: the frame's draw calls so far, and the last frame's (LastFrameBatches).</summary>
    int64_t _FrameBatches = 0;
    int64_t _LastFrameBatches = 0;
    /// <summary>Port-only (HD weather): the frame's passes with the glints so far, and the last frame's.</summary>
    int64_t _FrameGlintPasses = 0;
    int64_t _LastFrameGlintPasses = 0;
    UploadTally _LastFrameUploads;
    /// <summary>The span walks' state (as the software renderer keeps its own).</summary>
    MCSpanState _Spans;
    MirrorTally _Mirror;
    /// <summary>
    /// The last terrain layer's haze tables (for one, two and three corners seen), as copies: the water they draw keeps
    /// its own index on the GPU (draw.pshader's HazedWater), so <see cref="Compare"/> takes the software renderer's
    /// pixel there as one of them maps the GPU's.
    /// </summary>
    std::vector<std::array<uint8_t, 256>> _MirrorHaze;
    /// <summary>The surfaces dumped (-gpudump).</summary>
    std::set<MCAddressKey> _Dumped;
};

/// <summary>
/// Port-only: a frame's commands as recorded, and once sealed (<see cref="MCVulkanRenderer::Seal"/>), what their run
/// reads of the renderer's state and the game's, copied in: nothing the recording of the frames after it changes.
/// </summary>
struct MCVulkanRenderer::FrameRun
{
    // As the frame is recorded.

    std::vector<Record> Records;
    std::vector<DrawFields> Draws;
    /// <summary>
    /// Port-only: each draw's next in its record (NoDraw for its record's latest), and whether a draw went into a
    /// record before the latest, so the draws need laying out in their records' order (LayOutDraws).
    /// </summary>
    std::vector<uint32_t> DrawNext;
    bool DrawsMoved = false;
    /// <summary>The uploads, run in order before the draws, and their bytes (the staging; Upload::Offset).</summary>
    std::vector<Upload> Uploads;
    std::vector<uint8_t> Staging;
    std::vector<TerrainDraw> TerrainDraws;
    /// <summary>The frame's pictures in use (SourceKind::Picture's indices).</summary>
    std::vector<FramePicture> Pictures;
    /// <summary>The pixels <see cref="MCVulkanRenderer::Write"/> was given: a row each (StripWidth wide).</summary>
    std::vector<uint8_t> StripPixels;
    uint32_t StripRows = 0;
    /// <summary>
    /// Textures, transfer buffers and movie pictures let go that the frame's commands may still read: held until it
    /// has run (SDL frees them once the GPU is done with them).
    /// </summary>
    std::vector<Texture> Retired;
    std::vector<std::shared_ptr<SDL_GPUTransferBuffer>> RetiredTransfers;
    std::vector<MoviePicture> RetiredMovies;

    // Taken as it is sealed.

    /// <summary>Every surface, in the renderer's order (a surface keeps its place for good).</summary>
    std::vector<SealedSurface> Surfaces;
    /// <summary>The atlas's pages (SourceKind::Atlas's indices), and the textures every draw reads.</summary>
    std::vector<std::shared_ptr<SDL_GPUTexture>> Pages;
    std::shared_ptr<SDL_GPUTexture> Tables;
    std::shared_ptr<SDL_GPUTexture> Alpha;
    std::shared_ptr<SDL_GPUTexture> Fits;
    std::shared_ptr<SDL_GPUTexture> Palette;
    /// <summary>The palette as shown, the colour cycle, and the HD art pack whose colours the palette's second row holds.</summary>
    std::array<SDL_Color, 256> Colors{};
    MCColorCycle Cycle;
    uint64_t HdPaletteVersion = 0;
    /// <summary>The draws' looks (FrameUniforms::Look: the glows' ovals, the sun shadows' darkness, whether they're cast again).</summary>
    std::array<float, 3> Look{};
    /// <summary>Port-only (HD movies): the movie pictures whose colours the frame makes from their planes.</summary>
    std::vector<MovieConversion> Conversions;
};

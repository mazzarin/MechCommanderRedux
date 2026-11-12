#pragma once

#include "platform/MCPresenter.h"
#include "platform/MCVulkanRenderer.h"

#include <condition_variable>
#include <thread>

/// <summary>
/// Port-only (HD weather's snow): where the specks' lattices lie on an underlay's world (MCWeather::ShownSpeckle,
/// composite.pshader's SpecksAt), in its texels, wrapped: each layer's moved as its specks fall, and all with the
/// ground as the view pans (still as it zooms); and, once placed, the game's time, the texel of the map's middle and the
/// texels a view pixel (across, down) they were last moved at.
/// </summary>
struct MCSpeckLattice
{
    std::array<std::array<double, 2>, 4> Moved{};
    bool Placed = false;
    float Time = 0.0f;
    std::array<float, 2> Ground{};
    std::array<float, 2> Scale{};
};

/// <summary>
/// The Vulkan renderer's presenter: an SDL GPU device (SPIR-V, Vulkan) that owns the window's swapchain. Each frame
/// it composites the screen over its underlays in palette indices into the frame texture at the screen's size
/// (quad.vshader + composite.pshader), and blits the shown view into the swapchain, letterboxed, nearest or linear.
/// </summary>
/// <remarks>
/// <para>What it composites: when its renderer draws the frame surfaces (<see cref="MCGpuDrawing::On"/>), their
/// textures as the renderer left them, and only the palette goes up (when it changed). Otherwise (the software
/// renderer draws them, or mirror mode) the screen, its op plane, the op tables in use and the underlays' surfaces are
/// uploaded each frame: R8 (palette indices), the op tables R8 256 x 256 (row 0 the identity).</para>
/// <para>The palette is RGBA 256 x 1 and the frame texture RGBA: the palette is resolved in the composite, so palette
/// fades and cycling recolour everything, as in the original.</para>
/// <para>Port-only: a frame presented ahead (<see cref="MCFrame::Ahead"/>) runs on the render thread (its own command
/// buffer: the renderer's draws, the uploads and the composite) while the game makes the next; its swapchain image,
/// which only the window's thread may take, is blitted and submitted by the game's thread once the run is done, at its
/// next check (<see cref="ShowFinished"/>: as it pumps the window's events, starts a frame's drawing and every so many
/// draws), else as it presents the next frame. What the run reads of the game is taken as the frame is presented
/// (<see cref="Plan"/>, the renderer's <see cref="MCVulkanRenderer::Seal"/>), so the next frame's drawing changes none
/// of it. One frame runs at a time, and it is shown before the next runs, so one frame texture does.</para>
/// </remarks>
class MCVulkanPresenter final : public MCPresenter
{
    /// <summary>Only <see cref="Create"/> makes one.</summary>
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>A presenter with no device yet.</summary>
    explicit MCVulkanPresenter(Key) {}

    /// <summary>Makes the device, claims <paramref name="window"/> and loads the shaders.</summary>
    static std::expected<std::unique_ptr<MCVulkanPresenter>, std::string> Create(std::shared_ptr<SDL_Window> window,
                                                                                 bool vsync,
                                                                                 const MCPresentation& presentation);

    ~MCVulkanPresenter() override;

    /// <summary>
    /// Port-only (tests): the third swapchain image on every driver, not Vulkan's alone (_ThirdImage), so a test on
    /// the Mac's Metal runs its waits. Set before the presenter is made.
    /// </summary>
    static inline bool ThirdImageEverywhere = false;

    /// <summary>Port-only: the frames presented whose GPU work may not be done yet (_Sent), for the tests.</summary>
    size_t FramesOnGpu() const { return _Sent.size(); }

    MCRendererKind Kind() const override { return MCRendererKind::Vulkan; }
    std::string Name() const override;
    bool CompositesOnGpu() const override { return true; }
    void SetPresentation(const MCPresentation& presentation) override { _Presentation = presentation; }
    void SetVSync(bool on) override;
    std::expected<void, std::string> Present(const MCFrame& frame) override;
    std::expected<std::vector<SDL_Color>, std::string> ReadFrame(const MCFrame& frame) override;
    std::expected<std::vector<uint8_t>, std::string> ReadScreen(const MCFrame& frame) override;
    std::expected<void, std::string> Discard(const MCFrame& frame) override;
    MCViewport Viewport(int viewWidth, int viewHeight) const override;
    void ShowFinished() override;
    void ShowPresented() override;

private:
    /// <summary>
    /// Port-only: what a frame's run reads (the renderer's sealed frame, the composite's plan, the frame's layout and
    /// colours), taken as it's presented (<see cref="Plan"/>). Defined in the .cpp; shared by the game's thread and the
    /// render thread while it runs, the error the run's.
    /// </summary>
    struct PlannedFrame;

    /// <summary>
    /// Port-only: the thread frames presented ahead run on: one job at a time, posted by the game's thread, which
    /// waits for it to return before posting the next or touching what it uses.
    /// </summary>
    class RenderThread
    {
    public:
        RenderThread();
        /// <summary>Lets the job running return, then stops the thread.</summary>
        ~RenderThread();
        RenderThread(const RenderThread&) = delete;
        RenderThread& operator=(const RenderThread&) = delete;

        /// <summary>Runs <paramref name="job"/> on the thread; none may be running (<see cref="Wait"/> first).</summary>
        void Post(std::function<void()> job);

        /// <summary>Whether no job runs: the last one posted has returned, what it did seen by the caller.</summary>
        bool Idle() const;

        /// <summary>Returns once no job runs (at once on the thread itself: what it calls is in its own order).</summary>
        void Wait();

    private:
        /// <summary>The thread: runs each job posted, until asked to stop.</summary>
        void Loop(std::stop_token stop);

        mutable std::mutex _Lock;
        /// <summary>Told when a job is posted (and when the thread is asked to stop).</summary>
        std::condition_variable_any _Posted;
        /// <summary>Told when a job returns.</summary>
        std::condition_variable _Returned;
        std::function<void()> _Job;
        bool _Busy = false;
        /// <summary>The thread; last, so it stops before the rest goes.</summary>
        std::jthread _Thread;
    };

    /// <summary>Port-only: where a composited frame (in _Frame) is shown from: its view, and its output size (0: none).</summary>
    struct ShownLayout
    {
        SDL_Rect View{};
        int OutputWidth = 0;
        int OutputHeight = 0;

        /// <summary>Whether it's composited at the output's size (<see cref="MCFrame::ScaledOutput"/>).</summary>
        bool Scaled() const { return OutputWidth > 0 && OutputHeight > 0; }
    };

    /// <summary>
    /// Port-only: what <paramref name="frame"/>'s run reads, taken now: the renderer's frame sealed (placed as the
    /// frame is shown unless only <paramref name="read"/>: see <see cref="Prepare"/>), and the composite's plan. An
    /// error when the renderer's frame can't be sealed.
    /// </summary>
    std::expected<std::shared_ptr<PlannedFrame>, std::string> Plan(const MCFrame& frame, bool read);
    /// <summary>Port-only: the composite's plan for <paramref name="frame"/> (<see cref="Plan"/>): what it reads of the game.</summary>
    void PlanComposite(const MCFrame& frame, PlannedFrame& planned);
    /// <summary>
    /// Port-only: runs <paramref name="planned"/> on <paramref name="commands"/>: the renderer's draws, the frame's
    /// uploads, the composite into _Frame. On the render thread, or on the game's with none running.
    /// </summary>
    std::expected<void, std::string> RunPlanned(SDL_GPUCommandBuffer& commands, PlannedFrame& planned);
    /// <summary>Port-only: the render thread's job: <paramref name="planned"/> run and submitted on a command buffer of its own.</summary>
    void RunAhead(PlannedFrame& planned);
    /// <summary>Port-only: gives <paramref name="planned"/>'s renderer frame back to the renderer, once it has run.</summary>
    void Recycle(PlannedFrame& planned);
    /// <summary>Port-only: logs <paramref name="error"/>, a frame run ahead's, and keeps it for the next present.</summary>
    void KeepLate(std::string_view error);
    /// <summary>
    /// Port-only: blits _Frame (laid out as <paramref name="layout"/>) into a swapchain image taken on
    /// <paramref name="commands"/>, and submits it, waiting for the image as vsync has it; the game's thread.
    /// </summary>
    std::expected<void, std::string> ShowFrame(SDL_GPUCommandBuffer& commands, const ShownLayout& layout);
    /// <summary>Port-only: shows the frame run ahead (_Unshown), which has run, on a command buffer of its own.</summary>
    std::expected<void, std::string> Show();
    /// <summary>
    /// Port-only: takes back the frame that ran ahead, which has returned: its renderer's frame recycled, its error
    /// kept for the next present (_LateError).
    /// </summary>
    void TakeBack();
    /// <summary>
    /// Port-only: waits for the frame running ahead and shows it, so nothing runs or waits to be shown; the errors of
    /// the frames run ahead since the last present.
    /// </summary>
    std::expected<void, std::string> FinishFrames();
    /// <summary>An R8 or RGBA texture, grown as needed (only its top-left Width x Height is in use).</summary>
    struct Texture
    {
        MCGpuHandle<SDL_GPUTexture> Handle;
        /// <summary>The allocated size.</summary>
        uint32_t Width = 0;
        uint32_t Height = 0;
        /// <summary>Its mip levels.</summary>
        uint32_t Levels = 1;
    };

    /// <summary>Makes sure <paramref name="texture"/> holds at least <paramref name="width"/> x
    /// <paramref name="height"/> (exactly that with <paramref name="exact"/>), in <paramref name="levels"/> mip
    /// levels.</summary>
    std::expected<void, std::string> Ensure(Texture& texture, SDL_GPUTextureFormat format,
                                            SDL_GPUTextureUsageFlags usage, uint32_t width, uint32_t height, bool exact,
                                            uint32_t levels = 1);
    /// <summary>
    /// Whether the frame surfaces' GPU textures are composited (the renderer draws them and the screen has one, once
    /// the frame sealed last has run).
    /// </summary>
    bool ShowsGpuSurfaces(const MCFrame& frame) const;
    /// <summary>
    /// Tells the renderer the scale the underlays are composited at (the window's pixels per screen pixel of a
    /// scaled-up view, else 1), so the world view's surface is drawn at the resolution it is shown at.
    /// </summary>
    void PlaceUnderlays(const MCFrame& frame);
    /// <summary>
    /// Readies the renderer's commands to run with the frame (<see cref="Plan"/>): its colours, its surfaces placed as
    /// the frame is shown, then sealed.
    /// </summary>
    /// <remarks>
    /// A frame only <paramref name="read"/> keeps the underlays where the shown frames placed them: read at the
    /// screen's size, it would otherwise place them at another scale than a scaled window's (a Retina screen's), and
    /// a surface placed at a new size is made again, empty, until the game next draws all of it.
    /// </remarks>
    std::expected<MCVulkanRenderer::SealedFrame, std::string> Prepare(const MCFrame& frame, bool read);
    /// <summary>
    /// Uploads the frame's textures in a copy pass on <paramref name="commands"/> (with the GPU's surfaces, the palette
    /// only; else the frame's screen, op plane and underlays, read where the frame lies: on the game's thread).
    /// </summary>
    std::expected<void, std::string> Upload(SDL_GPUCommandBuffer& commands, const PlannedFrame& planned);
    /// <summary>Bytes to go up into a texture's top left, and (UploadParts') where they lie in the transfer buffer.</summary>
    struct UploadPart
    {
        std::span<const std::byte> Data;
        std::reference_wrapper<SDL_GPUTexture> Texture;
        uint32_t Width;
        uint32_t Height;
        uint32_t BytesPerPixel;
        uint32_t Offset;
    };

    /// <summary>
    /// Uploads <paramref name="parts"/> (none: nothing) in a copy pass on <paramref name="commands"/>, through
    /// <paramref name="buffer"/> (<paramref name="bufferSize"/> bytes; made or grown as needed).
    /// </summary>
    std::expected<void, std::string> UploadParts(SDL_GPUCommandBuffer& commands, std::span<UploadPart> parts,
                                                 MCGpuHandle<SDL_GPUTransferBuffer>& buffer, uint32_t& bufferSize);
    /// <summary>
    /// Composites the frame planned into the frame texture, from the uploaded textures or the GPU's surfaces.
    /// </summary>
    void Composite(SDL_GPUCommandBuffer& commands, PlannedFrame& planned);
    /// <summary>
    /// Port-only (HD lighting): the bloom of underlay <paramref name="index"/>'s GPU world <paramref name="world"/>
    /// (<paramref name="width"/> x <paramref name="height"/>): its bright pass from brightness <paramref name="from"/>
    /// (the lighting's, as the frame was planned) into the underlay's bloom texture (_Blooms), at half its size, and
    /// that texture's mips, recorded on <paramref name="commands"/> outside any pass. None (null) for a world too small
    /// for its levels.
    /// </summary>
    SDL_GPUTexture* Bloom(SDL_GPUCommandBuffer& commands, size_t index, SDL_GPUTexture& world, uint32_t width,
                          uint32_t height, float from);
    static void Release(Texture& texture);

    /// <summary>The window shown in (the display's).</summary>
    std::shared_ptr<SDL_Window> _Window;
    /// <summary>The device; every GPU object holds it, so it goes last.</summary>
    std::shared_ptr<SDL_GPUDevice> _Device;
    bool _Claimed = false;
    bool _VSync = true;
    /// <summary>
    /// Port-only: three swapchain images rather than SDL's two (where the driver is Vulkan): with vsync, a frame that
    /// runs past a refresh then waits for that refresh alone, where with two the next frame waits too and the rate
    /// falls in steps (240, 120, 80 fps on a 240 Hz screen). The game is still held to two frames on the GPU, as two
    /// images held it (_Sent).
    /// </summary>
    bool _ThirdImage = false;
    /// <summary>Port-only (with the third image): the fences of the frames presented last, oldest first.</summary>
    std::vector<SDL_GPUFence*> _Sent;
    /// <summary>
    /// Port-only (with the third image): waits for the frame before last's GPU work (_Sent); an error when the wait
    /// fails (a lost device).
    /// </summary>
    std::expected<void, std::string> HoldFramesOnGpu();
    MCPresentation _Presentation;
    MCGpuHandle<SDL_GPUShader> _QuadShader;
    MCGpuHandle<SDL_GPUShader> _CompositeShader;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _CompositePipeline;
    /// <summary>
    /// Port-only (HD lighting): the bloom's bright pass (Bloom) and the GPU world's field pass, each the composite's
    /// shader built for it alone (shaders/composite_bright.pshader, composite_field.pshader).
    /// </summary>
    MCGpuHandle<SDL_GPUShader> _BrightShader;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _BrightPipeline;
    MCGpuHandle<SDL_GPUShader> _FieldShader;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _FieldPipeline;
    /// <summary>
    /// Port-only (HD weather): the composite without the weather, built without the rain's wet look, the weather's veil
    /// and the snow's cover (shaders/composite_dry.pshader), for a quad where none shows.
    /// </summary>
    MCGpuHandle<SDL_GPUShader> _DryShader;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _DryPipeline;
    /// <summary>
    /// Port-only (HD weather's snow): the weather's composite built without the snow's specks
    /// (shaders/composite_unspecked.pshader), for a quad where the weather shows but no specks do.
    /// </summary>
    MCGpuHandle<SDL_GPUShader> _UnspeckedShader;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _UnspeckedPipeline;
    /// <summary>
    /// Port-only (HD weather): the GPU world's puddle pass, the composite's shader built for it alone
    /// (shaders/composite_puddle.pshader).
    /// </summary>
    MCGpuHandle<SDL_GPUShader> _PuddleShader;
    MCGpuHandle<SDL_GPUGraphicsPipeline> _PuddlePipeline;
    MCGpuHandle<SDL_GPUSampler> _Nearest;
    /// <summary>Port-only (HD lighting): linear between texels and mips, for the bloom.</summary>
    MCGpuHandle<SDL_GPUSampler> _Linear;
    Texture _Screen;
    Texture _Ops;
    Texture _Tables;
    Texture _Palette;
    /// <summary>The composited frame, RGBA at the screen's size.</summary>
    Texture _Frame;
    /// <summary>The underlays' surfaces, in the order of the frame's underlays.</summary>
    std::vector<Texture> _Underlays;
    /// <summary>Port-only (HD lighting): the underlays' blooms (Bloom), in the same order.</summary>
    std::vector<Texture> _Blooms;
    /// <summary>
    /// Port-only (HD lighting): the underlays' fields (composite.pshader's Field: the bloom and the clouds' cover at the
    /// bloom's texels), in the same order.
    /// </summary>
    std::vector<Texture> _Fields;
    /// <summary>
    /// Port-only (HD weather): the underlays' puddles' fields (composite.pshader's PuddleCell), in the same order.
    /// </summary>
    std::vector<Texture> _Puddles;
    /// <summary>
    /// Port-only (HD lighting): the underlays' light tiles (composite.pshader's lightTileTex), in the same order, and
    /// one bound where there are none (never read; made empty).
    /// </summary>
    std::vector<Texture> _LightTiles;
    Texture _NoLightTiles;
    /// <summary>Port-only (HD weather's snow): the underlays' specks' lattices, in the same order.</summary>
    std::vector<MCSpeckLattice> _Specks;
    /// <summary>The upload buffer, and its size; the light tiles' (uploaded after the frame's textures), and its.</summary>
    MCGpuHandle<SDL_GPUTransferBuffer> _Upload;
    uint32_t _UploadSize = 0;
    MCGpuHandle<SDL_GPUTransferBuffer> _LightTileUpload;
    uint32_t _LightTileUploadSize = 0;
    /// <summary>The renderer of the frame surfaces (the renderers refer to it while it is set as the hardware one).</summary>
    std::shared_ptr<MCVulkanRenderer> _Renderer;
    /// <summary>
    /// Port-only: the frame posted to the render thread and not taken back yet (<see cref="TakeBack"/>), the layout of
    /// the frame run ahead and not shown yet, and the errors of frames run or shown since the last present.
    /// </summary>
    std::shared_ptr<PlannedFrame> _Running;
    std::optional<ShownLayout> _Unshown;
    std::string _LateError;
    /// <summary>Port-only: the render thread; last, so it stops before what it uses goes.</summary>
    RenderThread _Thread;
};

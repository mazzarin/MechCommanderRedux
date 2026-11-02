#include "stdafx.h"
#include "platform/MCRenderer.h"
#include "platform/MCNeverDestroyed.h"
#include "platform/MCSoftwareRenderer.h"
#include "vfx/MCVfxView.h"

namespace
{
    /// <summary>The hardware renderer (the Vulkan presenter's) and who draws the frame surfaces.</summary>
    std::weak_ptr<MCRenderer> HardwareRenderer;
    MCGpuDrawing Drawing = MCGpuDrawing::Off;
    std::optional<MCGpuDrawing> Requested;

    /// <summary>Calls <paramref name="visit"/> with every renderer there is.</summary>
    template <typename Visit> void ForEachRenderer(Visit visit)
    {
        visit(static_cast<MCRenderer&>(MCSoftwareRenderer::Instance()));

        if (const std::shared_ptr<MCRenderer> hardware = HardwareRenderer.lock())
        {
            visit(*hardware);
        }
    }

    /// <summary>The address of a window's pixels (none for a window without them).</summary>
    MCAddressKey PixelsOf(const MCWindow& window)
    {
        return window.Pixels != nullptr ? MCAddressKey::Of(window.Pixels[0]) : MCAddressKey();
    }

    // The registries are never destroyed: the display and the view windows (globals, and objects on the game's heaps)
    // remove themselves from them as they go, which can be during the exit's static destruction.

    /// <summary>A frame surface, and whether its memory is kept drawn too.</summary>
    struct FrameSurface
    {
        std::weak_ptr<MCWindow> Window;
        /// <summary>The window's address, to find it by after it has gone.</summary>
        MCAddressKey Key;
        bool Kept;
    };

    /// <summary>The frame surfaces.</summary>
    std::vector<FrameSurface>& FrameSurfaces()
    {
        static MCNeverDestroyed<std::vector<FrameSurface>> surfaces;
        return *surfaces;
    }

    /// <summary>The frame surface <paramref name="window"/> is (or lies over the pixels of), and its window.</summary>
    struct FoundSurface
    {
        std::shared_ptr<MCWindow> Window;
        bool Kept = false;
    };

    FoundSurface FrameSurfaceEntry(const MCWindow& window)
    {
        const MCAddressKey key = MCAddressKey::Of(window);
        const MCAddressKey pixels = PixelsOf(window);

        for (const FrameSurface& surface : FrameSurfaces())
        {
            std::shared_ptr<MCWindow> held = surface.Window.lock();

            if (held != nullptr && (surface.Key == key || (pixels && pixels == PixelsOf(*held))))
            {
                return FoundSurface{std::move(held), surface.Kept};
            }
        }

        return {};
    }

    /// <summary>The underlays set (see <see cref="MCRenderer::SetUnderlay"/>).</summary>
    std::vector<MCUnderlay>& UnderlayList()
    {
        static MCNeverDestroyed<std::vector<MCUnderlay>> underlays;
        return *underlays;
    }

    /// <summary>A window with an op plane: the window and its pixels (when it was set), and the plane.</summary>
    struct OpPlaneEntry
    {
        MCAddressKey Window;
        MCAddressKey Pixels;
        std::weak_ptr<std::vector<uint8_t>> Ops;
    };

    /// <summary>The windows with an op plane, and their planes.</summary>
    std::vector<OpPlaneEntry>& OpPlanes()
    {
        static MCNeverDestroyed<std::vector<OpPlaneEntry>> planes;
        return *planes;
    }

    /// <summary>The frame's op tables, and their bookkeeping.</summary>
    struct OpTableSet
    {
        OpTableSet() { Reset(); }

        void Reset()
        {
            for (int32_t i = 0; i < 256; ++i)
            {
                Rows[i] = static_cast<uint8_t>(i);
            }

            Count = 1;
            ByTable.clear();
            Composed.clear();
        }

        std::array<uint8_t, 256 * 256> Rows{};
        int32_t Count = 1;
        /// <summary>The rows by the address of the table they were taken from.</summary>
        std::map<MCAddressKey, uint8_t> ByTable;
        /// <summary>first << 8 | second → their composition.</summary>
        std::unordered_map<uint16_t, uint8_t> Composed;
    };

    OpTableSet& Tables()
    {
        static MCNeverDestroyed<OpTableSet> tables;
        return *tables;
    }
}

namespace
{
    /// <summary>
    /// The renderer of a view: each command, already clipped to the view's window and scissor by the front end, is
    /// moved by the view's origin and drawn on its target by the target's renderer.
    /// </summary>
    class MCViewRenderer final : public MCRenderer
    {
    public:
        /// <summary>Draws into <paramref name="view"/> until the next call.</summary>
        MCViewRenderer& Bind(const MCView& view)
        {
            SDL_assert(view.Target != nullptr && !view.Target->View.has_value());
            _Target = view.Target != nullptr ? *view.Target : Unbound();
            _KeyTransparent = view.KeyTransparent;
            _X = view.OriginX;
            _Y = view.OriginY;
            return *this;
        }

        void Clear(MCWindow&, const MCRect& rect, uint8_t color) override
        {
            if (!Drops(color))
            {
                Target().Clear(Window(), Move(rect), color);
            }
        }

        void Hash(MCWindow&, const MCRect& rect, uint8_t color) override
        {
            if (!Drops(color))
            {
                Target().Hash(Window(), Move(rect), color);
            }
        }

        void Copy(MCWindow&, const MCCopyCommand& command) override
        {
            // A view has no pixels to copy from.
            SDL_assert(!command.Source.get().View.has_value());

            if (command.Source.get().View.has_value())
            {
                return;
            }

            MCCopyCommand moved = command;
            moved.X += _X;
            moved.Y += _Y;

            if (_KeyTransparent && !moved.ColorKey)
            {
                moved.ColorKey = true;
                moved.Key = 0xff;
            }

            Target().Copy(Window(), moved);
        }

        void AlphaBlit(MCWindow&, const MCAlphaBlitCommand& command) override
        {
            Target().AlphaBlit(Window(), Move(command));
        }

        void ShapeBlit(MCWindow&, const MCShapeBlitCommand& command) override
        {
            MCShapeBlitCommand moved = command;
            moved.Blit = Move(command.Blit);
            Target().ShapeBlit(Window(), moved);
        }

        void Write(MCWindow&, int32_t x, int32_t y, std::span<const uint8_t> pixels) override
        {
            if (!_KeyTransparent)
            {
                Target().Write(Window(), x + _X, y + _Y, pixels);
                return;
            }

            // The runs between the key pixels.
            const auto count = static_cast<int32_t>(pixels.size());
            int32_t start = 0;

            while (start < count)
            {
                while (start < count && pixels[start] == 0xff)
                {
                    ++start;
                }

                int32_t end = start;

                while (end < count && pixels[end] != 0xff)
                {
                    ++end;
                }

                if (end > start)
                {
                    Target().Write(Window(), x + start + _X, y + _Y,
                                   pixels.subspan(static_cast<size_t>(start), static_cast<size_t>(end - start)));
                }

                start = end;
            }
        }

        void Pixel(MCWindow&, int32_t x, int32_t y, uint8_t color) override
        {
            if (!Drops(color))
            {
                Target().Pixel(Window(), x + _X, y + _Y, color);
            }
        }

        void Shape(MCWindow&, const MCShapeCommand& command) override
        {
            MCShapeCommand moved = command;
            moved.Top += _Y;
            moved.Left += _X;
            moved.Lo += _X;
            moved.Hi += _X;
            moved.ClipTop += _Y;
            moved.ClipBottom += _Y;
            Target().Shape(Window(), moved);
        }

        void FastShape(MCWindow&, const MCFastShapeCommand& command) override
        {
            MCFastShapeCommand moved = command;
            moved.Top += _Y;
            moved.StartX += _X;
            moved.ClipX0 += _X;
            moved.ClipX1 += _X;
            moved.ClipY0 += _Y;
            moved.ClipY1 += _Y;
            Target().FastShape(Window(), moved);
        }

        void Tile(MCWindow&, const MCTileCommand& command) override
        {
            MCTileCommand moved = command;
            moved.Left += _X;
            moved.Top += _Y;
            moved.Lo += _X;
            moved.Hi += _X;
            Target().Tile(Window(), moved);
        }

        void Polygon(MCWindow&, const MCPolygonCommand& command) override
        {
            MCPolygonCommand moved = command;
            moved.OriginX += _X;
            moved.OriginY += _Y;
            Target().Polygon(Window(), moved);
        }

        void MapQuad(MCWindow&, const MCMapQuadCommand& command) override
        {
            MCMapQuadCommand moved = command;

            for (MCMapQuadVertex& corner : moved.Corners)
            {
                corner.X += _X;
                corner.Y += _Y;
            }

            moved.Clip = Move(command.Clip);
            Target().MapQuad(Window(), moved);
        }

        void Line(MCWindow&, const MCLineCommand& command) override
        {
            if (!command.Table.has_value() && Drops(command.Color))
            {
                return;
            }

            MCLineCommand moved = command;
            moved.X += _X;
            moved.Y += _Y;
            Target().Line(Window(), moved);
        }

        void Ellipse(MCWindow&, const MCEllipseCommand& command) override
        {
            if (!command.Alpha && Drops(command.Color))
            {
                return;
            }

            MCEllipseCommand moved = command;
            moved.CenterX += _X;
            moved.CenterY += _Y;
            moved.Clip = Move(command.Clip);
            Target().Ellipse(Window(), moved);
        }

        void StatusBar(MCWindow&, const MCStatusBarCommand& command) override
        {
            MCStatusBarCommand moved = command;
            moved.Box = Move(command.Box);
            moved.FrameTop += _Y;
            moved.FrameBottom += _Y;
            Target().StatusBar(Window(), moved);
        }

        void Shadow(MCWindow&, const MCShadowCommand& command) override
        {
            MCShadowCommand moved = command;
            moved.X0 += static_cast<float>(_X);
            moved.Y0 += static_cast<float>(_Y);
            moved.X1 += static_cast<float>(_X);
            moved.Y1 += static_cast<float>(_Y);
            Target().Shadow(Window(), moved);
        }

        void Particle(MCWindow&, const MCParticleCommand& command) override
        {
            MCParticleCommand moved = command;
            moved.X0 += static_cast<float>(_X);
            moved.Y0 += static_cast<float>(_Y);
            moved.X1 += static_cast<float>(_X);
            moved.Y1 += static_cast<float>(_Y);
            Target().Particle(Window(), moved);
        }

        void Glyph(MCWindow&, const MCGlyphCommand& command) override
        {
            MCGlyphCommand moved = command;
            moved.X += _X;
            moved.Y += _Y;
            Target().Glyph(Window(), moved);
        }

    protected:
        void OnAlphaTableChanged() override {}

    private:
        /// <summary>The view's target, for the command being handed on.</summary>
        MCWindow& Window() const { return _Target.get(); }

        /// <summary>The target while no view is bound (never drawn into: a view is bound before every command).</summary>
        static MCWindow& Unbound()
        {
            static MCWindow none{};
            return none;
        }

        MCRenderer& Target() const { return MCRenderer::For(Window()); }

        MCRect Move(const MCRect& rect) const { return MCRect{rect.X0 + _X, rect.Y0 + _Y, rect.X1 + _X, rect.Y1 + _Y}; }

        /// <summary><paramref name="blit"/> moved by the view's origin: where it writes and the rectangle it keeps to.</summary>
        MCAlphaBlitCommand Move(const MCAlphaBlitCommand& blit) const
        {
            MCAlphaBlitCommand moved = blit;
            moved.Left += _X;
            moved.Top += _Y;
            moved.Clip = Move(blit.Clip);
            return moved;
        }

        /// <summary>Whether a write of <paramref name="color"/> draws nothing (0xff in a colour-keyed view).</summary>
        bool Drops(uint8_t color) const { return _KeyTransparent && color == 0xff; }

        /// <summary>The bound view's target, origin and key (for the one command being handed on).</summary>
        std::reference_wrapper<MCWindow> _Target{Unbound()};
        bool _KeyTransparent = false;
        int32_t _X = 0;
        int32_t _Y = 0;
    };

    /// <summary>Mirror mode's renderer of the frame surfaces: each command goes to the hardware renderer, then the
    /// software one.</summary>
    class MCMirrorRenderer final : public MCRenderer
    {
    public:
        void Clear(MCWindow& target, const MCRect& rect, uint8_t color) override
        {
            Gpu().Clear(target, rect, color);
            Cpu().Clear(target, rect, color);
        }

        void Hash(MCWindow& target, const MCRect& rect, uint8_t color) override
        {
            Gpu().Hash(target, rect, color);
            Cpu().Hash(target, rect, color);
        }

        void Copy(MCWindow& target, const MCCopyCommand& command) override
        {
            Gpu().Copy(target, command);
            Cpu().Copy(target, command);
        }

        void AlphaBlit(MCWindow& target, const MCAlphaBlitCommand& command) override
        {
            Gpu().AlphaBlit(target, command);
            Cpu().AlphaBlit(target, command);
        }

        void ShapeBlit(MCWindow& target, const MCShapeBlitCommand& command) override
        {
            Gpu().ShapeBlit(target, command);
            Cpu().ShapeBlit(target, command);
        }

        void Write(MCWindow& target, int32_t x, int32_t y, std::span<const uint8_t> pixels) override
        {
            Gpu().Write(target, x, y, pixels);
            Cpu().Write(target, x, y, pixels);
        }

        void Pixel(MCWindow& target, int32_t x, int32_t y, uint8_t color) override
        {
            Gpu().Pixel(target, x, y, color);
            Cpu().Pixel(target, x, y, color);
        }

        void Shape(MCWindow& target, const MCShapeCommand& command) override
        {
            Gpu().Shape(target, command);
            Cpu().Shape(target, command);
        }

        void FastShape(MCWindow& target, const MCFastShapeCommand& command) override
        {
            Gpu().FastShape(target, command);
            Cpu().FastShape(target, command);
        }

        void Tile(MCWindow& target, const MCTileCommand& command) override
        {
            Gpu().Tile(target, command);
            Cpu().Tile(target, command);
        }

        void Polygon(MCWindow& target, const MCPolygonCommand& command) override
        {
            Gpu().Polygon(target, command);
            Cpu().Polygon(target, command);
        }

        void MapQuad(MCWindow& target, const MCMapQuadCommand& command) override
        {
            Gpu().MapQuad(target, command);
            Cpu().MapQuad(target, command);
        }

        void Line(MCWindow& target, const MCLineCommand& command) override
        {
            Gpu().Line(target, command);
            Cpu().Line(target, command);
        }

        void Ellipse(MCWindow& target, const MCEllipseCommand& command) override
        {
            Gpu().Ellipse(target, command);
            Cpu().Ellipse(target, command);
        }

        void StatusBar(MCWindow& target, const MCStatusBarCommand& command) override
        {
            Gpu().StatusBar(target, command);
            Cpu().StatusBar(target, command);
        }

        void Glyph(MCWindow& target, const MCGlyphCommand& command) override
        {
            Gpu().Glyph(target, command);
            Cpu().Glyph(target, command);
        }

        // Only the GPU keeps the light a shadow falls in.
        void Shadow(MCWindow& target, const MCShadowCommand& command) override { Gpu().Shadow(target, command); }

        // The GPU draws the layer from its mesh; the software renderer draws the pass's tiles, which still come.
        std::expected<void, std::string> TerrainLayer(MCWindow& target, const MCTerrainFrame& frame) override
        {
            return Gpu().TerrainLayer(target, frame);
        }

        void EndTerrainLayer(MCWindow& target) override { Gpu().EndTerrainLayer(target); }

    protected:
        void OnAlphaTableChanged() override {}

    private:
        // The presenter that set the hardware renderer owns it while commands reach it.
        static MCRenderer& Gpu() { return *HardwareRenderer.lock(); }
        static MCRenderer& Cpu() { return MCSoftwareRenderer::Instance(); }
    };
}

std::expected<void, std::string> MCRenderer::TerrainLayer(MCWindow& /*target*/, const MCTerrainFrame& /*frame*/)
{
    return std::unexpected("this renderer draws no terrain layer");
}

void MCRenderer::EndTerrainLayer(MCWindow& /*target*/)
{
}

void MCRenderer::Shadow(MCWindow& /*target*/, const MCShadowCommand& /*command*/)
{
}

void MCRenderer::Particle(MCWindow& /*target*/, const MCParticleCommand& /*command*/)
{
}

MCRenderer& MCRenderer::For(const MCWindow& window)
{
    if (window.View.has_value())
    {
        static MCViewRenderer viewRenderer;
        return viewRenderer.Bind(*window.View);
    }

    if (Drawing != MCGpuDrawing::Off)
    {
        if (const FoundSurface surface = FrameSurfaceEntry(window); surface.Window != nullptr)
        {
            // A kept surface's memory is read by the game, so the software renderer draws it as well. The presenter
            // that set the hardware renderer owns it while commands reach it.
            if (const std::shared_ptr<MCRenderer> hardware = HardwareRenderer.lock(); hardware != nullptr)
            {
                static MCMirrorRenderer mirror;
                return Drawing == MCGpuDrawing::Mirror || surface.Kept ? static_cast<MCRenderer&>(mirror) : *hardware;
            }
        }
    }

    // The software renderer draws into the picture's memory: its texture is no longer what the GPU holds.
    if (const std::shared_ptr<MCTexture> texture = window.Texture.lock(); texture != nullptr)
    {
        texture->Dirty = true;

        if (texture->CpuStale)
        {
            NoteStale("a draw into a stream texture whose pixels went to the GPU alone");
        }
    }

    return MCSoftwareRenderer::Instance();
}

namespace
{
    /// <summary>The textures made and not yet destroyed (each knows its slot).</summary>
    std::vector<std::shared_ptr<MCTexture>>& TextureList()
    {
        static MCNeverDestroyed<std::vector<std::shared_ptr<MCTexture>>> textures;
        return *textures;
    }

    int64_t Unregistered = 0;

    /// <summary>How many <see cref="MCRenderer::ExpectUnregistered"/> scopes are alive.</summary>
    int32_t UnregisteredExpected = 0;

    /// <summary>Whether a <see cref="MCRenderer::GlowScope"/> is drawing a glow.</summary>
    bool GlowDrawn = false;

    /// <summary>The in-between a <see cref="MCRenderer::InBetweenScope"/> is drawing, none outside one.</summary>
    std::optional<MCInBetween> InBetweenDrawn;

    /// <summary>Whether a <see cref="MCRenderer::SunShadowScope"/> is drawing a unit's sun shadow.</summary>
    bool SunShadowDrawn = false;

    /// <summary>A window's pixels as a span (none for a window without them).</summary>
    std::span<uint8_t> WindowPixels(const MCWindow& window)
    {
        return window.Bytes();
    }
}

std::shared_ptr<MCTexture> MCRenderer::CreateTexture(std::span<uint8_t> pixels, int32_t width, int32_t height,
                                                     MCTextureUse use)
{
    auto& texture = TextureList().emplace_back(std::make_shared<MCTexture>());
    texture->Pixels = pixels;
    texture->Width = width;
    texture->Height = height;
    texture->Use = use;
    texture->Slot = TextureList().size() - 1;
    return texture;
}

std::shared_ptr<MCTexture> MCRenderer::CreateTexture(MCWindow& window, MCTextureUse use)
{
    DestroyTexture(window);
    std::shared_ptr<MCTexture> texture = CreateTexture(WindowPixels(window), window.XMax + 1, window.YMax + 1, use);
    window.Texture = texture;
    return texture;
}

void MCRenderer::ResizeTexture(MCTexture& texture, std::span<uint8_t> pixels, int32_t width, int32_t height)
{
    if (texture.Width != width || texture.Height != height)
    {
        if (const std::shared_ptr<MCRenderer> hardware = HardwareRenderer.lock())
        {
            hardware->OnTextureReleased(texture);
        }

        texture.Width = width;
        texture.Height = height;
    }

    texture.Pixels = pixels;
    texture.Dirty = true;
}

void MCRenderer::ResizeTexture(MCWindow& window)
{
    if (const std::shared_ptr<MCTexture> texture = window.Texture.lock(); texture != nullptr)
    {
        ResizeTexture(*texture, WindowPixels(window), window.XMax + 1, window.YMax + 1);
    }
}

void MCRenderer::DestroyTexture(MCTexture& texture)
{
    if (const std::shared_ptr<MCRenderer> hardware = HardwareRenderer.lock())
    {
        hardware->OnTextureReleased(texture);
    }

    auto& textures = TextureList();
    const size_t slot = texture.Slot;
    std::swap(textures[slot], textures.back());
    textures[slot]->Slot = slot;
    textures.pop_back();
}

void MCRenderer::DestroyTexture(std::weak_ptr<MCTexture>& texture)
{
    if (const std::shared_ptr<MCTexture> held = texture.lock())
    {
        DestroyTexture(*held);
    }

    texture.reset();
}

void MCRenderer::DestroyTexture(MCWindow& window)
{
    DestroyTexture(window.Texture);
}

std::span<uint8_t> MCRenderer::LockTexture(MCTexture& texture)
{
    return LockTexture(texture, MCRect{0, 0, texture.Width - 1, texture.Height - 1});
}

std::span<uint8_t> MCRenderer::LockTexture(MCTexture& texture, const MCRect& rect)
{
    SDL_assert(rect.X0 >= 0 && rect.Y0 >= 0 && rect.X1 < texture.Width && rect.Y1 < texture.Height &&
               rect.X0 <= rect.X1 && rect.Y0 <= rect.Y1);
    texture.Locked = rect;
    texture.LockedOnHardware = false;
    const std::shared_ptr<MCRenderer> hardware = HardwareRenderer.lock();

    if (texture.Use == MCTextureUse::Stream && hardware != nullptr && Drawing == MCGpuDrawing::On)
    {
        if (const std::span<uint8_t> mapped = hardware->OnLockStream(texture); !mapped.empty())
        {
            texture.LockedOnHardware = true;
            return mapped;
        }
    }

    return texture.Pixels.subspan(static_cast<size_t>(rect.Y0) * texture.Width + rect.X0);
}

void MCRenderer::UnlockTexture(MCTexture& texture)
{
    const std::shared_ptr<MCRenderer> hardware = HardwareRenderer.lock();

    if (texture.LockedOnHardware)
    {
        hardware->OnUnlockStream(texture, {});
        texture.LockedOnHardware = false;
        texture.CpuStale = true;
        return;
    }

    // Mirror mode sends a stream's pixels up the same way, from its memory.
    if (texture.Use == MCTextureUse::Stream && hardware != nullptr && Drawing == MCGpuDrawing::Mirror && !texture.Dirty)
    {
        const MCRect& rect = texture.Locked;
        hardware->OnUnlockStream(texture,
                                 texture.Pixels.subspan(static_cast<size_t>(rect.Y0) * texture.Width + rect.X0));
        return;
    }

    texture.Dirty = true;
}

void MCRenderer::ShowHdPicture(MCTexture& texture, std::shared_ptr<const MCHdArt::Frame> frame, int32_t x, int32_t y)
{
    if (const std::shared_ptr<MCRenderer> hardware = HardwareRenderer.lock())
    {
        hardware->OnShowHdPicture(texture, std::move(frame), x, y);
    }
}

bool MCRenderer::ShowsHdPictures()
{
    const std::shared_ptr<MCRenderer> hardware = HardwareRenderer.lock();
    return hardware != nullptr && Drawing == MCGpuDrawing::On && hardware->OnShowsHdPictures();
}

std::span<const std::shared_ptr<MCTexture>> MCRenderer::Textures()
{
    return TextureList();
}

void MCRenderer::NoteUnregistered(std::string_view what)
{
    static std::set<std::string, std::less<>> logged;

    if (logged.emplace(what).second)
    {
        SDL_Log("MCRenderer: unregistered: %.*s (its owner never registered it)", static_cast<int>(what.size()),
                what.data());
    }

    ++Unregistered;
    SDL_assert(UnregisteredExpected > 0 && "a draw read data its owner never registered (logged above)");
}

int64_t MCRenderer::UnregisteredDraws()
{
    return Unregistered;
}

MCRenderer::ExpectUnregistered::ExpectUnregistered()
{
    ++UnregisteredExpected;
}

MCRenderer::ExpectUnregistered::~ExpectUnregistered()
{
    --UnregisteredExpected;
}

MCRenderer::GlowScope::GlowScope(bool glow) : _Was(GlowDrawn)
{
    GlowDrawn = _Was || glow;
}

MCRenderer::GlowScope::~GlowScope()
{
    GlowDrawn = _Was;
}

bool MCRenderer::DrawingGlow()
{
    return GlowDrawn;
}

MCRenderer::InBetweenScope::InBetweenScope(const std::optional<MCInBetween>& inBetween) : _Was(InBetweenDrawn)
{
    InBetweenDrawn = inBetween;
}

MCRenderer::InBetweenScope::~InBetweenScope()
{
    InBetweenDrawn = _Was;
}

const std::optional<MCInBetween>& MCRenderer::DrawingInBetween()
{
    return InBetweenDrawn;
}

MCRenderer::SunShadowScope::SunShadowScope(bool shadow) : _Was(SunShadowDrawn)
{
    SunShadowDrawn = _Was || shadow;
}

MCRenderer::SunShadowScope::~SunShadowScope()
{
    SunShadowDrawn = _Was;
}

bool MCRenderer::DrawingSunShadow()
{
    return SunShadowDrawn;
}

void MCRenderer::AddFrameSurface(std::shared_ptr<MCWindow> window, bool kept)
{
    auto& surfaces = FrameSurfaces();
    const MCAddressKey key = MCAddressKey::Of(*window);

    if (std::ranges::any_of(surfaces, [key](const FrameSurface& surface) { return surface.Key == key; }))
    {
        return;
    }

    surfaces.push_back(FrameSurface{window, key, kept});
    const std::shared_ptr<MCRenderer> hardware = HardwareRenderer.lock();

    // A kept surface may hold pixels already: the GPU's copy starts from them.
    if (kept && Drawing != MCGpuDrawing::Off && hardware != nullptr && window->Pixels != nullptr)
    {
        const std::span<const uint8_t> pixels = WindowPixels(*window);
        const auto width = static_cast<size_t>(window->XMax + 1);

        for (int32_t y = 0; y <= window->YMax; ++y)
        {
            hardware->Write(*window, 0, y, pixels.subspan(static_cast<size_t>(y) * width, width));
        }
    }
}

void MCRenderer::RemoveFrameSurface(const MCWindow& window)
{
    const MCAddressKey key = MCAddressKey::Of(window);

    if (std::erase_if(FrameSurfaces(), [key](const FrameSurface& surface) { return surface.Key == key; }) != 0)
    {
        if (const std::shared_ptr<MCRenderer> hardware = HardwareRenderer.lock())
        {
            hardware->OnFrameSurfaceRemoved(window);
        }
    }
}

std::shared_ptr<MCWindow> MCRenderer::FrameSurfaceOf(const MCWindow& window)
{
    return FrameSurfaceEntry(window).Window;
}

std::shared_ptr<MCWindow> MCRenderer::FrameSurfaceOver(MCAddressKey pixels)
{
    if (!pixels)
    {
        return nullptr;
    }

    for (const FrameSurface& surface : FrameSurfaces())
    {
        if (std::shared_ptr<MCWindow> held = surface.Window.lock(); held != nullptr && PixelsOf(*held) == pixels)
        {
            return held;
        }
    }

    return nullptr;
}

bool MCRenderer::KeptSurface(const MCWindow& window)
{
    const FoundSurface surface = FrameSurfaceEntry(window);
    return surface.Window != nullptr && surface.Kept;
}

void MCRenderer::SetHardware(std::shared_ptr<MCRenderer> hardware, MCGpuDrawing drawing)
{
    Drawing = hardware != nullptr ? drawing : MCGpuDrawing::Off;
    HardwareRenderer = hardware;
}

std::shared_ptr<MCRenderer> MCRenderer::Hardware()
{
    return HardwareRenderer.lock();
}

MCGpuDrawing MCRenderer::GpuDrawing()
{
    return Drawing;
}

MCGpuDrawing MCRenderer::RequestedGpuDrawing()
{
    return Requested.value_or(MCGpuDrawing::On);
}

void MCRenderer::RequestGpuDrawing(MCGpuDrawing drawing)
{
    Requested = drawing;
}

namespace
{
    std::filesystem::path& DumpFolder()
    {
        static MCNeverDestroyed<std::filesystem::path> folder;
        return *folder;
    }
}

const std::filesystem::path& MCRenderer::MirrorDumpFolder()
{
    return DumpFolder();
}

void MCRenderer::SetMirrorDumpFolder(const std::filesystem::path& folder)
{
    DumpFolder() = folder;
}

std::optional<MCGpuDrawing> MCGpuDrawingFromName(std::string_view name)
{
    for (const auto& [text, drawing] : {std::pair{"off", MCGpuDrawing::Off}, std::pair{"on", MCGpuDrawing::On},
                                        std::pair{"mirror", MCGpuDrawing::Mirror}})
    {
        if (MCIEquals(name, text))
        {
            return drawing;
        }
    }

    return std::nullopt;
}

namespace
{
    int64_t StaleReads = 0;
}

void MCRenderer::NoteStale(std::string_view what)
{
    static std::set<std::string, std::less<>> logged;

    if (logged.emplace(what).second)
    {
        SDL_Log("MCRenderer: %.*s; its memory is stale", static_cast<int>(what.size()), what.data());
    }

    ++StaleReads;
}

void MCRenderer::NoteCpuRead(const MCWindow& source, std::string_view command)
{
    if (const std::shared_ptr<MCTexture> texture = source.Texture.lock(); texture != nullptr && texture->CpuStale)
    {
        NoteStale(std::format("{} reads a stream texture whose pixels went to the GPU alone", command));
        return;
    }

    const FoundSurface surface = FrameSurfaceEntry(source);

    if (Drawing != MCGpuDrawing::On || surface.Window == nullptr || surface.Kept)
    {
        return;
    }

    NoteStale(std::format("{} reads a frame surface the GPU draws", command));
}

int64_t MCRenderer::StaleCpuReads()
{
    return StaleReads;
}

void MCRenderer::AlphaTableChanged()
{
    ForEachRenderer([](MCRenderer& renderer) { renderer.OnAlphaTableChanged(); });
}

namespace
{
    /// <summary>The registered data blocks, by their first byte.</summary>
    std::map<MCAddressKey, MCDataBlock>& DataBlocks()
    {
        static MCNeverDestroyed<std::map<MCAddressKey, MCDataBlock>> blocks;
        return *blocks;
    }

    /// <summary>The block <see cref="MCRenderer::DataBlockOf"/> found last (most draws read the block before's).</summary>
    std::optional<MCDataBlock> LastDataBlock;

    /// <summary>Drops a registered block: every renderer forgets what it made of it.</summary>
    std::map<MCAddressKey, MCDataBlock>::iterator DropDataBlock(std::map<MCAddressKey, MCDataBlock>::iterator it)
    {
        const MCDataBlock block = it->second;
        LastDataBlock.reset();
        it = DataBlocks().erase(it);
        MCRenderer::DataChanged(block.Begin, static_cast<size_t>(block.End - block.Begin));
        return it;
    }
}

void MCRenderer::RegisterData(std::span<const uint8_t> bytes, MCDataKind kind)
{
    if (bytes.empty())
    {
        return;
    }

    const MCAddressKey first = MCAddressKey::OfData(bytes);
    const MCAddressKey end = first + bytes.size();
    auto& blocks = DataBlocks();
    auto it = blocks.upper_bound(first);

    // A block over any of these bytes is gone: the memory holds the new data now.
    if (it != blocks.begin() && std::prev(it)->second.End > first)
    {
        DropDataBlock(std::prev(it));
    }

    for (it = blocks.lower_bound(first); it != blocks.end() && it->first < end;)
    {
        it = DropDataBlock(it);
    }

    blocks.emplace(first, MCDataBlock{first, end, kind});
}

void MCRenderer::DataChanged(std::span<const uint8_t> bytes)
{
    DataChanged(MCAddressKey::OfData(bytes), bytes.size());
}

void MCRenderer::DataChanged(MCAddressKey begin, size_t size)
{
    // The frame's op rows taken from the bytes keep the old ones (draws made before used them); a new use takes the
    // new bytes.
    auto& byTable = Tables().ByTable;
    byTable.erase(byTable.lower_bound(MCFirstTableReaching(begin)), byTable.lower_bound(begin + size));
    ForEachRenderer([&](MCRenderer& renderer) { renderer.OnDataChanged(begin, size); });
}

void MCRenderer::UnregisterData(std::span<const uint8_t> bytes)
{
    const MCAddressKey first = MCAddressKey::OfData(bytes);
    auto& blocks = DataBlocks();

    for (auto it = blocks.lower_bound(first);
         it != blocks.end() && static_cast<size_t>(it->first - first) < bytes.size();)
    {
        it = DropDataBlock(it);
    }
}

std::optional<MCDataBlock> MCRenderer::DataBlockOf(MCAddressKey at)
{
    if (LastDataBlock.has_value() && at >= LastDataBlock->Begin && at < LastDataBlock->End)
    {
        return LastDataBlock;
    }

    auto& blocks = DataBlocks();
    auto it = blocks.upper_bound(at);

    if (it == blocks.begin() || at >= std::prev(it)->second.End)
    {
        return std::nullopt;
    }

    LastDataBlock = std::prev(it)->second;
    return LastDataBlock;
}

size_t MCRenderer::DataBlockCount()
{
    return DataBlocks().size();
}

void MCRenderer::SetUnderlay(const MCUnderlay& underlay)
{
    for (MCUnderlay& existing : UnderlayList())
    {
        if (existing.Owner == underlay.Owner)
        {
            existing = underlay;
            return;
        }
    }

    UnderlayList().push_back(underlay);
}

void MCRenderer::RemoveUnderlay(MCAddressKey owner)
{
    std::erase_if(UnderlayList(), [owner](const MCUnderlay& underlay) { return underlay.Owner == owner; });
}

std::span<const MCUnderlay> MCRenderer::Underlays()
{
    return UnderlayList();
}

void MCRenderer::SetOpPlane(const MCWindow& target, std::shared_ptr<std::vector<uint8_t>> ops)
{
    auto& planes = OpPlanes();
    const MCAddressKey key = MCAddressKey::Of(target);
    std::erase_if(planes, [key](const OpPlaneEntry& plane) { return plane.Window == key; });

    if (ops != nullptr)
    {
        planes.push_back(OpPlaneEntry{key, PixelsOf(target), ops});
    }
}

std::span<uint8_t> MCRenderer::OpPlane(const MCWindow& target)
{
    const MCAddressKey key = MCAddressKey::Of(target);
    const MCAddressKey pixels = PixelsOf(target);

    for (const OpPlaneEntry& plane : OpPlanes())
    {
        if (plane.Window == key || (plane.Pixels && plane.Pixels == pixels))
        {
            // The plane's owner keeps it while the target is drawn into (the display holds the screen's).
            const std::shared_ptr<std::vector<uint8_t>> ops = plane.Ops.lock();
            return ops != nullptr ? std::span<uint8_t>(*ops) : std::span<uint8_t>();
        }
    }

    return {};
}

std::span<const uint8_t> MCRenderer::OpTables()
{
    return Tables().Rows;
}

int32_t MCRenderer::OpTableCount()
{
    return Tables().Count;
}

uint8_t MCRenderer::OpFor(MCColorTable table)
{
    OpTableSet& tables = Tables();
    const MCAddressKey key = MCAddressKey::OfData(table);

    if (const auto found = tables.ByTable.find(key); found != tables.ByTable.end())
    {
        return found->second;
    }

    const std::optional<MCDataBlock> block = DataBlockOf(key);

    if (!block.has_value() || block->Kind != MCDataKind::Tables || block->BytesFrom(key) < 256)
    {
        NoteUnregistered("a colour table mapping an underlay");
    }

    if (tables.Count == 256)
    {
        return 0;
    }

    const auto op = static_cast<uint8_t>(tables.Count++);
    std::ranges::copy(table, tables.Rows.begin() + static_cast<ptrdiff_t>(op) * 256);
    tables.ByTable.emplace(key, op);
    return op;
}

uint8_t MCRenderer::ComposeOps(uint8_t first, uint8_t second)
{
    if (first == 0)
    {
        return second;
    }

    if (second == 0)
    {
        return first;
    }

    OpTableSet& tables = Tables();
    const auto key = static_cast<uint16_t>(first << 8 | second);

    if (const auto found = tables.Composed.find(key); found != tables.Composed.end())
    {
        return found->second;
    }

    if (tables.Count == 256)
    {
        return 0;
    }

    const auto op = static_cast<uint8_t>(tables.Count++);
    const size_t composed = static_cast<size_t>(op) * 256;
    const size_t a = static_cast<size_t>(first) * 256;
    const size_t b = static_cast<size_t>(second) * 256;

    for (size_t i = 0; i < 256; ++i)
    {
        tables.Rows[composed + i] = tables.Rows[b + tables.Rows[a + i]];
    }

    tables.Composed.emplace(key, op);
    return op;
}

void MCRenderer::ResetOpTables()
{
    Tables().Reset();
}

void MCRenderer::ComposeUnderlays(const MCWindow& target, std::span<uint8_t> pixels, const MCRect& rect)
{
    const int32_t stride = target.XMax + 1;
    const std::span<const uint8_t> ops = OpPlane(target);
    const std::span<const uint8_t> tables = OpTables();
    const MCAddressKey targetPixels = PixelsOf(target);

    for (const MCUnderlay& underlay : UnderlayList())
    {
        const std::shared_ptr<const MCWindow> source = underlay.Source.lock();

        if (!underlay.TargetPixels || underlay.TargetPixels != targetPixels || source == nullptr ||
            source->Pixels == nullptr)
        {
            continue;
        }

        const MCRect& shown = underlay.Rect;
        const int32_t x0 = std::max({rect.X0, shown.X0, 0});
        const int32_t y0 = std::max({rect.Y0, shown.Y0, 0});
        const int32_t x1 = std::min({rect.X1, shown.X1, target.XMax});
        const int32_t y1 = std::min({rect.Y1, shown.Y1, target.YMax});

        if (x1 < x0 || y1 < y0)
        {
            continue;
        }

        // The source pixel under each target pixel's centre: (2 * offset + 1) * source / (2 * shown), as a GPU's
        // nearest sampling picks it.
        const int64_t shownWidth = shown.X1 - shown.X0 + 1;
        const int64_t shownHeight = shown.Y1 - shown.Y0 + 1;
        const int64_t sourceWidth = source->XMax + 1;
        const int64_t sourceHeight = source->YMax + 1;
        const int32_t sourceStride = source->XMax + 1;
        const std::span<const uint8_t> sourcePixels = source->Bytes();

        for (int32_t y = y0; y <= y1; ++y)
        {
            const auto row = static_cast<size_t>(static_cast<intptr_t>(y) * stride);
            const auto sourceY = static_cast<int32_t>((2 * (y - shown.Y0) + 1) * sourceHeight / (2 * shownHeight));
            const auto sourceRow = static_cast<size_t>(static_cast<intptr_t>(sourceY) * sourceStride);

            for (int32_t x = x0; x <= x1; ++x)
            {
                const size_t at = row + static_cast<size_t>(x);

                if (pixels[at] == UnderlayKey)
                {
                    const uint8_t world = sourcePixels[sourceRow + static_cast<size_t>((2 * (x - shown.X0) + 1) *
                                                                                       sourceWidth / (2 * shownWidth))];
                    const uint8_t op = !ops.empty() ? ops[at] : 0;
                    pixels[at] = tables[static_cast<size_t>(op) * 256 + world];
                }
            }
        }
    }
}

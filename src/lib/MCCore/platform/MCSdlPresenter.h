#pragma once

#include "platform/MCPresenter.h"
#include "platform/MCSdlHandle.h"

/// <summary>
/// The software renderer's presenter: the CPU composites the screen's underlays (<see cref="MCRenderer::ComposeUnderlays"/>)
/// and SDL's 2D renderer (whichever backend it picks) shows the result from an <c>SDL_PIXELFORMAT_INDEX8</c> texture,
/// scaled into the window with <c>SDL_SetRenderLogicalPresentation</c>.
/// </summary>
class MCSdlPresenter final : public MCPresenter
{
    /// <summary>Only <see cref="Create"/> makes one.</summary>
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>A presenter with no renderer yet.</summary>
    explicit MCSdlPresenter(Key) {}

    /// <summary>Makes SDL's renderer for <paramref name="window"/>.</summary>
    static std::expected<std::unique_ptr<MCSdlPresenter>, std::string> Create(std::shared_ptr<SDL_Window> window,
                                                                              bool vsync,
                                                                              const MCPresentation& presentation);

    ~MCSdlPresenter() override;

    MCRendererKind Kind() const override { return MCRendererKind::Software; }
    std::string Name() const override;
    bool CompositesOnGpu() const override { return false; }
    void SetPresentation(const MCPresentation& presentation) override;
    void SetVSync(bool on) override;
    std::expected<void, std::string> Present(const MCFrame& frame) override;
    std::expected<std::vector<SDL_Color>, std::string> ReadFrame(const MCFrame& frame) override;
    MCViewport Viewport(int viewWidth, int viewHeight) const override;

private:
    /// <summary>Remakes the textures for a screen of <paramref name="width"/> x <paramref name="height"/>.</summary>
    std::expected<void, std::string> EnsureTextures(int width, int height);
    /// <summary>
    /// Uploads the frame's palette and pixels (composited by the CPU when it has underlays, at the window's pixels
    /// when <see cref="MCFrame::ScaledOutput"/>).
    /// </summary>
    std::expected<void, std::string> Upload(const MCFrame& frame);
    /// <summary>The part of the uploaded texture that is shown: the view, or the whole composite at the output's size.</summary>
    static SDL_Rect ShownRect(const MCFrame& frame);
    /// <summary>Fits <paramref name="view"/> into the window (SDL's logical presentation).</summary>
    void ApplyPresentation(const SDL_Rect& view);

    /// <summary>The window shown in (the display's); destroyed after the renderer.</summary>
    std::shared_ptr<SDL_Window> _Window;
    MCSdlHandle<SDL_Renderer> _Renderer;
    MCSdlHandle<SDL_Palette> _Palette;
    /// <summary>The screen, INDEX8.</summary>
    MCSdlHandle<SDL_Texture> _Texture;
    /// <summary>With linear filtering: the screen converted to RGBA at its size, which is then scaled.</summary>
    MCSdlHandle<SDL_Texture> _Target;
    int _Width = 0;
    int _Height = 0;
    /// <summary>The view the logical presentation was set for.</summary>
    SDL_Rect _View{};
    MCPresentation _Presentation;
    /// <summary>The screen fade the palette was last set with (<see cref="MCFrame::Fade"/>).</summary>
    int _Fade = 0;
    /// <summary>Port-only: whether that fade scaled the colours (<see cref="MCFrame::FadeScales"/>).</summary>
    bool _FadeScales = false;
    /// <summary>The CPU's composite of the screen.</summary>
    std::vector<uint8_t> _Composed;
};

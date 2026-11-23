#include "stdafx.h"
#include "platform/MCPresenter.h"

std::optional<MCRendererKind> MCRendererKindFromName(std::string_view name)
{
    auto equals = [name](std::string_view other)
    {
        return name.size() == other.size() && std::equal(name.begin(), name.end(), other.begin(), [](char a, char b)
                                                         { return std::tolower(static_cast<unsigned char>(a)) == b; });
    };

    if (equals("vulkan"))
    {
        return MCRendererKind::Vulkan;
    }

    if (equals("software"))
    {
        return MCRendererKind::Software;
    }

    return std::nullopt;
}

std::string_view MCRendererKindName(MCRendererKind kind)
{
    return kind == MCRendererKind::Software ? "software" : "vulkan";
}

std::expected<std::vector<uint8_t>, std::string> MCPresenter::ReadScreen(const MCFrame& frame)
{
    const auto size = static_cast<ptrdiff_t>(frame.Width) * frame.Height;
    std::vector<uint8_t> pixels(frame.Pixels.begin(), frame.Pixels.begin() + size);
    MCRenderer::ComposeUnderlays(frame.Screen.get(), pixels, MCRect{0, 0, frame.Width - 1, frame.Height - 1});
    return pixels;
}

void MCComposeScaled(const MCFrame& frame, std::vector<uint8_t>& pixels)
{
    const int outputWidth = frame.OutputWidth;
    const int outputHeight = frame.OutputHeight;
    const SDL_Rect& view = frame.View;
    pixels.resize(static_cast<size_t>(outputWidth) * static_cast<size_t>(outputHeight));

    if (outputWidth <= 0 || outputHeight <= 0 || view.w <= 0 || view.h <= 0)
    {
        return;
    }

    // Screen pixels per output pixel, and the screen pixel under each output column's and row's centre.
    const double stepX = static_cast<double>(view.w) / outputWidth;
    const double stepY = static_cast<double>(view.h) / outputHeight;
    std::vector<int32_t> columns(static_cast<size_t>(outputWidth));
    std::vector<int32_t> rows(static_cast<size_t>(outputHeight));

    for (int x = 0; x < outputWidth; ++x)
    {
        columns[static_cast<size_t>(x)] =
            view.x + std::min(static_cast<int32_t>(std::floor((x + 0.5) * stepX)), view.w - 1);
    }

    for (int y = 0; y < outputHeight; ++y)
    {
        rows[static_cast<size_t>(y)] =
            view.y + std::min(static_cast<int32_t>(std::floor((y + 0.5) * stepY)), view.h - 1);
    }

    for (int y = 0; y < outputHeight; ++y)
    {
        const auto row = static_cast<size_t>(rows[static_cast<size_t>(y)]) * static_cast<size_t>(frame.Width);
        const auto at = static_cast<size_t>(y) * static_cast<size_t>(outputWidth);

        for (int x = 0; x < outputWidth; ++x)
        {
            pixels[at + static_cast<size_t>(x)] =
                frame.Pixels[row + static_cast<size_t>(columns[static_cast<size_t>(x)])];
        }
    }

    const std::span<const uint8_t> tables = MCRenderer::OpTables();

    for (const MCUnderlay& underlay : frame.Underlays)
    {
        const std::shared_ptr<const MCWindow> source = underlay.Source.lock();

        if (source == nullptr || source->Pixels == nullptr)
        {
            continue;
        }

        // The underlay's rectangle in output pixels, and the source pixel under each output pixel's centre.
        const MCRect& shown = underlay.Rect;
        const double left = (shown.X0 - view.x) / stepX;
        const double top = (shown.Y0 - view.y) / stepY;
        const double shownWidth = (shown.X1 - shown.X0 + 1) / stepX;
        const double shownHeight = (shown.Y1 - shown.Y0 + 1) / stepY;
        const int32_t sourceWidth = source->XMax + 1;
        const int32_t sourceHeight = source->YMax + 1;
        const std::span<const uint8_t> sourcePixels = source->Bytes();
        std::vector<int32_t> sourceColumns(static_cast<size_t>(outputWidth));

        for (int x = 0; x < outputWidth; ++x)
        {
            sourceColumns[static_cast<size_t>(x)] = std::clamp(
                static_cast<int32_t>(std::floor((x + 0.5 - left) * sourceWidth / shownWidth)), 0, sourceWidth - 1);
        }

        for (int y = 0; y < outputHeight; ++y)
        {
            const int32_t screenY = rows[static_cast<size_t>(y)];

            if (screenY < shown.Y0 || screenY > shown.Y1)
            {
                continue;
            }

            const int32_t sourceY = std::clamp(
                static_cast<int32_t>(std::floor((y + 0.5 - top) * sourceHeight / shownHeight)), 0, sourceHeight - 1);
            const auto sourceRow = static_cast<size_t>(sourceY) * static_cast<size_t>(sourceWidth);
            const auto screenRow = static_cast<size_t>(screenY) * static_cast<size_t>(frame.Width);
            const auto at = static_cast<size_t>(y) * static_cast<size_t>(outputWidth);

            for (int x = 0; x < outputWidth; ++x)
            {
                const int32_t screenX = columns[static_cast<size_t>(x)];
                uint8_t& pixel = pixels[at + static_cast<size_t>(x)];

                if (screenX < shown.X0 || screenX > shown.X1 || pixel != MCRenderer::UnderlayKey)
                {
                    continue;
                }

                const uint8_t world =
                    sourcePixels[sourceRow + static_cast<size_t>(sourceColumns[static_cast<size_t>(x)])];
                const uint8_t op = !frame.Ops.empty() ? frame.Ops[screenRow + static_cast<size_t>(screenX)] : 0;
                pixel = tables[static_cast<size_t>(op) * 256 + world];
            }
        }
    }
}

SDL_Color MCFadedSurround(const MCFrame& frame)
{
    const auto faded = [&frame](uint8_t component)
    { return static_cast<uint8_t>(std::max(static_cast<int>(component) - frame.Fade, 0)); };
    return SDL_Color{faded(frame.Surround.r), faded(frame.Surround.g), faded(frame.Surround.b), SDL_ALPHA_OPAQUE};
}

MCViewport MCPresentViewport(int outputWidth, int outputHeight, int viewWidth, int viewHeight,
                             const MCPresentation& presentation)
{
    MCViewport view;

    if (outputWidth <= 0 || outputHeight <= 0 || viewWidth <= 0 || viewHeight <= 0)
    {
        return view;
    }

    const float outW = static_cast<float>(outputWidth);
    const float outH = static_cast<float>(outputHeight);

    if (presentation.Stretch)
    {
        return MCViewport{0.0f, 0.0f, outW, outH};
    }

    const float logW = static_cast<float>(viewWidth);
    const float logH = static_cast<float>(viewHeight);
    const float wantAspect = logW / logH;
    const float realAspect = outW / outH;

    if (presentation.IntegerScale)
    {
        float scale = wantAspect > realAspect ? static_cast<float>(outputWidth / viewWidth)
                                              : static_cast<float>(outputHeight / viewHeight);

        if (scale < 1.0f)
        {
            scale = 1.0f;
        }

        view.W = std::floor(logW * scale);
        view.H = std::floor(logH * scale);
        view.X = (outW - view.W) / 2.0f;
        view.Y = (outH - view.H) / 2.0f;
    }
    else if (std::fabs(wantAspect - realAspect) < 0.0001f)
    {
        view = {0.0f, 0.0f, outW, outH};
    }
    else if (wantAspect > realAspect)
    {
        const float scale = outW / logW;
        view.X = 0.0f;
        view.W = outW;
        view.H = std::floor(logH * scale);
        view.Y = (outH - view.H) / 2.0f;
    }
    else
    {
        const float scale = outH / logH;
        view.Y = 0.0f;
        view.H = outH;
        view.W = std::floor(logW * scale);
        view.X = (outW - view.W) / 2.0f;
    }

    return view;
}

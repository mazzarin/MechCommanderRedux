#include "stdafx.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "ai/MCMoveGeometry.h"
#include "vfx/MCVfxFunctions.h"
#include "platform/MCBlockStore.h"

namespace
{
    /// <summary>
    /// Points <paramref name="window"/> and <paramref name="pane"/> at a <paramref name="width"/> x
    /// <paramref name="height"/> bitmap (the pane covers all of it).
    /// </summary>
    void SetExtent(MCWindow& window, std::weak_ptr<MCWindow> shared, MCPane& pane, int32_t width, int32_t height)
    {
        window.XMax = width - 1;
        window.YMax = height - 1;
        pane.Window = std::move(shared);
        pane.X1 = width - 1;
        pane.Y1 = height - 1;
    }

    /// <summary>
    /// Zeroed pixels for a <paramref name="width"/> x <paramref name="height"/> bitmap, none for none (the original
    /// heaps' allocation of 0 bytes failed).
    /// </summary>
    std::shared_ptr<uint8_t[]> AllocatePixels(int32_t width, int32_t height)
    {
        const auto size = static_cast<uint32_t>(width * height);
        return size != 0 ? std::make_shared<uint8_t[]>(size) : nullptr;
    }
}

// aPort

MCGuiPort::~MCGuiPort()
{
    FreeBitmap();
}

auto MCGuiPort::MakeBitmap(int32_t width, int32_t height) -> int32_t
{
    if (_Window != nullptr)
    {
        MCRenderer::DestroyTexture(*_Window);
    }

    _Window = std::make_shared<MCWindow>();
    _Window->XMax = width - 1;
    _Window->YMax = height - 1;

    if (!_Screen)
    {
        _Window->Pixels = AllocatePixels(width, height);

        if (_Window->Pixels == nullptr)
        {
            return 3;
        }
    }

    _Pane = std::make_shared<MCPane>();
    SetExtent(*_Window, _Window, *_Pane, width, height);
    PortWidth = width;
    PortHeight = height;

    if (_Window->Pixels != nullptr)
    {
        MCRenderer::CreateTexture(*_Window, MCTextureUse::Dynamic);
    }

    return 0;
}

auto MCGuiPort::ResizeBitmap(int32_t width, int32_t height) -> int32_t
{
    // A view has no pixels to reallocate, and the screen port's are the display's.
    if (!_Screen && !IsView())
    {
        _Window->Pixels = AllocatePixels(width, height);
    }

    SetExtent(*_Window, _Window, *_Pane, width, height);
    PortWidth = width;
    PortHeight = height;
    MCRenderer::ResizeTexture(*_Window);
    return 0;
}

auto MCGuiPort::FreeBitmap() -> void
{
    if (_Window != nullptr)
    {
        MCRenderer::DestroyTexture(*_Window);
    }

    _Window.reset();
    _Pane.reset();
}

auto MCGuiPort::Init(int32_t width, int32_t height) -> int32_t
{
    if (width == PortWidth && height == PortHeight)
    {
        return 0;
    }

    return MakeBitmap(width, height);
}

auto MCGuiPort::InitScreen(int32_t width, int32_t height) -> void
{
    _Screen = true;
    MakeBitmap(width, height);
}

auto MCGuiPort::Init(int32_t artPacket) -> int32_t
{
    MCPacketFile& artFile = *GuiSystem().ArtFile;

    if (artFile.SeekPacket(artPacket) != 0)
    {
        Fatal(0, "Art packet not found");
    }

    MCFile file;

    if (file.Open(artFile, static_cast<uint32_t>(artFile.GetPacketSize())) != 0)
    {
        Fatal(0, "Cant open child file.");
    }

    const uint32_t size = file.FileSize();

    if (size == 0)
    {
        Fatal(-2, "Bad child file in art.pak");
    }

    std::vector<uint8_t> gif(size);
    file.Read(gif);
    file.Close();

    // The game's own art: a picture it can't draw means a damaged install, which it can't run without.
    const std::expected<int32_t, std::string> resolution = VfxGifResolution(gif);

    if (!resolution.has_value())
    {
        Fatal(-2, "Bad picture in art.pak: ", resolution.error());
    }

    const auto dimensions = static_cast<uint32_t>(*resolution);
    const int32_t result = Init(static_cast<int32_t>(dimensions >> 16), static_cast<int32_t>(dimensions & 0xffff));

    if (result != 0)
    {
        return result;
    }

    // The layout was checked above, so the draw refuses nothing.
    static_cast<void>(VfxGifDraw(*_Pane, gif));
    return 0;
}

auto MCGuiPort::Init(std::string_view fileName) -> int32_t
{
    std::string path;
    MCFile file;
    bool opened = false;

    if (CurPlanet == 1)
    {
        path = std::format("{}x{}", std::string_view(ArtPath), fileName);
        opened = file.Open(path) == 0;
    }

    if (!opened)
    {
        path = std::format("{}{}", std::string_view(ArtPath), fileName);

        if (file.Open(path) != 0)
        {
            path = fileName;

            if (file.Open(path) != 0)
            {
                GeneralMsg(std::format("Error reading '{}'", path));
            }
        }
    }

    const uint32_t size = file.FileSize();

    if (size == 0)
    {
        GeneralMsg(std::format("Error reading '{}'", path));
    }

    std::vector<uint8_t> data(size);
    file.Read(data);
    file.Close();

    // A TGA header: the image width and height are the 16-bit values at +0x0c and +0x0e, and the 8-bit pixels follow
    // the 18-byte header and a 256-entry palette.
    int16_t tgaWidth = 0;
    int16_t tgaHeight = 0;
    std::memcpy(&tgaWidth, data.data() + 0x0c, sizeof(tgaWidth));
    std::memcpy(&tgaHeight, data.data() + 0x0e, sizeof(tgaHeight));
    const int32_t width = tgaWidth;
    const int32_t height = tgaHeight;
    const int32_t result = Init(width, height);

    if (result != 0)
    {
        Fatal(result, std::format("Failed trying to create a {} by {} aPort with dataBuffer", width, height));
    }

    const std::shared_ptr<MCTexture> texture = _Window->Texture.lock();
    std::memcpy(MCRenderer::LockTexture(*texture).data(), data.data() + 0x312, static_cast<size_t>(height * width));
    MCRenderer::UnlockTexture(*texture);
    return 0;
}

auto MCGuiPort::Resize(int32_t width, int32_t height) -> int32_t
{
    return ResizeBitmap(width, height);
}

auto MCGuiPort::InitView(int32_t width, int32_t height) -> int32_t
{
    if (IsView() && width == PortWidth && height == PortHeight)
    {
        return 0;
    }

    FreeBitmap();
    _Window = std::make_shared<MCWindow>();
    _Pane = std::make_shared<MCPane>();
    _Window->View.emplace();
    SetExtent(*_Window, _Window, *_Pane, width, height);
    PortWidth = width;
    PortHeight = height;
    return 0;
}

auto MCGuiPort::OpenView(std::shared_ptr<MCWindow> target, int32_t x, int32_t y, const MCRect& scissor,
                         bool keyTransparent) -> void
{
    MCView& view = View();
    view.OriginX = x;
    view.OriginY = y;
    view.Scissor = scissor;
    view.KeyTransparent = keyTransparent;

    // A view as the target (a block drawn in place, see lBlockPort): the view lands on that view's target, moved by
    // its origin and cut to its scissor; shut when it is.
    if (target != nullptr && target->View.has_value())
    {
        const MCView& outer = *target->View;

        if (outer.Target == nullptr || !outer.Open())
        {
            view.Target = nullptr;
            CloseView();
            return;
        }

        view.Target = outer.Target;
        view.OriginX += outer.OriginX;
        view.OriginY += outer.OriginY;
        view.Scissor.X0 = std::max({scissor.X0, 0}) + outer.OriginX;
        view.Scissor.Y0 = std::max({scissor.Y0, 0}) + outer.OriginY;
        view.Scissor.X1 = std::min(scissor.X1, target->XMax) + outer.OriginX;
        view.Scissor.Y1 = std::min(scissor.Y1, target->YMax) + outer.OriginY;
        view.Scissor.X0 = std::max(view.Scissor.X0, outer.Scissor.X0);
        view.Scissor.Y0 = std::max(view.Scissor.Y0, outer.Scissor.Y0);
        view.Scissor.X1 = std::min(view.Scissor.X1, outer.Scissor.X1);
        view.Scissor.Y1 = std::min(view.Scissor.Y1, outer.Scissor.Y1);
        view.KeyTransparent = keyTransparent || outer.KeyTransparent;
        return;
    }

    view.Target = std::move(target);
}

auto MCGuiPort::OpenViewOn(const MCPane& dest, int32_t xPos, int32_t yPos, bool keyTransparent) -> void
{
    // The block in the destination window's coordinates: the pane cut to its window, and to the view's size.
    std::shared_ptr<MCWindow> target = dest.Window.lock();

    if (target == nullptr)
    {
        CloseView();
        return;
    }

    const int32_t originX = dest.X0 + xPos;
    const int32_t originY = dest.Y0 + yPos;
    const MCRect scissor{std::max({dest.X0, 0, originX}), std::max({dest.Y0, 0, originY}),
                         std::min({dest.X1, target->XMax, originX + PortWidth - 1}),
                         std::min({dest.Y1, target->YMax, originY + PortHeight - 1})};
    OpenView(std::move(target), originX, originY, scissor, keyTransparent);
}

auto MCGuiPort::CopyTo(const MCPane& dest, int32_t xPos, int32_t yPos, bool transparent) const -> void
{
    // A view has no picture to copy (its owner draws itself in the frame pass).
    if (IsView())
    {
        return;
    }

    if (transparent)
    {
        DrawTransparent(dest, *_Window, xPos, yPos, _Window->XMax + 1, _Window->YMax + 1);
        return;
    }

    VfxPaneCopy(*_Pane, 0, 0, dest, xPos, yPos, -1);
}

// aScrollPort

auto MCGuiScrollPort::Init(int32_t width, int32_t height) -> int32_t
{
    return MakeBitmap(width, height);
}

auto MCGuiScrollPort::Resize(int32_t width, int32_t height) -> int32_t
{
    return ResizeBitmap(width, height);
}

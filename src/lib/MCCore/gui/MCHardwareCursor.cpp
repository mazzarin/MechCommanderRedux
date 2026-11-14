#include "stdafx.h"
#include "gui/MCHardwareCursor.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCUpdateDisplay.h"
#include "platform/MCInput.h"
#include "sprite/MCBc7.h"
#include "sprite/MCHdArt.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>The cursor shapes as pictures, made by MCHardwareCursorPreload.</summary>
    std::vector<MCCursorImage> ShapeImages;

    /// <summary>What this frame's cursor carries (MCHardwareCursorCarry).</summary>
    bool Carrying = false;
    MCCursorImage Carried;

    /// <summary>
    /// Port-only: whether the shapes were preloaded, and the HD art pack then (its version, and whether HD art was on):
    /// the pictures are made again when it changes.
    /// </summary>
    bool Preloaded = false;
    uint64_t PreloadedPack = 0;
    bool PreloadedHdArt = false;

    /// <summary>
    /// Port-only: the HD art pack's copy of frame <paramref name="shapeNum"/> (its bounds <paramref name="width"/> x
    /// <paramref name="height"/>) of the cursor shapes <paramref name="table"/> (a whole PAK packet, found by its bytes'
    /// hash as the renderer finds it); none when HD art is off or the pack has none.
    /// </summary>
    std::shared_ptr<const MCCursorHd> HdCursor(std::span<const uint8_t> table, int32_t shapeNum, int32_t width,
                                               int32_t height)
    {
        const std::shared_ptr<const MCHdArt::Frame> frame =
            MCHdArt::Active() ? MCHdArt::FindShape(MCHdArt::ShapeTableHash(table), shapeNum, width, height) : nullptr;

        if (frame == nullptr || frame->Blended)
        {
            return nullptr;
        }

        auto hd = std::make_shared<MCCursorHd>();
        hd->Scale = frame->Scale;
        hd->Rgba = frame->Format == MCHdArt::ColorFormat::Bc7
                       ? MCBc7::Decode(frame->Colors, frame->Width, frame->Height)
                       : frame->Colors;

        if (const std::optional<std::array<uint8_t, MCHdArt::PaletteBytes>>& made = MCHdArt::MadeWith(); made)
        {
            hd->MadeWith.assign(made->begin(), made->end());
        }

        return hd;
    }

    /// <summary>A cursor shape's bounds and hot spot (its origin), as <c>AG_shape_draw</c> draws it.</summary>
    struct ShapeExtent
    {
        int32_t Width = 0;
        int32_t Height = 0;
        int32_t HotX = 0;
        int32_t HotY = 0;
    };

    ShapeExtent ExtentOf(std::span<const uint8_t> shapeTable, int32_t shapeNum)
    {
        const int32_t resolution = VfxShapeResolution(shapeTable, shapeNum);
        const int32_t minXY = VfxShapeMinxy(shapeTable, shapeNum);
        return {resolution >> 16, resolution & 0xffff, -(minXY >> 16), -static_cast<int16_t>(minXY)};
    }

    /// <summary>Draws a shape onto a pane of <paramref name="fill"/>, hot spot at (hotX, hotY).</summary>
    std::vector<uint8_t> DrawShape(std::span<const uint8_t> shapeTable, int32_t shapeNum, int32_t width, int32_t height,
                                   int32_t hotX, int32_t hotY, uint8_t fill)
    {
        const std::shared_ptr<MCWindow> window = MCWindow::Make(width, height);
        std::ranges::fill(window->Bytes(), fill);
        AGShapeDraw(MCPane::Over(window), shapeTable, shapeNum, hotX, hotY);
        return {window->Bytes().begin(), window->Bytes().end()};
    }
}

MCCursorImage MCCursorImageFromShape(std::span<const uint8_t> shapeTable, int32_t shapeNum)
{
    const auto [width, height, hotX, hotY] = ExtentOf(shapeTable, shapeNum);

    if (width <= 0 || height <= 0)
    {
        return {};
    }

    // Drawn twice, over 0 and over 255: a pixel the shape skips keeps the background, so it differs between the two.
    const std::vector<uint8_t> overBlack = DrawShape(shapeTable, shapeNum, width, height, hotX, hotY, 0);
    const std::vector<uint8_t> overWhite = DrawShape(shapeTable, shapeNum, width, height, hotX, hotY, 0xff);
    MCCursorImage image = MCCursorImage::Blank(width, height, hotX, hotY);

    for (size_t i = 0; i < image.Pixels.size(); ++i)
    {
        if (overBlack[i] != overWhite[i])
        {
            continue;
        }

        image.Pixels[i] = overBlack[i];
        image.Opaque[i] = 1;
    }

    image.Hd = HdCursor(shapeTable, shapeNum, width, height);
    return image;
}

MCCursorImage MCCursorImageFromPane(const MCPane& pane, int32_t hotX, int32_t hotY)
{
    const std::shared_ptr<MCWindow> window = pane.Window.lock();

    if (window == nullptr || window->Pixels == nullptr)
    {
        return {};
    }

    const int32_t width = pane.X1 - pane.X0 + 1;
    const int32_t height = pane.Y1 - pane.Y0 + 1;

    if (width <= 0 || height <= 0)
    {
        return {};
    }

    MCCursorImage image = MCCursorImage::Blank(width, height, hotX, hotY);
    const int32_t pitch = window->XMax + 1;

    for (int32_t y = 0; y < height; ++y)
    {
        const std::span<const uint8_t> row = window->Bytes().subspan(
            static_cast<size_t>(pane.Y0 + y) * static_cast<size_t>(pitch) + static_cast<size_t>(pane.X0),
            static_cast<size_t>(width));
        std::ranges::copy(row, image.Pixels.begin() + static_cast<ptrdiff_t>(y) * width);
    }

    std::ranges::fill(image.Opaque, 1);
    return image;
}

void MCHardwareCursorPreload(std::span<const MCRegisteredBlock> shapes)
{
    Preloaded = true;
    PreloadedPack = MCHdArt::PackVersion();
    PreloadedHdArt = MCHdArt::Active();
    ShapeImages.assign(128, {});
    // Port-only (SPEC-first-use.md): their HD frames read by HD art's readers, several at once, before they're made.
    std::vector<MCHdArt::ShapeAhead> ahead;

    for (size_t shape = 0; shape < ShapeImages.size() && shape < shapes.size(); ++shape)
    {
        if (!shapes[shape].Empty())
        {
            const ShapeExtent extent = ExtentOf(shapes[shape].Bytes(), 0);
            ahead.push_back(MCHdArt::ShapeAhead{shapes[shape].Bytes(), 0, extent.Width, extent.Height});
        }
    }

    MCHdArt::ReadShapesAhead(ahead);
    MCHdArt::WaitForLoadReads();
    MCHdArt::KeepReadAhead();

    for (size_t shape = 0; shape < ShapeImages.size() && shape < shapes.size(); ++shape)
    {
        if (!shapes[shape].Empty())
        {
            ShapeImages[shape] = MCCursorImageFromShape(shapes[shape].Bytes(), 0);
        }
    }

    MCCursor::Preload(ShapeImages);
}

void MCHardwareCursorNewFrame()
{
    Carrying = false;
}

bool MCHardwareCursorCarry(MCGuiObject& object, const MCPane& pixels)
{
    if (GSoftwareCursor != 0 || MCInput::Display() == nullptr)
    {
        return false;
    }

    // The object was placed from the mouse position of the message the game handled last; the same offset under
    // the live mouse keeps it where the game put it relative to the cursor.
    const MCPoint mouse = MCInput::GetMessagePos();
    const int32_t offsetX = mouse.x - object.GlobalX();
    const int32_t offsetY = mouse.y - object.GlobalY();

    if (offsetX < 0 || offsetY < 0 || offsetX >= object.Width() || offsetY >= object.Height())
    {
        return false;
    }

    MCCursorImage image = MCCursorImageFromPane(pixels, offsetX, offsetY);

    if (image.Width == 0)
    {
        return false;
    }

    Carried = std::move(image);
    Carrying = true;
    return true;
}

void MCHardwareCursorUpdate()
{
    // Port-only: HD art turned on or off, or another pack: the pictures again.
    if (Preloaded && (MCHdArt::PackVersion() != PreloadedPack || MCHdArt::Active() != PreloadedHdArt))
    {
        MCHardwareCursorPreload(GuiSystem().CursorShapes());
    }

    const int32_t shape = GuiSystem().CursorShape;
    const bool hasShape = shape >= 0 && static_cast<size_t>(shape) < ShapeImages.size() &&
                          ShapeImages[static_cast<size_t>(shape)].Width > 0;

    if (Carrying)
    {
        Carrying = false;
        MCCursor::Show(hasShape ? MCCursorImage::Overlay(Carried, ShapeImages[static_cast<size_t>(shape)]) : Carried);
    }
    else if (hasShape)
    {
        MCCursor::ShowShape(static_cast<size_t>(shape));
    }
    else
    {
        MCCursor::Hide();
    }
}

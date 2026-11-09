#pragma once

// Port-only helpers shared by the vfx/*.cpp files. The asm expanded the same prologue macro at the top of every
// routine that draws through a pane; the port has it once here.
//
// The vfx routines are the renderer's front end: they keep the original's clip arithmetic and hand each draw, resolved,
// to the renderer of the window (MCRenderer::For), which writes the pixels.

#include "platform/MCRenderer.h"
#include "vfx/MCVfxFunctions.h"
#include "vfx/MCVfxView.h"

/// <summary>A pane resolved against its window, as the VFX prologue computes it, for one draw.</summary>
struct MCVfxClip
{
    /// <summary>The pane's window, held for the draw.</summary>
    std::shared_ptr<MCWindow> Window;
    /// <summary>The window's row length in bytes (x_max + 1).</summary>
    int32_t Stride = 0;
    /// <summary>The window's height (y_max + 1).</summary>
    int32_t Height = 0;
    /// <summary>The pane's origin, unclipped: pane coordinates are relative to it.</summary>
    int32_t PaneX = 0;
    int32_t PaneY = 0;
    /// <summary>The pane clipped to the window, inclusive, in window coordinates.</summary>
    int32_t X0 = 0;
    int32_t Y0 = 0;
    int32_t X1 = 0;
    int32_t Y1 = 0;

    /// <summary>The window drawn into.</summary>
    MCWindow& Target() const { return *Window; }

    /// <summary>Whether the window has pixels to read (a view has none).</summary>
    bool HasPixels() const { return Window->Pixels != nullptr; }

    /// <summary>Window row <paramref name="y"/> from column <paramref name="x"/> to the end of the row.</summary>
    std::span<uint8_t> Row(int32_t x, int32_t y) const
    {
        return Window->Bytes().subspan(static_cast<size_t>(y) * static_cast<size_t>(Stride) + static_cast<size_t>(x),
                                       static_cast<size_t>(Stride - x));
    }
};

/// <summary>
/// Resolves <paramref name="pane"/>: the pane's rectangle with x0/y0 raised to 0 and x1/y1 lowered to the window's
/// last column/row.
/// </summary>
/// <returns>
/// 0, VfxErrBadWindow when the window has no pixels (or is gone), or VfxErrEmptyPane when the result is empty.
/// </returns>
inline int32_t MCVfxClipPane(const MCPane& pane, MCVfxClip& clip)
{
    clip.Window = pane.Window.lock();

    if (clip.Window == nullptr)
    {
        return VfxErrBadWindow;
    }

    const MCWindow& window = *clip.Window;
    clip.Stride = window.XMax + 1;

    if (clip.Stride <= 0)
    {
        return VfxErrBadWindow;
    }

    clip.Height = window.YMax + 1;

    if (clip.Height <= 0)
    {
        return VfxErrBadWindow;
    }

    clip.PaneX = pane.X0;
    clip.PaneY = pane.Y0;
    clip.X0 = pane.X0 > 0 ? pane.X0 : 0;
    clip.Y0 = pane.Y0 > 0 ? pane.Y0 : 0;
    clip.X1 = pane.X1 < clip.Stride - 1 ? pane.X1 : clip.Stride - 1;
    clip.Y1 = pane.Y1 < clip.Height - 1 ? pane.Y1 : clip.Height - 1;
    MCClipToView(window, clip.X0, clip.Y0, clip.X1, clip.Y1);

    if (clip.X1 < clip.X0 || clip.Y1 < clip.Y0)
    {
        return VfxErrEmptyPane;
    }

    return 0;
}

/// <summary>
/// The table <c>VFX_shape_lookaside</c> fills and the translating shape draws map pixels through (VFX's own copy,
/// at 0x007a995c in MCX.EXE). Defined in vfx/MCVfxShape.cpp.
/// </summary>
extern std::array<uint8_t, 256> VfxShapeLookasideTable;

/// <summary>A shape's header in a VFX shape table.</summary>
struct MCVfxShapeHeader
{
    int32_t Bounds;
    int32_t Origin;
    int32_t XMin;
    int32_t YMin;
    int32_t XMax;
    int32_t YMax;
};

static_assert(sizeof(MCVfxShapeHeader) == 0x18);

/// <summary>Reads a little-endian 32-bit value at <paramref name="offset"/> (shape data isn't always aligned).</summary>
inline int32_t MCVfxRead32(std::span<const uint8_t> data, size_t offset = 0)
{
    int32_t value;
    std::memcpy(&value, data.subspan(offset, 4).data(), 4);
    return value;
}

/// <summary>
/// Whether shape <paramref name="shapeNum"/> of <paramref name="shapeTable"/> is one of the table's shapes (under its
/// count, at 4) with its header inside the table (at the offset in its directory, 8 + 8n).
/// </summary>
/// <remarks>
/// OB-190 (fixed): some of the retail mechs' shape tables list frames that aren't there, their directory entries
/// pointing at the table's end (the Hollander's whole fall back from a run; the Atlas's right arm at one facing of its
/// fall back from standing). The original read such a frame's header and rows from whatever followed the table.
/// </remarks>
inline bool MCVfxHasShape(std::span<const uint8_t> shapeTable, int32_t shapeNum)
{
    constexpr size_t CountAt = 4;
    constexpr size_t DirectoryAt = 8;
    constexpr size_t EntryBytes = 8;
    constexpr size_t OffsetBytes = 4;

    if (shapeNum < 0 || shapeTable.size() < DirectoryAt || shapeNum >= MCVfxRead32(shapeTable, CountAt))
    {
        return false;
    }

    const size_t entry = DirectoryAt + static_cast<size_t>(shapeNum) * EntryBytes;

    if (entry + OffsetBytes > shapeTable.size())
    {
        return false;
    }

    const int32_t offset = MCVfxRead32(shapeTable, entry);
    return offset >= 0 && static_cast<size_t>(offset) + sizeof(MCVfxShapeHeader) <= shapeTable.size();
}

/// <summary>
/// Shape <paramref name="shapeNum"/> of <paramref name="shapeTable"/>, from its header (at the offset in the table's
/// directory, 8 + 8n) to the table's end.
/// </summary>
inline std::span<const uint8_t> MCVfxShape(std::span<const uint8_t> shapeTable, int32_t shapeNum)
{
    const int32_t offset = MCVfxRead32(shapeTable, 8 + static_cast<size_t>(shapeNum) * 8);
    return shapeTable.subspan(static_cast<size_t>(offset));
}

/// <summary>Shape <paramref name="shapeNum"/> of a table whose pixels are changed in place.</summary>
inline std::span<uint8_t> MCVfxShape(std::span<uint8_t> shapeTable, int32_t shapeNum)
{
    const int32_t offset = MCVfxRead32(shapeTable, 8 + static_cast<size_t>(shapeNum) * 8);
    return shapeTable.subspan(static_cast<size_t>(offset));
}

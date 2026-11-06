#pragma once

#include "vfx/MCVfx.h"

/// <summary>
/// A grid of byte flags (0 or 0xFF) with a VFX window and pane over it, so shapes (circles) can be drawn into it. The
/// terrain keeps what each side sees this way.
/// </summary>
/// <remarks>
/// Original source: <c>engine\bitflag.cpp</c>. Writes go through the renderer (the tactical map shows the visible
/// bits from a copy of them on the GPU, see <c>MCTacticalMap::Init</c>).
/// </remarks>
class MCByteFlag
{
public:
    /// <summary>
    /// A <paramref name="numRows"/> x <paramref name="numColumns"/> grid, all 0xFF when
    /// <paramref name="initialValue"/>, and its window and pane.
    /// </summary>
    MCByteFlag(uint32_t numRows, uint32_t numColumns, bool initialValue);

    MCByteFlag(const MCByteFlag&) = delete;
    MCByteFlag& operator=(const MCByteFlag&) = delete;

    /// <summary>Sets the flags in a filled circle of <paramref name="radius"/> at (<paramref name="x"/>, <paramref name="y"/>).</summary>
    void SetCircle(uint32_t x, uint32_t y, uint32_t radius);

    /// <summary>Sets every flag (<paramref name="value"/>) or clears them.</summary>
    void ResetAll(bool value);

    /// <summary>Sets the flag at (<paramref name="r"/>, <paramref name="c"/>).</summary>
    void SetFlag(uint32_t r, uint32_t c);

    /// <summary>Sets <paramref name="length"/> flags from (<paramref name="r"/>, <paramref name="c"/>).</summary>
    void SetGroup(uint32_t r, uint32_t c, uint32_t length);
    /// <summary>
    /// Port-only: sets row <paramref name="r"/>'s flags from column <paramref name="first"/> to <paramref name="last"/>
    /// (both kept), the part on the grid.
    /// </summary>
    void SetRun(int32_t r, int32_t first, int32_t last);

    /// <summary>Whether the flag at (<paramref name="r"/>, <paramref name="c"/>) is set (false outside the grid).</summary>
    bool GetFlag(uint32_t r, uint32_t c) const;

    /// <summary>The number of rows.</summary>
    uint32_t Rows() const { return _Rows; }

    /// <summary>The number of columns.</summary>
    uint32_t Columns() const { return _Columns; }

    /// <summary>The bytes (one more than rows x columns).</summary>
    std::span<uint8_t> Data() const { return std::span(_Window->Pixels.get(), _Size); }

    /// <summary>The window over the grid's bytes.</summary>
    MCWindow& Window() { return *_Window; }

    /// <summary>The window, for a holder that shares it (a port that draws the grid).</summary>
    std::shared_ptr<MCWindow> SharedWindow() const { return _Window; }

private:
    /// <summary>
    /// Sets <paramref name="count"/> bytes of the grid from byte <paramref name="first"/> (rows run on into the
    /// next) to 0xFF, through the renderer.
    /// </summary>
    void SetBytes(uint32_t first, uint32_t count);

    /// <summary>The number of bytes, the window's pixels.</summary>
    /// <remarks>
    /// One more than the grid: <see cref="SetFlag"/> and <see cref="GetFlag"/> take column == columns, which reaches
    /// one byte past the last row (the original's heap had slack there).
    /// </remarks>
    size_t _Size = 0;
    /// <summary>The number of rows.</summary>
    uint32_t _Rows = 0;
    /// <summary>The number of columns.</summary>
    uint32_t _Columns = 0;
    /// <summary>The window over the grid's bytes.</summary>
    std::shared_ptr<MCWindow> _Window = std::make_shared<MCWindow>();
    /// <summary>A pane over the whole grid.</summary>
    MCPane _Pane{};
};

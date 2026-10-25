#include "stdafx.h"
#include "vfx/MCVfxClip.h"

// The terrain tile drawer (mcx\vfx\vfxtile.cpp). A tile is a diamond of opaque pixels stored as one span per row:
//
//   +0  uint8   hot spot x (subtracted from the draw position)
//   +1  uint8   hot spot y
//   +2  uint8   height (rows)
//   +3  uint8   width (columns of the bounding box)
//   +4  uint32  offset[height + 1]: where each row starts, from the tile's start; the last is the end of the data
//   row i, at offset[i]: uint8 x (the span's first column), then offset[i + 1] - offset[i] - 1 pixels
//
// A row of length 0 is empty.

namespace
{
    /// <summary>The port's result for a tile whose data doesn't fit its table (see <see cref="VfxTileFits"/>).</summary>
    constexpr auto TileDataBad = static_cast<int32_t>(0xcdcf0002);
}

bool VfxTileFits(std::span<const uint8_t> tile)
{
    if (tile.size() < 4)
    {
        return false;
    }

    const uint32_t height = tile[2];
    const uint32_t width = tile[3];
    const size_t table = 4 + (static_cast<size_t>(height) + 1) * 4;

    if (tile.size() < table)
    {
        return false;
    }

    const auto offset = [&](uint32_t row)
    {
        uint32_t value = 0;
        std::memcpy(&value, tile.data() + 4 + static_cast<size_t>(row) * 4, sizeof(value));
        return static_cast<size_t>(value);
    };

    size_t start = offset(0);

    if (start < table || start > tile.size())
    {
        return false;
    }

    for (uint32_t row = 0; row < height; ++row)
    {
        const size_t end = offset(row + 1);

        // The last pixel's column (x + pixels - 1) is at most the width: the retail tiles reach column `width`, one past
        // their stated box, which the drawer allows for (it draws unclipped only when left + width is inside).
        if (end < start || end > tile.size() || (end > start + 1 && tile[start] + (end - start - 2) > width))
        {
            return false;
        }

        start = end;
    }

    return true;
}

int32_t VfxTileWidth(std::span<const uint8_t> tile)
{
    const auto offset = [&](int32_t row)
    {
        uint32_t value = 0;
        std::memcpy(&value, tile.data() + 4 + static_cast<size_t>(row) * 4, sizeof(value));
        return value;
    };

    int32_t width = 0;

    for (int32_t row = 0; row < tile[2]; ++row)
    {
        const uint32_t start = offset(row);
        const auto length = static_cast<int32_t>(offset(row + 1) - start);

        if (length > 1)
        {
            width = std::max(width, tile[start] + length - 1);
        }
    }

    return width;
}

int32_t VfxNTileDraw(const MCPane& pane, std::span<const uint8_t> tile, int32_t x, int32_t y, const MCTileXlat& xlat)
{
    const std::shared_ptr<MCWindow> window = pane.Window.lock();

    if (window == nullptr || tile.empty())
    {
        return 0;
    }

    if (!VfxTileFits(tile))
    {
        return TileDataBad;
    }

    const int32_t stride = window->XMax + 1;
    int32_t cx0 = pane.X0 < 0 ? 0 : pane.X0;
    int32_t cy0 = pane.Y0 < 0 ? 0 : pane.Y0;
    int32_t cx1 = pane.X1 < stride ? pane.X1 : window->XMax;
    int32_t cy1 = pane.Y1 < window->YMax + 1 ? pane.Y1 : window->YMax;
    MCClipToView(*window, cx0, cy0, cx1, cy1);

    const int32_t height = tile[2];
    const int32_t width = tile[3];
    // OB-115: (x, y) are relative to the pane's origin; the asm offset by its corner clipped to the window. OB-116:
    // and counted a tile starting on the last column or row as outside.
    const int32_t left = pane.X0 - tile[0] + x;
    const int32_t top = pane.Y0 - tile[1] + y;

    if (cx1 < cx0 || cy1 < cy0 || left > cx1 || top > cy1 || left <= cx0 - width || top <= cy0 - height)
    {
        return static_cast<int32_t>(0xcdcf0001);
    }

    const int32_t skip = top < cy0 ? cy0 - top : 0;
    const int32_t rows = height + top > cy1 ? cy1 - top + 1 : height;

    if (skip >= rows)
    {
        return 0;
    }

    MCTileCommand command;
    command.Tile = tile;

    command.Left = left;
    command.Top = top > cy0 ? top : cy0;
    command.FirstRow = skip;
    command.EndRow = rows;
    command.Lo = cx0;
    command.Hi = cx1;
    // Wholly inside horizontally: spans are written without clipping.
    command.Unclipped = left > cx0 && left + width < cx1;
    command.Fill = xlat.Fill;
    command.Table = command.Fill ? std::nullopt : xlat.Table;
    MCRenderer::For(*window).Tile(*window, command);
    return 0;
}

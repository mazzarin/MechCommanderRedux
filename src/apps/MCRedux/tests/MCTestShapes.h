#pragma once

/// <summary>The tests' shape tables, as the game's encoder lays them out.</summary>
namespace MCTestShapes
{
    /// <summary>A frame of a VFX shape table: its bounds' top left from the hot spot, its size, and its encoded rows.</summary>
    struct TableFrame
    {
        int32_t XMin = 0;
        int32_t YMin = 0;
        int32_t Width = 0;
        int32_t Height = 0;
        std::span<const uint8_t> Rows;
    };

    /// <summary>A shape's VFX table of <paramref name="frames"/>, as the game's encoder lays one out (vfx/MCVfxShapeScan.cpp).</summary>
    inline std::vector<uint8_t> FramesTable(std::span<const TableFrame> frames)
    {
        std::vector<uint8_t> table = {'1', '.', '1', '0'};
        const auto put = [&table](int32_t value)
        {
            for (int32_t byte = 0; byte < 4; ++byte)
            {
                table.push_back(static_cast<uint8_t>(static_cast<uint32_t>(value) >> (8 * byte)));
            }
        };

        // The directory (each frame's offset, then 0) after the count, then each frame's header and rows.
        constexpr int32_t DirectoryAt = 8;
        constexpr int32_t EntryBytes = 8;
        constexpr int32_t HeaderBytes = 0x18;
        constexpr int BoundsShift = 16;
        const auto count = static_cast<int32_t>(frames.size());
        put(count);
        int32_t offset = DirectoryAt + EntryBytes * count;

        for (const TableFrame& frame : frames)
        {
            put(offset);
            put(0);
            offset += HeaderBytes + static_cast<int32_t>(frame.Rows.size());
        }

        for (const TableFrame& frame : frames)
        {
            put(frame.Width << BoundsShift | frame.Height);
            put(0);
            put(frame.XMin);
            put(frame.YMin);
            put(frame.XMin + frame.Width - 1);
            put(frame.YMin + frame.Height - 1);
            table.insert(table.end(), frame.Rows.begin(), frame.Rows.end());
        }

        return table;
    }
}

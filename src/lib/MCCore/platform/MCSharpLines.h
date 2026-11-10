#pragma once

/// <summary>
/// Port-only: line art (one-pixel outlines: the mechs' and vehicles' diagrams in the store and the mech bay, and the
/// store's sold-out outlines) redrawn as smooth strokes, for a screen drawn at the size it is shown
/// (<see cref="MCFrame::SharpScreen"/>). Its pixels are joined as a glyph's are (<see cref="MCSharpText"/>), into runs
/// between ends and forks; each run's steps are straightened (no point of the run left further than
/// <c>StepTolerance</c> from the line drawn) and its gentle bends rounded, its corners kept. A round detail (a small
/// ring, as a gun's muzzle or a launcher's tube, or a two by two dot) is drawn as an ellipse. The strokes are a pixel
/// wide, as the art's are, and each redrawn pixel keeps the byte of the art pixel nearest it, so the colours (its table)
/// are the original's.
/// </summary>
namespace MCSharpLines
{
    /// <summary>
    /// A part of the art drawn by hand, where its pixels can't say what they mean (the Atlas's eyes, a skull's
    /// sockets): the art's pixels from (<c>Left</c>, <c>Top</c>) to (<c>Right</c>, <c>Bottom</c>) left out of the
    /// strokes, and a smooth loop drawn in their place through <c>Through</c> (a closed Catmull-Rom curve), in art pixels
    /// (a pixel's centre half a pixel from its corner). Its strokes keep the bytes of the art pixels nearest them.
    /// </summary>
    struct MCDrawnLoop
    {
        int32_t Left = 0;
        int32_t Top = 0;
        int32_t Right = 0;
        int32_t Bottom = 0;
        std::vector<std::array<float, 2>> Through;
    };

    /// <summary>
    /// The art <paramref name="columns"/> x <paramref name="rows"/> bytes at <paramref name="pixels"/> (rows
    /// <paramref name="pitch"/> apart, <see cref="MCSharpText::NoInk"/> where nothing is drawn), redrawn at
    /// <see cref="MCSharpText::Scale"/> in <see cref="MCSharpText::Redraw"/>'s form: RGBA8, red the byte of the nearest
    /// art pixel, green the strokes' coverage; <paramref name="drawn"/> its parts drawn by hand.
    /// </summary>
    std::vector<uint8_t> Redraw(std::span<const uint8_t> pixels, int32_t pitch, int32_t columns, int32_t rows,
                                std::span<const MCDrawnLoop> drawn = {});
}

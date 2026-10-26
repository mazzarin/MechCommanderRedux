#pragma once

/// <summary>
/// Port-only: line art (a one-pixel outline: the purchase screen's sold-out mechs and vehicles) redrawn as smooth
/// strokes, for a screen drawn at the size it is shown (<see cref="MCFrame::SharpScreen"/>). Its pixels are joined as a
/// glyph's are (<see cref="MCSharpText"/>), into runs between ends and forks; each run's steps are straightened (no
/// point of the run left further than <c>StepTolerance</c> from the line drawn) and its gentle bends rounded, its
/// corners kept. The strokes are a pixel wide, as the art's are, and each redrawn pixel keeps the byte of the art pixel
/// nearest it, so the colours (its table) are the original's.
/// </summary>
namespace MCSharpLines
{
    /// <summary>
    /// The art <paramref name="columns"/> x <paramref name="rows"/> bytes at <paramref name="pixels"/> (rows
    /// <paramref name="pitch"/> apart, <see cref="MCSharpText::NoInk"/> where nothing is drawn), redrawn at
    /// <see cref="MCSharpText::Scale"/> in <see cref="MCSharpText::Redraw"/>'s form: RGBA8, red the byte of the nearest
    /// art pixel, green the strokes' coverage.
    /// </summary>
    std::vector<uint8_t> Redraw(std::span<const uint8_t> pixels, int32_t pitch, int32_t columns, int32_t rows);
}

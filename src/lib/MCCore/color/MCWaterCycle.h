#pragma once

class MCDisplay;
class MCPalette;

/// <summary>The first of the eight palette entries the water's colour cycle animates.</summary>
inline constexpr uint8_t FirstWaterColor = 0xd8;

/// <summary>The palette entries whose colours the water entries take, one step further each cycle.</summary>
inline constexpr std::array<uint8_t, 8> WaterMagicColors = {0x5a, 0x59, 0x5a, 0x5b, 0x5d, 0x5c, 0x5b, 0x59};

/// <summary>The current step of the water colour cycle (0..7).</summary>
extern uint8_t CurrentMagic;

/// <summary>
/// One step of the water's colour cycle: water entry i takes the colour of <c>WaterMagicColors[(step + i) % 8]</c> in
/// <paramref name="palette"/>, the display (when there is one) shows the cycle, and the step moves on.
/// </summary>
/// <remarks>
/// The original handed the eight entries to the display (<c>gamePalette->animate(0xd8, 8)</c>). The display shows
/// them as an index remap instead, so the palette the GPU holds doesn't change every cycle; the next palette set over
/// them ends the remap, as it overwrote the animated entries.
/// </remarks>
void StepWaterColors(MCPalette& palette, std::shared_ptr<MCDisplay> display);

/// <summary>
/// The interface's colour callback: steps the water colours every scenario CycleLength seconds while palette
/// cycling is on (PREFS "PaletteCycle").
/// </summary>
/// <remarks>Original source: <c>color\color.cpp</c> (<c>cycleColors</c>).</remarks>
void CycleColors();

/// <summary>
/// Port-only (HD lighting's living water): how far the water's colour cycle has gone from its last step toward its next,
/// 0 to 1; 0 while palette cycling is off or without a scenario.
/// </summary>
float WaterCyclePhase();

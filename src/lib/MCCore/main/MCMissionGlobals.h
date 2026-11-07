#pragma once

// The mission's clock and world scale, which every layer reads. The original spread them over its files
// (object\warrior.cpp, mission\scenario.cpp, object\mech.cpp, object\gvehicl.cpp); the port keeps them together.

/// <summary>Seconds since the mission started.</summary>
extern float ScenarioTime;

/// <summary>Frames (turns) since the mission started.</summary>
extern int32_t Turn;

/// <summary>Seconds this frame covers (0.05 to begin with).</summary>
extern float FrameLength;

/// <summary>The frames' length under the original's frame lock (Ctrl+Alt+S): 15 a second.</summary>
inline constexpr float LockedFrameLength = 1.0f / 15.0f;

/// <summary>World units per meter (3.34 until a scenario's game system file sets it).</summary>
extern float WorldUnitsPerMeter;

/// <summary>Meters per world unit (0.2994, 1 / 3.34, until a scenario's game system file sets it).</summary>
extern float MetersPerWorldUnit;

/// <summary>Nonzero once the mission is over and the game is heading to the results screen.</summary>
extern int EventsToMissionResultsScreen;

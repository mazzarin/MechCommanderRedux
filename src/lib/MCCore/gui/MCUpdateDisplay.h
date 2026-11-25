#pragma once

// The display update and the mouse thread: UpdateDisplay draws the frame and presents the screen buffer (with the
// optional screenshot, static and progress bar), and a timer thread (20 ticks a second) counts ticks for the timed
// screens and services the sound streams.
//
// Original source: gui\updisp.cpp. The original's flip is MCDisplay::Present. Its timer also moved and redrew the
// software cursor between frames, but only while the display flipped pages, which the port never does.

#include "platform/MCRegisteredBlock.h"

class MCGuiObject;

/// <summary>Counts the mouse timer's ticks (20 a second).</summary>
extern uint32_t MouseTicks;
/// <summary>The cursor on the screen, updated by <see cref="UpdateDisplay"/>.</summary>
extern int MouseScreenX;
extern int MouseScreenY;
/// <summary>The number of frames the frame graph shows.</summary>
inline constexpr size_t FrameGraphLength = 512;
/// <summary>The last frame rates (capped at 60), for the frame graph.</summary>
extern std::array<float, FrameGraphLength> FrameRateArray;
/// <summary>The next entry of <see cref="FrameRateArray"/>.</summary>
extern size_t FrPointer;
/// <summary>Set to show a black screen instead of the frame.</summary>
extern bool KeepScreenBlack;
/// <summary>
/// Port-only (SPEC-menus.md's stage 1): while set, the frame loop's frames draw the loading screen (static over the
/// screen, the connect shape and the bar as the load last drew them) in place of the screen window: the loading screen
/// faded in before the mission's load and out after it (MCMission). The load's own frames draw it as ever.
/// </summary>
extern bool LoadingScreenUp;
/// <summary>The screenshot counter ("scrn%04d.tga").</summary>
extern int32_t ShotNum;
/// <summary>
/// The "connecting" shape <see cref="UpdateDisplay"/> draws with the progress bar (loaded while a scenario loads,
/// empty otherwise).
/// </summary>
MCRegisteredBlock& ConnectShape();

/// <summary>Clears the screen buffer and shows it (with the cursor).</summary>
void BlankScreen();

/// <summary>
/// Port-only: while one lives, <see cref="UpdateDisplay"/>'s frame may be shown after it returns
/// (<see cref="MCFrame::Ahead"/>): its GPU work runs on the presenter's thread while the caller makes the next frame. Held
/// around the frame loops' own UpdateDisplay (MCGuiSystem::Run, the tests' frames), which present again soon and check for
/// the frame meanwhile (as they pump the window's events); every other present shows its frame before it returns.
/// </summary>
class MCFramesAhead
{
public:
    MCFramesAhead();
    ~MCFramesAhead();
    MCFramesAhead(const MCFramesAhead&) = delete;
    MCFramesAhead& operator=(const MCFramesAhead&) = delete;

    /// <summary>Whether one lives.</summary>
    static bool On();

private:
    /// <summary>Whether one lived before this one.</summary>
    bool _Outer = false;
};

/// <summary>
/// Shows the frame: lets the GUI draw, then optionally writes a screenshot, fills rows with static, draws the
/// connect shape with a progress bar of <paramref name="progress"/> percent; records the frame rate; draws the
/// cursor and presents the buffer.
/// </summary>
/// <param name="screenShot">Write the frame to scrn%04d.tga first.</param>
/// <param name="staticNoise">Each row turns to static with 1-in-<paramref name="noiseChance"/> odds.</param>
/// <param name="showProgress">Draw the connect shape and the progress bar.</param>
int32_t UpdateDisplay(bool screenShot, bool staticNoise, int32_t noiseChance, bool showProgress, int32_t progress);
/// <summary>
/// Port-only: the screen as it stands drawn once as a rehearsal (MCRenderer::RehearsalScope): what its first frame
/// would do first (its art loaded, its HD pictures read, its textures made, its pictures, sharp text and line art taken
/// and queued to go up) done now, nothing shown, and what it queued sent up (MCDisplay::RunRecorded). For a screen set
/// up in a fade, where nothing is redrawn for a while, so its first frames shown don't do it. Only with the GPU's
/// surfaces shown and logistics up; nothing otherwise.
/// </summary>
void RehearseScreen();
/// <summary>Port-only, for the tests: the rehearsals run so far (RehearseScreen's).</summary>
size_t RehearsalsRun();
/// <summary>
/// Port-only: what a fade into a screen does, in pieces, each looked up as it runs: the screen shown rehearsed
/// (RehearseScreen). Run in a palette fade near black (FadeOutWhile), or a frame each while a change of screen is dark
/// (gui/MCScreenChange.h).
/// </summary>
std::vector<std::function<void()>> IntoScreenPieces();
/// <summary>
/// Port-only: <paramref name="work"/> done in a fade of the screen shown (MCGuiSystem::FadeDownCurrentPalette) once it's
/// nearly black (MCGuiSystem::FadeNearBlack), then what a fade into a screen does (the screen rehearsed): for work that sets up the next screen where the one shown would sit frozen
/// until its fade begins (the way back from a mission's results, NEW CAMPAIGN's click). With no fade (the screen black already, the display
/// not up, the GUI stopping), only <paramref name="work"/>, as before.
/// </summary>
void FadeOutWhile(const std::function<void()>& work);
/// <summary>
/// Port-only (SPEC-menus.md's stage 7): the palette waiting (MCGuiSystem::ActivatePalette) put in now, with no fade:
/// for a 1998 screen's palette set while a remade screen covers the window and no change of screen runs (the main
/// menu's submenus' BACK, in place), where the next frame's fade would only darken the remade screen for a second.
/// </summary>
void ActivatePaletteNow();

/// <summary>Starts the mouse timer.</summary>
void MouseTimerInit();
/// <summary>Stops the mouse timer.</summary>
void MouseTimerKill();
/// <summary>The mouse timer's tick: counts it, and services the sound streams every half second.</summary>
void MouseTimerTick();

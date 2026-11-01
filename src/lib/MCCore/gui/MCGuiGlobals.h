#pragma once

// The GUI's shared settings and state (gui\asystem.cpp's globals): the display settings PREFS and the command line
// set, the fonts, the pause and cheat flags, the frame's timing, and the mouse thread's lock.

#include "gui/MCGuiOwned.h"

class MCGuiFont;
class MCGuiObject;

/// <summary>The feature screen (the mission's start-up picture) while it shows; the mission makes and ends it.</summary>
extern MCGuiOwned<MCGuiObject> FeatureScreen;
/// <summary>Set (-1) by a key press while the feature screen shows: the player is done with it.</summary>
extern int FeatureScreenDone;
/// <summary>Set (-1) when Escape ended the movie playing.</summary>
extern int EscapedSmackerMovie;

/// <summary>The fonts made by <see cref="MCGuiSystem::Start"/> (shared with the GUI system; null before it starts).</summary>
extern std::shared_ptr<MCGuiFont> SystemFont;
extern std::shared_ptr<MCGuiFont> BlackFont;
extern std::shared_ptr<MCGuiFont> GreyFont;
extern std::shared_ptr<MCGuiFont> WhiteFont;
extern std::shared_ptr<MCGuiFont> RedFont;
extern std::shared_ptr<MCGuiFont> GreenFont;
extern std::shared_ptr<MCGuiFont> BlueFont;
extern std::shared_ptr<MCGuiFont> DimFont;
extern std::shared_ptr<MCGuiFont> YellowFont;
extern std::shared_ptr<MCGuiFont> YellowDropFont;
extern std::shared_ptr<MCGuiFont> BlueDropFont;
extern std::shared_ptr<MCGuiFont> MedBlackFont;
extern std::shared_ptr<MCGuiFont> MedGreyFont;
extern std::shared_ptr<MCGuiFont> MedWhiteFont;
extern std::shared_ptr<MCGuiFont> MedRedFont;
extern std::shared_ptr<MCGuiFont> MedGreenFont;
extern std::shared_ptr<MCGuiFont> MedBlueFont;
extern std::shared_ptr<MCGuiFont> MedDimFont;
extern std::shared_ptr<MCGuiFont> MedYellowFont;
extern std::shared_ptr<MCGuiFont> LgBlackFont;
extern std::shared_ptr<MCGuiFont> LgGreyFont;
extern std::shared_ptr<MCGuiFont> LgWhiteFont;
extern std::shared_ptr<MCGuiFont> LgRedFont;
extern std::shared_ptr<MCGuiFont> LgGreenFont;
extern std::shared_ptr<MCGuiFont> LgBlueFont;
extern std::shared_ptr<MCGuiFont> LgDimFont;
extern std::shared_ptr<MCGuiFont> LgYellowFont;

/// <summary>
/// The same fonts by colour and size: row = colour (0 black, 1 red, 2 yellow, 3 green, 4 blue, 5 grey, 6 white, 7 dim,
/// 8 yellow drop, 9 blue drop), column = small, medium, large. The scrolling text objects pick fonts from it. The drop
/// fonts have no large size: those two entries are fonts without letters (they draw nothing).
/// </summary>
extern std::array<std::array<std::shared_ptr<MCGuiFont>, 3>, 10> Fonts;

/// <summary>Set while the game is paused, and while it asks the player something (quit, ...).</summary>
extern bool GamePaused;
extern bool GameAsked;

/// <summary>The screen's size in pixels.</summary>
extern int GWidth;
extern int GHeight;
extern int GBitDepth;
/// <summary>PREFS "FullScreen" (read and written as an int): full screen instead of a window.</summary>
extern int GFullScreen;
/// <summary>Port-only: stretch the picture over the whole window instead of keeping 4:3 with bars (PREFS
/// "StretchToFit", read by systemInit).</summary>
extern int GStretchToFit;
/// <summary>Port-only: draw the cursor into the frame as the original did, instead of showing it as the system
/// cursor (PREFS "SoftwareCursor", read by systemInit).</summary>
extern int GSoftwareCursor;
/// <summary>Port-only: open the display's window hidden (the tests that run a mission).</summary>
extern bool GHiddenWindow;
/// <summary>Port-only: the renderer asked for, an <c>MCRendererKind</c> (PREFS "Renderer", read by systemInit, then
/// the command line's <c>-renderer</c>).</summary>
extern int GRenderer;
/// <summary>Port-only: the renderer PREFS "Renderer" asks for (the preferences screen's choice, written back by
/// WritePrefs; it takes effect at the next start). <see cref="GRenderer"/> is what this run uses.</summary>
extern int GRendererPreference;
/// <summary>
/// Port-only: the most lines the in-mission UI is drawn at (PREFS "UIResolution", the preferences screen's UI
/// RESOLUTION box): 480, 600, 720, 900 or 1080, or 0 for native (the window's height in points, which is half its
/// pixels on a Retina screen, so the UI is the size it is on a 1x screen). The screen is never taller than
/// the window nor than <see cref="MaxUiResolution"/>, and is as wide as the window's aspect makes it; the world view is
/// still drawn at the window's pixels (<see cref="MCGuiSystem::FollowWindowSize"/>).
/// </summary>
extern int GUiResolution;
/// <summary>Port-only: the tallest the in-mission UI is drawn, native included.</summary>
inline constexpr int MaxUiResolution = 1080;
/// <summary>Port-only: the choices of <see cref="GUiResolution"/> other than native.</summary>
inline constexpr std::array<int, 5> UiResolutions = {480, 600, 720, 900, 1080};
/// <summary>Port-only: draw the frame counter in the top-right corner (PREFS "ShowFps", read by systemInit, or the
/// command line's <c>-fps</c>).</summary>
extern int GShowFps;
/// <summary>Port-only: PREFS "ShowFps" as read (written back by WritePrefs, whatever the command line said).</summary>
extern int GShowFpsPreference;
/// <summary>Port-only: wait for the display's refresh when showing a frame (cleared by the command line's
/// <c>-novsync</c>).</summary>
extern bool GVSync;
/// <summary>Whether the game's window is active (it stops running frames when not).</summary>
extern bool ApplicationActive;
/// <summary>The screen size systemInit picks from PREFS "Resolution" (read, then ignored by the port).</summary>
extern int32_t DisplayWidth;
extern int32_t DisplayHeight;
/// <summary>Where <see cref="CheckMouse"/> last saw the mouse.</summary>
extern int OldMouseX;
extern int OldMouseY;
/// <summary>Frames per second, from the last frame's length (at least 4).</summary>
extern float FrameRate;
/// <summary>The performance counter at the frame's start and end, the previous start, and the counts per second.</summary>
extern int64_t PerfStartTime;
extern int64_t PerfStopTime;
extern int64_t PrevStart;
extern int64_t CountsPerSecond;
/// <summary>The last cursor position of a drag.</summary>
extern int32_t LastX;
extern int32_t LastY;
/// <summary>The window's title: <see cref="AppName"/>, and the screen's name after it.</summary>
extern std::string AppName;
extern std::string WindowTitle;
/// <summary>The palette's file name.</summary>
extern std::string PaletteName;
/// <summary>The command line's <c>-load</c> file (empty without one).</summary>
extern std::string StartupPakFile;

/// <summary>Cheats on (missions read it from their FIT).</summary>
extern bool CheatsOn;
/// <summary>Port: the command line's <c>-debug</c>: Enter opens the debug console in a single-player mission.</summary>
extern bool DebugOn;
/// <summary>The "can't hit me" cheat: home team movers take no hits outside multiplayer (the name is the port's).</summary>
extern bool CantHitMe;
/// <summary>The salvage cheat: a destroyed mover's salvage is never blown up.</summary>
extern bool CantBlowSalvage;
/// <summary>The bunny strike cheat: the debug strike key works.</summary>
extern bool BunnyStrikesOn;
/// <summary>The "duh" cheat: the enemy pilots' brains don't run.</summary>
extern bool Duh;
/// <summary>Ctrl+Alt+S: frames take at least 1/15 s.</summary>
extern bool LockFrameRate;
/// <summary>Set to save the next frame as a screenshot.</summary>
extern bool TakeScreenShot;
/// <summary>When the mouse first reached the screen's edge (scrolling starts after the interface's delay).</summary>
extern uint32_t ScrollWait;
/// <summary>Ctrl+Alt+P: the profile display (0 off, 1, 2).</summary>
extern int32_t DisplayProfileData;
/// <summary>The last key pressed (the player controls read it).</summary>
extern char KeySetting;
/// <summary>Ctrl+Alt+G (cheats on): gates stay shut.</summary>
extern bool ForceGatesClosed;
/// <summary>Ctrl+L (cheats on): the terrain grid is drawn.</summary>
extern bool DrawTerrainGrid;
/// <summary>The frame-graph cheat.</summary>
extern bool AndyFramerate;

/// <summary>The mouse thread's lock (a CRITICAL_SECTION in the original), and whether it is held for a frame.</summary>
extern std::recursive_mutex MouseCritSec;
extern volatile int InMouseCritSec;
/// <summary>Set while the mouse thread (the cursor timer) runs.</summary>
extern int MouseThreadStarted;

/// <summary>Holds the mouse thread's lock for its lifetime, when the thread runs.</summary>
class MCMouseThreadLock
{
public:
    MCMouseThreadLock() : _Locked(MouseThreadStarted != 0)
    {
        if (_Locked)
        {
            MouseCritSec.lock();
        }
    }

    ~MCMouseThreadLock()
    {
        if (_Locked)
        {
            MouseCritSec.unlock();
        }
    }

    MCMouseThreadLock(const MCMouseThreadLock&) = delete;
    MCMouseThreadLock& operator=(const MCMouseThreadLock&) = delete;

private:
    bool _Locked;
};

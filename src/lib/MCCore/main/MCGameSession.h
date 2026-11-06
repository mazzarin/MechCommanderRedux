#pragma once

#include "gui/MCGuiCallback.h"
#include "gui/MCGuiRef.h"

class MCAblDebuggerWindow;
class MCScrollingTextWindow;

/// <summary>
/// The game's systems made after the display is up and before the first frame (honorb.cpp's userInit), taken down in
/// the original's order when it is destroyed (userDestroy). The GUI system owns it from its start to its stop.
/// </summary>
/// <remarks>
/// Making it: the game system's text window (SYSTEM.CFG "DebugGameSystem"), the ABL debugger window (placed by iface.fit's
/// "ABL Window"), the sound system (unless one is installed), the palette cycling callback, multiplayer (kept only when a
/// lobby launched the game, which the port's never does), and the mission, loaded with the campaign (or SYSTEM.CFG's
/// mission for a game segment). Destroying it: the debugger window, the mission, the callback, multiplayer, the
/// FastFiles and the sound system, then the game system's window (which the original never freed).
/// </remarks>
class MCGameSession
{
public:
    /// <summary>Makes the systems. A mission that fails to load is fatal.</summary>
    MCGameSession();

    /// <summary>Takes the systems down in the original's order.</summary>
    ~MCGameSession();

    MCGameSession(const MCGameSession&) = delete;
    MCGameSession& operator=(const MCGameSession&) = delete;

    /// <summary>The ABL debugger's window, when SYSTEM.CFG enables the debugger.</summary>
    std::shared_ptr<MCAblDebuggerWindow> DebuggerWindow() const;

    /// <summary>The palette cycling callback (runs every frame).</summary>
    const MCGuiCallback& ColorCallback() const { return *_ColorCallback; }

private:
    /// <summary>The game system's text window (also <c>GameSystemWindow</c>), when SYSTEM.CFG asks for it.</summary>
    MCGuiHandle<MCScrollingTextWindow> _GameSystemWindow;
    MCGuiHandle<MCAblDebuggerWindow> _DebuggerWindow;
    std::shared_ptr<MCGuiCallback> _ColorCallback = std::make_shared<MCGuiCallback>();
};

/// <summary>Thrown by <see cref="KillTheGame"/>: unwinds to <c>RunGame</c>, which stops the game and returns the code.</summary>
struct MCGameQuit
{
    /// <summary>The process's exit code: 0 for a quit the player asked for, else an error.</summary>
    int32_t ExitCode = 0;
};

/// <summary>Ends the game at once: <c>RunGame</c> stops it and returns <paramref name="exitCode"/>.</summary>
/// <remarks>
/// The original shut down multiplayer, the mouse timer, sound and the display and called <c>_exit(1)</c>. The port
/// throws <see cref="MCGameQuit"/> instead, so the GUI system's stop and SDL's own cleanup still run.
/// </remarks>
[[noreturn]] void KillTheGame(int32_t exitCode = 0);
/// <summary>Whether the game system text window is up (SYSTEM.CFG's game system debug option).</summary>
bool HasGameSystemWindow();
/// <summary>Prints <paramref name="line"/> in the game system text window, when it is up.</summary>
void PrintGameSystem(std::string line);

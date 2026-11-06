#include "stdafx.h"
#include "main/MCGameSession.h"
#include "abl/MCAblDebuggerWindow.h"
#include "abl/MCScrollingTextWindow.h"
#include "camera/MCCamera.h"
#include "color/MCWaterCycle.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCGuiTextObject.h"
#include "lib/MCFastFileSet.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "linkup/MCSessionManager.h"
#include "main/MCAblDebuggerConsole.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCSystemConfig.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"
#include "object/MCMechWarrior.h"
#include "sound/MCSoundSystem.h"

namespace
{
    /// <summary>The game system text window the session holds, while it does.</summary>
    std::weak_ptr<MCScrollingTextWindow> GameSystemWindow;

    /// <summary>The ABL debugger window's place on the screen.</summary>
    struct MCDebuggerPlacement
    {
        int32_t X = 0;
        int32_t Y = 400;
        int32_t Width = 260;
        int32_t Height = 125;
    };

    /// <summary>iface.fit's [ABL Window] (the defaults for what it lacks).</summary>
    MCDebuggerPlacement ReadDebuggerPlacement()
    {
        MCDebuggerPlacement placement;
        MCFitIniFile ifaceFile;

        if (ifaceFile.Open(GamePath(InterfacePath, "iface", ".fit")) != 0 || ifaceFile.SeekBlock("ABL Window") != 0)
        {
            return placement;
        }

        // Read as unsigned numbers, as the original did.
        const auto read = [&ifaceFile](std::string_view name, int32_t& value)
        {
            if (const MCFitResult<uint32_t> number = ifaceFile.Read<uint32_t>(name); number.has_value())
            {
                value = static_cast<int32_t>(*number);
            }
        };

        read("X", placement.X);
        read("Y", placement.Y);
        read("Width", placement.Width);
        read("Height", placement.Height);
        return placement;
    }
}

MCGameSession::MCGameSession()
{
    // Port: the original switched off the screen saver, low-power and power-off timeouts (SystemParametersInfo) and
    // put them back when it ended. SDL keeps the screen saver off while its window is up.
    GlobalPane = ScreenPort()->SharedFrame();

    if (DebugGameSystem)
    {
        _GameSystemWindow = ScreenWindow()->CreateChild<MCScrollingTextWindow>(
            [this](MCScrollingTextWindow& window)
            {
                _GameSystemWindow = window.WeakAs<MCScrollingTextWindow>();
                window.Init(10, 20, 250, 300, "Game System");
            });
        GameSystemWindow = _GameSystemWindow;
    }

    if (AblDebuggerEnabled)
    {
        const MCDebuggerPlacement placement = ReadDebuggerPlacement();
        _DebuggerWindow = ScreenWindow()->CreateChild<MCAblDebuggerWindow>(
            [this, &placement](MCAblDebuggerWindow& window)
            {
                _DebuggerWindow = window.WeakAs<MCAblDebuggerWindow>();
                window.Init(placement.X, placement.Y, placement.Width, placement.Height, "ABL Developer Studio (tm)");
            });
        _DebuggerWindow->Input().SetEventRoutine(AblDebuggerEventRoutine);
    }

    if (SoundSystem() == nullptr)
    {
        MCGameContext::Current().SetSoundSystem(std::make_unique<MCSoundSystem>("sound"));
    }

    _ColorCallback->SetExec(CycleColors);
    GuiSystem().AddCallback(_ColorCallback);

    // Multiplayer is made to ask the session manager whether a lobby launched the game, and dropped when not. The
    // port has no lobby (MCDirectPlay), so it is always dropped here.
    MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
    MultiPlayer()->Start();
    LaunchedFromLobby = MCSessionManager::WasLaunchedFromLobby();

    if (!LaunchedFromLobby)
    {
        MCGameContext::Current().SetMultiPlayer(nullptr);
    }

    MCGameContext::Current().SetMission(std::make_unique<MCMission>());

    // A game segment (-mission N on the command line) starts SYSTEM.CFG's missionName; otherwise the campaign.
    if (const int32_t result = Mission()->Load(GlobalGameSegment == 0 ? CampaignFile : MissionName); result != 0)
    {
        Fatal(result, "Error initializing mission.");
    }
}

auto MCGameSession::DebuggerWindow() const -> std::shared_ptr<MCAblDebuggerWindow>
{
    return _DebuggerWindow.Lock();
}

MCGameSession::~MCGameSession()
{
    CloseGuiObject(_DebuggerWindow);

    if (Mission() != nullptr)
    {
        // Taken down while still installed: what it frees reaches for it.
        Mission()->Shutdown();
        MCGameContext::Current().SetMission(nullptr);
    }

    if (HasGuiSystem())
    {
        GuiSystem().RemoveCallback(*_ColorCallback);
    }

    _ColorCallback->Clear();

    if (MultiPlayer() != nullptr)
    {
        MCGameContext::Current().SetMultiPlayer(nullptr);
    }

    MCGameContext::Current().FastFiles().Clear();
    MCGameContext::Current().SetSoundSystem(nullptr);

    if (_GameSystemWindow != nullptr)
    {
        GameSystemWindow.reset();
        CloseGuiObject(_GameSystemWindow);
    }
}

void KillTheGame(int32_t exitCode)
{
    throw MCGameQuit{exitCode};
}

auto HasGameSystemWindow() -> bool
{
    return !GameSystemWindow.expired();
}

auto PrintGameSystem(std::string line) -> void
{
    if (const std::shared_ptr<MCScrollingTextWindow> window = GameSystemWindow.lock())
    {
        window->Print(line.data());
    }
}

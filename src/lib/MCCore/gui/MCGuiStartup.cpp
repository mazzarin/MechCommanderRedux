#include "stdafx.h"
#include "network/MCMPGameConfiguration.h"
#include "gui/MCGuiStartup.h"
#include "color/MCPalette.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "linkup/MCFidpMessage.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCLinkupMessages.h"
#include "linkup/MCSessionManager.h"
#include "main/MCGamePaths.h"
#include "logistics/MCConnectMenu.h"
#include "main/MCGameContext.h"
#include "main/MCGameSession.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCFileSystem.h"
#include "platform/MCFrameLog.h"
#include "platform/MCInput.h"
#include "platform/MCPresenter.h"
#include "platform/MCRenderer.h"
#include "platform/MCWin32Defs.h"

namespace
{
    /// <summary>The test message <see cref="SendAndReceiveTestMessages"/> sends: a header, the frame and a count.</summary>
#pragma pack(push, 1)
    struct MCTestMessage
    {
        MCFIGuaranteedMessageHeader Header;
        uint32_t Frame = 0;
        uint32_t Index = 0;
    };
#pragma pack(pop)
    static_assert(sizeof(MCTestMessage) == 0x10);

    /// <summary>The network test's frame count.</summary>
    uint32_t NetworkFrame = 0;

    /// <summary>Takes a test message (looks its sender up).</summary>
    void TestMsgCallback(MCFidpMessage& message)
    {
        MultiPlayer()->SessionManager->GetPlayer(message.FromID);
    }

    /// <summary>The network test: sends two test messages to the group every 50 ms, forever (Escape asserts out).</summary>
    [[noreturn]] void SendAndReceiveTestMessages()
    {
        MCSessionManager& manager = *MultiPlayer()->SessionManager;
        MCTestMessage test = {};
        test.Header.Header = 0x1064;
        manager.ApplicationCallback = TestMsgCallback;

        for (;;)
        {
            uint32_t frameStart;

            do
            {
                Assert((MCInput::GetAsyncKeyState(VK_ESCAPE) & 1) == 0, 0, "User exited");
                manager.ProcessMessages();

                for (uint32_t i = 0; i < 2; i++)
                {
                    test.Frame = NetworkFrame;
                    test.Index = i;
                    manager.SendMessageToGroup(0, LinkupBytes(test));
                }

                NetworkFrame++;
                frameStart = MCPort::Milliseconds();
            } while (frameStart + 50 <= MCPort::Milliseconds());

            while (MCPort::Milliseconds() < frameStart + 50)
            {
            }
        }
    }

    /// <summary>
    /// The command line's <c>-network</c> test: joins or hosts the session the mission file's "Multiplayer" block
    /// names, waits for the players, then runs <see cref="SendAndReceiveTestMessages"/>.
    /// </summary>
    void StartMultiplayerGame(std::string_view missionFile)
    {
        MCFitIniFile gameFile;
        MCInput::GetAsyncKeyState(VK_ESCAPE);

        if (gameFile.Open(GamePath(MissionPath, missionFile, "")) != 0 || gameFile.SeekBlock("Multiplayer") != 0)
        {
            return;
        }

        MCGameContext::Current().SetMultiPlayer(std::make_shared<MCMultiPlayer>());
        Assert(MultiPlayer()->Start() == 0, 0, "could not initialize multiplayer");

        if (MultiPlayer()->StartScriptedGame(gameFile) == static_cast<int32_t>(0x8877042e))
        {
            const int32_t numPlayers = MultiPlayer()->NumPlayers();
            uint32_t tries = 0;

            if (!MultiPlayer()->IsServer)
            {
                uint32_t result;

                do
                {
                    tries += 50;
                    Assert((MCInput::GetAsyncKeyState(VK_ESCAPE) & 0x8000) == 0, 0, "User exited");
                    result = static_cast<uint32_t>(MultiPlayer()->JoinSession());
                    Assert(result != 0xfffffffe, result, "Error joining session!");
                } while (result != 0);
            }
            else
            {
                MultiPlayer()->CreateSession(6);

                while (MultiPlayer()->PlayersInSession() < numPlayers)
                {
                    tries++;

                    if (tries % 50 == 0)
                    {
                        MultiPlayer()->ProcessReceiveList();
                        Assert((MCInput::GetAsyncKeyState(VK_ESCAPE) & 1) == 0, 0, "User exited");
                    }
                }

                Assert(MultiPlayer()->PlayersInSession() > 1, 0, "No other players joined in time.");
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        MultiPlayer()->ProcessReceiveList();

        while (MultiPlayer()->SessionManager->MyPlayer()->PlayerNumber < 0)
        {
            MultiPlayer()->ProcessReceiveList();
            Assert((MCInput::GetAsyncKeyState(VK_ESCAPE) & 0x8000) == 0, 0, "User exited");
        }

        SendAndReceiveTestMessages();
    }

    /// <summary>A mission number from the command line: <paramref name="last"/> at most, else 0 (none).</summary>
    int32_t SegmentNumber(std::string_view word, int32_t last)
    {
        const int32_t segment = std::atoi(std::string(word).c_str());
        return segment >= 1 && segment <= last ? segment : 0;
    }
}

auto ParseCommandLine(std::string_view commandLine) -> void
{
    // The words, split at runs of spaces, respecting quoted arguments.
    std::vector<std::string> words;
    size_t pos = 0;

    while (pos < commandLine.size())
    {
        while (pos < commandLine.size() && (commandLine[pos] == ' ' || commandLine[pos] == '\t'))
        {
            pos++;
        }

        if (pos >= commandLine.size())
        {
            break;
        }

        // Only double quotes group words: an apostrophe is part of a name or path (O'Brien).
        std::string token;
        bool inQuote = false;

        while (pos < commandLine.size())
        {
            const char c = commandLine[pos];

            if (inQuote)
            {
                if (c == '"')
                {
                    inQuote = false;
                    pos++;
                }
                else
                {
                    token += c;
                    pos++;
                }
            }
            else
            {
                if (c == '"')
                {
                    inQuote = true;
                    pos++;
                }
                else if (c == ' ' || c == '\t')
                {
                    break;
                }
                else
                {
                    token += c;
                    pos++;
                }
            }
        }

        words.push_back(std::move(token));
    }

    std::optional<std::string_view> networkFile;

    // A switch's argument: the next word, if there is one.
    for (size_t i = 0; i < words.size(); i++)
    {
        const std::string_view word = words[i];
        const bool hasArgument = i + 1 < words.size();

        if (MCIEquals(word, "-host") || MCIEquals(word, "--host"))
        {
            LobbyLaunchArgs.Active = true;
            LobbyLaunchArgs.IsHost = true;

            if (hasArgument && !words[i + 1].empty() && words[i + 1][0] != '-')
            {
                const std::string& next = words[++i];
                const int port = std::atoi(next.c_str());

                if (port > 0 && port < 65536)
                {
                    LobbyLaunchArgs.Address = ":" + next;
                }
                else
                {
                    LobbyLaunchArgs.SessionName = next;
                }
            }
        }
        else if (MCIEquals(word, "-join") || MCIEquals(word, "--join"))
        {
            LobbyLaunchArgs.Active = true;
            LobbyLaunchArgs.IsHost = false;

            if (hasArgument && !words[i + 1].empty() && words[i + 1][0] != '-')
            {
                LobbyLaunchArgs.Address = words[++i];
            }
        }
        else if (MCIEquals(word, "-player") || MCIEquals(word, "--player"))
        {
            if (hasArgument)
            {
                LobbyLaunchArgs.PlayerName = words[++i];
            }
        }
        else if (MCIEquals(word, "-session") || MCIEquals(word, "--session"))
        {
            if (hasArgument)
            {
                LobbyLaunchArgs.SessionName = words[++i];
            }
        }
        else if (MCIEquals(word, "-maxplayers") || MCIEquals(word, "--maxplayers"))
        {
            if (hasArgument)
            {
                LobbyLaunchArgs.MaxPlayers = std::atoi(words[++i].c_str());
            }
        }
        else if (MCIEquals(word, "-port") || MCIEquals(word, "--port"))
        {
            if (hasArgument)
            {
                const std::string p = words[++i];

                if (LobbyLaunchArgs.IsHost)
                {
                    LobbyLaunchArgs.Address = ":" + p;
                }
                else if (!LobbyLaunchArgs.Address.empty() && LobbyLaunchArgs.Address.find(':') == std::string::npos)
                {
                    LobbyLaunchArgs.Address += ":" + p;
                }
            }
        }
        else if (MCIEquals(word, "-matchmap"))
        {
            // Port: "-matchmap <mpk name>" with -team1rp/-team2rp (per player), -team1tech/-team2tech (is|clan) and
            // "-slot <player> <team 1|2> <slot 0-2>" per player fill the host's MPGameConfiguration: a match whose
            // settings are all known, which skips the ready room and goes from "Waiting for Players" to the mech bay.
            if (hasArgument)
            {
                // The bare name: a folder or ".mpk" typed by habit is dropped.
                std::string name = words[++i];
                const size_t slash = name.find_last_of("\\/:");
                name = slash == std::string::npos ? name : name.substr(slash + 1);

                if (name.size() > 4 && MCIEquals(name.substr(name.size() - 4), ".mpk"))
                {
                    name.resize(name.size() - 4);
                }

                GameConfiguration.Active = true;
                GameConfiguration.Mission = std::move(name);
            }
        }
        else if (MCIEquals(word, "-team1rp") || MCIEquals(word, "-team2rp"))
        {
            if (hasArgument)
            {
                const int32_t points = std::max(std::atoi(words[i + 1].c_str()), 0);
                (MCIEquals(word, "-team1rp") ? GameConfiguration.Team1RpPerPlayer
                                             : GameConfiguration.Team2RpPerPlayer) = points;
                i++;
            }
        }
        else if (MCIEquals(word, "-team1tech") || MCIEquals(word, "-team2tech"))
        {
            if (hasArgument)
            {
                const auto techBase = static_cast<int8_t>(MCIEquals(words[i + 1], "clan") ? -1 : 1);
                (MCIEquals(word, "-team1tech") ? GameConfiguration.Team1TechBase : GameConfiguration.Team2TechBase) =
                    techBase;
                i++;
            }
        }
        else if (MCIEquals(word, "-slot"))
        {
            if (i + 3 < words.size())
            {
                MPGamePlayer player;
                player.Name = words[i + 1];
                player.Team = static_cast<int8_t>(std::atoi(words[i + 2].c_str()));
                player.Slot = static_cast<int8_t>(std::atoi(words[i + 3].c_str()));
                GameConfiguration.Roster.push_back(std::move(player));
                i += 3;
            }
        }
        else if (MCIEquals(word, "-mission"))
        {
            if (hasArgument)
            {
                GlobalGameSegment = SegmentNumber(words[++i], 99);
            }
            else
            {
                i++;
            }
        }
        else if (word == "+")
        {
            if (hasArgument)
            {
                GlobalGameSegment = SegmentNumber(words[++i], 50);
            }
            else
            {
                i++;
            }
        }
        else if (MCIEquals(word, "-renderer"))
        {
            // Port: "-renderer vulkan|software" picks the renderer over PREFS "Renderer".
            if (hasArgument)
            {
                if (const std::optional<MCRendererKind> kind = MCRendererKindFromName(std::string(words[i + 1])))
                {
                    GRenderer = static_cast<int>(*kind);
                }
            }

            i++;
        }
        else if (MCIEquals(word, "-gpudraw"))
        {
            // Port: "-gpudraw off|on|mirror": who draws the frame with the Vulkan renderer (see MCGpuDrawing).
            if (hasArgument)
            {
                if (const std::optional<MCGpuDrawing> drawing = MCGpuDrawingFromName(std::string(words[i + 1])))
                {
                    MCRenderer::RequestGpuDrawing(*drawing);
                }
            }

            i++;
        }
        else if (MCIEquals(word, "-fps"))
        {
            // Port: "-fps" draws the frame counter (as PREFS "ShowFps").
            GShowFps = 1;
        }
        else if (MCIEquals(word, "-framelog"))
        {
            // Port: "-framelog <file>" writes the slow frames there, with what took their time (MCFrameLog).
            if (hasArgument && !MCFrameLog::Open(MCFileSystem::FromUtf8(words[i + 1])))
            {
                SDL_Log("-framelog: can't write %s", std::string(words[i + 1]).c_str());
            }

            i++;
        }
        else if (MCIEquals(word, "-debug") || MCIEquals(word, "--debug"))
        {
            // Port: "-debug" lets Enter open the debug console in a single-player mission (MCGuiConsole).
            DebugOn = true;
        }
        else if (MCIEquals(word, "-novsync"))
        {
            // Port: "-novsync" shows frames as soon as they are drawn instead of at the display's refresh.
            GVSync = false;
        }
        else if (MCIEquals(word, "-gpudump"))
        {
            // Port: "-gpudump <folder>": mirror mode saves the first frame that differs there.
            if (hasArgument)
            {
                MCRenderer::SetMirrorDumpFolder(MCFileSystem::FromUtf8(words[i + 1]));
            }

            i++;
        }
        else if (MCIEquals(word, "-network"))
        {
            if (hasArgument)
            {
                networkFile = words[i + 1];
            }

            i++;
        }
        else if (MCIEquals(word, "-load"))
        {
            if (hasArgument)
            {
                StartupPakFile = words[i + 1];
            }

            i++;
        }
    }

    if (LobbyLaunchArgs.Active)
    {
        LaunchedFromLobby = true;
    }

    // Port: only the host runs a configured match, and only a configuration that can be run (a mission, a player on
    // each team, each player once on a free slot).
    if (GameConfiguration.Active)
    {
        std::string why;

        if (!LobbyLaunchArgs.IsHost)
        {
            why = "needs -host";
        }

        if (!why.empty() || !GameConfiguration.Validate(why))
        {
            SDL_Log("-matchmap: %s; running as a normal game", why.c_str());
            GameConfiguration = MPGameConfiguration{};
        }
    }

    if (networkFile)
    {
        StartMultiplayerGame(*networkFile);
    }
}

auto RunGame(std::string_view commandLine) -> int
{
    // The original noted the stack top and warned when the page file was under 48,000,000 bytes (GlobalMemoryStatus,
    // string 0x355), and refused a second instance; none of that applies.
    MCPort::SeedRand(static_cast<uint32_t>(std::time(nullptr)));
    SavePath = R"(c:\Program Files\Honor Bound\)";
    DirectXPath = R"(\honorb\directx\)";
    TerrainPath = "data\\terrain\\";
    PalettePath = "data\\palette\\";
    ArtPath = "data\\art\\";
    FontPath = "data\\fonts\\";
    SoundPath = "data\\sound\\";
    SpritePath = "data\\sprites\\";
    InterfacePath = "data\\iface\\";
    PaletteName = "palette.gif";
    OldMouseY = -1;
    OldMouseX = -1;

    MCGameContext& context = MCGameContext::Current();
    context.SetGuiSystem(std::make_unique<MCGuiSystem>());

    // KillTheGame ends the game from wherever it is (MCGameQuit). During the start that is only before the display is
    // up (no system.cfg), so there is nothing to stop yet.
    try
    {
        if (GuiSystem().Start(commandLine, 640, 480) != 0)
        {
            return -4;
        }
    }
    catch (const MCGameQuit& quit)
    {
        context.SetGuiSystem(nullptr);
        return quit.ExitCode;
    }

    int exitCode = 0;

    try
    {
        GuiSystem().Run();
    }
    catch (const MCGameQuit& quit)
    {
        exitCode = quit.ExitCode;
    }

    GuiSystem().Stop();
    context.SetGuiSystem(nullptr);
    return exitCode;
}

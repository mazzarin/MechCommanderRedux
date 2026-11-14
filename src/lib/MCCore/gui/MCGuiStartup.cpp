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
#include "main/MCBenchmark.h"
#include "main/MCGamePaths.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCInternetMenu.h"
#include "main/MCGameContext.h"
#include "main/MCGameSession.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"
#include "network/MCRejoinNote.h"
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

    /// <summary>The command line's words, split at runs of spaces; only double quotes group words.</summary>
    std::vector<std::string> CommandLineWords(std::string_view commandLine)
    {
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

        return words;
    }

    bool IsHostSwitch(std::string_view word)
    {
        return MCIEquals(word, "-host") || MCIEquals(word, "--host");
    }

    bool IsJoinSwitch(std::string_view word)
    {
        return MCIEquals(word, "-join") || MCIEquals(word, "--join");
    }

    /// <summary>A mission number from the command line: <paramref name="last"/> at most, else 0 (none).</summary>
    int32_t SegmentNumber(std::string_view word, int32_t last)
    {
        const int32_t segment = std::atoi(std::string(word).c_str());
        return segment >= 1 && segment <= last ? segment : 0;
    }

    /// <summary>The lobby's switches that take the word after them, as ParseCommandLine reads them.</summary>
    constexpr std::array<std::string_view, 13> LobbyValueSwitches{
        "-player", "--player",  "-session", "--session", "-maxplayers", "--maxplayers", "-port",
        "--port",  "-matchmap", "-team1rp", "-team2rp",  "-team1tech",  "-team2tech"};
    /// <summary>Port (Redux HD): an internet game's switches: hosted on the service or joined by its code, and listed.</summary>
    constexpr std::string_view InternetSwitch = "-internet";
    constexpr std::string_view PublicSwitch = MCRejoinNote::PublicSwitch;
    /// <summary>
    /// Port (Redux HD): a restarted host's place on the service, and the words it takes (its code and token); a room the
    /// menus opened (network/MCRejoinNote.h).
    /// </summary>
    constexpr std::string_view PlaceSwitch = "-place";
    constexpr size_t PlaceWords = 2;
    constexpr std::string_view MenusSwitch = "-menus";
    /// <summary>The lobby's -slot and the words it takes: the player, the team and the slot.</summary>
    constexpr std::string_view SlotSwitch = "-slot";
    constexpr size_t SlotWords = 3;

    /// <summary>
    /// How many words after <paramref name="words"/>[<paramref name="at"/>] it takes, as ParseCommandLine reads them,
    /// when it is one of the lobby's words; none when it isn't.
    /// </summary>
    std::optional<size_t> LobbyWordValues(const std::vector<std::string>& words, size_t at)
    {
        const std::string_view word = words[at];
        const size_t hasArgument = at + 1 < words.size() ? 1 : 0;

        if (IsHostSwitch(word) || IsJoinSwitch(word))
        {
            // The next word only when it isn't a switch.
            return hasArgument != 0 && !words[at + 1].empty() && words[at + 1][0] != '-' ? 1 : 0;
        }

        if (MCIEquals(word, MCRejoinNote::RejoinSwitch) || MCIEquals(word, InternetSwitch) ||
            MCIEquals(word, PublicSwitch) || MCIEquals(word, MenusSwitch))
        {
            return 0;
        }

        if (MCIEquals(word, PlaceSwitch))
        {
            return at + PlaceWords < words.size() ? std::optional<size_t>(PlaceWords) : std::nullopt;
        }

        if (MCIEquals(word, SlotSwitch))
        {
            // A -slot short of its words is read as nothing.
            return at + SlotWords < words.size() ? std::optional<size_t>(SlotWords) : std::nullopt;
        }

        if (MPIsMatchOptionFlag(word) ||
            std::ranges::any_of(LobbyValueSwitches, [&](std::string_view lobby) { return MCIEquals(word, lobby); }))
        {
            return hasArgument;
        }

        return std::nullopt;
    }

    /// <summary>Adds <paramref name="word"/> to <paramref name="line"/>, quoted when it is empty or has a space or tab.</summary>
    void AddWord(std::string& line, std::string_view word)
    {
        if (!line.empty())
        {
            line += ' ';
        }

        if (word.empty() || word.find_first_of(" \t") != std::string_view::npos)
        {
            line += std::format("\"{}\"", word);
        }
        else
        {
            line += word;
        }
    }
}

auto LobbyCommandWords(std::span<const std::string> words) -> std::string
{
    std::string line;

    for (const std::string& word : words)
    {
        AddWord(line, word);
    }

    return line;
}

auto LobbyStartLogged(std::string_view commandLine) -> std::string
{
    constexpr std::string_view Starting = "starting";
    constexpr std::string_view Hosting = "hosting";
    constexpr std::string_view Joining = "joining";
    constexpr std::string_view HostingAgainFormat = "hosting {} again";
    const std::vector<std::string> words = CommandLineWords(commandLine);
    std::string_view action = Starting;
    std::string room;
    std::optional<std::string> place;

    for (size_t i = 0; i < words.size(); i++)
    {
        const bool hasRoom = i + 1 < words.size() && !words[i + 1].empty() && words[i + 1][0] != '-';

        if (IsHostSwitch(words[i]))
        {
            action = Hosting;
        }
        else if (IsJoinSwitch(words[i]))
        {
            action = Joining;
            room = hasRoom ? words[i + 1] : std::string();
        }
        else if (MCIEquals(words[i], PlaceSwitch) && i + PlaceWords < words.size())
        {
            // Its code alone, not its token.
            place = words[i + 1];
            i += PlaceWords;
        }
    }

    if (action == Hosting && place.has_value())
    {
        return std::format(HostingAgainFormat, *place);
    }

    return room.empty() ? std::string(action) : std::format("{} {}", action, room);
}

auto StartsInLobby(std::string_view commandLine) -> bool
{
    return std::ranges::any_of(CommandLineWords(commandLine),
                               [](const std::string& word) { return IsHostSwitch(word) || IsJoinSwitch(word); });
}

auto LobbyWords(std::string_view commandLine) -> std::string
{
    const std::vector<std::string> words = CommandLineWords(commandLine);
    std::string kept;
    std::string left;

    for (size_t i = 0; i < words.size(); i++)
    {
        const std::optional<size_t> values = LobbyWordValues(words, i);
        std::string& line = values.has_value() ? kept : left;

        for (size_t word = i; word <= i + values.value_or(0); word++)
        {
            AddWord(line, words[word]);
        }

        i += values.value_or(0);
    }

    if (!left.empty())
    {
        SDL_Log("Lobby's words: left out %s", left.c_str());
    }

    return kept;
}

auto ParseCommandLine(std::string_view commandLine) -> void
{
    // Port-only (the personal build): a switch the lobby gives goes in LobbyWordValues too (the rejoin note keeps those).
    const std::vector<std::string> words = CommandLineWords(commandLine);

    std::optional<std::string_view> networkFile;

    // A switch's argument: the next word, if there is one.
    for (size_t i = 0; i < words.size(); i++)
    {
        const std::string_view word = words[i];
        const bool hasArgument = i + 1 < words.size();

        if (IsHostSwitch(word))
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
        else if (IsJoinSwitch(word))
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
        else if (MCIEquals(word, MCRejoinNote::RejoinSwitch))
        {
            // Port-only (the personal build): a lobby's start again from the rejoin note (network/MCRejoinNote.h).
            LobbyLaunchArgs.Rejoin = true;
        }
        else if (MCIEquals(word, InternetSwitch))
        {
            // Port (Redux HD): -host or -join an internet game, the code -join's address (network/MCInternetGame.h).
            LobbyLaunchArgs.Internet = true;
        }
        else if (MCIEquals(word, PublicSwitch))
        {
            LobbyLaunchArgs.Public = true;
        }
        else if (MCIEquals(word, PlaceSwitch))
        {
            // Port (Redux HD): a host's internet game back in its place (network/MCRejoinNote.h).
            if (i + PlaceWords < words.size())
            {
                LobbyLaunchArgs.Place = MCInternetPlace{.Code = words[i + 1], .Token = words[i + 2]};
                i += PlaceWords;
            }
        }
        else if (MCIEquals(word, MenusSwitch))
        {
            LobbyLaunchArgs.FromMenus = true;
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
        else if (MPIsMatchOptionFlag(word))
        {
            // Port: the match's options, the host's first choices on the session screen's MATCH OPTIONS (and a
            // -matchmap match's): "-matchweather <clear|rain|downpour|lightsnow|snow|blizzard|mission>", "-matchtime
            // <day|dusk|night|mission>", "-matchbraking <on|off|prefs>", "-matchartillery", "-matchsensors" and
            // "-matchmines" <on|off>.
            if (hasArgument)
            {
                MPReadMatchOption(word, words[++i], GameConfiguration.Options);
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
        else if (MCIEquals(word, "-benchmark"))
        {
            // Port-only (Redux HD): "-benchmark [weather,time,hd|original]" plays the trailer's canyon battle and a pan
            // round the map at real time, logs its frames and quits (main/MCBenchmark.h); the word only when it isn't
            // a switch.
            const bool variantGiven = hasArgument && !words[i + 1].empty() && words[i + 1][0] != '-';
            MCBenchmark::Arm(variantGiven ? std::string_view(words[++i]) : std::string_view());
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
        LobbyLaunchArgs.CommandLine = commandLine;
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
            // The match options stay the session screen's first choices.
            SDL_Log("-matchmap: %s; running as a normal game", why.c_str());
            const MCMatchOptions options = GameConfiguration.Options;
            GameConfiguration = MPGameConfiguration{};
            GameConfiguration.Options = options;
        }
    }

    if (networkFile)
    {
        StartMultiplayerGame(*networkFile);
    }
}

auto RunGame(std::string_view commandLine, std::chrono::milliseconds internetLeaveWait) -> int
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

    // Port-only (Redux HD): a benchmark's run lets go of the mission's units while the game is still there.
    MCBenchmark::Release();

    // Port (Redux HD): an internet game's leaving reaches the service before the session goes (its place freed, a
    // host's game ended or handed on), and the internet screens let go of their requests.
    if (const std::shared_ptr<MCMultiPlayer> multiPlayer = MultiPlayer();
        multiPlayer != nullptr && multiPlayer->SessionManager != nullptr)
    {
        multiPlayer->SessionManager->EndInternet(internetLeaveWait);
    }

    ReleaseInternetMenus();
    GuiSystem().Stop();
    context.SetGuiSystem(nullptr);
    return exitCode;
}

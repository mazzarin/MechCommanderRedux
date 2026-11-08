#pragma once

// Port (Redux HD): an internet game on this machine (SPEC-in-game-lobby.md): one it hosts on the service, or one it
// joins by its code. Its links (platform/MCIceTransport, the service's signals through platform/MCServiceSignaling)
// stand in for the system's sockets while it lives (platform/MCNetSwitch), so MCDirectPlay hosts and joins it as a LAN
// game. Each step waits on the network across frames: Update moves it along and says how far it got, so the game's
// own thread never waits. It ends when it's destroyed or End is called: its links close, and its sockets close, which
// the service takes as its player leaving (it frees the place; a host's leaving ends the game).

#include "platform/MCIceTransport.h"
#include "platform/MCServiceSignaling.h"
#include "update/MCHttpTransfer.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

class MCNetSwitch;

/// <summary>The internet games' service (tools/cloudflare/games in the workspace).</summary>
inline constexpr std::string_view MCInternetService = "wss://mechcommander-redux-games.mazzarin.workers.dev";

/// <summary>What internet games are made with: the game's set them at its start (Main.cpp), the tests their own.</summary>
struct MCInternetSetup
{
    /// <summary>Opens a socket to the service: none, and there are no internet games.</summary>
    MCWebSocketOpener Open;
    /// <summary>Makes a transfer for the service's plain requests (the count online): none, and none is asked.</summary>
    std::function<std::unique_ptr<MCHttpSource::MCGet>()> Get;
    /// <summary>The build's version, which every socket says (MC_VERSION_SHOWN).</summary>
    std::string Version;
    /// <summary>The copy's ID every socket says: empty, the one kept in the user folder (MCInternetCopyId).</summary>
    std::string Copy;
    std::string Service = std::string(MCInternetService);
    /// <summary>How long the service gets to answer a host or a guest by default.</summary>
    static constexpr std::chrono::milliseconds DefaultServiceWait{15000};

    /// <summary>How long the service gets to answer a host or a guest.</summary>
    std::chrono::milliseconds ServiceWait = DefaultServiceWait;
    MCIceTransport::Deadlines Deadlines;
    /// <summary>The links bind to this address alone (the tests' loopback: MCServiceSignaling::BindLinksTo).</summary>
    std::string BindAddress;
};

/// <summary>An internet game on this machine (see the file's comment).</summary>
class MCInternetGame
{
public:
    enum class Stage
    {
        /// <summary>The service is answering: a host's game made, a guest's place in a game.</summary>
        Service,
        /// <summary>A guest's link to its host opening.</summary>
        Reaching,
        /// <summary>A host: its game is on the service (its code known). A guest: its link to the host is open.</summary>
        Ready,
        /// <summary>It couldn't start (<see cref="Refusal"/> says why), or it ended.</summary>
        Failed,
    };

    /// <summary>Sets what internet games are made with (the setup with no opener: none can be).</summary>
    static void Setup(MCInternetSetup setup);
    /// <summary>Whether internet games can be made (a socket opener is set).</summary>
    static bool Available();
    /// <summary>What internet games are made with now (a copy: the INTERNET screens' list and count use it too).</summary>
    static MCInternetSetup SetupNow();

    /// <summary>
    /// Hosts a game on <paramref name="net"/>: a public one is listed and its links take relays only, so no player
    /// learns another's address. None when <see cref="Available"/> isn't.
    /// </summary>
    static std::unique_ptr<MCInternetGame> Host(MCNetSwitch& net, bool isPublic);
    /// <summary>Joins the game whose code is <paramref name="code"/> (either case) on <paramref name="net"/>.</summary>
    static std::unique_ptr<MCInternetGame> Join(MCNetSwitch& net, std::string_view code);
    /// <summary>A code as the service and the links name it, typed in either case.</summary>
    static std::string CodeOf(std::string_view typed);

    ~MCInternetGame();
    MCInternetGame(const MCInternetGame&) = delete;
    MCInternetGame& operator=(const MCInternetGame&) = delete;

    /// <summary>Moves it along (the service's answer, a guest's link): how far it has got.</summary>
    Stage Update();
    bool Hosting() const { return _Hosting; }
    /// <summary>The game's code: a host's once the service gave it (empty before), a guest's as asked for.</summary>
    const std::string& Code() const { return _Code; }
    /// <summary>Why it failed: the service's refusal, or Unreachable (no answer, or no link to the host).</summary>
    std::optional<MCServiceRefusal> Refusal() const { return _Refusal; }
    /// <summary>A host's room as it stands, for the service's list (its public setting the game's own).</summary>
    void SetRoom(MCServiceRoom room);
    /// <summary>Whether the service won't list the room by its name.</summary>
    bool NameRefused();

    /// <summary>
    /// Ends it: its links close, and its sockets close as its player leaving, waiting up to <paramref name="wait"/> for
    /// their closes to go (a quitting game's, which the process's end would cut off).
    /// </summary>
    void End(std::chrono::milliseconds wait = {});

private:
    using Clock = std::chrono::steady_clock;

    MCInternetGame(MCNetSwitch& net, const MCInternetSetup& setup, bool hosting, bool isPublic, std::string code);

    /// <summary>Fails with <paramref name="refusal"/>, saying why in the log.</summary>
    Stage Fail(MCServiceRefusal refusal, std::string_view why);
    /// <summary>The game as the log names it: its code, or that it's being hosted.</summary>
    std::string_view LogName() const;

    MCNetSwitch& _Net;
    std::shared_ptr<MCServiceSignaling> _Signaling;
    std::shared_ptr<MCIceTransport> _Transport;
    bool _Hosting = false;
    bool _Public = false;
    std::string _Code;
    Stage _Stage = Stage::Service;
    std::optional<MCServiceRefusal> _Refusal;
    Clock::time_point _AskedAt;
    std::chrono::milliseconds _ServiceWait;
    /// <summary>A guest's link to its host: when it began opening, and how long it may take (the transport's own wait, and more).</summary>
    Clock::time_point _ReachingSince;
    std::chrono::milliseconds _ReachWait;
};

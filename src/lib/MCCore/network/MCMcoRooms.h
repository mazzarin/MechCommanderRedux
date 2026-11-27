#pragma once

// Port-only (Redux HD; SPEC-mco-rooms.md): MechCommander Online in the game: the player's account (signed in, a guest's
// profile, signed out), MCO's Redux rooms while INTERNET lists them, the room the player is in, and its matches, which
// the game plays itself on this computer's loopback, carried over MCO's relay by the bridge (network/MCMcoBridge.h) as
// MCO's helper carries a launcher's game. MCO's services are asked on a thread of its own (each request takes a moment,
// and the lobby and a room are polled about once a second, as MCO's launcher polls them); the screens read a copy of
// what it has heard (<see cref="MCMcoRooms::Seen"/>) and ask for what the player does. Nothing of MCO's runs.

#include "network/MCMcoApi.h"
#include "network/MCMcoBridge.h"
#include "network/MCMcoKeys.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

/// <summary>What MCO in the game is made with: the game's set at its start (Main.cpp), the tests their own.</summary>
struct MCMcoSetup
{
    /// <summary>Makes a transfer for MCO's services (the platform's): none, and there's no MCO in the game.</summary>
    std::function<std::unique_ptr<MCHttpSource::MCGet>()> Get;
    /// <summary>Who this copy is to MCO, and where its services are.</summary>
    MCMcoApi::MCClient Client;
    /// <summary>Where the sign-in is kept (none: nothing is kept between starts).</summary>
    std::shared_ptr<MCMcoKeys> Keys;
    /// <summary>A match's connections to its relay (TLS with the computer's trust; the tests' plain TCP).</summary>
    std::function<MCMcoBridge::MCDial(const MCMcoApi::MCMatch& match)> Dial;
    /// <summary>The game data's fingerprint as MCO takes it (network/MCMcoContent.h), or why it couldn't be made.</summary>
    std::function<std::expected<std::string, std::string>()> Content;
    /// <summary>A free port for the host's game, TCP and UDP (0: none); none given, the computer's own.</summary>
    std::function<uint16_t()> GamePort;
    /// <summary>How often the lobby and a room are polled: about once a second, as MCO's launcher (850-1150 ms).</summary>
    static constexpr std::chrono::milliseconds DefaultPollLeast{850};
    static constexpr std::chrono::milliseconds DefaultPollMost{1150};
    std::chrono::milliseconds PollLeast = DefaultPollLeast;
    std::chrono::milliseconds PollMost = DefaultPollMost;
    /// <summary>How long the host's game has to answer its session query before the ticket is asked (the launcher's).</summary>
    static constexpr int DefaultHostGameMs = 45000;
    int HostGameMs = DefaultHostGameMs;
};

/// <summary>Where the player stands with MCO.</summary>
enum class MCMcoAccount : uint8_t
{
    /// <summary>Not signed in (or not yet known: a kept sign-in is being resumed while <see cref="MCMcoSeen::Busy"/>).</summary>
    SignedOut,
    /// <summary>Signed in to an account.</summary>
    SignedIn,
    /// <summary>Playing on a guest's profile.</summary>
    Guest,
    /// <summary>The account is signed in on another computer: "Sign in here instead?" waits for the player.</summary>
    OtherComputer,
};

/// <summary>A match's part the game plays, as far as it has gone.</summary>
enum class MCMcoMatchStage : uint8_t
{
    /// <summary>No match.</summary>
    None,
    /// <summary>MCO and its relay are being asked for the match: "Connecting to the match...".</summary>
    Starting,
    /// <summary>The host's game is to host the match on <see cref="MCMcoSeen::GamePort"/> (the game does it).</summary>
    HostGame,
    /// <summary>A guest's game is to join the bridge at 127.0.0.1:<see cref="MCMcoSeen::GamePort"/>.</summary>
    JoinGame,
    /// <summary>The game is in the match, carried over the relay.</summary>
    Playing,
};

/// <summary>The room the player is in, as its last poll had it.</summary>
struct MCMcoRoomSeen
{
    std::string RoomId;
    std::string Name;
    bool Hosting = false;
    /// <summary>The room's players, the host first, each READY or not.</summary>
    std::vector<MCMcoApi::MCRoomPlayer> Players;
    int32_t MaxPlayers = 0;
    /// <summary>The host's name (the players' host's, else the room's).</summary>
    std::string Host;
    bool operator==(const MCMcoRoomSeen&) const = default;
};

/// <summary>What the screens show of MCO: a copy, taken under the model's lock.</summary>
struct MCMcoSeen
{
    MCMcoAccount Account = MCMcoAccount::SignedOut;
    /// <summary>The player's name on MCO (signed in or a guest).</summary>
    std::string Username;
    /// <summary>A request the player is waiting on is under way; <see cref="Doing"/> says what ("Signing in...").</summary>
    bool Busy = false;
    std::string Doing;
    /// <summary>The last thing the player asked for that MCO refused, one sentence; cleared by the next ask.</summary>
    std::string Refusal;
    /// <summary>The lobby has been polled: <see cref="Rooms"/> is MCO's Redux rooms on its stock build.</summary>
    bool Listed = false;
    std::vector<MCMcoApi::MCRoom> Rooms;
    /// <summary>The players online, as the lobby's poll counts them.</summary>
    size_t Online = 0;
    /// <summary>The lobby's last poll failed: why, in the screens' words (the list keeps what it had).</summary>
    std::string LobbyTrouble;
    /// <summary>The room the player is in.</summary>
    std::optional<MCMcoRoomSeen> Room;
    /// <summary>The room the player was in closed, or they were taken out: why ("Bob left. The room has closed.").</summary>
    std::string Left;
    MCMcoMatchStage Match = MCMcoMatchStage::None;
    /// <summary>The match's port on this computer (<see cref="MCMcoMatchStage::HostGame"/>, JoinGame and on).</summary>
    uint16_t GamePort = 0;
    /// <summary>The host's game's session name ("MCO " and the match's first 12 hex digits) and its players.</summary>
    std::string SessionName;
    int32_t Capacity = 0;
    /// <summary>The player's name in the match, as MCO's launcher passes it to the game.</summary>
    std::string MatchName;
    /// <summary>Each change bumps it: a screen redraws only when it has moved on.</summary>
    uint64_t Revision = 0;
};

/// <summary>MCO in the game (see the file's comment). One at a time: <see cref="Setup"/> makes it, the game owns it.</summary>
class MCMcoRooms
{
public:
    /// <summary>Makes MCO in the game with <paramref name="setup"/> (none with no transfer), the last one stopped.</summary>
    static void Setup(MCMcoSetup setup);
    /// <summary>Stops it (the game quitting): its thread ended, the player out of MCO's lobby or room.</summary>
    static void Release();
    /// <summary>Whether MCO is in the game (a transfer was set up).</summary>
    static bool Available();
    /// <summary>Whether the game is playing MCO's match (its game joining or in it): its room's end goes back to MCO's.</summary>
    static bool InMatch();
    /// <summary>MCO in the game; only while <see cref="Available"/>.</summary>
    static MCMcoRooms& Now();

    explicit MCMcoRooms(MCMcoSetup setup);
    ~MCMcoRooms();
    MCMcoRooms(const MCMcoRooms&) = delete;
    MCMcoRooms& operator=(const MCMcoRooms&) = delete;

    // The account (SIGN IN, CREATE ACCOUNT, PLAY AS GUEST, the other computer's question, SECURE PROFILE, SIGN OUT).
    // Each is a request on the model's thread; Seen says how it went.

    /// <summary>Resumes the sign-in kept from the last start, once (INTERNET's first show does it).</summary>
    void Resume();
    void SignIn(std::string email, std::string password);
    void CreateAccount(std::string name, std::string email, std::string password);
    void PlayAsGuest(std::string name);
    /// <summary>The answer to "You're signed in on another computer. Sign in here instead?".</summary>
    void TakeOver(bool here);
    void SecureProfile(std::string email, std::string password);
    void SignOut();

    // The lobby, while INTERNET shows it: the player registered in MCO's lobby and its rooms polled; out of it after.

    /// <summary>
    /// The lobby opened. With <paramref name="guestName"/> (INTERNET's player name, the screens' way: they have no
    /// sign-in of their own), a player not signed in plays as a guest under it, and a guest takes it as their name.
    /// </summary>
    void OpenLobby(std::string guestName = {});
    void CloseLobby();

    // The room: JOIN GAME on a room (its password, when locked), HOST GAME, LEAVE, the host's START.

    /// <summary>JOIN GAME on a room; a guest takes <paramref name="guestName"/> as their name first, when given.</summary>
    void Join(std::string roomId, std::string password, std::string guestName = {});
    void Host(std::string name, int32_t maxPlayers, std::string password);
    void Leave();
    void Start();
    /// <summary>The room's closing said (its OK): <see cref="MCMcoSeen::Left"/> cleared.</summary>
    void ClearLeft();

    // The match, as the game plays it (the screens act on MCMcoSeen::Match and say how it went).

    /// <summary>The game couldn't host or join the match: it's called off (the host's), or left (a guest's).</summary>
    void GameFailed();
    /// <summary>The game is in the match (its session joined, or hosted with the bridge open).</summary>
    void GameInMatch();
    /// <summary>The game's match is over (its session left or ended): the player is back in the room.</summary>
    void GameEnded();

    /// <summary>What the screens show, as last heard.</summary>
    MCMcoSeen Seen() const;
    /// <summary>The revision of what <see cref="Seen"/> would give (cheap: a screen asks each frame).</summary>
    uint64_t Revision() const;

    /// <summary>What the room's host START needs: two players at least, every guest READY (MCO's launcher's rule).</summary>
    static bool Startable(const MCMcoRoomSeen& room);

    /// <summary>A refusal of MCO's in the screens' words (one sentence, sentence case).</summary>
    static std::string RefusalText(const MCMcoApi::MCError& error);
    /// <summary>Why the player is no longer in the room, in a sentence naming its host.</summary>
    static std::string LeftText(std::string_view why, std::string_view host, bool inMatch);

    /// <summary>The screens' words.</summary>
    static constexpr std::string_view SigningInText = "Signing in...";
    static constexpr std::string_view CreatingText = "Making your account...";
    static constexpr std::string_view ResumingText = "Signing in to MechCommander Online...";
    static constexpr std::string_view JoiningFormat = "Joining {}...";
    static constexpr std::string_view HostingText = "Setting up your room...";
    static constexpr std::string_view ConnectingText = "Connecting to the match...";
    static constexpr std::string_view UnreachableText = "MechCommander Online can't be reached. Try again later.";
    static constexpr std::string_view ChangedText = "MechCommander Online has changed. Update Redux HD to play there.";
    static constexpr std::string_view WrongSignInText = "Wrong email or password.";
    static constexpr std::string_view ThreeAccountsText = "This computer has made its three accounts. Sign in to one "
                                                          "of them.";
    static constexpr std::string_view TooManyText = "Too many tries. Wait a minute.";
    static constexpr std::string_view FullFormat = "{} is full.";
    static constexpr std::string_view StartedFormat = "{} has started.";
    static constexpr std::string_view NoMatchText = "Couldn't connect to the match. Try again.";
    static constexpr std::string_view OtherDataText = "This room plays other game data than yours.";
    static constexpr std::string_view NoBuildText = "MechCommander Online names no build for its Redux rooms.";
    static constexpr std::string_view GoneText = "The room has closed.";
    /// <summary>A room the joining line can't name (gone from the list as it's asked).</summary>
    static constexpr std::string_view SomeRoom = "the room";
    static constexpr std::string_view RoomClosedFormat = "{} left. The room has closed.";
    static constexpr std::string_view MatchEndedFormat = "{} left. The match has ended.";
    /// <summary>A guest's name when INTERNET's isn't one MCO takes (MCO's launcher names every new guest so).</summary>
    static constexpr std::string_view GuestNameNone = "Player";

private:
    /// <summary>A request of the player's, run on the model's thread.</summary>
    using MCJob = std::function<void()>;

    void Post(MCJob job);
    void Run();
    /// <summary>The lobby or the room polled when due, a guest's match started; when to look again.</summary>
    std::chrono::steady_clock::time_point Tend();
    std::chrono::milliseconds PollGap();

    // On the model's thread.
    void DoResume();
    void Activated(const MCMcoApi::MCSignIn& signIn, bool takeOver);
    void SignedIn(const MCMcoApi::MCSignIn& signIn, std::string peerToken);
    void DoGuest(const std::string& name);
    /// <summary>A guest takes <paramref name="name"/> (one MCO takes, else GuestNameNone) as their name, kept.</summary>
    void Rename(const std::string& name);
    /// <summary>The guest's profile kept in the system's store (said in the log when it can't be).</summary>
    void KeepGuest();
    /// <summary>INTERNET's <paramref name="name"/> as a guest's: MCO's own form of it, or GuestNameNone.</summary>
    static std::string GuestName(const std::string& name);
    void DoSignOut();
    void Forget();
    bool Registered();
    void PollLobby();
    void PollRoom();
    bool Prepared();
    void DoJoin(const std::string& roomId, std::string password);
    void DoHost(const std::string& name, int32_t maxPlayers, std::string password);
    void DoLeave();
    void DoStart();
    void PlayAsGuest(const MCMcoApi::MCMatch& match);
    void HostMatch(const MCMcoApi::MCMatch& match);
    void EndMatch(bool played);
    /// <summary>The room left behind (closed, or the player taken out), with why.</summary>
    void LeftRoom(std::string why);
    void Refused(const MCMcoApi::MCError& error);
    void Refused(std::string text);
    /// <summary>Signs the player out where MCO says their sign-in is no longer good.</summary>
    bool SignsOut(const MCMcoApi::MCError& error);

    /// <summary>Changes what the screens see under the lock, bumping its revision.</summary>
    template <typename Change> void Show(Change change)
    {
        const std::scoped_lock lock(_Lock);
        change(_Seen);
        _Seen.Revision++;
    }

    MCMcoSetup _Setup;
    std::unique_ptr<MCHttpSource::MCGet> _Transfer;
    std::unique_ptr<MCMcoApi::MCService> _Service;

    mutable std::mutex _Lock;
    std::condition_variable _Wake;
    std::deque<MCJob> _Jobs;
    bool _Stopping = false;
    MCMcoSeen _Seen;

    // The model's thread's own (never touched by another).
    MCMcoApi::MCPeer _Peer;
    std::optional<MCMcoApi::MCSignIn> _Waiting;
    bool _Resumed = false;
    /// <summary>An account's sign-in was kept: no guest is made in its place.</summary>
    bool _AccountKept = false;
    bool _LobbyWanted = false;
    bool _InLobby = false;
    std::optional<MCMcoApi::MCSeat> _Seat;
    std::string _Content;
    std::string _Stock;
    std::chrono::steady_clock::time_point _NextPoll;
    std::optional<MCMcoApi::MCMatch> _Match;
    /// <summary>The match a guest played last (not played again while its start signal stays).</summary>
    std::string _Played;
    std::unique_ptr<MCMcoBridge::MCGuest> _Guest;
    std::unique_ptr<MCMcoBridge::MCHost> _Hosted;
    bool _GameIn = false;
    /// <summary>The room's polls failed in a row; a guest's tries at the match the room started, and the last's time.</summary>
    int _PollFailures = 0;
    int _MatchTries = 0;
    std::chrono::steady_clock::time_point _MatchTried;
    /// <summary>Set by the screens' thread when the game couldn't host: the wait for its answer gives up at once.</summary>
    std::atomic<bool> _Cancel = false;

    std::thread _Thread;
};

#pragma once

#include "platform/MCIceLink.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Port-only (the personal build): reports of the player's computer, how smoothly the game runs on it (frame times, what
// fell back to keep up), its settings and its crashes, under the copy's random ID, sent to the project's own server (a Cloudflare Worker) for the performance targets; PLAN.md's "Telemetry"
// has the design. A backend the app sets before the game starts (the Mac app's, apps/MCRedux/MCTelemetryMac.mm;
// Windows', MCTelemetryWin.cpp) reads the machine and sends; this keeps the player's consent, measures the play, keeps
// the black box and makes the reports. Nothing is sent before the player has seen the notice or answered the question
// (the main menu shows it: logistics/MCReportsNotice.h), nor while REPORTS (HD OPTIONS) is off; a build without a
// backend (upstream, the tests) sends nothing.
//
// Reports: a multiplayer match's at its end; an internet game's link's as it ends (opened or not); a file the campaign's
// load refused, as it does (why, and what was read of it: never its name); single-player
// missions' at the end of the first, then of the first a week or more after the last, covering every mission since, and
// of the first after an update (the play before it under the version it was played on, then its own); a
// crash's at the next start, with the black box: the last moments' state and events (the match's phases, its messages
// by type and size, players lost, loads refused), kept in a file mapped into memory so a crash leaves it behind. Play under two minutes counts for nothing. No name, path or address is in
// any report; the copy's random ID (made while REPORTS is on, dropped when it goes off) lets one copy's reports be
// followed across updates. Everything runs on the main thread: the backend's replies wait for the next frame.

/// <summary>REPORTS (PREFS' "Reports", HD OPTIONS): 1 sends reports once the player has seen what they hold.</summary>
extern int GReports;

namespace MCTelemetry
{
    /// <summary>
    /// The reports' format, sent in each as <c>v</c>. A field added keeps it (the server keeps the fields it knows).
    /// The player sees the notice or question once, whatever the version: a later one never brings it back.
    /// </summary>
    inline constexpr int32_t ReportVersion = 1;

    /// <summary>The computer, as the backend reads it.</summary>
    struct MCMachine
    {
        /// <summary>"mac" or "windows".</summary>
        std::string Platform;
        /// <summary>The game's version, as shown ("0.2.1 (468)").</summary>
        std::string AppVersion;
        /// <summary>The computer's model: "MacBookAir10,1", "Dell Inc. Inspiron 5593".</summary>
        std::string Model;
        /// <summary>The processor's name: "Apple M1".</summary>
        std::string Cpu;
        /// <summary>Its performance and efficiency cores (no efficiency cores: all are performance cores).</summary>
        int32_t PerformanceCores = 0;
        int32_t EfficiencyCores = 0;
        /// <summary>The graphics chip's cores, where the system says (Apple's); 0 when not known.</summary>
        int32_t GpuCores = 0;
        /// <summary>Memory, in whole GB.</summary>
        int32_t MemoryGb = 0;
        /// <summary>The system's version: "26.5", "10.0.22631".</summary>
        std::string System;
    };

    /// <summary>How the computer is running now: its heat and its power.</summary>
    struct MCPower
    {
        /// <summary>The system's thermal state: 0 nominal, 1 fair, 2 serious, 3 critical (throttled).</summary>
        int32_t Thermal = 0;
        /// <summary>On battery.</summary>
        bool Battery = false;
        /// <summary>Low Power Mode or battery saver.</summary>
        bool LowPower = false;
    };

    /// <summary>A crash, as the system recorded it: what failed and the failing thread's frames, with no paths.</summary>
    struct MCCrash
    {
        /// <summary>The exception: "EXC_BAD_ACCESS SIGSEGV KERN_INVALID_ADDRESS at 0x0".</summary>
        std::string What;
        /// <summary>The failing thread's frames, innermost first: "MechCommander Redux +0x1a2b3c MCFoo::Bar + 52".</summary>
        std::vector<std::string> Frames;
    };

    /// <summary>What the app gives the reports: the machine, the clock, the sending and the black box's file.</summary>
    class MCBackend
    {
    public:
        virtual ~MCBackend() = default;

        /// <summary>The computer (read once a run).</summary>
        virtual MCMachine Machine() = 0;

        /// <summary>Its heat and power now (read every few seconds of play).</summary>
        virtual MCPower Power() = 0;

        /// <summary>The countries the system's settings name: its region's, its time zone's (two-letter codes).</summary>
        virtual std::vector<std::string> LocalCountries() = 0;

        /// <summary>Now, in seconds since 1970 (UTC).</summary>
        virtual int64_t Now() = 0;

        /// <summary>
        /// The time the computer has been awake, from any start: play's frames are timed by it, so a frame across the
        /// machine asleep counts only its waking part. The Mac's and Windows' backends read their systems' clocks that
        /// stop in sleep; this one doesn't.
        /// </summary>
        virtual std::chrono::nanoseconds Awake() { return std::chrono::steady_clock::now().time_since_epoch(); }

        /// <summary>
        /// Sends <paramref name="body"/> to the server's <paramref name="path"/> (a GET when empty, else a POST of
        /// JSON), and calls <paramref name="done"/> with the HTTP status (0: no answer) and the reply, from any thread.
        /// </summary>
        virtual void Request(std::string path, std::string body,
                             std::function<void(int32_t status, std::string reply)> done) = 0;

        /// <summary>
        /// The black box's file, <paramref name="size"/> bytes mapped into memory with what the last run left in it,
        /// or empty when it can't be (the black box then lives in memory, and a crash takes it).
        /// </summary>
        virtual std::span<std::byte> MapBlackBox(size_t size) = 0;

        /// <summary>The crash the system recorded for the run that started at <paramref name="started"/>, if any.</summary>
        virtual std::optional<MCCrash> Crash(int64_t started) = 0;
    };

    /// <summary>What the main menu shows the player once: nothing, the notice, or the question.</summary>
    enum class MCPrompt : uint8_t
    {
        None,
        /// <summary>Reports are sent: the notice says what, as anonymous, and how to turn them off (OK).</summary>
        Notice,
        /// <summary>
        /// The notice, naming the reports' random ID rather than calling them anonymous: where the law counts a lasting
        /// ID as personal data but wants no consent first (the UK, Switzerland).
        /// </summary>
        NoticeWithId,
        /// <summary>Send reports? (OK or CANCEL: the law where the player is wants consent first.)</summary>
        Ask,
    };

    /// <summary>How an internet game's link ended (platform/MCIceTransport.h), for its report.</summary>
    enum class MCLinkEnd : uint8_t
    {
        /// <summary>Unopened: the service has no such game (a guest's link).</summary>
        NoGame,
        /// <summary>Unopened: the host never answered (full, not hosting, or gone; a guest's link).</summary>
        Unanswered,
        /// <summary>Unopened: the guest left the service (a host's link).</summary>
        GuestLeft,
        /// <summary>Unopened: both ends answered, and ICE found no path in time.</summary>
        NoPath,
        /// <summary>Unopened: ICE found a path, and the encrypted channel never opened over it.</summary>
        NoChannel,
        /// <summary>The other end closed it.</summary>
        Closed,
        /// <summary>It broke open: the other end stopped answering (its game or its network gone).</summary>
        Lost,
        /// <summary>This end closed it: nothing used it any more.</summary>
        Unused,
        /// <summary>This end cut it: the other end sent what this game can't take (a flood, or not its frames).</summary>
        CutOff,
        /// <summary>This end's transport went: the game left the internet game, or quit.</summary>
        Left,
    };

    /// <summary>An internet game's link as it ended: never an address, a code or a name.</summary>
    struct MCLink
    {
        /// <summary>This game opened it, joining (a guest), or answered it (the host).</summary>
        bool Guest = false;
        bool Opened = false;
        /// <summary>The path it opened on.</summary>
        MCIceLink::Path Path = MCIceLink::Path::None;
        /// <summary>Until it opened (opened), or until it ended (not).</summary>
        std::chrono::milliseconds Setup{0};
        /// <summary>How long it was open.</summary>
        std::chrono::milliseconds Open{0};
        MCIceLink::Traffic Carried;
        /// <summary>The kinds of address this end offered, and the other end.</summary>
        MCIceLink::Offered Here;
        MCIceLink::Offered There;
        MCLinkEnd End = MCLinkEnd::Left;
    };

    /// <summary>Why the campaign's load refused a file (main/MCLogistics.h's LoadCampaign), for its report.</summary>
    enum class MCLoadRefusal : uint8_t
    {
        /// <summary>A refusal the load names no reason for.</summary>
        Other,
        /// <summary>The file couldn't be opened: not there, or the system refused it.</summary>
        Unopened,
        /// <summary>It isn't a packet file, or its first packet (the save's settings) isn't in it.</summary>
        NotPacked,
        /// <summary>Its first packet doesn't open as the save's settings.</summary>
        Unreadable,
        /// <summary>Its mission number is past the campaign's end, or below 0.</summary>
        MissionPast,
        /// <summary>It names no mission, and none was set: no scenario to open.</summary>
        NoMission,
        /// <summary>Its mission number is the campaign's end itself, which picks no scenario.</summary>
        CampaignEnd,
        /// <summary>A component entry (its block, its id or its count) didn't read: the record's At says which.</summary>
        Components,
        /// <summary>The mission's results (its .pkk), which a load after a mission applies, couldn't be opened.</summary>
        NoResults,
    };

    /// <summary>A file the campaign's load refused: never its name, nor what it holds.</summary>
    struct MCRefusedLoad
    {
        MCLoadRefusal Reason = MCLoadRefusal::Other;
        /// <summary>
        /// The file's kind, its extension: "sav" a campaign save, "sol" a single mission, "pkk" the game's own start
        /// and results files, "mpk" a match's mission.
        /// </summary>
        std::string Kind;
        /// <summary>The player picked it on the load screen (else the game opened its own).</summary>
        bool Player = false;
        /// <summary>The file's bytes and packets, once it opened.</summary>
        std::optional<int64_t> Bytes;
        std::optional<int32_t> Packets;
        /// <summary>Its planet (0 the original campaign, 1 Desperate Measures), once read.</summary>
        std::optional<int32_t> Planet;
        /// <summary>The mission number it names, and the campaign's scenarios then, once read.</summary>
        std::optional<int32_t> Mission;
        std::optional<int32_t> Scenarios;
        /// <summary>The component entry that didn't read, of the entries the file says it has (Components).</summary>
        std::optional<int32_t> At;
        std::optional<int32_t> Entries;
    };

    /// <summary>The app's backend (before the game starts); none takes it away.</summary>
    void SetBackend(std::unique_ptr<MCBackend> backend);

    /// <summary>Whether this build reports (it has a backend).</summary>
    bool Available();

    /// <summary>
    /// The game started (the main menu): reads what was sent, sends the last run's crash, and finds out whether the
    /// notice or the question is due (once a run).
    /// </summary>
    void Start();

    /// <summary>The game is quitting as it should: the black box marks the run as ended.</summary>
    void Quit();

    /// <summary>What the main menu should show the player now.</summary>
    MCPrompt Prompt();

    /// <summary>The player's answer: OK to the notice or the question (send), or CANCEL to the question.</summary>
    void Answered(bool send);

    /// <summary>REPORTS was changed in HD OPTIONS: the player has chosen, so no notice or question comes.</summary>
    void Chosen();

    /// <summary>Whether reports are being sent (a backend, REPORTS on, and the player has seen what they hold).</summary>
    bool Reporting();

    /// <summary>One frame of the game's loop: its time, measured since the last, and the replies come in.</summary>
    void Frame();

    /// <summary>One frame of <paramref name="milliseconds"/> (the tests' clock).</summary>
    void Frame(double milliseconds);

    /// <summary>
    /// One frame of the game's loop that the player didn't see (the window minimized or covered), or the wait while
    /// the game wasn't the active window: in place of <see cref="Frame()"/>. It isn't play, nor is the time until the
    /// next frame seen; the replies still come in.
    /// </summary>
    void Unseen();

    /// <summary>A scenario starts loading: <paramref name="mission"/>, as a match when <paramref name="match"/>.</summary>
    void PlayLoading(std::string_view mission, bool match);

    /// <summary>The scenario is loaded and runs: <paramref name="players"/> in it, this one the server or not.</summary>
    void PlayStarted(int32_t players, bool server);

    /// <summary>The scenario ended (its results are up, or it was left): the report, when one is due.</summary>
    void PlayEnded();

    /// <summary>A player left the match, or was lost.</summary>
    void PlayerLost();

    /// <summary>An HD movie fell behind (or failed) and played on as the original.</summary>
    void MovieFellBehind();

    /// <summary>
    /// The frame about to be measured threw the HD weather's falling pieces, <paramref name="share"/> of them (1 all;
    /// fewer on a machine that can't keep up, sprite/MCWeatherPace.h).
    /// </summary>
    void WeatherDrawn(float share);

    /// <summary>
    /// An internet game's link ended: its report goes at once, as a player who can't join may quit next. At most a few
    /// dozen a run, whatever a stranger with the game's code does.
    /// </summary>
    void LinkEnded(const MCLink& link);

    /// <summary>A link's part of its report (LinkEnded's "link"), as JSON: the internet games' trial prints it too.</summary>
    std::string LinkJson(const MCLink& link);

    /// <summary>
    /// The campaign's load refused a file: the black box notes it, and its report goes at once (at most 10 a run), as
    /// no other report says what a load did.
    /// </summary>
    void LoadRefused(const MCRefusedLoad& load);

    /// <summary>A multiplayer message came in (<paramref name="type"/>, <paramref name="bytes"/>), for the black box.</summary>
    void MessageIn(uint16_t type, size_t bytes);

    /// <summary>A multiplayer message went out, for the black box.</summary>
    void MessageOut(uint16_t type, size_t bytes);

    /// <summary>The frame times' percentile <paramref name="share"/> (0.5, 0.95...) of a histogram, in milliseconds.</summary>
    double Percentile(std::span<const uint32_t> histogram, double share);

    /// <summary>Whether the law where <paramref name="country"/> is wants consent first, on day <paramref name="day"/>.</summary>
    bool AskFirst(std::string_view country, int64_t day);
}

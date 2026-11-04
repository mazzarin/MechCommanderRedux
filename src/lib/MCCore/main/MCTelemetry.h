#pragma once

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
// Reports: a multiplayer match's at its end; single-player missions' at the end of the first, then of the first a week
// or more after the last, covering every mission since; a crash's at the next start, with the black box: the last
// moments' state and events (the match's phases, its messages by type and size, players lost), kept in a file mapped
// into memory so a crash leaves it behind. Play under two minutes counts for nothing. No name, path or address is in
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

    /// <summary>A multiplayer message came in (<paramref name="type"/>, <paramref name="bytes"/>), for the black box.</summary>
    void MessageIn(uint16_t type, size_t bytes);

    /// <summary>A multiplayer message went out, for the black box.</summary>
    void MessageOut(uint16_t type, size_t bytes);

    /// <summary>The frame times' percentile <paramref name="share"/> (0.5, 0.95...) of a histogram, in milliseconds.</summary>
    double Percentile(std::span<const uint32_t> histogram, double share);

    /// <summary>Whether the law where <paramref name="country"/> is wants consent first, on day <paramref name="day"/>.</summary>
    bool AskFirst(std::string_view country, int64_t day);
}

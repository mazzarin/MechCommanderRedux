#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

// Port-only (the personal build): the updater. An update has two halves (SPEC-install-and-updates.md):
//
// - The program, through a backend the app sets before the game starts (the Mac app's Sparkle,
//   apps/MCRedux/MCSparkleUpdater.mm), which reports each step here.
// - The content, through the game's own updater (update/MCContentUpdater.h), worked on a thread of its own and picked
//   up each frame (Frame). Set (SetContent), it leads: the check reads release.json, holds the program's backend to
//   the build it gives this copy, and plans the content; INSTALL stages the content, then downloads the program, and
//   the content is armed the moment the program is, so READY means both are in and neither goes in without the other.
//   Without it, the program's backend alone finds, downloads and installs its newest version.
//
// The main menu's UPDATE button and page (logistics/MCUpdatePage.h) show where it is and pass on the player's answers:
// nothing downloads until the player chooses INSTALL. With neither half (the tests, a build without one) there is
// nothing to update and the main menu keeps ORIGINAL CINEMA. Everything here happens on the main thread: Sparkle calls
// its driver there, as the frame pumps the window's events. Standard headers only: the Mac's Sparkle backend includes
// it without the precompiled header.

namespace MCContentStage
{
    class MCSource;
}

namespace MCContentUpdater
{
    struct MCSetup;
}

namespace MCReleaseFile
{
    struct MCProgram;
}

namespace MCUpdater
{
    /// <summary>Where the update is.</summary>
    enum class MCStage : uint8_t
    {
        /// <summary>Nothing new (not looked, none found, or skipped).</summary>
        None,
        /// <summary>A newer version is out: the player installs it, skips it, or leaves it for now.</summary>
        Found,
        /// <summary>The player chose to install it: it downloads.</summary>
        Downloading,
        /// <summary>Downloaded: a restart installs it, and so does quitting.</summary>
        Ready,
        /// <summary>The game is quitting so it can be installed; the new version starts by itself.</summary>
        Installing,
        /// <summary>A step failed (<see cref="MCStatus::Error"/> says how); the next start looks again.</summary>
        Failed,
    };

    /// <summary>The player's answer to the update shown, or the updater's own to the program's backend.</summary>
    enum class MCAnswer : uint8_t
    {
        /// <summary>Found: download and install it. Ready: restart to install it now.</summary>
        Install,
        /// <summary>Found: never offer this version again. Ready: install nothing after all (the content couldn't be armed).</summary>
        Skip,
        /// <summary>Found: not now (the content's own updater keeps the player's skip).</summary>
        Later,
    };

    /// <summary>Why HD OPTIONS can't change the HD pack now: an update's work comes first.</summary>
    enum class MCHdHold : uint8_t
    {
        /// <summary>It can change.</summary>
        None,
        /// <summary>The start's check looks for an update.</summary>
        Looking,
        /// <summary>An update downloads.</summary>
        UpdateDownloading,
        /// <summary>An update waits for a restart (it goes in as the game quits).</summary>
        UpdateReady,
        /// <summary>A change of the pack goes in at the next start.</summary>
        NextStart,
    };

    /// <summary>The HD pack as HD OPTIONS shows it: whether the copy has it, and the change under way.</summary>
    struct MCHdPack
    {
        /// <summary>Whether the game manages its content (the content's updater is set); without, nothing to show.</summary>
        bool Managed = false;
        /// <summary>Whether the copy has it installed, and whether the player chose it.</summary>
        bool Installed = false;
        bool Chosen = false;
        /// <summary>Its size at the release installed (0: not known).</summary>
        uint64_t Bytes = 0;
        /// <summary>
        /// The change: none, downloading (<see cref="Received"/> of <see cref="Expected"/>), ready (in or out at the
        /// next start) or failed (<see cref="Error"/>).
        /// </summary>
        MCStage Stage = MCStage::None;
        uint64_t Received = 0;
        uint64_t Expected = 0;
        std::string Error;
        /// <summary>The player changed their mind while it downloaded: it stops, then the new choice is planned.</summary>
        bool Stopping = false;
        /// <summary>The update shown brings the change (it was planned with the choice).</summary>
        bool WithUpdate = false;
        MCHdHold Hold = MCHdHold::None;
    };

    /// <summary>The update as the backend last reported it.</summary>
    struct MCStatus
    {
        MCStage Stage = MCStage::None;
        /// <summary>The newer version, as shown ("0.2.1 (470)").</summary>
        std::string Version;
        /// <summary>What it brings, as plain text (lines apart by newlines).</summary>
        std::string Notes;
        /// <summary>
        /// Downloading: the bytes in so far, and of the whole (0 until known). Found: the download's size, when known
        /// (the content's updater knows it).
        /// </summary>
        uint64_t Received = 0;
        uint64_t Expected = 0;
        /// <summary>Failed: what went wrong.</summary>
        std::string Error;
    };

    /// <summary>What the app gives the updater: the version running, the check and the answers.</summary>
    class MCBackend
    {
    public:
        virtual ~MCBackend() = default;

        /// <summary>The version running, as shown ("0.2.1 (468)").</summary>
        virtual std::string RunningVersion() = 0;

        /// <summary>
        /// Looks for a newer version: build <paramref name="build"/> (release.json's for this copy), or with none its
        /// own newest. What it finds comes later, through the reports below; the build not found, it closes.
        /// </summary>
        virtual void Check(std::optional<uint64_t> build) = 0;

        /// <summary>Passes on the player's answer to the update shown (its stage: <see cref="Status"/>).</summary>
        virtual void Answer(MCAnswer answer) = 0;

        /// <summary>
        /// The program release.json gives this copy, told just before <see cref="Check"/> looks for its build: a
        /// backend that installs the program itself (Windows', update/MCSetupBackend.h) fetches it from here; Sparkle
        /// reads its own feed.
        /// </summary>
        virtual void Offered([[maybe_unused]] const MCReleaseFile::MCProgram& program) {}

        /// <summary>Each frame, on the main thread: a backend working on a thread of its own reports its steps here.</summary>
        virtual void Frame() {}

        /// <summary>The game is quitting: an update ready to install goes in as it ends (Sparkle sees to its own).</summary>
        virtual void Quit() {}
    };

    /// <summary>The app's backend (before the game starts); none takes it away.</summary>
    void SetBackend(std::unique_ptr<MCBackend> backend);

    /// <summary>A transfer to the release's host, made for each piece of the content's work, on that work's thread.</summary>
    using MCSourceMaker = std::function<std::unique_ptr<MCContentStage::MCSource>()>;

    /// <summary>
    /// The game's own updater for the content, for the copy <paramref name="setup"/> describes, reading the release's
    /// host through what <paramref name="source"/> makes (before the game starts).
    /// </summary>
    void SetContent(const MCContentUpdater::MCSetup& setup, MCSourceMaker source);

    /// <summary>No content updater: any work under way stops.</summary>
    void ClearContent();

    /// <summary>How RESTART quits the game for an update with no program (none: the input's quit; the tests count it).</summary>
    void SetQuit(std::function<void()> quit);

    /// <summary>Whether the content's work runs: a check, or staging (the tests wait on it).</summary>
    bool Working();

    /// <summary>Picks up the content's work (each frame): its progress, and its end.</summary>
    void Frame();

    /// <summary>The game is quitting: work under way stops (staged files stay for the next try).</summary>
    void Quit();

    /// <summary>Whether RESTART asked for the game to start again once it has quit (an update with no program).</summary>
    bool RestartWanted();

    /// <summary>Whether this build updates itself (it has a backend, or the content's updater).</summary>
    bool Available();

    /// <summary>The HD pack (HD OPTIONS' HD PACK row).</summary>
    const MCHdPack& HdPack();

    /// <summary>
    /// The player chose to have the HD pack or not (HD OPTIONS' GET, REMOVE and STOP): kept as the copy's choice, then
    /// planned against the release installed, downloaded while the game plays, and in or out at the next start (RESTART,
    /// or quitting). The same choice again after a failure tries again. With an update shown,
    /// the update is planned again with it; changed while it downloads, the download stops for the new choice. A
    /// change made while an update's work comes first goes in at a later start.
    /// </summary>
    void ChooseHd(bool chosen);

    /// <summary>RESTART on HD OPTIONS: with the HD pack's change ready, the game quits and starts again, putting it in.</summary>
    void RestartForHd();

    /// <summary>The version running, as shown, or empty without a backend.</summary>
    std::string RunningVersion();

    /// <summary>The game started: looks for a newer version (once a run).</summary>
    void CheckAtStart();

    /// <summary>The update as last reported.</summary>
    const MCStatus& Status();

    /// <summary>Whether an update waits for the player: found, downloading, ready or failed (the UPDATE button lit).</summary>
    bool Waiting();

    /// <summary>The player's answer (from the UPDATE page), passed to the backend.</summary>
    void Answer(MCAnswer answer);

    /// <summary>Called after each change of <see cref="Status"/> (the UPDATE button and page); none to stop.</summary>
    void SetListener(std::function<void()> listener);

    /// <summary>The backend's reports: a newer version is out, as shown, with what it brings.</summary>
    void Found(std::string version, std::string notes);

    /// <summary>What the update found brings, when the feed links it instead of holding it.</summary>
    void ReleaseNotes(std::string notes);

    /// <summary>The download's progress: <paramref name="received"/> bytes of <paramref name="expected"/> (0: not known).</summary>
    void Downloading(uint64_t received, uint64_t expected);

    /// <summary>Downloaded and unpacked: ready to install.</summary>
    void Ready();

    /// <summary>The game is to quit so the update installs.</summary>
    void Installing();

    /// <summary>A step failed: <paramref name="error"/> says how (the check itself failing is only logged).</summary>
    void Failed(std::string error);

    /// <summary>
    /// The backend's session ended: an update found and not downloaded goes (none found, or skipped); one ready or
    /// installing stays, as it installs when the game quits, and so does a failure.
    /// </summary>
    void Closed();
}

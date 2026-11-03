#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// Port-only (the personal build): the updater. A backend the app sets before the game starts (the Mac app's Sparkle,
// apps/MCRedux/MCSparkleUpdater.mm) looks for a newer version when the game starts and reports each step here; the
// main menu's UPDATE button and page (logistics/MCUpdatePage.h) show where it is and pass on the player's answers:
// nothing downloads until the player chooses INSTALL. Without a backend (the tests, a build without one) there is nothing to update and the main menu
// keeps ORIGINAL CINEMA. Everything happens on the main thread: Sparkle calls its driver there, as the frame pumps the
// window's events.

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

    /// <summary>The player's answer to the update shown.</summary>
    enum class MCAnswer : uint8_t
    {
        /// <summary>Found: download and install it. Ready: restart to install it now.</summary>
        Install,
        /// <summary>Found: never offer this version again.</summary>
        Skip,
    };

    /// <summary>The update as the backend last reported it.</summary>
    struct MCStatus
    {
        MCStage Stage = MCStage::None;
        /// <summary>The newer version, as shown ("0.2.1 (470)").</summary>
        std::string Version;
        /// <summary>What it brings, as plain text (lines apart by newlines).</summary>
        std::string Notes;
        /// <summary>Downloading: the bytes in so far, and of the whole (0 until known).</summary>
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

        /// <summary>Looks for a newer version; what it finds comes later, through the reports below.</summary>
        virtual void Check() = 0;

        /// <summary>Passes on the player's answer to the update shown (its stage: <see cref="Status"/>).</summary>
        virtual void Answer(MCAnswer answer) = 0;
    };

    /// <summary>The app's backend (before the game starts); none takes it away.</summary>
    void SetBackend(std::unique_ptr<MCBackend> backend);

    /// <summary>Whether this build updates itself (it has a backend).</summary>
    bool Available();

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

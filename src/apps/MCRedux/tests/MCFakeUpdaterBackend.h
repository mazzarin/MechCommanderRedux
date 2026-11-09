#pragma once

#include "main/MCUpdater.h"

// The tests' stand-in for the program's updater backend (the Mac app's Sparkle, main/MCUpdater.h's MCBackend): it keeps
// what it's asked, and the test reports its steps (MCUpdater::Found, Ready, Failed and the like) as the backend would.

namespace MCTestUpdater
{
    /// <summary>The version the stand-in says runs, unless a test gives it another.</summary>
    inline constexpr std::string_view FakeRunningVersion = "0.2.1 (468)";

    /// <summary>What the stand-in backend was asked.</summary>
    struct MCBackendLog
    {
        /// <summary>The build each check looked for (none: the backend's own newest).</summary>
        std::vector<std::optional<uint64_t>> Checks;
        std::vector<MCUpdater::MCAnswer> Answers;
        /// <summary>How the stand-in says its program goes in (Sparkle's: as the game quits).</summary>
        MCUpdater::MCGoesIn GoesIn = MCUpdater::MCGoesIn::AsItQuits;
    };

    /// <summary>A stand-in for Sparkle: it keeps the checks and the answers in its log.</summary>
    class MCFakeBackend final : public MCUpdater::MCBackend
    {
    public:
        MCFakeBackend(std::shared_ptr<MCBackendLog> log, std::string_view running)
            : _Log(std::move(log)), _Running(running)
        {
        }

        std::string RunningVersion() override { return _Running; }

        void Check(std::optional<uint64_t> build) override { _Log->Checks.push_back(build); }

        void Answer(MCUpdater::MCAnswer answer) override { _Log->Answers.push_back(answer); }

        MCUpdater::MCGoesIn GoesIn() override { return _Log->GoesIn; }

    private:
        std::shared_ptr<MCBackendLog> _Log;
        std::string _Running;
    };

    /// <summary>Sets a stand-in backend, running <paramref name="running"/>, for the test's length, then takes it away.</summary>
    class MCFakeUpdater
    {
    public:
        explicit MCFakeUpdater(std::string_view running = FakeRunningVersion)
        {
            MCUpdater::SetBackend(std::make_unique<MCFakeBackend>(Log, running));
        }

        ~MCFakeUpdater() { MCUpdater::SetBackend(nullptr); }
        MCFakeUpdater(const MCFakeUpdater&) = delete;
        MCFakeUpdater& operator=(const MCFakeUpdater&) = delete;

        std::shared_ptr<MCBackendLog> Log = std::make_shared<MCBackendLog>();
    };
}

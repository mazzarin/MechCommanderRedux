#include "stdafx.h"
#include "main/MCUpdater.h"

namespace
{
    /// <summary>
    /// The most a version and its notes may hold: they come from the feed, outside the game, so a broken one stays
    /// small.
    /// </summary>
    constexpr size_t MaxVersionLength = 40;
    constexpr size_t MaxNotesLength = 8192;
    constexpr size_t MaxErrorLength = 300;

    /// <summary>
    /// <paramref name="text"/> with only what the game's fonts write (printable ASCII, and newlines when
    /// <paramref name="newlines"/>; a tab is a space), cut at <paramref name="maxLength"/>.
    /// </summary>
    std::string Printable(std::string_view text, size_t maxLength, bool newlines)
    {
        std::string kept;

        for (const char c : text)
        {
            if (kept.size() >= maxLength)
            {
                break;
            }

            if (c == '\t')
            {
                kept += ' ';
            }
            else if ((c == '\n' && newlines) || (c >= ' ' && c <= '~'))
            {
                kept += c;
            }
        }

        return kept;
    }

    std::unique_ptr<MCUpdater::MCBackend> Backend;
    MCUpdater::MCStatus Current;
    std::function<void()> Listener;
    /// <summary>The start's check was made: once a run.</summary>
    bool CheckedAtStart = false;

    /// <summary>Tells the listener <see cref="Current"/> changed.</summary>
    void Changed()
    {
        if (Listener)
        {
            Listener();
        }
    }
}

auto MCUpdater::SetBackend(std::unique_ptr<MCBackend> backend) -> void
{
    Backend = std::move(backend);
    Current = {};
    CheckedAtStart = false;
    Changed();
}

auto MCUpdater::Available() -> bool
{
    return Backend != nullptr;
}

auto MCUpdater::RunningVersion() -> std::string
{
    return Backend != nullptr ? Backend->RunningVersion() : std::string();
}

auto MCUpdater::CheckAtStart() -> void
{
    if (Backend == nullptr || CheckedAtStart)
    {
        return;
    }

    CheckedAtStart = true;
    SDL_Log("Updater: looking for a newer version than %s", Backend->RunningVersion().c_str());
    Backend->Check();
}

auto MCUpdater::Status() -> const MCStatus&
{
    return Current;
}

auto MCUpdater::Waiting() -> bool
{
    return Current.Stage != MCStage::None;
}

auto MCUpdater::Answer(MCAnswer answer) -> void
{
    if (Backend == nullptr)
    {
        return;
    }

    const MCStage stage = Current.Stage;
    SDL_Log("Updater: %s at %s", answer == MCAnswer::Install ? "install" : "skip", Current.Version.c_str());

    // The answer shows at once; the backend's reports follow (the download's progress, or nothing for a skip).
    if (answer == MCAnswer::Skip && stage == MCStage::Found)
    {
        Current = {};
        Changed();
    }
    else if (answer == MCAnswer::Install && stage == MCStage::Found)
    {
        Current.Stage = MCStage::Downloading;
        Current.Received = 0;
        Current.Expected = 0;
        Changed();
    }
    else if (answer == MCAnswer::Install && stage == MCStage::Ready)
    {
        Current.Stage = MCStage::Installing;
        Changed();
    }
    else
    {
        return;
    }

    Backend->Answer(answer);
}

auto MCUpdater::SetListener(std::function<void()> listener) -> void
{
    Listener = std::move(listener);
}

auto MCUpdater::Found(std::string version, std::string notes) -> void
{
    Current = {.Stage = MCStage::Found,
               .Version = Printable(version, MaxVersionLength, false),
               .Notes = Printable(notes, MaxNotesLength, true)};
    SDL_Log("Updater: %s is out", Current.Version.c_str());
    Changed();
}

auto MCUpdater::ReleaseNotes(std::string notes) -> void
{
    // Notes coming after a skip have no update to go with.
    if (Current.Stage == MCStage::None)
    {
        return;
    }

    Current.Notes = Printable(notes, MaxNotesLength, true);
    Changed();
}

auto MCUpdater::Downloading(uint64_t received, uint64_t expected) -> void
{
    Current.Stage = MCStage::Downloading;
    Current.Received = received;
    Current.Expected = expected;
    Changed();
}

auto MCUpdater::Ready() -> void
{
    SDL_Log("Updater: %s is downloaded", Current.Version.c_str());
    Current.Stage = MCStage::Ready;
    Changed();
}

auto MCUpdater::Installing() -> void
{
    Current.Stage = MCStage::Installing;
    Changed();
}

auto MCUpdater::Failed(std::string error) -> void
{
    // The check itself failing (offline, say) is no news for the player: the next start looks again.
    if (Current.Stage == MCStage::None)
    {
        SDL_Log("Updater: the check failed: %s", error.c_str());
        return;
    }

    SDL_Log("Updater: %s failed: %s", Current.Version.c_str(), error.c_str());
    Current.Stage = MCStage::Failed;
    Current.Error = Printable(error, MaxErrorLength, false);
    Changed();
}

auto MCUpdater::Closed() -> void
{
    // Only an update not yet downloaded goes: one ready or installing installs when the game quits (Sparkle ends its
    // session once the install is armed), and a failure stays shown until the next start.
    if (Current.Stage != MCStage::Found && Current.Stage != MCStage::Downloading)
    {
        return;
    }

    Current = {};
    Changed();
}

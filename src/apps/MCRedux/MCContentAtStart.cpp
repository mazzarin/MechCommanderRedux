#include "stdafx.h"
#include "MCContentAtStart.h"
#include "gui/MCGuiStartup.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentSwap.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateLock.h"

namespace
{
    using MCContentAtStart::MCOutcome;
    using MCContentAtStart::MCStart;

    /// <summary>The recovery screen, for an update that stopped partway (<see cref="Recover"/>), and REPAIR's window.</summary>
    constexpr std::string_view RecoveryHeading = "THE UPDATE STOPPED PARTWAY.";
    constexpr std::string_view RecoveryStep = "CLOSE ANY PROGRAM USING THE GAME'S FOLDER, THEN TRY AGAIN.";
    constexpr std::string_view RepairStep = "REPAIR DOWNLOADS ANY FILE THAT'S WRONG OR MISSING.";
    constexpr std::string_view RepairButton = "REPAIR";
    constexpr std::string_view RepairHeading = "REPAIRING THE GAME";

    /// <summary>The content downloaded, but another copy of the game holds the folder, so it can't go in yet.</summary>
    constexpr std::string_view HeldHeading = "THE GAME'S FILES ARE DOWNLOADED.";
    constexpr std::string_view HeldText = "ANOTHER COPY OF THE GAME IS RUNNING. CLOSE IT, THEN TRY AGAIN.";

    /// <summary>A step that gets the content from the release's host: the first start's install, or a repair.</summary>
    using MCContentStep = std::expected<void, MCUpdateWork::MCFailure> (*)(const MCContentUpdater::MCSetup&,
                                                                           MCUpdateDisk&, MCContentStage::MCSource&,
                                                                           const MCUpdateWork::MCProgressCallback&);

    /// <summary>
    /// The window's work (MCFirstStart.h): <paramref name="step"/> on the release's host, each byte from it counted for
    /// the window's speed.
    /// </summary>
    MCFirstStart::MCWork ContentWork(const MCStart& start, MCContentStep step)
    {
        // Its own copy of the setup, on the work's thread; its transfer is cancelled when the window is closed.
        return [setup = *start.Content, maker = start.Source,
                step](MCFirstStart::MCProgress& progress,
                      std::stop_token stop) -> std::expected<void, MCUpdateWork::MCFailure>
        {
            const MCUpdateWork::MCWorkSource source(MCUpdateWork::Counted(maker, progress), stop);

            const MCOptionalRef<MCContentStage::MCSource> transfer = source.Get();

            if (!transfer)
            {
                return std::unexpected(MCUpdateWork::MCFailure{.Reason = std::string(MCUpdateWork::NoSourceError)});
            }

            return step(setup, *MakeDisk(), transfer->get(), MCUpdateWork::Reporter(progress, stop));
        };
    }

    /// <summary><paramref name="text"/>'s ASCII letters upper-cased, as the game's menus letter their words.</summary>
    std::string Lettered(std::string text)
    {
        for (char& letter : text)
        {
            if (letter >= 'a' && letter <= 'z')
            {
                letter = static_cast<char>(letter - 'a' + 'A');
            }
        }

        return text;
    }

    /// <summary>
    /// The recovery screen (SPEC-install-and-updates.md's If it can't finish): an update stopped partway that can
    /// neither finish nor go back, or a repair not done. TRY AGAIN swaps again; REPAIR, in a build that updates its
    /// content, gives the update up and installs the release over the files as they are, under the game's lock held
    /// exclusive; QUIT ends the game. It asks again while the files can't be played. While it asks, the game holds no
    /// lock, so another copy may finish the update meanwhile: each answer takes the lock again and looks at the folder as
    /// it is then.
    /// </summary>
    /// <returns>Whether the game can play the files now.</returns>
    bool Recover(const MCStart& start, std::string problem, MCOptionalRef<MCUpdateLock> lock)
    {
        std::vector<std::string_view> buttons{MCFirstStart::TryAgainButton};

        if (start.Content)
        {
            buttons.push_back(RepairButton);
        }

        buttons.push_back(MCFirstStart::QuitButton);

        for (;;)
        {
            // The player may take long to answer: the lock isn't held meanwhile (the editor isn't kept waiting).
            if (lock)
            {
                lock->get().Release();
            }

            SDL_Log("Updates: the game's files can't be played (%s): the recovery screen asks", problem.c_str());
            const std::string text = start.Content
                                         ? std::format("{}.\n\n{} {}", Lettered(problem), RecoveryStep, RepairStep)
                                         : std::format("{}.\n\n{}", Lettered(problem), RecoveryStep);
            const std::optional<size_t> answer = MCFirstStart::Ask(RecoveryHeading, text, buttons, start.Window);

            if (!answer || buttons[*answer] == MCFirstStart::QuitButton)
            {
                return false;
            }

            if (buttons[*answer] == RepairButton)
            {
                if (lock && !lock->get().TryExclusive())
                {
                    problem = "another copy of the game is running";
                    continue;
                }

                // Another copy may have finished the update while the player chose: then there's nothing to repair.
                const auto underWay = MCContentSwap::UnderWay(*MakeDisk(), start.GameFolder);

                if (!underWay)
                {
                    problem = underWay.error();
                    continue;
                }

                if (*underWay)
                {
                    const auto repaired =
                        MCFirstStart::Run(RepairHeading, ContentWork(start, MCContentUpdater::Repair), start.Window);

                    if (!repaired)
                    {
                        problem = repaired.error().Reason;
                        continue;
                    }

                    if (!*repaired)
                    {
                        return false;
                    }
                }
            }

            // The swap again, the lock taken exclusive (held already after a repair), then held shared to play.
            const MCContentSwap::MCResult result =
                MCContentSwap::RunAtStart(*MakeDisk(), start.GameFolder, lock, start.RunningBuild);

            if (result.Outcome != MCContentSwap::MCOutcome::Stuck)
            {
                SDL_Log("Updates: the game's files can be played again");
                return true;
            }

            problem = result.Error;
        }
    }

    /// <summary>Whether the start must still get <paramref name="record"/>'s copy's content (MCInstalled::AtStart).</summary>
    bool StillNeeded(const MCStart& start, const MCInstalled::MCRecord& record)
    {
        return MCInstalled::AtStart(*MakeDisk(), start.GameFolder, record, start.RunningBuild,
                                    start.LowestContentRelease) == MCInstalled::MCStartNeed::Needed;
    }
}

bool MCContentAtStart::SwapStaged(const MCStart& start, std::string_view commandLine, MCOptionalRef<MCUpdateLock> lock)
{
    std::optional<std::chrono::steady_clock::time_point> deadline;

    if (StartsInLobby(commandLine))
    {
        deadline = std::chrono::steady_clock::now() + MCContentSwap::LobbyStartTime;
    }

    const MCContentSwap::MCResult result =
        MCContentSwap::RunAtStart(*MakeDisk(), start.GameFolder, lock, start.RunningBuild, deadline);

    switch (result.Outcome)
    {
        case MCContentSwap::MCOutcome::Nothing:
            return true;
        case MCContentSwap::MCOutcome::Swapped:
        {
            SDL_Log("Updates: the staged update is in place");
            return true;
        }
        case MCContentSwap::MCOutcome::PutBack:
        {
            SDL_Log("Updates: the staged update waits for the next start (%s)", result.Error.c_str());
            return true;
        }
        case MCContentSwap::MCOutcome::Disarmed:
        {
            SDL_Log("Updates: the staged update waits for its program (%s): the previous files play",
                    result.Error.c_str());
            return true;
        }
        case MCContentSwap::MCOutcome::Stuck:
            return Recover(start, result.Error, lock);
    }

    return true;
}

MCOutcome MCContentAtStart::GetAtStart(const MCStart& start, std::string_view commandLine,
                                       MCOptionalRef<MCUpdateLock> lock, MCInstalled::MCStartNeed need, bool askAlways,
                                       MCInstalled::MCRecord& record)
{
    if (!start.Content)
    {
        return MCOutcome::Play;
    }

    const bool needed = need == MCInstalled::MCStartNeed::Needed;
    SDL_Log("Updates: the start gets the game's content%s", needed ? "" : ", which its program was installed for");
    const MCFirstStart::MCWork work = ContentWork(start, MCContentUpdater::Install);

    // Content only due that can't come, or whose window the player closed, leaves the files to play as they are;
    // content needed (or asked for) ends the start then, and the next one carries on.
    if (needed || askAlways)
    {
        const std::expected<bool, std::string> done = MCFirstStart::RunAsking(GettingHeading, work, start.Window);

        if (!done)
        {
            SDL_Log("Updates: the game's content can't come: %s", done.error().c_str());
            return MCOutcome::Failed;
        }

        if (!*done)
        {
            return MCOutcome::Quit;
        }
    }
    else if (const auto done = MCFirstStart::Run(GettingHeading, work, start.Window); !done || !*done)
    {
        SDL_Log("Updates: the content due waits for a later start (%s): the files play as they are",
                done ? "the window was closed" : done.error().Reason.c_str());
        return MCOutcome::Play;
    }

    for (;;)
    {
        if (!SwapStaged(start, commandLine, lock))
        {
            return MCOutcome::Failed;
        }

        auto swapped = MCInstalled::Read(*MakeDisk(), start.GameFolder);

        if (!swapped)
        {
            SDL_Log("Updates: %s", swapped.error().c_str());
            return needed ? MCOutcome::Failed : MCOutcome::Play;
        }

        record = std::move(*swapped);

        // Put back (another copy of the game holds the folder): it goes in once that copy is closed.
        if (!needed || !StillNeeded(start, record))
        {
            return MCOutcome::Play;
        }

        SDL_Log("Updates: the content is downloaded, but another copy of the game holds the folder");
        constexpr std::array<std::string_view, 2> Buttons = {MCFirstStart::TryAgainButton, MCFirstStart::QuitButton};
        const std::optional<size_t> answer = MCFirstStart::Ask(HeldHeading, HeldText, Buttons, start.Window);

        if (!answer || Buttons[*answer] == MCFirstStart::QuitButton)
        {
            return MCOutcome::Quit;
        }
    }
}

int MCContentAtStart::GetContentOnly(const MCStart& start, MCOptionalRef<MCUpdateLock> lock)
{
    if (!start.Content)
    {
        SDL_Log("Getting the content: this build updates no content, so there's none to get");
        return 0;
    }

    if (!SwapStaged(start, {}, lock))
    {
        return 1;
    }

    auto installed = MCInstalled::Read(*MakeDisk(), start.GameFolder);

    if (!installed)
    {
        SDL_Log("Getting the content: %s", installed.error().c_str());
        return 1;
    }

    const MCInstalled::MCStartNeed need =
        MCInstalled::AtStart(*MakeDisk(), start.GameFolder, *installed, start.RunningBuild, start.LowestContentRelease);

    if (need == MCInstalled::MCStartNeed::Nothing)
    {
        SDL_Log("Getting the content: %s has what it needs", MCFileSystem::ToUtf8(start.GameFolder).c_str());
        return 0;
    }

    const MCOutcome outcome = GetAtStart(start, {}, lock, need, true, *installed);
    SDL_Log("Getting the content: %s",
            outcome == MCOutcome::Play ? "in place" : "not in place; the next start carries on");
    return outcome == MCOutcome::Play ? 0 : 1;
}

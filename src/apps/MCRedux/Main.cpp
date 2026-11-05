#include "stdafx.h"
#include "MCConsole.h"
#include "MCCrashTrace.h"
#include "MCFirstStart.h"
#include "MCGameFolder.h"
#include "MCHttpGet.h"
#include "MCRestart.h"
#include "MCTelemetryBackend.h"
#include "MCVersion.h"
#include "abl/MCAblRuntime.h"
#include "gui/MCGuiStartup.h"
#include "main/MCHdSettings.h"
#include "main/MCSystemConfig.h"
#include "main/MCUpdater.h"
#include "network/MCRejoinNote.h"
#include "platform/MCAllocator.h"
#include "platform/MCFileSystem.h"
#include "sprite/MCWeatherPace.h"
#include "update/MCContentRemoval.h"
#include "update/MCContentSwap.h"
#include "update/MCContentUpdater.h"
#include "update/MCHttpSource.h"
#include "update/MCSignedFile.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateLock.h"

#if defined(__APPLE__)
#include "MCSparkleUpdater.h"
#elif defined(_WIN32)
#include "MCSetupRun.h"
#include "update/MCSetupBackend.h"
#endif

namespace
{
    /// <summary>What the command line asks for: the two folders and the words passed on to the game.</summary>
    struct MCLaunchOptions
    {
        std::filesystem::path DataFolder;
        std::filesystem::path UserFolder;
        std::string GameCommandLine;
        /// <summary>--remove-content: the content goes, and the game doesn't start (the Windows uninstaller's).</summary>
        bool RemoveContent = false;
    };

    /// <summary>
    /// The release's host for the game's own updater: the downloads Worker (tools/cloudflare/downloads in the
    /// workspace), laid out as the release script writes it (SPEC-install-and-updates.md's Hosting).
    /// </summary>
    constexpr std::string_view ContentHost = "https://mechcommander-redux-downloads.mazzarin.workers.dev";

    /// <summary>
    /// The root keys' public halves (base64), one of which signs the key list (SPEC-install-and-updates.md's Signing
    /// keys). None yet: until the root key is made (tools/release/make_release.py new-key in the workspace), the game
    /// updates no content and takes no lock, and the Mac's Sparkle updates the program alone.
    /// </summary>
    constexpr std::array<std::string_view, 0> RootKeys = {};

    /// <summary>What the first start's window says while the game's content downloads.</summary>
    constexpr std::string_view FirstStartHeading = "GETTING THE GAME READY";

    /// <summary>The recovery screen, for an update that stopped partway (<see cref="Recover"/>), and REPAIR's window.</summary>
    constexpr std::string_view RecoveryHeading = "THE UPDATE STOPPED PARTWAY.";
    constexpr std::string_view RecoveryStep = "CLOSE ANY PROGRAM USING THE GAME'S FOLDER, THEN TRY AGAIN.";
    constexpr std::string_view RepairStep = "REPAIR DOWNLOADS ANY FILE THAT'S WRONG OR MISSING.";
    constexpr std::string_view TryAgainButton = "TRY AGAIN";
    constexpr std::string_view RepairButton = "REPAIR";
    constexpr std::string_view QuitButton = "QUIT";
    constexpr std::string_view RepairHeading = "REPAIRING THE GAME";

    /// <summary>This platform's name in release.json.</summary>
#ifdef _WIN32
    constexpr std::string_view ReleasePlatform = "windows";
#else
    constexpr std::string_view ReleasePlatform = "mac";
#endif

    /// <summary>The command line's help text.</summary>
    constexpr std::string_view UsageText =
        "MechCommander Redux " MC_VERSION_SHOWN ", commit " MC_VERSION_COMMIT "\n\n"
        "MCRedux [-d|--data <install folder>] [--user <save folder>] [game options]\n\n"
        "  -d, --data     - The MechCommander Gold install; without it, or when it has no\n"
        "                   SYSTEM.CFG, the current folder, then the executable's folder;\n"
        "                   without any, the folder chosen last time or found, else ask.\n"
        "  --user         - Where saves, prefs and temp files go (default: the SDL pref path)\n"
        "  --remove-content - Remove the game's downloaded files from the install (or the\n"
        "                   --data folder), leaving the player's own, and quit\n"
        "                   game options, as MCX.EXE takes them.\n"
        "  -mission <n>   - Start game segment n of SYSTEM.CFG's missionName (skips logistics)\n"
        "  -load <file>   - Load a saved game\n"
        "  -host [port]   - Host a multiplayer game (default port 28800)\n"
        "  -join <addr>   - Join a multiplayer game at host[:port]\n"
        "  -player <name> - Player callsign / name\n"
        "  -session <name>- Session / room name\n"
        "  -maxplayers <n>- Maximum players for host (2..6)\n"
        "  -port <port>   - Network port (default 28800)\n";

    /// <summary>
    /// Shows <paramref name="text"/> to the player: on the terminal MCRedux was started from, else in a message box.
    /// </summary>
    void Report(const std::string& text, bool isError)
    {
        if (MCConsole::HasOutput())
        {
            (isError ? std::cerr : std::cout) << text << "\n";
            return;
        }

        SDL_ShowSimpleMessageBox(isError ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, "MCRedux", text.c_str(),
                                 nullptr);
    }

    /// <summary>
    /// Whether <paramref name="folder"/> awaits its first install (an installer prepared it, and the game downloads its
    /// content): only in a build that updates its content (<paramref name="contentUpdates"/>).
    /// </summary>
    bool AwaitsInstall(const std::filesystem::path& folder, bool contentUpdates)
    {
        return contentUpdates && !folder.empty() && MCContentUpdater::AwaitsInstall(*MakeDisk(), folder);
    }

    /// <summary>
    /// The game's folder: the first of the --data folder (when given), the current folder and the executable's folder
    /// that holds the game's data. Without --data, then the folder an installer prepared for its first start (in a
    /// build that updates its content, <paramref name="contentUpdates"/>), the folder chosen last time, the one copy in
    /// the usual places (MCGameFolder), or the player's choice, remembered for next time. Empty when the player quits;
    /// an error naming every folder tried when --data names no game (a script gets no questions).
    /// </summary>
    std::expected<std::filesystem::path, std::string> FindGameData(const std::filesystem::path& dataArgument,
                                                                   const std::filesystem::path& userFolder,
                                                                   bool contentUpdates)
    {
        std::vector<std::filesystem::path> candidates;

        if (!dataArgument.empty())
        {
            candidates.push_back(dataArgument);
        }

        std::error_code error;
        candidates.push_back(std::filesystem::current_path(error));

        // SDL keeps the base path (null when it can't tell).
        if (SDL_GetBasePath() != nullptr)
        {
            candidates.push_back(MCFileSystem::FromUtf8(SDL_GetBasePath()));
        }

        std::string tried;

        for (const std::filesystem::path& folder : candidates)
        {
            if (!folder.empty() && (MCGameFolder::HasGameData(folder) || AwaitsInstall(folder, contentUpdates)))
            {
                return folder;
            }

            tried += std::format("\n  {}", MCFileSystem::ToUtf8(folder));
        }

        if (!dataArgument.empty())
        {
            return std::unexpected(std::format("No SYSTEM.CFG found in:{}\nRun MCRedux from the MechCommander Gold "
                                               "install, put it beside the install, or point -d/--data at it.",
                                               tried));
        }

        // The copy the player just installed is the one they mean to play, whatever was chosen before.
        if (const std::filesystem::path installed = MCGameFolder::InstallFolder();
            AwaitsInstall(installed, contentUpdates))
        {
            if (const std::string error = MCGameFolder::Remember(userFolder, installed); !error.empty())
            {
                SDL_Log("Game folder not remembered: %s", error.c_str());
            }

            return installed;
        }

        const bool choose = MCGameFolder::ChoiceRequested();

        if (const std::filesystem::path remembered = MCGameFolder::Remembered(userFolder);
            !choose && !remembered.empty() && MCGameFolder::HasGameData(remembered))
        {
            return remembered;
        }

        const std::vector<std::filesystem::path> found = MCGameFolder::FindInUsualPlaces();
        std::filesystem::path chosen;

        if (found.size() == 1 && !choose)
        {
            chosen = found.front();
        }
        else
        {
            auto asked = MCGameFolder::Ask(found);

            if (!asked || asked->empty())
            {
                return asked;
            }

            chosen = std::move(*asked);
        }

        // Not remembering only means searching (or asking) again next time.
        if (const std::string error = MCGameFolder::Remember(userFolder, chosen); !error.empty())
        {
            SDL_Log("Game folder not remembered: %s", error.c_str());
        }

        return chosen;
    }

    /// <summary>The launch options in <paramref name="arguments"/> (the program's name first, as main gets them).</summary>
    std::expected<MCLaunchOptions, std::string> ParseArguments(std::span<const std::string_view> arguments)
    {
        MCLaunchOptions options;

        for (size_t i = 1; i < arguments.size(); i++)
        {
            const std::string_view arg = arguments[i];

            if (arg == "--data" || arg == "-d" || arg == "--user")
            {
                if (i + 1 >= arguments.size())
                {
                    return std::unexpected(std::format("{} needs a folder", arg));
                }

                (arg == "--user" ? options.UserFolder : options.DataFolder) = MCFileSystem::FromUtf8(arguments[++i]);
            }
            else if (arg == "--remove-content")
            {
                options.RemoveContent = true;
            }
            else if (arg == "--help" || arg == "-h")
            {
                return std::unexpected(std::string());
            }
            else
            {
                if (!options.GameCommandLine.empty())
                {
                    options.GameCommandLine += ' ';
                }

                const bool hasSpace = arg.find(' ') != std::string_view::npos;

                if (hasSpace)
                {
                    options.GameCommandLine += '"';
                }

                options.GameCommandLine += arg;

                if (hasSpace)
                {
                    options.GameCommandLine += '"';
                }
            }
        }

        if (options.UserFolder.empty())
        {
            const std::unique_ptr<char, decltype(&SDL_free)> prefPath(SDL_GetPrefPath(nullptr, "MechCommander Redux"),
                                                                      &SDL_free);

            if (prefPath == nullptr)
            {
                return std::unexpected(std::format("No save folder: {}", SDL_GetError()));
            }

            options.UserFolder = MCFileSystem::FromUtf8(prefPath.get());
        }

        return options;
    }

    /// <summary>The release's host over https, or none when this build's address is unusable (logged).</summary>
    std::unique_ptr<MCContentStage::MCSource> MakeContentSource()
    {
        auto source = MCHttpSource::Make(ContentHost, MCMakeHttpGet());

        if (!source)
        {
            SDL_Log("Updates: %s", source.error().c_str());
            return nullptr;
        }

        return std::move(*source);
    }

    /// <summary>A step that gets the content from the release's host: the first start's install, or a repair.</summary>
    using MCContentStep = std::expected<void, std::string> (*)(const MCContentUpdater::MCSetup&, MCUpdateDisk&,
                                                               MCContentStage::MCSource&,
                                                               const std::function<bool(uint64_t, uint64_t)>&);

    /// <summary>The window's work (MCFirstStart.h): <paramref name="step"/> on the release's host.</summary>
    MCFirstStart::MCWork ContentWork(MCContentUpdater::MCSetup setup, MCContentStep step)
    {
        // Its own copy of the setup: a window closed mid-download leaves the work to end with the process.
        return [setup = std::move(setup), step](MCFirstStart::MCProgress& progress,
                                                std::stop_token stop) -> std::expected<void, std::string>
        {
            const std::unique_ptr<MCContentStage::MCSource> source = MakeContentSource();

            if (source == nullptr)
            {
                return std::unexpected("the release's host has no usable address in this build");
            }

            return step(setup, *MakeDisk(), *source,
                        [&](uint64_t received, uint64_t expected)
                        {
                            progress.Set(received, expected);
                            return !stop.stop_requested();
                        });
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
    /// content (<paramref name="setup"/>), gives the update up and installs the release over the files as they are,
    /// under the game's lock held exclusive; QUIT ends the game. It asks again while the files can't be played.
    /// </summary>
    /// <returns>Whether the game can play the files now.</returns>
    bool Recover(std::string problem, const std::filesystem::path& gameFolder, MCUpdateLock* lock,
                 const MCContentUpdater::MCSetup* setup)
    {
        std::vector<std::string_view> buttons{TryAgainButton};

        if (setup != nullptr)
        {
            buttons.push_back(RepairButton);
        }

        buttons.push_back(QuitButton);

        for (;;)
        {
            SDL_Log("Updates: the game's files can't be played (%s): the recovery screen asks", problem.c_str());
            const std::string text = setup != nullptr
                                         ? std::format("{}.\n\n{} {}", Lettered(problem), RecoveryStep, RepairStep)
                                         : std::format("{}.\n\n{}", Lettered(problem), RecoveryStep);
            const std::optional<size_t> answer = MCFirstStart::Ask(RecoveryHeading, text, buttons);

            if (!answer || buttons[*answer] == QuitButton)
            {
                return false;
            }

            if (buttons[*answer] == RepairButton)
            {
                if (lock != nullptr && !lock->TryExclusive())
                {
                    problem = "another copy of the game is running";
                    continue;
                }

                const auto repaired = MCFirstStart::Run(RepairHeading, ContentWork(*setup, MCContentUpdater::Repair));

                if (!repaired)
                {
                    problem = repaired.error();
                    continue;
                }

                if (!*repaired)
                {
                    return false;
                }
            }

            const MCContentSwap::MCResult result =
                MCContentSwap::RunAtStart(*MakeDisk(), gameFolder, lock, MC_VERSION_BUILD);

            if (result.Outcome != MCContentSwap::MCOutcome::Stuck)
            {
                SDL_Log("Updates: the game's files can be played again");
                return true;
            }

            problem = result.Error;
        }
    }

    /// <summary>
    /// Puts a staged content update in place, or finishes one a crash stopped (update/MCContentSwap.h), before any of
    /// the game's files opens. A lobby's start gives it <see cref="MCContentSwap::LobbyStartTime"/>, past which the
    /// previous files play and the next start tries again. Files it can't play go to the recovery screen
    /// (<see cref="Recover"/>; REPAIR in a build that updates its content, <paramref name="setup"/>).
    /// </summary>
    /// <returns>False when the game can't play the files as they are (the player quit the recovery screen).</returns>
    bool SwapStagedUpdate(const std::filesystem::path& gameFolder, std::string_view commandLine, MCUpdateLock* lock,
                          const MCContentUpdater::MCSetup* setup)
    {
        std::optional<std::chrono::steady_clock::time_point> deadline;

        if (StartsInLobby(commandLine))
        {
            deadline = std::chrono::steady_clock::now() + MCContentSwap::LobbyStartTime;
        }

        const MCContentSwap::MCResult result =
            MCContentSwap::RunAtStart(*MakeDisk(), gameFolder, lock, MC_VERSION_BUILD, deadline);

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
                return Recover(result.Error, gameFolder, lock, setup);
        }

        return true;
    }

    /// <summary>The root keys the build carries (<see cref="RootKeys"/>).</summary>
    std::vector<MCSignedFile::MCPublicKey> BuildRootKeys()
    {
        std::vector<MCSignedFile::MCPublicKey> keys;

        for (const std::string_view text : RootKeys)
        {
            if (const std::optional<MCSignedFile::MCPublicKey> key = MCSignedFile::KeyFromBase64(text))
            {
                keys.push_back(*key);
            }
            else
            {
                SDL_Log("Updates: the root key \"%.*s\" isn't a key", static_cast<int>(text.size()), text.data());
            }
        }

        return keys;
    }

    /// <summary>The copy and the build, for the game's own updater for the content.</summary>
    MCContentUpdater::MCSetup ContentSetup(const MCLaunchOptions& options,
                                           std::vector<MCSignedFile::MCPublicKey> rootKeys)
    {
        return {.GameFolder = options.DataFolder,
                .UserFolder = options.UserFolder,
                .RootKeys = std::move(rootKeys),
                .Platform = std::string(ReleasePlatform),
                .RunningBuild = MC_VERSION_BUILD,
                .RunningVersion = MC_VERSION_SHOWN};
    }

#ifdef _WIN32
    /// <summary>What Windows does for the program's updates: the setup judged and started (MCSetupRun.h).</summary>
    MCSetupBackend::MCPlatform SetupPlatform()
    {
        return {.Runnable = MCSetupRunnable, .Start = MCStartSetup, .Quit = nullptr};
    }

    /// <summary>The folder a program is hosted in (the fork's releases), over https.</summary>
    std::expected<std::unique_ptr<MCContentStage::MCSource>, std::string> MakeProgramSource(std::string_view folder)
    {
        return MCHttpSource::Make(folder, MCMakeHttpGet());
    }
#endif

    /// <summary>
    /// The first start of a copy an installer prepared: its content downloaded, checked and put in place before
    /// anything opens, a window showing the progress (MCFirstStart.h).
    /// </summary>
    /// <returns>
    /// None to play; else the exit code to quit with: the player closed the window (the next start carries on), or the
    /// content couldn't come or go in place (they're told).
    /// </returns>
    std::optional<int> InstallAtFirstStart(const MCContentUpdater::MCSetup& setup, std::string_view commandLine,
                                           MCUpdateLock* lock)
    {
        SDL_Log("Updates: the first start gets the game's content");
        const auto done = MCFirstStart::Run(FirstStartHeading, ContentWork(setup, MCContentUpdater::Install));

        if (!done)
        {
            Report(std::format("The game's files couldn't be downloaded: {}.\n\nStart the game again to carry on.",
                               done.error()),
                   true);
            return 1;
        }

        if (!*done)
        {
            return 0;
        }

        if (!SwapStagedUpdate(setup.GameFolder, commandLine, lock, &setup))
        {
            return 1;
        }

        // Put back (another copy of the game holds the folder): the files go in at the next start.
        if (MCContentUpdater::AwaitsInstall(*MakeDisk(), setup.GameFolder))
        {
            Report("The game's files are downloaded. Close any other copy of the game, then start it again.", true);
            return 1;
        }

        return std::nullopt;
    }

    /// <summary>
    /// --remove-content: the game's content goes from the install folder, or the --data folder (update/MCContentRemoval.h).
    /// The uninstaller runs it hidden, so it asks nothing and tells only a terminal it was started from.
    /// </summary>
    /// <returns>The exit code: 0 once removed.</returns>
    int RemoveContent(const MCLaunchOptions& options)
    {
        const std::filesystem::path folder =
            options.DataFolder.empty() ? MCGameFolder::InstallFolder() : options.DataFolder;

        if (folder.empty())
        {
            SDL_Log("Removing the content: no install folder");
            return 1;
        }

        const auto removed = MCContentRemoval::Remove(*MakeDisk(), folder);
        const std::string told = removed
                                     ? std::format("Removed {} of the game's files from {}; {} left as the player's",
                                                   removed->Removed, MCFileSystem::ToUtf8(folder), removed->Kept)
                                     : std::format("The game's files weren't all removed from {}: {}",
                                                   MCFileSystem::ToUtf8(folder), removed.error());
        SDL_Log("%s", told.c_str());

        if (MCConsole::HasOutput())
        {
            (removed ? std::cout : std::cerr) << told << "\n";
        }

        return removed ? 0 : 1;
    }

    /// <summary>Crash reporter: where the ABL interpreter was (module, routine, source line, code position).</summary>
    void ReportAblState()
    {
        const std::shared_ptr<MCAblRuntime> abl = AblRuntime();

        if (abl == nullptr)
        {
            std::fprintf(stderr, "ABL: not running\n");
            return;
        }

        const std::shared_ptr<MCAblModule> module = abl->CurrentModule();
        std::fprintf(stderr, "ABL: module %s (handle %d)\n", module != nullptr ? module->Name().c_str() : "(none)",
                     abl->CurrentModuleHandle());

        if (const MCAblSymbolId routineId = abl->CurrentRoutine(); routineId != MCAblSymbolId::None)
        {
            const MCAblSymbol& routine = abl->Symbol(routineId);
            std::fprintf(stderr, "ABL: routine %s, code segment %d, code position +%d, statement start +%d\n",
                         routine.Name.c_str(), std::to_underlying(routine.Defn.Info.Routine.CodeSegment),
                         abl->CodePosition(), abl->StatementStart());
        }

        std::fprintf(stderr, "ABL: line %d of %s, statement %d, call depth %d\n", abl->LineNumber(),
                     module != nullptr ? std::string(abl->FileName()).c_str() : "?", abl->StatementCount(),
                     abl->CallDepth());
    }
}

#ifdef _WIN32
// Windows hands main the command line in the ANSI code page, which can't spell every folder; wmain gets it whole.
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    MCConsole::AttachParent();

    if (const auto allocator = MCInitializeAllocator(); !allocator)
    {
        Report(allocator.error(), true);
        return 1;
    }

    MCCrashTrace::Install();
    MCCrashTrace::SetReporter(ReportAblState);
    std::vector<std::string> utf8Arguments;

    for (int i = 0; i < argc; i++)
    {
        // A path converts the wide argument to UTF-8; on other systems it is UTF-8 already.
        utf8Arguments.push_back(MCFileSystem::ToUtf8(std::filesystem::path(argv[i])));
    }

    const std::vector<std::string_view> arguments(utf8Arguments.begin(), utf8Arguments.end());
    auto options = ParseArguments(arguments);

    if (!options)
    {
        if (options.error().empty())
        {
            Report(std::string(UsageText), false);
            return 0;
        }

        // On a terminal the usage follows the error; a message box only needs the error.
        Report(MCConsole::HasOutput() ? std::format("{}\n\n{}", options.error(), UsageText) : options.error(), true);
        return 1;
    }

    if (options->RemoveContent)
    {
        return RemoveContent(*options);
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
    {
        Report(std::format("SDL_Init failed: {}", SDL_GetError()), true);
        return 1;
    }

    // A start with no command line after a lobby's game restarted into an update (Sparkle starts the new version with
    // none): the rejoin note's words, so it joins or hosts the room again (network/MCRejoinNote.h).
    if (options->GameCommandLine.empty())
    {
        if (std::optional<std::string> rejoin =
                MCRejoinNote::TakeAtStart(options->UserFolder, MC_VERSION_BUILD, MCRejoinNote::Now()))
        {
            options->GameCommandLine = std::move(*rejoin);
        }
    }
    else if (StartsInLobby(options->GameCommandLine))
    {
        MCRejoinNote::Discard(options->UserFolder);
    }

    // A build that updates its content carries the root keys (none until the root key is made).
    std::vector<MCSignedFile::MCPublicKey> rootKeys = BuildRootKeys();
    const bool contentUpdates = !rootKeys.empty();
    const auto dataFolder = FindGameData(options->DataFolder, options->UserFolder, contentUpdates);

    if (!dataFolder || dataFolder->empty())
    {
        if (!dataFolder)
        {
            Report(MCConsole::HasOutput() ? std::format("{}\n\n{}", dataFolder.error(), UsageText) : dataFolder.error(),
                   true);
        }

        SDL_Quit();
        return dataFolder ? 0 : 1;
    }

    options->DataFolder = *dataFolder;
#ifdef _WIN32
    // A program update armed before the game last ended and never run (a crash) installs first, with its content.
    if (contentUpdates && MCSetupBackend::RunLeftOver(options->DataFolder, MC_VERSION_BUILD, SetupPlatform()))
    {
        SDL_Quit();
        return 0;
    }
#endif

    // A build that updates its content holds the game's lock for its run, so no other copy swaps an update in under
    // it (update/MCUpdateLock.h); one that doesn't leaves the game folder as it is.
    std::unique_ptr<MCUpdateLock> gameLock =
        contentUpdates ? MCUpdateLock::Open(options->DataFolder, MCUpdateLock::GameLock) : nullptr;

    const std::optional<MCContentUpdater::MCSetup> repairSetup =
        contentUpdates ? std::optional(ContentSetup(*options, rootKeys)) : std::nullopt;

    if (!SwapStagedUpdate(options->DataFolder, options->GameCommandLine, gameLock.get(),
                          repairSetup ? &*repairSetup : nullptr))
    {
        SDL_Quit();
        return 1;
    }

    if (AwaitsInstall(options->DataFolder, contentUpdates))
    {
        if (const std::optional<int> quit =
                InstallAtFirstStart(ContentSetup(*options, rootKeys), options->GameCommandLine, gameLock.get()))
        {
            SDL_Quit();
            return *quit;
        }
    }

    MCFileSystem::SetGameRoot(options->DataFolder);
    MCFileSystem::SetUserRoot(options->UserFolder);
    SDL_Log("Game data: %s", MCFileSystem::ToUtf8(options->DataFolder).c_str());
    SDL_Log("User data: %s", MCFileSystem::ToUtf8(options->UserFolder).c_str());

    // The game folder's own settings and saves show through the user folder; what the original wrote into it from
    // Program Files went to Windows' VirtualStore, so the first start copies that into the user folder.
    if (const std::filesystem::path store = MCGameFolder::VirtualStore(options->DataFolder); !store.empty())
    {
        if (const std::string error = MCGameFolder::CarryOver(store, options->UserFolder); !error.empty())
        {
            SDL_Log("The original's settings and saves weren't all carried over. %s", error.c_str());
        }
    }

    // Personal build: the HD options are on unless PREFS.CFG turns them off, the Mac app updates itself, and a build
    // that names the reports' server sends them once the player has seen what they hold (main/MCTelemetry.h). The snow
    // thins itself on a machine whose frames can't keep up with it (sprite/MCWeatherPace.h).
    UseHdByDefault();
    MCWeatherPacing::Follow(true);
#if defined(__APPLE__)
    MCUpdater::SetBackend(MCMakeSparkleBackend());
#elif defined(_WIN32)
    if (contentUpdates)
    {
        MCUpdater::SetBackend(
            MCSetupBackend::Make(options->DataFolder, MC_VERSION_SHOWN, MakeProgramSource, SetupPlatform()));
    }
#endif

    if (contentUpdates)
    {
        MCUpdater::SetContent(ContentSetup(*options, std::move(rootKeys)), MakeContentSource);
    }

    MCTelemetry::SetBackend(MCMakeTelemetryBackend(options->UserFolder));
    const int result = RunGame(options->GameCommandLine);
    // A clean quit: staging stops (what's staged stays for the next try), and the reports' black box marks the run as
    // ended.
    MCUpdater::Quit();
    MCTelemetry::Quit();
    MCTelemetry::SetBackend(nullptr);

    // RESTART into an update with no program: the new copy waits for this one's lock, which the system lets go once
    // this process has ended and closed every file it held, then swaps the content in.
    if (MCUpdater::RestartWanted() && MCStartAgain(utf8Arguments))
    {
        static_cast<void>(gameLock.release());
    }

    SDL_Quit();
    return result;
}

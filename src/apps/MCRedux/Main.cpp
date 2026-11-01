#include "stdafx.h"
#include "MCConsole.h"
#include "MCCrashTrace.h"
#include "MCGameFolder.h"
#include "MCVersion.h"
#include "abl/MCAblRuntime.h"
#include "gui/MCGuiStartup.h"
#include "platform/MCAllocator.h"
#include "platform/MCFileSystem.h"

namespace
{
    /// <summary>What the command line asks for: the two folders and the words passed on to the game.</summary>
    struct MCLaunchOptions
    {
        std::filesystem::path DataFolder;
        std::filesystem::path UserFolder;
        std::string GameCommandLine;
    };

    /// <summary>The command line's help text.</summary>
    constexpr std::string_view UsageText =
        "MechCommander Redux " MC_VERSION_SEMVER " (build " MC_VERSION ", commit " MC_VERSION_COMMIT ")\n\n"
        "MCRedux [-d|--data <install folder>] [--user <save folder>] [game options]\n\n"
        "  -d, --data     - The MechCommander Gold install; without it, or when it has no\n"
        "                   SYSTEM.CFG, the current folder, then the executable's folder;\n"
        "                   without any, the folder chosen last time or found, else ask.\n"
        "  --user         - Where saves, prefs and temp files go (default: the SDL pref path)\n"
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
    /// The game's folder: the first of the --data folder (when given), the current folder and the executable's folder
    /// that holds the game's data. Without --data, then the folder chosen last time, the one copy in the usual places
    /// (MCGameFolder), or the player's choice, remembered for next time. Empty when the player quits; an error naming
    /// every folder tried when --data names no game (a script gets no questions).
    /// </summary>
    std::expected<std::filesystem::path, std::string> FindGameData(const std::filesystem::path& dataArgument,
                                                                   const std::filesystem::path& userFolder)
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
            if (!folder.empty() && MCGameFolder::HasGameData(folder))
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

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
    {
        Report(std::format("SDL_Init failed: {}", SDL_GetError()), true);
        return 1;
    }

    const auto dataFolder = FindGameData(options->DataFolder, options->UserFolder);

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
    MCFileSystem::SetGameRoot(options->DataFolder);
    MCFileSystem::SetUserRoot(options->UserFolder);
    SDL_Log("Game data: %s", MCFileSystem::ToUtf8(options->DataFolder).c_str());
    SDL_Log("User data: %s", MCFileSystem::ToUtf8(options->UserFolder).c_str());

    const int result = RunGame(options->GameCommandLine);
    SDL_Quit();
    return result;
}

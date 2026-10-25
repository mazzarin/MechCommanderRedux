// Built without the precompiled header: macOS's CoreGraphics and MCCore's Win32 stand-ins (MCWin32Defs.h) cannot meet.
#include "MCGameFolder.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <expected>
#include <format>
#include <fstream>
#include <mutex>
#include <string_view>
#include <system_error>

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace
{
    /// <summary>The file in the user folder that remembers the chosen game folder: its path, UTF-8, on one line.</summary>
    constexpr std::string_view RememberedFile = "gamefolder.txt";
    /// <summary>How deep a chosen folder that isn't an install is searched (a folder of games, a CD, a Wine drive).</summary>
    constexpr int ChosenFolderDepth = 4;
    /// <summary>How deep a Wine drive is searched: Program Files\MicroProse\MCX, where the installer puts it, is 3.</summary>
    constexpr int WineDriveDepth = 4;
    /// <summary>How deep /Applications and ~/Applications are searched: a folder of games in them is 2.</summary>
    constexpr int ApplicationsDepth = 2;
    /// <summary>The most folders one search reads, so a vast folder (a whole disk) can't stall the start.</summary>
    constexpr int MaxFoldersSearched = 5000;
    /// <summary>The copies the question names; the rest are reached with "Other Folder...".</summary>
    constexpr size_t MaxListed = 4;
    /// <summary>How often the folder dialog's answer is looked for (on Windows it comes from another thread).</summary>
    constexpr Uint32 DialogPollMs = 10;
    constexpr std::string_view AppExtension = ".app";
    /// <summary>Where a Wine wrapper keeps its C: drive: Wineskin and Porting Kit, then Kegworks and Sikarugir.</summary>
    constexpr std::array<std::string_view, 2> WrapperDrives = {"Contents/Resources/drive_c",
                                                               "Contents/SharedSupport/prefix/drive_c"};
    /// <summary>
    /// Folders never searched: Windows' own (large), and the user profiles (a Wine profile links the Mac user's
    /// Desktop, Documents and Downloads, which macOS asks the player before letting the app read).
    /// </summary>
    constexpr std::array<std::string_view, 2> SkippedFolders = {"windows", "users"};
    constexpr const char* DialogTitle = "MechCommander Redux";
    constexpr const char* QuitText = "Quit";
    constexpr const char* OtherFolderText = "Other Folder\xE2\x80\xA6";
    constexpr const char* ChooseFolderText = "Choose Folder\xE2\x80\xA6";

    /// <summary>The question's buttons other than the listed copies (whose IDs are their indexes).</summary>
    enum : int
    {
        OtherFolderButton = -1,
        QuitButton = -2,
    };

    std::string Utf8(const std::filesystem::path& path)
    {
        const std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    std::filesystem::path FromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }

    bool EqualsIgnoringCase(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
        {
            return false;
        }

        for (size_t i = 0; i < a.size(); i++)
        {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            {
                return false;
            }
        }

        return true;
    }

    /// <summary>Whether a folder named <paramref name="name"/> is never searched (hidden, or in SkippedFolders).</summary>
    bool IsSkipped(std::string_view name)
    {
        if (name.starts_with('.'))
        {
            return true;
        }

        for (const std::string_view skipped : SkippedFolders)
        {
            if (EqualsIgnoringCase(name, skipped))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>Whether <paramref name="folder"/> holds a file named <paramref name="name"/>, in any case.</summary>
    bool HasFile(const std::filesystem::path& folder, std::string_view name)
    {
        std::error_code error;

        for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
        {
            std::error_code typeError;

            if (EqualsIgnoringCase(Utf8(it->path().filename()), name) && it->is_regular_file(typeError))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>A search of folders for installs, with its count of folders read (see MaxFoldersSearched).</summary>
    class MCInstallSearch
    {
    public:
        explicit MCInstallSearch(std::vector<std::filesystem::path>& found) : _Found(found) {}

        /// <summary>Adds the installs in <paramref name="folder"/> and under it, <paramref name="depth"/> down.</summary>
        void Search(const std::filesystem::path& folder, int depth)
        {
            if (_Read >= MaxFoldersSearched)
            {
                return;
            }

            _Read++;

            if (MCGameFolder::IsInstall(folder))
            {
                _Found.push_back(folder);
                return;
            }

            if (depth <= 0)
            {
                return;
            }

            std::vector<std::filesystem::path> subfolders;
            std::error_code error;

            for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
            {
                std::error_code typeError;

                if (it->is_symlink(typeError) || !it->is_directory(typeError))
                {
                    continue;
                }

                subfolders.push_back(it->path());
            }

            if (error)
            {
                SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Game search: can't read %s: %s", Utf8(folder).c_str(),
                               error.message().c_str());
            }

            std::ranges::sort(subfolders);

            for (const std::filesystem::path& subfolder : subfolders)
            {
                if (IsSkipped(Utf8(subfolder.filename())))
                {
                    continue;
                }

                if (EqualsIgnoringCase(Utf8(subfolder.extension()), AppExtension))
                {
                    SearchWrapper(subfolder);
                }
                else
                {
                    Search(subfolder, depth - 1);
                }
            }
        }

        /// <summary>Adds the installs on the C: drive of <paramref name="app"/>, when it is a Wine wrapper.</summary>
        void SearchWrapper(const std::filesystem::path& app)
        {
            for (const std::string_view drive : WrapperDrives)
            {
                // In the platform's separators, so a path found inside reads as one built part by part.
                const std::filesystem::path path = app / std::filesystem::path(drive).make_preferred();
                std::error_code error;

                if (std::filesystem::is_directory(path, error) && !std::filesystem::is_symlink(path, error))
                {
                    Search(path, WineDriveDepth);
                }
            }
        }

    private:
        std::vector<std::filesystem::path>& _Found;
        int _Read = 0;
    };

    /// <summary>Adds the installs of <paramref name="more"/> that <paramref name="found"/> lacks, in order.</summary>
    void AddNew(std::vector<std::filesystem::path>& found, const std::vector<std::filesystem::path>& more)
    {
        std::vector<std::filesystem::path> known;

        for (const std::filesystem::path& install : found)
        {
            std::error_code error;
            known.push_back(std::filesystem::weakly_canonical(install, error));
        }

        for (const std::filesystem::path& install : more)
        {
            std::error_code error;
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(install, error);

            if (std::ranges::find(known, canonical) == known.end())
            {
                known.push_back(canonical);
                found.push_back(install);
            }
        }
    }

#if defined(__APPLE__)
    /// <summary>
    /// The app bundle MCRedux runs from, or empty when it isn't in one or runs from a translocated copy (a downloaded
    /// app not yet moved runs from a private read-only folder, so nothing is beside it).
    /// </summary>
    std::filesystem::path AppBundle()
    {
        const char* basePath = SDL_GetBasePath();

        if (basePath == nullptr)
        {
            return {};
        }

        const std::filesystem::path base = FromUtf8(basePath).lexically_normal();

        if (Utf8(base).find("/AppTranslocation/") != std::string::npos)
        {
            return {};
        }

        for (std::filesystem::path folder = base; folder.has_relative_path(); folder = folder.parent_path())
        {
            if (EqualsIgnoringCase(Utf8(folder.extension()), AppExtension))
            {
                return folder;
            }
        }

        return {};
    }
#endif

    /// <summary>What the question says, naming the <paramref name="listed"/> copies.</summary>
    std::string QuestionText(std::span<const std::filesystem::path> listed)
    {
        if (listed.empty())
        {
            return "MechCommander Redux plays your copy of MechCommander Gold, and couldn't find it.\n\nChoose the "
                   "folder MechCommander Gold is installed in (the one with SYSTEM.CFG and MCX.EXE), or a folder "
                   "that holds it.";
        }

        std::string text = "Which copy of MechCommander Gold should MechCommander Redux play?\n";

        for (size_t i = 0; i < listed.size(); i++)
        {
            text += std::format("\n{}. {}", i + 1, Utf8(listed[i]));
        }

#if defined(__APPLE__)
        text += "\n\nTo choose again later, hold Option (\xE2\x8C\xA5) while MechCommander Redux opens.";
#endif
        return text;
    }

    /// <summary>Asks which of <paramref name="listed"/> to play: its index, OtherFolderButton or QuitButton.</summary>
    std::expected<int, std::string> AskWhich(std::span<const std::filesystem::path> listed)
    {
        std::vector<std::string> labels;
        std::vector<SDL_MessageBoxButtonData> buttons;

        for (size_t i = 0; i < listed.size(); i++)
        {
            labels.push_back(std::format("{}. {}", i + 1, MCGameFolder::Label(listed[i])));
        }

        for (size_t i = 0; i < labels.size(); i++)
        {
            const SDL_MessageBoxButtonFlags flags = i == 0 ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0;
            buttons.push_back({flags, static_cast<int>(i), labels[i].c_str()});
        }

        buttons.push_back({listed.empty() ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0u, OtherFolderButton,
                           listed.empty() ? ChooseFolderText : OtherFolderText});
        buttons.push_back({SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, QuitButton, QuitText});

        const std::string text = QuestionText(listed);
        SDL_MessageBoxData box{};
        box.flags = SDL_MESSAGEBOX_INFORMATION;
        box.title = DialogTitle;
        box.message = text.c_str();
        box.numbuttons = static_cast<int>(buttons.size());
        box.buttons = buttons.data();
        int pressed = QuitButton;

        if (!SDL_ShowMessageBox(&box, &pressed))
        {
            return std::unexpected(std::format("Couldn't ask where MechCommander Gold is: {}", SDL_GetError()));
        }

        return pressed;
    }

    /// <summary>The folder dialog's answer, set by its callback (on Windows, from another thread).</summary>
    struct MCFolderAnswer
    {
        std::mutex Lock;
        bool Done = false;
        std::filesystem::path Folder;
        std::string Error;
    };

    void SDLCALL OnFolderChosen(void* userdata, const char* const* files, int /*filter*/)
    {
        auto& answer = *static_cast<MCFolderAnswer*>(userdata);
        const std::scoped_lock lock(answer.Lock);

        if (files == nullptr)
        {
            answer.Error = std::format("The folder dialog failed: {}", SDL_GetError());
        }
        else if (files[0] != nullptr)
        {
            answer.Folder = FromUtf8(files[0]);
        }

        answer.Done = true;
    }

    /// <summary>The folder the player picks in a folder dialog, or empty when they cancel.</summary>
    std::expected<std::filesystem::path, std::string> PickFolder()
    {
        MCFolderAnswer answer;
        const SDL_PropertiesID properties = SDL_CreateProperties();

        if (properties == 0)
        {
            return std::unexpected(std::format("Couldn't open the folder dialog: {}", SDL_GetError()));
        }

        SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, "Locate MechCommander Gold");
        SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_ACCEPT_STRING, "Choose");
        SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER, OnFolderChosen, &answer, properties);

        for (;;)
        {
            {
                const std::scoped_lock lock(answer.Lock);

                if (answer.Done)
                {
                    break;
                }
            }

            SDL_PumpEvents();
            SDL_Delay(DialogPollMs);
        }

        SDL_DestroyProperties(properties);

        if (!answer.Error.empty())
        {
            return std::unexpected(answer.Error);
        }

        return answer.Folder;
    }

    /// <summary>Says that <paramref name="folder"/> holds no copy of the game.</summary>
    void ReportNoneIn(const std::filesystem::path& folder)
    {
        const std::string text = std::format("There's no MechCommander Gold in {}.\n\nChoose the folder it's installed "
                                             "in: the one with SYSTEM.CFG and MCX.EXE.",
                                             Utf8(folder));

        if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, DialogTitle, text.c_str(), nullptr))
        {
            SDL_Log("%s (and no message box: %s)", text.c_str(), SDL_GetError());
        }
    }
}

bool MCGameFolder::HasGameData(const std::filesystem::path& folder)
{
    return HasFile(folder, "SYSTEM.CFG");
}

bool MCGameFolder::IsInstall(const std::filesystem::path& folder)
{
    return HasFile(folder, "SYSTEM.CFG") && HasFile(folder, "MCX.EXE");
}

std::vector<std::filesystem::path> MCGameFolder::FindInstalls(const std::filesystem::path& root, int depth)
{
    std::vector<std::filesystem::path> found;
    MCInstallSearch search(found);

    if (EqualsIgnoringCase(Utf8(root.extension()), AppExtension))
    {
        search.SearchWrapper(root);
    }
    else
    {
        search.Search(root, depth);
    }

    std::ranges::sort(found);
    return found;
}

std::vector<std::filesystem::path> MCGameFolder::FindInUsualPlaces()
{
    std::vector<std::filesystem::path> found;

#if defined(__APPLE__)
    // Beside the app (in its folder, or a folder or Wine wrapper next to it), then in the Applications folders.
    if (const std::filesystem::path bundle = AppBundle(); !bundle.empty())
    {
        AddNew(found, FindInstalls(bundle.parent_path(), 1));
    }

    AddNew(found, FindInstalls("/Applications", ApplicationsDepth));
    const char* home = SDL_getenv("HOME");

    if (home == nullptr)
    {
        return found;
    }

    const std::filesystem::path homeFolder = FromUtf8(home);
    AddNew(found, FindInstalls(homeFolder / "Applications", ApplicationsDepth));

    // CrossOver's bottles, each a Wine prefix: Bottles/<name>/drive_c.
    const std::filesystem::path bottles = homeFolder / "Library/Application Support/CrossOver/Bottles";
    std::error_code error;

    for (std::filesystem::directory_iterator it(bottles, error), end; !error && it != end; it.increment(error))
    {
        std::error_code typeError;

        if (it->is_directory(typeError) && !it->is_symlink(typeError))
        {
            AddNew(found, FindInstalls(it->path() / "drive_c", WineDriveDepth));
        }
    }
#endif

    return found;
}

std::filesystem::path MCGameFolder::Remembered(const std::filesystem::path& userFolder)
{
    std::ifstream in(userFolder / RememberedFile, std::ios::binary);
    std::string line;

    if (!in || !std::getline(in, line))
    {
        return {};
    }

    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
    {
        line.pop_back();
    }

    return FromUtf8(line);
}

std::string MCGameFolder::Remember(const std::filesystem::path& userFolder, const std::filesystem::path& folder)
{
    std::error_code error;
    std::filesystem::create_directories(userFolder, error);

    if (error)
    {
        return std::format("Can't make {}: {}", Utf8(userFolder), error.message());
    }

    const std::filesystem::path file = userFolder / RememberedFile;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << Utf8(folder) << '\n';

    if (!out.flush())
    {
        return std::format("Can't write {}", Utf8(file));
    }

    return {};
}

std::string MCGameFolder::Label(const std::filesystem::path& install)
{
    for (std::filesystem::path folder = install; folder.has_relative_path(); folder = folder.parent_path())
    {
        if (EqualsIgnoringCase(Utf8(folder.extension()), AppExtension))
        {
            return Utf8(folder.filename());
        }

        const std::filesystem::path parent = folder.parent_path();

        if (parent.filename() == "Bottles" && parent.parent_path().filename() == "CrossOver")
        {
            return std::format("{} bottle", Utf8(folder.filename()));
        }
    }

    return Utf8(install.filename());
}

bool MCGameFolder::ChoiceRequested()
{
#if defined(__APPLE__)
    return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & kCGEventFlagMaskAlternate) != 0;
#else
    return false;
#endif
}

std::expected<std::filesystem::path, std::string> MCGameFolder::Ask(std::span<const std::filesystem::path> found)
{
    std::vector<std::filesystem::path> listed(found.begin(), found.end());
    listed.resize(std::min(listed.size(), MaxListed));

    for (;;)
    {
        const auto pressed = AskWhich(listed);

        if (!pressed)
        {
            return std::unexpected(pressed.error());
        }

        if (*pressed == QuitButton)
        {
            return std::filesystem::path();
        }

        if (*pressed >= 0 && static_cast<size_t>(*pressed) < listed.size())
        {
            return listed[static_cast<size_t>(*pressed)];
        }

        const auto picked = PickFolder();

        if (!picked)
        {
            return std::unexpected(picked.error());
        }

        if (picked->empty())
        {
            continue;
        }

        if (HasGameData(*picked))
        {
            return *picked;
        }

        std::vector<std::filesystem::path> inside = FindInstalls(*picked, ChosenFolderDepth);

        if (inside.size() == 1)
        {
            return inside.front();
        }

        if (inside.empty())
        {
            ReportNoneIn(*picked);
            continue;
        }

        inside.resize(std::min(inside.size(), MaxListed));
        listed = std::move(inside);
    }
}

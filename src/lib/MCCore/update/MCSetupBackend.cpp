#include "stdafx.h"
#include "update/MCSetupBackend.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "update/MCReleaseFile.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateJson.h"

#include <mutex>
#include <thread>

namespace
{
    using MCUpdater::MCAnswer;

    /// <summary>The armed record's keys.</summary>
    constexpr std::string_view BuildKey = "build";
    constexpr std::string_view Sha256Key = "sha256";
    constexpr std::string_view ArgumentsKey = "arguments";

    /// <summary>A setup's file in the program folder: its build between these.</summary>
    constexpr std::string_view SetupPrefix = "setup-";
    constexpr std::string_view SetupSuffix = ".exe";

    /// <summary>The bytes a program's file name may hold in its address: none needs escaping.</summary>
    constexpr std::string_view NameBytes = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";

    /// <summary>The setup armed: its build, hash, and the arguments that run it silently.</summary>
    struct MCArmed
    {
        uint64_t Build = 0;
        MCSha256::MCDigest Sha256{};
        std::string Arguments;
    };

    std::filesystem::path In(const std::filesystem::path& gameFolder, std::string_view relative)
    {
        return gameFolder / MCFileSystem::FromUtf8(relative);
    }

    /// <summary>The setup's file for <paramref name="build"/>, in the program folder.</summary>
    std::filesystem::path SetupPath(const std::filesystem::path& gameFolder, uint64_t build)
    {
        return In(gameFolder, MCSetupBackend::ProgramFolder) /
               MCFileSystem::FromUtf8(std::format("{}{}{}", SetupPrefix, build, SetupSuffix));
    }

    std::string ArmedText(const MCArmed& armed)
    {
        return MCUpdateJson::MCJson{
            {BuildKey, armed.Build}, {Sha256Key, MCSha256::Hex(armed.Sha256)}, {ArgumentsKey, armed.Arguments}}
            .dump(1);
    }

    /// <summary>The setup armed, none, or why the record can't be read.</summary>
    std::expected<std::optional<MCArmed>, std::string> ReadArmed(const std::filesystem::path& gameFolder)
    {
        const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
        const std::filesystem::path path = In(gameFolder, MCSetupBackend::ArmedFile);

        if (!disk->Stat(path).Exists)
        {
            return std::nullopt;
        }

        const auto text = disk->ReadText(path);

        if (!text)
        {
            return std::unexpected(text.error());
        }

        return MCUpdateJson::Read(MCSetupBackend::ArmedFile,
                                  [&]() -> std::optional<MCArmed>
                                  {
                                      const MCUpdateJson::MCJson json = MCUpdateJson::Object(*text);
                                      return MCArmed{.Build = MCUpdateJson::Count(json, BuildKey),
                                                     .Sha256 = MCUpdateJson::Digest(json, Sha256Key),
                                                     .Arguments = MCUpdateJson::Text(json, ArgumentsKey)};
                                  });
    }

    /// <summary>Forgets the setup armed: it ran, or isn't to.</summary>
    void Disarm(const std::filesystem::path& gameFolder)
    {
        std::error_code error;
        std::filesystem::remove(In(gameFolder, MCSetupBackend::ArmedFile), error);

        if (error)
        {
            SDL_Log("Updater: the armed setup's record can't be removed: %s", error.message().c_str());
        }
    }

    /// <summary>
    /// <paramref name="url"/> as the folder it's in and its file's name, or why it can't be fetched (a name that would
    /// need escaping, a query).
    /// </summary>
    std::expected<std::pair<std::string, std::string>, std::string> Split(std::string_view url)
    {
        const size_t slash = url.rfind('/');

        if (slash == std::string_view::npos || slash + 1 == url.size() ||
            url.find_first_not_of(NameBytes, slash + 1) != std::string_view::npos)
        {
            return std::unexpected(std::format("\"{}\" isn't a program's address the game fetches", url));
        }

        return std::pair{std::string(url.substr(0, slash)), std::string(url.substr(slash + 1))};
    }

    /// <summary>What's wrong with <paramref name="program"/> as release.json gives it, or empty.</summary>
    std::string Problem(const MCReleaseFile::MCProgram& program)
    {
        if (!program.Sha256 || program.Size == 0)
        {
            return "release.json gives the program no size or SHA-256";
        }

        if (program.Silent.empty())
        {
            return "release.json gives no way to run the program's setup silently";
        }

        const auto split = Split(program.Url);
        return split ? std::string() : split.error();
    }

    /// <summary>The download's progress and end, shared by its thread and the frames that report it.</summary>
    struct MCDownload
    {
        std::mutex Mutex;
        uint64_t Received = 0;
        bool Finished = false;
        /// <summary>What failed; empty, it's in, checked and armed.</summary>
        std::string Error;
    };

    /// <summary>
    /// On the download's thread: <paramref name="program"/>'s setup fetched (resumed when cut off before), checked
    /// against release.json, asked whether the system runs it, and armed.
    /// </summary>
    std::expected<void, std::string> FetchSetup(
        const std::filesystem::path& gameFolder, const MCReleaseFile::MCProgram& program,
        const MCSetupBackend::MCSourceFor& sources,
        const std::function<std::string(const std::filesystem::path&)>& runnable, MCDownload& download,
        const std::stop_token& stop)
    {
        const auto split = Split(program.Url);

        if (!split)
        {
            return std::unexpected(split.error());
        }

        auto source = sources ? sources(split->first)
                              : std::expected<std::unique_ptr<MCContentStage::MCSource>, std::string>(
                                    std::unexpected("this build has no transfer for programs"));

        if (!source)
        {
            return std::unexpected(source.error());
        }

        const std::filesystem::path setup = SetupPath(gameFolder, program.Build);
        std::error_code error;
        std::filesystem::create_directories(setup.parent_path(), error);
        uint64_t had = std::filesystem::exists(setup, error) ? std::filesystem::file_size(setup, error) : 0;

        // Longer than the program, it isn't a cut-off download of it: fetched again.
        if (had > program.Size)
        {
            std::filesystem::remove(setup, error);
            had = 0;
        }

        {
            const std::lock_guard lock(download.Mutex);
            download.Received = had;
        }

        const auto fetched = (*source)->Fetch(split->second, setup, program.Size,
                                              [&](uint64_t bytes)
                                              {
                                                  const std::lock_guard lock(download.Mutex);
                                                  download.Received += bytes;
                                                  return !stop.stop_requested();
                                              });

        if (!fetched)
        {
            return std::unexpected(fetched.error());
        }

        const auto hash = MCSha256::OfFile(setup);

        if (!hash || std::filesystem::file_size(setup, error) != program.Size || *hash != *program.Sha256)
        {
            std::filesystem::remove(setup, error);
            return std::unexpected("the program's download isn't the one release.json names");
        }

        if (const std::string refused = runnable ? runnable(setup) : std::string(); !refused.empty())
        {
            return std::unexpected(refused);
        }

        const MCArmed armed{.Build = program.Build, .Sha256 = *program.Sha256, .Arguments = program.Silent};
        const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();

        if (!WriteDurably(*disk, In(gameFolder, MCSetupBackend::ArmedFile), ArmedText(armed)))
        {
            return std::unexpected(std::format("{} can't be written", MCSetupBackend::ArmedFile));
        }

        return {};
    }

    /// <summary>Removes the setups for <paramref name="runningBuild"/> and older, and their folder once it's empty.</summary>
    void ClearDone(const std::filesystem::path& gameFolder, uint64_t runningBuild)
    {
        const std::filesystem::path folder = In(gameFolder, MCSetupBackend::ProgramFolder);
        std::error_code error;

        if (!std::filesystem::is_directory(folder, error))
        {
            return;
        }

        std::vector<std::filesystem::path> done;

        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, error))
        {
            const std::string name = MCFileSystem::ToUtf8(entry.path().filename());
            uint64_t build = 0;

            if (name.starts_with(SetupPrefix) && name.ends_with(SetupSuffix))
            {
                const std::string_view digits(name.data() + SetupPrefix.size(),
                                              name.size() - SetupPrefix.size() - SetupSuffix.size());
                const auto [end, failed] = std::from_chars(digits.data(), digits.data() + digits.size(), build);

                if (failed == std::errc() && end == digits.data() + digits.size() && build <= runningBuild)
                {
                    done.push_back(entry.path());
                }
            }
        }

        for (const std::filesystem::path& path : done)
        {
            std::filesystem::remove(path, error);

            // A setup still running (it started this game) holds its file: the next start tries again.
            if (error)
            {
                SDL_Log("Updater: %s can't be removed yet: %s", MCFileSystem::ToUtf8(path).c_str(),
                        error.message().c_str());
            }
        }

        std::filesystem::remove(folder, error);
    }

    /// <summary>The backend: the program's setup, downloaded on a thread of its own and reported each frame.</summary>
    class MCBackend final : public MCUpdater::MCBackend
    {
    public:
        MCBackend(std::filesystem::path gameFolder, std::string runningVersion, MCSetupBackend::MCSourceFor sources,
                  MCSetupBackend::MCPlatform platform)
            : _GameFolder(std::move(gameFolder))
            , _RunningVersion(std::move(runningVersion))
            , _Sources(std::move(sources))
            , _Platform(std::move(platform))
        {
        }

        ~MCBackend() override { StopDownload(); }
        MCBackend(const MCBackend&) = delete;
        MCBackend& operator=(const MCBackend&) = delete;

        std::string RunningVersion() override { return _RunningVersion; }

        void Offered(const MCReleaseFile::MCProgram& program) override { _Program = program; }

        void Check(std::optional<uint64_t> build) override
        {
            StopDownload();
            _State = MCState::Idle;

            // Only the build release.json gives this copy: none, or another, isn't out.
            if (!build || !_Program || _Program->Build != *build)
            {
                Report([] { MCUpdater::Closed(); });
                return;
            }

            if (const std::string problem = Problem(*_Program); !problem.empty())
            {
                Report([problem] { MCUpdater::Failed(problem); });
                return;
            }

            _State = MCState::Found;
            Report([build = *build] { MCUpdater::Found(std::format("({})", build), std::string()); });
        }

        void Answer(MCAnswer answer) override
        {
            if (answer == MCAnswer::Install && _State == MCState::Found)
            {
                StartDownload();
                return;
            }

            if (answer == MCAnswer::Install && _State == MCState::Ready)
            {
                // RESTART: the game quits, and the setup runs as it ends (Quit), then starts the new version.
                SDL_Log("Updater: build %llu installs as the game quits, then starts",
                        static_cast<unsigned long long>(_Program->Build));
                _State = MCState::Installing;
                _Restart = true;
                MCUpdater::Installing();

                if (_Platform.Quit)
                {
                    _Platform.Quit();
                }
                else
                {
                    MCInput::PostQuitMessage();
                }

                return;
            }

            // Skip once ready (the content couldn't be armed), or Later: the setup isn't run.
            if (_State == MCState::Ready)
            {
                Disarm(_GameFolder);
            }

            StopDownload();
            _State = MCState::Idle;
            Report([] { MCUpdater::Closed(); });
        }

        void Frame() override
        {
            // Reports go out a frame after what caused them, as Sparkle's do: the updater is never called back from
            // inside its own call.
            for (const std::function<void()>& report : std::exchange(_Reports, {}))
            {
                report();
            }

            if (_State != MCState::Downloading || _Download == nullptr)
            {
                return;
            }

            uint64_t received = 0;
            bool finished = false;
            std::string error;

            {
                const std::lock_guard lock(_Download->Mutex);
                received = _Download->Received;
                finished = _Download->Finished;
                error = _Download->Error;
            }

            if (!finished)
            {
                if (received != _Shown)
                {
                    _Shown = received;
                    MCUpdater::Downloading(received, _Program->Size);
                }

                return;
            }

            StopDownload();

            if (!error.empty())
            {
                _State = MCState::Idle;
                MCUpdater::Failed(error);
                return;
            }

            _State = MCState::Ready;
            MCUpdater::Downloading(_Program->Size, _Program->Size);
            MCUpdater::Ready();
        }

        void Quit() override
        {
            StopDownload();

            if (_State != MCState::Ready && _State != MCState::Installing)
            {
                return;
            }

            const std::filesystem::path setup = SetupPath(_GameFolder, _Program->Build);
            const std::string failed = _Platform.Start ? _Platform.Start(setup, _Program->Silent, _Restart)
                                                       : std::string("this build can't run a setup");

            if (!failed.empty())
            {
                SDL_Log("Updater: build %llu's setup didn't start (%s); the next start tries again",
                        static_cast<unsigned long long>(_Program->Build), failed.c_str());
                return;
            }

            SDL_Log("Updater: build %llu's setup installs it as the game ends",
                    static_cast<unsigned long long>(_Program->Build));
            Disarm(_GameFolder);
        }

    private:
        enum class MCState : uint8_t
        {
            Idle,
            Found,
            Downloading,
            Ready,
            Installing,
        };

        void Report(std::function<void()> report) { _Reports.push_back(std::move(report)); }

        void StartDownload()
        {
            SDL_Log("Updater: downloading build %llu's setup", static_cast<unsigned long long>(_Program->Build));
            _State = MCState::Downloading;
            _Shown = 0;
            _Download = std::make_shared<MCDownload>();
            _Worker = std::jthread(
                [download = _Download, folder = _GameFolder, program = *_Program, sources = _Sources,
                 runnable = _Platform.Runnable](std::stop_token stop)
                {
                    const auto done = FetchSetup(folder, program, sources, runnable, *download, stop);
                    const std::lock_guard lock(download->Mutex);
                    download->Finished = true;

                    if (!done)
                    {
                        download->Error = done.error();
                    }
                });
        }

        /// <summary>Stops the download under way (what's in stays, for the next try) and waits for its thread.</summary>
        void StopDownload()
        {
            if (_Worker.joinable())
            {
                _Worker.request_stop();
                _Worker.join();
            }

            _Download.reset();
        }

        std::filesystem::path _GameFolder;
        std::string _RunningVersion;
        MCSetupBackend::MCSourceFor _Sources;
        MCSetupBackend::MCPlatform _Platform;
        std::optional<MCReleaseFile::MCProgram> _Program;
        MCState _State = MCState::Idle;
        /// <summary>RESTART: the setup starts the game again once it's in.</summary>
        bool _Restart = false;
        std::vector<std::function<void()>> _Reports;
        std::shared_ptr<MCDownload> _Download;
        std::jthread _Worker;
        /// <summary>The bytes last reported.</summary>
        uint64_t _Shown = 0;
    };
}

std::unique_ptr<MCUpdater::MCBackend> MCSetupBackend::Make(std::filesystem::path gameFolder, std::string runningVersion,
                                                           MCSourceFor sources, MCPlatform platform)
{
    return std::make_unique<MCBackend>(std::move(gameFolder), std::move(runningVersion), std::move(sources),
                                       std::move(platform));
}

bool MCSetupBackend::RunLeftOver(const std::filesystem::path& gameFolder, uint64_t runningBuild,
                                 const MCPlatform& platform)
{
    const auto armed = ReadArmed(gameFolder);

    if (!armed)
    {
        SDL_Log("Updater: the armed setup's record can't be read (%s): it's dropped", armed.error().c_str());
        Disarm(gameFolder);
    }

    if (!armed || !*armed || (*armed)->Build <= runningBuild)
    {
        if (armed && *armed)
        {
            Disarm(gameFolder);
        }

        ClearDone(gameFolder, runningBuild);
        return false;
    }

    // Tried once: a setup that fails doesn't stop every start after it.
    const MCArmed left = **armed;
    Disarm(gameFolder);
    const std::filesystem::path setup = SetupPath(gameFolder, left.Build);
    const auto hash = MCSha256::OfFile(setup);

    if (!hash || *hash != left.Sha256)
    {
        SDL_Log("Updater: build %llu's armed setup isn't as downloaded: it's dropped",
                static_cast<unsigned long long>(left.Build));
        return false;
    }

    const std::string failed =
        platform.Start ? platform.Start(setup, left.Arguments, true) : std::string("this build can't run a setup");

    if (!failed.empty())
    {
        SDL_Log("Updater: build %llu's armed setup didn't start: %s", static_cast<unsigned long long>(left.Build),
                failed.c_str());
        return false;
    }

    SDL_Log("Updater: build %llu's setup, armed before the game last ended, installs it now",
            static_cast<unsigned long long>(left.Build));
    return true;
}

#include "stdafx.h"
#include "main/MCUpdater.h"
#include "platform/MCInput.h"
#include "update/MCContentList.h"
#include "update/MCContentUpdater.h"
#include "update/MCInstalled.h"
#include "update/MCReleaseFile.h"
#include "update/MCUpdateDisk.h"
#include "update/MCUpdateLock.h"
#include "update/MCUpdateWork.h"

namespace
{
    using MCContentUpdater::MCOffer;
    using MCUpdater::MCAnswer;
    using MCUpdater::MCStage;

    /// <summary>
    /// The most a version, its notes and an error may hold: they come from outside the game (the feed,
    /// release.json), so a broken one stays small.
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

    /// <summary>The content's updater's steps, finer than the stages the page shows.</summary>
    enum class MCStep : uint8_t
    {
        /// <summary>Nothing under way.</summary>
        Idle,
        /// <summary>The check runs: release.json read, the content planned.</summary>
        CheckingContent,
        /// <summary>The program's backend looks for the build release.json gives this copy.</summary>
        CheckingProgram,
        /// <summary>The update is shown: the player answers.</summary>
        Offered,
        /// <summary>The content stages.</summary>
        StagingContent,
        /// <summary>The content is staged; the program downloads.</summary>
        DownloadingProgram,
        /// <summary>An update with nothing to fetch goes in unasked.</summary>
        ArmingQuietly,
        /// <summary>Both halves armed: RESTART or quitting installs them.</summary>
        Armed,
        /// <summary>Installing, or failed: nothing more this run.</summary>
        Finished,
        /// <summary>The sets the player chose (HD OPTIONS) are planned, staged and armed, with no update.</summary>
        ChangingSets,
        /// <summary>The change of sets goes in at the next start: nothing more this run.</summary>
        SetsArmed,
    };

    /// <summary>
    /// The content's work off the main thread (a check, staging, arming, the HD pack's change): its progress and end,
    /// read each frame. It ends with the update found, when it looked for one, or what failed.
    /// </summary>
    using MCWorkResult = std::expected<std::optional<MCOffer>, std::string>;
    using MCContentWork = MCUpdateWork::MCWork<std::optional<MCOffer>>;
    using MCJob = std::function<MCWorkResult(MCUpdateWork::MCContext& context)>;

    std::unique_ptr<MCUpdater::MCBackend> Backend;
    MCUpdater::MCStatus Current;
    std::function<void()> Listener;
    /// <summary>The start's check was made: once a run.</summary>
    bool CheckedAtStart = false;

    /// <summary>The content's updater: the copy, its host, where it is and the update offered.</summary>
    std::optional<MCContentUpdater::MCSetup> Content;
    MCUpdater::MCSourceMaker MakeSource;
    MCStep Step = MCStep::Idle;
    std::optional<MCOffer> Offer;
    /// <summary>The work under way.</summary>
    std::unique_ptr<MCContentWork> Work;
    /// <summary>Held from INSTALL until both halves are armed or it fails: one copy stages at a time.</summary>
    std::unique_ptr<MCUpdateLock> StagingLock;
    /// <summary>The HD pack as HD OPTIONS shows it.</summary>
    MCUpdater::MCHdPack Hd;
    /// <summary>The player chose again while the change of sets ran: it stops, then the new choice is planned.</summary>
    bool SetsChosenAgain = false;
    /// <summary>
    /// The program's backend was told to let its update go (LookAgain) and hasn't closed its session yet: a new look
    /// for the program waits for that (MCUpdater::Closed), so the old session's end isn't taken for the new one's.
    /// </summary>
    bool ProgramClosing = false;
    /// <summary>A match's mission plays: the content's downloads wait (<see cref="MCUpdater::PauseWork"/>).</summary>
    bool Paused = false;
    /// <summary>The download the pause stopped (staging or the change of sets), to go on with after it; none, Idle.</summary>
    MCStep PausedStep = MCStep::Idle;

    /// <summary>The input's quit: the game ends as if the player had closed it.</summary>
    void InputQuit()
    {
        MCInput::PostQuitMessage();
    }

    std::function<void()> QuitGame = InputQuit;
    bool RestartAfterQuit = false;

    /// <summary>Tells the listener <see cref="Current"/> changed.</summary>
    void Changed()
    {
        if (Listener)
        {
            Listener();
        }
    }

    /// <summary>The program's backend looks for the build release.json gives this copy, told its entry there first.</summary>
    void CheckProgram()
    {
        if (const auto program = Offer->Release.Programs.find(Content->Platform);
            program != Offer->Release.Programs.end())
        {
            Backend->Offered(program->second);
        }

        Backend->Check(Offer->ProgramBuild);
    }

    /// <summary>Stops the work under way (its transfer cancelled at once) and waits for it.</summary>
    void StopWork()
    {
        if (Work == nullptr)
        {
            return;
        }

        Work->Stop();

        if (const MCWorkResult ended = Work->Join(); !ended)
        {
            SDL_Log("Updater: the work under way stopped: %s", ended.error().c_str());
        }

        Work.reset();
    }

    /// <summary>Forgets the content's update: back to nothing under way, the staging lock let go.</summary>
    void ResetContent()
    {
        StopWork();
        StagingLock.reset();
        Offer.reset();
        Step = MCStep::Idle;
        RestartAfterQuit = false;
        Hd = {};
        SetsChosenAgain = false;
        ProgramClosing = false;
        PausedStep = MCStep::Idle;
    }

    /// <summary>What the copy has of the HD pack, read from its record and the sets chosen.</summary>
    void ReadHd()
    {
        const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
        const auto installed = MCInstalled::Read(Content->GameFolder);

        if (!installed)
        {
            SDL_Log("Updater: %s", installed.error().c_str());
        }

        const MCInstalled::MCRecord record = installed.value_or(MCInstalled::MCRecord{});
        const MCContentUpdater::MCSets has =
            record.Release != 0 ? record.Sets : MCContentUpdater::SetsFound(Content->GameFolder);
        Hd = {.Managed = true,
              .Installed = has.contains(MCContentList::HdSet),
              .Chosen = MCContentUpdater::Wanted(*disk, Content->GameFolder, record).contains(MCContentList::HdSet)};

        if (const auto bytes = record.SetBytes.find(MCContentList::HdSet); bytes != record.SetBytes.end())
        {
            Hd.Bytes = bytes->second;
        }
    }

    /// <summary>
    /// Runs <paramref name="job"/> (<paramref name="name"/> in the logs) on a thread of its own;
    /// <see cref="MCUpdater::Frame"/> picks up its end.
    /// </summary>
    void RunWork(std::string name, MCJob job)
    {
        StopWork();
        Work = std::make_unique<MCContentWork>(std::move(name), std::move(job));
    }

    /// <summary>
    /// A step failed: the page says why, and nothing more happens this run. Work still under way (the program's
    /// backend failing while the content stages) stops before the staging lock goes, so no copy stages beside it.
    /// </summary>
    void Fail(std::string_view error)
    {
        SDL_Log("Updater: %s failed: %s", Current.Version.c_str(), std::string(error).c_str());
        StopWork();
        StagingLock.reset();
        PausedStep = MCStep::Idle;
        Step = MCStep::Finished;
        Current.Stage = MCStage::Failed;
        Current.Error = Printable(error, MaxErrorLength, false);
        Changed();
    }

    /// <summary>What the whole update downloads: the content and the program.</summary>
    uint64_t DownloadBytes()
    {
        return Offer->ContentBytes() + (Offer->ProgramBuild ? Offer->ProgramBytes : 0);
    }

    /// <summary>The update is shown, with what it brings and its size.</summary>
    void ShowOffer()
    {
        Step = MCStep::Offered;
        Current = {.Stage = MCStage::Found,
                   .Version = Printable(Offer->ShownVersion(Content->RunningBuild), MaxVersionLength, false),
                   .Notes = Printable(Offer->Release.Notes, MaxNotesLength, true),
                   .Expected = DownloadBytes()};
        SDL_Log("Updater: %s is out (%llu bytes to download)", Current.Version.c_str(),
                static_cast<unsigned long long>(Current.Expected));
        Changed();
    }

    /// <summary>The downloads' progress: <paramref name="received"/> bytes in of the whole update.</summary>
    void ShowProgress(uint64_t received, uint64_t expected)
    {
        if (Current.Received == received && Current.Expected == expected)
        {
            return;
        }

        Current.Received = received;
        Current.Expected = expected;
        Changed();
    }

    /// <summary>Takes the staging lock: false while another copy stages.</summary>
    bool TakeStagingLock()
    {
        StagingLock = MCUpdateLock::Open(Content->GameFolder, MCUpdateLock::StagingLock);

        if (StagingLock == nullptr || !StagingLock->TryExclusive())
        {
            StagingLock.reset();
            return false;
        }

        return true;
    }

    /// <summary>Stages the update's content on the work's thread; <paramref name="arm"/>, arms it there too.</summary>
    void StartStaging(bool arm)
    {
        RunWork("the update's staging",
                [setup = *Content, makeSource = MakeSource, offer = *Offer, arm](MCUpdateWork::MCContext& context)
                {
                    return MCUpdateWork::WithSource<std::optional<MCOffer>>(
                        makeSource, context.Stop,
                        [&](MCContentStage::MCSource& source) -> MCWorkResult
                        {
                            const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();

                            if (const auto staged =
                                    MCContentUpdater::Stage(setup, *disk, source, offer,
                                                            MCUpdateWork::Reporter(context.Progress, context.Stop));
                                !staged)
                            {
                                return std::unexpected(staged.error());
                            }

                            if (const auto armed = arm ? MCContentUpdater::Arm(setup, *disk, offer)
                                                       : std::expected<void, std::string>();
                                !armed)
                            {
                                return std::unexpected(armed.error());
                            }

                            return {};
                        });
                });
    }

    /// <summary>Arms the staged content once the program is in too: READY, or neither half goes in.</summary>
    /// <returns>Whether it's armed; if not, the update failed.</returns>
    bool ArmContent()
    {
        const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();

        if (const auto armed = MCContentUpdater::Arm(*Content, *disk, *Offer); !armed)
        {
            // The program's installer is armed already: told to skip, it installs nothing either.
            if (Offer->ProgramBuild && Backend != nullptr)
            {
                Backend->Answer(MCAnswer::Skip);
            }

            Fail(armed.error());
            return false;
        }

        StagingLock.reset();
        Step = MCStep::Armed;
        SDL_Log("Updater: %s is ready to install", Current.Version.c_str());
        Current.Stage = MCStage::Ready;
        Current.Received = Current.Expected;
        Changed();
        return true;
    }

    /// <summary>
    /// Plans, stages and arms the sets the player chose on the work's thread (no update: the release installed). The
    /// staging lock is held until it ends.
    /// </summary>
    void StartSetsChange()
    {
        Hd.Stopping = false;
        Hd.Error.clear();
        Hd.Received = 0;
        Hd.Expected = 0;

        if (!TakeStagingLock())
        {
            SDL_Log("Updater: the HD pack waits: another copy of the game is staging an update");
            Hd.Stage = MCStage::Failed;
            Hd.Error = "another copy of the game is installing an update";
            return;
        }

        SDL_Log("Updater: the HD pack is %s", Hd.Chosen ? "coming" : "going");
        Step = MCStep::ChangingSets;
        Hd.Stage = MCStage::Downloading;
        RunWork("the HD pack's change",
                [setup = *Content, makeSource = MakeSource](MCUpdateWork::MCContext& context)
                {
                    return MCUpdateWork::WithSource<std::optional<MCOffer>>(
                        makeSource, context.Stop,
                        [&](MCContentStage::MCSource& source) -> MCWorkResult
                        {
                            const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
                            auto change = MCContentUpdater::CheckSets(setup, *disk, source);

                            if (!change || !*change)
                            {
                                return change;
                            }

                            context.Progress.Set(0, (*change)->ContentBytes());

                            if (const auto staged =
                                    MCContentUpdater::Stage(setup, *disk, source, **change,
                                                            MCUpdateWork::Reporter(context.Progress, context.Stop));
                                !staged)
                            {
                                return std::unexpected(staged.error());
                            }

                            if (const auto armed = MCContentUpdater::Arm(setup, *disk, **change); !armed)
                            {
                                return std::unexpected(armed.error());
                            }

                            return change;
                        });
                });
    }

    /// <summary>
    /// Nothing more for an update this run: a change of sets the player chose and the copy lacks starts, as the start's
    /// check found no update to carry it (one chosen in an earlier run, stopped as the game quit).
    /// </summary>
    void Settle()
    {
        Step = MCStep::Idle;

        if (Hd.Managed && Hd.Chosen != Hd.Installed && Hd.Stage != MCStage::Failed)
        {
            StartSetsChange();
        }
    }

    /// <summary>The change of sets' end: armed for the next start, nothing to change, stopped for a new choice, or failed.</summary>
    void SetsDone(const std::string& error, bool armed)
    {
        StagingLock.reset();
        Step = MCStep::Idle;
        const bool again = std::exchange(SetsChosenAgain, false);
        Hd.Stopping = false;

        if (armed)
        {
            // A new choice that came too late goes in after it, at a later start.
            SDL_Log("Updater: the HD pack's change goes in at the next start");
            Step = MCStep::SetsArmed;
            Hd.Stage = MCStage::Ready;
            Hd.Received = Hd.Expected;
            return;
        }

        if (again)
        {
            StartSetsChange();
            return;
        }

        if (!error.empty())
        {
            SDL_Log("Updater: the HD pack's change failed: %s", error.c_str());
            Hd.Stage = MCStage::Failed;
            Hd.Error = Printable(error, MaxErrorLength, false);
            return;
        }

        Hd.Stage = MCStage::None;
    }

    /// <summary>The content's check, on the work's thread: release.json read and the content planned.</summary>
    void LookForUpdate()
    {
        Step = MCStep::CheckingContent;
        RunWork("the update's check",
                [setup = *Content, makeSource = MakeSource](MCUpdateWork::MCContext& context)
                {
                    return MCUpdateWork::WithSource<std::optional<MCOffer>>(
                        makeSource, context.Stop, [&](MCContentStage::MCSource& source)
                        { return MCContentUpdater::Check(setup, *MakeDisk(), source); });
                });
    }

    /// <summary>
    /// The update shown is looked for again (the sets chosen changed, so its content is planned again): the page shows
    /// nothing until it's back. The program's backend lets its update go first; it's found again with the check.
    /// </summary>
    void LookAgain()
    {
        const bool program = Offer->ProgramBuild.has_value() && Backend != nullptr;
        Offer.reset();
        Current = {};
        Changed();
        LookForUpdate();

        if (program)
        {
            ProgramClosing = true;
            Backend->Answer(MCAnswer::Later);
        }
    }

    /// <summary>The download a match's mission stopped goes on from where it was (its staged files kept).</summary>
    void Resume()
    {
        const MCStep resume = std::exchange(PausedStep, MCStep::Idle);
        SDL_Log("Updater: the mission is over: the download goes on");

        if (resume == MCStep::StagingContent)
        {
            StartStaging(false);
        }
        else
        {
            // Its staging lock is still this copy's: taken again for the sets' change.
            StagingLock.reset();
            StartSetsChange();
        }
    }

    /// <summary>The check's end: nothing, an update to show, or one to look for the program of first.</summary>
    void CheckDone(const std::string& error, std::optional<MCOffer> offer)
    {
        Step = MCStep::Idle;

        if (!error.empty())
        {
            // No news for the player (offline, say): the next start looks again. A change of sets the player chose
            // still tries, and says so if it fails too.
            SDL_Log("Updater: the check failed: %s", error.c_str());
            Settle();
            return;
        }

        if (!offer)
        {
            SDL_Log("Updater: nothing newer than %s", Content->RunningVersion.c_str());
            Settle();
            return;
        }

        Offer = std::move(offer);

        if (Offer->Quiet())
        {
            // Nothing to fetch or show: the record catches up with the release at the next start.
            if (!TakeStagingLock())
            {
                SDL_Log("Updater: another copy of the game is staging an update");
                Offer.reset();
                return;
            }

            Step = MCStep::ArmingQuietly;
            StartStaging(true);
            return;
        }

        if (Offer->ProgramBuild)
        {
            if (Backend == nullptr)
            {
                SDL_Log("Updater: release %llu needs build %llu, which this build can't install",
                        static_cast<unsigned long long>(Offer->Release.Number),
                        static_cast<unsigned long long>(*Offer->ProgramBuild));
                Offer.reset();
                Settle();
                return;
            }

            Step = MCStep::CheckingProgram;

            if (!ProgramClosing)
            {
                CheckProgram();
            }

            return;
        }

        ShowOffer();
    }

    /// <summary>Staging's end: on to the program's download, or armed when there's none.</summary>
    void StagingDone(const std::string& error)
    {
        if (!error.empty())
        {
            if (Offer->ProgramBuild && Backend != nullptr)
            {
                Backend->Answer(MCAnswer::Later);
            }

            Fail(error);
            return;
        }

        if (Offer->ProgramBuild)
        {
            Step = MCStep::DownloadingProgram;
            Backend->Answer(MCAnswer::Install);
            return;
        }

        ArmContent();
    }

    /// <summary>The quiet update's end.</summary>
    void QuietDone(const std::string& error)
    {
        if (error.empty())
        {
            SDL_Log("Updater: release %llu goes in at the next start (no file changes)",
                    static_cast<unsigned long long>(Offer->Release.Number));
        }
        else
        {
            SDL_Log("Updater: release %llu's record can't catch up: %s",
                    static_cast<unsigned long long>(Offer->Release.Number), error.c_str());
        }

        StagingLock.reset();
        Offer.reset();
        Settle();
    }

    /// <summary>The player's answer, with the content's updater set.</summary>
    void AnswerContent(MCAnswer answer)
    {
        if (Current.Stage == MCStage::Found && Step == MCStep::Offered && answer == MCAnswer::Install)
        {
            SDL_Log("Updater: install at %s", Current.Version.c_str());
            Current.Stage = MCStage::Downloading;
            Current.Received = 0;
            Current.Expected = DownloadBytes();

            // The program alone too: the lock keeps another copy from downloading the same setup at once.
            if (!TakeStagingLock())
            {
                if (Offer->ProgramBuild)
                {
                    Backend->Answer(MCAnswer::Later);
                }

                Fail("another copy of the game is installing it");
                return;
            }

            if (Offer->Content == nullptr)
            {
                Step = MCStep::DownloadingProgram;
                Changed();
                Backend->Answer(MCAnswer::Install);
                return;
            }

            Step = MCStep::StagingContent;
            Changed();
            StartStaging(false);
        }
        else if (Current.Stage == MCStage::Found && Step == MCStep::Offered && answer == MCAnswer::Skip)
        {
            SDL_Log("Updater: skip at %s", Current.Version.c_str());
            const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();

            if (const auto skipped = MCContentUpdater::Skip(*Content, *disk, *Offer); !skipped)
            {
                SDL_Log("Updater: the skip isn't kept: %s", skipped.error().c_str());
            }

            if (Offer->ProgramBuild)
            {
                Backend->Answer(MCAnswer::Later);
            }

            Offer.reset();
            Current = {};
            Changed();
            Settle();
        }
        else if (Current.Stage == MCStage::Ready && Step == MCStep::Armed && answer == MCAnswer::Install)
        {
            SDL_Log("Updater: restart into %s", Current.Version.c_str());
            Step = MCStep::Finished;
            Current.Stage = MCStage::Installing;
            Changed();

            // The program's installer quits the game and starts the new version, whose start swaps the content in;
            // with no program, the game quits and starts again itself.
            if (Offer->ProgramBuild)
            {
                Backend->Answer(MCAnswer::Install);
            }
            else
            {
                RestartAfterQuit = true;
                QuitGame();
            }
        }
    }
}

auto MCUpdater::SetBackend(std::unique_ptr<MCBackend> backend) -> void
{
    ResetContent();
    Backend = std::move(backend);
    Current = {};
    CheckedAtStart = false;
    Changed();
}

auto MCUpdater::SetContent(const MCContentUpdater::MCSetup& setup, MCSourceMaker source) -> void
{
    ResetContent();
    Content = setup;
    MakeSource = std::move(source);
    Current = {};
    CheckedAtStart = false;
    ReadHd();
    Changed();
}

auto MCUpdater::ClearContent() -> void
{
    ResetContent();
    Content.reset();
    MakeSource = nullptr;
    Current = {};
    CheckedAtStart = false;
    Changed();
}

auto MCUpdater::SetQuit(std::function<void()> quit) -> void
{
    QuitGame = quit ? std::move(quit) : InputQuit;
}

auto MCUpdater::Working() -> bool
{
    return Work != nullptr;
}

auto MCUpdater::Frame() -> void
{
    if (Backend != nullptr)
    {
        Backend->Frame();
    }

    if (Work == nullptr)
    {
        return;
    }

    if (!Work->Finished())
    {
        const auto [received, expected] = Work->Progress().Get();

        // Staging's whole is the plan's until its first bytes say more (a patch that fell back on its zip).
        if (Step == MCStep::StagingContent && Offer)
        {
            ShowProgress(received,
                         std::max(expected, Offer->ContentBytes()) + (Offer->ProgramBuild ? Offer->ProgramBytes : 0));
        }
        else if (Step == MCStep::ChangingSets)
        {
            Hd.Received = received;
            Hd.Expected = expected;
        }

        return;
    }

    MCWorkResult ended = Work->Join();
    Work.reset();
    const std::string error = ended ? std::string() : std::move(ended.error());
    std::optional<MCOffer> offer = ended ? std::move(*ended) : std::nullopt;

    // Stopped for a match's mission: it goes on once the mission is over (at once, if it already is). Done just as the
    // mission began, it ends as it would have.
    if (PausedStep != MCStep::Idle && !error.empty())
    {
        if (!Paused)
        {
            Resume();
            return;
        }

        SDL_Log("Updater: the download waits for the mission to end (%s)", error.c_str());
        return;
    }

    PausedStep = MCStep::Idle;

    switch (Step)
    {
        case MCStep::CheckingContent:
            CheckDone(error, std::move(offer));
            break;
        case MCStep::StagingContent:
            StagingDone(error);
            break;
        case MCStep::ArmingQuietly:
            QuietDone(error);
            break;
        case MCStep::ChangingSets:
            SetsDone(error, offer.has_value());
            break;
        default:
            break;
    }
}

auto MCUpdater::PauseWork(bool paused) -> void
{
    if (paused)
    {
        // Each frame: a download that began during the mission (the start's check ending in the HD pack's change)
        // stops too. Only downloads stop: a check or the quiet update's record is done in a moment.
        Paused = true;

        if (Work != nullptr && PausedStep == MCStep::Idle &&
            (Step == MCStep::StagingContent || Step == MCStep::ChangingSets))
        {
            SDL_Log("Updater: the download stops while the mission plays");
            PausedStep = Step;
            Work->Stop();
        }

        return;
    }

    if (!std::exchange(Paused, false))
    {
        return;
    }

    // Still stopping, its end goes on with it (Frame).
    if (PausedStep != MCStep::Idle && Work == nullptr)
    {
        Resume();
    }
}

auto MCUpdater::Quit() -> void
{
    // Its transfer cancelled, work under way ends at once (staged files stay for the next try).
    StopWork();
    StagingLock.reset();

    if (Backend != nullptr)
    {
        Backend->Quit();
    }
}

auto MCUpdater::RestartWanted() -> bool
{
    return RestartAfterQuit;
}

auto MCUpdater::Available() -> bool
{
    return Backend != nullptr || Content.has_value();
}

auto MCUpdater::RunningVersion() -> std::string
{
    if (Content)
    {
        return Content->RunningVersion;
    }

    return Backend != nullptr ? Backend->RunningVersion() : std::string();
}

auto MCUpdater::ExpectedBuild() -> std::optional<uint64_t>
{
    if (Content)
    {
        return Offer && Offer->ProgramBuild ? *Offer->ProgramBuild : Content->RunningBuild;
    }

    // The backend's version as shown, "0.2.2 (480)": the build in its brackets.
    const std::string& shown = Current.Version;
    const size_t open = shown.rfind('(');

    if (open == std::string::npos || !shown.ends_with(')'))
    {
        return std::nullopt;
    }

    const char* const last = shown.data() + shown.size() - 1;
    uint64_t build = 0;
    const auto [end, error] = std::from_chars(shown.data() + open + 1, last, build);
    return error == std::errc() && end == last ? std::optional<uint64_t>(build) : std::nullopt;
}

auto MCUpdater::CheckAtStart() -> void
{
    if (!Available() || CheckedAtStart)
    {
        return;
    }

    CheckedAtStart = true;
    SDL_Log("Updater: looking for a newer version than %s", RunningVersion().c_str());

    if (!Content)
    {
        Backend->Check(std::nullopt);
        return;
    }

    LookForUpdate();
}

auto MCUpdater::HdPack() -> const MCHdPack&
{
    switch (Step)
    {
        case MCStep::CheckingContent:
        case MCStep::CheckingProgram:
        case MCStep::ArmingQuietly:
            Hd.Hold = MCHdHold::Looking;
            break;
        case MCStep::StagingContent:
        case MCStep::DownloadingProgram:
            Hd.Hold = MCHdHold::UpdateDownloading;
            break;
        case MCStep::Armed:
            Hd.Hold = MCHdHold::UpdateReady;
            break;
        case MCStep::Finished:
            Hd.Hold = Current.Stage == MCStage::Failed ? MCHdHold::None : MCHdHold::UpdateReady;
            break;
        case MCStep::SetsArmed:
            Hd.Hold = MCHdHold::NextStart;
            break;
        default:
            Hd.Hold = MCHdHold::None;
            break;
    }

    Hd.WithUpdate = Step == MCStep::Offered && Hd.Chosen != Hd.Installed && Offer && Offer->Content != nullptr;
    return Hd;
}

auto MCUpdater::ChooseHd(bool chosen) -> void
{
    if (!Content || !Hd.Managed || (chosen == Hd.Chosen && Hd.Stage != MCStage::Failed))
    {
        return;
    }

    const std::unique_ptr<MCUpdateDisk> disk = MakeDisk();
    const auto installed = MCInstalled::Read(Content->GameFolder);
    MCContentUpdater::MCSets sets =
        MCContentUpdater::Wanted(*disk, Content->GameFolder, installed.value_or(MCInstalled::MCRecord{}));

    if (chosen)
    {
        sets.emplace(MCContentList::HdSet);
    }
    else
    {
        sets.erase(std::string(MCContentList::HdSet));
    }

    if (const auto kept = MCContentUpdater::Choose(*disk, Content->GameFolder, sets); !kept)
    {
        SDL_Log("Updater: the HD pack's choice isn't kept: %s", kept.error().c_str());
        Hd.Stage = MCStage::Failed;
        Hd.Error = Printable(kept.error(), MaxErrorLength, false);
        return;
    }

    Hd.Chosen = chosen;

    switch (Step)
    {
        case MCStep::Idle:
            StartSetsChange();
            break;
        case MCStep::Finished:
        {
            // After a failed update this run; one installing goes in as the game quits, the choice at a later start.
            if (Current.Stage == MCStage::Failed)
            {
                StartSetsChange();
            }

            break;
        }
        case MCStep::Offered:
            LookAgain();
            break;
        case MCStep::ChangingSets:
        {
            // Stopped where it is (staged files stay); its end plans the new choice.
            SetsChosenAgain = true;
            Hd.Stopping = true;

            if (Work != nullptr)
            {
                Work->Stop();
            }

            break;
        }
        default:
            // An update's work comes first: the choice is planned at a later start.
            break;
    }
}

auto MCUpdater::RestartForHd() -> void
{
    if (Step != MCStep::SetsArmed)
    {
        return;
    }

    // The new copy waits for this one's lock as it ends, then its start swaps the change in.
    SDL_Log("Updater: restart for the HD pack's change");
    RestartAfterQuit = true;
    QuitGame();
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
    if (!Available())
    {
        return;
    }

    if (Content)
    {
        AnswerContent(answer);
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
    // With the content's updater, the version and notes are release.json's, and the program found is the build it
    // gave: the update shows once the program is known to be out.
    if (Content)
    {
        if (Step == MCStep::CheckingProgram)
        {
            ShowOffer();
        }

        return;
    }

    Current = {.Stage = MCStage::Found,
               .Version = Printable(version, MaxVersionLength, false),
               .Notes = Printable(notes, MaxNotesLength, true)};
    SDL_Log("Updater: %s is out", Current.Version.c_str());
    Changed();
}

auto MCUpdater::ReleaseNotes(std::string notes) -> void
{
    // Notes coming after a skip have no update to go with; release.json's notes are the content's updater's.
    if (Current.Stage == MCStage::None || Content)
    {
        return;
    }

    Current.Notes = Printable(notes, MaxNotesLength, true);
    Changed();
}

auto MCUpdater::Downloading(uint64_t received, uint64_t expected) -> void
{
    if (Content)
    {
        // The content is in by now: the program's bytes count after it, out of release.json's whole.
        if (Step == MCStep::DownloadingProgram && Offer)
        {
            ShowProgress(Offer->ContentBytes() + received, DownloadBytes());
        }

        return;
    }

    Current.Stage = MCStage::Downloading;
    Current.Received = received;
    Current.Expected = expected;
    Changed();
}

auto MCUpdater::Ready() -> void
{
    if (Content)
    {
        if (Step == MCStep::DownloadingProgram)
        {
            ArmContent();
        }

        return;
    }

    SDL_Log("Updater: %s is downloaded", Current.Version.c_str());
    Current.Stage = MCStage::Ready;
    Changed();
}

auto MCUpdater::Installing() -> void
{
    if (Content)
    {
        // Sparkle can install without its ready step (an update it downloaded in an earlier run): the content goes in
        // with it. Content that can't be armed leaves the update failed: with no step waiting for an answer, Sparkle
        // installs the program alone, and the new build's start looks for the content again.
        if (Step == MCStep::DownloadingProgram && !ArmContent())
        {
            SDL_Log("Updater: %s's program installs without its content (%s)", Current.Version.c_str(),
                    Current.Error.c_str());
            return;
        }

        Step = MCStep::Finished;
    }

    Current.Stage = MCStage::Installing;
    Changed();
}

auto MCUpdater::Failed(std::string error) -> void
{
    // The check itself failing (offline, say) is no news for the player: the next start looks again.
    if (Current.Stage == MCStage::None)
    {
        SDL_Log("Updater: the check failed: %s", error.c_str());

        if (Content && Step == MCStep::CheckingProgram)
        {
            Offer.reset();
            Settle();
        }

        return;
    }

    if (Content)
    {
        // A late report while the HD pack's change is the work this run (after a failed update) fails nothing.
        if (Step == MCStep::ChangingSets || Step == MCStep::SetsArmed)
        {
            SDL_Log("Updater: the program's update failed after it was given up: %s", error.c_str());
            return;
        }

        Fail(error);
        return;
    }

    SDL_Log("Updater: %s failed: %s", Current.Version.c_str(), error.c_str());
    Current.Stage = MCStage::Failed;
    Current.Error = Printable(error, MaxErrorLength, false);
    Changed();
}

auto MCUpdater::Closed() -> void
{
    if (Content)
    {
        // The session LookAgain let go has ended: the look for the program that waited for it goes ahead.
        if (ProgramClosing)
        {
            ProgramClosing = false;

            if (Step == MCStep::CheckingProgram)
            {
                CheckProgram();
            }

            return;
        }

        // Looking for release.json's build and closing without it: the program isn't out yet. Closing while the update
        // is shown or its program downloads, it can't install: the page says so rather than wait for it.
        if (Step == MCStep::CheckingProgram)
        {
            SDL_Log("Updater: build %llu isn't out yet",
                    static_cast<unsigned long long>(Offer ? Offer->ProgramBuild.value_or(0) : 0));
            Offer.reset();
            Settle();
        }
        else if (Step == MCStep::Offered || Step == MCStep::DownloadingProgram)
        {
            Fail("the program's update stopped");
        }

        return;
    }

    // Only an update not yet downloaded goes: one ready or installing installs when the game quits (Sparkle ends its
    // session once the install is armed), and a failure stays shown until the next start.
    if (Current.Stage != MCStage::Found && Current.Stage != MCStage::Downloading)
    {
        return;
    }

    Current = {};
    Changed();
}

#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "fakes/MCNullAudioDevice.h"
#include "MCScratchFolder.h"
#include "camera/MCCamera.h"
#include "lib/MCFirstUse.h"
#include "main/MCGameContext.h"
#include "main/MCHdSettings.h"
#include "mission/MCStagedBattle.h"
#include "mission/MCTrailerMission.h"
#include "object/MCArtillery.h"
#include "object/MCCommander.h"
#include "object/MCForces.h"
#include "object/MCMover.h"
#include "platform/MCDisplay.h"
#include "platform/MCFrameLog.h"
#include "platform/MCInput.h"
#include "platform/MCMemoryLedger.h"
#include "platform/MCMemoryRoom.h"
#include "platform/MCRenderer.h"
#include "sound/MCHdAudio.h"
#include "sprite/MCHdArt.h"

// Port-only (the workspace's SPEC-first-use.md, its guard: lib/MCFirstUse.h): once a mission's load is over, the
// game's thread reads, unpacks and waits on nothing for the first time. The guard's tests play a mission's battle as a
// player's game would, with the HD settings a player's are, and fail on any first use the game's thread made in play;
// the tool (--first-uses) plays any mission and lists them, each with its cost.

namespace
{
    /// <summary>A frame of a mission played as the game plays it (15 a second).</summary>
    constexpr float PlayFrameSeconds = 1.0f / 15.0f;
    /// <summary>A frame of a staged battle (MCBattleSpec::FramesPerSecond).</summary>
    constexpr float BattleFrameSeconds = 1.0f / MCBattleSpec::FramesPerSecond;
    /// <summary>How often the player's units are sent at their nearest enemies again, in frames of play.</summary>
    constexpr int32_t OrderEveryFrames = 45;
    /// <summary>When the player's strikes are called, in frames of play (once the fight has begun).</summary>
    constexpr int32_t StrikesAtFrame = 450;
    /// <summary>Bytes in a megabyte, for the output.</summary>
    constexpr double BytesPerMb = 1024.0 * 1024.0;
    /// <summary>A player's window: the view sees as much of the map as at 1080p.</summary>
    constexpr int PlayerWindowWidth = 1920;
    constexpr int PlayerWindowHeight = 1080;
    /// <summary>The strike kinds a commander keeps a count of (CallArtillery's 0-3: small, large, sensor, camera).</summary>
    constexpr int32_t StrikeKinds = 4;
    /// <summary>The first uses a failing guard lists.</summary>
    constexpr size_t ListedUses = 40;

    /// <summary>
    /// Boots mission <paramref name="mission"/> as a player's game: a player's window, the HD settings a player's are
    /// (the app's defaults) set before the load, the pack <paramref name="pack"/> (none: the original's art alone), and
    /// HD audio into a device that plays nothing.
    /// </summary>
    void StartAsPlayer(MCTestContextScope& scope, int32_t mission, const std::filesystem::path& pack)
    {
        scope.Context().SetAudio(std::make_unique<MCNullAudioDevice>());
        MCTestGame::BootOptions.Sound = true;
        MCTestGame::BootOptions.WindowWidth = PlayerWindowWidth;
        MCTestGame::BootOptions.WindowHeight = PlayerWindowHeight;
        MCTestGame::BootOptions.FixedZoomHeight = 0.0f;
        MCTestGame::BootOptions.Started = [pack]
        {
            UseHdByDefault();
            MCHdSettings::UseDefaults();

            if (pack.empty())
            {
                GHdArt = 0;
                GHdAudio = HdAudioOff;
            }

            MCHdArt::SetPackFolder(pack);
        };

        MCRenderer::RequestGpuDrawing(MCGpuDrawing::On);
        REQUIRE(MCTestGame::StartMission(mission));
    }

    /// <summary>Whether <paramref name="mover"/> fights against the player (not the player's team nor its allies).</summary>
    bool PlayersEnemy(MCMover& mover)
    {
        const std::shared_ptr<MCTeam> team = mover.GetTeam();
        return team != nullptr && team != HomeTeam() && team != AlliedTeam();
    }

    /// <summary>The live movers of the player's team, and the live movers fighting it.</summary>
    std::pair<std::vector<std::shared_ptr<MCMover>>, std::vector<std::shared_ptr<MCMover>>> Sides()
    {
        std::vector<std::shared_ptr<MCMover>> players;
        std::vector<std::shared_ptr<MCMover>> enemies;

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            if (std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
                mover != nullptr && mover->IsDisabled() == 0)
            {
                if (mover->GetTeam() == HomeTeam())
                {
                    players.push_back(std::move(mover));
                }
                else if (PlayersEnemy(*mover))
                {
                    enemies.push_back(std::move(mover));
                }
            }
        }

        return {std::move(players), std::move(enemies)};
    }

    /// <summary>The square of the ground distance between two movers.</summary>
    float DistanceSquared(MCMover& one, MCMover& other)
    {
        const MCVector3D a = one.GetPosition();
        const MCVector3D b = other.GetPosition();
        return (a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y);
    }

    /// <summary>
    /// The player's units each sent at the enemy nearest it, the camera on the first of them; false when either side
    /// has no one left standing.
    /// </summary>
    bool SendPlayersAtNearest()
    {
        const auto [players, enemies] = Sides();

        if (players.empty() || enemies.empty())
        {
            return false;
        }

        for (const std::shared_ptr<MCMover>& player : players)
        {
            const auto nearest = std::ranges::min_element(enemies, {}, [&](const std::shared_ptr<MCMover>& enemy)
                                                          { return DistanceSquared(*player, *enemy); });
            MCStaging::Attack(*player, *nearest);
        }

        Eye()->ChangeTarget(players.front(), true);
        return true;
    }

    /// <summary>Each kind of strike the player has left called once, on the enemy nearest the player's first unit.</summary>
    void CallPlayersStrikes()
    {
        const auto [players, enemies] = Sides();
        const std::shared_ptr<MCCommander> commander = HomeCommander();

        if (players.empty() || enemies.empty() || commander == nullptr)
        {
            return;
        }

        const auto nearest = std::ranges::min_element(enemies, {}, [&](const std::shared_ptr<MCMover>& enemy)
                                                      { return DistanceSquared(*players.front(), *enemy); });
        const std::array<int32_t, StrikeKinds> left = {commander->NumSmallStrikes, commander->NumLargeStrikes,
                                                       commander->NumSensorStrikes, commander->NumCameraDrones};

        for (int32_t kind = 0; kind < StrikeKinds; ++kind)
        {
            if (left.at(static_cast<size_t>(kind)) > 0)
            {
                CallArtillery(commander->Id, kind, (*nearest)->GetPosition(), -1, 0);
            }
        }
    }

    /// <summary>
    /// The HD art's memory on the CPU through a play, taken after each frame: its frames' records and colours, those
    /// let go of that something still holds, those read ahead and the unpacked streams kept (not the hold's, which a
    /// mission holds whole); and the most mechs up at once, for its cost per mech.
    /// </summary>
    struct HdMemory
    {
        static constexpr std::array Holders{MCMemoryLedger::MCHolder::HdFrames, MCMemoryLedger::MCHolder::HdColors,
                                            MCMemoryLedger::MCHolder::HdFramesLetGo,
                                            MCMemoryLedger::MCHolder::HdReadAhead,
                                            MCMemoryLedger::MCHolder::HdKeptStreams};

        int64_t Peak = 0;
        double Sum = 0.0;
        int64_t Samples = 0;
        size_t Mechs = 0;

        void Sample()
        {
            int64_t bytes = 0;

            for (const MCMemoryLedger::MCHolder holder : Holders)
            {
                bytes += MCMemoryLedger::Of(holder).Payload;
            }

            Peak = std::max(Peak, bytes);
            Sum += static_cast<double>(bytes);
            ++Samples;
            size_t mechs = 0;

            for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
            {
                const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
                mechs += mover != nullptr && mover->ObjectClass == MCObjectClass::BattleMech && mover->IsDisabled() == 0
                             ? 1
                             : 0;
            }

            Mechs = std::max(Mechs, mechs);
        }

        double Mean() const { return Samples > 0 ? Sum / static_cast<double>(Samples) : 0.0; }
    };

    /// <summary>
    /// Plays the mission up for <paramref name="frames"/> frames as a player would: the player's units sent at their
    /// nearest enemies every few seconds, the camera on the fight, the player's strikes called once the fight is on.
    /// Ends early once a side has no one left.
    /// </summary>
    /// <returns>The frames played.</returns>
    int32_t PlayBattle(int32_t frames, HdMemory& memory)
    {
        int32_t frame = 0;

        for (; frame < frames; ++frame)
        {
            if (frame % OrderEveryFrames == 0 && !SendPlayersAtNearest())
            {
                break;
            }

            if (frame == StrikesAtFrame)
            {
                CallPlayersStrikes();
            }

            MCTestGame::RunFrame(PlayFrameSeconds);
            memory.Sample();
        }

        return frame;
    }

    /// <summary>The trailer's canyon battle (MCBattleSpec::BuiltIn), staged for <paramref name="frames"/> frames.</summary>
    MCBattleSpec CanyonSpec(int32_t frames)
    {
        const std::optional<std::string_view> text = MCBattleSpec::BuiltIn("canyon");
        REQUIRE(text.has_value());
        std::expected<MCBattleSpec, std::string> spec = MCBattleSpec::Parse(*text);
        REQUIRE(spec.has_value());
        spec->Frames = frames;
        return std::move(*spec);
    }

    /// <summary>Plays the staged battle <paramref name="spec"/>, its mission up, for its frames.</summary>
    void PlayStaged(const MCBattleSpec& spec, HdMemory& memory)
    {
        MCStagedBattle battle(spec, [](MCBattleLine, std::string_view) {});
        const std::expected<void, std::string> begun = battle.Begin();
        REQUIRE(begun.has_value());

        for (int32_t frame = 0; frame < spec.Frames; ++frame)
        {
            battle.Stage(frame);
            MCTestGame::RunFrame(BattleFrameSeconds);
            battle.Staged(frame);
            memory.Sample();
        }
    }

    /// <summary>A first use as a line: its frame, kind, milliseconds, bytes and what.</summary>
    std::string UseLine(const MCFirstUse& use)
    {
        return std::format("{}\t{}\t{:.3f}\t{}\t{}", use.Frame, MCFirstUses::KindName(use.Kind), use.Ms, use.Bytes,
                           use.What);
    }

    /// <summary>
    /// A kind of first use in play still open, until the PLAN.md item named closes it: a use of its kind whose What holds
    /// its text. The guard passes these and fails on any other, so a new gap (an effect not loaded, a file read on first
    /// show) fails the suite at once. The list only shrinks: an item's entry goes when it lands.
    /// </summary>
    struct MCOpenFirstUse
    {
        MCFirstUseKind Kind;
        std::string_view Text;
        std::string_view Item;
    };

    constexpr auto OpenFirstUses = std::to_array<MCOpenFirstUse>({
        {MCFirstUseKind::Open, "sound/music", "music starts read on the game's thread"},
        {MCFirstUseKind::Read, "sound/music", "music starts read on the game's thread"},
        {MCFirstUseKind::Open, "stereo/MUSIC", "music starts read on the game's thread"},
        {MCFirstUseKind::Read, "stereo/MUSIC", "music starts read on the game's thread"},
        {MCFirstUseKind::Wait, "a frame being read: ", "HD art's reads late (step 5)"},
        {MCFirstUseKind::Wait, "an archive being opened: ", "HD art's reads late (step 5)"},
        {MCFirstUseKind::Unpack, ".hda ", "HD art's held entries unpacked on the game's thread (step 5)"},
        {MCFirstUseKind::Movie, "first frame: ", "HD movies' first frame waited for"},
        {MCFirstUseKind::Movie, "opened: ", "HD movies opened on the game's thread"},
    });

    /// <summary>Whether <paramref name="use"/> is of a kind still open (<see cref="OpenFirstUses"/>).</summary>
    bool StillOpen(const MCFirstUse& use)
    {
        return std::ranges::any_of(OpenFirstUses, [&use](const MCOpenFirstUse& open)
                                   { return open.Kind == use.Kind && use.What.contains(open.Text); });
    }

    /// <summary>
    /// The guard's check: no first use in play but those of kinds still open; else the first ListedUses of the others,
    /// each a line.
    /// </summary>
    void RequireNoFirstUses(const std::vector<MCFirstUse>& uses)
    {
        std::vector<MCFirstUse> gaps;
        std::ranges::copy_if(uses, std::back_inserter(gaps), [](const MCFirstUse& use) { return !StillOpen(use); });
        std::string listed;

        for (size_t i = 0; i < std::min(gaps.size(), ListedUses); ++i)
        {
            listed += "\n  " + UseLine(gaps.at(i));
        }

        if (!gaps.empty())
        {
            FAIL_CHECK(std::format("the game's thread's first uses in play, of no kind still open ({} in all, {} of "
                                   "kinds still open; frame, kind, ms, bytes, what):{}",
                                   gaps.size(), uses.size() - gaps.size(), listed));
        }

        CHECK(MCFirstUses::Dropped() == 0);
    }

    /// <summary>The pack the guard plays with: --hd-pack's (absolute: the game works in the install's folder), else none.</summary>
    std::filesystem::path GuardPack()
    {
        const std::optional<std::string_view> pack = MCTest::Option("hd-pack");
        return pack.has_value() ? std::filesystem::absolute(std::filesystem::path(*pack)) : std::filesystem::path();
    }

    /// <summary>The frames of play the guard's missions run: a minute of the game's.</summary>
    constexpr int32_t GuardPlayFrames = 900;
    /// <summary>The frames of the canyon's battle the guard stages: its first 40 seconds, the fight's first waves.</summary>
    constexpr int32_t GuardCanyonFrames = 1200;

    /// <summary>The guard: mission <paramref name="mission"/> played a minute as a player's, no first use in play.</summary>
    void GuardMission(int32_t mission)
    {
        if (!MCTestGame::Available())
        {
            return;
        }

        MCTestContextScope scope;
        StartAsPlayer(scope, mission, GuardPack());
        HdMemory memory;
        PlayBattle(GuardPlayFrames, memory);
        RequireNoFirstUses(MCFirstUses::Take());
    }
}

TEST_CASE_ISOLATED("game: in mission 1's battle the game's thread reads and unpacks nothing after the load")
{
    GuardMission(1);
}

TEST_CASE_ISOLATED("game: in mission 10's battle the game's thread reads and unpacks nothing after the load")
{
    GuardMission(10);
}

TEST_CASE_ISOLATED("game: in the canyon's battle the game's thread reads and unpacks nothing after the load")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCBattleSpec spec = CanyonSpec(GuardCanyonFrames);
    const std::expected<void, std::string> put = MCTrailerMission::Put(spec.Force);
    REQUIRE(put.has_value());
    MCTestContextScope scope;
    StartAsPlayer(scope, spec.Mission, GuardPack());
    HdMemory memory;
    PlayStaged(spec, memory);
    RequireNoFirstUses(MCFirstUses::Take());
}

/// <summary>
/// Tool (the first-use check, SPEC-first-use.md): <c>--first-uses &lt;file&gt;</c> plays <c>--mission &lt;n&gt;</c>
/// (default 1) as a player's game for <c>--first-uses-seconds &lt;s&gt;</c> of the game's time (default 240), or with
/// <c>--first-uses-battle canyon</c> the trailer's staged battle, with the HD pack <c>--hd-pack &lt;folder&gt;</c> (none:
/// the original's art), and appends each first use the game's thread made in play to the file, a line each: the
/// mission, the frame, the kind, its milliseconds, its bytes and what. Its count and cost go to the output, with the HD
/// art's memory at most and on average (HdMemory's) and the most mechs up at once. With
/// <c>--first-uses-framelog</c>, <c>--first-uses-trace</c> and <c>--first-uses-memory &lt;GB&gt;</c>: the run's frame log,
/// its HD trace, and its plan as a machine with that much memory; with <c>--first-uses-card &lt;MB&gt;</c>, drawn on a
/// card of its own whose driver gives the game that much (MCMemoryRoom::SetCardBytes: the GPU's use there the pictures'
/// alone, so they may take 90% of it), the output saying whether the whole mission went on the GPU; with <c>--first-uses-first-frames off</c>, a mech's
/// gestures to come read without the frames they start on sooner (MCHdArt::ReaderSettings::FirstFramesSoon), to
/// measure them.
/// </summary>
/// <summary>
/// Port-only (20 Nov, the A10: play's first two or three frames came one or two refreshes late, making the screen's
/// targets with the GPU still busy with the load): a mission's load draws play's first frames as its last, until one
/// makes none of the screen's targets, and waits for the GPU (MCMission's DrawPlaysFirstFrames), so play's frames make
/// no target of the screen's.
/// </summary>
TEST_CASE_ISOLATED("game: a mission's load draws play's first frames, so play's frames make no target of the screen's")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view DrawnNote = "play's first frames drawn under the bar: ";
    constexpr std::string_view StartedNote = "mission started";
    constexpr std::string_view MadeNote = "GPU texture made, ";
    constexpr int32_t FirstFramesMost = 4;
    const MCScratchFolder folder("mc_first_use_tests", "first-frames");
    const std::filesystem::path log = folder.Path() / "load.framelog";
    REQUIRE(MCFrameLog::Open(log));
    REQUIRE(MCTestGame::StartMission(1));
    // The test game's frames don't move the log's on: its first frame, which the notes so far go to, then written.
    MCFrameLog::NextFrame();
    MCFrameLog::NextFrame();
    MCFrameLog::Close();
    std::ifstream in(log);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    const size_t drawn = text.find(DrawnNote);
    const size_t started = text.find(StartedNote);
    REQUIRE(drawn != std::string::npos);
    REQUIRE(started != std::string::npos);
    CHECK(drawn < started);
    int32_t frames = 0;
    REQUIRE_EQ(std::sscanf(text.c_str() + drawn + DrawnNote.size(), "%d", &frames), 1);
    CHECK(frames >= 1);
    CHECK(frames <= FirstFramesMost);
    std::cout << "  " << text.substr(drawn, text.find('\n', drawn) - drawn) << "\n";

    // The screen's targets (the window's pixels): made under the bar, none made once play shows its frames.
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    int width = 0;
    int height = 0;
    REQUIRE(SDL_GetWindowSizeInPixels(&display->Window(), &width, &height));
    const std::string screen = std::format("{}{}x{}\n", MadeNote, width, height);
    const size_t made = text.find(screen);
    CHECK(made < drawn);
    CHECK(text.find(screen, started) == std::string::npos);
}

TEST_CASE_ISOLATED("tool: --first-uses plays a mission as a player's and lists the game's thread's first uses")
{
    const std::optional<std::string_view> out = MCTest::Option("first-uses");

    if (!out.has_value() || !MCTestGame::Available())
    {
        return;
    }

    int32_t mission = 1;
    int32_t seconds = 240;

    if (const std::optional<std::string_view> given = MCTest::Option("mission"); given.has_value())
    {
        std::from_chars(given->data(), given->data() + given->size(), mission);
    }

    if (const std::optional<std::string_view> given = MCTest::Option("first-uses-seconds"); given.has_value())
    {
        std::from_chars(given->data(), given->data() + given->size(), seconds);
    }

    const bool canyon = MCTest::Option("first-uses-battle") == std::optional<std::string_view>("canyon");
    const MCBattleSpec spec = CanyonSpec(seconds * MCBattleSpec::FramesPerSecond);

    if (canyon)
    {
        const std::expected<void, std::string> put = MCTrailerMission::Put(spec.Force);
        REQUIRE(put.has_value());
        mission = spec.Mission;
    }

    // --first-uses-framelog <file>: the frame log of the run (the load's "original packets" note, the memory ledger).
    if (const std::optional<std::string_view> log = MCTest::Option("first-uses-framelog"); log.has_value())
    {
        REQUIRE(MCFrameLog::Open(std::filesystem::path(*log)));
    }

    // --first-uses-trace <file>: the HD trace of the run (MCHdArt::OpenTrace: each read asked, each frame's first use).
    if (const std::optional<std::string_view> trace = MCTest::Option("first-uses-trace"); trace.has_value())
    {
        REQUIRE(MCHdArt::OpenTrace(std::filesystem::path(*trace)));
    }

    // --first-uses-memory <GB>: planned as a machine with that much memory (MCMemoryRoom::SetMachineBytes).
    if (const std::optional<std::string_view> gb = MCTest::Option("first-uses-memory"); gb.has_value())
    {
        constexpr int BytesPerGbShift = 30;
        uint64_t planned = 0;
        std::from_chars(gb->data(), gb->data() + gb->size(), planned);
        REQUIRE(planned > 0);
        MCMemoryRoom::SetMachineBytes(planned << BytesPerGbShift);
    }

    // --first-uses-card <MB>: drawn on a card of its own whose driver gives the game that much.
    if (const std::optional<std::string_view> mb = MCTest::Option("first-uses-card"); mb.has_value())
    {
        constexpr int BytesPerMbShift = 20;
        uint64_t card = 0;
        std::from_chars(mb->data(), mb->data() + mb->size(), card);
        REQUIRE(card > 0);
        MCMemoryRoom::SetCardBytes(card << BytesPerMbShift);
    }

    // --first-uses-first-frames off: a mech's gestures to come read without the frames they start on sooner, to measure
    // them.
    if (MCTest::Option("first-uses-first-frames") == std::optional<std::string_view>("off"))
    {
        MCHdArt::ReaderSettings settings = MCHdArt::GetReaderSettings();
        settings.FirstFramesSoon = false;
        MCHdArt::SetReaderSettings(settings);
    }

    MCTestContextScope scope;
    const auto started = std::chrono::steady_clock::now();
    StartAsPlayer(scope, mission, GuardPack());
    const double loadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    int32_t played = 0;
    HdMemory memory;

    if (canyon)
    {
        PlayStaged(spec, memory);
        played = spec.Frames;
    }
    else
    {
        played = PlayBattle(static_cast<int32_t>(std::lround(static_cast<float>(seconds) / PlayFrameSeconds)), memory);
    }

    const std::vector<MCFirstUse> uses = MCFirstUses::Take();
    std::ofstream file(std::string(*out), std::ios::app);
    REQUIRE(file.good());
    double ms = 0.0;

    for (const MCFirstUse& use : uses)
    {
        file << mission << '\t' << UseLine(use) << '\n';
        ms += use.Ms;
    }

    const auto [players, enemies] = Sides();
    const MCHdArt::GpuFill fill = MCHdArt::GpuFillNow();
    std::cout << std::format(
        "  mission {}: up in {:.1f} s, {} frames played ({} of the player's and {} enemies left), "
        "{} first uses in play ({:.1f} ms), {} past the list; the HD art's memory at most "
        "{:.1f} MB (on average {:.1f}), {} mechs up at most; the mission on the GPU {} ({:.1f} MB; the fill asked {} "
        "frames and put {} up)\n",
        mission, loadSeconds, played, players.size(), enemies.size(), uses.size(), ms, MCFirstUses::Dropped(),
        static_cast<double>(memory.Peak) / BytesPerMb, memory.Mean() / BytesPerMb, memory.Mechs,
        fill.Fits ? "whole" : "not whole", static_cast<double>(fill.MissionBytes) / BytesPerMb, fill.Asked, fill.Up);
    MCFrameLog::Close();
    MCHdArt::CloseTrace();
    MCMemoryRoom::SetMachineBytes(std::nullopt);
    MCMemoryRoom::SetCardBytes(std::nullopt);
}

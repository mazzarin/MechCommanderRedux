#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "fakes/MCNullAudioDevice.h"
#include "MCScratchFolder.h"
#include "camera/MCCamera.h"
#include "lib/MCFirstUse.h"
#include "main/MCGameContext.h"
#include "main/MCHdSettings.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "mission/MCStagedBattle.h"
#include "mission/MCTrailerMission.h"
#include "object/MCArtillery.h"
#include "object/MCCommander.h"
#include "object/MCForces.h"
#include "object/MCMover.h"
#include "platform/MCFileSystem.h"
#include "platform/MCFrameLog.h"
#include "platform/MCMemoryLedger.h"
#include "platform/MCMemoryRoom.h"
#include "platform/MCRenderer.h"
#include "sound/MCHdAudio.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCOpeningLists.h"

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

    /// <summary>The music's files, as their first uses name them (its own PLAN.md item).</summary>
    constexpr std::string_view MusicFiles = "sound/music";
    constexpr std::string_view StereoMusicFiles = "stereo/MUSIC";
    constexpr std::string_view MusicItem = "music starts read on the game's thread";
    /// <summary>
    /// The interface's HD line art and glyphs taken from the pack as each is first shown (MCSharpPictures::Take), its
    /// own item: about 4 us a picture, 0.1-0.2 ms in play's first frames (19 Nov; with the player's pack, 32-41 in
    /// play's first frame, 21 Nov).
    /// </summary>
    constexpr std::string_view SharpPictures = "sharp pictures ";
    constexpr std::string_view SharpPicturesItem = "the interface's line art taken from the pack as first shown";

    constexpr auto OpenFirstUses = std::to_array<MCOpenFirstUse>({
        {MCFirstUseKind::Unpack, SharpPictures, SharpPicturesItem},
        {MCFirstUseKind::Open, MusicFiles, MusicItem},
        {MCFirstUseKind::Read, MusicFiles, MusicItem},
        {MCFirstUseKind::Open, StereoMusicFiles, MusicItem},
        {MCFirstUseKind::Read, StereoMusicFiles, MusicItem},
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
    /// The first ListedUses of <paramref name="uses"/> but those <paramref name="passed"/> passes, each a line, failed as
    /// the game's thread's first uses <paramref name="when"/>.
    /// </summary>
    void RequireNoGaps(const std::vector<MCFirstUse>& uses, const std::function<bool(const MCFirstUse&)>& passed,
                       std::string_view when)
    {
        std::vector<MCFirstUse> gaps;
        std::ranges::copy_if(uses, std::back_inserter(gaps), [&passed](const MCFirstUse& use) { return !passed(use); });
        std::string listed;

        for (size_t i = 0; i < std::min(gaps.size(), ListedUses); ++i)
        {
            listed += "\n  " + UseLine(gaps.at(i));
        }

        if (!gaps.empty())
        {
            FAIL_CHECK(std::format("the game's thread's first uses {} ({} in all, {} passed; frame, kind, ms, bytes, "
                                   "what):{}",
                                   when, gaps.size(), uses.size() - gaps.size(), listed));
        }

        CHECK(MCFirstUses::Dropped() == 0);
    }

    /// <summary>
    /// The guard's check: no first use in play but those of kinds still open; else the first ListedUses of the others,
    /// each a line.
    /// </summary>
    void RequireNoFirstUses(const std::vector<MCFirstUse>& uses)
    {
        RequireNoGaps(uses, StillOpen, "in play, of no kind still open");
    }

    /// <summary>
    /// Port-only (SPEC-memory.md's "Under the loading bar": the opening view): the frames counted from play's start
    /// (MCFirstUse::Frame) whose draws the load prepared: the one play starts in and the two after it, the first drawn
    /// among them (the test game's start-up turns then run on, a mech's next gestures read ahead as in play).
    /// </summary>
    constexpr int32_t OpeningFrames = 3;

    /// <summary>A crater or footprint a unit makes in play, its own item (SPEC-first-use.md's craters, step5-fill).</summary>
    constexpr std::string_view Craters = "craters.hda ";

    /// <summary>
    /// Whether <paramref name="use"/> is one the opening's check passes: the music's start, an HD movie's (a pilot's
    /// video the mission opens with), the interface's line art or a crater made in play (each its own item), not the
    /// opening view's.
    /// </summary>
    bool OpeningPasses(const MCFirstUse& use)
    {
        const bool music = (use.Kind == MCFirstUseKind::Open || use.Kind == MCFirstUseKind::Read) &&
                           (use.What.contains(MusicFiles) || use.What.contains(StereoMusicFiles));
        const bool made = use.What.starts_with(SharpPictures) || use.What.contains(Craters);
        return music || use.Kind == MCFirstUseKind::Movie || (use.Kind == MCFirstUseKind::Unpack && made);
    }

    /// <summary>
    /// The opening's check: nothing in play's first frames read, unpacked, waited on, decoded or opened for the first
    /// time on the game's thread, not even of the kinds still open in play (HD art's held entries unpacked among them),
    /// the music's and HD movies' starts, the interface's line art and craters made in play aside (OpeningPasses); else
    /// the first ListedUses, each a line.
    /// </summary>
    void RequireOpeningUsesNothing(const std::vector<MCFirstUse>& uses)
    {
        RequireNoGaps(
            uses, [](const MCFirstUse& use) { return use.Frame >= OpeningFrames || OpeningPasses(use); },
            "in play's first frames, not the music's, movies', line art's or craters'");
    }

    /// <summary>The app's name for its player's folder, as SDL keeps it (Main.cpp's PlayerFolder).</summary>
    constexpr const char* PlayerFolderName = "MechCommander Redux";

    /// <summary>
    /// The pack the guard plays with: --hd-pack's (absolute: the game works in the install's folder), else the one this
    /// machine's player plays with (the HD pack in the app's own player folder, as the game finds it there), else none:
    /// the original's art alone. Port-only (21 Nov): without --hd-pack the suite's guard played with no pack, so it caught
    /// no HD first use.
    /// </summary>
    std::filesystem::path GuardPack()
    {
        if (const std::optional<std::string_view> pack = MCTest::Option("hd-pack"); pack.has_value())
        {
            return std::filesystem::absolute(std::filesystem::path(*pack));
        }

        const std::unique_ptr<char, decltype(&SDL_free)> player(SDL_GetPrefPath(nullptr, PlayerFolderName), &SDL_free);

        if (player == nullptr)
        {
            return {};
        }

        const std::filesystem::path pack = MCFileSystem::FromUtf8(player.get()) / MCHdArt::DefaultFolder;
        std::error_code error;
        return std::filesystem::is_directory(pack, error) ? pack : std::filesystem::path();
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

    /// <summary>
    /// The HD trace's use lines (MCHdArt's TraceUse): their mark, and their fields: the mark, the time, how, the folder,
    /// the frame's name and its microseconds.
    /// </summary>
    constexpr std::string_view TraceUseMark = "U\t";
    constexpr size_t TraceUseFields = 6;
    constexpr size_t TraceUseHow = 2;
    constexpr size_t TraceUseFolder = 3;
    /// <summary>How a use line says a frame was first drawn: kept as read ahead, taken from a reader, or read then.</summary>
    constexpr auto FirstDrawUses = std::to_array<std::string_view>({"kept", "taken", "read"});
    /// <summary>The opening list's lines (sprite/MCOpeningLists.h): a folder of the mission's, a packet of the player's mechs.</summary>
    constexpr std::string_view OpeningFolderLine = "F";
    constexpr std::string_view OpeningLanceLine = "L";

    /// <summary>Whether <paramref name="folder"/> is a packet's: its last part a number (<c>objects/shapes/614/0</c>).</summary>
    bool NumberedFolder(std::string_view folder)
    {
        const std::string_view last = folder.substr(folder.rfind('/') + 1);
        return !last.empty() && std::ranges::all_of(last, [](unsigned char c) { return std::isdigit(c) != 0; });
    }

    /// <summary>
    /// Port-only (the opening's list, sprite/MCOpeningLists.h): writes to <c>&lt;folder&gt;/&lt;mission&gt;.txt</c> the
    /// pack's packets the play that the HD trace <paramref name="trace"/> records first drew, in that order, each once:
    /// a folder of the mission's <c>F &lt;folder&gt;</c>, a packet of the player's mechs <c>L &lt;its part's PAK
    /// folder&gt; &lt;place&gt; &lt;packet&gt;</c> (a mech part's archive the mission's list lacks, as it leaves the
    /// player's out: MCScenario::HoldList), its place its archive's among the player's of that PAK in
    /// MCHdArt::OpeningLance's order (none, <c>L &lt;PAK&gt; &lt;packet&gt;</c>, where it isn't there). Only packets'
    /// folders, not the ground's or the interface's. Gives the lines written.
    /// </summary>
    size_t WriteOpening(const std::filesystem::path& trace, const std::filesystem::path& folder)
    {
        const std::string mission = Mission()->Scenarios.at(static_cast<size_t>(Mission()->CurrentScenario));
        std::unordered_set<std::string> missionMechs;

        for (const std::string& listed : Scenario()->HoldList(mission).Folders)
        {
            if (std::optional<std::string> archive = MCHdArt::MechPartArchive(listed); archive.has_value())
            {
                missionMechs.insert(std::move(*archive));
            }
        }

        // Each of the player's mechs' archives' place among those of its PAK.
        std::unordered_map<std::string, size_t> places;
        std::unordered_map<std::string, size_t> paks;

        for (const std::string& archive : MCHdArt::OpeningLance())
        {
            places.emplace(archive, paks[archive.substr(0, archive.rfind('/'))]++);
        }

        std::ifstream in(trace);
        REQUIRE(in.good());
        std::unordered_set<std::string> written;
        std::string lines;

        for (std::string line; std::getline(in, line);)
        {
            if (!line.starts_with(TraceUseMark))
            {
                continue;
            }

            std::vector<std::string_view> fields;

            for (const auto part : std::views::split(std::string_view(line), '\t'))
            {
                fields.emplace_back(part.begin(), part.end());
            }

            if (fields.size() < TraceUseFields || !std::ranges::contains(FirstDrawUses, fields[TraceUseHow]) ||
                !NumberedFolder(fields[TraceUseFolder]))
            {
                continue;
            }

            const std::string at(fields[TraceUseFolder]);
            const std::optional<std::string> archive = MCHdArt::MechPartArchive(at);
            // A packet of the archive's own, its number alone past it (mechs/legs90/7/51).
            const bool lance = archive.has_value() && !missionMechs.contains(*archive) && at.size() > archive->size() &&
                               at.find('/', archive->size() + 1) == std::string::npos;
            std::string row = std::format("{} {}", OpeningFolderLine, at);

            if (lance)
            {
                const std::string pak = archive->substr(0, archive->rfind('/'));
                const std::string packet = at.substr(archive->size() + 1);
                const auto place = places.find(*archive);
                row = place != places.end() && place->second < MCOpeningLists::AnyPlace
                          ? std::format("{} {} {} {}", OpeningLanceLine, pak, place->second, packet)
                          : std::format("{} {} {}", OpeningLanceLine, pak, packet);
            }

            if (written.insert(row).second)
            {
                lines += row + '\n';
            }
        }

        std::filesystem::create_directories(folder);
        std::ofstream(folder / MCFileSystem::FromUtf8(mission + ".txt"), std::ios::binary) << lines;
        return written.size();
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
/// Port-only (the opening view's test): the campaign mission it plays, whose opening view holds map objects release 9
/// drew from the pack in play's first frame (waited on: 6.9 ms, 19 Nov).
/// </summary>
constexpr int32_t OpeningMission = 3;

/// <summary>
/// Port-only (SPEC-memory.md's "Under the loading bar": the opening view): play's first frames in mission 3, as a
/// player's, with the guard's pack (GuardPack), read, unpack and wait on nothing on the game's thread: the load read and
/// unpacked what its view and the ring around it draw, and put it on the GPU.
/// </summary>
TEST_CASE_ISOLATED("game: play's first frames in mission 3 read and unpack nothing on the game's thread")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestContextScope scope;
    StartAsPlayer(scope, OpeningMission, GuardPack());
    HdMemory memory;
    PlayBattle(OpeningFrames, memory);
    RequireOpeningUsesNothing(MCFirstUses::Take());
}

/// <summary>
/// Port-only (the opening view): the same for the canyon's battle, its staged force as the benchmark plays it, at the
/// zoom play opens at: the battle's own zoom from its first frame (540 lines, eased) is the zoom's read-ahead's, as a
/// player's is.
/// </summary>
TEST_CASE_ISOLATED("game: play's first frames in the canyon read and unpack nothing on the game's thread")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCBattleSpec spec = CanyonSpec(OpeningFrames);
    spec.Zoom = 0;
    const std::expected<void, std::string> put = MCTrailerMission::Put(spec.Force);
    REQUIRE(put.has_value());
    MCTestContextScope scope;
    StartAsPlayer(scope, spec.Mission, GuardPack());
    HdMemory memory;
    PlayStaged(spec, memory);
    RequireOpeningUsesNothing(MCFirstUses::Take());
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
/// Port-only (20 Nov, the A10: play's first frames with the world waited 16 and 20 ms for the GPU, still busy with the
/// load's last copies): a mission's load ends once the GPU has done all it was given (MCMission's
/// WaitForTheLoadsGpuWork), its frame log's note before "mission started".
/// </summary>
TEST_CASE_ISOLATED("game: a mission's load ends once the GPU has done the load's work")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    constexpr std::string_view DoneNote = "the load's GPU work done: ";
    const MCScratchFolder folder("mc_first_use_tests", "load-gpu-work");
    const std::filesystem::path log = folder.Path() / "load.framelog";
    REQUIRE(MCFrameLog::Open(log));
    REQUIRE(MCTestGame::StartMission(1));
    // The test game's frames don't move the log's on: its first frame, which the notes so far go to, then written.
    MCFrameLog::NextFrame();
    MCFrameLog::NextFrame();
    MCFrameLog::Close();
    std::ifstream in(log);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    const size_t done = text.find(DoneNote);
    REQUIRE(done != std::string::npos);
    CHECK(done < text.find("mission started"));
    CHECK(text.find(DoneNote, done + 1) == std::string::npos);
    std::cout << "  " << text.substr(done, text.find('\n', done) - done) << "\n";
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
        "frames and put {} up; the opening {:.1f} MB walked, {} folders asked{})\n",
        mission, loadSeconds, played, players.size(), enemies.size(), uses.size(), ms, MCFirstUses::Dropped(),
        static_cast<double>(memory.Peak) / BytesPerMb, memory.Mean() / BytesPerMb, memory.Mechs,
        fill.Fits ? "whole" : "not whole", static_cast<double>(fill.MissionBytes) / BytesPerMb, fill.Asked, fill.Up,
        static_cast<double>(fill.OpeningBytes) / BytesPerMb, fill.OpeningAsked, fill.OpeningDone ? "" : ", not done");
    MCFrameLog::Close();
    MCHdArt::CloseTrace();
    MCMemoryRoom::SetMachineBytes(std::nullopt);
    MCMemoryRoom::SetCardBytes(std::nullopt);

    // --first-uses-opening <folder>: the mission's opening, as its trace (--first-uses-trace) records the play first drew
    // it (WriteOpening), for --opening-lists-table. A staged battle has none: it's no mission's.
    if (const std::optional<std::string_view> opening = MCTest::Option("first-uses-opening");
        opening.has_value() && !canyon)
    {
        const std::optional<std::string_view> trace = MCTest::Option("first-uses-trace");
        REQUIRE(trace.has_value());
        const size_t lines = WriteOpening(MCFileSystem::FromUtf8(*trace), MCFileSystem::FromUtf8(*opening));
        std::cout << std::format("  the opening: {} packets first drawn, to {}\n", lines, *opening);
        CHECK(lines > 0);
    }
}

namespace
{
    /// <summary>
    /// The opening table's preamble and its types (sprite/MCOpeningLists.h), as --opening-lists-table writes them ahead of
    /// the tables.
    /// </summary>
    constexpr std::string_view OpeningTableHead = R"(#pragma once

// Port-only (SPEC-memory.md's "The GPU from the briefing": the opening): the HD pack's packets each mission's play
// draws first, in the order a recorded play first drew them, which the fill puts on the GPU first (MCHdArt's
// opening). Worked out by playing each mission a minute with mc_tests --first-uses --first-uses-opening
// (tests/FirstUseTests.cpp) and written by mc_tests --opening-lists-table. Don't edit it.

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

// clang-format off
namespace MCOpeningLists
{
/// <summary>An Entry's Packet for a folder of the mission's own, and its Place for a packet of every player's mech.</summary>
inline constexpr uint16_t NoPacket = 0xffff;
inline constexpr uint8_t AnyPlace = 0xff;

/// <summary>
/// A packet a mission's play draws first: a folder of Folders whole (Packet NoPacket), or a packet of the player's mechs
/// (Folders names its part's PAK, "mechs/legs90"; every packet number names one gesture at one facing on any mech): that
/// of the mech at Place among the player's mechs whose part is that PAK's (MCHdArt::OpeningLance's order), or of each.
/// </summary>
struct Entry
{
    uint16_t Folder = 0;
    uint16_t Packet = 0;
    uint8_t Place = AnyPlace;
};

/// <summary>A mission (its file's name, lower case) and its entries, in the order its play first drew them.</summary>
struct Mission
{
    std::string_view Name;
    std::span<const Entry> Entries;
};

)";

    /// <summary>The doc of the table's AnyMission (OpeningTableHead's types).</summary>
    constexpr std::string_view AnyMissionDoc = R"(/// <summary>
/// The player's mechs' packets for a mission with no list of its own (a match's map, a mod's): those most missions'
/// plays drew first, in their order on average.
/// </summary>
)";

    /// <summary>
    /// --opening-lists-table's: a packet of the player's mechs common enough for AnyMission, in a quarter of the missions'
    /// openings at least; the table's rows, so many entries or names a line.
    /// </summary>
    constexpr size_t AnyMissionShare = 4;
    constexpr size_t OpeningRowEntries = 8;

    /// <summary>
    /// A line of an opening's list (WriteOpening): its folder, and the packet of a line of the player's mechs and its
    /// mech's place.
    /// </summary>
    struct OpeningLine
    {
        std::string Folder;
        std::optional<uint16_t> Packet;
        std::optional<uint8_t> Place;
    };

    /// <summary>An entry of the table's arrays: its folder's index in Folders, its Packet and its Place (none: AnyPlace).</summary>
    struct OpeningRow
    {
        size_t Folder = 0;
        uint16_t Packet = MCOpeningLists::NoPacket;
        std::optional<uint8_t> Place;
    };

    /// <summary>The opening's list WriteOpening wrote at <paramref name="path"/>, its lines in order.</summary>
    std::vector<OpeningLine> ReadOpening(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        std::vector<OpeningLine> lines;

        for (std::string line; std::getline(file, line);)
        {
            std::istringstream words(line);
            std::string kind;
            std::string folder;
            words >> kind >> folder;
            // A lance line's numbers: its packet, or its place and its packet.
            std::vector<uint32_t> numbers;

            for (uint32_t number = 0; words >> number;)
            {
                numbers.push_back(number);
            }

            if (kind == OpeningFolderLine && !folder.empty() && numbers.empty())
            {
                lines.push_back(OpeningLine{std::move(folder), std::nullopt, std::nullopt});
            }
            else if (kind == OpeningLanceLine && (numbers.size() == 1 || numbers.size() == 2))
            {
                const uint32_t packet = numbers.back();
                REQUIRE(packet < MCOpeningLists::NoPacket);
                REQUIRE(numbers.front() < MCOpeningLists::AnyPlace);
                lines.push_back(
                    OpeningLine{std::move(folder), static_cast<uint16_t>(packet),
                                numbers.size() == 2 ? std::optional<uint8_t>(numbers.front()) : std::nullopt});
            }
            else
            {
                FAIL_CHECK(std::format("{}: a line neither a folder's nor a packet's: \"{}\"", path.string(), line));
            }
        }

        return lines;
    }

    /// <summary>
    /// The entries of the table's array, OpeningRowEntries a line, each as its Entry (Folder, Packet, and Place where
    /// it has one).
    /// </summary>
    std::string OpeningRows(std::span<const OpeningRow> entries)
    {
        std::string rows;

        for (size_t at = 0; at < entries.size(); ++at)
        {
            const OpeningRow& entry = entries[at];
            rows += (at % OpeningRowEntries == 0 ? "    " : " ") +
                    (entry.Place.has_value() ? std::format("{{{}, {}, {}}},", entry.Folder, entry.Packet, *entry.Place)
                                             : std::format("{{{}, {}}},", entry.Folder, entry.Packet)) +
                    (at % OpeningRowEntries == OpeningRowEntries - 1 || at + 1 == entries.size() ? "\n" : "");
        }

        return rows;
    }

    /// <summary>A table's <c>std::array</c> of <paramref name="type"/> named <paramref name="name"/> with its rows.</summary>
    std::string OpeningArray(std::string_view type, std::string_view name, size_t count, const std::string& rows)
    {
        return count == 0
                   ? std::format("inline constexpr std::array<{}, 0> {}{{}};\n", type, name)
                   : std::format("inline constexpr std::array<{}, {}> {}{{{{\n{}}}}};\n", type, count, name, rows);
    }
}

/// <summary>
/// Tool (SPEC-memory.md's "The GPU from the briefing": the opening): <c>--opening-lists-table &lt;folder&gt;</c> reads the
/// openings --first-uses-opening wrote there (<c>&lt;mission&gt;.txt</c> each) and writes sprite/MCOpeningLists.h, the
/// openings the program carries, to <c>--opening-lists-out &lt;file&gt;</c> (<c>&lt;folder&gt;/MCOpeningLists.h</c> by
/// default): each folder once, each mission's entries as indices into them, the missions by name, and for a mission with
/// none the player's mechs' packets a quarter of the missions' openings or more hold, in their order on average.
/// </summary>
TEST_CASE("tool: --opening-lists-table writes the missions' openings as the table the program carries")
{
    const std::optional<std::string_view> in = MCTest::Option("opening-lists-table");

    if (!in.has_value())
    {
        return;
    }

    const std::filesystem::path folder = MCFileSystem::FromUtf8(*in);
    const std::filesystem::path out = MCTest::Option("opening-lists-out").has_value()
                                          ? MCFileSystem::FromUtf8(*MCTest::Option("opening-lists-out"))
                                          : folder / "MCOpeningLists.h";
    std::map<std::string, std::vector<OpeningLine>> missions;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder))
    {
        if (entry.path().extension() == ".txt")
        {
            std::string name = MCFileSystem::ToUtf8(entry.path().stem());
            std::ranges::transform(name, name.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            missions[name] = ReadOpening(entry.path());
        }
    }

    REQUIRE(!missions.empty());
    std::vector<std::string> folders;
    std::unordered_map<std::string, size_t> indices;
    const auto indexOf = [&](const std::string& name)
    {
        const auto [found, fresh] = indices.try_emplace(name, folders.size());

        if (fresh)
        {
            folders.push_back(name);
        }

        return found->second;
    };

    // Each packet of the player's mechs: the missions whose openings hold it, and its places among theirs (0 to 1).
    std::map<std::pair<std::string, uint16_t>, std::pair<size_t, double>> lance;
    std::string arrays;
    std::string table;
    size_t entries = 0;

    for (const auto& [name, lines] : missions)
    {
        std::string identifier = name;
        std::ranges::replace_if(identifier, [](unsigned char c) { return std::isalnum(c) == 0; }, '_');
        const auto lanceLines = static_cast<size_t>(
            std::ranges::count_if(lines, [](const OpeningLine& line) { return line.Packet.has_value(); }));
        std::vector<OpeningRow> rows;
        size_t lanceAt = 0;
        // A packet counted once a mission, at its first place there, whichever mech's.
        std::set<std::pair<std::string, uint16_t>> counted;

        for (const OpeningLine& line : lines)
        {
            rows.push_back(
                OpeningRow{indexOf(line.Folder), line.Packet.value_or(MCOpeningLists::NoPacket), line.Place});

            if (!line.Packet.has_value())
            {
                continue;
            }

            const double at = static_cast<double>(lanceAt++) / static_cast<double>(lanceLines);

            if (counted.emplace(line.Folder, *line.Packet).second)
            {
                auto& [count, places] = lance[{line.Folder, *line.Packet}];
                ++count;
                places += at;
            }
        }

        arrays += OpeningArray("Entry", "Opening_" + identifier, rows.size(), OpeningRows(rows)) + "\n";
        table += std::format("    {{\"{}\", Opening_{}}},\n", name, identifier);
        entries += rows.size();
    }

    std::vector<std::tuple<double, std::string, uint16_t>> common;

    for (const auto& [key, tally] : lance)
    {
        if (tally.first * AnyMissionShare >= missions.size())
        {
            common.emplace_back(tally.second / static_cast<double>(tally.first), key.first, key.second);
        }
    }

    std::ranges::sort(common);
    // Every player's mech's packets (no place): a match's lance is no list's.
    std::vector<OpeningRow> any;

    for (const auto& [place, part, packet] : common)
    {
        any.push_back(OpeningRow{indexOf(part), packet, std::nullopt});
    }

    REQUIRE(folders.size() < MCOpeningLists::NoPacket);
    std::string names;

    for (size_t at = 0; at < folders.size(); ++at)
    {
        names += std::format("    \"{}\",\n", folders[at]);
    }

    const std::string text =
        std::string(OpeningTableHead) + OpeningArray("std::string_view", "Folders", folders.size(), names) + "\n" +
        arrays + OpeningArray("Mission", "Missions", missions.size(), table) + "\n" + std::string(AnyMissionDoc) +
        OpeningArray("Entry", "AnyMission", any.size(), OpeningRows(any)) + "}\n\n// clang-format on\n";
    std::ofstream(out, std::ios::binary) << text;
    std::cout << std::format(
        "  {} missions' openings, {} entries, {} folders, {} packets of the player's mechs for any "
        "mission, to {}\n",
        missions.size(), entries, folders.size(), any.size(), MCFileSystem::ToUtf8(out));
}

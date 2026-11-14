#pragma once

// Port-only: a staged battle, the Redux HD trailer's: one mission's battle played out the same every time from a spec,
// the same orders, camera cuts, hits and losses. The recorder (mc_tests --showcase) records it for the trailer's
// takes, frame by frame on a manual clock; the benchmark (main/MCBenchmark.h) plays it in the game's own window and
// frame loop at real time. Each frame of the battle (30 a second) is staged before the game's logic runs it
// (MCStagedBattle::Stage) and answered after (Staged); between, the camera may be aimed again each drawn frame (Aim).
// The staging itself (who walks where, who fires at whom, what each hit takes) is described with --showcase in
// apps/MCRedux/tests/HdArtTests.cpp.

#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class MCBattleMech;
class MCMover;
class MCVector3D;

/// <summary>A staged moment: from a frame, one of the player's mechs (by place) and what befalls it.</summary>
struct MCBattleEvent
{
    int32_t Frame = 0;
    /// <summary>The mech's place in the player's force (0 the first), or -1 for none named.</summary>
    int32_t Mech = -1;
    std::string What;
};

/// <summary>
/// A battle's spec: its mission and how it's staged, a value per name. The names are the recorder's options without
/// --showcase- (--showcase-camera is camera), and mission and force; a spec's text is a line per name, "name value",
/// # starting a comment.
/// </summary>
struct MCBattleSpec
{
    /// <summary>The frames of the battle a second.</summary>
    static constexpr int32_t FramesPerSecond = 30;
    /// <summary>How long before the walk's end the contact steps into sight, unless the spec says (contact-step).</summary>
    static constexpr int32_t ContactStepFrames = 100;

    /// <summary>The mission: its game segment of SYSTEM.CFG's campaign, as -mission takes it.</summary>
    int32_t Mission = 1;
    /// <summary>The mission file whose trailer's force it has (mission/MCTrailerMission.h); empty: the mission's own.</summary>
    std::string Force;
    /// <summary>How many frames it plays; 0: as long as whoever plays it says.</summary>
    int32_t Frames = 0;
    /// <summary>The view's zoom in game lines (480 the closest a player gets; fewer as a taller screen's); 0: as it is.</summary>
    int32_t Zoom = 0;
    int32_t JumpEvery = 0;
    std::vector<int32_t> Strikes;
    std::vector<int32_t> SnowLeast;
    std::vector<int32_t> Kills;
    std::vector<int32_t> Watch;
    std::vector<int32_t> JumpAt;
    int32_t Walk = 0;
    int32_t Contact = -1;
    int32_t ContactStep = ContactStepFrames;
    int32_t Answer = -1;
    int32_t WatchZoom = 0;
    int32_t ZoomEase = 1;
    std::vector<MCBattleEvent> Incoming;
    std::vector<MCBattleEvent> Cameras;
    std::vector<MCBattleEvent> Dfas;
    std::vector<MCBattleEvent> Mines;
    std::vector<MCBattleEvent> Alphas;
    std::vector<MCBattleEvent> Destroys;
    std::vector<MCBattleEvent> Runs;
    std::vector<MCBattleEvent> Closes;
    std::vector<MCBattleEvent> Volleys;
    std::vector<MCBattleEvent> Losses;
    std::vector<MCBattleEvent> Smokes;
    std::vector<MCBattleEvent> Wears;
    std::optional<std::string> Converge;
    std::optional<std::string> CloseNames;

    /// <summary>A spec's value by name, if it has one.</summary>
    using MCLookup = std::function<std::optional<std::string>(std::string_view name)>;

    /// <summary>A spec's values by name (every name it knows), each as <paramref name="value"/> gives it.</summary>
    /// <returns>The spec, or what's wrong with a value (a number that isn't one).</returns>
    static std::expected<MCBattleSpec, std::string> Read(const MCLookup& value);

    /// <summary>A spec text's values: a line per name ("name value"), # starting a comment, blank lines skipped.</summary>
    /// <returns>The values, or what's wrong with a line (no value, a name no spec has, a name twice).</returns>
    static std::expected<std::map<std::string, std::string, std::less<>>, std::string> Values(std::string_view text);

    /// <summary>A spec text read whole (<see cref="Values"/>, then <see cref="Read"/>).</summary>
    static std::expected<MCBattleSpec, std::string> Parse(std::string_view text);

    /// <summary>The text of the battle built in by <paramref name="name"/> (canyon: the trailer's), if there's one.</summary>
    static std::optional<std::string_view> BuiltIn(std::string_view name);
};

/// <summary>What a battle's log line tells: a staged event, or each second's state of the fight.</summary>
enum class MCBattleLine : uint8_t
{
    Event,
    Second,
};

/// <summary>Where a battle's log lines go (without indent or newline).</summary>
using MCBattleLog = std::function<void(MCBattleLine, std::string_view)>;

/// <summary>A staged battle being played (see the file's comment), its mission up.</summary>
class MCStagedBattle
{
public:
    MCStagedBattle(MCBattleSpec spec, MCBattleLog log);
    ~MCStagedBattle();
    MCStagedBattle(const MCStagedBattle&) = delete;
    MCStagedBattle& operator=(const MCStagedBattle&) = delete;

    const MCBattleSpec& Spec() const;

    /// <summary>
    /// Its start, once the mission's scenario is playing: the view zoomed as the spec says, and the forces logged.
    /// </summary>
    /// <returns>Nothing, or why it can't be played (no view; a force spec'd that the mission hasn't).</returns>
    std::expected<void, std::string> Begin();

    /// <summary>Frame <paramref name="frame"/> staged, before the game's logic runs it (frames in order from 0).</summary>
    void Stage(int32_t frame);

    /// <summary>The camera aimed as in frame <paramref name="frame"/> (Stage does), for a frame drawn between two staged.</summary>
    void Aim(int32_t frame);

    /// <summary>Frame <paramref name="frame"/> answered, after the game's logic ran it (the force's fire, the log).</summary>
    void Staged(int32_t frame);

    /// <summary>Whether the force's target was down as the last frame staged started.</summary>
    bool TargetDown() const;

    /// <summary>How many frames have started with the force's target down, and how many jumps were staged.</summary>
    int32_t FramesTargetDown() const;
    int32_t Jumps() const;

private:
    struct MCState;
    std::unique_ptr<MCState> _State;
};

/// <summary>The staging's helpers other tools use.</summary>
namespace MCStaging
{
    /// <summary>The player's first mech's part id; the force's are the next ones, up to twelve.</summary>
    inline constexpr int32_t PlayerMechPartId = 0x200;

    /// <summary>The live enemy (another team's mover, not disabled) nearest the first live mech of the player's lance.</summary>
    std::shared_ptr<MCMover> NearestEnemy();

    /// <summary>
    /// The player's lance selected (its brackets and bars drawn) and ordered to attack <paramref name="target"/>, the
    /// camera on the player's mech.
    /// </summary>
    void SendLanceAt(const std::shared_ptr<MCMover>& target);

    /// <summary>Turns <paramref name="mech"/> to face <paramref name="degrees"/> (about the world's up axis).</summary>
    void FaceMech(MCBattleMech& mech, float degrees);

    /// <summary>
    /// Turns <paramref name="mech"/> to face <paramref name="point"/>: FaceMech's facing turned by the mech's relative
    /// facing to the point one way or the other, whichever leaves it facing the point.
    /// </summary>
    void FaceMechTo(MCBattleMech& mech, const MCVector3D& point);
}

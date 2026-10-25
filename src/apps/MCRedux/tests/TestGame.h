#pragma once

/// <summary>
/// The retail install the <c>game:</c> tests read: the runner's <c>--game &lt;folder&gt;</c>, else the current folder.
/// Without one those tests note that they were skipped and pass.
/// </summary>
class MCBattleMech;
class MCSpriteTree;

namespace MCTestGame
{
    /// <summary>Whether --game (or the current folder) is an install; sets it as the game root the first time.</summary>
    bool Available();

    /// <summary>Opens the five FastFiles as the game does at startup (once per run).</summary>
    void OpenFastFiles();

    /// <summary>
    /// Boots the whole game in a hidden window with <c>-mission &lt;segment&gt;</c> (SYSTEM.CFG's campaign, logistics
    /// skipped), as RunGame does, and runs frames until the scenario is playing. Only a TEST_CASE_ISOLATED test may:
    /// the boot expects a fresh process. The clock is an MCManualClock, installed in the context current at the boot
    /// (a test may install its own MCTestContextScope first, to give the game other services), and the dice are seeded,
    /// so a run plays out the same every time. The game stays up for the rest of the run, so only one segment can be
    /// started; later calls with the same segment return at once.
    /// </summary>
    /// <returns>Whether the scenario is up.</returns>
    bool StartMission(int32_t segment);

    /// <summary>
    /// Boots the whole game as a plain start does (no <c>-mission</c>): the intro, then logistics' main menu. Runs
    /// frames until the main menu is up. Same rules as <see cref="StartMission"/>: a TEST_CASE_ISOLATED test only, and
    /// one boot per process (StartMission after it fails, and the reverse).
    /// </summary>
    /// <returns>Whether the main menu is up.</returns>
    bool StartLogistics();

    /// <summary>
    /// Runs one pass of aSystem::run's frame loop (the per-frame callbacks and the display update), with the frame
    /// lasting <paramref name="seconds"/> instead of the measured time.
    /// </summary>
    void RunFrame(float seconds);

    /// <summary>
    /// Called on every present after the boot (from MCDisplay::OnPresent, which the boot takes over). A test that
    /// folds the presents into a hash sets it, and clears it at the end.
    /// </summary>
    /// <remarks>
    /// Every present is also written to <c>--present-log &lt;file&gt;</c> (appended: a <c>== &lt;test&gt;</c> line, then
    /// <c>&lt;index&gt; 0x&lt;screen hash&gt;</c> per present), and <c>--present-shot &lt;n&gt;,&lt;n&gt;...</c> saves those
    /// presents into the <c>--shots</c> folder. tools/ci/baseline.py compares two builds' logs.
    /// </remarks>
    extern std::function<void()> OnPresent;

    /// <summary>
    /// FNV-1a of the game's state: the turn, the scenario clock, the dice's state, every mover (position, frame,
    /// status, internal structure and armor per location) and its pilot (status, current order), and the objectives'
    /// statuses. Floats are hashed by their bits.
    /// </summary>
    /// <remarks>
    /// While a scenario is up, each <see cref="RunFrame"/> writes it to <c>--state-log &lt;file&gt;</c> (appended: a
    /// <c>== &lt;test&gt;</c> line, then <c>&lt;frame&gt; 0x&lt;hash&gt;</c> per frame).
    /// </remarks>
    uint32_t StateHash();

    /// <summary>The mech tree named <paramref name="name"/> (its FIT's Main Info "Name") in the game's sprite PAK.</summary>
    std::shared_ptr<MCSpriteTree> FindMechTree(std::string_view name);

    /// <summary>Turns <paramref name="mech"/> to face <paramref name="degrees"/> (about the world's up axis).</summary>
    void FaceMech(MCBattleMech& mech, float degrees);
}

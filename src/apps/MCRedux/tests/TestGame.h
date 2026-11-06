#pragma once

/// <summary>
/// The retail install the <c>game:</c> tests read: the runner's <c>--game &lt;folder&gt;</c>, else the current folder.
/// Without one those tests note that they were skipped and pass.
/// </summary>
class MCBattleMech;
class MCMover;
class MCSpriteTree;

namespace MCTestGame
{
    /// <summary>Whether --game (or the current folder) is an install; sets it as the game root the first time.</summary>
    bool Available();

    /// <summary>Opens the five FastFiles as the game does at startup (once per run).</summary>
    void OpenFastFiles();

    /// <summary>
    /// How <see cref="StartMission"/> and <see cref="StartLogistics"/> boot. The defaults are the recorded runs': a
    /// 640x480 window and a world view fixed at 480 lines.
    /// </summary>
    struct MCBootOptions
    {
        /// <summary>
        /// The hidden window's size in pixels once the display is open, before the first frame (the window a player
        /// had in the menus); 0 keeps 640x480.
        /// </summary>
        int WindowWidth = 0;
        int WindowHeight = 0;
        /// <summary>The world view's lines at any zoom (MCFixedZoomHeight); 0 lets the zoom start as in play.</summary>
        float FixedZoomHeight = 480.0f;
        /// <summary>Called once the game has started (PREFS read, the display open), before the window is resized.</summary>
        std::function<void()> Started;
        /// <summary>
        /// Whether the game's sound runs (SYSTEM.CFG's sound and music, into the context's audio device); off, the game
        /// boots with GNoSound, as the tests run.
        /// </summary>
        bool Sound = false;
    };

    /// <summary>The options the next boot uses (set them before it).</summary>
    extern MCBootOptions BootOptions;

    /// <summary>The size of a player's window the large-window tests boot in, in pixels.</summary>
    inline constexpr int LargeWindowWidth = 1600;
    inline constexpr int LargeWindowHeight = 1200;

    /// <summary>
    /// Sets <see cref="BootOptions"/> for a player's 1600x1200 window with the zoom free, at UI RESOLUTION
    /// <paramref name="uiResolution"/> and INTERFACE SIZE <paramref name="interfaceSize"/> (in place of PREFS's).
    /// </summary>
    void BootInLargeWindow(int uiResolution, int interfaceSize);

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
    /// Stages mission 1's battle once StartMission(1) is up: the player's three mechs (parts 0x200-0x202) ordered to
    /// attack the clan's Uller (part 896) and pursue it.
    /// </summary>
    /// <returns>The Uller (null when the mission hasn't one).</returns>
    std::shared_ptr<MCMover> OrderMissionOneAttack();

    /// <summary>
    /// Boots the whole game as a plain start does (no <c>-mission</c>): the intro, then logistics' main menu. Runs
    /// frames until the main menu is up. Same rules as <see cref="StartMission"/>: a TEST_CASE_ISOLATED test only, and
    /// one boot per process (StartMission after it fails, and the reverse).
    /// </summary>
    /// <param name="afterBoot">Run once the game has booted (its paths read from SYSTEM.CFG), before the main menu
    /// shows: where a test sets up what the menu reads (<see cref="PinnedSaves"/>).</param>
    /// <returns>Whether the main menu is up.</returns>
    bool StartLogistics(const std::function<void()>& afterBoot = {});

    /// <summary>
    /// Boots the whole game as a multiplayer lobby starts it, with <paramref name="commandLine"/> (-host or -join and
    /// the lobby's other words): it hosts or joins the room as the game starts, then runs frames until logistics shows
    /// the room (the session screen), or the message saying the join failed. Same rules as
    /// <see cref="StartMission"/>: a TEST_CASE_ISOLATED test only, and one boot per process.
    /// </summary>
    /// <returns>Whether logistics came up.</returns>
    bool StartLobby(std::string_view commandLine);

    /// <summary>
    /// From logistics' main menu (<see cref="StartLogistics"/>): a new campaign's first mission, started with the
    /// force's first mech deployed. Runs frames until its scenario has started up.
    /// </summary>
    /// <returns>Whether the scenario is up.</returns>
    bool StartCampaignMission();

    /// <summary>The tests' user folder (under the temporary folder): the game writes its saves and settings there.</summary>
    std::filesystem::path UserFolder();

    /// <summary>The main menu's LOAD MISSION button (its element 3, which MCGenericScreen::ShowGuiWindow enables).</summary>
    inline constexpr int32_t MainMenuLoadButton = 3;

    /// <summary>
    /// While it lives, the game's save folder (SavePath, and the folder logistics' file panes list) is a folder of the
    /// tests' user folder holding copies of what the game ships in its own (campaign starts, solo missions, multiplayer
    /// maps) and, of saves, just those it names (empty files: the load screen lists each by name, without an operation,
    /// mission or points). What the main menu and the load screen show then doesn't depend on the install's saves or
    /// those earlier tests left. Made once the game has read its paths and before the main menu shows
    /// (StartLogistics' afterBoot).
    /// </summary>
    class PinnedSaves
    {
    public:
        explicit PinnedSaves(std::span<const std::string_view> names = {});
        ~PinnedSaves();

        PinnedSaves(const PinnedSaves&) = delete;
        PinnedSaves& operator=(const PinnedSaves&) = delete;

    private:
        /// <summary>The save folder it replaced.</summary>
        std::string _Kept;
    };

    /// <summary>
    /// Deletes the save games written while it lives (a mission's start and a finished mission save the campaign),
    /// which would otherwise pile up in the test user folder.
    /// </summary>
    class SaveGameCleanup
    {
    public:
        SaveGameCleanup();
        ~SaveGameCleanup();

        SaveGameCleanup(const SaveGameCleanup&) = delete;
        SaveGameCleanup& operator=(const SaveGameCleanup&) = delete;

    private:
        /// <summary>The files directly in the save folder.</summary>
        std::vector<std::filesystem::path> Files() const;

        std::filesystem::path _Folder;
        std::set<std::filesystem::path> _Before;
    };

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

    /// <summary>The mech trees in the game's sprite PAK: each one's name (its FIT's Main Info "Name") and packet.</summary>
    std::vector<std::pair<std::string, int32_t>> MechTrees();

    /// <summary>The mech tree named <paramref name="name"/> (its FIT's Main Info "Name") in the game's sprite PAK.</summary>
    std::shared_ptr<MCSpriteTree> FindMechTree(std::string_view name);

    /// <summary>Turns <paramref name="mech"/> to face <paramref name="degrees"/> (about the world's up axis).</summary>
    void FaceMech(MCBattleMech& mech, float degrees);

    /// <summary>
    /// Puts <paramref name="count"/> of every mech variant, component and vehicle in the logistics store, each store
    /// row readied again as the game does when its stock changes.
    /// </summary>
    void StockStore(int32_t count);
}

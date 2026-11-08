#pragma once

#include "gui/MCGuiButton.h"
#include "gui/MCGuiRef.h"
#include "gui/MCGuiSystem.h"
#include "mission/MCMissionResults.h"

class MCGuiPort;
class MCGuiButton;
class MCGuiScrollTextObject;
class MCMechWarrior;

/// <summary>
/// Port: a pilot line's icon, drawn in place each frame from the pilot's mech bar icon, its frame trimmed (so HD art
/// shows the pack's pictures of its parts): the pilot, and for a pilot missing (whose icon shows the dead image) the
/// pilot's own portrait, shown in its place.
/// </summary>
struct MCResultsIcon
{
    std::weak_ptr<MCMechWarrior> Warrior;
    std::unique_ptr<MCGuiPort> Portrait;
};

/// <summary>Port-only (Redux HD): REMATCH's pictures, up and held (none where a face can't be read).</summary>
struct MCRematchFaces
{
    std::shared_ptr<MCGuiPort> Up;
    std::shared_ptr<MCGuiPort> Down;
};

/// <summary>
/// Port-only (Redux HD): the multiplayer results' REMATCH (network/MCRoomEnd.h) beside the move-on button, made of its
/// faces (NEXT MISSION's, up and held): the word wiped, each row the face's colour, and REMATCH laid on in the art's
/// lettering (mission/MCRematchLetters.h) and the art's ink, dark on the up face and light on the held one, each pixel
/// the palette's grey nearest the face blended with the ink by the word's coverage. Made in the faces' files' own
/// colours, each face is the same picture on any screen, so the HD art pack's copy (found by its pixels) shows.
/// </summary>
MCRematchFaces RematchFaces();

/// <summary>
/// Port-only (Redux HD): REMATCH on <paramref name="parent"/> at (<paramref name="xPos"/>, <paramref name="yPos"/>),
/// hidden: a button showing <see cref="RematchFaces"/>, whose release hides it and begins the rematch.
/// </summary>
MCGuiHandle<MCGuiButton> AddRematchButton(MCGuiObject& parent, int32_t xPos, int32_t yPos);

/// <summary>The steps the single-player results screen counts through, each drawn a line at a time.</summary>
enum class MCResultsStep : int32_t
{
    /// <summary>The resource points count up.</summary>
    ResourcePoints,
    /// <summary>The kill and loss statistics.</summary>
    Statistics,
    /// <summary>The primary objectives.</summary>
    PrimaryObjectives,
    /// <summary>The secondary objectives, then the tonnage bonus.</summary>
    SecondaryObjectives,
    /// <summary>The pilots.</summary>
    Pilots,
    /// <summary>The debriefing text comes up.</summary>
    Debriefing,
    /// <summary>Everything is shown.</summary>
    Done
};

/// <summary>
/// The mission results screen: resource points, kill statistics, objectives and pilots, each shown a step at a time
/// (timed with <see cref="_NextDrawTime"/>; Escape skips the animation), then the "move on" button. What it shows is
/// worked out once by <see cref="Activate"/> (<see cref="MCMissionResults"/>); <see cref="Draw"/> draws it each frame.
/// </summary>
class MCMissionResultsScreen : public MCGuiObject
{
public:
    ~MCMissionResultsScreen() override;

    /// <summary>Opens the window: background, the move-on button, the objective marks and the scroll buttons.</summary>
    int32_t Init();
    using MCGuiObject::Init;

    /// <summary>
    /// Frees the screen's ports and results and stops sending events to it. (The original also ended the scenario
    /// here; <see cref="MCMission::CloseResultsScreen"/> does, once the screen is destroyed.)
    /// </summary>
    void OnDestroy() override;

    /// <summary>Scroll-button clicks and auto-repeat timers, Escape (skip the animation), and the multiplayer timeout.</summary>
    void HandleEvent(MCGuiEvent& event) override;
    /// <summary>
    /// Port-only: the mouse wheel scrolls the text anywhere over the screen, as the scroll buttons do (single player
    /// only, as they are).
    /// </summary>
    bool MouseWheel(int32_t steps, int32_t xPos, int32_t yPos) override;

    /// <summary>Dims the screen, advances the steps that are due, and draws the window and its children.</summary>
    void Display() override;

    /// <summary>Port: the screen draws itself each frame (see <see cref="Draw"/>).</summary>
    bool DrawsLive() override { return true; }

    /// <summary>
    /// Port: the background, the move-on label, then what the steps have reached so far (single player) or the whole
    /// summary (multiplayer). The original painted each step into the window's picture as it came.
    /// </summary>
    void Draw() override;

    /// <summary>
    /// Shows the multiplayer pilot list of the home side when <paramref name="showHomeSide"/>, of the other side
    /// otherwise (from the pilot switch button's state).
    /// </summary>
    void ShowMPPilots(bool showHomeSide) { _MpShowHomeSide = showHomeSide; }

    /// <summary>
    /// Fills the screen at the scenario's end: counts kills and losses, builds the pilot lines (applying skill-ups),
    /// totals the resource points, and shows the window.
    /// </summary>
    int32_t Activate();

    /// <summary>Port-only: whether every step is shown (the debriefing text is up).</summary>
    bool Finished() const { return _Step == MCResultsStep::Done; }

private:
    /// <summary>Counts the resource points up, one step per call.</summary>
    void StepResourcePoints();
    /// <summary>Steps to the next kill/loss statistic.</summary>
    void StepStatistics();
    /// <summary>Steps to the next objective (and the tonnage bonus after the secondary ones).</summary>
    void StepObjectives();
    /// <summary>Steps to the next pilot, taking the picture of the pilot's icon.</summary>
    void StepPilots();
    /// <summary>Shows the debriefing text.</summary>
    void StepDebriefing();
    /// <summary>On to the next step's first item.</summary>
    void AdvanceStep();

    /// <summary>The resource point box, once the count has started.</summary>
    void DrawResourcePoints();
    /// <summary>The statistics the steps have reached.</summary>
    void DrawStatistics();
    /// <summary>The objectives the steps have reached, laid out from the top of the list.</summary>
    void DrawObjectiveList();
    /// <summary>Pilot <paramref name="index"/>'s line, at its place in the single-player list.</summary>
    void DrawPilot(int32_t index);
    /// <summary>The multiplayer pilot boxes, with the pilots of the side <see cref="_MpShowHomeSide"/> picks.</summary>
    void DrawMPPilotList();
    /// <summary>The multiplayer summary: the best pilot, the statistics, the time and the commanders.</summary>
    void DrawMPSummary();
    /// <summary>The home side's primary objectives in a multiplayer game.</summary>
    void DrawMPObjectives();
    /// <summary>Whether the tonnage bonus is listed after the secondary objectives.</summary>
    static bool ShowsTonnageBonus();

    /// <summary>A commander's line on the multiplayer screen, as <see cref="Activate"/> found it.</summary>
    struct CommanderLine
    {
        /// <summary>The place (1-6) by kills.</summary>
        int32_t Place = 0;
        /// <summary>The player's name (kept: the session may be left before the screen closes).</summary>
        std::string Name;
        int32_t Score = 0;
    };

    /// <summary>What the screen shows.</summary>
    MCMissionResults _Results;
    /// <summary>Which part of the screen the steps have reached.</summary>
    MCResultsStep _Step = MCResultsStep::ResourcePoints;
    /// <summary>The item within the current step (objective, statistic, pilot).</summary>
    int32_t _StepIndex = 0;
    /// <summary>
    /// The debriefing text box: the screen's own, showing the tactical map's salvage text as it was when the screen
    /// came up (the original borrowed the map's box).
    /// </summary>
    MCGuiHandle<MCGuiScrollTextObject> _TextObject;
    /// <summary>The "objective succeeded" mark (<c>guimr08.tga</c>).</summary>
    std::unique_ptr<MCGuiPort> _SuccessPort;
    /// <summary>The "objective failed" mark (<c>guimr07.tga</c>).</summary>
    std::unique_ptr<MCGuiPort> _FailurePort;
    /// <summary>The move-on button's label image, picked in <see cref="Activate"/>.</summary>
    std::unique_ptr<MCGuiPort> _MoveOnPort;
    /// <summary>The "move on" button (closes the screen).</summary>
    MCGuiHandle<MCGuiButton> _MoveOnButton;
    /// <summary>Multiplayer only: the button switching the pilot list's page.</summary>
    MCGuiHandle<MCGuiButton> _PilotSwitchButton;
    /// <summary>Port-only (Redux HD): multiplayer only, the host's REMATCH, shown when it can be had (network/MCRoomEnd.h).</summary>
    MCGuiHandle<MCGuiButton> _RematchButton;
    /// <summary>The scroll-up button's hot spot, in window coordinates.</summary>
    tagRECT _ScrollUpRect{};
    /// <summary>The scroll-down button's hot spot.</summary>
    tagRECT _ScrollDownRect{};
    /// <summary>The scroll bar's hot spot (a click there scrolls to the clicked line).</summary>
    tagRECT _ScrollBarRect{};
    /// <summary>The scroll-up button's pressed image (shown while held).</summary>
    MCGuiHandle<MCGuiObject> _ScrollUpButton;
    /// <summary>The scroll-down button's pressed image.</summary>
    MCGuiHandle<MCGuiObject> _ScrollDownButton;
    /// <summary><c>MouseTicks</c> at which the next step is due; 0 when the steps are finished.</summary>
    uint32_t _NextDrawTime = 0;
    /// <summary>Set by Escape: the remaining steps are shown at once, without sounds.</summary>
    bool _SkipAnimation = false;
    /// <summary>The resource points the count shows; -1 before the count starts.</summary>
    int32_t _ShownResourcePoints = -1;
    /// <summary>
    /// Each pilot line's icon, taken when the line comes up (multiplayer: in <see cref="Activate"/>); none when the
    /// pilot has no icon.
    /// </summary>
    std::vector<std::optional<MCResultsIcon>> _PilotIcons;
    /// <summary>The multiplayer best pilot's icon (taken before the list's, as the original drew it).</summary>
    std::optional<MCResultsIcon> _BestPilotIcon;
    /// <summary>Which side the multiplayer pilot list shows.</summary>
    bool _MpShowHomeSide = true;
    /// <summary>The multiplayer commanders by kills.</summary>
    std::vector<CommanderLine> _CommanderLines;
    /// <summary>The multiplayer game's length, "mm:ss".</summary>
    std::string _TimeText;
};

/// <summary>Ticks between two steps of the results screen (20).</summary>
extern uint32_t ResultsStepTicks;

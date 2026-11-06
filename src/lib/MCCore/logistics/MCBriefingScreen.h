#pragma once

#include "gui/MCGuiRef.h"
#include "gui/MCGuiSmackerWindow.h"
#include "gui/MCListView.h"
#include "gui/MCScrollPane.h"
#include "logistics/MCLogObject.h"

class MCBriefingBox;
class MCGuiEvent;
class MCLogPart;
class MCMechBriefBlock;
class MCScrollPane;

/// <summary>The tonnage of the units in the drop slots.</summary>
extern int32_t CurDeployTonnage;

/// <summary>The mission's drop tonnage limit.</summary>
extern int32_t MaxDeployTonnage;

/// <summary>
/// The logistics briefing screen: the operation briefing (a Smacker movie, or the operation picture in
/// multiplayer), the mission briefing text, and the deploy area where the player drags mechs and vehicles into the
/// drop slots under the tonnage limit.
/// </summary>
/// <remarks>Original source: <c>logistics\logbri.cpp</c> (<c>BriefingScreen</c>).</remarks>
class MCBriefingScreen : public MCLogObject
{
public:
    /// <summary>The drop slots: three lances of four (a game rule), two columns of six on the screen.</summary>
    static constexpr size_t NumDropSlots = 12;

    /// <summary>The briefing tabs.</summary>
    static constexpr int32_t OperationTab = 1;
    static constexpr int32_t MissionTab = 2;

    /// <summary>Destroys the screen if nothing did (its cleanup stops the movie).</summary>
    ~MCBriefingScreen() override { Destroy(); }

    /// <summary>Makes the full-screen object, its scroll panes and ports, and the drop-slot rectangles.</summary>
    void Init();

    /// <summary>Readies a new mission: the empty slot's picture, the tac map, the drop zone markers; the mission tab.</summary>
    void DrawBackground();

    /// <summary>Displays the screen; starts or restarts the operation movie when it is due, or ends its window after it.</summary>
    void Display() override;

    /// <summary>
    /// Port: draws the screen from its state (<see cref="PaintLook"/>) and the shared places
    /// (<see cref="MCLogScreenChrome"/>) in the frame pass. The original's draw did nothing (the screen was drawn
    /// piecewise into its picture).
    /// </summary>
    void Draw() override;

    /// <summary>Port: the screen draws itself each frame (its port is a view).</summary>
    bool DrawsLive() override { return true; }

    /// <summary>Port: see <see cref="MCLogScreenChrome"/>.</summary>
    MCOptionalRef<MCLogScreenChrome> Chrome() override { return ScreenChrome; }

    /// <summary>Stops the movie, lets the mission pane go of its picture, and frees the pictures and the look.</summary>
    void OnDestroy() override;

    /// <summary>The multiplayer version of <see cref="CalcTonnages"/>: every lance counts.</summary>
    void MpCalcTonnages();

    /// <summary>Adds up <see cref="CurDeployTonnage"/> from the lances the player can fill.</summary>
    void CalcTonnages();

    /// <summary>Port: a new figure ends a press shown on the launch button (the bar is drawn each frame).</summary>
    void DrawTonnageBar();

    /// <summary>The tabs (operation, mission), the launch and screen buttons, help texts, cheat keys and timers.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    void ShowGuiWindow(bool show) override;

    /// <summary>Shows the operation tab and starts its movie (or, in multiplayer, the chat).</summary>
    void SetUpOperation();

    /// <summary>Shows the mission tab with its briefing text.</summary>
    void SetUpMission();

    /// <summary>Stops and frees the operation movie.</summary>
    void StopSmackerMovies();

    /// <summary>
    /// Refills the deploy scroll pane with a block for every undeployed mech and vehicle, and shows the drop slots again
    /// (<see cref="ShowSlots"/>).
    /// </summary>
    void SetUpDeploy();

    /// <summary>
    /// Port: matches the drop slots' blocks to the deploy data (<see cref="SlotUnit"/>): a slot whose unit changed gets
    /// a block of the new one (or none); the block lifted out of a slot goes once its drag is over. The original moved
    /// each unit's own block onto the screen and off it.
    /// </summary>
    void ShowSlots();

    /// <summary>
    /// Port: the unit in drop slot <paramref name="slot"/>: the local player's from the deploy slots, else (multiplayer)
    /// a teammate's from the side's drop slots; null for none.
    /// </summary>
    static std::shared_ptr<MCLogPart> SlotUnit(size_t slot);

    /// <summary>Port: the block showing <paramref name="part"/> (in a drop slot or the deploy pane), or null; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCMechBriefBlock> BlockOf(const MCLogPart& part) const;

    /// <summary>
    /// Shows <paramref name="part"/>'s briefing in the box area: the box shown already if it is that unit's (its weapon
    /// list refilled at the same scroll), else a new box (the old one goes).
    /// </summary>
    /// <remarks>The original kept a box per unit (made with it) and moved the one shown onto the screen.</remarks>
    void ShowBriefing(const std::shared_ptr<MCLogPart>& part);

    /// <summary>The box area shows blank: the box shown goes.</summary>
    void HideBriefing();

    /// <summary>
    /// Port: draws the screen from its state into <paramref name="target"/> (screen coordinates): the art, the map, the
    /// lance labels, the tonnage bar and launch button, the tab, the operation area, the drop slots with their units
    /// and the briefing box. The original painted these into the screen's picture as events changed them.
    /// </summary>
    void PaintLook(const MCPane& target);

    /// <summary>Port: a new screen-sized picture holding <see cref="PaintLook"/> (for transitions).</summary>
    std::shared_ptr<MCLogPort> NewLookPicture();

    /// <summary>
    /// Port: <paramref name="block"/> was picked up out of its slot (which shows empty again): it lives on, off the
    /// slots, while it is dragged.
    /// </summary>
    void LiftFromSlot(const MCMechBriefBlock& block);

    /// <summary>Lance <paramref name="lance"/>'s tonnage as its label shows it (what <see cref="CalcTonnages"/> counts).</summary>
    static int32_t LanceTons(int32_t lance);

    /// <summary>Whether <paramref name="part"/> fits under the drop tonnage limit.</summary>
    static bool FitsTonnage(const MCLogPart& part);

    /// <summary>The mech list indices of the mechs with a pilot waiting to be deployed, in list order.</summary>
    std::vector<int32_t> UndeployedMechs;
    /// <summary>Set when the operation movie should (re)start; cleared once it has.</summary>
    bool PlayMovie = false;
    /// <summary>While set, the screen and tab buttons are ignored (multiplayer launch in progress).</summary>
    bool ButtonsLocked = false;
    /// <summary>Set while the chat button blinks (timer 5; multiplayer).</summary>
    bool ChatBlinking = false;
    /// <summary>The chat button's blink phase.</summary>
    bool ChatBlinkOn = false;
    /// <summary>Set while the chat blink timer (id 5) runs (started by <c>Logistics::setUpBriefingScreen</c>).</summary>
    bool ChatTimerOn = false;
    /// <summary>The mission briefing text pane (a child).</summary>
    MCGuiHandle<MCScrollPane> MissionPane;
    /// <summary>The picture the mission text is formatted into (the logistics screens make it per mission).</summary>
    std::shared_ptr<MCLogPort> MissionPort;
    /// <summary>The operation picture (shown when the movie is over; the logistics screens load it per operation).</summary>
    std::shared_ptr<MCLogPort> OperationPicture;
    /// <summary>A copy of an empty drop slot (under a block dragged out of one).</summary>
    std::shared_ptr<MCLogPort> EmptySlot;
    /// <summary>The chat button's lit picture (<c>lsbdw08</c>).</summary>
    std::shared_ptr<MCLogPort> ChatBlinkPort;
    /// <summary>The chat button's normal picture (<c>lsbdw03</c>).</summary>
    std::shared_ptr<MCLogPort> ChatRegularPort;
    /// <summary>The window the operation movie plays in while it plays (a child; it closes the movie).</summary>
    MCGuiHandle<MCGuiSmackerWindow> SmackerWindow;
    /// <summary>The briefing box shown (a child; none: the box area is blank).</summary>
    MCGuiHandle<MCBriefingBox> BriefingBox;
    /// <summary>The drop slots on the screen (left, top, right, bottom), lance by lance.</summary>
    std::array<RECT, NumDropSlots> SlotRects = {};
    /// <summary>The pane of undeployed mech and vehicle blocks (a child; a list of <see cref="DeployUnits"/>).</summary>
    MCGuiHandle<MCListView> DeployPane;
    /// <summary>
    /// Port: the units the deploy pane lists, taken by <see cref="SetUpDeploy"/> (the undeployed mechs with a pilot,
    /// then the undeployed vehicles): what the original's pane held blocks of.
    /// </summary>
    std::vector<std::shared_ptr<MCLogPart>> DeployUnits;
    /// <summary>The tab shown (<see cref="OperationTab"/>, <see cref="MissionTab"/>).</summary>
    int32_t CurrentTab = 0;
    /// <summary>Set once the operation movie has been started by <see cref="Display"/>.</summary>
    bool MovieStarted = false;

    /// <summary>Port: what the screen shows of the shared places.</summary>
    MCLogScreenChrome ScreenChrome;

    // Port: the screen's own state that PaintLook draws (the rest is the game's: the drop slots, the tab, the chat
    // blink, the tonnages). drawBackground (a new mission) resets it.

    /// <summary>Port: the mission's tac map picture, turned onto the map area (null before drawBackground).</summary>
    std::shared_ptr<MCLogPort> MapPicture;
    /// <summary>Port: the first drop zone marked on the map (multiplayer), or -1.</summary>
    int32_t MarkedZone = -1;
    /// <summary>Port: the map's world units per pixel, for the drop zone markers.</summary>
    float MarkerScale = 0.0f;
    /// <summary>Port: the launch button was clicked: it shows pressed until the release (or while locked).</summary>
    bool LaunchPressed = false;
    /// <summary>
    /// Port: how far the operation area has got: its art shown (<see cref="SetUpOperation"/>), and the operation
    /// picture over it (before the movie). The movie's closing art shows over both once the movie is over.
    /// </summary>
    bool OperationShown = false;
    bool OperationPictureShown = false;
    /// <summary>Port: the block in each drop slot, if any (children, made by <see cref="ShowSlots"/>).</summary>
    std::array<MCGuiHandle<MCMechBriefBlock>, NumDropSlots> SlotBlocks;

private:
    /// <summary>Port: the block lifted out of a slot, dragged (a child, drawn in no slot) until <see cref="ShowSlots"/>.</summary>
    MCGuiHandle<MCMechBriefBlock> _LiftedBlock;

    /// <summary>Port: back to the screen's art alone: no units in the slots, no box, no operation art.</summary>
    void ClearLook();

    /// <summary>The help line for whatever the mouse is over at (<paramref name="xPos"/>, <paramref name="yPos"/>), and the highlighted screen button.</summary>
    void ShowHoverHelp(int32_t xPos, int32_t yPos);

    /// <summary>A left click at (<paramref name="xPos"/>, <paramref name="yPos"/>) (screen relative): the screen buttons, the tabs, launch.</summary>
    void HandleClick(int32_t xPos, int32_t yPos);

    /// <summary>The launch button: drops the force (single player), or says it's ready (multiplayer).</summary>
    void Launch();

    /// <summary>Port: draws the tonnage bar into <paramref name="target"/>.</summary>
    static void PaintTonnageBar(const MCPane& target, int32_t maxTons, int32_t tons, bool hammerDown);
};

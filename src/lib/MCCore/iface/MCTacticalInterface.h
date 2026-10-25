#pragma once

#include "gui/MCGuiOwned.h"
#include "iface/MCInterfaceTypes.h"
#include "lib/MCVector2D.h"
#include "lib/MCOptionalRef.h"
#include "lib/MCVector3D.h"

class MCBaseObject;
class MCCommandParser;
class MCFloatHelp;
class MCFriendlyMechIcon;
class MCGameObject;
class MCGuiCallback;
class MCGuiEvent;
class MCGuiObject;
class MCMechBar;
class MCMover;
class MCMoverGroup;
class MCOrderSink;
class MCTacticalOrder;
class MCViewWindow;

/// <summary>
/// The player's command interface in a mission: the mech bar, the current selection of movers and lances, the mode,
/// the key bindings and the mouse state over the map. The game has one (<see cref="TacticalInterface"/>), made at
/// start-up and kept until shutdown; a scenario starts and ends it.
/// </summary>
/// <remarks>
/// Original source: <c>iface\iface.cpp</c> (<c>InterfaceObject</c>).
/// <para>
/// Each key binding in <see cref="Keys"/> packs a key code in bits 0-15 and the modifiers the event must carry:
/// <see cref="KeyShift"/>, <see cref="KeyCtrl"/>, <see cref="KeyAlt"/>. Assigning a key code keeps the modifier bits
/// (a bitfield struct in the original).
/// </para>
/// </remarks>
class MCTacticalInterface
{
public:
    /// <summary>
    /// The movers that can be selected at once (kept: a player's force; the multiplayer order and group messages carry
    /// a player's movers as 12 bits).
    /// </summary>
    static constexpr size_t MaxSelectedMovers = 12;
    /// <summary>The lances that can be selected at once (kept: the commander's four lances).</summary>
    static constexpr size_t MaxSelectedLances = 4;
    /// <summary>How far a zoom key press zooms (about four presses from one end to the other).</summary>
    static constexpr float ZoomKeyStep = 1.5f;
    /// <summary>How far a notch of the mouse wheel zooms.</summary>
    static constexpr float ZoomWheelStep = 1.1f;

    /// <summary>An interface with no windows yet (<see cref="Init"/> makes them).</summary>
    MCTacticalInterface();
    /// <summary>Takes down the mech bar, the parser and the floating tags.</summary>
    ~MCTacticalInterface();
    MCTacticalInterface(const MCTacticalInterface&) = delete;
    MCTacticalInterface& operator=(const MCTacticalInterface&) = delete;

    /// <summary>
    /// Makes the mech bar and the twelve floating tags, reads <c>iface.fit</c> (drag distance, scroll speeds, shuffle
    /// frames) and sets the default key bindings.
    /// </summary>
    /// <returns>0, or the ini error.</returns>
    int32_t Init();

    /// <summary>
    /// Every tactical-screen event: keys (modes, map pages, zoom, lances), clicks and drags on the map and the order
    /// they give, and part-object creation/destruction messages.
    /// </summary>
    void HandleEvent(MCGuiEvent& event);
    /// <summary>
    /// The zoom-in key: the camera's view shows <paramref name="factor"/> times fewer lines of the world (eased), down
    /// to the closest zoom, with the original's sound; the tactical map's zoom button follows. (The original switched
    /// the camera between scales 100 and 1.) The mouse wheel zooms without the sound.
    /// </summary>
    static void ZoomIn(float factor = ZoomKeyStep, bool sound = true);
    /// <summary>The zoom-out key, as <see cref="ZoomIn"/>: <paramref name="factor"/> times more lines.</summary>
    static void ZoomOut(float factor = ZoomKeyStep, bool sound = true);

    /// <summary>Makes the command parser, starts the edge scrolling and the lance icons, and takes the player's keys.</summary>
    void StartScenario();

    /// <summary>
    /// Port: takes the player's key bindings (<see cref="MCKeyBindings::Player"/>), with the salvage page's key
    /// swapped for the chat page's in multiplayer unless the player gave it another (StartScenario calls it).
    /// </summary>
    void TakePlayerKeys();
    /// <summary>Drops the selection, parser, buttons, lance icons and reserve icons.</summary>
    void EndScenario();

    /// <summary>
    /// Makes an icon for the mover with part id <paramref name="partId"/>: on the mech bar in lance
    /// <paramref name="lance"/> when <paramref name="onBar"/>, else kept aside in <see cref="ReserveIcons"/>.
    /// </summary>
    void AddMech(int32_t partId, int32_t lance, bool active, bool onBar);
    /// <summary>Brings a mover's icon into play and re-places the buttons.</summary>
    void ActivateMech(int32_t partId) const;
    /// <summary>Drops a (dead) mover from the selection, its lance's selection and the parser.</summary>
    void RemoveMech(int32_t partId);
    /// <summary>Redraws the mech bar.</summary>
    void UpdateInterface() const;

    bool IsSelected(int32_t partId) const;
    bool IsSelected(const MCMoverGroup& group) const;
    /// <summary>Selects the mover with part id <paramref name="partId"/> (unless it is disabled, or the selection is full).</summary>
    void SelectMech(int32_t partId);
    /// <summary>Selects every player mover on screen.</summary>
    void SelectVisibleMechs();
    /// <summary>
    /// Deselects the mover; a lance it was selected through breaks up, its other movers staying selected on their own.
    /// </summary>
    void DeselectMech(int32_t partId);
    void DeselectEnemy() const;
    /// <summary>Selects a lance: a linked lance as a whole, an unlinked one mover by mover.</summary>
    void SelectLance(std::weak_ptr<MCMoverGroup> group);
    void DeselectLance(std::shared_ptr<MCMoverGroup> group);
    void ClearMechSelection();
    /// <summary>Whether part id <paramref name="partId"/> is one of the player's movers on the mech bar.</summary>
    bool IsOurs(int32_t partId) const;
    /// <summary>
    /// Whether anything is selected; with <paramref name="armed"/>, whether a selected mover is alive with weapons
    /// (can take an attack).
    /// </summary>
    bool AnySelected(bool armed = false) const;

    /// <summary>
    /// Works out what the mouse is over (icon, object, terrain), whether it can be captured, and sets the cursor for
    /// the current mode. <paramref name="event"/> may be null (the per-frame callback).
    /// </summary>
    void UpdateMouseState(MCOptionalRef<MCGuiEvent> event);

    /// <summary>
    /// Calls an artillery strike of <paramref name="strikeType"/> at <paramref name="position"/> (or at
    /// <paramref name="target"/>'s): for the home commander's team when <paramref name="forCommander"/>, else for the
    /// Inner Sphere (or the clans with <paramref name="forClans"/>); in multiplayer it goes over the network.
    /// </summary>
    static void CallStrike(int32_t strikeType, std::optional<MCVector3D> position, std::shared_ptr<MCGameObject> target,
                           bool forCommander, bool forClans, float delay);
    /// <summary>Hides the twelve floating tags.</summary>
    void HideTags();
    /// <summary>Makes the selected <paramref name="movers"/> lance <paramref name="groupId"/> and relinks their icons.</summary>
    void SetUnit(int32_t groupId, std::span<const std::shared_ptr<MCMover>> movers, int32_t pointIndex) const;
    /// <summary>Marks (or unmarks) a mover's icon as its lance's point.</summary>
    void SetPoint(int32_t partId, bool isPoint) const;
    /// <summary>Picks the cursor's direction (<see cref="CursorOffset"/>) from the selection's centre to <paramref name="screenPos"/>.</summary>
    void SetCursorOffset(MCVector2D screenPos);
    /// <summary>Whether the one selected mover, a live refit vehicle, can refit <paramref name="target"/>.</summary>
    bool RefitCheck(std::shared_ptr<MCGameObject> target) const;
    /// <summary>
    /// Whether <paramref name="target"/>, a repair bay of the player's with repair points, can fix the one selected
    /// mover (mech bays fix mechs, vehicle bays vehicles) within 100 units.
    /// </summary>
    bool GetFixedCheck(std::shared_ptr<MCGameObject> target) const;
    /// <summary>The icon of the mover with part id <paramref name="partId"/> (on the bar or in reserve), or null.</summary>
    std::shared_ptr<MCFriendlyMechIcon> GetMechIconFromID(int32_t partId) const;

    /// <summary>Whether every selected mover can jump (the tactical map's jump button too). Original name lost.</summary>
    bool CanSelectionJump() const;
    /// <summary>
    /// Whether the selection (movers and lances) can jump to <paramref name="position"/>: passable, not onto one of the
    /// player's movers, and within every mover's jump range (from its last queued point with
    /// <paramref name="fromWayPoint"/>). Original name lost.
    /// </summary>
    bool CanSelectionJumpTo(MCVector3D position, std::shared_ptr<MCGameObject> target, bool fromWayPoint) const;

    /// <summary>Sets the mode, for more than one order (<see cref="OneShotMode"/> cleared).</summary>
    void SetMode(MCInterfaceMode mode)
    {
        CurrentMode = mode;
        OneShotMode = false;
    }

    /// <summary>Where the interface's orders go: the game's sink unless a test put its own in.</summary>
    MCOrderSink& Orders() const;
    /// <summary>Sends the orders to <paramref name="sink"/> (null: the game's).</summary>
    void SetOrderSink(std::shared_ptr<MCOrderSink> sink) { _OrderSink = std::move(sink); }

    /// <summary>The binding of <paramref name="command"/>.</summary>
    uint32_t Key(MCKeyCommand command) const { return Keys[static_cast<size_t>(command)]; }

    /// <summary>Lets go of every scroll key (their key-ups never come once the window loses focus).</summary>
    void ReleaseScrollKeys();

    /// <summary>Restores every live mech bar mover's armour, internal structure, weapons and ammunition (a cheat).</summary>
    void CheatHealAll() const;
    /// <summary>Sets every live mech bar mover's pilot gunnery skill to 120 (a cheat).</summary>
    void CheatDeadEye() const;

    /// <summary>Shows <paramref name="mover"/>'s callsign and name on <paramref name="tag"/> (not yet shown).</summary>
    static void TagMover(MCFloatHelp& tag, MCGameObject& mover);

    /// <summary>The world point under the last map click.</summary>
    MCVector3D MouseWorldPos;
    /// <summary>The key that rotates the camera while held (0x38, alt).</summary>
    int16_t RotateKey = 0;
    /// <summary>The direction the arrow keys scroll the map (0-7 clockwise from up; -1 = none).</summary>
    int32_t ScrollDirection = -1;
    /// <summary>The direction the ctrl+arrow keys scroll the tactical map (-1 = none).</summary>
    int32_t TacScrollDirection = -1;
    /// <summary>Which map scroll keys are held: up, down, left, right (the order of their bindings).</summary>
    std::array<bool, 4> ScrollKeysHeld{};
    /// <summary>Which tactical map scroll keys are held, as <see cref="ScrollKeysHeld"/>.</summary>
    std::array<bool, 4> TacScrollKeysHeld{};
    /// <summary>The mode the next click's order is given in.</summary>
    MCInterfaceMode CurrentMode = MCInterfaceMode::Unset;
    /// <summary>
    /// Set when the mode was picked for one order only (a command palette button): the parser goes back to
    /// <see cref="MCInterfaceMode::None"/> after sending it.
    /// </summary>
    bool OneShotMode = false;
    /// <summary>How many frames the mech bar's buttons take to dance into place (<c>Shuffle Frames</c>, 15).</summary>
    int16_t ShuffleFrames = 0;
    /// <summary>
    /// Whether the object under the mouse can be captured: capturable, in sight, not the player's, and the mode is
    /// none or run (<see cref="UpdateMouseState"/>).
    /// </summary>
    bool CanCapture = false;
    /// <summary>Whether that capture is blocked (the object has a capture blocker against the player).</summary>
    bool CaptureBlocked = false;
    /// <summary>The cursor's direction variant, from <see cref="SetCursorOffset"/>.</summary>
    int32_t CursorOffset = 0;
    std::unique_ptr<MCCommandParser> CommandParser;
    MCGuiOwned<MCMechBar> MechBar;
    /// <summary>Icons of movers not on the mech bar (<see cref="AddMech"/> off the bar).</summary>
    std::vector<std::shared_ptr<MCFriendlyMechIcon>> ReserveIcons;
    /// <summary>How far the mouse must move with a button down to start a drag (<c>Drag Distance</c>).</summary>
    int16_t DragDistance = 10;
    /// <summary><c>Scroll Speed</c>.</summary>
    int16_t ScrollSpeed = 4;
    /// <summary><c>Tac Scroll Speed</c>.</summary>
    int16_t TacScrollSpeed = 1;
    /// <summary><c>Scroll Start</c>: the delay before edge scrolling starts.</summary>
    int16_t ScrollStart = 500;
    /// <summary>Part ids of the selected movers.</summary>
    std::vector<int32_t> SelectedMovers;
    /// <summary>The selected lances.</summary>
    std::vector<std::weak_ptr<MCMoverGroup>> SelectedLances;
    /// <summary>Where the left button went down (screen), to tell a click from a drag.</summary>
    float MouseDownX = 0.0f;
    float MouseDownY = 0.0f;
    /// <summary>Whether the left button is down on the map.</summary>
    bool MouseDown = false;
    /// <summary>What the mouse is over.</summary>
    MCMouseTarget MouseTarget = MCMouseTarget::NotYetUpdated;
    /// <summary>The object (or icon's mover) under the mouse.</summary>
    std::weak_ptr<MCBaseObject> MouseObject;
    /// <summary>The enemy selected as the target.</summary>
    std::weak_ptr<MCGameObject> SelectedEnemy;
    /// <summary>The key bindings by <see cref="MCKeyCommand"/>, packed as described in the class remarks.</summary>
    std::array<uint32_t, NumKeyCommands> Keys{};
    /// <summary>Whether the tactical map's large view is held up (<see cref="MCKeyCommand::HoldTacticalMap"/>).</summary>
    bool TacMapShown = false;
    /// <summary>The floating tags shown over movers (kept: one per mover of a full force).</summary>
    std::array<MCGuiOwned<MCFloatHelp>, MaxSelectedMovers> FloatingTags;
    /// <summary>Whether a forced-order key is held: the next click's order is queued.</summary>
    bool ForcingOrder = false;
    /// <summary>The forced order a click gives where the mouse is now.</summary>
    MCForcedOrder ForcedOrder = MCForcedOrder::None;
    /// <summary>The map view a selection box is being dragged on, or null.</summary>
    std::weak_ptr<MCViewWindow> DragTarget;

private:
    /// <summary>A key going down: the mode, map page, zoom, scroll or lance it picks.</summary>
    void HandleKeyDown(MCGuiEvent& event);
    /// <summary>A key coming up: forced orders end, scrolling stops, held keys act (lance links, artillery).</summary>
    void HandleKeyUp(MCGuiEvent& event);
    /// <summary>A mouse event on the map: a click's order, a forced order, or a selection box.</summary>
    void HandleMouse(MCGuiEvent& event);
    /// <summary>The order a click gives, by what it is on (<see cref="MouseTarget"/>) and the mode.</summary>
    void HandleClick(MCGuiEvent& event);
    /// <summary>Sends <paramref name="order"/> as a forced order to the selected movers' pilots (and the server).</summary>
    void QueueForcedOrder(MCTacticalOrder& order);
    /// <summary>Picks the cursor for the mode over what the mouse is on (<see cref="UpdateMouseState"/>'s end).</summary>
    void UpdateCursor(MCGuiObject& window, MCVector2D mousePos);
    /// <summary>The cursor over an ally or a plain spot, by the mode.</summary>
    void PlainCursor(MCGuiObject& window, MCVector2D mousePos);

    /// <summary>The order sink a test put in, or null for the game's.</summary>
    std::shared_ptr<MCOrderSink> _OrderSink;
    /// <summary>The edge-scrolling callback (<c>ScrollScreen</c>), live during a mission.</summary>
    std::shared_ptr<MCGuiCallback> _ScrollCallback;
    /// <summary>
    /// The building, turret, gate or terrain object the mouse last highlighted (<c>setSelected(1)</c>); unhighlighted
    /// when the mouse leaves it.
    /// </summary>
    std::weak_ptr<MCGameObject> _HighlightedObject;
};

/// <summary>The game's tactical interface (null before start-up makes it).</summary>
std::shared_ptr<MCTacticalInterface> TacticalInterface();

/// <summary>Per-frame callback: updates the mouse state once the mission is past its first turn.</summary>
void UpdateMouseStateCallback();

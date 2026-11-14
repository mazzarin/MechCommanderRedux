#pragma once

// Original source: mcx\logistics.cpp, the logistics (between-missions) layer: the player's mechs, vehicles, pilots
// and components, the screens that buy, repair and deploy them, campaign loading and saving, and the multiplayer
// force exchange.

#include "gui/MCGuiRef.h"
#include "logistics/MCDragIcon.h"
#include "logistics/MCLogObject.h"
#include "main/MCDropSlot.h"
#include "main/MCInventoryList.h"
#include "main/MCLogMechList.h"
#include "main/MCLogVehicleList.h"
#include "main/MCLogWarriorList.h"
#include "main/MCMPPlayerLights.h"
#include "platform/MCRegisteredBlock.h"

class MCBriefingScreen;
class MCFidpMessage;
class MCFitIniFile;
class MCLogChat;
class MCLogChatWindow;
class MCLogInvScreen;
class MCPacketFile;
struct MCPane;
class MCPurchaseDlg;
class MCPurchaseScreen;
class MCPurMechList;
class MCPurPilotList;
class MCPurVehicleList;
class MCRefitDialog;
class MCRepairScreen;
class MCReusableDialog;
class MCSessionScreen;
class MCSplashScreen;
class MCTicker;
class MCTickerLine;
struct MCDeployForce;
enum class MCPilotRank : int32_t;

namespace MCTelemetry
{
    struct MCRefusedLoad;
}

/// <summary>
/// The logistics layer: every screen (main menu, multiplayer, load/save, preferences, briefing, purchase, repair,
/// session), the player's force and inventory, the art they share; it runs campaign loading/saving and the
/// multiplayer force exchange. One instance, <see cref="GlobalLog"/>, owned by the mission (shared, so the global
/// refers to it by weak_ptr).
/// </summary>
/// <remarks>
/// Original source: <c>logistics.cpp</c>, 0x1908 bytes (allocated in <c>mission.cpp</c>). The original's
/// <c>init</c> is <see cref="Start"/> (the empty object is what tests build), its <c>destroy</c> the destructor.
/// </remarks>
class MCLogistics : public std::enable_shared_from_this<MCLogistics>
{
public:
    /// <summary>The drop zone's lances.</summary>
    static constexpr size_t NumLances = 3;
    /// <summary>The slots of a lance.</summary>
    static constexpr size_t LanceSlots = 4;
    /// <summary>The drop slots (three lances of four: the force size, a game rule).</summary>
    static constexpr size_t NumDropSlots = NumLances * LanceSlots;
    /// <summary>The drop zones a mission has at most: three for each side (asserted when read).</summary>
    static constexpr size_t MaxDropZones = 6;
    /// <summary>The players of one side whose units logistics keeps (six players in all, three a side).</summary>
    static constexpr size_t SidePlayers = 3;

    /// <summary>An empty logistics layer: the unit lists and inventories, no screens (tests fill what they need).</summary>
    MCLogistics();

    /// <summary>
    /// Takes down what <see cref="Start"/> made (multiplayer state, art, dialogs, lists and screens, in the original's
    /// order), then the rest.
    /// </summary>
    ~MCLogistics();

    MCLogistics(const MCLogistics&) = delete;
    MCLogistics& operator=(const MCLogistics&) = delete;

    /// <summary>
    /// Makes every screen and dialog, loads the shared art, shapes and sort tables, and shows the main screen (or the
    /// ready room when launched from a lobby). Makes this the <see cref="GlobalLog"/> (it must be shared).
    /// </summary>
    void Start();

    /// <summary>
    /// Port-only: ties the mech and vehicle icons' damage shapes (<c>mi##.shp</c>, <c>vi1##.shp</c>) to the HD art
    /// pack's (the mech bar's icons), and marks the repair shapes' diagrams (the store's stocked rows' and the mech
    /// bay's: <see cref="MechArmorShapes"/>, <see cref="MechInternalShapes"/>, <see cref="VehicleArmorShapes"/>) and
    /// sold-out outlines (<see cref="MechSoldOutShape"/>, <see cref="VehicleSoldOutShape"/>) as line art, drawn as
    /// smooth strokes (MCHdArt::SetLineArtFrames). Start does it; again when HD art comes on.
    /// </summary>
    void AttachHdArt();

    /// <summary>
    /// Port-only: on a sharp screen (HD art, the GPU drawing), what the HD menus first show done ahead, off the game's
    /// thread: every font's sharp glyphs made (platform/MCSharpPictures) and the HD copies of the pictures the screens
    /// are made of read (MCHdArt::ReadPicturesAhead), so a screen's first frame neither redraws its text nor reads the
    /// pack. The end of Start does it, and AttachHdArtToLoaded when HD art comes on.
    /// </summary>
    void MakeAhead() const;

    /// <summary>
    /// Port-only: on a sharp screen, has the pack's reading thread read the HD copies of the pictures every live texture
    /// shows (MCHdArt::ReadPicturesAhead): MakeAhead's for the menus, and a mission's interface as its load ends.
    /// </summary>
    static void ReadTexturesAhead();

    /// <summary>
    /// Port-only: on a sharp screen, the line art of the units the screens list (the shop's and the player's mechs and
    /// vehicles: their diagrams and sold-out outlines) made ahead, off the game's thread (platform/MCSharpPictures).
    /// The briefing, purchase and repair screens ask as they're set up; what's made is never made again.
    /// </summary>
    void MakeLineArtAhead() const;

    /// <summary>
    /// Port-only: the screens a slide from the one shown may bring in, readied hidden in the fade into it
    /// (<see cref="ReadyForSlide"/>, RehearseHidden), so the slide neither waits for them nor draws them first: from
    /// the briefing, the purchase screen (its store the most to make) then the repair screen; none from any other (they
    /// were readied on the way, and readying one while the other shows would scroll the inventory list shown: each tab
    /// fills both screens').
    /// </summary>
    std::vector<MCGuiHandle<MCLogInvScreen>> ScreensSlidIn() const;

    /// <summary>
    /// Port-only: hidden <paramref name="screen"/> (the purchase or repair screen) filled as its set-up will fill it for
    /// the inventory tab shown (<see cref="SetUpPurchaseScreen"/>, <see cref="SetUpRepairScreen"/>): its background and
    /// lists, the purchase screen's store too. Its set-up does it all again as it's shown, and nothing is selected.
    /// </summary>
    void ReadyForSlide(MCLogInvScreen& screen) const;

    /// <summary>
    /// The frames of a mech's repair shapes its diagram is drawn with, a frame a location: the armor (from 0) over the
    /// internal structure (from 0xb).
    /// </summary>
    static constexpr int32_t MechArmorShapes = 0;
    static constexpr int32_t MechInternalShapes = 0xb;
    static constexpr int32_t MechDiagramLocations = 8;
    /// <summary>The frames of a vehicle's repair shapes its diagram is drawn with, a frame a location (from 0).</summary>
    static constexpr int32_t VehicleArmorShapes = 0;
    static constexpr int32_t VehicleDiagramLocations = 5;
    /// <summary>The frame of a mech's repair shapes drawn as its sold-out outline (on the purchase screen).</summary>
    static constexpr int32_t MechSoldOutShape = 0x13;
    /// <summary>The frame of a vehicle's repair shapes drawn as its sold-out outline.</summary>
    static constexpr int32_t VehicleSoldOutShape = 6;

    /// <summary>Draws the damage state of location <paramref name="location"/> of a mech into <paramref name="port"/>.</summary>
    static void DrawMechBodyLoc(MCLogMech& mech, int32_t location, MCLogPort& port, int32_t xPos, int32_t yPos);

    /// <summary>Draws the damage state of location <paramref name="location"/> of a vehicle into <paramref name="port"/>.</summary>
    static void DrawVehicleBodyLoc(MCLogVehicle& vehicle, int32_t location, MCLogPort& port, int32_t xPos,
                                   int32_t yPos);

    /// <summary>
    /// Port: the damage state <see cref="DrawMechBodyLoc"/> draws location <paramref name="location"/> of a mech in
    /// (-1 undamaged, 0..3 worse).
    /// </summary>
    static int32_t MechBodyLocState(const MCLogMech& mech, int32_t location);

    /// <summary>
    /// Port: the damage state <see cref="DrawVehicleBodyLoc"/> draws location <paramref name="location"/> of a vehicle
    /// in; none for a location without armor, which it doesn't draw.
    /// </summary>
    static std::optional<int32_t> VehicleBodyLocState(const MCLogVehicle& vehicle, int32_t location);

    /// <summary>
    /// Port: draws diagram shape <paramref name="location"/> of <paramref name="shapes"/> (a mech's or vehicle's icon
    /// shapes) in damage state <paramref name="state"/> at (<paramref name="xPos"/>, <paramref name="yPos"/>) of
    /// <paramref name="port"/>, as <see cref="DrawMechBodyLoc"/> does.
    /// </summary>
    static void DrawBodyLoc(std::span<const uint8_t> shapes, int32_t location, int32_t state, MCLogPort& port,
                            int32_t xPos, int32_t yPos);

    /// <summary>Makes the multiplayer lists and drop slots and reads the net mech/warrior/vehicle lists.</summary>
    void InitializeMultiplayer();

    /// <summary>Frees what <see cref="InitializeMultiplayer"/> made.</summary>
    void DestroyMultiplayer();

    /// <summary>
    /// Port: makes the ready lights (the session screen made them when it opened) for the players <paramref name="sortedIds"/>,
    /// all on a team, for a match that never showed the session screen.
    /// </summary>
    void SetUpPlayerLights(std::span<const uint32_t> sortedIds);

    /// <summary>Shows or hides the current logistics screen (activating the logistics palette when <paramref name="redraw"/>).</summary>
    void ShowLogScreen(bool show, bool redraw) const;

    /// <summary>
    /// Switches to the main screen; leaving a multiplayer game takes the session down. <paramref name="fromMenu"/>:
    /// the main menu's own return, which keeps <see cref="PreviousState"/>.
    /// </summary>
    int32_t SetUpMainScreen(bool fromMenu);

    /// <summary>
    /// Reads the campaign's purchase file <paramref name="purchaseFile"/>: the costs, the campaign briefing, the
    /// current mission's purchase file, cinema and operation; then sets up the shop from <paramref name="file"/>.
    /// </summary>
    /// <returns>The current mission's purchase file.</returns>
    std::string SetUpCampaignPurchasing(std::string_view purchaseFile, MCPacketFile& file);

    /// <summary>Sets up what can be bought in a multiplayer game.</summary>
    void SetUpMPPurchasing(std::string_view purchaseFile);

    /// <summary>Port: takes the pilots for hire more skilled than <paramref name="most"/> off the shop (MOST SKILLED PILOT).</summary>
    void HidePilotsAbove(MCPilotRank most) const;

    /// <summary>
    /// Port: takes the components banned by <paramref name="bans"/> (MCWeaponBans.h) off the shop: not sold, off the
    /// mechs for sale (their price lowered by theirs), and the vehicles carrying one not sold.
    /// </summary>
    void HideBannedWeapons(uint32_t bans) const;

    /// <summary>Sets up what can be bought from a campaign's purchase packet (its last).</summary>
    void SetUpPurchasing(MCPacketFile& file);

    /// <summary>Changes what can be bought by an old-style purchase file (counts added to the shop's).</summary>
    void SetUpOldPurchasing(std::string_view purchaseFile) const;

    /// <summary>Switches to the purchase screen (with the slide when <paramref name="animate"/>).</summary>
    int32_t SetUpPurchaseScreen(bool animate);

    /// <summary>
    /// Painted the screen switch buttons on the current screen: each normal, the current screen's grayed, none lit.
    /// Port: the buttons are drawn each frame (<see cref="DrawScreenChrome"/>); this puts out the lit one.
    /// </summary>
    void DrawScreenButtons() const;

    /// <summary>
    /// Port: screen button <paramref name="button"/> of <paramref name="screen"/> is under the mouse and shows lit
    /// (the original copied the lit picture over it).
    /// </summary>
    static void HoverScreenButton(MCLogObject& screen, int32_t button);

    /// <summary>
    /// Port: draws the shared places of <paramref name="screen"/> from the state (<see cref="MCLogScreenChrome"/>): the
    /// multiplayer lights' backing, the screen buttons, the ticker line, the resource points and the clock.
    /// </summary>
    void DrawScreenChrome(MCLogObject& screen, const MCPane& target);

    /// <summary>
    /// Shows the ticker on <paramref name="screen"/>: its line there shows, in front, and runs the scroll timer; the
    /// other screens' lines hide (the original moved its one ticker onto the screen). Raised again with
    /// <paramref name="shuffle"/>, as at the start.
    /// </summary>
    void ShowTickerOn(MCLogObject& screen, bool shuffle = false);

    /// <summary>
    /// Makes the ready lights, none shown yet, and their view on each screen that can show them (the session, purchase,
    /// briefing and repair screens).
    /// </summary>
    void MakePlayerLights();

    /// <summary>
    /// Shows the ready lights on <paramref name="screen"/>, in front (at depth <paramref name="depth"/> when one is
    /// given); the other screens' views hide (the original moved its one lights object onto the screen).
    /// </summary>
    void ShowLightsOn(MCLogObject& screen, std::optional<int32_t> depth);

    /// <summary>Takes the ready lights and their views down (the original deleted its lights object).</summary>
    void ResetPlayerLights();

    /// <summary>Empties the multiplayer chat and every chat window's history (a room left).</summary>
    void ClearChat();

    /// <summary>The chat window on <paramref name="screen"/>, or null; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogChatWindow> ChatWindowOn(const MCLogObject& screen) const;

    /// <summary>
    /// Puts the chat on <paramref name="screen"/>: its window there takes over, in front, shown as the chat was, with
    /// the team choice, the caret's blink and the text focus of the window before it; that one hides (the original
    /// moved its one chat window onto the screen).
    /// </summary>
    void ShowChatOn(MCLogObject& screen);

    /// <summary>Shows or hides the chat on the screen it is on (remembered while it is on none).</summary>
    void ShowChat(bool show);

    /// <summary>Switches to the briefing screen (with the slide when <paramref name="animate"/>).</summary>
    int32_t SetUpBriefingScreen(bool animate);

    /// <summary>Switches to the multiplayer session screen.</summary>
    int32_t SetUpSessionScreen();

    /// <summary>Switches to the repair screen (with the slide when <paramref name="animate"/>).</summary>
    int32_t SetUpRepairScreen(bool animate);

    /// <summary>Loads a multiplayer quick-start force (mechs, pilots, vehicles) from <paramref name="file"/>.</summary>
    void LoadQuickStart(MCFitIniFile& file);

    /// <summary>Saves the campaign as <paramref name="fileName"/>.</summary>
    static int32_t SaveCampaign(std::string_view fileName);

    /// <summary>
    /// Loads a campaign (or a saved game) and its force: save <paramref name="saveName"/> with extension
    /// <paramref name="extension"/>; <paramref name="newCampaign"/> starts one, <paramref name="loadForce"/> reloads
    /// the force without the time passing. A save it refuses (non-zero) leaves no campaign: the lists it had emptied stay
    /// empty and <see cref="CurrentMission"/> is -1, so nothing returns to them until a campaign loads. The reports say
    /// why it was refused (MCTelemetry::LoadRefused).
    /// </summary>
    int32_t LoadCampaign(std::string_view saveName, std::string_view extension, bool newCampaign, bool loadForce);

    /// <summary>Writes the deployed force into the mission's start file <paramref name="startFile"/> and the profiles.</summary>
    int32_t PrepareScenario(std::string_view scenarioName, std::string_view startFile);

    /// <summary>Gives mech <paramref name="mechIndex"/> pilot <paramref name="pilotIndex"/> (a negative one takes it off).</summary>
    void SetPilot(int32_t mechIndex, int32_t pilotIndex) const;

    /// <summary>
    /// Takes a deployed <paramref name="mech"/> off its drop zone slot (telling the other players in a multiplayer
    /// game); it is no longer deployed.
    /// </summary>
    void UndeployMech(MCLogMech& mech);

    /// <summary>
    /// Updates the drop zone slots for force vehicle <paramref name="forceIndex"/> leaving the force list: its slot is
    /// cleared and the slots of the vehicles after it move up one place with them.
    /// </summary>
    void RemoveVehicleFromSlots(int32_t forceIndex);

    /// <summary>Moves assigned mechs into the force list and unassigned ones back, then renumbers their rows.</summary>
    void ReorderMechs() const;

    /// <summary>As <see cref="ReorderMechs"/> for vehicles.</summary>
    void ReorderVehicles() const;

    /// <summary>Moves assigned pilots into the assigned list and unassigned ones back, then renumbers their rows.</summary>
    void ReorderWarriors() const;

    /// <summary>Shifts the force mechs' pilot indexes from <paramref name="from"/> on by <paramref name="amount"/>.</summary>
    void ShiftPilots(int32_t from, int32_t amount) const;

    /// <summary>Whether every required mech and vehicle is in the drop.</summary>
    bool RequiredAssigned() const;

    /// <summary>Reads the current mission's tonnage, briefing, map and drop zones.</summary>
    void GetCurrentMission();

    /// <summary>
    /// Port: what a screen change slides over the screen's right part (<see cref="Transition"/>): paints the picture
    /// (<see cref="SlideWidth"/> x <see cref="SlideHeight"/>) into the port given, its corner at the port's (0, 0), as the
    /// original put it together in a work picture before the slide. The slide paints it each frame, so with HD art on it
    /// is drawn sharp.
    /// </summary>
    using MCSlidePicture = std::function<void(MCLogPort& picture)>;

    /// <summary>The slide's place on the screen and its size (the original's work pictures').</summary>
    static constexpr int32_t SlideX = 0xd3;
    static constexpr int32_t SlideY = 0x10;
    static constexpr int32_t SlideWidth = 0x1ab;
    static constexpr int32_t SlideHeight = 0x1ce;

    /// <summary>Slides from <paramref name="from"/> to <paramref name="to"/> (a screen change).</summary>
    void Transition(const MCSlidePicture& from, const MCSlidePicture& to, int direction) const;

    /// <summary>Darkens <paramref name="port"/> through the fade table.</summary>
    static void Darken(int32_t amount, MCColorTable fadeTable, MCLogPort& port);

    /// <summary>
    /// Port: <see cref="Darken"/> of a <paramref name="width"/> x <paramref name="height"/> block of
    /// <paramref name="port"/> at (<paramref name="xPos"/>, <paramref name="yPos"/>), drawn in place: the fade
    /// table's translate polygon over the block (the original copied the block out, translated it and copied it back,
    /// which comes out the same). On the GPU it is one blended quad.
    /// </summary>
    static void DarkenRect(MCLogPort& port, int32_t xPos, int32_t yPos, int32_t width, int32_t height,
                           MCColorTable fadeTable);

    /// <summary>Renumbers the component inventory's rows.</summary>
    int32_t ReIndexInventory() const;

    /// <summary>Handles a multiplayer "deploy force" message: adds the other player's mech or vehicle to its drop slot.</summary>
    void HandleDeployForceMessage(uint32_t playerID, std::span<const uint8_t> message);

    /// <summary>Handles a multiplayer "remove force" message: empties the other player's drop slot.</summary>
    void HandleRemoveForceMessage(uint32_t playerID, std::span<const uint8_t> message);

    /// <summary>Blinks the chat button and passes a chat message to the chat window.</summary>
    void HandleChatMessage(uint32_t playerID, std::span<const uint8_t> message);

    void SendRemoveForceMessage(int lance, int slot) const;

    void SendAddMechMessage(const MCLogMech& mech, int lance, int slot) const;

    void SendAddVehicleMessage(const MCLogVehicle& vehicle, int lance, int slot) const;

    /// <summary>A player left: tells the player (once any exiting dialog is answered).</summary>
    void HandleLostPlayer(uint32_t playerID, int hostLeft) const;

    /// <summary>
    /// A match's start (the host's START, its word to a guest, a configured match's): the multiplayer lists made and
    /// <paramref name="mission"/> (a bare name, as LoadCampaign takes it) loaded, then the briefing's screens shown.
    /// </summary>
    /// <returns>
    /// Whether it loaded. Port fix: the start went on after a refusal into the mech bay with nothing loaded, and the
    /// scenario's request then quit the game (an empty name, MCScenarioLoad's Fatal): the caller leaves instead.
    /// </returns>
    bool LoadMatchMission(std::string_view mission);

    /// <summary>The host started the mission: its scenario requested.</summary>
    /// <returns>Whether it was: port fix, only a match's mission loaded is (LoadMatchMission).</returns>
    bool HandlePrepareScenarioMessage() const;

    /// <summary>Writes the multiplayer start file <paramref name="startFile"/> (every player's drop slots) and the profiles.</summary>
    int32_t PrepareMultiplayerScenario(std::string_view scenarioName, std::string_view startFile);

    /// <summary>Feeds a typed key (a scan code) to the cheat code matcher.</summary>
    void ProcessCheatCode(int16_t key);

    /// <summary>Draws the bar of the pilot's skill <paramref name="skill"/> (an index into its skills).</summary>
    static void DrawPilotSkillBar(const MCLogWarrior& warrior, int32_t skill, int32_t xPos, int32_t yPos, int32_t row,
                                  int32_t width, int32_t rowHeight, MCLogPort& port);

    /// <summary>
    /// Draws a 4-pixel skill bar <paramref name="width"/> wide at (<paramref name="xPos"/>, <paramref name="yPos"/> +
    /// <paramref name="row"/> * <paramref name="rowHeight"/>), filled in proportion to <paramref name="value"/>
    /// between <c>MinPilotSkill</c> and <c>MaxPilotSkill</c>.
    /// </summary>
    static void DrawPilotSkillBar(int32_t value, int32_t xPos, int32_t yPos, int32_t row, int32_t width,
                                  int32_t rowHeight, MCLogPort& port);

protected:
    /// <summary>Marks which of the 12 drop slots are the local player's.</summary>
    void SetupSlotsForMultiplayer(int32_t playerIndex, int32_t numPlayers);

    /// <summary>
    /// The mech list of player <paramref name="playerID"/> (on the local side when <paramref name="teammate"/>); none
    /// when the player has none there.
    /// </summary>
    MCOptionalRef<MCLogMechList> FindMPMechList(uint32_t playerID, bool teammate);

    /// <summary>The vehicle list of player <paramref name="playerID"/>; none when the player has none there.</summary>
    MCOptionalRef<MCLogVehicleList> FindMPVehicleList(uint32_t playerID, bool teammate);

    /// <summary>Builds a mech (with its pilot and inventory) from a deploy force message.</summary>
    std::shared_ptr<MCLogMech> AddMechFromNetworkMessage(MCLogMechList& list, const MCDeployForce& force) const;

    /// <summary>Builds a vehicle (with its inventory) from a deploy force message.</summary>
    std::shared_ptr<MCLogVehicle> AddVehicleFromNetworkMessage(MCLogVehicleList& list,
                                                               const MCDeployForce& force) const;

    /// <summary>Empties drop slot <paramref name="slot"/> of the player's (or, without <paramref name="teamTable"/>, the opponents') table.</summary>
    bool RemoveForceAtDropSlot(int32_t slot, uint32_t playerID, bool teamTable);

public:
    /// <summary>A world position on the map.</summary>
    struct DropZonePosition
    {
        float X = 0;
        float Y = 0;
    };

    /// <summary>A drop slot of the local player: the index of the mech or vehicle placed there (-1 = none).</summary>
    struct DeploySlot
    {
        /// <summary>The mech's index in the force mech list, or -1.</summary>
        int32_t Unit = 0;
        /// <summary>The vehicle's index in the force vehicle list, or -1 (only read when <see cref="Unit"/> is -1).</summary>
        int32_t Vehicle = 0;
    };

    /// <summary>A drop slot's place relative to its zone (the mission's <c>OffsetX</c>, <c>OffsetY</c> and <c>Rotation</c>).</summary>
    struct DeploySlotInfo
    {
        float OffsetX = 0;
        float OffsetY = 0;
        float Rotation = 0;
    };

    /// <summary>The ticker: the help line's text and scroll position, shown by a line on each screen.</summary>
    std::shared_ptr<MCTicker> Ticker;
    /// <summary>The ticker's lines, one on each screen that shows it (children of the screens).</summary>
    std::vector<MCGuiHandle<MCTickerLine>> TickerLines;
    /// <summary>The multiplayer ready lights (shown by a view on each multiplayer screen), or null.</summary>
    std::shared_ptr<MCMPPlayerLights> PlayerLights;
    /// <summary>The current mission's number in the campaign (-1 = none).</summary>
    int32_t CurrentMission = 0;
    /// <summary>The campaign's purchase file (the save's "purchaseFile"; written to starting fits as PurchaseFile).</summary>
    std::string PurchaseFile;
    /// <summary>Every pilot the player has.</summary>
    std::unique_ptr<MCLogWarriorList> WarriorList;
    /// <summary>The pilots assigned to mechs.</summary>
    std::unique_ptr<MCLogWarriorList> AssignedWarriorList;
    /// <summary>Every mech the player has.</summary>
    std::unique_ptr<MCLogMechList> MechList;
    /// <summary>Every vehicle the player has.</summary>
    std::unique_ptr<MCLogVehicleList> VehicleList;
    /// <summary>The mechs in the force (repair and briefing screens).</summary>
    std::unique_ptr<MCLogMechList> ForceMechList;
    /// <summary>The vehicles in the force.</summary>
    std::unique_ptr<MCLogVehicleList> ForceVehicleList;
    /// <summary>The other multiplayer players' mechs: the local side's ([0]) and the other side's ([1]), per player.</summary>
    std::array<std::array<std::unique_ptr<MCLogMechList>, SidePlayers>, 2> MpMechLists;
    /// <summary>The other multiplayer players' vehicles.</summary>
    std::array<std::array<std::unique_ptr<MCLogVehicleList>, SidePlayers>, 2> MpVehicleLists;
    /// <summary>The pilots of mechs received over the network.</summary>
    std::unique_ptr<MCLogWarriorList> MpWarriorList;
    /// <summary>The mechs for sale (empty until a campaign's shop is read).</summary>
    std::unique_ptr<MCPurMechList> PurMechList;
    /// <summary>The vehicles for sale.</summary>
    std::unique_ptr<MCPurVehicleList> PurVehicleList;
    /// <summary>The pilots for hire.</summary>
    std::unique_ptr<MCPurPilotList> PurPilotList;
    /// <summary>The player's spare components.</summary>
    std::unique_ptr<MCInventoryList> ComponentInventory;
    /// <summary>The components for sale.</summary>
    std::unique_ptr<MCInventoryList> PurchaseComponents;
    /// <summary>Component master ids in range order (<c>logart\comp.rsp</c>).</summary>
    std::vector<uint32_t> RangeSortList;
    /// <summary>Which screen or sub-screen logistics is on (1 main, 2.. the others, as the callbacks set it).</summary>
    int32_t LogisticsState = 0;
    /// <summary>
    /// The <see cref="LogisticsState"/> before the main menu opened over it: the menu's return and a finished save
    /// go back to the purchase (2) or repair (4) screen, else the briefing.
    /// </summary>
    int32_t PreviousState = 0;
    /// <summary>
    /// Which of the 12 drop slots (three lances of four) may be filled: the local player's in multiplayer; in single
    /// player the briefing screen shows the lances whose slots are set.
    /// </summary>
    std::array<bool, NumDropSlots> LocalDropSlot{};
    /// <summary>A world position per drop zone: three for side 0, then three for side 1 (MultiPlayer homeTeam 1).</summary>
    std::array<DropZonePosition, MaxDropZones> DropZonePositions{};
    /// <summary>The local player's drop zone: per lance and slot, the mech or vehicle placed there.</summary>
    std::array<std::array<DeploySlot, LanceSlots>, NumLances> DeploySlots{};
    /// <summary>
    /// Per drop zone and slot, the unit's place relative to the zone (read by <see cref="GetCurrentMission"/>): the
    /// six zones of <see cref="DropZonePositions"/>. <see cref="Start"/> clears only the first three.
    /// </summary>
    std::array<std::array<DeploySlotInfo, LanceSlots>, MaxDropZones> DeploySlotPlacements{};
    /// <summary>The drop slots every force of the local side is placed in (multiplayer).</summary>
    std::array<std::array<MCDropSlot, LanceSlots>, NumLances> DropSlots{};
    /// <summary>The drop slots of the players not on the local player's team (multiplayer).</summary>
    std::array<std::array<MCDropSlot, LanceSlots>, NumLances> OpponentDropSlots{};
    /// <summary>The current mission's map file.</summary>
    std::string MissionFileName;
    /// <summary>The inventory tab shown: 0 mechs, 1 pilots, 2 components, 3 vehicles.</summary>
    int32_t CurrentInvTab = 0;
    /// <summary>The cost of an armor point (the purchase file's PurchaseCosts).</summary>
    int32_t ArmorCost = 0;
    /// <summary>The cost of an internal structure point.</summary>
    int32_t InternalCost = 0;
    /// <summary>The cost of engine work.</summary>
    int32_t EngineCost = 0;
    /// <summary>The cost of a green, regular, veteran and elite pilot.</summary>
    std::array<int32_t, 4> PilotCosts{};
    /// <summary>The price factor of clan technology.</summary>
    float ClanCostFactor = 0;
    /// <summary>The screen being shown (one of the screens below; null before the first).</summary>
    std::shared_ptr<MCLogObject> CurrentScreen() const { return _CurrentScreen.lock(); }

    /// <summary>Makes <paramref name="screen"/> the screen being shown (empty: none).</summary>
    void SetCurrentScreen(std::weak_ptr<MCLogObject> screen) { _CurrentScreen = std::move(screen); }

    /// <summary>Makes <paramref name="screen"/> the screen being shown.</summary>
    void SetCurrentScreen(MCLogObject& screen) { _CurrentScreen = screen.WeakAs<MCLogObject>(); }
    MCGuiHandle<MCSessionScreen> SessionScreen;
    MCGuiHandle<MCSplashScreen> SerialScreen;
    /// <summary>The multiplayer connection screen (the one after the protocol choice).</summary>
    MCGuiHandle<MCSplashScreen> ConnectScreen;
    MCGuiHandle<MCSplashScreen> ModemScreen;
    MCGuiHandle<MCSplashScreen> LanScreen;
    /// <summary>Port (Redux HD): INTERNET CONNECTION, the LAN screen's layout over the service's games (MCInternetMenu).</summary>
    MCGuiHandle<MCSplashScreen> InternetScreen;
    MCGuiHandle<MCSplashScreen> MainScreen;
    MCGuiHandle<MCSplashScreen> MultiplayerScreen;
    MCGuiHandle<MCSplashScreen> LoadScreen;
    MCGuiHandle<MCSplashScreen> SaveScreen;
    MCGuiHandle<MCSplashScreen> PrefScreen;
    MCGuiHandle<MCBriefingScreen> BriefingScreen;
    MCGuiHandle<MCPurchaseScreen> PurchaseScreen;
    MCGuiHandle<MCRepairScreen> RepairScreen;
    /// <summary>The preferences as they were when the preferences screen opened (restored by CancelPrefs).</summary>
    int32_t SavedPrefs0 = 0;
    int32_t SavedPrefs1 = 0;
    int32_t SavedPrefs2 = 0;
    uint32_t SavedPrefs3 = 0;
    uint32_t SavedPrefs4 = 0;
    uint32_t SavedPrefs5 = 0;
    int32_t SavedPrefs6 = 0;
    /// <summary>
    /// The current mission's operation number (the campaign's Operation; picks the briefing's operation picture),
    /// 0 when it has none.
    /// </summary>
    int32_t Operation = 0;
    /// <summary>The multiplayer chat (its history and the line being typed), or null in the demo.</summary>
    std::shared_ptr<MCLogChat> Chat;
    /// <summary>The chat's windows, one on each screen that shows it (the session and briefing screens' children).</summary>
    std::vector<MCGuiHandle<MCLogChatWindow>> ChatWindows;
    /// <summary>The chat window on the screen the chat is on (<see cref="ShowChatOn"/>); empty before it is on one.</summary>
    MCGuiHandle<MCLogChatWindow> ChatWindow;
    /// <summary>Whether the chat shows, while it is on no screen (<see cref="ShowChat"/>).</summary>
    bool ChatShown = false;
    /// <summary>The mech repair screen shapes (<c>mechrep##.shp</c>, by mech name index).</summary>
    std::array<MCRegisteredBlock, 24> MechRepShapes;
    /// <summary>The vehicle repair screen shapes (<c>vr1_##.shp</c>; none where the file is missing).</summary>
    std::array<MCRegisteredBlock, 35> VehicleRepShapes;
    /// <summary>The mech icon shapes (<c>mi##.shp</c>).</summary>
    std::array<MCRegisteredBlock, 24> MechIconShapes;
    /// <summary>The vehicle icon shapes (<c>vi1_##.shp</c>; none where the file is missing).</summary>
    std::array<MCRegisteredBlock, 35> VehicleIconShapes;
    /// <summary>Colour remap tables for drawing shapes (<c>VFX_shape_lookaside</c>).</summary>
    std::array<std::array<uint8_t, 256>, 10> ShapeLookaside{};
    /// <summary>Port-only: what the icon shapes' ties to the HD art pack last while it lives (AttachHdArt).</summary>
    std::shared_ptr<const int> HdArtOwner;
    /// <summary>The repair screen's mech picture background (<c>lsrupm00.tga</c>).</summary>
    std::shared_ptr<MCLogPort> RepairBackPort;
    /// <summary>The inventory block background (<c>invblock.tga</c>).</summary>
    std::shared_ptr<MCLogPort> InvBlockPort;
    /// <summary>The box behind the resource figure at the top right.</summary>
    std::shared_ptr<MCLogPort> ResourceBackPort;
    /// <summary>The box behind the clock at the top right.</summary>
    std::shared_ptr<MCLogPort> ClockBackPort;
    /// <summary>The repair screen pieces (<c>lsrupm03, 01, 04, 02, 06, 07.tga</c>).</summary>
    std::array<std::shared_ptr<MCLogPort>, 6> RepairPorts;
    /// <summary>The purchase screen pieces (<c>lspcb05, 07, 06, 09.tga</c>).</summary>
    std::array<std::shared_ptr<MCLogPort>, 4> PurchasePorts;
    /// <summary>The screen switch buttons' pictures: button 0, exit, buttons 1..3; normal, highlighted, gray.</summary>
    std::array<std::array<std::shared_ptr<MCLogPort>, 3>, 5> ScreenButtonPorts;
    /// <summary>The inventory tab icons: mechs, pilots, components, vehicles (<c>lscii?.tga</c>).</summary>
    std::array<std::shared_ptr<MCLogPort>, 4> InventoryIconPorts;
    /// <summary>
    /// The chat text colour of each player number (1, 3, 4, 2, 6, 5), used as <c>%fc</c> codes by
    /// <c>MCLogChatWindow::ProcessChatString</c>.
    /// </summary>
    std::array<int32_t, 6> PlayerColors{};
    /// <summary>Each component's place in the logistics sort order (<c>objsort.rsp</c>; one per master id).</summary>
    std::array<int32_t, 256> ComponentSort{};
    /// <summary>The icon following the mouse while an inventory row is dragged, a child of its screen (<see cref="MCDragIcon::Create"/>, <see cref="MCDragIcon::Remove"/>).</summary>
    MCGuiHandle<MCDragIcon> DragIcon;
    /// <summary>The id the next <see cref="MCLogWarrior"/> gets.</summary>
    int32_t NextWarriorID = 0;
    MCGuiHandle<MCPurchaseDlg> PurchaseDialog;
    /// <summary>The one-button (or yes/no) message dialog.</summary>
    MCGuiHandle<MCReusableDialog> MessageDialog;
    /// <summary>The yes/no dialog.</summary>
    MCGuiHandle<MCReusableDialog> QuestionDialog;
    MCGuiHandle<MCRefitDialog> RefitDialog;
    /// <summary>The campaign's CampaignBriefing Filename (empty when it has none).</summary>
    std::string CampaignBriefingName;
    /// <summary>The mission's OperationCinema: the briefing movie played in <c>data\movies\</c> (empty when it has none).</summary>
    std::string OperationCinema;
    /// <summary>Set when the mission file has a HammerDown1 block (or by the cheat): the drop tonnage limit is not enforced.</summary>
    bool HammerDown = false;
    /// <summary>The mission's AutoPlay: the briefing screen starts the operation movie when first shown.</summary>
    bool AutoPlayMovie = false;
    /// <summary>The mechs allowed in multiplayer (<c>netmechs.rsp</c>; three variants per name).</summary>
    std::vector<std::string> NetMechNames;
    /// <summary>The pilots allowed in multiplayer (<c>netwars.rsp</c>).</summary>
    std::vector<std::string> NetWarriorNames;
    /// <summary>The vehicles allowed in multiplayer (<c>netvhcls.rsp</c>).</summary>
    std::vector<std::string> NetVehicleNames;
    /// <summary>Set once <see cref="InitializeMultiplayer"/> ran.</summary>
    bool MultiplayerInitialized = false;
    /// <summary>The default multiplayer planning time in seconds (240).</summary>
    uint32_t DefaultPlanningTime = 0;
    /// <summary>The multiplayer planning time: a save's PlanningTime, else <see cref="DefaultPlanningTime"/>.</summary>
    uint32_t PlanningTime = 0;
    /// <summary>The multiplayer mission name.</summary>
    std::string MpMissionName;

private:
    /// <summary>
    /// <see cref="LoadCampaign"/>'s reading: empties the lists, then reads the save into them, noting in
    /// <paramref name="read"/> what it read of the file and, when it refuses it, why.
    /// </summary>
    int32_t ReadCampaign(std::string_view saveName, std::string_view extension, bool newCampaign, bool loadForce,
                         MCTelemetry::MCRefusedLoad& read);

    /// <summary>Set by <see cref="Start"/>: the destructor takes the screens and art down.</summary>
    bool _Started = false;
    /// <summary>The screen being shown (one of the screens it owns).</summary>
    std::weak_ptr<MCLogObject> _CurrentScreen;
};

/// <summary>
/// The player's name for the multiplayer screens: the name last entered (the registry's "Player Name"), else the
/// logged-in user's; none when neither is known or fits 63 characters.
/// </summary>
std::optional<std::string> MyGetUserName();

/// <summary>A dialog's answer: cancels (and quits a game launched from a lobby).</summary>
void CancelBool(int32_t answer);

/// <summary>The "player left" dialog's answer.</summary>
void LostPlayerHandler(int32_t answer);

/// <summary>The multiplayer chat handler while in logistics.</summary>
void LogisticsChatCallback(MCFidpMessage& message);

/// <summary>The logistics screens while they exist (a view: <c>Mission()-&gt;Logistics</c> owns them).</summary>
/// <summary>
/// The logistics screens while they exist (null otherwise: <c>Mission()-&gt;Logistics</c> owns them); the caller keeps
/// it as its pin.
/// </summary>
std::shared_ptr<MCLogistics> GlobalLog();

/// <summary>Makes <paramref name="logistics"/> the one <see cref="GlobalLog"/> returns (empty: none).</summary>
void SetGlobalLog(std::weak_ptr<MCLogistics> logistics);
/// <summary>The logistics state last left for a mission (shared with <c>mission.cpp</c>).</summary>
extern int32_t LastLogisticsMissionState;
/// <summary>The "player left" message <see cref="LostPlayerHandler"/> shows.</summary>
extern std::string HoldString;
/// <summary>Which logistics cheat codes still match what was typed.</summary>
extern std::array<bool, 7> LogCheatActive;
/// <summary>The multiplayer players' colors (gamesys.fit's mPlayerColors).</summary>
extern std::array<int32_t, 6> MultiPlayerColors;
/// <summary>How many characters of a cheat code have been typed.</summary>
extern int32_t LogCurCheatChar;
/// <summary>Nonzero in the demo version.</summary>
extern int InDemo;
/// <summary>
/// A single (non-campaign) mission is being played: units sell back at full price (as in multiplayer) rather than
/// half.
/// </summary>
extern bool Solo;

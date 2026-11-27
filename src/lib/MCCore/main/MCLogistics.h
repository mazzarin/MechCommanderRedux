#pragma once

// Original source: mcx\logistics.cpp, the logistics (between-missions) layer: the player's mechs, vehicles, pilots
// and components, the screens that buy, repair and deploy them, campaign loading and saving, and the multiplayer
// force exchange.

#include "gui/MCGuiRef.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCLogPort.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCRoomModel.h"
#include "main/MCDropSlot.h"
#include "main/MCInventoryList.h"
#include "main/MCLogMechList.h"
#include "main/MCLogVehicleList.h"
#include "main/MCLogWarriorList.h"
#include "main/MCMPPlayerLights.h"
#include "platform/MCRegisteredBlock.h"
#include "sprite/MCHdArt.h"

class MCFidpMessage;
class MCFitIniFile;
class MCLogChat;
class MCMenuDialog;
class MCPacketFile;
struct MCPane;
class MCPurMechList;
class MCPurPilotList;
class MCPurVehicleList;
struct MCDeployForce;
enum class MCPilotRank : int32_t;

namespace MCTelemetry
{
    struct MCRefusedLoad;
}

/// <summary>
/// The logistics layer: the menus' models (the room, the chat, the dialogs, the ready lights), the player's force and
/// inventory, the art they share; it runs campaign loading/saving and the multiplayer force exchange. One instance,
/// <see cref="GlobalLog"/>, owned by the mission (shared, so the global refers to it by weak_ptr).
/// </summary>
/// <remarks>
/// Original source: <c>logistics.cpp</c>, 0x1908 bytes (allocated in <c>mission.cpp</c>). The original's
/// <c>init</c> is <see cref="Start"/> (the empty object is what tests build), its <c>destroy</c> the destructor.
/// </remarks>
/// <summary>
/// The logistics' own art files, which <see cref="MCLogistics::Start"/> loads before the screens draw from them (each is
/// only drawn from): a base of <see cref="MCLogistics"/>, so they can be kept across a mission whole
/// (<see cref="MCLogisticsArt"/>).
/// </summary>
struct MCLogisticsArtFiles
{
    /// <summary>The mech icon shapes (<c>mi##.shp</c>).</summary>
    std::array<MCRegisteredBlock, 24> MechIconShapes;
    /// <summary>The vehicle icon shapes (<c>vi1_##.shp</c>; none where the file is missing).</summary>
    std::array<MCRegisteredBlock, 35> VehicleIconShapes;
};

/// <summary>
/// Port-only (SPEC-menus.md's stage 2: the menus' data outlives a mission): what the art files were loaded from, the
/// same files only when all three are as then.
/// </summary>
struct MCLogisticsArtKey
{
    /// <summary><c>MCReleaseData::Switches()</c>: the game's files (a match's or single player's).</summary>
    uint32_t Switches = 0;
    /// <summary><c>MCHdArt::NumberedFolders()</c>: a shape file read as the pack's copy.</summary>
    bool Numbered = false;
    /// <summary><c>MCHdArt::PackFolder()</c>.</summary>
    std::filesystem::path Pack;

    bool operator==(const MCLogisticsArtKey&) const = default;
};

/// <summary>
/// Port-only (SPEC-menus.md's stage 2: the menus' data outlives a mission): a logistics' art kept by the mission while
/// it plays (<see cref="MCLogistics::TakeArt"/>) and taken back by the next logistics if it would load the same files
/// (<see cref="MCLogistics::ReuseArt"/>), so the way back makes only the screens again.
/// </summary>
struct MCLogisticsArt
{
    MCLogisticsArtKey Key;
    MCLogisticsArtFiles Files;
    /// <summary><c>LogArt</c>'s pictures (logistics/MCLogPort.h).</summary>
    MCLogArtCache Loaded;
};

class MCLogistics
    : public std::enable_shared_from_this<MCLogistics>
    , public MCLogisticsArtFiles
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
    /// Takes down what <see cref="Start"/> made (multiplayer state, art, dialogs and lists, in the original's order),
    /// then the rest.
    /// </summary>
    ~MCLogistics();

    MCLogistics(const MCLogistics&) = delete;
    MCLogistics& operator=(const MCLogistics&) = delete;

    /// <summary>
    /// Makes the dialogs' models, loads the shared art, shapes and sort tables, and shows the main menu (or the ready
    /// room when launched from a lobby). Makes this the <see cref="GlobalLog"/> (it must be shared).
    /// </summary>
    void Start();

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 2): hands over this logistics' art (<see cref="MCLogisticsArtFiles"/> and
    /// <c>LogArt</c>'s), leaving it none, for the mission to keep while it plays; none before <see cref="Start"/>.
    /// </summary>
    std::shared_ptr<MCLogisticsArt> TakeArt();

    /// <summary>
    /// Port-only: the art a logistics before this one loaded (<see cref="TakeArt"/>), which <see cref="Start"/> takes
    /// instead of loading it again when it was loaded from the same files (<see cref="MCLogisticsArtKey"/>).
    /// </summary>
    void ReuseArt(std::shared_ptr<MCLogisticsArt> art);

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
    /// shows (MCHdArt::ReadPicturesAhead) as <paramref name="as"/>: MakeAhead's for the menus, and a mission's
    /// interface as its load ends and as the scenario starts.
    /// </summary>
    static void ReadTexturesAhead(MCHdArt::ReadAs as = MCHdArt::ReadAs::Set);

    /// <summary>Port-only: a picture's 8-bit pixels, row by row, and its size (<see cref="BriefingMapOf"/>).</summary>
    struct MCPicturePixels
    {
        std::vector<uint8_t> Pixels;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    /// <summary>
    /// Port-only: the map campaign mission <paramref name="scenario"/>'s briefing shows (its mission file's MapFile, as
    /// GetCurrentMission reads it, and the map's <c>.log.tga</c>, as MCBriefingScreen loads it); none when the mission
    /// file or the map can't be read (the briefing's set-up reports it).
    /// </summary>
    static std::optional<MCPicturePixels> BriefingMapOf(std::string_view scenario);

    /// <summary>
    /// Port-only: the map picture at game path <paramref name="path"/> (a map's <c>.log.tga</c>); none when it can't
    /// be read.
    /// </summary>
    static std::optional<MCPicturePixels> MapPictureAt(const std::string& path);

    /// <summary>
    /// Port-only: the game path of the map <see cref="BriefingMapOf"/> reads for <paramref name="scenario"/>
    /// (<c>TerrainPath</c>'s <c>&lt;MapFile&gt;.log.tga</c>); none when the mission file names none.
    /// </summary>
    static std::optional<std::string> BriefingMapFile(std::string_view scenario);

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 9): the game path of the briefing text campaign mission <paramref name="scenario"/>
    /// names (its mission file's BriefingFile, as GetCurrentMission reads it into BriefingTextPath), for the loading
    /// screen of a load with no logistics before it (-mission's game segment); none when the mission file names none.
    /// </summary>
    static std::optional<std::string> BriefingTextFile(std::string_view scenario);

    /// <summary>
    /// Port-only: on a sharp screen, has the pack's readers read the HD copy of <see cref="BriefingMapOf"/>
    /// <paramref name="scenario"/> ahead of the briefing's set-up: the way back from a mission's results asks as its
    /// fade begins, so the briefing made near black finds it read.
    /// </summary>
    static void ReadBriefingMapAhead(std::string_view scenario);

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

    /// <summary>
    /// Port-only: the parts of frame <paramref name="frame"/> of mech <paramref name="mech"/>'s repair shapes
    /// (<c>mechrep##.shp</c>) that line art draws by hand (the Atlas's eyes); none for the rest.
    /// </summary>
    static std::vector<MCSharpLines::MCDrawnLoop> RepairShapesDrawn(size_t mech, int32_t frame);

    /// <summary>
    /// Port: the damage state the diagrams draw location <paramref name="location"/> of a mech in (-1 undamaged, 0..3
    /// worse): the weaker of a torso's front and rear armor.
    /// </summary>
    static int32_t MechBodyLocState(const MCLogMech& mech, int32_t location);

    /// <summary>
    /// Port: the damage state the diagrams draw location <paramref name="location"/> of a vehicle in; none for a location
    /// without armor, which they don't draw.
    /// </summary>
    static std::optional<int32_t> VehicleBodyLocState(const MCLogVehicle& vehicle, int32_t location);

    /// <summary>
    /// Port: draws diagram shape <paramref name="location"/> of <paramref name="shapes"/> (a mech's or vehicle's icon
    /// shapes) in damage state <paramref name="state"/> at (<paramref name="xPos"/>, <paramref name="yPos"/>) of
    /// <paramref name="port"/>.
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

    /// <summary>Switches to the main menu; leaving a multiplayer game takes the session down.</summary>
    int32_t SetUpMainScreen();

    /// <summary>
    /// Port-only (stage 9, step 2): the main menu's multiplayer teardown (SetUpMainScreen's logic): the match's
    /// logistics and game let go, the chat's blinks stopped, the purchase dialog hidden.
    /// </summary>
    void LeaveMatchAtMainMenu();

    /// <summary>
    /// Reads the campaign's purchase file <paramref name="purchaseFile"/>: the costs, the campaign briefing, the
    /// current mission's purchase file, cinema and operation; then sets up the shop from <paramref name="file"/>.
    /// </summary>
    /// <returns>The current mission's purchase file.</returns>
    std::string SetUpCampaignPurchasing(std::string_view purchaseFile, MCPacketFile& file);

    /// <summary>
    /// Port-only: <see cref="SetUpCampaignPurchasing"/>'s reading as it goes, a part at a time (the campaign's load's
    /// steps): the shop's packet open, how far its mechs are read.
    /// </summary>
    struct MCShopReading;

    /// <summary>
    /// <see cref="SetUpCampaignPurchasing"/> begun: the costs, the cinemas and the operation's picture read, the shop
    /// emptied and its packet in <paramref name="file"/> opened, its gifts given.
    /// </summary>
    std::shared_ptr<MCShopReading> BeginCampaignShop(std::string_view purchaseFile, MCPacketFile& file);

    /// <summary>A slice of the shop's mechs read (at least one, while any are left); whether any are left.</summary>
    bool ReadShopMechs(MCShopReading& shop);

    /// <summary>The shop's vehicles and components read.</summary>
    void ReadShopGoods(MCShopReading& shop);

    /// <summary>The rest of the shop read (its pilots): the current mission's purchase file.</summary>
    std::string EndCampaignShop(MCShopReading& shop);

    /// <summary>Sets up what can be bought in a multiplayer game.</summary>
    void SetUpMPPurchasing(std::string_view purchaseFile);

    /// <summary>Port: takes the pilots for hire more skilled than <paramref name="most"/> off the shop (MOST SKILLED PILOT).</summary>
    void HidePilotsAbove(MCPilotRank most) const;

    /// <summary>
    /// Port: takes the components banned by <paramref name="bans"/> (MCWeaponBans.h) off the shop: not sold, off the
    /// mechs for sale (their price lowered by theirs), and the vehicles carrying one not sold.
    /// </summary>
    void HideBannedWeapons(uint32_t bans) const;

    /// <summary>Changes what can be bought by an old-style purchase file (counts added to the shop's).</summary>
    void SetUpOldPurchasing(std::string_view purchaseFile) const;

    /// <summary>Reads the inventory's sort orders (ART's <c>logart\\comp.rsp</c>, <c>objsort.rsp</c>).</summary>
    void ReadSortTables();

    /// <summary>
    /// Reads <c>gamesys.fit</c>'s pilots' values (their skills' bounds, weights and ranks) and the players' colours.
    /// </summary>
    void ReadGameSystemValues();

    /// <summary>
    /// Port-only (Redux HD): reads the sort orders and the game system's values again when what's read has changed
    /// since (a match begun or ended: lib/MCReleaseData.h), so a match plays the release's and single player the game
    /// folder's.
    /// </summary>
    void ReadKeptTablesIfSwitched();
    /// <summary>Loads the art files (<see cref="MCLogisticsArtFiles"/>) but the inventory tabs' icons.</summary>
    void LoadArtFiles();
    /// <summary>Takes <see cref="ReuseArt"/>'s art when it was loaded from the files this one would load.</summary>
    /// <returns>Whether it was taken (so not to be loaded).</returns>
    bool TakeBackArt();

    /// <summary>Makes the ready lights (the remade room, briefing and loading screens show them).</summary>
    void MakePlayerLights();

    /// <summary>Takes the ready lights down (the original deleted its lights object).</summary>
    void ResetPlayerLights();

    /// <summary>Empties the multiplayer chat's history (a room left).</summary>
    void ClearChat();

    /// <summary>Switches to the briefing's screens (the remade BRIEFING and DEPLOY).</summary>
    int32_t SetUpBriefingScreen();

    /// <summary>Switches to the multiplayer session screen.</summary>
    int32_t SetUpSessionScreen();

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 2): <see cref="SetUpSessionScreen"/>'s set-up of the room (the last session's
    /// lists let go of, its players and picks emptied, a host's word to its guests to show it), without showing it: at a network's word, as the original set it
    /// up, so what the host says next lands in it while the screen shown fades out.
    /// </summary>
    void ReadySessionScreen();

    /// <summary>Port-only: <see cref="SetUpSessionScreen"/>'s showing of the room, readied (<see cref="ReadySessionScreen"/>).</summary>
    void ShowSessionScreen();

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

    /// <summary>
    /// Port-only (SPEC-menus.md's changes of screen): <see cref="LoadCampaign"/> as a change's pieces, one a step of its
    /// reading (the palette, the save, the force, the components, the mission, the shop, the inventory), for the job of
    /// a change from its press, so no frame holds the whole load. After the last, <paramref name="result"/> holds
    /// LoadCampaign's result; a refusal skips the steps after it. Dropped before the end (the change called off), it
    /// leaves no campaign, as a refusal does.
    /// </summary>
    std::vector<MCScreenChange::MCPiece> LoadCampaignPieces(std::string_view saveName, std::string_view extension,
                                                            bool newCampaign, bool loadForce,
                                                            const std::shared_ptr<int32_t>& result);

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

    /// <summary>
    /// Moves assigned mechs into the force list and unassigned ones back, then renumbers their rows and the force's
    /// (<see cref="NumberForce"/>).
    /// </summary>
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

    /// <summary>Renumbers the component inventory's rows.</summary>
    int32_t ReIndexInventory() const;

    /// <summary>
    /// Numbers the spare mechs' rows (their <c>ListIndex</c>: the order of the mech list), as the original's
    /// CreateMechInvBlock did after each change to the list.
    /// </summary>
    void NumberMechs() const;

    /// <summary>Numbers the spare vehicles' rows (their <c>ListIndex</c>: the order of the vehicle list).</summary>
    void NumberVehicles() const;

    /// <summary>
    /// Numbers the force's units (their <c>SlotIndex</c>: the mechs in order, then the vehicles), as the 1998 'MECH BAY's
    /// force list did as it showed them, after every unit in or out (<see cref="ReorderMechs"/>).
    /// </summary>
    void NumberForce() const;

    /// <summary>
    /// Readies the store's descriptions (each mech's variant shown, the vehicles', the components', the pilots' for
    /// hire) as the 1998 store's panes did as the shop was read (PrepareInfoDescription).
    /// </summary>
    void ReadyStoreDescriptions() const;

    /// <summary>
    /// Numbers the spare MechWarriors' rows (their <c>ListIndex</c>: the order of the pilot list), which a MechWarrior's
    /// assignment and dismissal move the others' by.
    /// </summary>
    void NumberPilots() const;

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

    /// <summary>The multiplayer ready lights (the remade screens show them), or null.</summary>
    std::shared_ptr<MCMPPlayerLights> PlayerLights;
    /// <summary>The current mission's number in the campaign (-1 = none).</summary>
    int32_t CurrentMission = 0;
    /// <summary>The campaign's purchase file (the save's "purchaseFile"; written to starting fits as PurchaseFile).</summary>
    std::string PurchaseFile;
    /// <summary>The count of MCReleaseData::Switches the sort orders and game system's values were read at.</summary>
    uint32_t KeptTablesSwitches = 0;
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
    /// Which of the 12 drop slots (three lances of four) may be filled: the local player's in multiplayer; in single
    /// player the briefing screen shows the lances whose slots are set.
    /// </summary>
    std::array<bool, NumDropSlots> LocalDropSlot{};
    /// <summary>A world position per drop zone: three for side 0, then three for side 1 (MultiPlayer homeTeam 1).</summary>
    std::array<DropZonePosition, MaxDropZones> DropZonePositions{};

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 9): where a match's briefing marks its team's drop zones
    /// (MCCampaignMenu::ZoneTags), and DEPLOY's briefing map a campaign's or a match's (MCCampaignMenu::DropZones), as
    /// 1998's briefing worked it out from the mission's map (<see cref="GetCurrentMission"/>).
    /// </summary>
    struct MCZoneMarks
    {
        /// <summary>The team's first drop zone (the Clans' the last three); -1 in single player.</summary>
        int32_t Zone = -1;
        /// <summary>The world units to a map pixel (the 1998 briefing map's scale).</summary>
        float Scale = 0.0f;
        /// <summary>The briefing map picture's size; 0 when it can't be read.</summary>
        int32_t MapWidth = 0;
        int32_t MapHeight = 0;
    };

    MCZoneMarks ZoneMarks;
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
    /// <summary>
    /// Port-only (SPEC-menus.md's stage 9): <paramref name="id"/> (on <paramref name="page"/>) the screen logistics is
    /// on, the flow's top for the game's logistics (MCMenuScreens::Top); another than before hides the remade screens
    /// shown (its own shows), as a 1998 screen coming up did. A change of screen made outside a change of the flow's,
    /// with the menus up, is counted (MCScreenFlow::Unrouted).
    /// </summary>
    void NoteTop(MCScreenFlow::MCScreenId id,
                 MCMenuScreens::MCCampaignPage page = MCMenuScreens::MCCampaignPage::Briefing);

    /// <summary>Port-only (SPEC-menus.md's stage 4): the multiplayer room's state, which the room's screens view.</summary>
    MCRoomModel Room;
    /// <summary>
    /// The current mission's operation number (the campaign's Operation; picks the briefing's operation picture),
    /// 0 when it has none.
    /// </summary>
    int32_t Operation = 0;
    /// <summary>The multiplayer chat (its history and the line being typed), or null in the demo.</summary>
    std::shared_ptr<MCLogChat> Chat;
    /// <summary>Colour remap tables for drawing shapes (<c>VFX_shape_lookaside</c>).</summary>
    std::array<std::array<uint8_t, 256>, 10> ShapeLookaside{};
    /// <summary>Port-only: what the icon shapes' ties to the HD art pack last while it lives (AttachHdArt).</summary>
    std::shared_ptr<const int> HdArtOwner;
    /// <summary>
    /// The chat text colour of each player number (1, 3, 4, 2, 6, 5), used as <c>%fc</c> codes in the chat's lines
    /// (MCLogChat).
    /// </summary>
    std::array<int32_t, 6> PlayerColors{};
    /// <summary>Each component's place in the logistics sort order (<c>objsort.rsp</c>; one per master id).</summary>
    std::array<int32_t, 256> ComponentSort{};
    /// <summary>The id the next <see cref="MCLogWarrior"/> gets.</summary>
    int32_t NextWarriorID = 0;
    /// <summary>The one-button (or yes/no) message dialog.</summary>
    std::shared_ptr<MCMenuDialog> Message;
    /// <summary>The yes/no dialog.</summary>
    std::shared_ptr<MCMenuDialog> Question;
    /// <summary>The refit list (the 1998 'MECH BAY's).</summary>
    std::shared_ptr<MCMenuDialog> Refit;
    /// <summary>The campaign's CampaignBriefing Filename (empty when it has none).</summary>
    std::string CampaignBriefingName;
    /// <summary>
    /// Port-only (SPEC-menus.md's stage 8): the briefing text's file GetCurrentMission read (the mission's BriefingFile, a
    /// match's side's), which the remade BRIEFING reads into its parts.
    /// </summary>
    std::string BriefingTextPath;
    /// <summary>The mission's OperationCinema: the briefing movie played in <c>data\movies\</c> (empty when it has none).</summary>
    std::string OperationCinema;
    /// <summary>Set when the mission file has a HammerDown1 block (or by the cheat): the drop tonnage limit is not enforced.</summary>
    bool HammerDown = false;
    /// <summary>
    /// The mission's AutoPlay (the campaign file's, on each operation's first mission): the briefing starts the operation
    /// movie when first shown, and clears it (the remade BRIEFING's MCCampaignScreen::OnFrame).
    /// </summary>
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
    /// A campaign's load as it goes, from one step of <see cref="LoadCampaign"/>'s reading to the next: the save named,
    /// its files open, what it read of the file and, when it refuses it, why (for the reports).
    /// </summary>
    struct MCCampaignReading;

    /// <summary>
    /// A step of <see cref="LoadCampaign"/>'s reading: its frame log part, and its reading (0, or the refusal), run again
    /// while it leaves more to do (MCCampaignReading's Again).
    /// </summary>
    struct MCCampaignStep
    {
        std::string_view Part;
        int32_t (MCLogistics::*Read)(MCCampaignReading& reading);
    };

    /// <summary>The steps of <see cref="LoadCampaign"/>'s reading, in order.</summary>
    static std::span<const MCCampaignStep> CampaignSteps();

    /// <summary>The steps: the palette and the tables kept across missions.</summary>
    int32_t ReadCampaignPalette(MCCampaignReading& reading);
    /// <summary>The lists emptied, the save opened and its start read (the planet, the shop's file, the mission).</summary>
    int32_t ReadCampaignStart(MCCampaignReading& reading);
    /// <summary>The force's pilots, its mechs, its vehicles.</summary>
    int32_t ReadCampaignForce(MCCampaignReading& reading);
    int32_t ReadCampaignMechs(MCCampaignReading& reading);
    int32_t ReadCampaignVehicles(MCCampaignReading& reading);
    /// <summary>The components the game knows, and the save's.</summary>
    int32_t ReadCampaignComponents(MCCampaignReading& reading);
    /// <summary>The mission now, or the last mission's results applied.</summary>
    int32_t ReadCampaignMission(MCCampaignReading& reading);
    /// <summary>The shop of this point in the campaign begun (a match's read whole).</summary>
    int32_t ReadCampaignShop(MCCampaignReading& reading);
    /// <summary>A slice of the shop's mechs (the step runs again while any are left).</summary>
    int32_t ReadCampaignShopMechs(MCCampaignReading& reading);
    /// <summary>The shop's vehicles and components; then its pilots, the mission start's save, the killed pilots gone.</summary>
    int32_t ReadCampaignShopGoods(MCCampaignReading& reading);
    int32_t ReadCampaignShopEnd(MCCampaignReading& reading);
    /// <summary>The inventory's blocks.</summary>
    int32_t ReadCampaignBlocks(MCCampaignReading& reading);

    /// <summary>The reading's end: a refusal leaves no campaign, and the reports say why. Its result.</summary>
    int32_t EndCampaignRead(MCCampaignReading& reading);

    /// <summary>Set by <see cref="Start"/>: the destructor takes the screens and art down.</summary>
    bool _Started = false;
    /// <summary>The art <see cref="ReuseArt"/> handed over, until <see cref="Start"/> takes it or drops it.</summary>
    std::shared_ptr<MCLogisticsArt> _ReusedArt;
    /// <summary>What <see cref="Start"/> loaded or took its art from.</summary>
    MCLogisticsArtKey _ArtKey;
    /// <summary>Port-only: set while <see cref="Start"/> makes the screens (its screen changes are the menus' first).</summary>
    bool _Starting = false;
    /// <summary>The screen logistics is on (<see cref="NoteTop"/>).</summary>
    MCScreenFlow::MCScreenId _Top = MCScreenFlow::NoScreen;
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

/// <summary>Which resource figure the screens show (0: the resource points; 1 and 2 were the logistics heap's).</summary>
extern int32_t ResourceDisplayState;

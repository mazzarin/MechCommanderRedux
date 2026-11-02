#pragma once

#include <list>

#include "EditorTile.h"
#include "MissionText.h"
#include "MissionTimeOfDay.h"
#include "MissionWeather.h"

class ControlLink;
class DaoDatabase;
class EditorObject;
class GlobalMap;
class ScenarioMap;
class MapMarker;
class MoverObject;
class Objective;
class StructureRecord;

// The map the editor edits (the document's map, DreditorDoc +0x68; the class at 0x00450960-0x0045e170 and more). Its
// source file name wasn't kept; the names are the port's. Translated so far: construction, tile and vertex access,
// the projection between map and view coordinates (in x87 extended precision, as the original computes it), the
// terrain height, drawing the terrain, objects and footprints, placing objects, and reading a map's .elv and .obj
// files.

/// <summary>Cells per tile side, as the map's cell grid counts them.</summary>
inline constexpr int32_t MAP_CELLS_PER_TILE = 3;
/// <summary>Tiles per side of a terrain block in the .elv and .obj files.</summary>
inline constexpr int32_t MAP_BLOCK_TILES = 20;
/// <summary>Tiles per row of the drawn-tile bits (the largest map's side).</summary>
inline constexpr int32_t MAP_VISIBLE_ROW = 0xf0;
/// <summary>Words of the drawn-tile bits: each holds 8 tiles in its low byte.</summary>
inline constexpr size_t MAP_VISIBLE_WORDS = 0x1c20;

/// <summary>The colour of the tiles' outlines when the gridlines are on (0x00527de0).</summary>
extern int32_t gridLineColor;
/// <summary>The colour of a selected tile's outline (0x00527de8).</summary>
extern int32_t selectedTileColor;
/// <summary>The colour of the selected vertices' squares (0x00527ddc, yellow).</summary>
extern COLORREF vertexMarkColor;
/// <summary>Whether a selected vertex shows its elevation (0x00527df0, on).</summary>
extern bool showVertexHeights;
/// <summary>The colour of the control structures' link lines (0x00527df8).</summary>
extern COLORREF linkLineColor;

/// <summary>A view coordinate <see cref="EditorMap::removeMark"/> takes as "every mark of the marker" (0x004fea50).</summary>
inline constexpr int32_t MARK_ANYWHERE = 0x7fffffff;

/// <summary>
/// Where <see cref="EditorMap::regionOf"/> puts a tile: inside the play area, outside it, on its edge (0x004fea58,
/// 0x004fea5c, 0x004fea60). It is also the tile's draw mode.
/// </summary>
inline constexpr int32_t REGION_INSIDE = 1;
inline constexpr int32_t REGION_OUTSIDE = 0;
inline constexpr int32_t REGION_EDGE = -1;

/// <summary>A tool's mark: a shape at a view position.</summary>
struct MapMark
{
    /// <summary>The view position.</summary>
    int32_t x;
    int32_t y;
    /// <summary>The shape (the tool's).</summary>
    MapMarker* marker;
};

/// <summary>The editor's map: tiles, the objects on them and the cell grid the objects occupy.</summary>
/// <remarks>
/// 0x7114 bytes in the original, with a virtual table (0x004fea78); <see cref="SingleMap"/> and
/// <see cref="MultiMap"/> derive from it.
/// </remarks>
class EditorMap
{
public:
    /// <summary>
    /// A map of <paramref name="maxRow"/> x <paramref name="maxCol"/> new tiles (<see cref="EditorTile::EditorTile"/>), the
    /// tiles in the cut-off corners marked, and an empty cell grid.
    /// </summary>
    EditorMap(int32_t maxRow, int32_t maxCol);
    /// <summary>Deletes the tiles and the placed objects.</summary>
    virtual ~EditorMap();
    EditorMap(const EditorMap&) = delete;
    EditorMap& operator=(const EditorMap&) = delete;

    /// <summary>Writes the mission <paramref name="name"/>: its records in <paramref name="database"/> and its files.</summary>
    /// <returns>Whether it was written.</returns>
    /// <remarks>Virtual table slot 1.</remarks>
    virtual bool saveMission(DaoDatabase& database, CString& name) = 0;
    /// <summary>Checks that the mission can be saved, saying why not in a message.</summary>
    /// <remarks>Virtual table slot 2; the arguments aren't used.</remarks>
    virtual bool checkMission(DaoDatabase& database, const CString& name) = 0;
    /// <summary>Reads the mission <paramref name="name"/>'s data from <paramref name="database"/> onto the map.</summary>
    /// <returns>Whether it loaded.</returns>
    /// <remarks>Virtual table slot 3 (pure in the base; the slots 1-6 are).</remarks>
    virtual bool loadMission(DaoDatabase& database, const CString& name) = 0;
    /// <summary>The most turrets the mission may have.</summary>
    /// <remarks>Virtual table slot 4.</remarks>
    virtual int32_t maxTurrets() = 0;
    /// <summary>The most drop zones the mission may have.</summary>
    /// <remarks>Virtual table slot 5.</remarks>
    virtual int32_t maxDropZones() = 0;
    /// <summary>The most players the mission may have.</summary>
    /// <remarks>Virtual table slot 6. Nothing in the editor calls it.</remarks>
    virtual int32_t maxPlayers() = 0;
    /// <summary>
    /// Draws the mission's marks (its drop zones and the like) on the tactical map picture <paramref name="image"/>
    /// (<paramref name="width"/> x <paramref name="height"/>, 3 bytes a pixel).
    /// </summary>
    /// <remarks>Virtual table slot 7; the base draws none (0x00450de0).</remarks>
    virtual void drawMissionMarks([[maybe_unused]] uint8_t* image, [[maybe_unused]] int32_t width,
                                  [[maybe_unused]] int32_t height)
    {
    }

    /// <summary>The tile at (<paramref name="col"/>, <paramref name="row"/>), or null outside the map.</summary>
    EditorTile* getTile(int32_t col, int32_t row);
    /// <summary>
    /// Which side of the play area's edges tile (<paramref name="col"/>, <paramref name="row"/>) lies on: inside when
    /// exactly one of two edge tests holds and the tile isn't a corner tile, on the edge when it lies on either line.
    /// </summary>
    /// <returns><c>REGION_INSIDE</c>, <c>REGION_OUTSIDE</c> or <c>REGION_EDGE</c>.</returns>
    /// <remarks>
    /// Inlined in the original (drawTerrain 0x00451560, generateMap 0x00449290). Original behaviour: a tile off the
    /// map that passes the edge tests is read through a null pointer.
    /// </remarks>
    int32_t regionOf(int32_t col, int32_t row);
    /// <summary>
    /// The map position of vertex (<paramref name="col"/>, <paramref name="row"/>): its column and row times
    /// <c>tileWorldWidth</c> and <c>tileWorldHeight</c>, its elevation times <c>elevationScale</c>; the last row and
    /// column are the corners of the tiles before them.
    /// </summary>
    /// <returns>false outside the map.</returns>
    bool getVertex(int32_t col, int32_t row, int32_t* x, int32_t* y, int32_t* z);
    /// <summary>The view position of map position (<paramref name="x"/>, <paramref name="y"/>, <paramref name="z"/>).</summary>
    void mapToView(int32_t x, int32_t y, int32_t z, int32_t* viewX, int32_t* viewY);
    /// <summary>
    /// The vertex whose view position is nearest (<paramref name="viewX"/>, <paramref name="viewY"/>), and the view
    /// offset from it.
    /// </summary>
    /// <returns>Whether any vertex was found.</returns>
    bool findNearestVertex(int32_t viewX, int32_t viewY, int32_t* col, int32_t* row, int32_t* offsetX,
                           int32_t* offsetY);
    /// <summary>
    /// The map position under view position (<paramref name="viewX"/>, <paramref name="viewY"/>): where the view ray
    /// meets the plane of the nearest vertex and two of its neighbours.
    /// </summary>
    void viewToMap(int32_t viewX, int32_t viewY, int32_t* x, int32_t* y, int32_t* z);
    /// <summary>The view position of vertex (<paramref name="col"/>, <paramref name="row"/>).</summary>
    /// <returns>false, setting nothing, outside the map.</returns>
    /// <remarks>0x00452af0.</remarks>
    bool getVertexView(int32_t col, int32_t row, int32_t* viewX, int32_t* viewY);
    /// <summary>
    /// The terrain's height at map position (<paramref name="x"/>, <paramref name="y"/>): on the plane of the tile
    /// triangle it lies in (the tile is split from its first corner to its last).
    /// </summary>
    /// <remarks>0x004534d0.</remarks>
    int32_t terrainHeight(int32_t x, int32_t y);

    /// <summary>
    /// Draws the terrain into <paramref name="buffer"/> (<paramref name="width"/> x <paramref name="height"/>, the
    /// view from <paramref name="origin"/>) at zoom level <paramref name="zoom"/>: every tile (copied inside the play
    /// area, amber outside it, brighter when highlighted, outlined when <paramref name="gridlines"/>) and its
    /// overlay, the selected tiles again on top, then the drop zones and objects on drawn tiles.
    /// </summary>
    /// <returns>true.</returns>
    /// <remarks>0x00451560. Marks the drawn tiles in <see cref="visibleTiles"/>.</remarks>
    bool drawTerrain(const POINT& origin, uint8_t* buffer, int32_t width, int32_t height, bool gridlines, int32_t zoom);
    /// <summary>
    /// Draws the outline of every cell the objects' footprints cover, in cyan, on <paramref name="dc"/> (the view from
    /// <paramref name="origin"/>).
    /// </summary>
    /// <param name="cells">Also the drawn tiles' cells the scenario map blocks (red) or blocks the sight through (cyan),
    /// then the global map's doors (yellow).</param>
    /// <returns>true.</returns>
    /// <remarks>0x00454760.</remarks>
    bool drawFootprints(const POINT& origin, HDC dc, int32_t width, int32_t height, bool cells, int32_t zoom);
    /// <summary>
    /// Draws the tactical map into <paramref name="buffer"/> (<paramref name="width"/> x <paramref name="height"/>, the
    /// map turned 45 degrees, a tile a 3 x 3 block): objects the tactical map shows in orange, overlays in their
    /// kind's colour, else the terrain shaded by its height; each colour the nearest of <paramref name="colors"/>
    /// (a BITMAPINFO's colour table).
    /// </summary>
    /// <returns>true.</returns>
    /// <remarks>0x004572e0, in x87 extended precision as the original computes it.</remarks>
    bool drawTacticalMap(uint8_t* buffer, int32_t width, int32_t height, const uint8_t* colors);
    /// <summary>
    /// The tactical map colour (blue, green, red into <paramref name="bgr"/>) of cell (<paramref name="subX"/>,
    /// <paramref name="subY"/>) of tile (<paramref name="col"/>, <paramref name="row"/>): an object shown on the
    /// tactical map, the overlay's corner, or the terrain shaded by its height of <paramref name="highest"/>.
    /// </summary>
    /// <remarks>Inlined in 0x004572e0 and 0x00457e10. Without a tile or object the colour is left as it is.</remarks>
    void tacticalCellColor(int32_t col, int32_t row, int32_t subX, int32_t subY, int32_t highest, uint8_t* bgr);
    /// <summary>
    /// The position of cell (<paramref name="col"/>, <paramref name="row"/>) in the turned tactical map picture
    /// <see cref="writeTacticalImages"/> makes.
    /// </summary>
    /// <remarks>0x0045d140, in x87 extended precision as the original computes it.</remarks>
    void cellToTactical(int32_t col, int32_t row, int32_t* x, int32_t* y) const;
    /// <summary>
    /// Writes the terrain (.elv): the signature, a table of the 20 x 20-vertex blocks' offsets, then each block's
    /// vertices (elevation + 1, terrain type, the tile and its overlay).
    /// </summary>
    /// <returns>true.</returns>
    /// <remarks>0x004564d0.</remarks>
    bool writeElevations(CArchive& archive);
    /// <summary>
    /// Writes the objects (.obj) block by block (the game's tile size, the offsets in a table at the start, patched
    /// at the end through <paramref name="file"/>): each object's FIT id, its offset in the view from its tile's
    /// vertex, its tile and block, and its look. <see cref="objects"/> is sorted by block for it and back after.
    /// </summary>
    /// <returns>true.</returns>
    /// <remarks>0x00456770.</remarks>
    bool writeObjects(CArchive& archive, CFile& file);
    /// <summary>
    /// Writes the buildings the overlays make (.bdg): a count (patched at the end through <paramref name="file"/>),
    /// then for each overlay that is damaged, or has a damaged look in the library, its block, its vertex in the
    /// block, the building type of its kind and whether it is damaged.
    /// </summary>
    /// <returns>true.</returns>
    /// <remarks>0x00456f60.</remarks>
    bool writeBuildings(CArchive& archive, CFile& file);
    /// <summary>
    /// Writes the tiles for the game to load first (.pre): the ids of the tiles in use among the first 3711, as long
    /// as their art comes to less than 2 MB.
    /// </summary>
    /// <returns>true.</returns>
    /// <remarks>0x00457200.</remarks>
    bool writePreload(CFile& file);
    /// <summary>
    /// Replaces mission <paramref name="missionId"/>'s rows: deletes its structures, lookout points, objectives, their
    /// scripts and units, writes the units (<see cref="writeUnits"/>) and structures (<see cref="writeStructures"/>),
    /// then each objective with a value set: its Objectives row and its script (<see cref="Objective::writeScripting"/>).
    /// </summary>
    /// <returns>Whether everything was written; false after a database error.</returns>
    /// <remarks>0x00458d50 (MissionSave.cpp).</remarks>
    bool saveUnits(DaoDatabase& database, long missionId);
    /// <summary>
    /// Puts each side's units in force groups (by the objectives that refer to them), then writes a MissionAssignments
    /// row per unit, the other side first, and its orders to the mission's warrior FIT
    /// (Data\Missions\Warriors\&lt;ScriptFileName&gt;.fit).
    /// </summary>
    /// <returns>true.</returns>
    /// <remarks>0x004598e0.</remarks>
    bool writeUnits(DaoDatabase& database, long missionId);
    /// <summary>Replaces the mission's DropZones rows with the drop zones, the other side's first.</summary>
    /// <returns>true.</returns>
    /// <remarks>0x0045ab80.</remarks>
    bool writeDropZones(DaoDatabase& database, long missionId);
    /// <summary>
    /// Writes the Structure rows of the controllers with links and their StructureLinks rows (a power station's also
    /// for the turrets of its turret controls), then of the other structures with a role (classes 2-8 and the turrets
    /// no control has).
    /// </summary>
    /// <returns>false after a database error, which a message box reports.</returns>
    /// <remarks>0x0045b450.</remarks>
    bool writeStructures(DaoDatabase& database, long missionId);
    /// <summary>
    /// Adds or updates <paramref name="object"/>'s Structure row in <paramref name="structures"/>: its block, vertex
    /// and number, building type, identifier, side and whether it can be captured.
    /// </summary>
    /// <returns>true.</returns>
    /// <remarks>Inline in 0x0045b450.</remarks>
    bool writeStructureRow(StructureRecord& structures, long missionId, const EditorObject& object,
                           int32_t objectClass);
    /// <summary>Adds a StructureLinks row linking <paramref name="structure"/> to the controller's row.</summary>
    /// <remarks>Inline in 0x0045b450.</remarks>
    void addStructureLink(DaoDatabase& database, const EditorObject& structure, long controllerId);
    /// <summary>
    /// Draws the tactical map picture of the map (a pixel a cell, turned 45 degrees and smoothed) and writes it as
    /// Targa files: <paramref name="tgaName"/> in <paramref name="palette"/>'s colours, then, with the mission's marks
    /// drawn on (<see cref="drawMissionMarks"/>), <paramref name="logisticName"/> in the logistics palette and
    /// <paramref name="trueColorName"/> in 24 bits. A null name isn't written.
    /// </summary>
    /// <returns>false when the picture can't be allocated.</returns>
    /// <remarks>0x00457e10.</remarks>
    bool writeTacticalImages(const uint8_t* palette, const char* tgaName, const char* trueColorName,
                             const char* logisticName);
    /// <summary>
    /// Draws the terrain (no gridlines) from <paramref name="origin"/> into <paramref name="buffer"/> and writes it as
    /// an 8-bit Targa with the editor's palette to <paramref name="fileName"/>.
    /// </summary>
    /// <returns>false when the file doesn't open.</returns>
    /// <remarks>0x0045d250.</remarks>
    bool saveImage(const CString& fileName, const POINT& origin, uint8_t* buffer, int32_t width, int32_t height,
                   int32_t zoom);
    /// <summary>
    /// Draws cell (<paramref name="cell"/>.x column, .y row)'s outline, 2 map units in from its edges, on
    /// <paramref name="dc"/> with its pen; nothing when a corner's terrain height is outside 0-128.
    /// </summary>
    /// <remarks>0x00455050.</remarks>
    void drawCellOutline(HDC dc, const POINT& cell, const POINT& origin, int32_t zoom);
    /// <summary>
    /// Draws the marks over the map on <paramref name="dc"/> (the view from <paramref name="origin"/>): the tools'
    /// <see cref="marks"/>, the <see cref="selectedVertices"/>, a green (side 2) or red box around each object and
    /// mover on a side and a yellow one around each selected one, the objectives' targets, and with
    /// <paramref name="links"/> a dashed line from each control structure to each structure it controls.
    /// </summary>
    /// <returns>Whether every mark and vertex drew.</returns>
    /// <remarks>0x00452320.</remarks>
    bool drawMarks(const POINT& origin, HDC dc, int32_t width, int32_t height, int32_t zoom, bool links);
    /// <summary>
    /// Marks each of the <see cref="selectedVertices"/> in view with a yellow square, and with
    /// <see cref="showVertexHeights"/> writes its elevation beside it.
    /// </summary>
    /// <returns>false when a vertex lies outside the map.</returns>
    /// <remarks>0x00452750; the original also takes the zoom level and doesn't use it.</remarks>
    bool drawVertexMarks(const POINT& origin, HDC dc, int32_t width, int32_t height);
    /// <summary>
    /// Draws the corners of <paramref name="object"/>'s box in <paramref name="color"/>: its frame's bounds,
    /// <paramref name="margin"/> out, with arms <paramref name="size"/> long (all scaled to zoom level
    /// <paramref name="zoom"/>).
    /// </summary>
    /// <returns>true.</returns>
    /// <remarks>0x00455380.</remarks>
    bool drawObjectBox(const POINT& origin, HDC dc, const EditorObject& object, int32_t zoom, COLORREF color,
                       int32_t size, int32_t margin);
    /// <summary>
    /// Marks <paramref name="object"/> as selected: a yellow box, and a tick in the middle of its left and right
    /// sides.
    /// </summary>
    /// <returns>true.</returns>
    /// <remarks>0x00455670.</remarks>
    bool drawSelectionBox(const POINT& origin, HDC dc, const EditorObject& object, int32_t zoom);
    /// <summary>
    /// Adds <paramref name="marker"/> at view position (<paramref name="x"/>, <paramref name="y"/>) to
    /// <see cref="marks"/>, unless it is there already.
    /// </summary>
    /// <returns>Whether it was added.</returns>
    /// <remarks>0x00452180.</remarks>
    bool addMark(MapMarker* marker, int32_t x, int32_t y);
    /// <summary>
    /// Removes the marks at view position (<paramref name="x"/>, <paramref name="y"/>), or every mark of
    /// <paramref name="marker"/> when either is <see cref="MARK_ANYWHERE"/>.
    /// </summary>
    /// <returns>Whether any was removed.</returns>
    /// <remarks>0x00452220. The markers aren't deleted.</remarks>
    bool removeMark(MapMarker* marker, int32_t x, int32_t y);
    /// <summary>Deletes every mark's marker and empties <see cref="marks"/>.</summary>
    /// <returns>true.</returns>
    /// <remarks>0x0045d1e0.</remarks>
    bool clearMarks();

    /// <summary>
    /// Places <paramref name="object"/>: marks its cell and footprint in the cell grid and adds it to its list (drop
    /// zones apart, the rest sorted by row and column).
    /// </summary>
    /// <returns>false, placing nothing, when its cell is taken or a mech or vehicle stands one cell down-right.</returns>
    bool addObject(EditorObject* object);
    /// <summary>
    /// Places <paramref name="mover"/> as <see cref="addObject"/> places an object, in <see cref="movers"/> (sorted by
    /// row and column).
    /// </summary>
    /// <returns>false, placing nothing, when its cell is taken or a mech or vehicle stands one cell down-right.</returns>
    /// <remarks>0x0045c7a0.</remarks>
    bool addMover(MoverObject* mover);

    /// <summary>
    /// The object on cell (<paramref name="col"/>, <paramref name="row"/>), or a mech or vehicle on the cell one down
    /// and right of it.
    /// </summary>
    /// <returns>Null when there is none or the cell is off the grid.</returns>
    /// <remarks>0x00404a40.</remarks>
    EditorObject* objectAt(int32_t col, int32_t row);
    /// <summary>The cell grid's entry for (<paramref name="col"/>, <paramref name="row"/>), null off the grid.</summary>
    EditorObject* objectAtCell(int32_t col, int32_t row);
    /// <summary>The mech or vehicle <see cref="objectAt"/> finds, null for another object.</summary>
    /// <remarks>0x0045cfe0.</remarks>
    EditorObject* moverAt(int32_t col, int32_t row);
    /// <summary>
    /// The map block and the vertex (tile) in it of <paramref name="object"/>'s cell, the way the database numbers
    /// structures.
    /// </summary>
    /// <remarks>0x0045c080.</remarks>
    void blockVertex(const EditorObject& object, int32_t* block, int32_t* vertex) const;
    /// <summary>
    /// <see cref="blockVertex"/> of <paramref name="object"/>, and its place among the placed objects on that vertex.
    /// </summary>
    /// <remarks>0x0045c4d0.</remarks>
    void structureIndex(const EditorObject& object, int32_t* block, int32_t* vertex, int32_t* number) const;
    /// <summary>The <paramref name="number"/>th placed object on vertex <paramref name="vertex"/> of block <paramref name="block"/>.</summary>
    /// <returns>Null when there is none.</returns>
    /// <remarks>0x0045c660.</remarks>
    EditorObject* structureAt(long block, long vertex, long number) const;
    /// <summary>The map position (y up) of cell (<paramref name="col"/>, <paramref name="row"/>)'s centre.</summary>
    /// <remarks>0x0045b140, in x87 extended precision as the original computes it.</remarks>
    void cellToWorld(int32_t col, int32_t row, int32_t* x, int32_t* y) const;
    /// <summary>The cell under map position (<paramref name="x"/>, <paramref name="y"/>) (y up).</summary>
    /// <remarks>0x0045b1d0.</remarks>
    void worldToCell(int32_t x, int32_t y, int32_t* col, int32_t* row) const;

    /// <summary>
    /// The tile under view position (<paramref name="viewX"/>, <paramref name="viewY"/>): of the nearest vertex's four
    /// tiles, the one the view offset from the vertex points into.
    /// </summary>
    /// <returns>Whether the tile is on the map.</returns>
    /// <remarks>0x004533d0.</remarks>
    bool viewToTile(int32_t viewX, int32_t viewY, int32_t* col, int32_t* row);
    /// <summary>
    /// The cell under view position (<paramref name="viewX"/>, <paramref name="viewY"/>): the map position there
    /// (<see cref="viewToMap"/>) divided by a third of <c>tileWorldWidth</c>, truncated.
    /// </summary>
    /// <returns>Whether the cell is on the cell grid.</returns>
    /// <remarks>0x004034a0, a free function of the tools; the place and move tools compute it inline.</remarks>
    bool viewToCell(int32_t viewX, int32_t viewY, int32_t* col, int32_t* row);
    /// <summary>
    /// Whether <paramref name="object"/> may be added: its cell at least 4 cells inside the grid and free, a drop zone
    /// within the mission's count (and with 6 allowed, at most 3 per side), a turret within the mission's count.
    /// </summary>
    /// <remarks>0x00453690. The rows are bounded by the column count, as the maps are square.</remarks>
    bool isPlaceable(const EditorObject& object);
    /// <summary>
    /// The elevation of vertex (<paramref name="col"/>, <paramref name="row"/>): the last corner of the tile up-left of
    /// it, or on the top and left edges the corner of the tile there.
    /// </summary>
    /// <returns>The elevation; 0x7fffffff off the map.</returns>
    /// <remarks>0x00453160.</remarks>
    int32_t vertexElevation(int32_t col, int32_t row);
    /// <summary>
    /// Adds vertex (<paramref name="col"/>, <paramref name="row"/>) to <see cref="selectedVertices"/>, or when it is
    /// there already removes it unless <paramref name="keep"/>.
    /// </summary>
    /// <remarks>0x00453040.</remarks>
    void setVertexSelected(int32_t col, int32_t row, bool keep);
    /// <summary>Unselects everything: the vertices, the objects, movers and drop zones, and the tiles.</summary>
    /// <remarks>0x0045d3d0.</remarks>
    void deselectAll();
    /// <summary>Takes every tile's hover highlight off.</summary>
    /// <remarks>0x0045d490.</remarks>
    void clearHighlight();
    /// <summary>
    /// The object (not a mover) whose cell or footprint covers cell (<paramref name="col"/>, <paramref name="row"/>):
    /// the first such of <see cref="objects"/>, then of <see cref="dropZones"/>.
    /// </summary>
    /// <returns>Null when the cell is off the grid or empty.</returns>
    /// <remarks>0x00404990.</remarks>
    EditorObject* objectCoveringCell(int32_t col, int32_t row);
    /// <summary>
    /// Removes the object like <paramref name="object"/> (same cell and id) from the map and deletes it. With
    /// <paramref name="checkReferences"/>, objectives that refer to it are deleted and units whose orders do get new
    /// orders, each after the user agrees (otherwise nothing is removed), and its control links are undone.
    /// </summary>
    /// <returns>Whether it was found and removed.</returns>
    /// <remarks>0x00453a10.</remarks>
    bool removeObject(const EditorObject& object, bool checkReferences);
    /// <summary>Removes the mover like <paramref name="mover"/> as <see cref="removeObject"/> removes an object.</summary>
    /// <remarks>0x0045ca20.</remarks>
    bool removeMover(const EditorObject& mover, bool checkReferences);
    /// <summary><see cref="removeMover"/> for a mech or vehicle, else <see cref="removeObject"/>.</summary>
    /// <remarks>0x0045c760.</remarks>
    bool removeAnyObject(const EditorObject& object, bool checkReferences);
    /// <summary>The links of <paramref name="controller"/> (<see cref="ControlLink::isFor"/>), or null.</summary>
    /// <remarks>0x0045b2b0.</remarks>
    ControlLink* controlLinkFor(const EditorObject& controller);
    /// <summary>The links <paramref name="object"/> is the controller of or linked in, or null.</summary>
    /// <remarks>0x0045b2f0.</remarks>
    ControlLink* controlLinkWith(const EditorObject& object);
    /// <summary>The first objective that refers to <paramref name="object"/>, or null.</summary>
    /// <remarks>0x0045d080.</remarks>
    Objective* objectiveFor(const EditorObject& object);
    /// <summary>Adds <paramref name="objective"/> to the end of <see cref="objectives"/>, which takes it, unless it is there.</summary>
    /// <returns>Whether it was added.</returns>
    /// <remarks>0x00458c60.</remarks>
    bool addObjective(Objective* objective);
    /// <summary>
    /// Takes <paramref name="objective"/> out of <see cref="objectives"/>, has it let go of what it put on the map and
    /// deletes it.
    /// </summary>
    /// <returns>false when it isn't one of the map's.</returns>
    /// <remarks>0x00458ce0.</remarks>
    bool removeObjective(Objective* objective);
    /// <summary>Adds <paramref name="link"/> to <see cref="controlLinks"/>, which takes it, unless an equal one is there.</summary>
    /// <returns>Whether it was added; the caller keeps it otherwise.</returns>
    /// <remarks>0x0045b340.</remarks>
    bool addControlLink(ControlLink* link);
    /// <summary>Removes and deletes the first of <see cref="controlLinks"/> equal to <paramref name="link"/>.</summary>
    /// <returns>Whether one was removed.</returns>
    /// <remarks>0x0045b3d0.</remarks>
    bool removeControlLink(const ControlLink& link);
    /// <summary><see cref="addMover"/> for a mech or vehicle, else <see cref="addObject"/>.</summary>
    /// <remarks>0x0045c730.</remarks>
    bool placeAnyObject(EditorObject* object);
    /// <summary>
    /// Moves <paramref name="object"/> to cell (<paramref name="col"/>, <paramref name="row"/>): the objectives' and
    /// orders' references to it follow, a control structure's links follow it, and its cells in the grid move.
    /// </summary>
    /// <returns>true.</returns>
    /// <remarks>0x00454330. The lists aren't sorted again.</remarks>
    bool moveObject(EditorObject& object, int32_t col, int32_t row);

    /// <summary>
    /// Reads the mission <paramref name="missionId"/>'s units (<see cref="loadAssignments"/>), objectives and structure
    /// links from <paramref name="database"/>, replacing the objectives.
    /// </summary>
    /// <returns>Whether all of it read.</returns>
    /// <remarks>0x00459680.</remarks>
    bool loadMissionData(DaoDatabase& database, long missionId);
    /// <summary>
    /// Places the mission's units from MissionAssignments (a mech or a vehicle each, with its pilot), then gives each
    /// its orders from the mission's warrior FIT.
    /// </summary>
    /// <returns>true.</returns>
    /// <remarks>0x0045a330.</remarks>
    bool loadAssignments(DaoDatabase& database, long missionId);
    /// <summary>
    /// Gives each of the mission's structures (MissionStructures) its side, and links the control structures to the
    /// structures they control (StructureAssociates).
    /// </summary>
    /// <returns>Whether every linked structure was found.</returns>
    /// <remarks>0x0045c110.</remarks>
    bool loadStructureLinks(DaoDatabase& database, long missionId);
    /// <summary>Places the mission's drop zones (PlayerEntryPoints): the first three of side 2, the rest of side 1.</summary>
    /// <returns>true.</returns>
    /// <remarks>0x0045ae60.</remarks>
    bool loadDropZones(DaoDatabase& database, long missionId);

    /// <summary>
    /// Reads the tiles from a .elv file positioned at its first block: each block's 20 x 20 tiles give their corner
    /// elevations, terrain tile (whose textures and corner heights the tile library knows) and overlay.
    /// </summary>
    /// <returns>true.</returns>
    bool readElevations(CArchive& archive);
    /// <summary>
    /// Reads <paramref name="count"/> objects from a .obj file positioned at its first packet, places each at the cell
    /// under its view position and adds it (<see cref="addObject"/>); objects of no known type are skipped.
    /// </summary>
    /// <param name="blocksPerRow">Terrain blocks per side of the map.</param>
    /// <returns>false when an object was skipped; otherwise what the original leaves in its result (true here).</returns>
    bool readObjects(int32_t count, int32_t blocksPerRow, CArchive& archive);

    /// <summary>
    /// Tiles across. The map's vertices, one per tile of the .elv file, are one more: the game's map is
    /// <c>maxRow + 1</c> tiles wide.
    /// </summary>
    int32_t maxRow;
    /// <summary>Tiles down (the game's map is <c>maxCol + 1</c> tiles high).</summary>
    int32_t maxCol;
    /// <summary>
    /// The multiplayer mission's description (Mission &gt; Description), kept in its save's third packet; constructed
    /// empty.
    /// </summary>
    CString description;
    /// <summary>The tiles, <see cref="maxRow"/> per row.</summary>
    std::vector<EditorTile> tiles;
    /// <summary>
    /// The tiles <see cref="drawTerrain"/> drew last: tile <c>i = row * MAP_VISIBLE_ROW + column</c> is bit
    /// <c>i &amp; 7</c> of word <c>i &gt;&gt; 3</c> (+0x14).
    /// </summary>
    std::vector<uint32_t> visibleTiles = std::vector<uint32_t>(MAP_VISIBLE_WORDS);
    /// <summary>
    /// The object on each cell (<see cref="maxRow"/> * 3 cells per row): every cell of an object's footprint and its
    /// own cell point to it.
    /// </summary>
    std::vector<EditorObject*> cellGrid;
    /// <summary>The tools' marks, in the order added (+0x7098).</summary>
    std::list<MapMark> marks;
    /// <summary>The selected vertices (column, row), in the order selected (+0x70a4).</summary>
    std::list<POINT> selectedVertices;
    /// <summary>The placed objects but drop zones, sorted by cell row, then column (+0x70b0).</summary>
    std::list<EditorObject*> objects;
    /// <summary>The drop zones, in the order placed (+0x70c8).</summary>
    std::list<EditorObject*> dropZones;
    /// <summary>The objectives (+0x70bc); a new map has one, "Destroy All Enemies".</summary>
    std::list<Objective*> objectives;
    /// <summary>The control structures' links (+0x70d4).</summary>
    std::list<ControlLink*> controlLinks;
    /// <summary>The placed mechs and vehicles, sorted by cell row, then column (+0x70e0).</summary>
    std::list<EditorObject*> movers;
    /// <summary>The mission's dispatch text (+0x70ec).</summary>
    MissionText missionText;
    /// <summary>The number of turrets placed.</summary>
    int32_t turretCount = 0;
    /// <summary>The global (pathfinding) map (+0x710c): empty until a save builds it.</summary>
    GlobalMap* globalMap = nullptr;
    /// <summary>The scenario (movement) map (+0x7108): empty until a save builds it.</summary>
    ScenarioMap* scenarioMap = nullptr;
    /// <summary>
    /// Tiles are selected (+0x7110): the select tools set it and clearing the selection clears it. The palette, vertex
    /// and damage tools then work on the selection at once.
    /// </summary>
    bool tilesSelected = false;
    /// <summary>
    /// Port-only: the time of day the mission is set at (Mission > Time of Day), saved in its NewMissions row; not in
    /// the original's layout.
    /// </summary>
    MissionTimeOfDay timeOfDay = MissionTimeOfDay::Day;
    /// <summary>
    /// Port-only: the weather the mission is set in (Mission > Weather), its MISSION_WEATHERS value, saved in its
    /// NewMissions row; not in the original's layout.
    /// </summary>
    long weather = MISSION_WEATHER_CLEAR;

    /// <summary>
    /// The global map's area of the tile of cell (<paramref name="col"/>, <paramref name="row"/>): 0 without a global
    /// map, -1 for none.
    /// </summary>
    /// <remarks>0x0045d0c0.</remarks>
    int32_t areaAt(int32_t col, int32_t row) const;

protected:
    /// <summary>
    /// The mission size checks both maps' <see cref="checkMission"/> end with: the bytes the game needs for the
    /// objects, units, drop zones and buildings, then the game play objects; each refusal said in a message.
    /// </summary>
    /// <returns>Whether the mission is within both.</returns>
    /// <remarks>Inline in 0x00470650 and 0x00468de0.</remarks>
    bool checkMissionSize();
    /// <summary>The text of <paramref name="path"/> with a terminator after it, or nothing when it doesn't open.</summary>
    /// <remarks>
    /// Inline in the saves (0x00470a90, 0x00471990, 0x0046a0b0). EB-118 fixed: the original reads the file into a
    /// buffer of exactly its length and gives it to fprintf as the format, reading on past its end; the port ends it.
    /// </remarks>
    static std::optional<std::vector<char>> readTemplate(const CString& path);

private:
    /// <summary>
    /// Whether <paramref name="object"/>'s cell is free, with no mech or vehicle one cell down-right (always for a cell
    /// off the grid).
    /// </summary>
    bool canPlace(const EditorObject& object);
    /// <summary>Whether cell (<paramref name="col"/>, <paramref name="row"/>) lies on the cell grid.</summary>
    bool isCellInGrid(int32_t col, int32_t row) const;
    /// <summary>Marks <paramref name="object"/>'s cell and the cells of its footprint in the cell grid.</summary>
    void markCells(EditorObject* object);
    /// <summary>
    /// Sets cell (<paramref name="col"/>, <paramref name="row"/>) and the cells of <paramref name="id"/>'s footprint
    /// around it to <paramref name="value"/> (null to clear them).
    /// </summary>
    void setFootprintCells(uint32_t id, int32_t col, int32_t row, EditorObject* value);
    /// <summary>The view, for the questions the removals ask.</summary>
    static CView* activeView();
    /// <summary>Inserts <paramref name="object"/> into <paramref name="list"/> before the first object after it by row, then column.</summary>
    static void insertSorted(std::list<EditorObject*>& list, EditorObject* object);
};

/// <summary>A single-player mission's settings, as the Settings dialog edits them.</summary>
/// <remarks>0x20 bytes, laid out as the map's settings from +0x7114.</remarks>
struct MissionSettings
{
    /// <summary>The ArtillerySmall setting.</summary>
    long artillerySmall = 0;
    /// <summary>The ArtilleryLarge setting.</summary>
    long artilleryLarge = 0;
    /// <summary>The ArtillerySensor setting.</summary>
    long artillerySensor = 0;
    /// <summary>The ArtilleryCamera setting.</summary>
    long artilleryCamera = 0;
    /// <summary>The TimeLimit setting, in seconds.</summary>
    long timeLimit = 0;
    /// <summary>The PlayerForceLimit setting.</summary>
    long playerForceLimit = 0x3de;
    /// <summary>The player's resource points.</summary>
    long resourcePoints = 100000;
    /// <summary>Whether the player's inventory has no Uller.</summary>
    bool noUller = true;
};

/// <summary>The single-player map.</summary>
/// <remarks>0x7134 bytes, the virtual table at 0x004fe8d0; the multiplayer one is 0x716c bytes.</remarks>
class SingleMap : public EditorMap
{
public:
    /// <summary>An <see cref="EditorMap"/> with the single-player settings at their defaults.</summary>
    SingleMap(int32_t maxRow, int32_t maxCol);

    /// <summary>The mission's settings.</summary>
    /// <remarks>0x00471970.</remarks>
    MissionSettings settings() const;
    /// <summary>Replaces the mission's settings with <paramref name="settings"/>.</summary>
    /// <remarks>0x00471950.</remarks>
    void setSettings(const MissionSettings& settings);

    /// <summary>
    /// Reads mission <paramref name="name"/> onto the map: its NewMissions settings, the dispatch text, the resource
    /// points and whether the player has an Uller from its save (Data\Savegame\<paramref name="name"/>.sol), then its
    /// units, objectives, structure links and drop zones.
    /// </summary>
    /// <returns>Whether the mission is in the database.</returns>
    /// <remarks>0x00471160.</remarks>
    bool loadMission(DaoDatabase& database, const CString& name) override;
    /// <returns>200.</returns>
    /// <remarks>0x004480c0.</remarks>
    int32_t maxTurrets() override;
    /// <returns>3.</returns>
    /// <remarks>0x004480a0.</remarks>
    int32_t maxDropZones() override;
    /// <returns>1.</returns>
    /// <remarks>0x004480b0.</remarks>
    int32_t maxPlayers() override;
    /// <summary>
    /// Writes mission <paramref name="name"/>: its dispatch text (Data\Missions\name.txt), its NewMissions row (added
    /// or updated), its units', structures' and objectives' rows, its campaign file from Teditor\campaign.txt and its
    /// save.
    /// </summary>
    /// <returns>What writing the save returned; false when campaign.txt doesn't open.</returns>
    /// <remarks>0x00470a90.</remarks>
    bool saveMission(DaoDatabase& database, CString& name) override;
    /// <summary>
    /// Checks the mission has an objective, a drop zone and a unit, isn't too big (objects, units, drop zones and
    /// overlays) and hasn't too many game play objects.
    /// </summary>
    /// <remarks>0x00470650.</remarks>
    bool checkMission(DaoDatabase& database, const CString& name) override;
    /// <summary>
    /// Writes the save game Data\Savegame\&lt;<paramref name="saveName"/> or <paramref name="name"/>&gt;.sol: Teditor\sol.txt
    /// filled with the save name, whether the map is on Garotte and the resource points, packed with the player's
    /// inventory (Teditor\issolo.pur without an Uller, else clansolo.pur).
    /// </summary>
    /// <returns>false when sol.txt or the working file doesn't open.</returns>
    /// <remarks>0x00471990.</remarks>
    bool writeSave(const CString& name, const char* saveName);
    /// <remarks>0x00471d40.</remarks>
    void drawMissionMarks(uint8_t* image, int32_t width, int32_t height) override;

    /// <summary>The ArtillerySmall setting (+0x7114).</summary>
    long artillerySmall = 0;
    /// <summary>The ArtilleryLarge setting.</summary>
    long artilleryLarge = 0;
    /// <summary>The ArtillerySensor setting.</summary>
    long artillerySensor = 0;
    /// <summary>The ArtilleryCamera setting.</summary>
    long artilleryCamera = 0;
    /// <summary>The TimeLimit setting.</summary>
    long timeLimit = 0;
    /// <summary>The PlayerForceLimit setting; constructed 990.</summary>
    long playerForceLimit = 0x3de;
    /// <summary>The player's resource points (the save's numPoints); constructed 100000.</summary>
    long resourcePoints = 100000;
    /// <summary>Whether the save's inventory has no Uller; constructed true.</summary>
    bool noUller = true;
};

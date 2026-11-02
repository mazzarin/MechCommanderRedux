#pragma once

#include "EditorTool.h"

class BrushTool;
class EditorMap;
class EditorObject;
class MapFeaturesBar;
class MoverObject;
class ObjectivesTeamDialog;
class TacticalMapDialog;

/// <summary>
/// The zoom the views draw at (0x00527524): above 0.99 the full-size tiles, above 0.49 the middle ones, else the
/// smallest.
/// </summary>
extern float viewZoom;
/// <summary>The tactical map window the views keep up to date (0x006c237c), made by the first view.</summary>
extern TacticalMapDialog* tacticalMap;

/// <summary>A view of the mission map.</summary>
/// <remarks>0x8d0 bytes in the original; only the members the translated code uses are here yet.</remarks>
class DreditorView : public CView
{
protected:
    /// <summary>
    /// Creates the object from serialization or a document template: gridlines on, the gamma setting's index, and
    /// the tactical map object if there is none yet.
    /// </summary>
    DreditorView();
    DECLARE_DYNCREATE(DreditorView)

public:
    /// <summary>Frees the bitmap, the memory DC and (any view's end) the tactical map object.</summary>
    /// <remarks>The tool is deleted by <see cref="OnDestroy"/>.</remarks>
    ~DreditorView() override;

    /// <summary>
    /// Draws the map: scrolls first when the cursor is near an edge during a drag, then draws the terrain and objects
    /// into the view's bitmap, the marks over them (with the link lines while a link tool is on) and the footprints
    /// when the gridlines are on, and copies it to the window.
    /// </summary>
    void OnDraw(CDC* pDC) override;
    /// <summary>
    /// Adds the scroll bars, sets their ranges to the map's extent in the view, scrolls to the map's centre and makes
    /// the palette.
    /// </summary>
    void OnInitialUpdate() override;
    /// <summary>Invalidates the view, then lets the base handle the scroll.</summary>
    BOOL OnScroll(UINT nScrollCode, UINT nPos, BOOL bDoScroll = TRUE) override;

    /// <summary>
    /// Scrolls to <paramref name="position"/> (scroll bar units: view pixels from the map's top-left extent less two
    /// tiles), clamped to the map: moves <see cref="origin"/>, the tactical map's view rectangle and the scroll bars.
    /// </summary>
    /// <remarks>0x0041b1a0.</remarks>
    void scrollTo(const POINT& position);
    /// <summary>
    /// Takes the window's new size (rounded up to a multiple of 4) for the bitmap, which is made again on the next
    /// draw, and scrolls to the current position again.
    /// </summary>
    /// <remarks>0x0041c300; <paramref name="zoom"/> isn't used.</remarks>
    void resize(const CSize& size, float zoom);

    /// <summary>The frame's status bar (+0x90), set by the frame.</summary>
    CStatusBar* statusBar = nullptr;
    /// <summary>The frame's Map Features palette (+0x9c), set by the frame.</summary>
    MapFeaturesBar* mapFeaturesBar = nullptr;

protected:
    /// <summary>The cursor is over the window: the tool's cursor is drawn (+0x40).</summary>
    bool cursorInView = false;
    /// <summary>The bitmap the map is drawn into (+0x44).</summary>
    HBITMAP dib = nullptr;
    /// <summary>Its pixels (+0x48).</summary>
    uint8_t* bits = nullptr;
    /// <summary>Its size: the window's rounded up to a multiple of 4 (+0x4c, +0x50).</summary>
    int32_t bitsWidth = 0;
    int32_t bitsHeight = 0;
    /// <summary>The current tool (+0x54); a select tool at first.</summary>
    EditorTool* tool = nullptr;
    /// <summary>
    /// The left button is down on the map and the tool in use (+0x58); the view scrolls when the cursor nears an edge.
    /// </summary>
    bool dragging = false;
    /// <summary>The tool can be applied where the cursor is (+0x59): its cursor or its "not allowed" one is drawn.</summary>
    bool canApply = false;
    /// <summary>The command that made the current tool (+0x5c).</summary>
    int32_t toolCommand = ID_TOOLS_SELECT;
    /// <summary>The index of the gamma setting in the gamma menu (+0x60).</summary>
    int32_t gammaIndex = 0;
    /// <summary>The view position of the window's top-left corner (+0x64).</summary>
    POINT origin = {};
    /// <summary>The tiles are outlined and the footprints drawn (+0x6c).</summary>
    bool gridlines = true;
    /// <summary>The cells' passability is drawn (+0x6d).</summary>
    bool showCells = false;
    /// <summary>
    /// The select tool is in use (+0x6e): the palette's terrain and overlay buttons paint the selection instead of
    /// picking a tool.
    /// </summary>
    bool selectMode = false;
    /// <summary>The window position of the last mouse message (+0x70).</summary>
    POINT lastMouse = {};
    /// <summary>The map position (in the game's units) of the last click, the Delta pane's start (+0x78).</summary>
    POINT measurePoint = {};
    /// <summary>The DC the bitmap is selected into to draw and copy it (+0x80).</summary>
    CDC* memoryDC = nullptr;
    /// <summary>The palette of the window's DC (+0x84).</summary>
    CPalette palette;
    /// <summary>The last draw scrolled (+0x8c).</summary>
    bool scrolled = false;
    /// <summary>The objectives dialog, since it was last opened (+0x94).</summary>
    ObjectivesTeamDialog* objectivesDialog = nullptr;
    /// <summary>The unit settings dialog, while it is open (+0x98): the tools are held off while it is shown.</summary>
    CWnd* pausingDialog = nullptr;
    /// <summary>The last pick was asked for by the unit settings dialog, not the objectives one (+0xa0).</summary>
    bool pickingForUnit = false;
    /// <summary>The view's timer (+0xa4).</summary>
    UINT_PTR timer = 0;
    /// <summary>The object the last right click was on (+0xa8), for its popup menu.</summary>
    EditorObject* clickedObject = nullptr;
    /// <summary>The cursor drawn when there is no tool (+0xac).</summary>
    CursorTool defaultCursor;
    /// <summary>The cached enablings below are worked out again on the next update (+0x8bc).</summary>
    bool refreshEnabling = true;
    /// <summary>The cached enablings of Rotate, Damage and Repair (+0x8bd, +0x8be, +0x8bf).</summary>
    bool rotateEnabled = true;
    bool damageEnabled = true;
    bool repairEnabled = true;
    /// <summary>The request for the WM_MOUSELEAVE that hides the tool's cursor (+0x8c0).</summary>
    TRACKMOUSEEVENT trackMouse = {};

    /// <summary>
    /// Takes the WM_MOUSELEAVE <see cref="trackMouse"/> asks for: hides the tool's cursor and ends a drag as the
    /// button's release does.
    /// </summary>
    /// <remarks>Virtual table slot 40 (0x00423960).</remarks>
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;
    /// <summary>
    /// Draws the tool's cursor image (or its "not allowed" one, or the default cursor's) centred on the cursor into
    /// the bitmap, its 0xff pixels transparent.
    /// </summary>
    /// <remarks>0x0041e1e0.</remarks>
    void drawToolCursor();
    /// <summary>Plays the click, unselects everything and makes the tool a select tool (area mode with <paramref name="area"/>).</summary>
    /// <remarks>Inlined in the select commands; the click and the unselecting only where the original does.</remarks>
    void selectTool(bool area, int32_t command);
    /// <summary>The document's map.</summary>
    EditorMap* map() const;
    /// <summary>
    /// Uses <paramref name="painter"/> on the selection (the palette's buttons in select mode): begins it, applies it
    /// to every selected tile (a message when that fails somewhere), puts its action on the undo list and redraws.
    /// </summary>
    /// <remarks>0x0041b6d0.</remarks>
    void applyToSelection(BrushTool& painter);
    /// <summary>
    /// Whether tiles are selected, or an object, mover or drop zone: the test the commands make before working on the
    /// selection in select mode.
    /// </summary>
    /// <remarks>Inlined in the Erase, Damage, Repair and Rotate commands.</remarks>
    bool anythingSelected() const;
    /// <summary>Whether an object, mover or drop zone is selected.</summary>
    bool anyObjectSelected() const;
    /// <summary>Whether <see cref="pausingDialog"/> is shown, which holds the tools off.</summary>
    bool toolsPaused() const;
    /// <summary>
    /// The Raise and Lower Vertex commands: a second press goes back to the select tool; in select mode with vertices
    /// selected they move by <paramref name="step"/> at once, otherwise a vertex tool of that step is the tool.
    /// </summary>
    /// <remarks>Inlined in 0x0041d2e0 and 0x0041d5b0.</remarks>
    void vertexCommand(int32_t command, int32_t step);
    /// <summary>Moves the selected vertices by <paramref name="step"/> as one undoable action.</summary>
    void moveSelectedVertices(int32_t step);
    /// <summary>
    /// The Raise and Lower commands (the keys): the selected vertices move by <paramref name="step"/>, or with none
    /// selected the Raise or Lower Vertex command runs.
    /// </summary>
    /// <remarks>Inlined in 0x0041da30 and 0x0041d880.</remarks>
    void raiseOrLower(int32_t step);
    /// <summary>
    /// The View menu's Zoom In and Out: zooms once about the window's centre, with the view's Zoom In tool when that
    /// is the tool, else with one of its own.
    /// </summary>
    /// <remarks>Inlined in 0x00421250 and 0x004213c0.</remarks>
    void zoomCommand(bool zoomIn);
    /// <summary>
    /// The Drag View and Zoom tool commands: a second press goes back to the select tool, otherwise the tool
    /// <paramref name="make"/> makes is the tool.
    /// </summary>
    /// <remarks>Inlined in 0x0041fd80, 0x004200d0 and 0x00420390.</remarks>
    void viewToolCommand(int32_t command, EditorTool* (*make)());
    /// <summary>
    /// The Make Hilly and Flatten commands: a second press goes back to the select tool; in select mode with tiles
    /// selected the selected vertices are made hilly or set to their average at once, otherwise the hill tool is the
    /// tool.
    /// </summary>
    /// <remarks>Inlined in 0x0041f0c0 and 0x0041f640.</remarks>
    void hillCommand(int32_t command, bool flatten);
    /// <summary>
    /// The Damage and Repair commands: a second press goes back to the select tool; in select mode with something
    /// selected it is damaged or repaired at once, otherwise the damage tool is the tool.
    /// </summary>
    /// <remarks>Inlined in 0x0041e7b0 and 0x0041eba0.</remarks>
    void damageCommand(int32_t command, bool damage);
    /// <summary>
    /// The Link and Unlink Buildings commands: a second press goes back to the select tool, otherwise the link tool
    /// is the tool.
    /// </summary>
    /// <remarks>Inlined in 0x00421a50 and 0x00421c30.</remarks>
    void linkCommand(int32_t command, bool link);
    /// <summary>
    /// The Left and Right keys: an object or mover place tool (its command up to <paramref name="lastObjectCommand"/>
    /// or a mover's) steps its object <paramref name="delta"/> frames; the wrong-button sound when it can't or the
    /// tools are paused.
    /// </summary>
    /// <remarks>Inlined in 0x0041f370 and 0x0041f4c0.</remarks>
    void frameCommand(int32_t delta, int32_t lastObjectCommand);
    /// <summary>
    /// Puts the object the last right click was on (and, for a control structure, the structures it is linked to) on
    /// <paramref name="side"/>.
    /// </summary>
    /// <remarks>0x00423480.</remarks>
    void setClickedSide(int32_t side);
    /// <summary>
    /// The popup menu for a right click on <paramref name="object"/> (null for none), loaded into
    /// <paramref name="menu"/>.
    /// </summary>
    /// <returns>Its first submenu; the base view has none.</returns>
    /// <remarks>Virtual table slot 66 (0x0041a5e0).</remarks>
    virtual CMenu* popupMenu(EditorObject* object, CMenu& menu);
    /// <summary>
    /// The views' popup menus: <paramref name="missionMenu"/> for no object, the unit menu for a mech or vehicle, else
    /// <paramref name="buildingMenu"/> with the object's side checked, without Rotate when it has one frame, and
    /// without the sides when an objective refers to it, its class takes no links, or it is linked to a control
    /// structure.
    /// </summary>
    /// <remarks>The bodies of SingleView's and MultiView's slot 66 (0x00430ca0, 0x0042bcc0).</remarks>
    CMenu* loadPopupMenu(EditorObject* object, CMenu& menu, UINT missionMenu, UINT buildingMenu);
    /// <summary>
    /// Replaces the unit settings dialog (if one is open) with a new one for <paramref name="mover"/>, shown centred.
    /// </summary>
    /// <remarks>Inlined in 0x00422d90 and 0x00423610.</remarks>
    void openUnitSettings(MoverObject& mover);

    /// <summary>
    /// The map's extent in the view: the leftmost and rightmost view x and the top and bottom view y of its edges'
    /// vertices.
    /// </summary>
    /// <remarks>0x0041b000, also inlined in <see cref="scrollTo"/> and <see cref="OnInitialUpdate"/>.</remarks>
    void mapExtent(int32_t* left, int32_t* right, int32_t* top, int32_t* bottom);
    /// <summary>The horizontal and vertical scroll positions.</summary>
    POINT scrollPosition();
    /// <summary>The tile art level of <see cref="viewZoom"/>: 0 full size, 1 the middle, 2 the smallest.</summary>
    static int32_t zoomLevel();

protected:
    /// <summary>Handles ON_WM_LBUTTONDOWN().</summary>
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);

    /// <summary>Handles ON_WM_LBUTTONUP().</summary>
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);

    /// <summary>Handles ON_WM_MOUSEMOVE().</summary>
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);

    /// <summary>Handles ON_WM_WINDOWPOSCHANGED().</summary>
    afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);

    /// <summary>Handles ON_COMMAND(ID_UNDO).</summary>
    afx_msg void OnUndo();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_UNDO).</summary>
    afx_msg void OnUpdateUndo(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_REDO).</summary>
    afx_msg void OnRedo();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_REDO).</summary>
    afx_msg void OnUpdateRedo(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_VIEW_GRIDLINES).</summary>
    afx_msg void OnViewGridlines();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_VIEW_GRIDLINES).</summary>
    afx_msg void OnUpdateViewGridlines(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_TOOLS_SELECT_AREA).</summary>
    afx_msg void OnToolsSelectArea();

    /// <summary>Handles ON_COMMAND(ID_TOOLS_SELECT).</summary>
    afx_msg void OnToolsSelect();

    /// <summary>Handles ON_COMMAND(ID_PAINT).</summary>
    afx_msg void OnPaint();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_PAINT).</summary>
    afx_msg void OnUpdatePaint(CCmdUI* pCmdUI);

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_TOOLS_SELECT).</summary>
    afx_msg void OnUpdateToolsSelect(CCmdUI* pCmdUI);

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_TOOLS_SELECT_AREA).</summary>
    afx_msg void OnUpdateToolsSelectArea(CCmdUI* pCmdUI);

    /// <summary>Handles ON_WM_ERASEBKGND().</summary>
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);

    /// <summary>Handles ON_COMMAND(ID_TOOLS_RAISE_VERTEX).</summary>
    afx_msg void OnToolsRaiseVertex();

    /// <summary>Handles ON_COMMAND(ID_TOOLS_LOWER_VERTEX).</summary>
    afx_msg void OnToolsLowerVertex();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_TOOLS_LOWER_VERTEX).</summary>
    afx_msg void OnUpdateToolsLowerVertex(CCmdUI* pCmdUI);

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_TOOLS_RAISE_VERTEX).</summary>
    afx_msg void OnUpdateToolsRaiseVertex(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_LOWER).</summary>
    afx_msg void OnLower();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_LOWER).</summary>
    afx_msg void OnUpdateLower(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_RAISE).</summary>
    afx_msg void OnRaise();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_RAISE).</summary>
    afx_msg void OnUpdateRaise(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_TOOLS_ERASE).</summary>
    afx_msg void OnToolsErase();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_TOOLS_ERASE).</summary>
    afx_msg void OnUpdateToolsErase(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_ZOOM_OUT).</summary>
    afx_msg void OnZoomOut();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_ZOOM_OUT).</summary>
    afx_msg void OnUpdateZoomOut(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_ZOOM_IN).</summary>
    afx_msg void OnZoomIn();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_ZOOM_IN).</summary>
    afx_msg void OnUpdateZoomIn(CCmdUI* pCmdUI);

    /// <summary>Handles ON_WM_DESTROY().</summary>
    afx_msg void OnDestroy();

    /// <summary>Handles ON_COMMAND(ID_SELECT_ALL).</summary>
    afx_msg void OnSelectAll();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_SELECT_ALL).</summary>
    afx_msg void OnUpdateSelectAll(CCmdUI* pCmdUI);

    /// <summary>Handles ON_WM_RBUTTONUP().</summary>
    afx_msg void OnRButtonUp(UINT nFlags, CPoint point);

    /// <summary>Handles ON_COMMAND(ID_TOOLS_MAKE_HILLY).</summary>
    afx_msg void OnToolsMakeHilly();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_TOOLS_MAKE_HILLY).</summary>
    afx_msg void OnUpdateToolsMakeHilly(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_PREVIOUS_FRAME), the Left key: the place tool's object turns back a frame.</summary>
    afx_msg void OnPreviousFrame();

    /// <summary>Handles ON_COMMAND(ID_NEXT_FRAME), the Right key: the place tool's object turns on a frame.</summary>
    afx_msg void OnNextFrame();

    /// <summary>Handles ON_WM_TIMER().</summary>
    afx_msg void OnTimer(UINT_PTR nIDEvent);

    /// <summary>Handles ON_COMMAND(ID_TOOLS_FLATTEN).</summary>
    afx_msg void OnToolsFlatten();

    /// <summary>
    /// Handles ON_COMMAND(ID_KEY_ESCAPE), the Escape key: finishes an orders path and, out of a drag, goes back to
    /// the select tool.
    /// </summary>
    afx_msg void OnKeyEscape();

    /// <summary>Handles ON_WM_HSCROLL().</summary>
    afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);

    /// <summary>Handles ON_WM_VSCROLL().</summary>
    afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);

    /// <summary>Handles ON_COMMAND(ID_TOGGLE_CELLS).</summary>
    afx_msg void OnToggleCells();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_TOGGLE_CELLS).</summary>
    afx_msg void OnUpdateToggleCells(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_VIEW_ZOOM_IN).</summary>
    afx_msg void OnViewZoomIn();

    /// <summary>Handles ON_COMMAND(ID_VIEW_ZOOM_OUT).</summary>
    afx_msg void OnViewZoomOut();

    /// <summary>Handles ON_COMMAND(ID_NO_TOOL): drops the tool, leaving none (no menu item or key sends it).</summary>
    afx_msg void OnNoTool();

    /// <summary>Handles ON_COMMAND(ID_MISSION_DROP_ZONES_TEAM_1).</summary>
    afx_msg void OnMissionDropZonesTeam1();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_MISSION_DROP_ZONES_TEAM_1).</summary>
    afx_msg void OnUpdateMissionDropZonesTeam1(CCmdUI* pCmdUI);

    /// <summary>Handles ON_WM_KEYDOWN().</summary>
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);

    /// <summary>Handles ON_COMMAND(ID_KEY_F10), the F10 key: a click, then F10 to <see cref="OnKeyDown"/>.</summary>
    afx_msg void OnKeyF10();

    /// <summary>Handles ON_COMMAND(ID_VIEW_TACTICAL_MAP).</summary>
    afx_msg void OnViewTacticalMap();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_VIEW_TACTICAL_MAP).</summary>
    afx_msg void OnUpdateViewTacticalMap(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_TOOLS_LINK_BUILDINGS).</summary>
    afx_msg void OnToolsLinkBuildings();

    /// <summary>Handles ON_COMMAND(ID_TOOLS_UNLINK_BUILDINGS).</summary>
    afx_msg void OnToolsUnlinkBuildings();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_TOOLS_LINK_BUILDINGS).</summary>
    afx_msg void OnUpdateToolsLinkBuildings(CCmdUI* pCmdUI);

    /// <summary>Handles ON_WM_LBUTTONDBLCLK().</summary>
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);

    /// <summary>Handles ON_COMMAND(ID_KEY_ENTER), the Enter key: finishes an orders path.</summary>
    afx_msg void OnKeyEnter();

    /// <summary>Handles ON_COMMAND(ID_COUNT_BUILDINGS): shows the number of objects on the map (no menu item or key sends it).</summary>
    afx_msg void OnCountBuildings();

    /// <summary>Handles ON_COMMAND(ID_SAVE_MAP_IMAGE): saves the whole map at the current zoom as a Targa (no menu item or key sends it).</summary>
    afx_msg void OnSaveMapImage();

    /// <summary>Handles ON_COMMAND(ID_ALLIED).</summary>
    afx_msg void OnAllied();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_ALLIED).</summary>
    afx_msg void OnUpdateAllied(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_ENEMY).</summary>
    afx_msg void OnEnemy();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_ENEMY).</summary>
    afx_msg void OnUpdateEnemy(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_NEUTRAL).</summary>
    afx_msg void OnNeutral();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_NEUTRAL).</summary>
    afx_msg void OnUpdateNeutral(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_UNIT_SETTINGS).</summary>
    afx_msg void OnUnitSettings();

    /// <summary>Handles ON_COMMAND(ID_CONTEXT_HELP).</summary>
    afx_msg void OnContextHelp();

    /// <summary>Handles ON_COMMAND(ID_HELP).</summary>
    afx_msg void OnHelp();

    /// <summary>Handles ON_WM_SETCURSOR().</summary>
    afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_TOOLS_FLATTEN).</summary>
    afx_msg void OnUpdateToolsFlatten(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND_RANGE(TERRAIN_COMMANDS, TERRAIN_COMMANDS_END), the palette's terrain buttons and keys 1-8.</summary>
    afx_msg void OnTerrainCommand(UINT nID);

    /// <summary>
    /// Handles ON_UPDATE_COMMAND_UI_RANGE(OBJECT_COMMANDS, OBJECT_COMMANDS_LAST) and
    /// ON_UPDATE_COMMAND_UI_RANGE(MOVER_COMMANDS, MOVER_COMMANDS_LAST), the Map Features
    /// palette's building and unit buttons: enabled, checked while their command is the tool.
    /// </summary>
    afx_msg void OnUpdateFeatureButton(CCmdUI* pCmdUI);

    /// <summary>
    /// Handles ON_UPDATE_COMMAND_UI_RANGE(TERRAIN_COMMANDS, TERRAIN_COMMANDS_END) and
    /// ON_UPDATE_COMMAND_UI_RANGE(OVERLAY_COMMANDS, OVERLAY_COMMANDS_LAST),
    /// the palette's terrain and overlay buttons.
    /// </summary>
    afx_msg void OnUpdatePaletteCommand(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND_RANGE(OVERLAY_COMMANDS, OVERLAY_COMMANDS_LAST), the palette's overlay buttons and Shift+1-0, _.</summary>
    afx_msg void OnOverlayCommand(UINT nID);

    /// <summary>Handles ON_MESSAGE(WM_EDITOR_SELECT_OBJECT): the place tool for the palette's object.</summary>
    afx_msg LRESULT OnSelectObject(WPARAM wParam, LPARAM lParam);

    /// <summary>Handles ON_MESSAGE(WM_EDITOR_SELECT_MOVER): the place tool for the palette's mech or vehicle.</summary>
    afx_msg LRESULT OnSelectMover(WPARAM wParam, LPARAM lParam);

    /// <summary>Handles ON_MESSAGE(WM_EDITOR_CENTRE_ON_TACTICAL): scrolls the view to the tactical map point clicked.</summary>
    afx_msg LRESULT OnCentreOnTacticalPoint(WPARAM wParam, LPARAM lParam);

    /// <summary>Handles ON_MESSAGE(WM_EDITOR_SCROLL_VIEW): scrolls by a step or centres on a view position.</summary>
    afx_msg LRESULT OnScrollView(WPARAM wParam, LPARAM lParam);

    /// <summary>Handles ON_COMMAND(ID_TOOLS_DAMAGE).</summary>
    afx_msg void OnToolsDamage();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_TOOLS_DAMAGE).</summary>
    afx_msg void OnUpdateToolsDamage(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_TOOLS_REPAIR).</summary>
    afx_msg void OnToolsRepair();

    /// <summary>Handles ON_COMMAND_RANGE(OVERLAY_LAYOUT_COMMANDS, OVERLAY_LAYOUT_COMMANDS_LAST), the overlay layout buttons.</summary>
    afx_msg void OnOverlayLayoutCommand(UINT nID);

    /// <summary>
    /// Handles ON_UPDATE_COMMAND_UI_RANGE(OVERLAY_LAYOUT_COMMANDS, OVERLAY_LAYOUT_COMMANDS_LAST), the palette's overlay
    /// layout buttons: checked while their
    /// command is the tool.
    /// </summary>
    afx_msg void OnUpdateOverlayLayoutCommand(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_TOOLS_DRAG_VIEW).</summary>
    afx_msg void OnToolsDragView();

    /// <summary>Handles ON_COMMAND(ID_TOOLS_ZOOM_IN).</summary>
    afx_msg void OnToolsZoomIn();

    /// <summary>Handles ON_COMMAND(ID_TOOLS_ZOOM_OUT).</summary>
    afx_msg void OnToolsZoomOut();

    /// <summary>Handles ON_COMMAND(ID_TOOLS_SPRAY_TREES).</summary>
    afx_msg void OnToolsSprayTrees();

    /// <summary>Handles ON_COMMAND(IDS_ASSIGN_A_SPECIFIC_HEIGHT).</summary>
    afx_msg void OnIdsAssignASpecificHeight();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(IDS_ASSIGN_A_SPECIFIC_HEIGHT).</summary>
    afx_msg void OnUpdateIdsAssignASpecificHeight(CCmdUI* pCmdUI);

    /// <summary>Handles ON_COMMAND(ID_TOOLS_ROTATE).</summary>
    afx_msg void OnToolsRotate();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_TOOLS_ROTATE).</summary>
    afx_msg void OnUpdateToolsRotate(CCmdUI* pCmdUI);

    /// <summary>Handles ON_MESSAGE(WM_EDITOR_PICK): a pick tool for a dialog's value field, the dialog hidden meanwhile.</summary>
    afx_msg LRESULT OnPickOnMap(WPARAM wParam, LPARAM lParam);

    /// <summary>Handles ON_COMMAND(ID_ROTATE_BUILDING).</summary>
    afx_msg void OnRotateBuilding();

    /// <summary>Handles ON_UPDATE_COMMAND_UI(ID_ROTATE_BUILDING).</summary>
    afx_msg void OnUpdateRotateBuilding(CCmdUI* pCmdUI);

    /// <summary>
    /// Port-only: handles ON_COMMAND_RANGE(ID_MISSION_TIME_OF_DAY_DAY, ID_MISSION_TIME_OF_DAY_NIGHT), Mission > Time of
    /// Day: sets the mission's time of day, and marks the document changed if it was another.
    /// </summary>
    afx_msg void OnMissionTimeOfDay(UINT nID);

    /// <summary>
    /// Port-only: handles ON_UPDATE_COMMAND_UI_RANGE(ID_MISSION_TIME_OF_DAY_DAY, ID_MISSION_TIME_OF_DAY_NIGHT): the
    /// mission's time of day is marked.
    /// </summary>
    afx_msg void OnUpdateMissionTimeOfDay(CCmdUI* pCmdUI);

    /// <summary>
    /// Port-only: handles ON_COMMAND_RANGE(MISSION_WEATHER_FIRST_COMMAND, MISSION_WEATHER_LAST_COMMAND), Mission >
    /// Weather: sets the mission's weather, and marks the document changed if it was another.
    /// </summary>
    afx_msg void OnMissionWeather(UINT nID);

    /// <summary>
    /// Port-only: handles ON_UPDATE_COMMAND_UI_RANGE(MISSION_WEATHER_FIRST_COMMAND, MISSION_WEATHER_LAST_COMMAND): the
    /// mission's weather is marked.
    /// </summary>
    afx_msg void OnUpdateMissionWeather(CCmdUI* pCmdUI);

    DECLARE_MESSAGE_MAP()
};

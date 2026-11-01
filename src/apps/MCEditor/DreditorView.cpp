#include "stdafx.h"
#include "DreditorView.h"
#include "DreditorDoc.h"
#include "EditorMap.h"
#include "EditorPalette.h"
#include "EditorObject.h"
#include "EditorTile.h"
#include "BrushTool.h"
#include "ControlLink.h"
#include "DamageTool.h"
#include "HillTool.h"
#include "LinkTool.h"
#include "MapFeaturesBar.h"
#include "MapPickTool.h"
#include "MoverObject.h"
#include "ObjectivesTeamDialog.h"
#include "UnitSettingsDialog.h"
#include "ObjectLibrary.h"
#include "OverlayTools.h"
#include "PlaceMoverTool.h"
#include "RotateTool.h"
#include "SelectTool.h"
#include "SetHeightDialog.h"
#include "SprayTreesTool.h"
#include "TacticalMapDialog.h"
#include "TileLibrary.h"
#include "ToolFactory.h"
#include "UndoList.h"
#include "VertexTool.h"
#include "ViewTools.h"
#include "X87Extended.h"
#include "resource.h"
#include "EditorFiles.h"

#include <bit>
#include <mmsystem.h>

float viewZoom = 0.5f;
TacticalMapDialog* tacticalMap = nullptr;

namespace
{
    /// <summary>How near an edge the cursor scrolls the view during a drag, and how far a scroll step goes.</summary>
    inline constexpr int32_t SCROLL_STEP = 0x18;
    /// <summary>Added to the map's extent for the scroll ranges.</summary>
    inline constexpr int32_t SCROLL_MARGIN = 0x14;
    /// <summary>The zooms above which the full-size and the middle tiles are drawn.</summary>
    inline constexpr double FULL_ZOOM = 0.99;
    inline constexpr double MIDDLE_ZOOM = 0.49;
    /// <summary>The colour the bitmap is cleared to before drawing.</summary>
    inline constexpr int BACKGROUND_COLOR = 0x10;
    /// <summary>RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW.</summary>
    inline constexpr UINT REDRAW_NOW = 0x121;
    /// <summary>The gammas of the gamma menu (0x004fb568).</summary>
    inline constexpr float GAMMAS[] = {0.4f, 0.55f, 0.7f, 0.8f, 1.0f, 1.3f, 1.6f, 2.0f};
    /// <summary>The tactical map's scale and the rotation of the view into it (0x004fc128, 0x004fc130, 0x004fc138).</summary>
    const double TACTICAL_SCALE = std::bit_cast<double>(0x4000f876ccdf6b99ull);
    const double MINUS_HALF_SQRT2 = std::bit_cast<double>(0xbfe6a09e667f3a21ull);
    const double HALF_SQRT2 = std::bit_cast<double>(0x3fe6a09e667f3a21ull);
    /// <summary>The tactical map's pixels back to map units across and down (0x004fc178, 0x004fc170).</summary>
    const double TACTICAL_X_SCALE = std::bit_cast<double>(0x3fde2b7dddfefca0ull);
    const double TACTICAL_Y_SCALE = std::bit_cast<double>(0x3fd5555555555555ull);
    /// <summary>The tactical map window's offset below the view when no position is kept.</summary>
    inline constexpr int32_t TACTICAL_MAP_OFFSET_Y = 0x28;
    /// <summary>The tactical map is put back on the screen when its corner is within this of the screen's edge.</summary>
    inline constexpr int32_t SCREEN_MARGIN = 0x1e;
    /// <summary>RDW_INVALIDATE | RDW_ERASE | RDW_FRAME.</summary>
    inline constexpr UINT REDRAW_FRAME_ERASE = 0x105;

    /// <summary>The select cursor's hot spot (the default cursor's).</summary>
    inline constexpr int32_t SELECT_CURSOR_HOT_X = 10;
    inline constexpr int32_t SELECT_CURSOR_HOT_Y = 4;
    /// <summary>RDW_INVALIDATE | RDW_ERASE | RDW_FRAME, the redraw when the cursor leaves.</summary>
    inline constexpr UINT REDRAW_FRAME = 0x105;
    /// <summary>The cursor moves at least this far (in either direction) for a mouse message to count.</summary>
    inline constexpr int32_t MOUSE_SLOP = 2;
    /// <summary>The game's map units per editor tile (a tile is <c>tileWorldWidth</c> wide in the editor).</summary>
    inline constexpr int32_t GAME_TILE_SIZE = 0x9e;
    /// <summary>The game's elevation units per editor elevation step, as a factor of <c>elevationScale</c> (0x004fc158).</summary>
    inline constexpr double GAME_ELEVATION_SCALE = 28.0;
    /// <summary>Metres per game tile in the X and Y panes (0x004fc150), and per game elevation unit (0x004fc148).</summary>
    inline constexpr double METRES_PER_TILE = 3.5;
    const double METRES_PER_ELEVATION = std::bit_cast<double>(0x3fe9249249249249ull);
    /// <summary>The click's map position's scale to the game's units (0x004fc140).</summary>
    const double MEASURE_SCALE = std::bit_cast<double>(0x3fe9ec8e951033d9ull);
    /// <summary>The status bar's panes.</summary>
    inline constexpr int PANE_X = 1;
    inline constexpr int PANE_Y = 2;
    inline constexpr int PANE_Z = 3;
    inline constexpr int PANE_BLOCK = 4;
    inline constexpr int PANE_VERTEX = 5;
    inline constexpr int PANE_DELTA = 6;
    inline constexpr int PANE_RANGE = 7;
    inline constexpr int PANE_AREA = 8;
    /// <summary>The Undo and Redo items' formats and the failures' messages.</summary>
    inline constexpr UINT UNDO_FAILED = 0xe003;
    inline constexpr UINT REDO_FAILED = 0xe004;
    /// <summary>The message when a palette button couldn't paint all of the selection.</summary>
    inline constexpr UINT PAINT_FAILED = 0xe005;
    /// <summary>The caption of the views' message boxes ("MCEditor").</summary>
    inline constexpr UINT MESSAGE_CAPTION = 2;
    /// <summary>The zooms below which Zoom Out and from which Zoom In are off (0x004fc160, 0x004fc168).</summary>
    inline constexpr double SMALLEST_ZOOM = 0.26;
    inline constexpr double LARGEST_ZOOM = 1.0;
    /// <summary>The sides an object can be on.</summary>
    inline constexpr int32_t NEUTRAL_SIDE = 0;
    inline constexpr int32_t ENEMY_SIDE = 1;
    inline constexpr int32_t PLAYER_SIDE = 2;
    /// <summary>The player's drop zones beyond which Drop Zones (team 1) is off.</summary>
    inline constexpr int32_t MAX_PLAYER_DROP_ZONES = 2;
    /// <summary>The longest status text the views load (a 32-byte buffer).</summary>
    inline constexpr int STATUS_FORMAT_SIZE = 0x20;
    /// <summary>The Raise and Lower tools' steps (0x004faed4, 0x004faed8).</summary>
    inline constexpr int32_t RAISE_STEP = 1;
    inline constexpr int32_t LOWER_STEP = -1;
    /// <summary>The Erase tool's cursor hot spot.</summary>
    inline constexpr int32_t ERASE_HOT_X = 4;
    inline constexpr int32_t ERASE_HOT_Y = 0x1a;
    /// <summary>The Erase tool erases whatever overlay it finds.</summary>
    inline constexpr int32_t ANY_OVERLAY = -1;
    /// <summary>The palette's overlay layout commands (10000-10100).</summary>
    inline constexpr UINT OVERLAY_LAYOUT_COMMANDS = 10000;
    inline constexpr UINT OVERLAY_LAYOUT_COMMANDS_LAST = 10100;
    /// <summary>The Rotate tool turns by one frame (0x004fac9c).</summary>
    inline constexpr int32_t ROTATE_STEP = 1;
    /// <summary>The tool command of a unit's orders path tool (WM_EDITOR_PICK), whose Erase takes its last point off.</summary>
    inline constexpr int32_t ORDERS_PATH_COMMAND = 0x505;
    /// <summary>The first commands of the palette's buildings (by type) and units (by group).</summary>
    inline constexpr int32_t OBJECT_COMMANDS = 0x66;
    inline constexpr int32_t MOVER_COMMANDS = 200;
    /// <summary>The last commands the building and unit buttons' update ranges cover.</summary>
    inline constexpr int32_t OBJECT_COMMANDS_LAST = 202;
    inline constexpr int32_t MOVER_COMMANDS_LAST = 300;
    /// <summary>The last object and mover commands the Left and Right keys step through.</summary>
    inline constexpr int32_t LAST_OBJECT_COMMAND_LEFT = 0x98;
    inline constexpr int32_t LAST_OBJECT_COMMAND_RIGHT = 0x97;
    inline constexpr int32_t LAST_MOVER_COMMAND = 0xfa;
    /// <summary>The Left and Right keys step a place tool's object by one frame.</summary>
    inline constexpr int32_t FRAME_STEP = 1;
    /// <summary>"None" in the palette's messages.</summary>
    inline constexpr int32_t NO_PICK = -1;
    /// <summary>The object id of the player's drop zone (frame 0) and its side.</summary>
    inline constexpr uint32_t DROP_ZONE_ID = 2;
    /// <summary>The command of no tool (32834).</summary>
    inline constexpr int32_t NO_TOOL_COMMAND = -1;
    /// <summary>
    /// The gamma steps the F7 and F8 keys cycle through: the table's 8 and one past it, where the original reads the
    /// pointer that follows the table (0x004fb588) as the gamma.
    /// </summary>
    inline constexpr int32_t GAMMA_STEPS = 9;
    const float GAMMA_PAST_TABLE = std::bit_cast<float>(0x005274fcu);
    /// <summary>The help contexts: the view's own, and those of the tool kinds.</summary>
    inline constexpr DWORD HELP_VIEW = 0x65;
    inline constexpr DWORD HELP_SELECT = 0x6e;
    inline constexpr DWORD HELP_OVERLAYS = 0x78;
    inline constexpr DWORD HELP_OVERLAY_LAYOUTS = 0x82;
    inline constexpr DWORD HELP_BUILDINGS = 0x8c;
    inline constexpr DWORD HELP_UNITS = 0x96;
    inline constexpr DWORD HELP_TOOLS = 0xaa;
    /// <summary>The ends of the command ranges the context help tells apart.</summary>
    inline constexpr int32_t TEXTURE_COMMANDS = 6000;
    inline constexpr int32_t LAST_TEXTURE_COMMAND = 0x17d3;
    inline constexpr int32_t OVERLAY_COMMANDS_END = 0x13ec;
    inline constexpr int32_t OVERLAY_LAYOUT_COMMANDS_END = 0x2774;
    inline constexpr int32_t OBJECT_COMMANDS_END = 0x98;
    inline constexpr int32_t LAST_MOVER_HELP_COMMAND = 0xf9;

    /// <summary>A status format string (at most 31 characters, as the original's buffer holds).</summary>
    CString statusFormat(UINT id)
    {
        char buffer[STATUS_FORMAT_SIZE];
        LoadStringA(GetModuleHandleA(nullptr), id, buffer, STATUS_FORMAT_SIZE);
        return buffer;
    }

    // Port-only: Mission > Time of Day's commands follow MissionTimeOfDay's order.
    static_assert(ID_MISSION_TIME_OF_DAY_DUSK - ID_MISSION_TIME_OF_DAY_DAY ==
                      static_cast<long>(MissionTimeOfDay::Dusk) &&
                  ID_MISSION_TIME_OF_DAY_NIGHT - ID_MISSION_TIME_OF_DAY_DAY ==
                      static_cast<long>(MissionTimeOfDay::Night));

    /// <summary>Port-only: the time of day a Mission > Time of Day command, <paramref name="id"/>, sets.</summary>
    MissionTimeOfDay timeOfDayCommanded(UINT id)
    {
        return MissionTimeOfDayOf(static_cast<long>(id - ID_MISSION_TIME_OF_DAY_DAY));
    }

    /// <summary>The click the views play for some commands (0x0041b8b0): the data path's Data\Sound\button7.wav.</summary>
    void playClickSound()
    {
        CString path = EditorDataPath();
        path += "\\Data\\Sound\\button7.wav";
        PlaySoundA(path, nullptr, SND_FILENAME | SND_ASYNC);
    }

    /// <summary>The sound of a key that does nothing: the data path's Data\Sound\wrongbt.WAV.</summary>
    void playWrongSound()
    {
        CString path = EditorDataPath();
        path += "\\Data\\Sound\\wrongbt.WAV";
        PlaySoundA(path, nullptr, SND_FILENAME | SND_ASYNC);
    }

    /// <summary>A scroll bar's settings.</summary>
    SCROLLINFO getScrollInfo(CWnd& window, int bar)
    {
        SCROLLINFO info = {sizeof(SCROLLINFO)};
        window.GetScrollInfo(bar, &info, SIF_ALL);
        return info;
    }
}

IMPLEMENT_DYNCREATE(DreditorView, CView)

DreditorView::DreditorView() : defaultCursor(IDB_SELECT_CURSOR, SELECT_CURSOR_HOT_X, SELECT_CURSOR_HOT_Y)
{
    tool = new SelectTool(false);

    if (tacticalMap == nullptr)
    {
        tacticalMap = new TacticalMapDialog(this);
    }

    const CString lastGamma = AfxGetApp()->GetProfileString("Gamma", "LastGamma", "1.0");
    const double gamma = atof(lastGamma);

    for (int32_t i = 0; i < static_cast<int32_t>(std::size(GAMMAS)); i++)
    {
        if (gamma == static_cast<double>(GAMMAS[i]))
        {
            gammaIndex = i;
        }
    }
}

DreditorView::~DreditorView()
{
    if (dib != nullptr)
    {
        DeleteObject(dib);
    }

    delete memoryDC;
    // The original leaves the objectives dialog to the end of the process.
    delete objectivesDialog;
    // Original behaviour: any view's end deletes the tactical map object.
    delete tacticalMap;
    tacticalMap = nullptr;
}

void DreditorView::OnDraw(CDC* pDC)
{
    POINT cursor = {};

    if (GetCursorPos(&cursor))
    {
        ::ScreenToClient(m_hWnd, &cursor);
    }

    bool moved = false;

    if (!dragging)
    {
        scrolled = false;
    }
    else
    {
        CRect client;
        GetClientRect(&client);

        if (cursor.x < SCROLL_STEP)
        {
            const POINT position = scrollPosition();
            scrollTo({position.x - SCROLL_STEP, position.y});
            moved = true;
        }
        else if (client.Width() - SCROLL_STEP < cursor.x)
        {
            const POINT position = scrollPosition();
            scrollTo({position.x + SCROLL_STEP, position.y});
            moved = true;
        }

        if (cursor.y < SCROLL_STEP)
        {
            const POINT position = scrollPosition();
            scrollTo({position.x, position.y - SCROLL_STEP});
            moved = true;
        }
        else if (client.Height() - SCROLL_STEP < cursor.y)
        {
            const POINT position = scrollPosition();
            scrollTo({position.x, position.y + SCROLL_STEP});
            moved = true;
        }

        scrolled = moved;
    }

    EditorMap* map = static_cast<DreditorDoc*>(m_pDocument)->map;
    pDC->SelectPalette(&palette, FALSE);
    ::RealizePalette(pDC->m_hDC);
    BITMAPINFO* info = editorPalette->bitmapInfo;
    info->bmiHeader.biWidth = bitsWidth;
    info->bmiHeader.biHeight = -bitsHeight;
    // Original behaviour: the size is given in bits.
    info->bmiHeader.biSizeImage = bitsHeight * bitsWidth * 8;

    if (memoryDC == nullptr)
    {
        memoryDC = new CDC;
        memoryDC->Attach(CreateCompatibleDC(pDC->m_hDC));
    }

    if (dib == nullptr)
    {
        dib = CreateDIBSection(pDC->m_hDC, info, DIB_RGB_COLORS, reinterpret_cast<void**>(&bits), nullptr, 0);
    }

    int32_t zoom = 2;

    if (static_cast<double>(viewZoom) > FULL_ZOOM)
    {
        zoom = 0;
    }
    else if (MIDDLE_ZOOM < static_cast<double>(viewZoom))
    {
        zoom = 1;
    }

    std::memset(bits, BACKGROUND_COLOR, static_cast<size_t>(bitsHeight) * bitsWidth);
    const POINT viewOrigin = origin;

    // The original draws the map without testing for one.
    if (map != nullptr)
    {
        map->drawTerrain(viewOrigin, bits, bitsWidth, bitsHeight, gridlines, zoom);
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDC->m_hDC, dib);
    const bool links = toolCommand == ID_TOOLS_LINK_BUILDINGS || toolCommand == ID_TOOLS_UNLINK_BUILDINGS;

    // The original draws the marks without testing for a map.
    if (map != nullptr)
    {
        map->drawMarks(viewOrigin, memoryDC->m_hDC, bitsWidth, bitsHeight, zoom, links);
    }

    if (gridlines && map != nullptr)
    {
        map->drawFootprints(origin, memoryDC->m_hDC, bitsWidth, bitsHeight, showCells, zoom);
    }

    if (cursorInView)
    {
        drawToolCursor();
    }

    BitBlt(pDC->m_hDC, 0, 0, bitsWidth, bitsHeight, memoryDC->m_hDC, 0, 0, SRCCOPY);
    SelectObject(memoryDC->m_hDC, oldBitmap);

    if (moved)
    {
        OnMouseMove(0, cursor);
    }
}

void DreditorView::OnInitialUpdate()
{
    CView::OnInitialUpdate();
    ModifyStyle(0, WS_HSCROLL | WS_VSCROLL, 0);
    trackMouse.cbSize = sizeof(TRACKMOUSEEVENT);
    trackMouse.dwFlags = TME_LEAVE;
    trackMouse.hwndTrack = m_hWnd;
    EditorMap* map = static_cast<DreditorDoc*>(m_pDocument)->map;

    // The original reads the map without testing for one.
    if (map != nullptr)
    {
        int32_t left;
        int32_t right;
        int32_t top;
        int32_t bottom;
        mapExtent(&left, &right, &top, &bottom);
        SCROLLINFO info = {sizeof(SCROLLINFO), SIF_RANGE};
        info.nMax = right - left + SCROLL_MARGIN;
        SetScrollInfo(SB_HORZ, &info, TRUE);
        info.nMax = bottom - top + SCROLL_MARGIN;
        SetScrollInfo(SB_VERT, &info, TRUE);
        CRect window;
        GetWindowRect(&window);
        scrollTo({(window.left - window.right - left + SCROLL_MARGIN + right) / 2,
                  (window.top - window.bottom - top + SCROLL_MARGIN + bottom) / 2});
    }

    // The tactical map shows this document; its window is made the first time, where the settings keep it (by
    // default 40 pixels below this window's top-left).
    tacticalMap->document = static_cast<DreditorDoc*>(m_pDocument);

    if (tacticalMap->m_hWnd == nullptr)
    {
        tacticalMap->Create(TacticalMapDialog::IDD, nullptr);
        tacticalMap->setParentView(this);
        tacticalMap->RedrawWindow(nullptr, nullptr, REDRAW_FRAME_ERASE);
        CRect window;
        GetWindowRect(&window);
        const int x = AfxGetApp()->GetProfileInt("TacMapLoc", "XPos", window.left);
        const int y = AfxGetApp()->GetProfileInt("TacMapLoc", "YPos", window.top + TACTICAL_MAP_OFFSET_Y);
        tacticalMap->SetWindowPos(nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    else
    {
        tacticalMap->RedrawWindow(nullptr, nullptr, REDRAW_FRAME_ERASE);
    }

    if (palette.m_hObject == nullptr)
    {
        palette.Attach(CreatePalette(editorPalette->gammaPalette));
    }

    showCells = false;
    SetFocus();
    resize(CSize(bitsWidth, bitsHeight), viewZoom);
}

BOOL DreditorView::OnScroll(UINT nScrollCode, UINT nPos, BOOL bDoScroll)
{
    ::InvalidateRgn(m_hWnd, nullptr, TRUE);
    return CView::OnScroll(nScrollCode, nPos, bDoScroll);
}

POINT DreditorView::scrollPosition()
{
    return {getScrollInfo(*this, SB_HORZ).nPos, getScrollInfo(*this, SB_VERT).nPos};
}

void DreditorView::mapExtent(int32_t* left, int32_t* right, int32_t* top, int32_t* bottom)
{
    EditorMap* map = static_cast<DreditorDoc*>(m_pDocument)->map;
    const int32_t across = map->maxRow;
    const int32_t down = map->maxCol;
    *left = 0x7fffffff;
    *right = -0x7fffffff;
    *top = 0x7fffffff;
    *bottom = -0x7fffffff;
    // Original behaviour: a vertex outside the map keeps the position of the one before.
    int32_t x = 0;
    int32_t y = 0;

    for (int32_t col = 0, row = down >> 1; col < across >> 1; col++, row--)
    {
        map->getVertexView(col, row, &x, &y);
        *top = (std::min)(*top, y);
    }

    for (int32_t col = across >> 1, row = down; col < across; col++, row--)
    {
        map->getVertexView(col, row, &x, &y);
        *bottom = (std::max)(*bottom, y);
    }

    for (int32_t col = 0, row = down >> 1; col < across >> 1; col++, row--)
    {
        map->getVertexView(col, row, &x, &y);
        *left = (std::min)(*left, x);
    }

    for (int32_t col = across >> 1, row = 0; col < across; col++, row++)
    {
        map->getVertexView(col, row, &x, &y);
        *right = (std::max)(*right, x);
    }
}

void DreditorView::scrollTo(const POINT& position)
{
    InvalidateRect(nullptr, TRUE);
    EditorMap* map = static_cast<DreditorDoc*>(m_pDocument)->map;

    if (map == nullptr)
    {
        return;
    }

    const int32_t across = map->maxRow;
    const int32_t down = map->maxCol;
    // The corners' view positions give the scroll bars' pages.
    int32_t topLeftX = 0;
    int32_t topLeftY = 0;
    int32_t topRightX = 0;
    int32_t topRightY = 0;
    int32_t bottomRightX = 0;
    int32_t bottomRightY = 0;
    int32_t bottomLeftX = 0;
    int32_t bottomLeftY = 0;
    map->getVertexView(0, 0, &topLeftX, &topLeftY);
    map->getVertexView(across, 0, &topRightX, &topRightY);
    map->getVertexView(across, down, &bottomRightX, &bottomRightY);
    map->getVertexView(0, down, &bottomLeftX, &bottomLeftY);
    int32_t left;
    int32_t right;
    int32_t top;
    int32_t bottom;
    mapExtent(&left, &right, &top, &bottom);
    CRect client;
    GetClientRect(&client);
    const int32_t originX = left - tileWorldWidth * 2;
    const int32_t originY = top - tileWorldHeight * 2;
    const int32_t rangeX = right + (tileWorldWidth * 2 - client.right) + client.left - originX + SCROLL_MARGIN;
    const int32_t rangeY = bottom + (tileWorldHeight * 2 - client.bottom) + client.top - originY;
    const auto clamp = [](int32_t value, int32_t limit) { return value > limit ? limit : (value < 0 ? 0 : value); };
    const int32_t positionX = clamp(position.x, rangeX);
    const int32_t positionY = clamp(position.y, rangeY + SCROLL_MARGIN);
    origin = {positionX + originX, positionY + originY};

    // The view's rectangle on the tactical map: its corners on the map, turned 45 degrees and scaled.
    using X = X87Extended;
    const int32_t halfHeight = ((down + 1) >> 1) * tileWorldHeight;
    int32_t mapX;
    int32_t mapY;
    int32_t mapZ;
    map->viewToMap(origin.x, origin.y, &mapX, &mapY, &mapZ);
    RECT rect;
    const float fromTop = (X(halfHeight) + X(-mapY)).toFloat();
    const X turned = X(mapX) * X(HALF_SQRT2);
    rect.left = static_cast<int32_t>(
        ((turned - X(fromTop) * X(MINUS_HALF_SQRT2)) * (X(TACTICAL_SCALE) / X(tileWorldWidth))).truncate());
    rect.top = static_cast<int32_t>(
        ((turned - X(fromTop) * X(HALF_SQRT2)) * (X(TACTICAL_SCALE) / X(tileWorldHeight))).truncate());
    map->viewToMap(origin.x + client.Width(), origin.y + client.Height(), &mapX, &mapY, &mapZ);
    const X scaledX = X(mapX) * (X(TACTICAL_SCALE) / X(tileWorldWidth));
    const float scaledFromTop = ((X(halfHeight) + X(-mapY)) * (X(TACTICAL_SCALE) / X(tileWorldHeight))).toFloat();
    const X turnedEnd = scaledX * X(HALF_SQRT2);
    rect.right = static_cast<int32_t>((turnedEnd - X(scaledFromTop) * X(MINUS_HALF_SQRT2)).truncate());
    rect.bottom = static_cast<int32_t>((turnedEnd - X(scaledFromTop) * X(HALF_SQRT2)).truncate());
    tacticalMap->setViewRect(rect);

    SCROLLINFO info = {sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMax = rangeX;
    info.nPage = static_cast<UINT>((topRightX - bottomLeftX) / (across >> 1));
    info.nPos = positionX;
    SetScrollInfo(SB_HORZ, &info, TRUE);
    info.nMin = 0;
    info.nMax = rangeY;
    info.nPage = static_cast<UINT>((bottomRightY - topLeftY) / (down >> 1));
    info.nPos = positionY;
    SetScrollInfo(SB_VERT, &info, TRUE);
}

void DreditorView::resize(const CSize& size, [[maybe_unused]] float zoom)
{
    const auto roundUp = [](int32_t value) { return value % 4 != 0 ? value / 4 * 4 + 4 : value; };
    bitsWidth = roundUp(size.cx);
    bitsHeight = roundUp(size.cy);
    // The original works out the view's centre from the scroll position and back again, which changes nothing.
    const POINT position = scrollPosition();

    if (dib != nullptr)
    {
        DeleteObject(dib);
        dib = nullptr;
        bits = nullptr;
    }

    scrollTo(position);
    InvalidateRect(nullptr, TRUE);
}

EditorMap* DreditorView::map() const
{
    return static_cast<DreditorDoc*>(m_pDocument)->map;
}

bool DreditorView::anythingSelected() const
{
    return map()->tilesSelected || anyObjectSelected();
}

bool DreditorView::anyObjectSelected() const
{
    const EditorMap& editorMap = *map();
    const auto selected = [](const std::list<EditorObject*>& list)
    { return std::any_of(list.begin(), list.end(), [](const EditorObject* object) { return object->selected; }); };
    return selected(editorMap.objects) || selected(editorMap.movers) || selected(editorMap.dropZones);
}

bool DreditorView::toolsPaused() const
{
    return pausingDialog != nullptr && ::IsWindowVisible(pausingDialog->m_hWnd);
}

void DreditorView::vertexCommand(int32_t command, int32_t step)
{
    playClickSound();

    if (toolCommand == command)
    {
        selectTool(false, ID_TOOLS_SELECT);
        return;
    }

    if (!selectMode || map()->selectedVertices.empty())
    {
        delete tool;
        tool = new VertexTool(step, TEXTURE_NONE, true, 0);
        toolCommand = command;
        selectMode = false;
    }
    else
    {
        moveSelectedVertices(step);
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::moveSelectedVertices(int32_t step)
{
    // Original behaviour: the tool's own step is always the raise step; the step given is the one used.
    VertexTool mover(RAISE_STEP, TEXTURE_NONE, true, 0);
    CWaitCursor wait;
    EditorMap& editorMap = *map();
    mover.begin(editorMap);
    mover.applyToSelection(editorMap, step);

    if (EditorAction* action = mover.end(); action != nullptr)
    {
        undoList->push(action);
        InvalidateRect(nullptr, TRUE);
    }
}

void DreditorView::raiseOrLower(int32_t step)
{
    if (map()->selectedVertices.empty())
    {
        if (step == RAISE_STEP)
        {
            OnToolsRaiseVertex();
        }
        else
        {
            OnToolsLowerVertex();
        }
    }
    else
    {
        VertexTool mover(RAISE_STEP, TEXTURE_NONE, true, 0);
        CWaitCursor wait;
        EditorMap& editorMap = *map();
        mover.begin(editorMap);
        mover.applyToSelection(editorMap, step);

        if (EditorAction* action = mover.end(); action != nullptr)
        {
            undoList->push(action);
        }

        ::InvalidateRect(tacticalMap->m_hWnd, nullptr, TRUE);
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::zoomCommand(bool zoomIn)
{
    playClickSound();
    CRect client;
    GetClientRect(&client);
    ZoomTool own(true, viewZoom);
    // The view's Zoom In tool zooms when it is the tool, so that it keeps count.
    EditorTool& zoomer = toolCommand == ID_TOOLS_ZOOM_IN ? *tool : own;
    EditorMap& editorMap = *map();
    zoomer.begin(editorMap);
    const int32_t x = (client.Width() >> 1) + origin.x;
    const int32_t y = (client.Height() >> 1) + origin.y;

    if (zoomIn)
    {
        zoomer.apply(editorMap, x, y, 0);
    }
    else
    {
        zoomer.applyAgain(editorMap, x, y, 0);
    }

    zoomer.end();
}

void DreditorView::viewToolCommand(int32_t command, EditorTool* (*make)())
{
    playClickSound();

    if (toolCommand == command)
    {
        selectTool(false, ID_TOOLS_SELECT);
        return;
    }

    // EB-085: the original deletes the tool during a drag too, and makes none.
    if (!dragging)
    {
        delete tool;
        tool = make();
        toolCommand = command;
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::drawToolCursor()
{
    EditorTool* cursorTool = tool != nullptr ? tool : &defaultCursor;
    POINT cursor;

    if (!GetCursorPos(&cursor))
    {
        return;
    }

    ::ScreenToClient(m_hWnd, &cursor);
    int32_t zoom = 2;

    if (static_cast<double>(viewZoom) > FULL_ZOOM)
    {
        zoom = 0;
    }
    else if (MIDDLE_ZOOM < static_cast<double>(viewZoom))
    {
        zoom = 1;
    }

    const POINT position = {origin.x + cursor.x, origin.y + cursor.y};
    POINT topLeft = {};
    SIZE size = {};
    const uint8_t* image = !canApply && cursorTool != &defaultCursor
                               ? cursorTool->notAllowedImage(*map(), position, &topLeft, &size, zoom)
                               : cursorTool->cursorImage(*map(), position, &topLeft, &size, zoom);
    // The image is centred on its returned position.
    const int32_t centreX = topLeft.x - origin.x;
    const int32_t centreY = topLeft.y - origin.y;

    if (image == nullptr || centreX <= 0 || centreX >= bitsWidth || centreY <= 0 || centreY >= bitsHeight)
    {
        return;
    }

    int32_t left = centreX - (size.cx >> 1);
    int32_t right = centreX + (size.cx >> 1);
    int32_t top = centreY - (size.cy >> 1);
    int32_t bottom = centreY + (size.cy >> 1);
    uint8_t* destination = bits + (std::max)(top, 0) * bitsWidth + (std::max)(left, 0);

    if (top < 0)
    {
        image -= top * size.cx;
        top = 0;
    }

    bottom = (std::min)(bottom, bitsHeight);

    if (left < 0)
    {
        image -= left;
        left = 0;
    }

    right = (std::min)(right, bitsWidth);

    for (int32_t y = top; y < bottom; y++)
    {
        for (int32_t x = 0; x < right - left; x++)
        {
            if (image[x] != 0xff)
            {
                destination[x] = image[x];
            }
        }

        destination += bitsWidth;
        image += size.cx;
    }
}

void DreditorView::selectTool(bool area, int32_t command)
{
    playClickSound();
    selectMode = false;
    delete tool;
    map()->deselectAll();
    tool = new SelectTool(area);
    selectMode = true;
    toolCommand = command;
    InvalidateRect(nullptr, TRUE);
    refreshEnabling = true;
}

LRESULT DreditorView::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_MOUSELEAVE)
    {
        cursorInView = false;
        RedrawWindow(nullptr, nullptr, REDRAW_FRAME);

        if (dragging)
        {
            if (EditorAction* action = tool->end(); action != nullptr)
            {
                undoList->push(action);
            }

            dragging = false;
            ReleaseCapture();
            ::InvalidateRect(tacticalMap->m_hWnd, nullptr, FALSE);
            InvalidateRect(nullptr, TRUE);
            Default();
        }
    }

    return CView::WindowProc(message, wParam, lParam);
}

BEGIN_MESSAGE_MAP(DreditorView, CView)
ON_WM_LBUTTONDOWN()
ON_WM_LBUTTONUP()
ON_WM_MOUSEMOVE()
ON_WM_WINDOWPOSCHANGED()
ON_COMMAND(ID_UNDO, &DreditorView::OnUndo)
ON_UPDATE_COMMAND_UI(ID_UNDO, &DreditorView::OnUpdateUndo)
ON_COMMAND(ID_REDO, &DreditorView::OnRedo)
ON_UPDATE_COMMAND_UI(ID_REDO, &DreditorView::OnUpdateRedo)
ON_COMMAND(ID_VIEW_GRIDLINES, &DreditorView::OnViewGridlines)
ON_UPDATE_COMMAND_UI(ID_VIEW_GRIDLINES, &DreditorView::OnUpdateViewGridlines)
ON_COMMAND(ID_TOOLS_SELECT_AREA, &DreditorView::OnToolsSelectArea)
ON_COMMAND(ID_TOOLS_SELECT, &DreditorView::OnToolsSelect)
ON_COMMAND(ID_PAINT, &DreditorView::OnPaint)
ON_UPDATE_COMMAND_UI(ID_PAINT, &DreditorView::OnUpdatePaint)
ON_UPDATE_COMMAND_UI(ID_TOOLS_SELECT, &DreditorView::OnUpdateToolsSelect)
ON_UPDATE_COMMAND_UI(ID_TOOLS_SELECT_AREA, &DreditorView::OnUpdateToolsSelectArea)
ON_WM_ERASEBKGND()
ON_COMMAND(ID_TOOLS_RAISE_VERTEX, &DreditorView::OnToolsRaiseVertex)
ON_COMMAND(ID_TOOLS_LOWER_VERTEX, &DreditorView::OnToolsLowerVertex)
ON_UPDATE_COMMAND_UI(ID_TOOLS_LOWER_VERTEX, &DreditorView::OnUpdateToolsLowerVertex)
ON_UPDATE_COMMAND_UI(ID_TOOLS_RAISE_VERTEX, &DreditorView::OnUpdateToolsRaiseVertex)
ON_COMMAND(ID_LOWER, &DreditorView::OnLower)
ON_UPDATE_COMMAND_UI(ID_LOWER, &DreditorView::OnUpdateLower)
ON_COMMAND(ID_RAISE, &DreditorView::OnRaise)
ON_UPDATE_COMMAND_UI(ID_RAISE, &DreditorView::OnUpdateRaise)
ON_COMMAND(ID_TOOLS_ERASE, &DreditorView::OnToolsErase)
ON_UPDATE_COMMAND_UI(ID_TOOLS_ERASE, &DreditorView::OnUpdateToolsErase)
ON_COMMAND(ID_ZOOM_OUT, &DreditorView::OnZoomOut)
ON_UPDATE_COMMAND_UI(ID_ZOOM_OUT, &DreditorView::OnUpdateZoomOut)
ON_COMMAND(ID_ZOOM_IN, &DreditorView::OnZoomIn)
ON_UPDATE_COMMAND_UI(ID_ZOOM_IN, &DreditorView::OnUpdateZoomIn)
ON_WM_DESTROY()
ON_COMMAND(ID_SELECT_ALL, &DreditorView::OnSelectAll)
ON_UPDATE_COMMAND_UI(ID_SELECT_ALL, &DreditorView::OnUpdateSelectAll)
ON_WM_RBUTTONUP()
ON_COMMAND(ID_TOOLS_MAKE_HILLY, &DreditorView::OnToolsMakeHilly)
ON_UPDATE_COMMAND_UI(ID_TOOLS_MAKE_HILLY, &DreditorView::OnUpdateToolsMakeHilly)
ON_COMMAND(ID_PREVIOUS_FRAME, &DreditorView::OnPreviousFrame)
ON_COMMAND(ID_NEXT_FRAME, &DreditorView::OnNextFrame)
ON_WM_TIMER()
ON_COMMAND(ID_TOOLS_FLATTEN, &DreditorView::OnToolsFlatten)
ON_COMMAND(ID_KEY_ESCAPE, &DreditorView::OnKeyEscape)
ON_WM_HSCROLL()
ON_WM_VSCROLL()
ON_COMMAND(ID_TOGGLE_CELLS, &DreditorView::OnToggleCells)
ON_UPDATE_COMMAND_UI(ID_TOGGLE_CELLS, &DreditorView::OnUpdateToggleCells)
ON_COMMAND(ID_VIEW_ZOOM_IN, &DreditorView::OnViewZoomIn)
ON_COMMAND(ID_VIEW_ZOOM_OUT, &DreditorView::OnViewZoomOut)
ON_COMMAND(ID_NO_TOOL, &DreditorView::OnNoTool)
ON_COMMAND(ID_MISSION_DROP_ZONES_TEAM_1, &DreditorView::OnMissionDropZonesTeam1)
ON_UPDATE_COMMAND_UI(ID_MISSION_DROP_ZONES_TEAM_1, &DreditorView::OnUpdateMissionDropZonesTeam1)
ON_WM_KEYDOWN()
ON_COMMAND(ID_KEY_F10, &DreditorView::OnKeyF10)
ON_COMMAND(ID_VIEW_TACTICAL_MAP, &DreditorView::OnViewTacticalMap)
ON_UPDATE_COMMAND_UI(ID_VIEW_TACTICAL_MAP, &DreditorView::OnUpdateViewTacticalMap)
ON_COMMAND(ID_TOOLS_LINK_BUILDINGS, &DreditorView::OnToolsLinkBuildings)
ON_COMMAND(ID_TOOLS_UNLINK_BUILDINGS, &DreditorView::OnToolsUnlinkBuildings)
ON_UPDATE_COMMAND_UI(ID_TOOLS_LINK_BUILDINGS, &DreditorView::OnUpdateToolsLinkBuildings)
ON_WM_LBUTTONDBLCLK()
ON_COMMAND(ID_KEY_ENTER, &DreditorView::OnKeyEnter)
ON_COMMAND(ID_COUNT_BUILDINGS, &DreditorView::OnCountBuildings)
ON_COMMAND(ID_SAVE_MAP_IMAGE, &DreditorView::OnSaveMapImage)
ON_COMMAND(ID_ALLIED, &DreditorView::OnAllied)
ON_UPDATE_COMMAND_UI(ID_ALLIED, &DreditorView::OnUpdateAllied)
ON_COMMAND(ID_ENEMY, &DreditorView::OnEnemy)
ON_UPDATE_COMMAND_UI(ID_ENEMY, &DreditorView::OnUpdateEnemy)
ON_COMMAND(ID_NEUTRAL, &DreditorView::OnNeutral)
ON_UPDATE_COMMAND_UI(ID_NEUTRAL, &DreditorView::OnUpdateNeutral)
ON_COMMAND(ID_UNIT_SETTINGS, &DreditorView::OnUnitSettings)
ON_COMMAND(ID_CONTEXT_HELP, &DreditorView::OnContextHelp)
ON_COMMAND(ID_HELP, &DreditorView::OnHelp)
ON_WM_SETCURSOR()
ON_UPDATE_COMMAND_UI(ID_TOOLS_UNLINK_BUILDINGS, &DreditorView::OnUpdateToolsLinkBuildings)
ON_UPDATE_COMMAND_UI(ID_TOOLS_FLATTEN, &DreditorView::OnUpdateToolsFlatten)
ON_UPDATE_COMMAND_UI(ID_VIEW_ZOOM_IN, &DreditorView::OnUpdateZoomIn)
ON_UPDATE_COMMAND_UI(ID_VIEW_ZOOM_OUT, &DreditorView::OnUpdateZoomOut)
ON_COMMAND(ID_FILE_PRINT, &CView::OnFilePrint)
ON_COMMAND(ID_FILE_PRINT_DIRECT, &CView::OnFilePrint)
ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CView::OnFilePrintPreview)
ON_COMMAND_RANGE(TERRAIN_COMMANDS, TERRAIN_COMMANDS_END, &DreditorView::OnTerrainCommand)
ON_UPDATE_COMMAND_UI_RANGE(OBJECT_COMMANDS, OBJECT_COMMANDS_LAST, &DreditorView::OnUpdateFeatureButton)
ON_UPDATE_COMMAND_UI_RANGE(TERRAIN_COMMANDS, TERRAIN_COMMANDS_END, &DreditorView::OnUpdatePaletteCommand)
ON_UPDATE_COMMAND_UI_RANGE(MOVER_COMMANDS, MOVER_COMMANDS_LAST, &DreditorView::OnUpdateFeatureButton)
ON_COMMAND_RANGE(OVERLAY_COMMANDS, OVERLAY_COMMANDS_LAST, &DreditorView::OnOverlayCommand)
ON_UPDATE_COMMAND_UI_RANGE(OVERLAY_COMMANDS, OVERLAY_COMMANDS_LAST, &DreditorView::OnUpdatePaletteCommand)
ON_MESSAGE(WM_EDITOR_SELECT_OBJECT, &DreditorView::OnSelectObject)
ON_MESSAGE(WM_EDITOR_SELECT_MOVER, &DreditorView::OnSelectMover)
ON_MESSAGE(WM_EDITOR_CENTRE_ON_TACTICAL, &DreditorView::OnCentreOnTacticalPoint)
ON_MESSAGE(WM_EDITOR_SCROLL_VIEW, &DreditorView::OnScrollView)
ON_COMMAND(ID_TOOLS_DAMAGE, &DreditorView::OnToolsDamage)
ON_UPDATE_COMMAND_UI(ID_TOOLS_DAMAGE, &DreditorView::OnUpdateToolsDamage)
ON_COMMAND(ID_TOOLS_REPAIR, &DreditorView::OnToolsRepair)
ON_UPDATE_COMMAND_UI(ID_TOOLS_REPAIR, &DreditorView::OnUpdateToolsDamage)
ON_COMMAND_RANGE(OVERLAY_LAYOUT_COMMANDS, OVERLAY_LAYOUT_COMMANDS_LAST, &DreditorView::OnOverlayLayoutCommand)
ON_UPDATE_COMMAND_UI_RANGE(OVERLAY_LAYOUT_COMMANDS, OVERLAY_LAYOUT_COMMANDS_LAST,
                           &DreditorView::OnUpdateOverlayLayoutCommand)
ON_COMMAND(ID_TOOLS_DRAG_VIEW, &DreditorView::OnToolsDragView)
ON_UPDATE_COMMAND_UI(ID_TOOLS_DRAG_VIEW, &DreditorView::OnUpdateToolsFlatten)
ON_COMMAND(ID_TOOLS_ZOOM_IN, &DreditorView::OnToolsZoomIn)
ON_UPDATE_COMMAND_UI(ID_TOOLS_ZOOM_IN, &DreditorView::OnUpdateToolsFlatten)
ON_COMMAND(ID_TOOLS_ZOOM_OUT, &DreditorView::OnToolsZoomOut)
ON_UPDATE_COMMAND_UI(ID_TOOLS_ZOOM_OUT, &DreditorView::OnUpdateToolsFlatten)
ON_COMMAND(ID_TOOLS_SPRAY_TREES, &DreditorView::OnToolsSprayTrees)
ON_UPDATE_COMMAND_UI(ID_TOOLS_SPRAY_TREES, &DreditorView::OnUpdateToolsFlatten)
ON_COMMAND(IDS_ASSIGN_A_SPECIFIC_HEIGHT, &DreditorView::OnIdsAssignASpecificHeight)
ON_UPDATE_COMMAND_UI(IDS_ASSIGN_A_SPECIFIC_HEIGHT, &DreditorView::OnUpdateIdsAssignASpecificHeight)
ON_COMMAND(ID_TOOLS_ROTATE, &DreditorView::OnToolsRotate)
ON_UPDATE_COMMAND_UI(ID_TOOLS_ROTATE, &DreditorView::OnUpdateToolsRotate)
ON_MESSAGE(WM_EDITOR_PICK, &DreditorView::OnPickOnMap)
ON_COMMAND(ID_ROTATE_BUILDING, &DreditorView::OnRotateBuilding)
ON_UPDATE_COMMAND_UI(ID_ROTATE_BUILDING, &DreditorView::OnUpdateRotateBuilding)
ON_COMMAND_RANGE(ID_MISSION_TIME_OF_DAY_DAY, ID_MISSION_TIME_OF_DAY_NIGHT, &DreditorView::OnMissionTimeOfDay)
ON_UPDATE_COMMAND_UI_RANGE(ID_MISSION_TIME_OF_DAY_DAY, ID_MISSION_TIME_OF_DAY_NIGHT,
                           &DreditorView::OnUpdateMissionTimeOfDay)
END_MESSAGE_MAP()

void DreditorView::OnLButtonDown(UINT nFlags, CPoint point)
{
    EditorMap* editorMap = map();
    const int32_t viewX = point.x + origin.x;
    const int32_t viewY = point.y + origin.y;
    int32_t mapX = 0;
    int32_t mapY = 0;
    int32_t mapZ = 0;
    editorMap->viewToMap(viewX, viewY, &mapX, &mapY, &mapZ);
    // The click's map position in the game's units, from which the Delta pane measures.
    const X87Extended scale = X87Extended(GAME_TILE_SIZE) * X87Extended(MEASURE_SCALE) / X87Extended(tileWorldWidth);
    const auto gameX = static_cast<int32_t>((X87Extended(mapX) * scale).truncate());
    const auto gameY = static_cast<int32_t>((X87Extended(mapY) * scale).truncate());

    if (statusBar != nullptr)
    {
        const auto deltaX = static_cast<float>(gameX - measurePoint.x);
        const auto deltaY = static_cast<float>(gameY - measurePoint.y);
        const X87Extended squares =
            X87Extended(deltaX) * X87Extended(deltaX) + X87Extended(deltaY) * X87Extended(deltaY);
        CString text;
        text.Format(statusFormat(IDS_INDICATOR_DELTA_FORMAT), std::sqrt(squares.toDouble()));
        statusBar->SetPaneText(PANE_DELTA, text);
        measurePoint = {gameX, gameY};
    }

    if (tool != nullptr && (pausingDialog == nullptr || !::IsWindowVisible(pausingDialog->m_hWnd)))
    {
        SetCapture();
        tool->begin(*editorMap);
        dragging = true;
        canApply = tool->canApply(*editorMap, viewX, viewY, nFlags);

        if (canApply)
        {
            tool->apply(*editorMap, viewX, viewY, nFlags);
        }

        InvalidateRect(nullptr, TRUE);
    }

    lastMouse = point;
    refreshEnabling = true;
    Default();
}

void DreditorView::OnLButtonUp([[maybe_unused]] UINT nFlags, [[maybe_unused]] CPoint point)
{
    if (dragging)
    {
        if (EditorAction* action = tool->end(); action != nullptr)
        {
            undoList->push(action);
        }

        dragging = false;
        ReleaseCapture();
        ::InvalidateRect(tacticalMap->m_hWnd, nullptr, FALSE);
        InvalidateRect(nullptr, TRUE);
    }

    Default();
}

void DreditorView::OnMouseMove(UINT nFlags, CPoint point)
{
    _TrackMouseEvent(&trackMouse);
    cursorInView = true;

    if (std::abs(point.x - lastMouse.x) < MOUSE_SLOP && std::abs(point.y - lastMouse.y) < MOUSE_SLOP)
    {
        Default();
        return;
    }

    EditorMap* editorMap = map();
    const int32_t viewX = point.x + origin.x;
    const int32_t viewY = point.y + origin.y;

    if (statusBar != nullptr)
    {
        using X = X87Extended;
        int32_t mapX = 0;
        int32_t mapY = 0;
        int32_t mapZ = 0;
        editorMap->viewToMap(viewX, viewY, &mapX, &mapY, &mapZ);
        const X cellSize = X(tileWorldWidth) / X(MAP_CELLS_PER_TILE);
        const auto cellCol = static_cast<int32_t>((X(mapX) / cellSize).truncate());
        const auto cellRow = static_cast<int32_t>((X(mapY) / cellSize).truncate());
        // The position in the game's units: x and y at its tile size, z at its elevation scale.
        const auto gameX =
            static_cast<int32_t>((X(GAME_TILE_SIZE) / X(tileWorldWidth) * X(static_cast<float>(mapX))).truncate());
        const auto gameY = static_cast<int32_t>(
            (X(static_cast<float>(GAME_TILE_SIZE)) / X(tileWorldHeight) * X(static_cast<float>(mapY))).truncate());
        const auto gameZ = static_cast<int32_t>((X(GAME_ELEVATION_SCALE) / X(elevationScale) * X(mapZ)).truncate());
        CString text;
        // In metres from the map's centre (y up).
        const int32_t halfAcross = (editorMap->maxRow + 1) >> 1;
        text.Format(
            statusFormat(IDS_INDICATOR_X_FORMAT),
            (X(gameX) * X(METRES_PER_TILE) / X(GAME_TILE_SIZE) - X(halfAcross) * X(METRES_PER_TILE)).toDouble());
        statusBar->SetPaneText(PANE_X, text);
        const int32_t halfDown = (editorMap->maxCol + 1) >> 1;
        text.Format(statusFormat(IDS_INDICATOR_Y_FORMAT),
                    -(X(gameY) * X(METRES_PER_TILE) / X(GAME_TILE_SIZE) - X(halfDown) * X(METRES_PER_TILE)).toDouble());
        statusBar->SetPaneText(PANE_Y, text);
        text.Format(statusFormat(IDS_INDICATOR_Z_FORMAT), (X(gameZ) * X(METRES_PER_ELEVATION)).toDouble());
        statusBar->SetPaneText(PANE_Z, text);
        CString area;
        area.Format("Area = %ld", editorMap->areaAt(cellCol, cellRow));
        statusBar->SetPaneText(PANE_AREA, area);
        const EditorObject* object = editorMap->objectCoveringCell(cellCol, cellRow);

        if (object != nullptr)
        {
            const int32_t blocksPerRow = (editorMap->maxRow + 1) / MAP_BLOCK_TILES;
            const int32_t tileCol = object->cellCol / MAP_CELLS_PER_TILE;
            const int32_t tileRow = object->cellRow / MAP_CELLS_PER_TILE;
            const int32_t blockCol = tileCol / MAP_BLOCK_TILES;
            const int32_t blockRow = tileRow / MAP_BLOCK_TILES;
            text.Format(statusFormat(IDS_INDICATOR_BLOCK_FORMAT), blockRow * blocksPerRow + blockCol);
            statusBar->SetPaneText(PANE_BLOCK, text);
            text.Format(statusFormat(IDS_INDICATOR_VERTEX_FORMAT),
                        MAP_BLOCK_TILES * (tileRow - MAP_BLOCK_TILES * blockRow) +
                            (tileCol - MAP_BLOCK_TILES * blockCol));
            statusBar->SetPaneText(PANE_VERTEX, text);
        }
        else
        {
            statusBar->SetPaneText(PANE_BLOCK, "");
            statusBar->SetPaneText(PANE_VERTEX, "");
        }

        // Port-only: the sight in the mission's time of day (the original's is the day's).
        text.Format(statusFormat(IDS_INDICATOR_RANGE_FORMAT),
                    MissionVisualRange(gameZ / elevationScale, map()->timeOfDay));
        statusBar->SetPaneText(PANE_RANGE, text);
    }

    if (!dragging)
    {
        if (tool != nullptr)
        {
            canApply = tool->canApply(*editorMap, viewX, viewY, nFlags);
        }

        RedrawWindow(nullptr, nullptr, REDRAW_NOW);
    }
    else if (!selectMode)
    {
        if (tool != nullptr)
        {
            canApply = tool->canApply(*editorMap, viewX, viewY, nFlags);

            if (canApply)
            {
                tool->apply(*editorMap, viewX, viewY, nFlags);
            }
        }

        RedrawWindow(nullptr, nullptr, REDRAW_NOW);
    }
    else
    {
        // The select tool applies only when no later movement is waiting.
        POINT cursor;

        if (GetCursorPos(&cursor))
        {
            ::ScreenToClient(m_hWnd, &cursor);

            if (cursor.x == point.x && cursor.y == point.y)
            {
                if (tool != nullptr)
                {
                    canApply = tool->canApply(*editorMap, viewX, viewY, nFlags);

                    if (canApply)
                    {
                        tool->apply(*editorMap, viewX, viewY, nFlags);
                    }
                }

                RedrawWindow(nullptr, nullptr, REDRAW_NOW);
            }

            refreshEnabling = true;
        }
    }

    lastMouse = point;
    Default();
}

void DreditorView::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
    if ((lpwndpos->flags & SWP_NOSIZE) == 0)
    {
        resize(CSize(lpwndpos->cx, lpwndpos->cy), viewZoom);
    }

    CView::OnWindowPosChanged(lpwndpos);
}

void DreditorView::OnUndo()
{
    if (undoList->canUndo())
    {
        playClickSound();

        if (!undoList->undo())
        {
            CString message;
            message.LoadString(UNDO_FAILED);
            CString caption;
            caption.LoadString(MESSAGE_CAPTION);
            MessageBox(message, caption, MB_OK);
        }
    }

    RedrawWindow(nullptr, nullptr, REDRAW_NOW);
    ::InvalidateRect(tacticalMap->m_hWnd, nullptr, FALSE);
}

void DreditorView::OnUpdateUndo(CCmdUI* pCmdUI)
{
    if (!undoList->canUndo())
    {
        pCmdUI->Enable(FALSE);
        return;
    }

    if (pausingDialog != nullptr && ::IsWindowVisible(pausingDialog->m_hWnd))
    {
        pCmdUI->Enable(FALSE);
        return;
    }

    if (const std::string* name = undoList->undoName(); name != nullptr)
    {
        CString format;
        format.LoadString(ID_UNDO_S);
        CString item;
        item.Format(format, name->c_str());
        pCmdUI->SetText(item);
        pCmdUI->Enable(TRUE);
    }
}

void DreditorView::OnRedo()
{
    if (undoList->canRedo())
    {
        playClickSound();

        if (!undoList->redo())
        {
            CString message;
            message.LoadString(REDO_FAILED);
            CString caption;
            caption.LoadString(MESSAGE_CAPTION);
            MessageBox(message, caption, MB_OK);
        }
    }

    RedrawWindow(nullptr, nullptr, REDRAW_NOW);
    ::InvalidateRect(tacticalMap->m_hWnd, nullptr, FALSE);
}

void DreditorView::OnUpdateRedo(CCmdUI* pCmdUI)
{
    if (!undoList->canRedo())
    {
        pCmdUI->Enable(FALSE);
        return;
    }

    // Original behaviour: unlike Undo, Redo stays on while the pausing dialog is shown.
    if (const std::string* name = undoList->redoName(); name != nullptr)
    {
        CString format;
        format.LoadString(ID_REDO_S);
        CString item;
        item.Format(format, name->c_str());
        pCmdUI->SetText(item);
        pCmdUI->Enable(TRUE);
    }
}

void DreditorView::OnViewGridlines()
{
    gridlines = !gridlines;
    playClickSound();
    RedrawWindow(nullptr, nullptr, REDRAW_NOW);
}

void DreditorView::OnUpdateViewGridlines(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(gridlines);
}

void DreditorView::OnToolsSelectArea()
{
    playClickSound();

    if (toolCommand == ID_TOOLS_SELECT_AREA || dragging)
    {
        // A second press goes back to the plain select tool.
        selectTool(false, ID_TOOLS_SELECT);
    }
    else
    {
        delete tool;
        tool = new SelectTool(true);
        selectMode = true;
        toolCommand = ID_TOOLS_SELECT_AREA;
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnToolsSelect()
{
    playClickSound();

    if (toolCommand == ID_TOOLS_SELECT || dragging)
    {
        selectTool(false, ID_TOOLS_SELECT);
    }
    else
    {
        delete tool;
        tool = new SelectTool(false);
        selectMode = true;
        toolCommand = ID_TOOLS_SELECT;
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnPaint()
{
    selectTool(false, ID_TOOLS_SELECT);
}

void DreditorView::OnUpdatePaint(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(!selectMode);
}

void DreditorView::OnUpdateToolsSelect(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(toolCommand == ID_TOOLS_SELECT ? 1 : 0);
}

void DreditorView::OnUpdateToolsSelectArea(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(toolCommand == ID_TOOLS_SELECT_AREA ? 1 : 0);
}

BOOL DreditorView::OnEraseBkgnd([[maybe_unused]] CDC* pDC)
{
    // The bitmap covers the window.
    return TRUE;
}

void DreditorView::OnToolsRaiseVertex()
{
    vertexCommand(ID_TOOLS_RAISE_VERTEX, RAISE_STEP);
}

void DreditorView::OnToolsLowerVertex()
{
    vertexCommand(ID_TOOLS_LOWER_VERTEX, LOWER_STEP);
}

void DreditorView::OnUpdateToolsLowerVertex(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(toolCommand == ID_TOOLS_LOWER_VERTEX ? 1 : 0);
}

void DreditorView::OnUpdateToolsRaiseVertex(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(toolCommand == ID_TOOLS_RAISE_VERTEX ? 1 : 0);
}

void DreditorView::OnLower()
{
    raiseOrLower(LOWER_STEP);
}

void DreditorView::OnUpdateLower(CCmdUI* pCmdUI)
{
    // The original asks a vertex tool whether the selection can be lowered (0x004158c0), which always says yes; with
    // no vertex selected the item is left as it is.
    if (!map()->selectedVertices.empty())
    {
        pCmdUI->Enable(TRUE);
    }
}

void DreditorView::OnRaise()
{
    raiseOrLower(RAISE_STEP);
}

void DreditorView::OnUpdateRaise(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(TRUE);
}

void DreditorView::OnToolsErase()
{
    playClickSound();

    if (toolCommand == ID_TOOLS_ERASE)
    {
        selectTool(false, ID_TOOLS_SELECT);
        return;
    }

    if (selectMode && anythingSelected() && !toolsPaused())
    {
        EraseTool eraser(IDB_ERASE_CURSOR, ERASE_HOT_X, ERASE_HOT_Y, ANY_OVERLAY);
        applyToSelection(eraser);
        InvalidateRect(nullptr, TRUE);
    }
    else if (toolCommand == ORDERS_PATH_COMMAND && tool != nullptr)
    {
        static_cast<MapPickTool*>(tool)->removeLastPoint(*map());
    }
    else if (!dragging)
    {
        delete tool;
        tool = new EraseTool(IDB_ERASE_CURSOR, ERASE_HOT_X, ERASE_HOT_Y, ANY_OVERLAY);
        toolCommand = ID_TOOLS_ERASE;
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnUpdateToolsErase(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(TRUE);
    pCmdUI->SetCheck(toolCommand == ID_TOOLS_ERASE);
}

void DreditorView::OnZoomOut()
{
    // The zoom tool has set the tile size for the new zoom already; the view keeps its centre and follows.
    if (static_cast<float>(SMALLEST_ZOOM) < viewZoom)
    {
        const POINT position = scrollPosition();
        CRect client;
        GetClientRect(&client);
        scrollTo({(position.x >> 1) - (client.Width() >> 2), (position.y >> 1) - (client.Height() >> 2)});
        POINT centre = {client.Width() >> 1, client.Height() >> 1};
        ClientToScreen(&centre);
        SetCursorPos(centre.x, centre.y);
        viewZoom = viewZoom * 0.5f;
    }
}

void DreditorView::OnUpdateZoomOut(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(viewZoom < static_cast<float>(SMALLEST_ZOOM) ? FALSE : TRUE);
}

void DreditorView::OnZoomIn()
{
    if (viewZoom < static_cast<float>(FULL_ZOOM))
    {
        viewZoom = viewZoom + viewZoom;
        const POINT position = scrollPosition();
        CRect client;
        GetClientRect(&client);
        scrollTo({position.x * 2 + (client.Width() >> 1), position.y * 2 + (client.Height() >> 1)});
        POINT centre = {client.Width() >> 1, client.Height() >> 1};
        ClientToScreen(&centre);
        SetCursorPos(centre.x, centre.y);
    }
}

void DreditorView::OnUpdateZoomIn(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(static_cast<float>(LARGEST_ZOOM) <= viewZoom ? FALSE : TRUE);
    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnDestroy()
{
    delete tool;
    tool = nullptr;
    CView::OnDestroy();
}

void DreditorView::OnSelectAll()
{
    if (!selectMode)
    {
        delete tool;
        tool = new SelectTool(true);
    }

    // Original behaviour: in select mode the tool is taken as a select tool, whichever it is.
    auto* select = static_cast<SelectTool*>(tool);
    select->begin(*map());
    select->selectAll();
    select->end();
    selectMode = true;
    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnUpdateSelectAll(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(TRUE);
}

void DreditorView::OnRButtonUp(UINT nFlags, CPoint point)
{
    int32_t col = 0;
    int32_t row = 0;

    if (map()->viewToCell(origin.x + point.x, origin.y + point.y, &col, &row))
    {
        clickedObject = map()->objectAt(col, row);
        CMenu menu;

        // EB-089: the menu is shown at the click's window position taken as a screen position.
        if (CMenu* popup = popupMenu(clickedObject, menu); popup != nullptr)
        {
            popup->TrackPopupMenu(nFlags, point.x, point.y, this, nullptr);
        }
    }

    Default();
}

CMenu* DreditorView::popupMenu([[maybe_unused]] EditorObject* object, [[maybe_unused]] CMenu& menu)
{
    return nullptr;
}

CMenu* DreditorView::loadPopupMenu(EditorObject* object, CMenu& menu, UINT missionMenu, UINT buildingMenu)
{
    const auto load = [&menu](UINT id)
    {
        menu.Attach(LoadMenuA(AfxGetResourceHandle(), MAKEINTRESOURCEA(id)));
        return CMenu::FromHandle(::GetSubMenu(menu.m_hMenu, 0));
    };

    if (object == nullptr)
    {
        return load(missionMenu);
    }

    if (isMoverClass(objectIdClass(object->id)))
    {
        return load(IDR_UNIT_POPUP);
    }

    CMenu* popup = load(buildingMenu);

    switch (object->side)
    {
        case NEUTRAL_SIDE:
        {
            CheckMenuItem(popup->m_hMenu, ID_NEUTRAL, MF_CHECKED);
            break;
        }

        case ENEMY_SIDE:
        {
            CheckMenuItem(popup->m_hMenu, ID_ENEMY, MF_CHECKED);
            break;
        }

        case PLAYER_SIDE:
        {
            CheckMenuItem(popup->m_hMenu, ID_ALLIED, MF_CHECKED);
            break;
        }

        default:
        {
            break;
        }
    }

    const bool turns = object->hasOtherFrame();

    if (!turns)
    {
        RemoveMenu(popup->m_hMenu, ID_ROTATE_BUILDING, MF_BYCOMMAND);
    }

    EditorMap& editorMap = *map();

    // The side can't change for an object an objective refers to, one whose class takes no links, or a structure
    // linked to a control structure.
    if (editorMap.objectiveFor(*object) == nullptr && !ObjectLibrary::isUnlinkedClass(objectIdClass(object->id)))
    {
        if (editorMap.controlLinkWith(*object) == nullptr || editorMap.controlLinkFor(*object) != nullptr)
        {
            return popup;
        }
    }

    // The sides, and the separator below them when Rotate is gone.
    if (!turns)
    {
        RemoveMenu(popup->m_hMenu, 3, MF_BYPOSITION);
    }

    RemoveMenu(popup->m_hMenu, 2, MF_BYPOSITION);
    RemoveMenu(popup->m_hMenu, 1, MF_BYPOSITION);
    RemoveMenu(popup->m_hMenu, 0, MF_BYPOSITION);
    return popup;
}

void DreditorView::setClickedSide(int32_t side)
{
    if (clickedObject == nullptr)
    {
        return;
    }

    playClickSound();
    EditorMap& editorMap = *map();

    if (!isMoverClass(objectIdClass(clickedObject->id)))
    {
        // A control structure's linked structures go to its side.
        if (const ControlLink* link = editorMap.controlLinkFor(*clickedObject); link != nullptr)
        {
            if (const int32_t count = link->linkedCount(); count != 0)
            {
                std::vector<POINT> cells(count);
                link->getLinkedCells(cells.data(), count);

                for (const POINT& cell : cells)
                {
                    // EB-090: the original writes the side through a null object when no object is on the cell.
                    if (EditorObject* linked = editorMap.objectAt(cell.x, cell.y); linked != nullptr)
                    {
                        linked->side = side;
                    }
                }
            }
        }
    }

    clickedObject->side = side;
}

void DreditorView::OnToolsMakeHilly()
{
    hillCommand(ID_TOOLS_MAKE_HILLY, false);
}

void DreditorView::hillCommand(int32_t command, bool flatten)
{
    playClickSound();

    if (toolCommand == command)
    {
        selectTool(false, ID_TOOLS_SELECT);
        return;
    }

    if (!selectMode || !map()->tilesSelected)
    {
        if (!dragging)
        {
            delete tool;
            tool = new HillTool(flatten);
            toolCommand = command;
            selectMode = false;
        }
    }
    else
    {
        VertexTool mover(RAISE_STEP, TEXTURE_NONE, true, 0);
        CWaitCursor wait;
        EditorMap& editorMap = *map();
        mover.begin(editorMap);
        bool changed = true;

        if (flatten)
        {
            // The selected vertices go to their average elevation.
            int32_t sum = 0;
            int32_t count = 0;

            for (const POINT& vertex : editorMap.selectedVertices)
            {
                sum += editorMap.vertexElevation(vertex.x, vertex.y);
                count++;
            }

            changed = count != 0;

            if (changed)
            {
                mover.setSelectedElevation(editorMap, sum / count);
            }
        }
        else
        {
            mover.makeHilly(editorMap);
        }

        if (changed)
        {
            if (EditorAction* action = mover.end(); action != nullptr)
            {
                undoList->push(action);
            }

            InvalidateRect(nullptr, TRUE);
            ::InvalidateRect(tacticalMap->m_hWnd, nullptr, TRUE);
        }
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnUpdateToolsMakeHilly(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(toolCommand == static_cast<int32_t>(pCmdUI->m_nID) ? 1 : 0);
    pCmdUI->Enable(TRUE);
}

void DreditorView::OnPreviousFrame()
{
    frameCommand(-FRAME_STEP, LAST_OBJECT_COMMAND_LEFT);
}

void DreditorView::OnNextFrame()
{
    frameCommand(FRAME_STEP, LAST_OBJECT_COMMAND_RIGHT);
}

void DreditorView::frameCommand(int32_t delta, int32_t lastObjectCommand)
{
    const bool objectTool = toolCommand >= OBJECT_COMMANDS && toolCommand <= lastObjectCommand;
    const bool moverTool = toolCommand >= MOVER_COMMANDS && toolCommand <= LAST_MOVER_COMMAND;

    // Original behaviour: Right leaves out the last building command that Left takes.
    if ((!objectTool && !moverTool) || tool == nullptr || !static_cast<PlaceObjectTool*>(tool)->stepFrame(delta) ||
        toolsPaused())
    {
        playWrongSound();
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnTimer(UINT_PTR nIDEvent)
{
    // Original behaviour: nothing starts the timer, so the autosave never runs.
    if (nIDEvent == timer)
    {
        static_cast<DreditorDoc*>(m_pDocument)->autoSave();
    }

    Default();
}

void DreditorView::OnToolsFlatten()
{
    hillCommand(ID_TOOLS_FLATTEN, true);
}

void DreditorView::OnKeyEscape()
{
    playClickSound();

    if (toolCommand == ORDERS_PATH_COMMAND && tool != nullptr)
    {
        static_cast<MapPickTool*>(tool)->finish(*map());
    }

    if (!dragging)
    {
        OnPaint();
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    POINT delta = {0, 0};
    bool step = true;

    switch (nSBCode)
    {
        case SB_LINELEFT:
        {
            delta.x = -SCROLL_STEP;
            break;
        }

        case SB_LINERIGHT:
        {
            delta.x = SCROLL_STEP;
            break;
        }

        case SB_PAGELEFT:
        {
            delta.x = -static_cast<int32_t>(getScrollInfo(*this, SB_HORZ).nPage);
            break;
        }

        case SB_PAGERIGHT:
        {
            delta.x = static_cast<int32_t>(getScrollInfo(*this, SB_HORZ).nPage);
            break;
        }

        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
        {
            scrollTo({static_cast<int32_t>(nPos), getScrollInfo(*this, SB_VERT).nPos});
            step = false;
            break;
        }

        default:
        {
            step = false;
            break;
        }
    }

    if (step)
    {
        OnScrollView(0, reinterpret_cast<LPARAM>(&delta));
    }

    CView::OnHScroll(nSBCode, nPos, pScrollBar);
}

void DreditorView::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    POINT delta = {0, 0};
    bool step = true;

    switch (nSBCode)
    {
        case SB_LINEUP:
        {
            delta.y = -SCROLL_STEP;
            break;
        }

        case SB_LINEDOWN:
        {
            delta.y = SCROLL_STEP;
            break;
        }

        case SB_PAGEUP:
        {
            delta.y = -static_cast<int32_t>(getScrollInfo(*this, SB_VERT).nPage);
            break;
        }

        case SB_PAGEDOWN:
        {
            delta.y = static_cast<int32_t>(getScrollInfo(*this, SB_VERT).nPage);
            break;
        }

        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
        {
            scrollTo({getScrollInfo(*this, SB_HORZ).nPos, static_cast<int32_t>(nPos)});
            step = false;
            break;
        }

        default:
        {
            step = false;
            break;
        }
    }

    if (step)
    {
        OnScrollView(0, reinterpret_cast<LPARAM>(&delta));
    }

    CView::OnVScroll(nSBCode, nPos, pScrollBar);
}

void DreditorView::OnToggleCells()
{
    playClickSound();

    // Turning the cells on saves the map to the working files and builds the pathfinding maps the display reads.
    if (!showCells)
    {
        CWaitCursor wait;
        static_cast<DreditorDoc*>(m_pDocument)->buildPathMaps();
    }

    showCells = !showCells;
    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnUpdateToggleCells(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(showCells);
}

void DreditorView::OnViewZoomIn()
{
    zoomCommand(true);
}

void DreditorView::OnViewZoomOut()
{
    zoomCommand(false);
}

void DreditorView::OnNoTool()
{
    delete tool;
    tool = nullptr;
    selectMode = false;
    toolCommand = NO_TOOL_COMMAND;
    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnMissionDropZonesTeam1()
{
    playClickSound();

    // EB-085: the original deletes the tool during a drag too, and makes none.
    if (!dragging)
    {
        delete tool;
        tool = new PlaceObjectTool(DROP_ZONE_ID, PLAYER_SIDE, false);
        toolCommand = ID_MISSION_DROP_ZONES_TEAM_1;
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnUpdateMissionDropZonesTeam1(CCmdUI* pCmdUI)
{
    int32_t playerZones = 0;

    for (const EditorObject* zone : map()->dropZones)
    {
        if (zone->side == PLAYER_SIDE)
        {
            playerZones++;
        }
    }

    pCmdUI->Enable(playerZones > MAX_PLAYER_DROP_ZONES ? FALSE : TRUE);
}

void DreditorView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    // Original behaviour: Shift alone isn't passed on.
    if (nChar == VK_SHIFT)
    {
        return;
    }

    if (nChar == VK_TAB)
    {
        // Ctrl+Tab turns the Map Features palette's page. EB-091: the original's test for Shift (the page before) is
        // always false, so Ctrl+Shift+Tab turns forward too.
        if (GetKeyState(VK_CONTROL) < 0 && mapFeaturesBar != nullptr)
        {
            mapFeaturesBar->cyclePage(1);
            Default();
            return;
        }
    }
    else if ((nChar == VK_F7 || nChar == VK_F8) && mapFeaturesBar != nullptr)
    {
        gammaIndex += nChar == VK_F7 ? -1 : 1;

        if (gammaIndex < 0)
        {
            gammaIndex = GAMMA_STEPS - 1;
        }

        if (gammaIndex > GAMMA_STEPS - 1)
        {
            gammaIndex = 0;
        }

        editorPalette->setGamma(gammaIndex < static_cast<int32_t>(std::size(GAMMAS)) ? GAMMAS[gammaIndex]
                                                                                     : GAMMA_PAST_TABLE);
        CRect client;
        GetClientRect(&client);
        resize(CSize(client.Width(), client.Height()), 1.0f);

        if (mapFeaturesBar != nullptr)
        {
            mapFeaturesBar->cyclePage(0);
        }

        RedrawWindow(nullptr, nullptr, REDRAW_NOW);
    }

    CView::OnKeyDown(nChar, nRepCnt, nFlags);
}

void DreditorView::OnKeyF10()
{
    playClickSound();
    OnKeyDown(VK_F10, 1, 0);
}

void DreditorView::OnViewTacticalMap()
{
    playClickSound();
    CRect window;
    tacticalMap->GetWindowRect(&window);
    CRect screen;
    ::GetWindowRect(::GetDesktopWindow(), &screen);
    screen.DeflateRect(SCREEN_MARGIN, SCREEN_MARGIN, SCREEN_MARGIN, SCREEN_MARGIN);

    // A window off the screen comes back to its middle.
    if (!screen.PtInRect(window.TopLeft()))
    {
        const int32_t x = (screen.left + screen.right) / 2;
        const int32_t y = (screen.bottom + screen.top) / 2;
        tacticalMap->MoveWindow(x, y, window.Width(), window.Height(), TRUE);
    }

    if ((tacticalMap->GetStyle() & WS_VISIBLE) != 0)
    {
        tacticalMap->ShowWindow(SW_HIDE);
        return;
    }

    tacticalMap->ShowWindow(SW_SHOW);
    ::InvalidateRect(tacticalMap->m_hWnd, nullptr, TRUE);
}

void DreditorView::OnUpdateViewTacticalMap(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck((tacticalMap->GetStyle() & WS_VISIBLE) != 0 ? 1 : 0);
}

void DreditorView::OnToolsLinkBuildings()
{
    linkCommand(ID_TOOLS_LINK_BUILDINGS, true);
}

void DreditorView::OnToolsUnlinkBuildings()
{
    linkCommand(ID_TOOLS_UNLINK_BUILDINGS, false);
}

void DreditorView::linkCommand(int32_t command, bool link)
{
    playClickSound();

    if (toolCommand == command)
    {
        selectTool(false, ID_TOOLS_SELECT);
        return;
    }

    // EB-085: the original deletes the tool during a drag too, and makes none.
    if (!dragging)
    {
        delete tool;
        tool = new LinkTool(*static_cast<DreditorDoc*>(m_pDocument), link);
        toolCommand = command;
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnUpdateToolsLinkBuildings(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(toolCommand == static_cast<int32_t>(pCmdUI->m_nID) ? 1 : 0);
}

void DreditorView::OnLButtonDblClk([[maybe_unused]] UINT nFlags, CPoint point)
{
    EditorMap& editorMap = *map();
    int32_t col = 0;
    int32_t row = 0;

    if (editorMap.viewToCell(origin.x + point.x, origin.y + point.y, &col, &row))
    {
        EditorObject* mover = editorMap.moverAt(col, row);

        if (mover == nullptr)
        {
            // EB-095: the search of the cells around goes on after a find, so only the right-hand column counts.
            for (int32_t dx = -1; dx < 2; dx++)
            {
                for (int32_t dy = -1; dy < 2; dy++)
                {
                    mover = editorMap.moverAt(col + dx, row + dy);

                    if (mover != nullptr)
                    {
                        break;
                    }
                }
            }
        }

        if (mover != nullptr)
        {
            openUnitSettings(static_cast<MoverObject&>(*mover));
        }
    }

    Default();
}

void DreditorView::openUnitSettings(MoverObject& mover)
{
    if (pausingDialog != nullptr)
    {
        pausingDialog->DestroyWindow();
        delete pausingDialog;
    }

    auto* dialog = new UnitSettingsDialog(*map(), mover, this);
    pausingDialog = dialog;
    dialog->Create(UnitSettingsDialog::IDD, this);
    dialog->CenterWindow();
    dialog->ShowWindow(SW_SHOW);
    pickingForUnit = true;
}

void DreditorView::OnKeyEnter()
{
    playClickSound();

    if (toolCommand == ORDERS_PATH_COMMAND && tool != nullptr)
    {
        static_cast<MapPickTool*>(tool)->finish(*map());
    }
}

void DreditorView::OnCountBuildings()
{
    playClickSound();
    CString message;
    message.Format(IDS_YOU_HAVE_LD_BUILDINGS, static_cast<long>(map()->objects.size()));
    CString caption;
    caption.LoadString(MESSAGE_CAPTION);
    MessageBox(message, caption, MB_OK);
}

void DreditorView::OnSaveMapImage()
{
    playClickSound();
    // The whole map at the current zoom, as a Targa.
    CFileDialog dialog(FALSE, nullptr, nullptr, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, nullptr, nullptr);
    dialog.GetOFN().lpstrDefExt = "tga";

    if (dialog.DoModal() != IDOK)
    {
        return;
    }

    CWaitCursor wait;
    int32_t left;
    int32_t right;
    int32_t top;
    int32_t bottom;
    mapExtent(&left, &right, &top, &bottom);
    const int32_t height = bottom - top;
    const int32_t width = right - left;
    std::vector<uint8_t> buffer;

    try
    {
        buffer.resize(static_cast<size_t>(height) * width);
    }
    catch (const std::bad_alloc&)
    {
        CString message;
        message.LoadString(IDS_NOT_ENOUGH_MEMORY_TRY);
        CString caption;
        caption.LoadString(MESSAGE_CAPTION);
        MessageBox(message, caption, MB_OK);
        return;
    }

    map()->saveImage(dialog.GetPathName(), {left, top}, buffer.data(), width, height, zoomLevel());
}

int32_t DreditorView::zoomLevel()
{
    if (static_cast<double>(viewZoom) > FULL_ZOOM)
    {
        return 0;
    }

    return MIDDLE_ZOOM < static_cast<double>(viewZoom) ? 1 : 2;
}

void DreditorView::OnAllied()
{
    setClickedSide(PLAYER_SIDE);
}

void DreditorView::OnUpdateAllied(CCmdUI* pCmdUI)
{
    if (clickedObject != nullptr && clickedObject->side == PLAYER_SIDE)
    {
        pCmdUI->SetCheck(1);
    }
}

void DreditorView::OnEnemy()
{
    setClickedSide(ENEMY_SIDE);
}

void DreditorView::OnUpdateEnemy(CCmdUI* pCmdUI)
{
    if (clickedObject != nullptr && clickedObject->side == ENEMY_SIDE)
    {
        pCmdUI->SetCheck(1);
    }
}

void DreditorView::OnNeutral()
{
    setClickedSide(NEUTRAL_SIDE);
}

void DreditorView::OnUpdateNeutral(CCmdUI* pCmdUI)
{
    if (clickedObject != nullptr && clickedObject->side == NEUTRAL_SIDE)
    {
        pCmdUI->SetCheck(1);
    }
}

void DreditorView::OnUnitSettings()
{
    if (clickedObject == nullptr)
    {
        return;
    }

    playClickSound();

    if (pausingDialog != nullptr)
    {
        // An open dialog goes, and the view goes back to the select tool.
        pausingDialog->DestroyWindow();
        delete pausingDialog;
        pausingDialog = nullptr;
        selectTool(false, ID_TOOLS_SELECT);
    }

    openUnitSettings(static_cast<MoverObject&>(*clickedObject));
}

void DreditorView::OnContextHelp()
{
    // The help of the current tool's kind.
    const int32_t command = toolCommand;
    DWORD context = HELP_UNITS;

    if (command >= TEXTURE_COMMANDS && command <= LAST_TEXTURE_COMMAND)
    {
        context = HELP_SELECT;
    }
    else if (command >= static_cast<int32_t>(OVERLAY_COMMANDS) && command < OVERLAY_COMMANDS_END)
    {
        context = HELP_OVERLAYS;
    }
    else if (command >= static_cast<int32_t>(OVERLAY_LAYOUT_COMMANDS) && command < OVERLAY_LAYOUT_COMMANDS_END)
    {
        context = HELP_OVERLAY_LAYOUTS;
    }
    else if (command >= OBJECT_COMMANDS && command < OBJECT_COMMANDS_END)
    {
        context = HELP_BUILDINGS;
    }
    else if (command < MOVER_COMMANDS || command > LAST_MOVER_HELP_COMMAND)
    {
        // The select tools tell by the object of the last right click.
        if (command != ID_TOOLS_SELECT_AREA && command != ID_TOOLS_SELECT)
        {
            context = HELP_TOOLS;
        }
        else if (clickedObject == nullptr)
        {
            context = HELP_SELECT;
        }
        else if (!isMoverClass(objectIdClass(clickedObject->id)))
        {
            context = HELP_BUILDINGS;
        }
    }

    AfxGetApp()->WinHelp(context, HELP_CONTEXT);
}

void DreditorView::OnHelp()
{
    WinHelp(HELP_VIEW, HELP_CONTEXT);
}

BOOL DreditorView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    // The tool's cursor is drawn into the map; the system's is hidden over it.
    if (nHitTest == HTCLIENT)
    {
        SetCursor(nullptr);
        return TRUE;
    }

    return static_cast<BOOL>(Default());
}

void DreditorView::OnUpdateToolsFlatten(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(toolCommand == static_cast<int32_t>(pCmdUI->m_nID) ? 1 : 0);
    pCmdUI->Enable(TRUE);
}

void DreditorView::OnTerrainCommand(UINT nID)
{
    playClickSound();

    if (toolCommand == static_cast<int32_t>(nID))
    {
        // A second press goes back to the select tool.
        OnPaint();
        return;
    }

    int32_t variant = mapFeaturesBar->checkedButtonData();

    if (variant == -1)
    {
        variant = 1;
    }

    if (!selectMode || !map()->tilesSelected)
    {
        delete tool;
        toolCommand = static_cast<int32_t>(nID);
        tool = makePaletteTool(nID, variant);
        selectMode = false;
    }
    else
    {
        BeginWaitCursor();
        BrushTool* painter = makePaletteTool(nID, variant);

        if (painter != nullptr)
        {
            applyToSelection(*painter);
        }

        EndWaitCursor();
        delete painter;
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::applyToSelection(BrushTool& painter)
{
    EditorMap* editorMap = map();
    painter.begin(*editorMap);

    if (!painter.applyToSelection(*editorMap, 0))
    {
        CString message;
        message.LoadString(PAINT_FAILED);
        CString caption;
        caption.LoadString(MESSAGE_CAPTION);
        MessageBox(message, caption, MB_OK);
    }

    if (EditorAction* action = painter.end(); action != nullptr)
    {
        undoList->push(action);
    }

    InvalidateRect(nullptr, TRUE);
    ::InvalidateRect(tacticalMap->m_hWnd, nullptr, TRUE);
    refreshEnabling = true;
}

void DreditorView::OnUpdateFeatureButton(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(TRUE);
    pCmdUI->SetCheck(toolCommand == static_cast<int32_t>(pCmdUI->m_nID) ? 1 : 0);
}

void DreditorView::OnUpdatePaletteCommand(CCmdUI* pCmdUI)
{
    if (!selectMode)
    {
        pCmdUI->SetCheck(toolCommand == static_cast<int32_t>(pCmdUI->m_nID) ? 1 : 0);
        return;
    }

    pCmdUI->SetCheck(0);

    if (!map()->tilesSelected)
    {
        pCmdUI->Enable(TRUE);
        return;
    }

    // The button is on when its tool has something to work on in the selection; the original makes the tool (cursor
    // bitmaps and all) at every update to ask.
    BrushTool* painter = makePaletteTool(pCmdUI->m_nID, 0);
    pCmdUI->Enable(painter != nullptr && painter->hasTarget(*map()));
    delete painter;
}

void DreditorView::OnOverlayCommand(UINT nID)
{
    playClickSound();

    if (toolCommand == static_cast<int32_t>(nID))
    {
        OnPaint();
        return;
    }

    if (!selectMode || !map()->tilesSelected)
    {
        delete tool;
        toolCommand = static_cast<int32_t>(nID);
        tool = makePaletteTool(nID, 0);
        selectMode = false;
    }
    else
    {
        BeginWaitCursor();
        BrushTool* painter = makePaletteTool(nID, 0);

        if (painter != nullptr)
        {
            applyToSelection(*painter);
        }

        EndWaitCursor();
        delete painter;
        InvalidateRect(nullptr, TRUE);
    }

    if (auto* frame = static_cast<CFrameWnd*>(AfxGetApp()->GetMainWnd()); frame != nullptr)
    {
        frame->RecalcLayout(TRUE);
    }

    InvalidateRect(nullptr, TRUE);
}

LRESULT DreditorView::OnSelectObject(WPARAM wParam, LPARAM lParam)
{
    const auto id = static_cast<int32_t>(wParam);
    const auto side = static_cast<int32_t>(lParam);
    // The command of the object's type.
    const int32_t command = static_cast<int32_t>(objectIdType(static_cast<uint32_t>(id))) + OBJECT_COMMANDS;

    if (toolCommand == command)
    {
        // A second press goes back to the select tool.
        selectTool(false, ID_TOOLS_SELECT);
        return 0;
    }

    if (tool != nullptr && (!selectMode || id != NO_PICK))
    {
        delete tool;
        tool = nullptr;
    }

    if (tool == nullptr && id != NO_PICK && side != NO_PICK)
    {
        tool = new PlaceObjectTool(static_cast<uint32_t>(id), side, false);
        playClickSound();
        selectMode = false;
        toolCommand = command;
        InvalidateRect(nullptr, TRUE);
        return 0;
    }

    if (!selectMode)
    {
        // Original behaviour: back to the select tool, but out of select mode.
        selectTool(false, ID_TOOLS_SELECT);
        selectMode = false;
        return 0;
    }

    playClickSound();
    InvalidateRect(nullptr, TRUE);
    return 0;
}

LRESULT DreditorView::OnSelectMover(WPARAM wParam, LPARAM lParam)
{
    const auto id = static_cast<int32_t>(wParam);
    const auto sideAndVariant = static_cast<int32_t>(lParam);
    playClickSound();
    // The command of the mover's group.
    const int32_t command = static_cast<int32_t>(objectIdType(static_cast<uint32_t>(id))) + MOVER_COMMANDS;

    if (toolCommand == command)
    {
        selectTool(false, ID_TOOLS_SELECT);
        return 0;
    }

    if (tool != nullptr && (!selectMode || id != NO_PICK))
    {
        delete tool;
        tool = nullptr;
    }

    if (!dragging)
    {
        if (id == NO_PICK || sideAndVariant == NO_PICK)
        {
            if (!selectMode)
            {
                selectTool(false, ID_TOOLS_SELECT);
                return 0;
            }
        }
        else
        {
            tool = new PlaceMoverTool(static_cast<uint32_t>(id), sideAndVariant & 0xffff, sideAndVariant >> 16);
            toolCommand = command;
            selectMode = false;
        }
    }

    InvalidateRect(nullptr, TRUE);
    return 0;
}

LRESULT DreditorView::OnCentreOnTacticalPoint([[maybe_unused]] WPARAM wParam, LPARAM lParam)
{
    using X = X87Extended;
    int32_t left;
    int32_t right;
    int32_t top;
    int32_t bottom;
    mapExtent(&left, &right, &top, &bottom);
    // The tactical map point (no negatives), turned back 45 degrees and scaled to the map, then into the view.
    const POINT& point = *reinterpret_cast<const POINT*>(lParam);
    const int32_t pointX = (std::max)(point.x, 0L);
    const int32_t pointY = (std::max)(point.y, 0L);
    EditorMap& editorMap = *map();
    const float scaledX = (X(tileWorldWidth) * X(pointX) * X(TACTICAL_X_SCALE)).toFloat();
    const X scaledY = X(tileWorldWidth) * X(-pointY) * X(TACTICAL_Y_SCALE);
    const X turnedX = X(scaledX) * X(HALF_SQRT2);
    const float mapXFloat = (turnedX - scaledY).toFloat();
    const int32_t halfHeight = ((editorMap.maxCol + 1) >> 1) * tileWorldWidth;
    const X mapYValue = X(halfHeight) - (scaledY + turnedX);
    const auto mapX = static_cast<int32_t>(X(mapXFloat).truncate());
    const auto mapY = static_cast<int32_t>(mapYValue.truncate());
    int32_t viewX = 0;
    int32_t viewY = 0;
    editorMap.mapToView(mapX, mapY, editorMap.terrainHeight(mapX, mapY), &viewX, &viewY);
    scrollTo({viewX - left, viewY - top});
    // The original returns what the scroll left in eax.
    return 0;
}

LRESULT DreditorView::OnScrollView(WPARAM wParam, LPARAM lParam)
{
    // lParam points to a scroll step (wParam 0) or to a view position to centre the window on.
    const POINT& point = *reinterpret_cast<const POINT*>(lParam);
    POINT delta = point;

    if (wParam != 0)
    {
        CRect client;
        GetClientRect(&client);
        delta.x = -((client.Width() >> 1) - (point.x - origin.x));
        delta.y = -((client.Height() >> 1) - (point.y - origin.y));
    }

    const POINT position = scrollPosition();
    scrollTo({position.x + delta.x, position.y + delta.y});
    // The original returns what the scroll left in eax.
    return 0;
}

void DreditorView::OnToolsDamage()
{
    damageCommand(ID_TOOLS_DAMAGE, true);
}

void DreditorView::damageCommand(int32_t command, bool damage)
{
    playClickSound();

    if (toolCommand == command)
    {
        selectTool(false, ID_TOOLS_SELECT);
        return;
    }

    if (selectMode && anythingSelected())
    {
        DamageTool damager(damage);
        applyToSelection(damager);
    }
    else if (!dragging)
    {
        delete tool;
        tool = new DamageTool(damage);
        toolCommand = command;

        // Original behaviour: Repair stays in select mode, so the palette's buttons go on painting the selection.
        if (damage)
        {
            selectMode = false;
        }
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnUpdateToolsDamage(CCmdUI* pCmdUI)
{
    const bool damage = pCmdUI->m_nID == ID_TOOLS_DAMAGE;

    if (!selectMode)
    {
        pCmdUI->SetCheck(toolCommand == static_cast<int32_t>(pCmdUI->m_nID) ? 1 : 0);
        pCmdUI->Enable(TRUE);
    }
    else
    {
        if (refreshEnabling)
        {
            DamageTool damager(damage);
            (damage ? damageEnabled : repairEnabled) = damager.hasTarget(*map());
        }

        pCmdUI->Enable(damage ? damageEnabled : repairEnabled);
        pCmdUI->SetCheck(0);
    }

    // Repair is updated after Damage: the enablings are worked out once per round.
    if (pCmdUI->m_nID == ID_TOOLS_REPAIR)
    {
        refreshEnabling = false;
    }
}

void DreditorView::OnToolsRepair()
{
    damageCommand(ID_TOOLS_REPAIR, false);
}

void DreditorView::OnOverlayLayoutCommand(UINT nID)
{
    playClickSound();

    if (toolCommand == static_cast<int32_t>(nID))
    {
        selectTool(false, ID_TOOLS_SELECT);
        return;
    }

    // The button's data is the overlay kind; its offset in the range is the layout.
    const int32_t kind = mapFeaturesBar->buttonData(nID);
    const auto layout = static_cast<int32_t>(nID - OVERLAY_LAYOUT_COMMANDS);

    if (!selectMode || !map()->tilesSelected)
    {
        if (!dragging)
        {
            delete tool;
            tool = new OverlayLayoutTool(kind, layout);
            selectMode = false;
            toolCommand = static_cast<int32_t>(nID);
        }
    }
    else
    {
        OverlayLayoutTool layer(kind, layout);
        applyToSelection(layer);
        InvalidateRect(nullptr, TRUE);
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnUpdateOverlayLayoutCommand(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(toolCommand == static_cast<int32_t>(pCmdUI->m_nID) ? 1 : 0);
}

void DreditorView::OnToolsDragView()
{
    viewToolCommand(ID_TOOLS_DRAG_VIEW, [] -> EditorTool* { return new DragViewTool(); });
}

void DreditorView::OnToolsZoomIn()
{
    viewToolCommand(ID_TOOLS_ZOOM_IN, [] -> EditorTool* { return new ZoomTool(true, viewZoom); });
}

void DreditorView::OnToolsZoomOut()
{
    viewToolCommand(ID_TOOLS_ZOOM_OUT, [] -> EditorTool* { return new ZoomTool(false, viewZoom); });
}

void DreditorView::OnToolsSprayTrees()
{
    playClickSound();

    if (toolCommand == ID_TOOLS_SPRAY_TREES)
    {
        selectTool(false, ID_TOOLS_SELECT);
        return;
    }

    if (selectMode && map()->tilesSelected)
    {
        // The original makes the tool (0x260c4 bytes) on the stack.
        auto sprayer = std::make_unique<SprayTreesTool>();
        EditorMap& editorMap = *map();
        {
            CWaitCursor wait;
            sprayer->begin(editorMap);

            if (!sprayer->spraySelection(editorMap))
            {
                CString message;
                message.LoadString(PAINT_FAILED);
                CString caption;
                caption.LoadString(MESSAGE_CAPTION);
                MessageBox(message, caption, MB_OK);
            }
        }

        if (EditorAction* action = sprayer->end(); action != nullptr)
        {
            undoList->push(action);
        }

        InvalidateRect(nullptr, TRUE);
        ::InvalidateRect(tacticalMap->m_hWnd, nullptr, TRUE);
    }
    else if (!dragging)
    {
        // EB-085: the original changes the tool during a drag too.
        delete tool;
        tool = new SprayTreesTool();
        toolCommand = ID_TOOLS_SPRAY_TREES;
        selectMode = false;
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnIdsAssignASpecificHeight()
{
    if (!selectMode || map()->selectedVertices.empty())
    {
        return;
    }

    playClickSound();
    VertexTool mover(RAISE_STEP, TEXTURE_NONE, true, 0);
    SetHeightDialog dialog(*map(), mover, this);
    dialog.DoModal();
}

void DreditorView::OnUpdateIdsAssignASpecificHeight(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(selectMode && !map()->selectedVertices.empty() ? TRUE : FALSE);
}

void DreditorView::OnToolsRotate()
{
    playClickSound();

    if (toolCommand == ID_TOOLS_ROTATE)
    {
        selectTool(false, ID_TOOLS_SELECT);
        return;
    }

    if (selectMode && anythingSelected())
    {
        if (!dragging && !toolsPaused())
        {
            RotateTool rotator(ROTATE_STEP);

            if (rotator.hasTarget(*map()))
            {
                applyToSelection(rotator);
            }
        }
    }
    else if (!dragging)
    {
        // EB-085: the original changes the tool during a drag too.
        delete tool;
        tool = new RotateTool(ROTATE_STEP);
        toolCommand = ID_TOOLS_ROTATE;
        selectMode = false;
    }

    InvalidateRect(nullptr, TRUE);
}

void DreditorView::OnUpdateToolsRotate(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(toolCommand == static_cast<int32_t>(pCmdUI->m_nID) ? 1 : 0);

    if ((selectMode && anyObjectSelected()) || map()->tilesSelected)
    {
        if (refreshEnabling)
        {
            RotateTool rotator(ROTATE_STEP);
            rotateEnabled = rotator.hasTarget(*map());
        }

        pCmdUI->Enable(rotateEnabled);
    }
    else
    {
        pCmdUI->Enable(TRUE);
    }
}

LRESULT DreditorView::OnPickOnMap(WPARAM wParam, LPARAM lParam)
{
    // EB-085: the original deletes the tool during a drag too, and makes none.
    if (!dragging)
    {
        delete tool;
        tool = new MapPickTool(static_cast<int32_t>(wParam), reinterpret_cast<CWnd*>(lParam));
    }

    toolCommand = ORDERS_PATH_COMMAND;

    // The dialog that asked is hidden while the pick goes on; the flag tells which.
    if (objectivesDialog != nullptr && ::IsWindowVisible(objectivesDialog->m_hWnd))
    {
        objectivesDialog->ShowWindow(SW_HIDE);
        pickingForUnit = false;
    }

    if (pausingDialog != nullptr && ::IsWindowVisible(pausingDialog->m_hWnd))
    {
        pausingDialog->ShowWindow(SW_HIDE);
        pickingForUnit = true;
    }

    InvalidateRect(nullptr, TRUE);
    return 0;
}

void DreditorView::OnRotateBuilding()
{
    playClickSound();

    if (clickedObject == nullptr)
    {
        return;
    }

    RotateTool rotator(ROTATE_STEP);
    EditorMap& editorMap = *map();
    rotator.begin(editorMap);
    rotator.rotateObject(editorMap, *clickedObject);

    if (EditorAction* action = rotator.end(); action != nullptr)
    {
        undoList->push(action);
    }

    // Original behaviour: the view isn't redrawn.
    clickedObject = nullptr;
}

void DreditorView::OnUpdateRotateBuilding(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(clickedObject != nullptr && clickedObject->hasOtherFrame() ? TRUE : FALSE);
}

void DreditorView::OnMissionTimeOfDay(UINT nID)
{
    EditorMap& editorMap = *map();

    if (const MissionTimeOfDay timeOfDay = timeOfDayCommanded(nID); editorMap.timeOfDay != timeOfDay)
    {
        editorMap.timeOfDay = timeOfDay;
        m_pDocument->SetModifiedFlag(TRUE);
    }
}

void DreditorView::OnUpdateMissionTimeOfDay(CCmdUI* pCmdUI)
{
    pCmdUI->SetRadio(map()->timeOfDay == timeOfDayCommanded(pCmdUI->m_nID) ? TRUE : FALSE);
}

#include "stdafx.h"
#include "EditorMap.h"
#include <fstream>
#include <sstream>
#include "ControlLink.h"
#include "DaoDatabase.h"
#include "DropZoneRecord.h"
#include "EditorObject.h"
#include "EditorPalette.h"
#include "MapMarker.h"
#include "MapOverlay.h"
#include "MissionRecordSet.h"
#include "MoverObject.h"
#include "Objective.h"
#include "ObjectiveMainRecord.h"
#include "ObjectLibrary.h"
#include "StaticObject.h"
#include "StructureLink.h"
#include "StructureRecord.h"
#include "TileLibrary.h"
#include "ai/move.h"
#include "UnitBrain.h"
#include "UnitRecord.h"
#include "X87Extended.h"
#include "lib/packet.h"
#include "vfx/vfxtile.h"
#include "EditorFiles.h"

namespace
{
    /// <summary>The view projection (0x005287f4): the rows give view x, view y and depth.</summary>
    inline constexpr float VIEW_MATRIX[3][3] = {{0.707107008f, -0.707107008f, 0.0f},
                                                {0.405580014f, 0.405580014f, -0.819151998f},
                                                {0.579227984f, 0.579227984f, 0.573575974f}};
    /// <summary>The view direction viewToMap moves along (0x004fea9c, 0x004fea98): the depth row.</summary>
    inline constexpr float VIEW_DIRECTION_XY = 0.579227984f;
    inline constexpr float VIEW_DIRECTION_Z = 0.573575974f;
    /// <summary>How far behind the view plane viewToMap starts its ray.</summary>
    inline constexpr double VIEW_RAY_START = 100.0;
    /// <summary>Added to a double to leave its value, rounded to the nearest integer, in the low word.</summary>
    inline constexpr double ROUNDING_MAGIC = 6755399441055744.0;
    /// <summary>The single-player mission's most turrets, drop zones and players.</summary>
    inline constexpr int32_t SINGLE_MAX_TURRETS = 200;
    inline constexpr int32_t SINGLE_MAX_DROP_ZONES = 3;
    inline constexpr int32_t SINGLE_MAX_PLAYERS = 1;
    /// <summary>The fallback step to a neighbour vertex outside the map.</summary>
    inline constexpr int32_t EDGE_STEP = 5;
    /// <summary>The cut-off corners: tiles within this distance of a corner point.</summary>
    inline constexpr double CORNER_RADIUS_SQUARED = 1690000.0;
    /// <summary>The corner test's units: 128 per tile, the centre 64 per tile from an edge.</summary>
    inline constexpr double CORNER_TILE = 128.0;
    inline constexpr double CORNER_HALF_TILE = 64.0;
    /// <summary>The tile width while objects are placed (the game's tile width).</summary>
    inline constexpr int32_t OBJECT_TILE_WIDTH = 0x9e;
    /// <summary>A footprint is 8 x 8 cells with its object's cell at (4, 4).</summary>
    inline constexpr int32_t FOOTPRINT_SIDE = 8;
    inline constexpr int32_t FOOTPRINT_CENTRE = 4;
    /// <summary>The overlay number of no overlay in a .elv file.</summary>
    inline constexpr int16_t ELV_NO_OVERLAY = 0x29;
    /// <summary>The cell display's colours: blocked cells red, cells that block sight cyan, doors yellow.</summary>
    inline constexpr COLORREF BLOCKED_CELL_COLOR = RGB(0xff, 0, 0);
    inline constexpr COLORREF SIGHT_CELL_COLOR = RGB(0, 0xff, 0xff);
    inline constexpr COLORREF DOOR_COLOR = RGB(0xff, 0xff, 0);
    /// <summary>A scenario map tile's cell bits: per cell, passable and see-through, from bit 14.</summary>
    inline constexpr int32_t PASSABLE_CELL_BIT = 14;
    inline constexpr int32_t SEE_THROUGH_CELL_BIT = 15;
    /// <summary>A global map door's first direction: the door runs down or across.</summary>
    inline constexpr char DOOR_VERTICAL = 1;
    inline constexpr char DOOR_HORIZONTAL = 2;
    /// <summary>Tiles of a .elv block side less one: the map's tile count is blocks * 20 - 1.</summary>
    inline constexpr uint32_t ELV_BLOCK_DIVISOR = MAP_BLOCK_TILES - 1;

    /// <summary>
    /// The integer the x87 code reads from the low word of <paramref name="value"/> + 1.5 * 2^52: the sum rounds to 64
    /// bits, then to a double as it is stored (so it rounds twice).
    /// </summary>
    int32_t roundToInt(const X87Extended& value)
    {
        const double sum = (value + X87Extended(ROUNDING_MAGIC)).toDouble();
        int64_t bits;
        std::memcpy(&bits, &sum, sizeof(bits));
        return static_cast<int32_t>(bits);
    }

    /// <summary>One view coordinate of map position (x, y, z): the dot product with a row of the matrix.</summary>
    int32_t viewCoordinate(int32_t x, int32_t y, int32_t z, const float (&row)[3])
    {
        return roundToInt(X87Extended(z) * X87Extended(row[2]) + X87Extended(y) * X87Extended(row[1]) +
                          X87Extended(x) * X87Extended(row[0]));
    }

    /// <summary>One coordinate of viewToMap's ray start: view x and y along a column, then back from the view plane.</summary>
    float rayStart(int32_t viewX, int32_t viewY, int32_t column)
    {
        return (X87Extended(viewX) * X87Extended(VIEW_MATRIX[0][column]) +
                X87Extended(viewY) * X87Extended(VIEW_MATRIX[1][column]) -
                X87Extended(VIEW_MATRIX[2][column]) * X87Extended(VIEW_RAY_START))
            .toFloat();
    }

    /// <summary>A neighbour's corner textures, water where its corner is at elevation 0.</summary>
    void neighbourTextures(const EditorTile& tile, uint8_t* textures)
    {
        for (int32_t i = 0; i < 4; i++)
        {
            textures[i] = tile.elevations[i] == 0 ? static_cast<uint8_t>(TEXTURE_WATER) : tile.textures[i];
        }
    }

    /// <summary>Whether a texture is one whose tiles take their corner textures from the neighbours.</summary>
    bool isPavedTexture(uint8_t texture)
    {
        return texture == TEXTURE_CONC || texture == TEXTURE_CROP;
    }

    /// <summary>How far the object passes look around an object's cell for a drawn tile.</summary>
    inline constexpr int32_t OBJECT_TILE_REACH = 8;
    /// <summary>Added to a cell's doubled column or row to reach its centre in half cells.</summary>
    inline constexpr float CELL_CENTRE = 1.0f;
    /// <summary>Half a cell, in cells.</summary>
    inline constexpr double HALF = 0.5;
    /// <summary>How far a cell's outline lies in from its edges, in map units.</summary>
    inline constexpr float CELL_OUTLINE_INSET = 2.0f;
    /// <summary>The highest terrain a cell outline is drawn on.</summary>
    inline constexpr int32_t CELL_OUTLINE_TOP = 0x80;
    /// <summary>The footprint outlines' colour (cyan).</summary>
    inline constexpr COLORREF FOOTPRINT_COLOR = RGB(0, 0xff, 0xff);

    /// <summary>Whether tile <paramref name="index"/> of the drawn-tile bits was drawn.</summary>
    bool isTileDrawn(const std::vector<uint32_t>& bits, int32_t index)
    {
        const int32_t word = index >> 3;

        // Original behaviour (EB-050): an object within 8 cells of the map's top or left edge, or of the end of the
        // bits, tests words outside them (the map's other fields); the port counts those tiles as not drawn.
        if (word < 0 || static_cast<size_t>(word) >= bits.size())
        {
            return false;
        }

        return (bits[static_cast<size_t>(word)] & (1u << (index & 7))) != 0;
    }

    /// <summary>The colours of the boxes around objects on a side: side 2's, the others'.</summary>
    inline constexpr COLORREF SIDE_2_BOX_COLOR = RGB(0, 0xff, 0);
    inline constexpr COLORREF OTHER_SIDE_BOX_COLOR = RGB(0xff, 0, 0);
    /// <summary>A side's box: arm length and margin.</summary>
    inline constexpr int32_t SIDE_BOX_SIZE = 8;
    inline constexpr int32_t SIDE_BOX_MARGIN = 4;
    /// <summary>A selected object's box: colour, arm length and margin.</summary>
    inline constexpr COLORREF SELECTION_COLOR = RGB(0xff, 0xff, 0);
    inline constexpr int32_t SELECTION_BOX_SIZE = 0xc;
    inline constexpr int32_t SELECTION_BOX_MARGIN = 8;
    /// <summary>The length of the ticks on a selected object's sides.</summary>
    inline constexpr int32_t SELECTION_TICK = 8;
    /// <summary>How far a vertex may lie outside the view and still be marked.</summary>
    inline constexpr int32_t VERTEX_MARK_SLACK = 2;
    /// <summary>How far right of its vertex a vertex's square is drawn.</summary>
    inline constexpr int32_t VERTEX_MARK_OFFSET = 7;
    /// <summary>Half the side of a vertex's square.</summary>
    inline constexpr int32_t VERTEX_MARK_HALF = 3;

    /// <summary>
    /// The frame whose bounds the boxes use: a mover's, a drop zone's by the id's frame field (always the first drop
    /// zone's: a zone's id keeps its kind in the type field), another object's.
    /// </summary>
    const ObjectFrame& boxFrame(uint32_t id)
    {
        const int32_t objectClass = objectIdClass(id);

        if (isMoverClass(objectClass))
        {
            return objectLibrary->moverGroups[objectIdGroup(id)].types[objectIdType(id)].frames[objectIdFrame(id)];
        }

        if (objectClass == OBJECT_CLASS_DROPZONE)
        {
            return objectLibrary->dropZones[objectIdFrame(id)];
        }

        return objectLibrary->objectGroups[objectIdGroup(id)].types[objectIdType(id)].frames[objectIdFrame(id)];
    }

    /// <summary>
    /// <paramref name="object"/>'s frame bounds at zoom level <paramref name="zoom"/>, each moved out by
    /// <paramref name="margin"/> (scaled too); an edge at 0 moves out to the negative side.
    /// </summary>
    void boxBounds(const EditorObject& object, int32_t zoom, int32_t margin, int32_t (&bounds)[4])
    {
        const ObjectFrame& frame = boxFrame(object.id);

        for (int32_t i = 0; i < 4; i++)
        {
            const int32_t edge = frame.bounds[i] >> zoom;
            bounds[i] = edge > 0 ? (margin >> zoom) + edge : edge - (margin >> zoom);
        }
    }

    /// <summary>
    /// The map position the boxes take for an object on cell (<paramref name="col"/>, <paramref name="row"/>): its
    /// centre, the column through a float.
    /// </summary>
    void boxCentre(int32_t col, int32_t row, int32_t* x, int32_t* y)
    {
        using X = X87Extended;
        const X halfCell = X(tileWorldWidth) / X(MAP_CELLS_PER_TILE) * X(HALF);
        const X across = X(col) * halfCell;
        const X down = X(row) * halfCell;
        *x = static_cast<int32_t>(X((across + across + halfCell).toFloat()).truncate());
        *y = static_cast<int32_t>((down + down + halfCell).truncate());
    }
}

int32_t gridLineColor = 0xff;
int32_t selectedTileColor = 10;
COLORREF vertexMarkColor = RGB(0xff, 0xff, 0);
bool showVertexHeights = true;
COLORREF linkLineColor = RGB(0x8b, 0x8d, 0xd1);

EditorMap::EditorMap(int32_t maxRow, int32_t maxCol) : maxRow(maxRow), maxCol(maxCol)
{
    const int32_t tileCount = maxRow * maxCol;
    tiles.resize(static_cast<size_t>(tileCount));
    const double halfHeight = maxCol * CORNER_HALF_TILE;

    for (int32_t row = 0; row < maxCol; row++)
    {
        for (int32_t col = 0; col < maxRow; col++)
        {
            const double y = static_cast<float>(row * CORNER_TILE - maxRow * CORNER_HALF_TILE);
            const double x = static_cast<float>(maxRow * CORNER_HALF_TILE - col * CORNER_TILE);
            const double xBelow = x - halfHeight;
            const double yBelow = y - halfHeight;
            const double xAbove = x + halfHeight;
            const double yAbove = y + halfHeight;

            if (xBelow * xBelow + y * y <= CORNER_RADIUS_SQUARED || x * x + yBelow * yBelow <= CORNER_RADIUS_SQUARED ||
                xAbove * xAbove + y * y <= CORNER_RADIUS_SQUARED || x * x + yAbove * yAbove <= CORNER_RADIUS_SQUARED)
            {
                getTile(col, row)->inCorner = true;
            }
        }
    }

    // Empty movement maps until a save or Toggle Cells builds them from the terrain.
    scenarioMap = new ScenarioMap();
    globalMap = new GlobalMap();
    cellGrid.assign(static_cast<size_t>(tileCount) * MAP_CELLS_PER_TILE * MAP_CELLS_PER_TILE, nullptr);
    objectives.push_back(new Objective(OBJECTIVE_DESTROY_ALL));
}

EditorMap::~EditorMap()
{
    for (EditorObject* object : objects)
    {
        delete object;
    }

    for (Objective* objective : objectives)
    {
        delete objective;
    }

    for (EditorObject* object : dropZones)
    {
        delete object;
    }

    for (EditorObject* mover : movers)
    {
        delete mover;
    }

    for (ControlLink* link : controlLinks)
    {
        delete link;
    }

    if (scenarioMap != nullptr)
    {
        scenarioMap->destroy();
        delete scenarioMap;
    }

    if (globalMap != nullptr)
    {
        globalMap->destroy();
        delete globalMap;
    }
}

EditorTile* EditorMap::getTile(int32_t col, int32_t row)
{
    if (col < 0 || col >= maxRow || row < 0 || row >= maxCol)
    {
        return nullptr;
    }

    return &tiles[static_cast<size_t>(maxRow * row + col)];
}

int32_t EditorMap::regionOf(int32_t col, int32_t row)
{
    const auto halfAcross = static_cast<int32_t>(static_cast<uint32_t>(maxRow) >> 1);
    const auto halfDown = static_cast<int32_t>(static_cast<uint32_t>(maxCol) >> 1);
    int32_t count = 0;
    int32_t first;
    int32_t second;

    if (static_cast<uint32_t>(row) < static_cast<uint32_t>(halfDown) + 1)
    {
        first = halfAcross + row;
        second = halfDown - row;
    }
    else
    {
        first = row - halfAcross;
        second = halfDown * 3 - row;
    }

    if (first > col)
    {
        count = 1;
    }
    else if (first == col)
    {
        return REGION_EDGE;
    }

    if (second > col)
    {
        count++;
    }
    else if (second == col)
    {
        return REGION_EDGE;
    }

    if (count != 1)
    {
        return REGION_OUTSIDE;
    }

    return getTile(col, row)->inCorner ? REGION_OUTSIDE : REGION_INSIDE;
}

bool EditorMap::getVertex(int32_t col, int32_t row, int32_t* x, int32_t* y, int32_t* z)
{
    if (col < 0 || row < 0 || maxRow < col || maxCol < row)
    {
        return false;
    }

    *x = tileWorldWidth * col;
    *y = tileWorldHeight * row;
    const EditorTile* tile = getTile(col, row);
    int32_t corner = 0;

    if (tile == nullptr)
    {
        if (col == maxRow)
        {
            if (row == maxCol)
            {
                tile = getTile(col - 1, row - 1);
                corner = 3;
            }
            else
            {
                tile = getTile(col - 1, row);
                corner = 1;
            }
        }
        else
        {
            if (row != maxCol)
            {
                return false;
            }

            tile = getTile(col, row - 1);
            corner = 2;
        }

        if (tile == nullptr)
        {
            return false;
        }
    }

    *z = tile->elevations[corner] * elevationScale;
    return true;
}

void EditorMap::mapToView(int32_t x, int32_t y, int32_t z, int32_t* viewX, int32_t* viewY)
{
    *viewX = viewCoordinate(x, y, z, VIEW_MATRIX[0]);
    *viewY = viewCoordinate(x, y, z, VIEW_MATRIX[1]);
}

bool EditorMap::findNearestVertex(int32_t viewX, int32_t viewY, int32_t* col, int32_t* row, int32_t* offsetX,
                                  int32_t* offsetY)
{
    *offsetX = 0x7fffffff;
    *offsetY = 0x7fffffff;
    // The original keeps the best distance as a float, so the comparisons are against its rounded value.
    float best = static_cast<float>(*offsetX);
    bool found = false;

    for (int32_t c = 0; c <= maxRow; c++)
    {
        for (int32_t r = 0; r <= maxCol; r++)
        {
            int32_t x;
            int32_t y;
            int32_t z;

            if (!getVertex(c, r, &x, &y, &z))
            {
                continue;
            }

            int32_t vertexX;
            int32_t vertexY;
            mapToView(x, y, z, &vertexX, &vertexY);
            const int32_t distance = (vertexY - viewY) * (vertexY - viewY) + (vertexX - viewX) * (vertexX - viewX);

            if (static_cast<double>(distance) < best)
            {
                *offsetX = viewX - vertexX;
                *offsetY = viewY - vertexY;
                best = static_cast<float>(distance);
                *col = c;
                found = true;
                *row = r;
            }
        }
    }

    return found;
}

void EditorMap::viewToMap(int32_t viewX, int32_t viewY, int32_t* x, int32_t* y, int32_t* z)
{
    // The ray's start, stored as floats.
    const float startX = rayStart(viewX, viewY, 0);
    const float startY = rayStart(viewX, viewY, 1);
    const float startZ = rayStart(viewX, viewY, 2);
    // Original behaviour: the vertex search writes into the view coordinates' slots, so without a vertex these stay
    // the view position, and the vertex stays unset when that isn't one.
    int32_t col = viewX;
    int32_t row = viewY;
    int32_t offsetX = 0;
    int32_t offsetY = 0;
    findNearestVertex(viewX, viewY, &col, &row, &offsetX, &offsetY);
    int32_t vx = 0;
    int32_t vy = 0;
    int32_t vz = 0;
    getVertex(col, row, &vx, &vy, &vz);
    // The plane is the vertex and the two neighbours on the side of the offset.
    int32_t ax = 0;
    int32_t ay = 0;
    int32_t az = 0;
    int32_t bx = 0;
    int32_t by = 0;
    int32_t bz = 0;
    const auto neighbour =
        [&](int32_t c, int32_t r, int32_t* nx, int32_t* ny, int32_t* nz, int32_t stepX, int32_t stepY)
    {
        if (!getVertex(c, r, nx, ny, nz))
        {
            *nx = vx + stepX;
            *ny = vy + stepY;
            *nz = vz;
        }
    };

    if (std::abs(offsetX) > std::abs(offsetY))
    {
        if (offsetX > 0)
        {
            neighbour(col, row - 1, &ax, &ay, &az, 0, -EDGE_STEP);
            neighbour(col + 1, row, &bx, &by, &bz, EDGE_STEP, 0);
        }
        else
        {
            neighbour(col - 1, row, &ax, &ay, &az, -EDGE_STEP, 0);
            neighbour(col, row + 1, &bx, &by, &bz, 0, EDGE_STEP);
        }
    }
    else if (offsetY > 0)
    {
        neighbour(col, row + 1, &ax, &ay, &az, 0, EDGE_STEP);
        neighbour(col + 1, row + 1, &bx, &by, &bz, EDGE_STEP, 0);
    }
    else
    {
        neighbour(col - 1, row, &ax, &ay, &az, -EDGE_STEP, 0);
        neighbour(col - 1, row - 1, &bx, &by, &bz, 0, -EDGE_STEP);
    }

    // The plane's normal and constant, in 32-bit arithmetic as the original (the products may wrap; the results fit).
    const auto wrap = [](int64_t value) { return static_cast<int32_t>(value); };
    const int32_t normalX = wrap(static_cast<int64_t>(vz - az) * by + static_cast<int64_t>(az - bz) * vy +
                                 static_cast<int64_t>(bz - vz) * ay);
    const int32_t normalY = wrap(static_cast<int64_t>(ax - bx) * vz + static_cast<int64_t>(bx - vx) * az +
                                 static_cast<int64_t>(vx - ax) * bz);
    const int32_t normalZ = wrap(static_cast<int64_t>(vy - ay) * bx + static_cast<int64_t>(ay - by) * vx +
                                 static_cast<int64_t>(by - vy) * ax);
    const int32_t constant =
        wrap(-(static_cast<int64_t>(wrap(static_cast<int64_t>(bz) * ay - static_cast<int64_t>(by) * az)) * vx +
               static_cast<int64_t>(wrap(static_cast<int64_t>(az) * vy - static_cast<int64_t>(vz) * ay)) * bx +
               static_cast<int64_t>(wrap(static_cast<int64_t>(by) * vz - static_cast<int64_t>(bz) * vy)) * ax));
    // In x87 extended precision, as the original.
    using X = X87Extended;
    const X numerator = X(constant) + X(startZ) * X(normalZ) + X(startY) * X(normalY) + X(startX) * X(normalX);
    const X denominator = (X(normalY) + X(normalX)) * X(VIEW_DIRECTION_XY) + X(normalZ) * X(VIEW_DIRECTION_Z);
    const X distance = (numerator / denominator).abs();
    const X alongXY = distance * X(VIEW_DIRECTION_XY);
    *x = static_cast<int32_t>((alongXY + X(startX)).truncate());
    *y = static_cast<int32_t>((alongXY + X(startY)).truncate());
    *z = static_cast<int32_t>((distance * X(VIEW_DIRECTION_Z) + X(startZ)).truncate());
}

bool EditorMap::getVertexView(int32_t col, int32_t row, int32_t* viewX, int32_t* viewY)
{
    int32_t x;
    int32_t y;
    int32_t z;

    if (!getVertex(col, row, &x, &y, &z))
    {
        return false;
    }

    mapToView(x, y, z, viewX, viewY);
    return true;
}

int32_t EditorMap::terrainHeight(int32_t x, int32_t y)
{
    int32_t col = x / tileWorldWidth;
    int32_t row = y / tileWorldHeight;
    const int32_t offsetX = x % tileWorldWidth;
    const int32_t offsetY = y % tileWorldHeight;
    // Original behaviour: a vertex outside the map leaves its values unset; the port's are 0.
    int32_t x0 = 0;
    int32_t y0 = 0;
    int32_t z0 = 0;
    int32_t x1 = 0;
    int32_t y1 = 0;
    int32_t z1 = 0;
    int32_t x2 = 0;
    int32_t y2 = 0;
    int32_t z2 = 0;
    getVertex(col, row, &x0, &y0, &z0);

    if (offsetX > offsetY)
    {
        col++;
        getVertex(col, row, &x1, &y1, &z1);
        row++;
    }
    else
    {
        row++;
        getVertex(col + 1, row, &x1, &y1, &z1);
    }

    getVertex(col, row, &x2, &y2, &z2);
    // The plane through the three vertices, in 32-bit arithmetic as the original (the products may wrap).
    const auto wrap = [](int64_t value) { return static_cast<int32_t>(value); };
    const auto mul = [&](int32_t a, int32_t b) { return wrap(static_cast<int64_t>(a) * b); };
    const int32_t constant =
        wrap(-(static_cast<int64_t>(mul(x0, wrap(static_cast<int64_t>(mul(y1, z2)) - mul(y2, z1)))) +
               mul(x2, wrap(static_cast<int64_t>(mul(z1, y0)) - mul(z0, y1))) +
               mul(x1, wrap(static_cast<int64_t>(mul(y2, z0)) - mul(z2, y0)))));
    const int32_t normalY = wrap(static_cast<int64_t>(mul(x1 - x2, z0)) + mul(x2 - x0, z1) + mul(x0 - x1, z2));
    const int32_t normalX = wrap(static_cast<int64_t>(mul(z2 - z0, y1)) + mul(z0 - z1, y2) + mul(z1 - z2, y0));
    const int32_t normalZ = wrap(static_cast<int64_t>(mul(y1 - y2, x0)) + mul(y0 - y1, x2) + mul(y2 - y0, x1));

    if (normalZ == 0)
    {
        // The original divides by zero: the masked x87 gives infinity or NaN, which __ftol turns into the integer
        // indefinite, whose low word is 0.
        return 0;
    }

    using X = X87Extended;
    const X numerator = X(constant) + X(normalY) * X(y) + X(normalX) * X(x);
    return static_cast<int32_t>((numerator / (X(0) - X(normalZ))).truncate());
}

bool EditorMap::drawTerrain(const POINT& origin, uint8_t* buffer, int32_t width, int32_t height, bool gridlines,
                            int32_t zoom)
{
    const uint8_t* amber = editorPalette->amberTint;
    const uint8_t* brighter = editorPalette->brighter;
    std::fill(visibleTiles.begin(), visibleTiles.end(), 0);
    int32_t firstCol = 0x7fffffff;
    int32_t firstRow = 0x7fffffff;
    int32_t lastCol = -1;
    int32_t lastRow = -1;
    // Original behaviour: a vertex outside the map keeps the view position of the one before.
    int32_t viewX = 0;
    int32_t viewY = 0;

    const auto projectVertex = [&](int32_t col, int32_t row)
    {
        int32_t x;
        int32_t y;
        int32_t z;

        if (getVertex(col, row, &x, &y, &z))
        {
            mapToView(x, y, z, &viewX, &viewY);
        }
    };

    const int32_t outline = gridlines ? gridLineColor : VFX_NO_OUTLINE;
    EditorTile* tile = tiles.data();

    for (int32_t row = 0; row < maxCol; row++)
    {
        const int32_t rowBits = row * MAP_VISIBLE_ROW;

        for (int32_t col = 0; col < maxRow; col++, tile++)
        {
            projectVertex(col, row);
            const int32_t region = regionOf(col, row);
            const uint8_t* xlat = tile->highlighted ? brighter : (region != REGION_INSIDE ? amber : nullptr);
            const int32_t x = viewX - origin.x;
            const int32_t y = viewY - origin.y;
            const uint32_t drawn =
                tile->drawArt(buffer, width, height, x, y, xlat, tile->selected ? selectedTileColor : outline, zoom);

            if (tile->overlay != nullptr)
            {
                tile->overlay->draw(buffer, width, height, x + 3, y - 3, xlat, zoom);
            }

            if (drawn != VFX_TILE_OUTSIDE)
            {
                const int32_t bit = rowBits + col;
                visibleTiles[static_cast<size_t>(bit >> 3)] |= 1u << (bit & 7);
                firstCol = (std::min)(firstCol, col);
                firstRow = (std::min)(firstRow, row);
                lastCol = (std::max)(lastCol, col);
                lastRow = (std::max)(lastRow, row);
            }
        }
    }

    // The selected tiles again, over their neighbours. Original behaviour (EB-052): the last drawn row and column are
    // left out.
    for (int32_t row = firstRow; row < lastRow; row++)
    {
        for (int32_t col = firstCol; col < lastCol; col++)
        {
            EditorTile* selected = getTile(col, row);
            projectVertex(col, row);

            if (!selected->selected)
            {
                continue;
            }

            const uint8_t* xlat = selected->highlighted ? brighter : nullptr;
            const int32_t x = viewX - origin.x;
            const int32_t y = viewY - origin.y;
            selected->drawArt(buffer, width, height, x, y, xlat, selectedTileColor, zoom);

            if (selected->overlay != nullptr)
            {
                selected->overlay->draw(buffer, width, height, x + 3, y - 3, xlat, zoom);
            }
        }
    }

    // The objects whose cell, or a cell 8 up-left or down-right of it, lies on a drawn tile.
    const float halfCell =
        (X87Extended(tileWorldWidth) / X87Extended(MAP_CELLS_PER_TILE) * X87Extended(HALF)).toFloat();

    // Returns whether the object was drawn.
    const auto drawObject = [&](EditorObject& object)
    {
        const int32_t col = object.cellCol;
        const int32_t row = object.cellRow;
        const auto tileIndex = [](int32_t r, int32_t c)
        { return r / MAP_CELLS_PER_TILE * MAP_VISIBLE_ROW + c / MAP_CELLS_PER_TILE; };

        if (!isTileDrawn(visibleTiles, tileIndex(row - OBJECT_TILE_REACH, col - OBJECT_TILE_REACH)) &&
            !isTileDrawn(visibleTiles, tileIndex(row + OBJECT_TILE_REACH, col + OBJECT_TILE_REACH)) &&
            !isTileDrawn(visibleTiles, tileIndex(row, col)))
        {
            return false;
        }

        using X = X87Extended;
        const auto centre = [&](int32_t cell)
        { return static_cast<int32_t>(((X(cell) + X(cell) + X(CELL_CENTRE)) * X(halfCell)).truncate()); };

        const int32_t x = centre(col);
        const int32_t y = centre(row);
        int32_t objectViewX;
        int32_t objectViewY;
        mapToView(x, y, terrainHeight(x, y), &objectViewX, &objectViewY);
        object.draw(buffer, width, height, objectViewX - origin.x, objectViewY - origin.y, zoom);
        return true;
    };

    for (EditorObject* object : dropZones)
    {
        drawObject(*object);
    }

    for (EditorObject* object : objects)
    {
        drawObject(*object);
    }

    for (EditorObject* object : movers)
    {
        if (drawObject(*object))
        {
            static_cast<MoverObject*>(object)->brain->drawParameters(*this, origin, buffer, width, height, zoom);
        }
    }

    for (Objective* objective : objectives)
    {
        objective->draw(*this, origin, buffer, width, height, zoom);
    }

    return true;
}

bool EditorMap::drawFootprints(const POINT& origin, HDC dc, [[maybe_unused]] int32_t width,
                               [[maybe_unused]] int32_t height, bool cells, int32_t zoom)
{
    constexpr int32_t FOOTPRINT_SIDE = 8;
    constexpr int32_t FOOTPRINT_CENTRE = 4;
    std::list<POINT> covered;

    for (const EditorObject* object : objects)
    {
        const uint64_t footprint = frameFootprint(objectFrame(object->id));

        for (int32_t r = 0; r < FOOTPRINT_SIDE; r++)
        {
            for (int32_t c = 0; c < FOOTPRINT_SIDE; c++)
            {
                if ((footprint >> (r * FOOTPRINT_SIDE + c) & 1) != 0)
                {
                    covered.push_back({c + object->cellCol - FOOTPRINT_CENTRE, r + object->cellRow - FOOTPRINT_CENTRE});
                }
            }
        }
    }

    HPEN pen = CreatePen(PS_SOLID, 0, FOOTPRINT_COLOR);
    HGDIOBJ oldPen = SelectObject(dc, pen);

    for (const POINT& cell : covered)
    {
        drawCellOutline(dc, cell, origin, zoom);
    }

    SelectObject(dc, oldPen);
    DeleteObject(pen);

    // With cells: the drawn tiles' cells the scenario map blocks (red) and blocks the sight through (cyan), then the
    // global map's doors (yellow). Toggle Cells builds both maps before it turns cells on; the original reads the
    // scenario map's tiles without testing that it has any.
    if (!cells || scenarioMap->map == nullptr)
    {
        return true;
    }

    HPEN blockedPen = CreatePen(PS_SOLID, 0, BLOCKED_CELL_COLOR);
    HPEN sightPen = CreatePen(PS_SOLID, 0, SIGHT_CELL_COLOR);
    oldPen = SelectObject(dc, blockedPen);

    for (int32_t row = 0; row < maxRow; row++)
    {
        for (int32_t col = 0; col < maxRow; col++)
        {
            const int32_t index = row * MAP_VISIBLE_ROW + col;

            if ((visibleTiles[static_cast<size_t>(index >> 3)] & 1u << (index & 7)) == 0)
            {
                continue;
            }

            const uint32_t tileCells = scenarioMap->map[scenarioMap->width * row + col].cells;

            for (int32_t cellCol = 0; cellCol < MAP_CELLS_PER_TILE; cellCol++)
            {
                for (int32_t cellRow = 0; cellRow < MAP_CELLS_PER_TILE; cellRow++)
                {
                    // Each cell has two bits from bit 14 on: passable and see-through.
                    const int32_t shift = cellCol * 2 + cellRow * 6;
                    const POINT cell = {MAP_CELLS_PER_TILE * col + cellCol, MAP_CELLS_PER_TILE * row + cellRow};

                    if ((tileCells >> (shift + PASSABLE_CELL_BIT) & 1) == 0)
                    {
                        SelectObject(dc, blockedPen);
                        drawCellOutline(dc, cell, origin, zoom);
                    }

                    if ((tileCells >> (shift + SEE_THROUGH_CELL_BIT) & 1) == 0)
                    {
                        SelectObject(dc, sightPen);
                        drawCellOutline(dc, cell, origin, zoom);
                    }
                }
            }
        }
    }

    SelectObject(dc, oldPen);
    DeleteObject(blockedPen);
    DeleteObject(sightPen);
    HPEN doorPen = CreatePen(PS_SOLID, 0, DOOR_COLOR);
    oldPen = SelectObject(dc, doorPen);
    const int32_t cellSize = tileWorldWidth / MAP_CELLS_PER_TILE;

    for (int32_t i = 0; i < globalMap->numDoors; i++)
    {
        // The door's cells as a quadrangle on the terrain: from its first cell's corner, its length across (a
        // horizontal door) or down (a vertical one). The original goes through floats, exactly.
        const GlobalMapDoor& door = globalMap->doors[i];
        const int32_t across = door.direction[0] == DOOR_HORIZONTAL ? door.length : 1;
        const int32_t down = door.direction[0] == DOOR_VERTICAL ? door.length : 1;
        const int32_t left = door.col * tileWorldWidth + door.cellC * cellSize;
        const int32_t top = door.row * tileWorldHeight + door.cellR * cellSize;
        const int32_t right = left + across * cellSize;
        const int32_t bottom = top + down * cellSize;
        const POINT mapCorners[4] = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
        POINT corners[5];

        for (int32_t c = 0; c < 4; c++)
        {
            int32_t viewX = 0;
            int32_t viewY = 0;
            mapToView(mapCorners[c].x, mapCorners[c].y, terrainHeight(mapCorners[c].x, mapCorners[c].y), &viewX,
                      &viewY);
            corners[c] = {viewX - origin.x, viewY - origin.y};
        }

        corners[4] = corners[0];
        Polyline(dc, corners, 5);
    }

    SelectObject(dc, oldPen);
    DeleteObject(doorPen);
    return true;
}

void EditorMap::drawCellOutline(HDC dc, const POINT& cell, const POINT& origin, [[maybe_unused]] int32_t zoom)
{
    using X = X87Extended;
    const X cellSize((X(tileWorldWidth) / X(MAP_CELLS_PER_TILE)).toFloat());
    const X inset(CELL_OUTLINE_INSET);
    const X one(CELL_CENTRE);
    const auto nearEdge = [&](int32_t index) { return static_cast<int32_t>((X(index) * cellSize + inset).truncate()); };
    const auto farEdge = [&](int32_t index)
    { return static_cast<int32_t>(((X(index) + one) * cellSize - inset).truncate()); };

    const int32_t left = nearEdge(cell.x);
    const int32_t top = nearEdge(cell.y);
    const int32_t right = farEdge(cell.x);
    const int32_t bottom = farEdge(cell.y);
    const int32_t corners[4][2] = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
    POINT points[5];

    for (int32_t i = 0; i < 4; i++)
    {
        const int32_t z = terrainHeight(corners[i][0], corners[i][1]);

        if (z < 0 || z > CELL_OUTLINE_TOP)
        {
            return;
        }

        int32_t x;
        int32_t y;
        mapToView(corners[i][0], corners[i][1], z, &x, &y);
        points[i] = {x, y};
    }

    points[4] = points[0];

    for (POINT& point : points)
    {
        point.x -= origin.x;
        point.y -= origin.y;
    }

    Polyline(dc, points, 5);
}

bool EditorMap::drawMarks(const POINT& origin, HDC dc, int32_t width, int32_t height, int32_t zoom, bool links)
{
    bool marksDrawn = true;

    for (const MapMark& mark : marks)
    {
        if (!mark.marker->draw(dc, width, height, mark.x - origin.x, mark.y - origin.y, zoom))
        {
            marksDrawn = false;
        }
    }

    const bool drawn = drawVertexMarks(origin, dc, width, height) && marksDrawn;

    // The objects, then the drop zones (one iterator runs through both lists), then the movers.
    const auto drawBoxes = [&](const EditorObject& object)
    {
        if (object.side != 0)
        {
            const COLORREF color = object.side == 2 ? SIDE_2_BOX_COLOR : OTHER_SIDE_BOX_COLOR;
            drawObjectBox(origin, dc, object, zoom, color, SIDE_BOX_SIZE, SIDE_BOX_MARGIN);
        }

        if (object.selected)
        {
            drawSelectionBox(origin, dc, object, zoom);
        }
    };

    for (const EditorObject* object : objects)
    {
        drawBoxes(*object);
    }

    for (const EditorObject* object : dropZones)
    {
        drawBoxes(*object);
    }

    for (const EditorObject* mover : movers)
    {
        drawBoxes(*mover);
    }

    for (Objective* objective : objectives)
    {
        objective->drawMarks(*this, origin, dc, width, height, false, zoom);
    }

    if (links)
    {
        using X = X87Extended;
        const float cellSize = (X(tileWorldWidth) / X(MAP_CELLS_PER_TILE)).toFloat();
        const auto toMap = [&](int32_t cell) { return static_cast<int32_t>((X(cell) * X(cellSize)).truncate()); };

        for (const ControlLink* link : controlLinks)
        {
            POINT cell;
            link->getCell(&cell);
            const int32_t startX = toMap(cell.x);
            const int32_t startY = toMap(cell.y);
            int32_t startViewX;
            int32_t startViewY;
            mapToView(startX, startY, terrainHeight(startX, startY), &startViewX, &startViewY);
            const int32_t count = link->linkedCount();
            std::vector<POINT> cells(static_cast<size_t>(count));
            link->getLinkedCells(cells.data(), count);

            for (POINT& target : cells)
            {
                target.x = toMap(target.x);
                target.y = toMap(target.y);
                int32_t viewX;
                int32_t viewY;
                mapToView(target.x, target.y, terrainHeight(target.x, target.y), &viewX, &viewY);
                const int32_t x = startViewX - origin.x;
                const int32_t y = startViewY - origin.y;
                HPEN pen = CreatePen(PS_DASH, 1, linkLineColor);
                HGDIOBJ oldPen = SelectObject(dc, pen);
                MoveToEx(dc, x, y, nullptr);
                LineTo(dc, viewX - startViewX + x, viewY - startViewY + y);
                SelectObject(dc, oldPen);
                DeleteObject(pen);
            }
        }
    }

    return drawn;
}

bool EditorMap::drawVertexMarks(const POINT& origin, HDC dc, int32_t width, int32_t height)
{
    bool drawn = true;
    const COLORREF oldColor = SetBkColor(dc, vertexMarkColor);
    const int oldMode = SetBkMode(dc, TRANSPARENT);
    // Original behaviour: a vertex outside the map keeps the view position of the one before, and the first one the
    // old background mode in both coordinates.
    int32_t viewX = oldMode;
    int32_t viewY = oldMode;

    for (const POINT& vertex : selectedVertices)
    {
        if (!getVertexView(vertex.x, vertex.y, &viewX, &viewY))
        {
            drawn = false;
        }

        if (viewX < origin.x - VERTEX_MARK_SLACK || viewX > origin.x + width + VERTEX_MARK_SLACK ||
            viewY < origin.y - VERTEX_MARK_SLACK || viewY > origin.y + height + VERTEX_MARK_SLACK)
        {
            continue;
        }

        const int32_t x = viewX + VERTEX_MARK_OFFSET - origin.x;
        const int32_t y = viewY - origin.y;
        const RECT square = {x - VERTEX_MARK_HALF, y - VERTEX_MARK_HALF, x + VERTEX_MARK_HALF, y + VERTEX_MARK_HALF};
        ExtTextOutA(dc, 0, 0, ETO_OPAQUE, &square, nullptr, 0, nullptr);

        if (showVertexHeights)
        {
            // Original behaviour: the elevation is read without testing the vertex (left unset outside the map; the
            // port's is 0).
            int32_t mapX = 0;
            int32_t mapY = 0;
            int32_t mapZ = 0;
            getVertex(vertex.x, vertex.y, &mapX, &mapY, &mapZ);
            char text[256];
            _itoa_s(mapZ / elevationScale, text, 10);
            TextOutA(dc, x + VERTEX_MARK_HALF, y + VERTEX_MARK_HALF, text, static_cast<int>(std::strlen(text)));
        }
    }

    SetBkColor(dc, oldColor);
    SetBkMode(dc, oldMode);
    return drawn;
}

bool EditorMap::drawObjectBox(const POINT& origin, HDC dc, const EditorObject& object, int32_t zoom, COLORREF color,
                              int32_t size, int32_t margin)
{
    int32_t bounds[4];
    boxBounds(object, zoom, margin, bounds);
    const int32_t arm = size >> zoom;
    int32_t x;
    int32_t y;
    boxCentre(object.cellCol, object.cellRow, &x, &y);
    int32_t viewX;
    int32_t viewY;
    mapToView(x, y, terrainHeight(x, y), &viewX, &viewY);
    const int32_t left = bounds[0] + viewX - origin.x;
    const int32_t top = bounds[1] + viewY - origin.y;
    const int32_t right = bounds[2] + viewX - origin.x;
    const int32_t bottom = bounds[3] + viewY - origin.y;
    const POINT topLeft[3] = {{left + arm, top}, {left, top}, {left, top + arm}};
    const POINT bottomLeft[3] = {{left, bottom - arm}, {left, bottom}, {left + arm, bottom}};
    const POINT topRight[3] = {{right - arm, top}, {right, top}, {right, top + arm}};
    const POINT bottomRight[3] = {{right, bottom - arm}, {right, bottom}, {right - arm, bottom}};
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polyline(dc, topLeft, 3);
    Polyline(dc, bottomLeft, 3);
    Polyline(dc, topRight, 3);
    Polyline(dc, bottomRight, 3);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    return true;
}

bool EditorMap::drawSelectionBox(const POINT& origin, HDC dc, const EditorObject& object, int32_t zoom)
{
    drawObjectBox(origin, dc, object, zoom, SELECTION_COLOR, SELECTION_BOX_SIZE, SELECTION_BOX_MARGIN);
    int32_t bounds[4];
    boxBounds(object, zoom, SELECTION_BOX_MARGIN, bounds);
    const int32_t tick = SELECTION_TICK >> zoom;
    int32_t x;
    int32_t y;
    boxCentre(object.cellCol, object.cellRow, &x, &y);
    int32_t viewX;
    int32_t viewY;
    mapToView(x, y, terrainHeight(x, y), &viewX, &viewY);
    const int32_t left = bounds[0] + viewX - origin.x;
    const int32_t right = bounds[2] + viewX - origin.x;
    const int32_t top = ((bounds[3] - bounds[1]) >> 1) - (tick >> 1) - origin.y + viewY + bounds[1];
    const POINT leftTick[2] = {{left, top}, {left, top + tick}};
    const POINT rightTick[2] = {{right, top + tick}, {right, top}};
    HPEN pen = CreatePen(PS_SOLID, 1, SELECTION_COLOR);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polyline(dc, leftTick, 2);
    Polyline(dc, rightTick, 2);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    return true;
}

bool EditorMap::addMark(MapMarker* marker, int32_t x, int32_t y)
{
    for (const MapMark& mark : marks)
    {
        if (mark.marker == marker && mark.x == x && mark.y == y)
        {
            return false;
        }
    }

    marks.push_back({x, y, marker});
    return true;
}

bool EditorMap::clearMarks()
{
    // The original deletes a marker once per mark; one marked at several places is deleted once here.
    std::vector<MapMarker*> markers;

    for (const MapMark& mark : marks)
    {
        if (std::find(markers.begin(), markers.end(), mark.marker) == markers.end())
        {
            markers.push_back(mark.marker);
        }
    }

    for (MapMarker* marker : markers)
    {
        delete marker;
    }

    marks.clear();
    return true;
}

bool EditorMap::removeMark(MapMarker* marker, int32_t x, int32_t y)
{
    const size_t count = marks.size();

    if (x == MARK_ANYWHERE || y == MARK_ANYWHERE)
    {
        marks.remove_if([&](const MapMark& mark) { return mark.marker == marker; });
    }
    else
    {
        // The marker isn't compared here: every mark at the position goes.
        marks.remove_if([&](const MapMark& mark) { return mark.x == x && mark.y == y; });
    }

    return marks.size() != count;
}

bool EditorMap::canPlace(const EditorObject& object)
{
    const int32_t col = object.cellCol;
    const int32_t row = object.cellRow;

    if (!isCellInGrid(col, row))
    {
        return true;
    }

    if (objectAtCell(col, row) != nullptr)
    {
        return false;
    }

    const EditorObject* below = objectAtCell(col + 1, row + 1);
    return below == nullptr || !isMoverClass(objectIdClass(below->id));
}

bool EditorMap::isCellInGrid(int32_t col, int32_t row) const
{
    return col >= 0 && col < maxRow * MAP_CELLS_PER_TILE && row >= 0 && row < maxCol * MAP_CELLS_PER_TILE;
}

void EditorMap::markCells(EditorObject* object)
{
    const int32_t cellsPerRow = maxRow * MAP_CELLS_PER_TILE;
    const int32_t col = object->cellCol;
    const int32_t row = object->cellRow;
    const uint64_t footprint = frameFootprint(objectFrame(object->id));

    // Original behaviour (EB-037): an object outside the grid is not refused, and neither its cell nor its footprint
    // is clipped, so it writes outside the grid.
    if (isCellInGrid(col, row))
    {
        cellGrid[static_cast<size_t>(cellsPerRow * row + col)] = object;
    }

    for (int32_t r = 0; r < FOOTPRINT_SIDE; r++)
    {
        for (int32_t c = 0; c < FOOTPRINT_SIDE; c++)
        {
            if ((footprint >> (r * FOOTPRINT_SIDE + c) & 1) != 0 || (c == FOOTPRINT_CENTRE && r == FOOTPRINT_CENTRE))
            {
                const int32_t cellCol = col - FOOTPRINT_CENTRE + c;
                const int32_t cellRow = row - FOOTPRINT_CENTRE + r;
                const auto index = static_cast<int64_t>(cellRow) * cellsPerRow + cellCol;

                if (index >= 0 && index < static_cast<int64_t>(cellGrid.size()))
                {
                    cellGrid[static_cast<size_t>(index)] = object;
                }
            }
        }
    }
}

void EditorMap::insertSorted(std::list<EditorObject*>& list, EditorObject* object)
{
    const int32_t col = object->cellCol;
    const int32_t row = object->cellRow;
    auto place = list.begin();

    for (; place != list.end(); ++place)
    {
        const EditorObject* other = *place;

        if (row < other->cellRow || (other->cellRow == row && col < other->cellCol))
        {
            break;
        }
    }

    list.insert(place, object);
}

bool EditorMap::addMover(MoverObject* mover)
{
    if (!canPlace(*mover))
    {
        return false;
    }

    markCells(mover);
    insertSorted(movers, mover);
    return true;
}

bool EditorMap::addObject(EditorObject* object)
{
    if (!canPlace(*object))
    {
        return false;
    }

    const uint32_t id = object->id;
    markCells(object);

    if (objectIdClass(id) == OBJECT_CLASS_DROPZONE)
    {
        dropZones.push_back(object);
    }
    else
    {
        insertSorted(objects, object);
    }

    if (objectIdClass(id) == OBJECT_CLASS_TURRET)
    {
        turretCount++;
    }

    return true;
}

bool EditorMap::readElevations(CArchive& archive)
{
    const uint8_t newTexture = TileLibrary::getDefaultTexture();
    // The paved tiles, whose corner textures come from their neighbours once all are read.
    std::list<std::pair<int32_t, int32_t>> pavedTiles;
    const auto blocks = static_cast<int32_t>(static_cast<uint32_t>(maxRow) / ELV_BLOCK_DIVISOR);
    // Original behaviour: a tile whose shape isn't found keeps the textures and heights the last one left here.
    uint8_t textures[4] = {};
    uint8_t heights[4] = {};

    for (int32_t blockRow = 0; blockRow < blocks; blockRow++)
    {
        for (int32_t blockCol = 0; blockCol < blocks; blockCol++)
        {
            for (int32_t r = 0; r < MAP_BLOCK_TILES; r++)
            {
                const int32_t row = blockRow * MAP_BLOCK_TILES + r;
                int32_t col = blockCol * MAP_BLOCK_TILES;

                for (int32_t i = 0; i < MAP_BLOCK_TILES; i++, col++)
                {
                    BYTE elevationByte;
                    BYTE unused;
                    WORD skipped;
                    WORD tileWord;
                    WORD overlayWord;
                    archive >> elevationByte >> unused >> skipped >> tileWord >> overlayWord;
                    auto elevation = static_cast<uint8_t>(elevationByte - 1);
                    EditorTile* tile = getTile(col, row);
                    EditorTile* upLeft = getTile(col - 1, row - 1);
                    EditorTile* up = getTile(col, row - 1);
                    EditorTile* left = getTile(col - 1, row);

                    if (upLeft != nullptr)
                    {
                        upLeft->elevations[3] = elevation;
                    }

                    if (left != nullptr)
                    {
                        left->elevations[1] = elevation;
                    }

                    if (up != nullptr)
                    {
                        up->elevations[2] = elevation;
                    }

                    if (tile == nullptr)
                    {
                        continue;
                    }

                    const auto tileId = static_cast<int16_t>(tileWord);
                    tileLibrary->getTileShape(tileId, textures, heights);
                    elevation = static_cast<uint8_t>(elevation - heights[0]);

                    if (!isPavedTexture(textures[0]))
                    {
                        for (int32_t corner = 0; corner < 4; corner++)
                        {
                            tile->textures[corner] = textures[corner] == TEXTURE_RUFF
                                                         ? static_cast<uint8_t>(TEXTURE_GRASS)
                                                         : textures[corner];
                        }
                    }
                    else
                    {
                        if (textures[0] == TEXTURE_CONC)
                        {
                            tile->concIndex = 0;
                        }
                        else if (textures[0] == TEXTURE_CROP)
                        {
                            tile->cropIndex = 0;
                        }

                        // At the front: the second pass walks them last read first.
                        pavedTiles.emplace_front(col, row);
                        uint8_t corner1 = newTexture;
                        uint8_t corner0 = newTexture;

                        if (upLeft != nullptr)
                        {
                            neighbourTextures(*upLeft, textures);

                            if (!isPavedTexture(textures[3]))
                            {
                                corner1 = textures[3];
                            }

                            if (!isPavedTexture(textures[2]))
                            {
                                corner0 = textures[2];
                            }

                            if (corner1 == TEXTURE_RUFF)
                            {
                                corner1 = TEXTURE_GRASS;
                            }

                            if (corner0 == TEXTURE_RUFF)
                            {
                                corner0 = TEXTURE_GRASS;
                            }
                        }

                        tile->textures[1] = corner1;
                        tile->textures[0] = corner0;
                        uint8_t corner2 = newTexture;

                        if (left != nullptr)
                        {
                            neighbourTextures(*left, textures);

                            if (!isPavedTexture(textures[1]))
                            {
                                corner2 = textures[1];
                            }
                        }

                        tile->textures[2] = corner2;
                    }

                    for (int32_t corner = 0; corner < 4; corner++)
                    {
                        tile->elevations[corner] = heights[corner] + elevation;
                    }

                    tile->tileId = tileId;
                    std::fill(std::begin(tile->zoomArt), std::end(tile->zoomArt), nullptr);
                    const auto overlayId = static_cast<int16_t>(overlayWord);

                    if (overlayId != ELV_NO_OVERLAY)
                    {
                        int32_t kinds[4];
                        bool damaged = false;

                        if (tileLibrary->findOverlay(overlayId, kinds, &damaged))
                        {
                            tile->overlay = new MapOverlay(kinds, overlayId, damaged);
                        }
                    }

                    for (uint8_t& texture : tile->textures)
                    {
                        if (texture == TEXTURE_RUFF)
                        {
                            texture = TEXTURE_GRASS;
                        }
                    }
                }
            }
        }
    }

    for (const auto& [col, row] : pavedTiles)
    {
        EditorTile* tile = getTile(col, row);
        const EditorTile* below = getTile(col, row + 1);
        const EditorTile* right = getTile(col + 1, row);

        if (below != nullptr)
        {
            neighbourTextures(*below, textures);
            tile->textures[2] = textures[0];
            tile->textures[3] = textures[1];
        }
        else
        {
            tile->textures[3] = newTexture;
        }

        if (right != nullptr)
        {
            neighbourTextures(*right, textures);
            tile->textures[1] = textures[0];
            tile->textures[3] = textures[2];
        }
    }

    return true;
}

bool EditorMap::readObjects(int32_t count, int32_t blocksPerRow, CArchive& archive)
{
    const int32_t savedWidth = tileWorldWidth;
    const int32_t savedHeight = tileWorldHeight;
    tileWorldWidth = OBJECT_TILE_WIDTH;
    tileWorldHeight = OBJECT_TILE_WIDTH;
    const X87Extended halfCell = X87Extended(OBJECT_TILE_WIDTH) / X87Extended(MAP_CELLS_PER_TILE * 2);
    const float cellSize = (halfCell + halfCell).toFloat();
    // Original behaviour: the result is only ever set false; it stays uninitialized otherwise.
    bool result = true;
    // Original behaviour: an object on a vertex outside the map keeps the view position of the one before.
    int32_t vertexViewX = 0;
    int32_t vertexViewY = 0;

    for (int32_t i = 0; i < count; i++)
    {
        int16_t objectNum;
        int16_t offsetX;
        int16_t offsetY;
        int16_t vertex;
        int16_t block;
        BYTE damage;
        archive >> objectNum >> offsetX >> offsetY >> vertex >> block >> damage;
        uint32_t id = 0xffffffff;
        const std::vector<ObjectGroup>& groups = objectLibrary->objectGroups;

        // The last type with a frame of this FIT id wins, with its first such frame.
        for (size_t g = 0; g < groups.size(); g++)
        {
            for (size_t t = 0; t < groups[g].types.size(); t++)
            {
                const ObjectType& type = groups[g].types[t];

                for (size_t f = 0; f < type.frames.size(); f++)
                {
                    if (type.frames[f].fitId == objectNum)
                    {
                        id = makeObjectId(static_cast<uint32_t>(g), static_cast<uint32_t>(t), static_cast<uint32_t>(f),
                                          static_cast<uint32_t>(type.objectClass));
                        break;
                    }
                }
            }
        }

        if (id == 0xffffffff)
        {
            result = false;
            continue;
        }

        const int32_t vertexCol = vertex % MAP_BLOCK_TILES + block % blocksPerRow * MAP_BLOCK_TILES;
        const int32_t vertexRow = vertex / MAP_BLOCK_TILES + block / blocksPerRow * MAP_BLOCK_TILES;
        int32_t x;
        int32_t y;
        int32_t z;

        if (getVertex(vertexCol, vertexRow, &x, &y, &z))
        {
            mapToView(x, y, z, &vertexViewX, &vertexViewY);
        }

        viewToMap(vertexViewX + offsetX, vertexViewY + offsetY, &x, &y, &z);
        const auto cellCol = static_cast<int32_t>((X87Extended(x) / X87Extended(cellSize)).truncate());
        const auto cellRow = static_cast<int32_t>((X87Extended(y) / X87Extended(cellSize)).truncate());
        auto* object = new StaticObject((damage & 0xf) | id, cellCol, cellRow, 0);

        // The original leaves an object that can't be placed allocated; the port frees it.
        if (!addObject(object))
        {
            delete object;
        }
    }

    tileWorldHeight = savedHeight;
    tileWorldWidth = savedWidth;
    return result;
}

SingleMap::SingleMap(int32_t maxRow, int32_t maxCol) : EditorMap(maxRow, maxCol)
{
}

int32_t SingleMap::maxTurrets()
{
    return SINGLE_MAX_TURRETS;
}

int32_t SingleMap::maxDropZones()
{
    return SINGLE_MAX_DROP_ZONES;
}

int32_t SingleMap::maxPlayers()
{
    return SINGLE_MAX_PLAYERS;
}

MissionSettings SingleMap::settings() const
{
    return {artillerySmall, artilleryLarge,   artillerySensor, artilleryCamera,
            timeLimit,      playerForceLimit, resourcePoints,  noUller};
}

void SingleMap::setSettings(const MissionSettings& settings)
{
    artillerySmall = settings.artillerySmall;
    artilleryLarge = settings.artilleryLarge;
    artillerySensor = settings.artillerySensor;
    artilleryCamera = settings.artilleryCamera;
    timeLimit = settings.timeLimit;
    playerForceLimit = settings.playerForceLimit;
    resourcePoints = settings.resourcePoints;
    noUller = settings.noUller;
}

EditorObject* EditorMap::objectAtCell(int32_t col, int32_t row)
{
    if (!isCellInGrid(col, row))
    {
        return nullptr;
    }

    return cellGrid[static_cast<size_t>(maxRow * MAP_CELLS_PER_TILE * row + col)];
}

EditorObject* EditorMap::objectAt(int32_t col, int32_t row)
{
    if (!isCellInGrid(col, row))
    {
        return nullptr;
    }

    if (EditorObject* object = objectAtCell(col, row); object != nullptr)
    {
        return object;
    }

    EditorObject* below = objectAtCell(col + 1, row + 1);
    return below != nullptr && isMoverClass(objectIdClass(below->id)) ? below : nullptr;
}

EditorObject* EditorMap::moverAt(int32_t col, int32_t row)
{
    EditorObject* object = objectAt(col, row);
    return object != nullptr && isMoverClass(objectIdClass(object->id)) ? object : nullptr;
}

void EditorMap::blockVertex(const EditorObject& object, int32_t* block, int32_t* vertex) const
{
    const int32_t tileCol = object.cellCol / MAP_CELLS_PER_TILE;
    const int32_t tileRow = object.cellRow / MAP_CELLS_PER_TILE;
    const int32_t blockCol = tileCol / MAP_BLOCK_TILES;
    const int32_t blockRow = tileRow / MAP_BLOCK_TILES;
    const auto blocksPerRow = static_cast<int32_t>(static_cast<uint32_t>(maxRow + 1) / MAP_BLOCK_TILES);
    *block = blocksPerRow * blockRow + blockCol;
    *vertex = (tileRow - MAP_BLOCK_TILES * blockRow - blockCol) * MAP_BLOCK_TILES + tileCol;
}

void EditorMap::structureIndex(const EditorObject& object, int32_t* block, int32_t* vertex, int32_t* number) const
{
    blockVertex(object, block, vertex);
    *number = 0;

    for (const EditorObject* other : objects)
    {
        int32_t otherBlock;
        int32_t otherVertex;
        blockVertex(*other, &otherBlock, &otherVertex);

        if (otherBlock == *block && otherVertex == *vertex)
        {
            if (other == &object)
            {
                return;
            }

            (*number)++;
        }
    }
}

EditorObject* EditorMap::structureAt(long block, long vertex, long number) const
{
    long count = 0;

    for (EditorObject* object : objects)
    {
        int32_t objectBlock;
        int32_t objectVertex;
        blockVertex(*object, &objectBlock, &objectVertex);

        if (objectBlock == block && objectVertex == vertex)
        {
            if (count == number)
            {
                return object;
            }

            count++;
        }
    }

    return nullptr;
}

void EditorMap::cellToWorld(int32_t col, int32_t row, int32_t* x, int32_t* y) const
{
    using X = X87Extended;
    // A cell is 128/3 map units; the map's centre is its origin.
    constexpr double HALF_CELL = 0.5;
    constexpr double CELL_UNITS = 128.0 / 3.0;
    constexpr double TILE_UNITS = 128.0;
    const auto alongX = static_cast<int32_t>(((X(col) + X(HALF_CELL)) * X(CELL_UNITS)).truncate());
    *x = static_cast<int32_t>((X(alongX) - X((maxRow + 1) >> 1) * X(TILE_UNITS)).truncate());
    const auto alongY = static_cast<int32_t>(((X(row) + X(HALF_CELL)) * X(CELL_UNITS)).truncate());
    *y = -static_cast<int32_t>((X(alongY) - X((maxCol + 1) >> 1) * X(TILE_UNITS)).truncate());
}

void EditorMap::worldToCell(int32_t x, int32_t y, int32_t* col, int32_t* row) const
{
    // The original computes these in doubles and floor(); every step is exact.
    constexpr int32_t TILE_UNITS = 128;
    constexpr double CELLS_PER_UNIT = 3.0 / 128.0;
    const int32_t alongX = ((maxRow + 1) >> 1) * TILE_UNITS + x;
    *col = static_cast<int32_t>(std::floor(alongX * CELLS_PER_UNIT));
    const int32_t alongY = ((maxCol + 1) >> 1) * TILE_UNITS - y;
    *row = static_cast<int32_t>(std::floor(alongY * CELLS_PER_UNIT));
}

bool EditorMap::loadMissionData(DaoDatabase& database, long missionId)
{
    bool loaded = loadAssignments(database, missionId);

    for (Objective* objective : objectives)
    {
        delete objective;
    }

    objectives.clear();
    ObjectiveMainRecord record(&database);
    record.Open(dbOpenDynaset, nullptr, 0);
    CString filter;
    filter.Format("[MissionID] = %d", missionId);

    try
    {
        if (record.FindFirst(filter))
        {
            do
            {
                Objective* objective = loadObjective(database, record.m_MissionID, record.m_Objective, *this);

                // EB-069 fixed: the original sets the name and priority of a null objective (one that didn't load);
                // the port only notes the failure.
                if (objective == nullptr)
                {
                    loaded = false;
                }
                else
                {
                    objective->name = record.m_Description;
                    objective->primary = record.m_Priority != 1;
                    objectives.push_back(objective);
                    objective->side = record.m_Alignment;
                }
            } while (record.FindNext(filter));
        }
    }
    catch (DaoException* e)
    {
        CString format;
        format.LoadString(IDS_A_DATABASE_QUERY_FAILED);
        CString text;
        text.Format(format, static_cast<LPCTSTR>(e->m_strDescription));
        CString caption;
        caption.LoadString(IDS_MCEDITOR);
        MessageBoxA(nullptr, text, caption, MB_OK);
        e->Delete();
    }

    return loadStructureLinks(database, missionId) && loaded;
}

bool EditorMap::loadAssignments(DaoDatabase& database, long missionId)
{
    MissionRecordSet mission(&database);
    mission.Open(dbOpenDynaset, nullptr, 0);
    CString filter;
    filter.Format("[MissionID] = %ld", missionId);
    mission.FindFirst(filter);
    CString path = EditorDataPath();
    path += "\\Data\\Missions\\Warriors\\";
    CString warriorPath = path;
    warriorPath += mission.m_ScriptFileName;
    warriorPath += ".fit";
    // A warrior FIT that doesn't open leaves the stream failed: every orders read fails.
    std::ifstream warriors(static_cast<LPCTSTR>(warriorPath));
    UnitRecord unit(&database);
    CString sql;
    sql.Format("SELECT * FROM [MissionAssignments] WHERE [MissionID] = %ld ORDER BY [Alignment] DESC, "
               "[ForceGroup] ASC, [Role] ASC",
               missionId);

    // The record's position in cells: the coordinates are map units with the map's centre as origin, y up.
    const auto unitCell = [&](int32_t* col, int32_t* row)
    {
        const auto y = static_cast<int32_t>(unit.m_YCoordinate);
        const auto alongX =
            static_cast<int32_t>(((maxRow + 1) >> 1) * 128.0 + static_cast<int32_t>(unit.m_XCoordinate));
        *col = static_cast<int32_t>(std::floor(alongX * (3.0 / 128.0)));
        const auto alongY = static_cast<int32_t>(((maxCol + 1) >> 1) * 128.0 - y);
        *row = static_cast<int32_t>(std::floor(alongY * (3.0 / 128.0)));
    };

    try
    {
        unit.Open(dbOpenDynaset, sql, 0);
        unit.MoveFirst();
        // The variant of the last mech read, for a mech whose id doesn't give one.
        int32_t variant = 1;

        while (!unit.IsEOF())
        {
            int32_t col;
            int32_t row;
            unitCell(&col, &row);
            const int32_t side = unit.m_Alignment == "Clan" ? 1 : 2;
            MoverObject* mover;

            if (!unit.IsFieldNull(&unit.m_MechID))
            {
                char digits[0x20];
                _itoa(static_cast<int32_t>(unit.m_MechID), digits, 10);
                const char variantDigit = digits[3];
                const uint32_t frame = objectLibrary->findMoverFrame(static_cast<int32_t>(unit.m_MechID),
                                                                     static_cast<int32_t>(unit.m_Rotation));

                if (variantDigit == '1')
                {
                    variant = 0;
                }
                else if (variantDigit == '3')
                {
                    variant = 2;
                }
                else if (variantDigit == '4')
                {
                    variant = 1;
                }

                mover = new MechObject(frame, col, row, side, variant);
            }
            else
            {
                const uint32_t frame = objectLibrary->findMoverFrame(static_cast<int32_t>(unit.m_VehicleID),
                                                                     static_cast<int32_t>(unit.m_Rotation));
                mover = new VehicleObject(frame, col, row, side);
            }

            mover->forceGroup = static_cast<int32_t>(unit.m_ForceGroup - 1.0);
            mover->role = static_cast<int32_t>(unit.m_Role);
            mover->setPilot(database, static_cast<long>(unit.m_MWID));

            // Original behaviour (EB-072: the port deletes it): a unit that can't be placed stays allocated.
            if (!addMover(mover))
            {
                delete mover;
            }

            unit.MoveNext();
        }

        unit.MoveFirst();
        int32_t warrior = 1;

        while (!unit.IsEOF())
        {
            int32_t col;
            int32_t row;
            unitCell(&col, &row);
            auto* mover = static_cast<MoverObject*>(moverAt(col, row));
            UnitBrain* brain = readBrain(unit.m_BrainFile, warriors, *this, warrior);

            // EB-073 fixed: the original stores the orders in the unit on the record's cell without checking there is
            // one, and keeps a null when the orders don't read (drawing then calls through it); the port skips a
            // missing unit and keeps a unit's orders when its new ones don't read.
            if (mover != nullptr && brain != nullptr)
            {
                delete mover->brain;
                mover->brain = brain;
            }
            else
            {
                delete brain;
            }

            warrior++;
            unit.MoveNext();
        }
    }
    catch (DaoException* e)
    {
        e->Delete();
    }

    return true;
}

bool EditorMap::loadStructureLinks(DaoDatabase& database, long missionId)
{
    StructureLink links(&database);
    links.Open(dbOpenDynaset, nullptr, 0);
    CString filter;
    filter.Format("[MissionID] = %ld", missionId);
    CString linkFilter;
    StructureRecord structures(&database);
    structures.Open(dbOpenDynaset, nullptr, 0);
    bool loaded = true;

    if (!structures.FindFirst(filter))
    {
        return loaded;
    }

    do
    {
        EditorObject* structure = structureAt(structures.m_Block, structures.m_Vertex, structures.m_Number);

        // EB-074 fixed: the original sets the side of a structure that isn't on the map through a null object; the
        // port counts it as not loaded.
        if (structure == nullptr)
        {
            loaded = false;
        }
        else
        {
            structure->side = structures.m_Alignment;
        }

        linkFilter.Format("[StructureID] = %ld", structures.m_StructureID);

        if (!links.FindFirst(linkFilter))
        {
            continue;
        }

        if (structure == nullptr)
        {
            loaded = false;
            continue;
        }

        ControlLink* link = nullptr;

        for (ControlLink* existing : controlLinks)
        {
            if (existing->isFor(*structure))
            {
                link = existing;
                break;
            }
        }

        // A new set is kept unless an equal one exists (the original then fills it and leaves it allocated; the port
        // fills it and frees it).
        bool keep = true;

        if (link == nullptr)
        {
            link = new ControlLink(*structure);

            for (const ControlLink* existing : controlLinks)
            {
                if (existing->equals(*link))
                {
                    keep = false;
                    break;
                }
            }

            if (keep)
            {
                controlLinks.push_back(link);
            }
        }

        do
        {
            EditorObject* target = structureAt(links.m_Block, links.m_Vertex, links.m_Number);

            if (target == nullptr)
            {
                loaded = false;
                continue;
            }

            if (ControlLink::controls(objectIdClass(structure->id), objectIdClass(target->id)))
            {
                link->addLinked(*target);

                // A power station's turret controls keep their own side.
                if (objectIdClass(target->id) != OBJECT_CLASS_TURRET_CONTROL)
                {
                    target->side = structures.m_Alignment;
                }
            }
        } while (links.FindNext(linkFilter));

        if (!keep)
        {
            delete link;
        }
    } while (structures.FindNext(filter));

    return loaded;
}

bool EditorMap::loadDropZones(DaoDatabase& database, long missionId)
{
    DropZoneRecord record(&database);
    record.Open(dbOpenDynaset, nullptr, 0);
    CString filter;
    filter.Format("[MissionID] = %ld", missionId);
    int32_t count = 0;

    if (record.FindFirst(filter))
    {
        do
        {
            int32_t col;
            int32_t row;
            worldToCell(record.m_PlayerEntryPointX, record.m_PlayerEntryPointY, &col, &row);
            // The first three are the second side's (drop zone frame 0), the rest the first side's (frame 1).
            const int32_t side = count < 3 ? 2 : 1;
            const uint32_t id = static_cast<uint32_t>((side != 2 ? 1 : 0) << 16) | OBJECT_CLASS_DROPZONE << 1;
            // The original's drop zones have a class of their own (0x004feb00) that adds nothing to a StaticObject.
            auto* zone = new StaticObject(id, col, row, side);

            // The original leaves a zone that can't be placed allocated; the port frees it.
            if (!addObject(zone))
            {
                delete zone;
            }

            count++;
        } while (record.FindNext(filter));
    }

    record.Close();
    return true;
}

bool SingleMap::loadMission(DaoDatabase& database, const CString& name)
{
    MissionRecordSet mission(&database);
    mission.Open(dbOpenDynaset, nullptr, 0);
    CString filter;
    filter.Format("[MissionName] = '%s'", static_cast<LPCTSTR>(name));

    if (!mission.FindFirst(filter))
    {
        return false;
    }

    artillerySensor = mission.m_ArtillerySensor;
    artilleryCamera = mission.m_ArtilleryCamera;
    artilleryLarge = mission.m_ArtilleryLarge;
    timeLimit = mission.m_TimeLimit;
    artillerySmall = mission.m_ArtillerySmall;
    playerForceLimit = mission.m_PlayerForceLimit;
    timeOfDay = MissionTimeOfDayOf(mission.m_TimeOfDay);
    weather = MissionWeatherOf(mission.m_Weather).value;
    const long missionId = mission.m_MissionID;
    mission.Close();

    const CString savePath = EditorDataPath() + "\\Data\\Savegame\\" + name + ".sol";
    CString textPath = EditorDataPath();
    textPath += "\\Data\\Missions\\";
    textPath += name + ".txt";
    missionText.read(textPath);

    constexpr int32_t SAVE_PACKETS = 0x32;
    constexpr std::streamsize LINE_SIZE = 0x100;
    PacketFile save;
    bool foundUller = false;

    // EB-075 fixed: the original reads the save's packets whether or not it opened; the port keeps the defaults.
    if (save.open(savePath, READ, SAVE_PACKETS) == NO_ERR)
    {
        // The two packets are FIT text: the purchase state, then the inventory.
        const auto packetText = [&](int32_t packet)
        {
            save.seekPacket(packet);
            std::string text(static_cast<size_t>(save.getPacketSize()), '\0');
            save.readPacket(packet, reinterpret_cast<uint8_t*>(text.data()));
            return std::istringstream(text);
        };

        std::istringstream points = packetText(0);
        char line[LINE_SIZE];

        while (!points.fail() && !points.bad())
        {
            points.getline(line, LINE_SIZE);

            if (std::strstr(line, "numPoints") != nullptr)
            {
                auto i = static_cast<ptrdiff_t>(std::strlen(line)) - 1;

                while (i >= 0 && (std::isdigit(static_cast<unsigned char>(line[i])) != 0 ||
                                  std::isspace(static_cast<unsigned char>(line[i])) != 0))
                {
                    i--;
                }

                resourcePoints = std::atol(line + i + 1);
            }
        }

        std::istringstream inventory = packetText(1);

        while (!inventory.fail() && !inventory.bad())
        {
            inventory.getline(line, LINE_SIZE);

            if (std::strstr(line, "Uller") != nullptr)
            {
                foundUller = true;
                break;
            }
        }
    }

    noUller = !foundUller;
    loadMissionData(database, missionId);
    loadDropZones(database, missionId);
    return true;
}

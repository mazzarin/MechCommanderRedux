// The Vulkan renderer's terrain (MCVulkanRenderer::TerrainLayer), with draw.pshader: the map's ground tiles from a mesh
// made once per map, one instance a cell, 6 vertices an instance. Each instance becomes the draw VFX_nTile_draw makes of
// the cell's tile (a resolved Draw, as instance.vshader passes on), so draw.pshader writes the pixels the software
// renderer writes for the same tile.
//
// A cell is the quad whose top-left corner is map vertex (row, col). Its tile is drawn at that vertex's screen point,
// which moves by whole pixels from vertex to vertex: (col - row) * stepX across, (row + col) * stepY - elevation *
// elevStep down, from the point of vertex (0, 0) this frame (origin). Only the terrain window's grid is drawn (the cells
// TerrainWindow::render draws; an instance each, row by row as the mesh holds them), not a cell whose four corners all
// lie outside the corner range (Vertex::clipped), and each tile is cut to the pane (clip).
//
// Haze: the corners the home side sees (the fog of war's flags, a byte per map vertex, read as ByteFlag::getFlag reads
// them) pick the tile's table: none with all four seen, the haze table for one to three, the fill (colour 0x10) for none
// or when everything is black.
//
// Port-only: with HD ground, a second pass (the seam pass, pane.z 1) draws every tile again, in the same order, through
// the blend pipeline (draw.pshader's KindTerrainSeam), its box SeamReach pixels wider: its colours at its pixels'
// smoothed coverage, so each tile fades into the ones drawn before it across their seams.
//
// Bindings (SDL GPU, SPIR-V): the fog's surface (sampled) and the mesh as storage buffers in space0, vertex uniforms in
// space1.

struct Image
{
    // Where the tile lies in the terrain atlas (xy) and its bounding box's size (zw).
    int4 atlas;
    // The tile's hot spot (xy: subtracted from the draw position); z: the HD atlas with its colours (1 or 2; 0 none), w
    // its place there in whole 4 x 4 blocks (x the low 16 bits, y the high).
    int4 hot;
    // Port-only (HD lighting's relief maps): x the relief atlas with its relief map (1 or 2; 0 none), y its place there
    // as hot.w's; zw unused.
    int4 relief;
    // x: the tile's width as its header gives it (a retail tile's pixels reach a column past it, so atlas.z can be one
    // more); yzw unused.
    int4 header;
};

// The fog of war's flags (red: 0xff where the home side sees the vertex).
Texture2D<float4> fogTex : register(t0, space0);
SamplerState fogSampler : register(s0, space0);
// Per cell: x the image (0xffffffff: none), y the elevations of its corners (top left, top right, bottom right, bottom
// left; a byte each), z how many of its map tile's movement cells can't be entered (port-only), w its eight neighbours'
// (port-only: MCTerrainMesh::CellBlockedRound).
StructuredBuffer<uint4> cells : register(t1, space0);
StructuredBuffer<Image> images : register(t2, space0);

#include "pixels.hlsli"

cbuffer Terrain : register(b0, space1)
{
    // The target window's size in pixels (xy), and the size its surface is drawn at (zw).
    float4 targetSize;
    // xy: the screen point of map vertex (0, 0) (pane pixels); z: the elevation step; w unused.
    int4 origin;
    // x, y: the step across and down a vertex; z, w: the map vertex of the mesh's first cell (row, col).
    int4 steps;
    // x: the mesh's cells a row; y: the HD atlas's scale (0: no HD atlas); z, w: the fog's columns and rows.
    int4 mesh;
    // The cells drawn: first row, first column, last row, last column (map vertices, inclusive).
    int4 grid;
    // A corner is clipped outside x minX..maxX, y minY..maxY (pane pixels).
    int4 corners;
    // The window pixels a tile may write: x0, y0, x1, y1.
    int4 clip;
    // x: everything filled; y, z, w: the table rows for one, two and three corners seen.
    uint4 haze;
    // The pane's corner in the window (xy); z: 1 in the seam pass; w: the relief atlases' scale (0: none).
    int4 pane;
};

struct Output
{
    float4 position : SV_Position;
    nointerpolation int4 rect : TEXCOORD0;
    nointerpolation int4 source : TEXCOORD1;
    nointerpolation uint4 op : TEXCOORD2;
    nointerpolation int4 span : TEXCOORD3;
    nointerpolation uint4 walk : TEXCOORD4;
    nointerpolation uint4 ground : TEXCOORD5;
};

static const float2 quad[6] = {
    float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
    float2(0.0, 1.0), float2(1.0, 0.0), float2(1.0, 1.0),
};

// draw.pshader's kinds and flags.
static const uint KindTexture = 2;
static const uint KindTerrainSeam = 15;
// draw.pshader's HD terrain fields in op.w: the HD scale in the low bits, then the relief map's scale and its place in
// whole blocks (x, y).
static const uint ReliefScaleShift = 4;
static const uint ReliefXShift = 8;
static const uint ReliefYShift = 20;
static const uint TableBefore = 1u << 7;
static const uint UseColor = 1u << 10;
static const uint HdColors = 1u << 16;
// How far the seam pass reaches past a tile's box (window pixels): as far as its B-spline's coverage reaches.
static const int SeamReach = 2;
// Port-only (HD weather's puddles and snow): with a relief map, where the cell lies on the window and how much of its
// map tile and its neighbours' can't be entered (cliffs), for draw.pshader's BlockedAt (its ground): x the top corner
// (vertex (row, col)) on the window, its x in the low 16 bits and y in the high, signed; y how far below it the right
// corner (row, col + 1) lies, then the left one (row + 1, col); z how far below it the bottom one lies, the step across
// from it to the right one (from GroundStepShift, GroundStepMask), how many of its corners the fog of war leaves seen
// where it's drawn through a haze table (1 to 3, from GroundFogShift; 0 where all are: draw.pshader keeps it for the
// composite's FogEdgeShade) and the tile's own count of movement cells that can't be entered (from GroundOwnShift); w
// the neighbours' counts, as the cell's w holds them.
static const uint GroundStepShift = 16;
static const uint GroundStepMask = 0x3ffu;
static const uint GroundFogShift = 26;
static const uint GroundOwnShift = 28;
static const uint MovementCells = 9;

// The screen point of map vertex (row, col) at elevation level e.
int2 Point(int row, int col, uint e)
{
    return origin.xy + int2((col - row) * steps.x, (row + col) * steps.y - (int)e * origin.z);
}

bool Clipped(int2 p)
{
    return p.x < corners.x || p.x > corners.y || p.y < corners.z || p.y > corners.w;
}

// ByteFlag::getFlag on the fog for the vertex at (row, col), as Vertex::posTile gives them (the column's low 16 bits).
uint Seen(int row, int col)
{
    const uint r = (uint)row;
    const uint c = (uint)col & 0xffffu;
    const uint columns = (uint)mesh.z;
    const uint rows = (uint)mesh.w;

    if (r >= rows || c > columns)
    {
        return 0;
    }

    const uint index = columns * r + c;

    if (index >= columns * rows)
    {
        return 0;
    }

    const float flag = fogTex.SampleLevel(fogSampler, (float2(index % columns, index / columns) + 0.5) /
                                                          float2(columns, rows), 0).r;
    return (uint)round(flag * 255.0) == 255u ? 1u : 0u;
}

Output Nothing()
{
    Output output;
    output.position = float4(2.0, 2.0, 0.0, 1.0);
    output.rect = int4(0, 0, -1, -1);
    output.source = int4(0, 0, 0, 0);
    output.op = uint4(0, 0, 0, 0);
    output.span = int4(0, 0, 0, 0);
    output.walk = uint4(0, 0, 0, 0);
    output.ground = uint4(0, 0, 0, 0);
    return output;
}

Output main(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    // The grid's cells in the mesh, an instance each.
    const int firstRow = max(grid.x, steps.z);
    const int firstCol = max(grid.y, steps.w);
    const uint columns = (uint)(min(grid.w, steps.w + mesh.x - 1) - firstCol + 1);
    const int row = firstRow + (int)(instance / columns);
    const int col = firstCol + (int)(instance % columns);
    const uint index = (uint)(row - steps.z) * (uint)mesh.x + (uint)(col - steps.w);
    const uint4 cell = cells[index];

    if (cell.x == 0xffffffffu || row < grid.x || col < grid.y || row > grid.z || col > grid.w)
    {
        return Nothing();
    }

    const uint4 e = uint4(cell.y & 255u, (cell.y >> 8) & 255u, (cell.y >> 16) & 255u, cell.y >> 24);
    const int2 topLeft = Point(row, col, e.x);

    if (Clipped(topLeft) && Clipped(Point(row, col + 1, e.y)) && Clipped(Point(row + 1, col + 1, e.z)) &&
        Clipped(Point(row + 1, col, e.w)))
    {
        return Nothing();
    }

    // The tile's box in the window (wider in the seam pass), cut to the pane.
    const Image image = images[cell.x];
    const bool seamPass = pane.z != 0;
    const int reach = seamPass ? SeamReach : 0;
    const int2 at = pane.xy + topLeft - image.hot.xy;

    // VFX_nTile_draw counts a tile as off the pane when its header's box ends left of it, so a retail tile whose last
    // column (one past that box) falls on the pane's first isn't drawn there.
    if (!seamPass && at.x <= clip.x - image.header.x)
    {
        return Nothing();
    }

    const int4 box = int4(at - reach, at + image.atlas.zw - 1 + reach);
    const int4 cut = int4(max(box.xy, clip.xy), min(box.zw, clip.zw));

    if (cut.z < cut.x || cut.w < cut.y)
    {
        return Nothing();
    }

    uint flags = seamPass ? KindTerrainSeam : KindTexture;
    uint color = 0;
    uint before = 0;
    uint seen = 0;

    if (haze.x == 0)
    {
        seen = Seen(row, col) + Seen(row, col + 1) + Seen(row + 1, col + 1) + Seen(row + 1, col);
    }

    if (haze.x != 0 || seen == 0)
    {
        flags |= UseColor;
        color = 0x10;
    }
    else if (seen != 4)
    {
        flags |= TableBefore;
        before = seen == 1 ? haze.y : (seen == 2 ? haze.z : haze.w);
    }

    // The drawn pixels showing the rectangle (instance.vshader's rule).
    Output output;
    output.position = float4(DrawnCorner(cut, max(int4(targetSize), 1), quad[vertex]), 0.0, 1.0);
    output.rect = cut;
    output.source = int4(image.atlas.xy + cut.xy - at, 1, 1);
    output.op = uint4(flags, color, before, 0);
    // The seam pass reads the tile's place in the atlas (walk.zw) and its size (span.xy).
    output.span = seamPass ? int4(image.atlas.zw, 0, 0) : int4(0, 0, 0, 0);
    output.walk = seamPass ? uint4(0, 0, (uint)image.atlas.x, (uint)image.atlas.y) : uint4(0, 0, 0, 0);
    output.ground = uint4(0, 0, 0, 0);

    // The HD art pack's colours for the tile (draw.pshader's HdColors): the scale, and the tile's place to stay in.
    if (mesh.y != 0 && image.hot.z != 0 && (flags & UseColor) == 0)
    {
        // The tile's HD place (walk.xy), its place in the atlas (walk.zw), its size and HD atlas (span.xyz); its relief
        // map's atlas (span.w), and its scale and place in op.w's bits above the HD scale.
        const uint packed = (uint)image.hot.w;
        const uint relief = pane.w != 0 ? (uint)image.relief.x : 0u;
        const uint reliefAt = (uint)image.relief.y;
        const uint fields = relief != 0 ? (uint)mesh.y | (uint)pane.w << ReliefScaleShift |
                                              (reliefAt & 0xffffu) << ReliefXShift | (reliefAt >> 16) << ReliefYShift
                                        : (uint)mesh.y;
        output.op = uint4(flags | HdColors, color, before, fields);
        output.walk = uint4((packed & 0xffffu) * 4u, (packed >> 16) * 4u, (uint)image.atlas.x, (uint)image.atlas.y);
        output.span = int4(image.atlas.zw, image.hot.z - 1, (int)relief);

        if (relief != 0)
        {
            // Where the cell lies on the window, and the counts that can't be entered round it (BlockedAt's ground).
            const int2 top = pane.xy + topLeft;
            const int right = Point(row, col + 1, e.y).y - topLeft.y;
            const int left = Point(row + 1, col, e.w).y - topLeft.y;
            const int bottom = Point(row + 1, col + 1, e.z).y - topLeft.y;
            const uint fogEdge = (flags & TableBefore) != 0 ? seen : 0u;
            output.ground = uint4((uint)top.x & 0xffffu | (uint)top.y << 16, (uint)right & 0xffffu | (uint)left << 16,
                                  (uint)bottom & 0xffffu | ((uint)steps.x & GroundStepMask) << GroundStepShift |
                                      fogEdge << GroundFogShift | min(cell.z, MovementCells) << GroundOwnShift,
                                  cell.w);
        }
    }

    return output;
}

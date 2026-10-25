// The Vulkan renderer's terrain (MCVulkanRenderer::TerrainLayer), with draw.pshader: the map's ground tiles from a mesh
// made once per map, one instance a cell, 6 vertices an instance. Each instance becomes the draw VFX_nTile_draw makes of
// the cell's tile (a resolved Draw, as instance.vshader passes on), so draw.pshader writes the pixels the software
// renderer writes for the same tile.
//
// A cell is the quad whose top-left corner is map vertex (row, col). Its tile is drawn at that vertex's screen point,
// which moves by whole pixels from vertex to vertex: (col - row) * stepX across, (row + col) * stepY - elevation *
// elevStep down, from the point of vertex (0, 0) this frame (origin). Only the terrain window's grid is drawn (the cells
// TerrainWindow::render draws), not a cell whose four corners all lie outside the corner range (Vertex::clipped), and
// each tile is cut to the pane (clip).
//
// Haze: the corners the home side sees (the fog of war's flags, a byte per map vertex, read as ByteFlag::getFlag reads
// them) pick the tile's table: none with all four seen, the haze table for one to three, the fill (colour 0x10) for none
// or when everything is black.
//
// Bindings (SDL GPU, SPIR-V): the fog's surface (sampled) and the mesh as storage buffers in space0, vertex uniforms in
// space1.

struct Image
{
    // Where the tile lies in the terrain atlas (xy) and its bounding box's size (zw).
    int4 atlas;
    // The tile's hot spot (xy: subtracted from the draw position); z: 1 when the HD atlas has its colours.
    int4 hot;
};

// The fog of war's flags (red: 0xff where the home side sees the vertex).
Texture2D<float4> fogTex : register(t0, space0);
SamplerState fogSampler : register(s0, space0);
// Per cell: x the image (0xffffffff: none), y the elevations of its corners (top left, top right, bottom right, bottom
// left; a byte each).
StructuredBuffer<uint2> cells : register(t1, space0);
StructuredBuffer<Image> images : register(t2, space0);

cbuffer Terrain : register(b0, space1)
{
    // The target window's size in pixels (xy), and the size its surface is drawn at (zw).
    float4 targetSize;
    // xy: the screen point of map vertex (0, 0) (pane pixels); z: the elevation step; w: the first cell drawn.
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
    // The pane's corner in the window (xy).
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
};

static const float2 quad[6] = {
    float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
    float2(0.0, 1.0), float2(1.0, 0.0), float2(1.0, 1.0),
};

// draw.pshader's kinds and flags.
static const uint KindTexture = 2;
static const uint TableBefore = 1u << 7;
static const uint UseColor = 1u << 10;
static const uint HdColors = 1u << 16;

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
    return output;
}

Output main(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    const uint index = (uint)origin.w + instance;
    const int row = steps.z + (int)(index / (uint)mesh.x);
    const int col = steps.w + (int)(index % (uint)mesh.x);
    const uint2 cell = cells[index];

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

    // The tile's box in the window, cut to the pane.
    const Image image = images[cell.x];
    const int2 at = pane.xy + topLeft - image.hot.xy;
    const int4 box = int4(at, at + image.atlas.zw - 1);
    const int4 cut = int4(max(box.xy, clip.xy), min(box.zw, clip.zw));

    if (cut.z < cut.x || cut.w < cut.y)
    {
        return Nothing();
    }

    uint flags = KindTexture;
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

    // The rectangle in the window's pixels, scaled to the size the surface is drawn at and grown by a drawn pixel each
    // way when that differs (instance.vshader's rule).
    const float2 corner = quad[vertex];
    const float2 pixel = float2(cut.xy) + corner * float2(cut.zw - cut.xy + 1);
    float2 ndc = pixel / targetSize.xy * 2.0 - 1.0;

    if (any(targetSize.zw != targetSize.xy))
    {
        ndc += (corner * 2.0 - 1.0) * 2.0 / targetSize.zw;
    }

    ndc.y = -ndc.y;

    Output output;
    output.position = float4(ndc, 0.0, 1.0);
    output.rect = cut;
    output.source = int4(image.atlas.xy + cut.xy - at, 1, 1);
    output.op = uint4(flags, color, before, 0);
    output.span = int4(0, 0, 0, 0);
    output.walk = uint4(0, 0, 0, 0);

    // The HD art pack's colours for the tile (draw.pshader's HdColors): the scale, and the tile's place to stay in.
    if (mesh.y != 0 && image.hot.z != 0 && (flags & UseColor) == 0)
    {
        output.op = uint4(flags | HdColors, color, before, (uint)mesh.y);
        output.walk = uint4(image.atlas);
    }

    return output;
}

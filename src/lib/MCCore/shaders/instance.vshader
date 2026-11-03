// The Vulkan renderer's draws (MCVulkanRenderer), with draw.pshader: one rectangle of the target per instance, made
// from the vertex number (6 vertices an instance, no vertex buffer). Each instance is a resolved draw command (struct
// Draw), passed on to draw.pshader unchanged, which works out every pixel from it.
//
// Draws are in the window's pixels. A surface shown scaled (the world view, drawn at the size it's shown) is drawn at
// another size: the quad covers the drawn pixels whose window pixel lies in the rectangle (pixels.hlsli's DrawnCorner).
//
// Bindings (SDL GPU, SPIR-V): the draws as a storage buffer in space0, vertex uniforms in space1.

struct Draw
{
    // The rectangle drawn, in target pixels, inclusive: x0, y0, x1, y1.
    int4 rect;
    // Where the source pixel of (x0, y0) lies (xy), and the source's step per target pixel across and down (zw).
    int4 source;
    // x: kind and flags, y: colour (bits 0-7) and key (bits 8-15), z: the table before, w: the table after.
    uint4 op;
    // Spans: a Gouraud span's colour and slope; a texel walk's offset, its step, and what a u or v carry adds.
    int4 span;
    // A texel walk's u and v fractions and their steps (16 bits each).
    uint4 walk;
};

StructuredBuffer<Draw> draws : register(t0, space0);

#include "pixels.hlsli"

cbuffer Batch : register(b0, space1)
{
    // The target window's size in pixels (xy), and the size its surface is drawn at (zw).
    float4 targetSize;
    // x: the batch's first draw in the buffer.
    uint4 batch;
};

struct Output
{
    float4 position : SV_Position;
    nointerpolation int4 rect : TEXCOORD0;
    nointerpolation int4 source : TEXCOORD1;
    nointerpolation uint4 op : TEXCOORD2;
    nointerpolation int4 span : TEXCOORD3;
    nointerpolation uint4 walk : TEXCOORD4;
    // draw.pshader's ground (terrain.vshader's alone): none.
    nointerpolation uint4 ground : TEXCOORD5;
};

static const float2 corners[6] = {
    float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
    float2(0.0, 1.0), float2(1.0, 0.0), float2(1.0, 1.0),
};

Output main(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    const Draw draw = draws[batch.x + instance];

    Output output;
    output.position = float4(DrawnCorner(draw.rect, max(int4(targetSize), 1), corners[vertex]), 0.0, 1.0);
    output.rect = draw.rect;
    output.source = draw.source;
    output.op = draw.op;
    output.span = draw.span;
    output.walk = draw.walk;
    output.ground = uint4(0, 0, 0, 0);
    return output;
}

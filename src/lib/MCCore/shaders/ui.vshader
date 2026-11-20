// Port-only (SPEC-menus.md): the remade menus' triangles (platform/MCUiGpu), drawn over the window: each vertex two
// float4s in a storage buffer (no vertex buffer), its place and texture place, then its premultiplied colour. Its
// place is already on a whole window pixel.
//
// Bindings (SDL GPU, SPIR-V): vertex storage buffers in space0, vertex uniforms in space1.

StructuredBuffer<float4> vertices : register(t0, space0);

cbuffer Ui : register(b0, space1)
{
    // The target's size in pixels (xy); zw unused.
    float4 targetSize;
    // x: the draw's first vertex in the buffer; yzw unused.
    uint4 batch;
};

struct Output
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};

Output main(uint vertex : SV_VertexID)
{
    const uint at = (batch.x + vertex) * 2;
    const float4 place = vertices[at];
    float2 ndc = place.xy / targetSize.xy * 2.0 - 1.0;
    // Target rows go down; NDC's y goes up.
    ndc.y = -ndc.y;
    Output output;
    output.position = float4(ndc, 0.0, 1.0);
    output.uv = place.zw;
    output.color = vertices[at + 1];
    return output;
}

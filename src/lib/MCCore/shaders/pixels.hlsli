// The window pixels a scaled surface's drawn pixels show (draw.pshader's rule), for the draw shaders and their vertex
// shaders.
//
// Port-only: on the surface's drawn grid (MCDrawnGrid), offset r: drawn pixel p shows window place
// ((2p + 1) W - 2r) / 2D, so a surface whose terrain layer puts the map's corner on a whole drawn pixel moves by whole
// drawn pixels as the view pans. r is 0 where a window pixel is a whole number of drawn pixels and on every surface
// without a terrain layer, and within (-W/2, W/2] (so a drawn pixel's window pixel lies in the window).
//
// GPUs have no integer divide (theirs is dozens of instructions, which the world's shaders spent on every pixel: a
// tenth of a frame, Xcode's shader profiler showed), so it's kept out of the pixels where it can be. The vertex shaders
// draw only the drawn pixels whose window pixel lies in the draw's rectangle (DrawnCorner), so no pixel tests that;
// only the original's kinds that read the window pixel work it out; and a surface drawn at its window's size (all but
// the world view, and every source that isn't a surface) needs no divide.

// The window pixel under drawn pixel p's centre, with sizes s (window xy, drawn zw) and the grid's offset r (never
// past W/2: the sum isn't below 0 for any drawn pixel).
int2 WindowPixel(int2 p, int4 s, int2 r)
{
    if (all(s.xy == s.zw))
    {
        return p;
    }

    return int2(uint2((2 * p + 1) * s.xy - 2 * r) / (2u * uint2(s.zw)));
}

// The drawn pixel holding window pixel t's centre (the first, where a drawn pixel is bigger than a window pixel and
// the offset would take the window's first pixel before it).
int2 DrawnPixel(int2 t, int4 s, int2 r)
{
    if (all(s.xy == s.zw))
    {
        return t;
    }

    return int2(uint2(max((2 * t + 1) * s.zw + 2 * r, 0)) / (2u * uint2(s.xy)));
}

// n / d rounded down, for d > 0 (integer division rounds toward zero).
int2 FloorDivide(int2 n, int2 d)
{
    return (n - int2(n < 0) * (d - 1)) / d;
}

// A corner (0 or 1 across and down) of the quad over the drawn pixels whose window pixel (WindowPixel) lies in window
// pixels rect (x0, y0, x1, y1, inclusive), in NDC, on a target of sizes s (window xy, drawn zw) and grid offset r:
// those t with 2 * x0 * drawn <= (2t + 1) * window - 2r < 2 * (x1 + 1) * drawn, and the same down. None when there are
// none.
float2 DrawnCorner(int4 rect, int4 s, int2 r, float2 corner)
{
    const int2 first = -FloorDivide(s.xy - 2 * rect.xy * s.zw - 2 * r, 2 * s.xy);
    const int2 last = FloorDivide(2 * (rect.zw + 1) * s.zw - 1 - s.xy + 2 * r, 2 * s.xy);
    const float2 from = float2(first);
    const float2 to = float2(max(last + 1, first));
    float2 ndc = (from + corner * (to - from)) / float2(s.zw) * 2.0 - 1.0;
    // Target rows go down; NDC's y goes up.
    ndc.y = -ndc.y;
    return ndc;
}

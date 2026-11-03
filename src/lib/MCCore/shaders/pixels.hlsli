// The window pixels a scaled surface's drawn pixels show (draw.pshader's rule), for the draw shaders and their vertex
// shaders.
//
// GPUs have no integer divide (theirs is dozens of instructions, which the world's shaders spent on every pixel: a
// tenth of a frame, Xcode's shader profiler showed), so it's kept out of the pixels where it can be. The vertex shaders
// draw only the drawn pixels whose window pixel lies in the draw's rectangle (DrawnCorner), so no pixel tests that;
// only the original's kinds that read the window pixel work it out; and a surface drawn at its window's size (all but
// the world view, and every source that isn't a surface) needs no divide.

// The window pixel under drawn pixel p's centre, with sizes s (window xy, drawn zw).
int2 WindowPixel(int2 p, int4 s)
{
    if (all(s.xy == s.zw))
    {
        return p;
    }

    return int2((2u * uint2(p) + 1u) * uint2(s.xy) / (2u * uint2(s.zw)));
}

// The drawn pixel holding window pixel t's centre.
int2 DrawnPixel(int2 t, int4 s)
{
    if (all(s.xy == s.zw))
    {
        return t;
    }

    return int2((2u * uint2(t) + 1u) * uint2(s.zw) / (2u * uint2(s.xy)));
}

// n / d rounded down, for d > 0 (integer division rounds toward zero).
int2 FloorDivide(int2 n, int2 d)
{
    return (n - int2(n < 0) * (d - 1)) / d;
}

// A corner (0 or 1 across and down) of the quad over the drawn pixels whose window pixel (WindowPixel) lies in window
// pixels rect (x0, y0, x1, y1, inclusive), in NDC, on a target of sizes s (window xy, drawn zw): those t with
// 2 * x0 * drawn <= (2t + 1) * window < 2 * (x1 + 1) * drawn, and the same down. None when there are none.
float2 DrawnCorner(int4 rect, int4 s, float2 corner)
{
    const int2 first = -FloorDivide(s.xy - 2 * rect.xy * s.zw, 2 * s.xy);
    const int2 last = FloorDivide(2 * (rect.zw + 1) * s.zw - 1 - s.xy, 2 * s.xy);
    const float2 from = float2(first);
    const float2 to = float2(max(last + 1, first));
    float2 ndc = (from + corner * (to - from)) / float2(s.zw) * 2.0 - 1.0;
    // Target rows go down; NDC's y goes up.
    ndc.y = -ndc.y;
    return ndc;
}

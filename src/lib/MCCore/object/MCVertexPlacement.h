#pragma once

#include "lib/MCVector2D.h"
#include "lib/MCVector3D.h"

class MCCamera;

/// <summary>
/// The map tile of a terrain vertex: vertices and map tiles share one grid, so a vertex's row and column are its
/// tile's.
/// </summary>
struct MCVertexCell
{
    /// <summary>The map row.</summary>
    int32_t Row = 0;
    /// <summary>The map column.</summary>
    int32_t Col = 0;

    /// <summary>The vertex of <paramref name="vertexNumber"/> within terrain block <paramref name="blockNumber"/>.</summary>
    static MCVertexCell Of(int32_t blockNumber, int32_t vertexNumber);

    /// <summary>The world X of the vertex (its tile's corner).</summary>
    float WorldX() const;
    /// <summary>The world Y of the vertex.</summary>
    float WorldY() const;
    /// <summary>
    /// The elevation of the tile in meters (asserting, as each object's placement did twice, that the tile is on the
    /// map: once with <paramref name="what"/>, once with the shared message).
    /// </summary>
    float Elevation(std::string_view what) const;
    /// <summary>
    /// How many of the four corners of the vertex's square the home team sees: (row, col), (row + 1, col),
    /// (row + 1, col + 1) and (row, col + 1).
    /// </summary>
    int32_t VisibleCorners() const;
    /// <summary>Whether the home team sees any of the four corners.</summary>
    bool AnyCornerVisible() const { return VisibleCorners() != 0; }
};

/// <summary>
/// Port-only (OB-192 fixed): the screen place in <paramref name="camera"/>'s view of terrain vertex
/// <paramref name="vertexNumber"/> of block <paramref name="blockNumber"/>: the terrain walk's this frame
/// (MCCamera::VertexProject), else projected as the walk projects its first vertex, to the whole pixel.
/// </summary>
MCVector2D VertexScreenPlace(int32_t blockNumber, int32_t vertexNumber, const MCCamera& camera);

/// <summary>
/// The world position of an object set on terrain vertex <paramref name="vertexNumber"/> of block
/// <paramref name="blockNumber"/>: the vertex's corner, moved by the pixel offset (turned into the isometric grid's
/// 60-degree axes), at the terrain's elevation there.
/// </summary>
/// <remarks>
/// The buildings, tree buildings, terrain objects, trees, gates, walls and bridges all place themselves this way on
/// their first update; the float and double steps are the ones MCX.EXE's code took.
/// <paramref name="position"/> is the object's position so far (its height is what the terrain is asked with).
/// </remarks>
MCVector3D PlaceOnVertex(MCVector3D position, int32_t blockNumber, int32_t vertexNumber, int32_t pixelOffsetX,
                         int32_t pixelOffsetY);

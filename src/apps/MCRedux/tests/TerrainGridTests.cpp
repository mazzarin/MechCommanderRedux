#include "stdafx.h"
#include "MCTest.h"
#include "fixtures/MCTerrainGeometryScope.h"
#include "main/MCMissionGlobals.h"
#include "terrain/MCMapBlockManager.h"
#include "terrain/MCTerrain.h"
#include "terrain/MCTerrainWindow.h"

namespace
{
    constexpr int32_t BlockSide = 20;
    constexpr int32_t MapBlocks = 6;
    constexpr int32_t MapVertices = BlockSide * MapBlocks;
    constexpr float MetersPerVertex = 128.0f;

    /// <summary>
    /// A 6x6-block map (the retail maps' 20 vertices per block, 128 m per vertex) held in a block cache, with the
    /// terrain's geometry set for it. Every map vertex is tagged with its own index in <c>TextureData</c>; the off-map
    /// block with -1.
    /// </summary>
    class TerrainGridFixture
    {
    public:
        TerrainGridFixture() : _Geometry(BlockSide, MapBlocks, MetersPerVertex, 16.0f)
        {
            _SavedTurn = Turn;
            Turn = 2;
            std::vector<MCPrecompVertex> vertices(static_cast<size_t>(MapBlocks * MapBlocks + 1) * BlockSide *
                                                  BlockSide);

            for (int32_t y = 0; y < MapVertices; y++)
            {
                for (int32_t x = 0; x < MapVertices; x++)
                {
                    const size_t block = static_cast<size_t>((y / BlockSide) * MapBlocks + x / BlockSide);
                    vertices[block * BlockSide * BlockSide + (y % BlockSide) * BlockSide + x % BlockSide].TextureData =
                        static_cast<int16_t>(y * MapVertices + x);
                }
            }

            for (size_t i = 0; i < BlockSide * BlockSide; i++)
            {
                vertices[static_cast<size_t>(MapBlocks * MapBlocks) * BlockSide * BlockSide + i].TextureData = -1;
            }

            _Blocks = std::make_shared<MCMapBlockManager>(vertices, MapBlocks * MapBlocks);
        }

        ~TerrainGridFixture() { Turn = _SavedTurn; }

        TerrainGridFixture(const TerrainGridFixture&) = delete;
        TerrainGridFixture& operator=(const TerrainGridFixture&) = delete;

        /// <summary>
        /// Builds a <paramref name="side"/>-vertex grid for a camera over map vertex (<paramref name="cameraX"/>,
        /// <paramref name="cameraY"/>) and checks that every grid vertex is the map vertex it stands for, or an
        /// off-map one past the map's edge.
        /// </summary>
        void checkGrid(int32_t side, int32_t cameraX, int32_t cameraY)
        {
            MCTest::Scope scope(std::format("grid {} with the camera over vertex ({}, {})", side, cameraX, cameraY));
            MCTerrainWindow window(0, side);
            const MCVector3D camera(
                MCTerrain::MapTopLeft3d100.X + (static_cast<float>(cameraX) + 0.5f) * MetersPerVertex,
                MCTerrain::MapTopLeft3d100.Y - (static_cast<float>(cameraY) + 0.5f) * MetersPerVertex, 0.0f);
            _Blocks->Update(camera, window);
            _Blocks->BuildWindow(window);
            REQUIRE_EQ(window.Vertices.size(), static_cast<size_t>(side) * side);
            REQUIRE_EQ(window.Blocks.size(), static_cast<size_t>(side - 1) * (side - 1));

            // The grid's corner is half a grid before the camera's vertex, as the window's world corner says.
            const int32_t cornerX = cameraX - side / 2;
            const int32_t cornerY = cameraY - side / 2;
            CHECK_EQ(window.TopLeftX, MCTerrain::MapTopLeft3d100.X + static_cast<float>(cornerX) * MetersPerVertex);
            CHECK_EQ(window.TopLeftY, MCTerrain::MapTopLeft3d100.Y - static_cast<float>(cornerY) * MetersPerVertex);

            int32_t wrong = 0;

            for (int32_t row = 0; row < side; row++)
            {
                for (int32_t col = 0; col < side; col++)
                {
                    const int32_t x = cornerX + col;
                    const int32_t y = cornerY + row;
                    const bool onMap = x >= 0 && y >= 0 && x < MapVertices && y < MapVertices;
                    const int16_t expected = onMap ? static_cast<int16_t>(y * MapVertices + x) : int16_t{-1};

                    if (window.Cache.lock()
                            ->Vertex(window.Vertices[static_cast<size_t>(row) * side + col].MapVertex)
                            .TextureData != expected)
                    {
                        wrong++;
                    }
                }
            }

            CHECK_EQ(wrong, 0);
        }

    private:
        MCTerrainGeometryScope _Geometry;
        std::shared_ptr<MCMapBlockManager> _Blocks;
        int32_t _SavedTurn = 0;
    };

    /// <summary>
    /// Camera vertices across the map: the edges, each side of a block edge, and the middle. Not past column 114:
    /// there, in the last block column from its vertex 15 on, MCX.EXE's buildWindow steps the corner's block by one
    /// and wraps into the next block row (OB-102), so the whole grid is off. The camera's edge margin (about ten
    /// vertices) keeps it from getting there.
    /// </summary>
    constexpr int32_t CameraVertices[] = {0, 3, 14, 15, 19, 20, 24, 35, 39, 40, 59, 60, 85, 99, 100, 105, 110, 114};
}

TEST_CASE("terrain: the data's 30-vertex grid holds the map vertices around the camera")
{
    TerrainGridFixture fixture;

    for (int32_t y : CameraVertices)
    {
        for (int32_t x : CameraVertices)
        {
            fixture.checkGrid(30, x, y);
        }
    }
}

TEST_CASE("terrain: a grid grown for a larger screen holds the map vertices around the camera")
{
    TerrainGridFixture fixture;

    for (int32_t side : {32, 42, 64, 80})
    {
        for (int32_t y : CameraVertices)
        {
            for (int32_t x : CameraVertices)
            {
                fixture.checkGrid(side, x, y);
            }
        }
    }
}

TEST_CASE("terrain: a grid vertex's quads are those with it as a corner, in the grid's order")
{
    // A corner, an edge, the middle and a grown grid's sides.
    for (int32_t side : {2, 3, 30, 42})
    {
        const MCTerrainWindow window(0, side);

        for (uint32_t vertex = 0; vertex < window.Vertices.size(); vertex++)
        {
            std::vector<size_t> searched;

            for (size_t quad = 0; quad < window.Blocks.size(); quad++)
            {
                if (std::ranges::contains(window.Blocks[quad].Vertices, vertex))
                {
                    searched.push_back(quad);
                }
            }

            const std::array<std::optional<size_t>, 4> around = window.QuadsAround(vertex);
            std::vector<size_t> named;

            for (const std::optional<size_t> quad : around)
            {
                if (quad.has_value())
                {
                    named.push_back(*quad);
                }
            }

            CHECK(std::ranges::is_partitioned(around,
                                              [](const std::optional<size_t>& quad) { return quad.has_value(); }));
            CHECK(named == searched);
        }
    }
}

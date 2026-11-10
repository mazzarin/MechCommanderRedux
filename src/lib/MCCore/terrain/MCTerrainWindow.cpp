#include "stdafx.h"
#include "terrain/MCTerrainWindow.h"
#include "ai/MCMoveSystem.h"
#include "camera/MCCamera.h"
#include "engine/MCByteFlag.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCLineElement.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFatal.h"
#include "mission/MCScenario.h"
#include "object/MCElemental.h"
#include "object/MCElementalType.h"
#include "object/MCElementalGameSystem.h"
#include "platform/MCRenderer.h"
#include "terrain/MCGroundMesh.h"
#include "terrain/MCMapBlockManager.h"
#include "terrain/MCTerrain.h"
#include "vfx/MCVfx.h"

namespace
{
    /// <summary>Depth of the debug grid's lines.</summary>
    constexpr int32_t GridLineDepth = -78000000;

    /// <summary>Projects a world point to the main camera's screen (the binary inlines this).</summary>
    MCVector2D EyeProject(const MCVector3D& point)
    {
        const std::shared_ptr<MCCamera> eye = Eye();
        const MCVector3D relative = point - eye->Position;
        return MCVector2D(relative.X * eye->CosAngle + relative.Y * eye->CosAngle + eye->HalfWidth,
                          ((relative.X * eye->SinAngle + eye->HalfHeight) - relative.Y * eye->SinAngle) - relative.Z);
    }

    /// <summary>
    /// Adds the four sides of a quad to the element list: from <paramref name="p1"/> to <paramref name="p2"/> to
    /// <paramref name="p3"/> to <paramref name="p0"/>, and <paramref name="p1"/> to <paramref name="p0"/>.
    /// </summary>
    void AddGridQuad(MCVector2D& p0, MCVector2D& p1, MCVector2D& p2, MCVector2D& p3, int32_t color)
    {
        ElementList().Add(ElementList().Make<MCLineElement>(p1, p2, color, std::nullopt, GridLineDepth, -1));
        ElementList().Add(ElementList().Make<MCLineElement>(p2, p3, color, std::nullopt, GridLineDepth, -1));
        ElementList().Add(ElementList().Make<MCLineElement>(p3, p0, color, std::nullopt, GridLineDepth, -1));
        ElementList().Add(ElementList().Make<MCLineElement>(p1, p0, color, std::nullopt, GridLineDepth, -1));
    }

    /// <summary>
    /// Projects the corners of a <paramref name="width"/> x <paramref name="height"/> rectangle whose top-left is
    /// <paramref name="origin"/>, each dropped onto the terrain, and adds its outline.
    /// </summary>
    void AddTerrainQuad(MCVector3D origin, float width, float height, int32_t color)
    {
        MCVector3D point = origin;
        point.Z = TerrainElevationAt(point);
        MCVector2D p0 = EyeProject(point);
        point.X += width;
        point.Z = TerrainElevationAt(point);
        MCVector2D p1 = EyeProject(point);
        point.Y -= height;
        point.Z = TerrainElevationAt(point);
        MCVector2D p2 = EyeProject(point);
        point.X -= width;
        point.Z = TerrainElevationAt(point);
        MCVector2D p3 = EyeProject(point);
        AddGridQuad(p0, p1, p2, p3, color);
    }

    /// <summary>Whether (tileRow, tileCol) is on the map.</summary>
    bool TileOnMap(int32_t tileRow, int32_t tileCol)
    {
        return tileRow >= 0 && tileRow < GameMap().Height && tileCol >= 0 && tileCol < GameMap().Width;
    }

    /// <summary>
    /// The debug grid for one map tile: each of its 3x3 cells outlined when impassable (0xfd), when its second
    /// passability bit is clear (0xfc) and when path-locked (0xfe).
    /// </summary>
    void DrawCellGrid(const MCTerrainWindow& window, int32_t tileRow, int32_t tileCol, int32_t col, int32_t row,
                      float vertexStep)
    {
        const float cellSize = static_cast<float>(MCTerrain::MetersPerVertexDivMapcellDim - 5.0);
        Assert(TileOnMap(tileRow, tileCol), 0, " terrwindow:render MapTile Out of Bounds ");
        Assert(TileOnMap(tileRow, tileCol), 0, " Map Tile out of bounds ");
        const MCMapTile& tile = GameMap().Map[GameMap().Width * tileRow + tileCol];
        const uint32_t cells = tile.Cells;
        const uint32_t overlay = tile.Overlay;

        uint32_t cellShift = 0;
        uint32_t lockShift = 0;

        for (int32_t cellR = 0; lockShift < 9; cellR++)
        {
            for (int32_t cellC = 0; cellC < 3; cellC++)
            {
                const MCVector3D origin(
                    static_cast<float>(cellC) * cellSize + static_cast<float>(col) * vertexStep + window.TopLeftX,
                    (window.TopLeftY - static_cast<float>(row) * vertexStep) - static_cast<float>(cellR) * cellSize,
                    0.0f);

                if (((0x4000u << cellShift) & cells) >> (cellShift + 14) == 0)
                {
                    AddTerrainQuad(origin, cellSize, cellSize, 0xfd);
                }

                if (((0x8000u << cellShift) & cells) >> (cellShift + 15) == 0)
                {
                    AddTerrainQuad(origin, cellSize, cellSize, 0xfc);
                }

                if (((0x8000u << lockShift) & overlay) >> (lockShift + 15) != 0)
                {
                    AddTerrainQuad(origin, cellSize, cellSize, 0xfe);
                }

                cellShift += 2;
                lockShift++;
            }
        }
    }

    /// <summary>The debug grid's global-map doors on one map tile, outlined in 0xea along their length.</summary>
    void DrawDoors(const MCTerrainWindow& window, int32_t tileRow, int32_t tileCol, int32_t col, int32_t row,
                   float vertexStep)
    {
        const float cellSize = static_cast<float>(MCTerrain::MetersPerVertexDivMapcellDim - 5.0);
        Assert(TileOnMap(tileRow, tileCol), 0, " terrwindow:render MapTile Out of Bounds ");

        for (int32_t i = 0; i < GlobalMoveMap().NumDoors; i++)
        {
            const MCGlobalMapDoor& door = GlobalMoveMap().Doors[i];

            if (door.Row != tileRow || door.Col != tileCol)
            {
                continue;
            }

            int32_t lengthX = 1;
            int32_t lengthY = 1;

            if (door.Direction[0] == 1)
            {
                lengthY = door.Length;
            }

            if (door.Direction[0] == 2)
            {
                lengthX = door.Length;
            }

            const MCVector3D origin(
                static_cast<float>(door.CellC) * cellSize + static_cast<float>(col) * vertexStep + window.TopLeftX,
                (window.TopLeftY - static_cast<float>(row) * vertexStep) - static_cast<float>(door.CellR) * cellSize,
                0.0f);
            AddTerrainQuad(origin, static_cast<float>(lengthX) * cellSize, static_cast<float>(lengthY) * cellSize,
                           0xea);
        }
    }

    /// <summary>
    /// The camera's screen position of the terrain point <paramref name="point"/> and how far the view scrolled
    /// since the last frame (from the camera's projected corners).
    /// </summary>
    void ScreenFromCamera(const MCVector3D& point, MCVector2D& screen, int32_t& scrollX, int32_t& scrollY)
    {
        const std::shared_ptr<MCCamera> eye = Eye();
        const MCVector2D screen100 = MCTerrain::ProjectTerrain(point);
        screen.X = (screen100.X - eye->ScreenUL.X) + eye->HalfWidth;
        screen.Y = (screen100.Y - eye->ScreenUL.Y) + eye->HalfHeight;
        scrollX = static_cast<int32_t>(std::floor(eye->LastScreenUL.X - eye->ScreenUL.X));
        scrollY = static_cast<int32_t>(std::floor(eye->LastScreenUL.Y - eye->ScreenUL.Y));
    }
}

MCTerrainWindow::MCTerrainWindow(int32_t windowNum, int32_t side)
    : Vertices(static_cast<size_t>(side) * side), WindowNum(windowNum), _Side(side)
{
    Blocks.reserve(static_cast<size_t>(side - 1) * (side - 1));

    for (int32_t row = 0; row < side - 1; row++)
    {
        for (int32_t col = 0; col < side - 1; col++)
        {
            const auto topLeft = static_cast<uint32_t>(row * side + col);
            const auto rowStep = static_cast<uint32_t>(side);
            Blocks.emplace_back(topLeft, topLeft + 1, topLeft + rowStep + 1, topLeft + rowStep);
        }
    }
}

auto MCTerrainWindow::QuadsAround(uint32_t vertex) const -> std::array<std::optional<size_t>, 4>
{
    // The quad at (row, col) has its top-left corner at vertex row * side + col (the constructor's layout).
    const auto vertexRow = static_cast<int32_t>(vertex) / _Side;
    const auto vertexCol = static_cast<int32_t>(vertex) % _Side;
    const int32_t quadsSide = _Side - 1;
    std::array<std::optional<size_t>, 4> quads{};
    size_t found = 0;

    for (int32_t row = vertexRow - 1; row <= vertexRow; row++)
    {
        for (int32_t col = vertexCol - 1; col <= vertexCol; col++)
        {
            if (row >= 0 && col >= 0 && row < quadsSide && col < quadsSide)
            {
                quads[found++] = static_cast<size_t>(row * quadsSide + col);
            }
        }
    }

    return quads;
}

auto MCTerrainWindow::Bind(std::weak_ptr<MCCamera> cam) -> void
{
    Camera = std::move(cam);
    Update();
}

auto MCTerrainWindow::Release() -> void
{
    Camera.reset();
}

auto MCTerrainWindow::CameraIsActive() const -> bool
{
    const std::shared_ptr<MCCamera> camera = Camera.lock();
    return camera != nullptr && camera->Active;
}

auto MCTerrainWindow::Update() -> void
{
    const std::shared_ptr<MCTerrain> terrain = Terrain();

    if (Scenario()->AlwaysRevealed == 0)
    {
        terrain->ISVisibleBits->ResetAll(false);
    }

    if (Scenario()->GodMode != 0)
    {
        terrain->ISVisibleBits->ResetAll(true);
    }

    terrain->MapBlocks->Update(Camera.lock()->Position, *this);
    terrain->MapBlocks->BuildWindow(*this);
}

auto MCTerrainWindow::Render(int32_t hazeFactor) -> void
{
    const std::shared_ptr<MCTerrain> terrain = Terrain();
    const std::shared_ptr<MCCamera> camera = Camera.lock();
    const std::shared_ptr<const MCMapBlockManager> cache = Cache.lock();
    const float vertexStep = MCTerrain::MetersPerVertex;
    const float elevStep = MCTerrain::MetersPerElevLevel;
    int32_t col = 0;
    int32_t row = 0;
    std::ranges::fill(terrain->ScreenPosX, 0x11111111);

    auto vertex = Vertices.begin();
    const auto elevationOf = [&cache](const MCVertex& at) { return cache->Vertex(at.MapVertex).Elevation; };
    MCVector3D point(0.0f + TopLeftX, TopLeftY - 0.0f, 0.0f);
    MCTerrain::ForceRedraw = true;
    point.Z = static_cast<float>(static_cast<int32_t>(elevationOf(*vertex))) * MCTerrain::MetersPerElevLevel;

    // The map tile under the grid's corner (for the debug grid), kept on the map.
    int32_t tileCol = 0;
    int32_t tileRow = 0;
    const int32_t mapSide = MCTerrain::BlocksMapSide * MCTerrain::VerticesBlockSide;

    if (DrawTerrainGrid != 0)
    {
        const float gridX = std::floor(MCTerrain::OneOvermetersPerVertex * point.X) * MCTerrain::MetersPerVertex;
        const float gridY = std::floor(MCTerrain::OneOvermetersPerVertex * point.Y) * MCTerrain::MetersPerVertex;
        const int32_t vertexX = static_cast<int32_t>(std::floor(gridX * MCTerrain::OneOvermetersPerVertex));
        const int32_t vertexY = static_cast<int32_t>(std::floor(gridY * MCTerrain::OneOvermetersPerVertex));
        tileCol = std::clamp(vertexX + (mapSide >> 1), 0, mapSide - 1);
        tileRow = std::clamp((mapSide >> 1) - vertexY, 0, mapSide - 1);
    }

    const int32_t firstTileCol = tileCol;
    MCVector2D rowStart;
    // Port fix: the original leaves the scroll uninitialised when useOldProject is set.
    int32_t scrollX = 0;
    int32_t scrollY = 0;

    if (UseOldProject != 0)
    {
        const MCVector3D corner(0.0f + TopLeftX, TopLeftY - 0.0f, static_cast<float>(elevationOf(*vertex)) * elevStep);
        const MCVector3D relative = corner - camera->Position;
        rowStart.X = relative.X * camera->CosAngle + relative.Y * camera->CosAngle + camera->HalfWidth;
        rowStart.Y =
            ((relative.X * camera->SinAngle + camera->HalfHeight) - relative.Y * camera->SinAngle) - relative.Z;
    }

    // One vertex step across the grid moves the screen point by (stepX, stepY), plus the elevation change.
    const MCTerrainMeshSteps steps = MCTerrainScreenSteps(*camera);
    const auto elevScreenStep = static_cast<float>(steps.Up);
    const auto stepX = static_cast<float>(steps.Across);
    const auto stepY = static_cast<float>(steps.Down);

    if (UseOldProject == 0)
    {
        MCVector2D screen;
        ScreenFromCamera(point, screen, scrollX, scrollY);
        rowStart = screen;
    }

    float lastElevation = static_cast<float>(elevationOf(*vertex));
    float rowFirstElevation = lastElevation;
    const int32_t minX = static_cast<int32_t>(std::floor(-stepX));
    const int32_t minY = static_cast<int32_t>(std::floor(-stepY));
    const int32_t maxX = (GlobalPane->X1 - GlobalPane->X0) + static_cast<int32_t>(std::floor(stepX));
    const int32_t maxY = GlobalPane->Y1 - GlobalPane->Y0;
    MCVector2D current = rowStart;
    const auto numVertices = static_cast<int32_t>(Vertices.size());

    for (int32_t index = 0; index < numVertices; index++)
    {
        if (index != 0 && col != 0)
        {
            const float elevation = static_cast<float>(elevationOf(*vertex));
            current.X = current.X + stepX;
            current.Y = (lastElevation - elevation) * elevScreenStep + current.Y + stepY;
            lastElevation = elevation;
        }

        const int32_t px = static_cast<int32_t>(std::floor(current.X));
        vertex->Px = px;
        const int32_t py = static_cast<int32_t>(std::floor(current.Y));
        vertex->Py = py;
        const int16_t blockNum = vertex->BlockNum;
        const int16_t vertexNum = vertex->VertexNum;

        if (blockNum >= 0 && vertexNum >= 0 && blockNum < MCTerrain::TotalBlocks && vertexNum < VerticesPerBlock)
        {
            const int32_t screenIndex = terrain->BlockOffsets[blockNum] + vertexNum;
            terrain->ScreenPosX[screenIndex] = static_cast<int32_t>(std::floor(current.X));
            terrain->ScreenPosY[screenIndex] = static_cast<int32_t>(std::floor(current.Y));
        }

        // Every vertex is redrawn, its edges too: this render asked for a forced redraw before the walk, and nothing in
        // it takes that back, so the original's tests of the scroll (a scroll of a screen or more, the strips it
        // uncovered) could only ask again.
        vertex->Redraw = true;
        vertex->EdgeRedraw = true;
        vertex->Clipped = px < minX || px > maxX || py < minY || py > maxY;

        if (!vertex->Clipped)
        {
            terrain->MarkBlockUsed(blockNum);

            if (DrawTerrainGrid != 0)
            {
                DrawCellGrid(*this, tileRow, tileCol, col, row, vertexStep);
            }
        }

        if (DrawTerrainGrid != 0)
        {
            DrawDoors(*this, tileRow, tileCol, col, row, vertexStep);
        }

        col++;
        vertex++;
        tileCol++;

        if (col == _Side)
        {
            row++;
            tileRow++;
            col = 0;

            if (row != _Side)
            {
                lastElevation = static_cast<float>(elevationOf(*vertex));
            }

            rowStart.X = rowStart.X - stepX;
            const float rise = rowFirstElevation - lastElevation;
            rowFirstElevation = lastElevation;
            rowStart.Y = rise * elevScreenStep + rowStart.Y + stepY;
            tileCol = firstTileCol;
            current = rowStart;
        }

        tileCol = std::clamp(tileCol, 0, mapSide - 1);
        tileRow = std::clamp(tileRow, 0, mapSide - 1);
    }

    ElementList().OpenGroup(50000000, false);

    // Port: the GPU draws the pass from the map's ground mesh in one draw; the tiles below then only reach the software
    // renderer (when it draws too). Where the mesh can't stand for the tiles, that's an error.
    const std::shared_ptr<MCWindow> target = GlobalPane->Window.lock();
    const bool layer = target != nullptr && MCRenderer::Hardware() != nullptr &&
                       MCRenderer::GpuDrawing() != MCGpuDrawing::Off && MCRenderer::FrameSurfaceOf(*target) != nullptr;

    if (layer)
    {
        MCTerrainFrame ground;
        auto drawn = MCTerrainGroundFrame(*this, hazeFactor, static_cast<int32_t>(stepX), static_cast<int32_t>(stepY),
                                          static_cast<int32_t>(elevScreenStep), minX, maxX, minY, maxY, ground);

        if (drawn)
        {
            drawn = MCRenderer::For(*target).TerrainLayer(*target, ground);
        }

        if (!drawn)
        {
            Fatal(-1, drawn.error().substr(0, 240));
        }
    }

    for (MCTerrainBlock& block : Blocks)
    {
        block.Draw(Vertices, *cache, hazeFactor);
    }

    if (layer)
    {
        MCRenderer::For(*target).EndTerrainLayer(*target);
    }

    ElementList().OpenGroup(10000000, false);

    for (MCTerrainBlock& block : Blocks)
    {
        block.DrawOverlay(Vertices, *cache, hazeFactor);
    }

    MCTerrain::ForceRedraw = false;
}

auto MCTerrainWindow::DrawLines() -> void
{
    ElementList().OpenGroup(49990000, false);

    for (const MCTerrainBlock& block : Blocks)
    {
        block.DrawLine(Vertices, 0xff);
    }
}

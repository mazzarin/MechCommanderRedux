#include "stdafx.h"
#include "terrain/MCTerrain.h"
#include "ai/MCMoveSystem.h"
#include "camera/MCCamera.h"
#include "engine/MCByteFlag.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGamePaths.h"
#include "main/MCGameContext.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "object/MCObjectBlockManager.h"
#include "object/MCForces.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "terrain/MCGroundMesh.h"
#include "terrain/MCMapBlockManager.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrainTiles.h"
#include "camera/MCViewWindow.h"

int32_t MCTerrain::VerticesBlockSide = 0;
int32_t MCTerrain::BlocksMapSide = 0;
int32_t MCTerrain::TotalBlocks = 0;
int32_t MCTerrain::VisibleVerticesPerSide = 0;
float MCTerrain::MetersPerElevLevel = 0.0f;
float MCTerrain::MetersPerVertex = 0.0f;
float MCTerrain::OneOvermetersPerVertex = 0.0f;
float MCTerrain::OneOververticesBlockSide = 0.0f;
int32_t MCTerrain::VerticesMapSide = 0;
float MCTerrain::MetersPerVertexDivMapcellDim = 0.0f;
float MCTerrain::MetersBlockSide = 0.0f;
bool MCTerrain::ForceRedraw = false;
float MCTerrain::ProjectionSin = 0.0f;
float MCTerrain::ProjectionCos = 0.0f;
MCVector2D MCTerrain::MapTopLeft2d100;
MCVector3D MCTerrain::MapTopLeft3d100;

double MCTerrainGridReach = 0.0;
int32_t VerticesPerBlock = 0;
float WorldUnitsMapSide = 0.0f;

namespace
{
    /// <summary>The error of the first of <paramref name="values"/> that failed, if any.</summary>
    template <typename... T> std::optional<std::string> FirstError(const std::expected<T, std::string>&... values)
    {
        std::optional<std::string> error;
        ((error.has_value() || values.has_value() ? void() : void(error = values.error())), ...);
        return error;
    }

    /// <summary>
    /// Entry <paramref name="name"/> of <paramref name="file"/>'s current block, or what is wrong with it.
    /// </summary>
    template <typename T> std::expected<T, std::string> Need(MCFitIniFile& file, std::string_view name)
    {
        MCFitResult<T> value = file.Read<T>(name);

        if (!value.has_value())
        {
            return std::unexpected(
                std::format("no {} in the terrain file ({:#x})", name, static_cast<uint32_t>(value.error())));
        }

        return *value;
    }

    /// <summary>Whether (tileRow, tileCol) is on the map.</summary>
    bool TileOnMap(int32_t tileRow, int32_t tileCol)
    {
        return tileRow >= 0 && tileRow < GameMap().Height && tileCol >= 0 && tileCol < GameMap().Width;
    }

    /// <summary>
    /// Port: the grid's vertices a side for the largest screen. Every map's grid is 30 vertices, made for 640x480;
    /// zoomed out at 1280x1024 it no longer reaches the screen's corners (black, jagged edges, and a tactical map view
    /// box that jumps when a corner lands on no terrain). The grid is a diamond, so what it has to cover grows with
    /// width / cos + height / sin of the view angle. Kept even, like the data's. The screen follows the window, which
    /// can grow during the scenario, so the grid covers the largest desktop. Sets <see cref="MCTerrainGridReach"/>.
    /// </summary>
    int32_t GrownGridSide(int32_t dataVertices)
    {
        int32_t screenWidth = GuiSystem().Width();
        int32_t screenHeight = GuiSystem().Height();

        if (const std::shared_ptr<const MCDisplay> display = MCInput::Display(); display != nullptr)
        {
            int32_t largestWidth = 0;
            int32_t largestHeight = 0;
            display->LargestScreenSize(largestWidth, largestHeight);
            screenWidth = std::max(screenWidth, largestWidth);
            screenHeight = std::max(screenHeight, largestHeight);
        }

        const double sinAngle = std::sin(MCTerrainViewAngle);
        const double cosAngle = std::cos(MCTerrainViewAngle);
        // The world view's surface is at most 2160 tall (MCViewWindow::ZoomFurthest), at the widest desktop's aspect.
        const double widest = std::max(static_cast<double>(screenWidth) / screenHeight, 16.0 / 9.0);
        const double furthest = static_cast<double>(MCViewWindow::ZoomFurthest);
        const double needed = std::max(static_cast<double>(screenWidth) / cosAngle + screenHeight / sinAngle,
                                       furthest * widest / cosAngle + furthest / sinAngle);
        const double designed = 640.0 / cosAngle + 480.0 / sinAngle;
        int32_t side = dataVertices;
        const auto grown = static_cast<int32_t>(std::ceil(dataVertices * needed / designed));

        if (grown > side)
        {
            side = (grown + 1) & ~1;
        }

        MCTerrainGridReach = designed * side / dataVertices;
        return side;
    }
}

std::shared_ptr<MCTerrain> Terrain()
{
    return MCGameContext::Current().Terrain();
}

std::shared_ptr<MCTacticalMap> TacticalMap()
{
    const std::shared_ptr<MCTerrain> terrain = Terrain();
    return terrain != nullptr ? terrain->TacticalMap.Lock() : nullptr;
}

auto RevealAll() -> void
{
    if (Terrain() == nullptr)
    {
        return;
    }

    const MCVector3D origin(0.0f, 0.0f, 0.0f);
    const MCVector3D lookVector;
    Terrain()->MarkRadiusSeen(origin, lookVector, 360.0f, 10000.0f, 1);
}

MCTerrain::MCTerrain() = default;

MCTerrain::~MCTerrain()
{
    Unload();
}

auto MCTerrain::Unload() -> void
{
    MCTerrainForgetMesh();
    Tiles.reset();
    Windows.clear();
    MapBlocks.reset();
    ObjectBlocks.reset();
    ISVisibleBits.reset();
    ClanVisibleBits.reset();
    CloseGuiObject(TacticalMap);
}

auto MCTerrain::SetGeometry(int32_t verticesBlockSide, int32_t blocksMapSide, float metersPerVertex,
                            float metersPerElevLevel) -> void
{
    VerticesBlockSide = verticesBlockSide;
    BlocksMapSide = blocksMapSide;
    MetersPerVertex = metersPerVertex;
    MetersPerElevLevel = metersPerElevLevel;
    TotalBlocks = BlocksMapSide * BlocksMapSide;
    VerticesPerBlock = VerticesBlockSide * VerticesBlockSide;
    MetersBlockSide = static_cast<float>(VerticesBlockSide) * MetersPerVertex;
    WorldUnitsMapSide = static_cast<float>(BlocksMapSide) * MetersBlockSide;
    OneOvermetersPerVertex = 1.0f / MetersPerVertex;
    MetersPerVertexDivMapcellDim = MetersPerVertex * (1.0f / 3.0f);
    OneOververticesBlockSide = 1.0f / static_cast<float>(VerticesBlockSide);
    VerticesMapSide = (BlocksMapSide * VerticesBlockSide) >> 1;
    MapTopLeft3d100.X = WorldUnitsMapSide * -0.5f;
    MapTopLeft3d100.Y = WorldUnitsMapSide * 0.5f;
    MapTopLeft3d100.Z = 0.0f;
}

auto MCTerrain::SetTopLeftElevation(float elevation) -> void
{
    MapTopLeft3d100.Z = elevation;
    ProjectionSin = static_cast<float>(std::sin(MCTerrainViewAngle));
    ProjectionCos = static_cast<float>(std::cos(MCTerrainViewAngle));
    MapTopLeft2d100.X = MapTopLeft3d100.X * ProjectionCos + MapTopLeft3d100.Y * ProjectionCos;
    MapTopLeft2d100.Y = MapTopLeft3d100.X * ProjectionSin - MapTopLeft3d100.Y * ProjectionSin;
}

auto MCTerrain::Load(std::string_view fileName) -> std::expected<void, std::string>
{
    ClearUsedBlocks();
    Name = fileName;

    MCFitIniFile terrainFile;

    if (const int32_t result = terrainFile.Open(GamePath(TerrainPath, fileName, ".fit")); result != 0)
    {
        return std::unexpected(std::format("could not open {}.fit ({:#x})", fileName, static_cast<uint32_t>(result)));
    }

    if (terrainFile.SeekBlock("TerrainData") != 0)
    {
        return std::unexpected("no TerrainData block in the terrain file");
    }

    const auto verticesBlockSide = Need<int32_t>(terrainFile, "VerticesBlockSide");
    const auto blocksMapSide = Need<int32_t>(terrainFile, "BlocksMapSide");
    const auto metersPerElevLevel = Need<float>(terrainFile, "MetersPerElevLevel");
    const auto metersPerVertex = Need<float>(terrainFile, "MetersPerVertex");
    const auto visibleVertices = Need<int32_t>(terrainFile, "VisibleVerticesPerSide");
    const auto numWindows = Need<int32_t>(terrainFile, "NumberOfWindows");
    // The heap sizes are still read (and required), then ignored.
    const auto heapSize = Need<uint32_t>(terrainFile, "TerrainHeapSize");

    if (const std::optional<std::string> error =
            FirstError(verticesBlockSide, blocksMapSide, visibleVertices, numWindows);
        error.has_value())
    {
        return std::unexpected(*error);
    }

    if (const std::optional<std::string> error = FirstError(metersPerElevLevel, metersPerVertex); error.has_value())
    {
        return std::unexpected(*error);
    }

    if (!heapSize.has_value())
    {
        return std::unexpected(heapSize.error());
    }

    if (terrainFile.SeekBlock("TileData") != 0)
    {
        return std::unexpected("no TileData block in the terrain file");
    }

    const auto tileHeapSize = Need<uint32_t>(terrainFile, "TerrainTileHeapSize");

    if (!tileHeapSize.has_value())
    {
        return std::unexpected(tileHeapSize.error());
    }

    const auto tileFileName = Need<std::string>(terrainFile, "TerrainTileFile");

    if (!tileFileName.has_value())
    {
        return std::unexpected(tileFileName.error());
    }

    SetGeometry(*verticesBlockSide, *blocksMapSide, *metersPerVertex, *metersPerElevLevel);
    VisibleVerticesPerSide = GrownGridSide(*visibleVertices);

    const int32_t mapVertices = TotalBlocks * VerticesBlockSide * VerticesBlockSide;
    ScreenPosX.assign(static_cast<size_t>(mapVertices), 0x11111111);
    ScreenPosY.assign(static_cast<size_t>(mapVertices), 0);
    BlockOffsets.assign(static_cast<size_t>(TotalBlocks), 0);

    for (int32_t i = 0, offset = 0; i < TotalBlocks; i++, offset += VerticesPerBlock)
    {
        BlockOffsets[i] = offset;
    }

    std::expected<std::unique_ptr<MCTerrainTiles>, std::string> tiles = MCTerrainTiles::Create(*tileFileName);

    if (!tiles.has_value())
    {
        return std::unexpected(tiles.error());
    }

    Tiles = std::move(*tiles);
    Tiles->Preload(fileName);

    const auto flagSide = static_cast<uint32_t>(BlocksMapSide * VerticesBlockSide);
    ISVisibleBits = std::make_unique<MCByteFlag>(flagSide, flagSide, false);
    ClanVisibleBits = std::make_unique<MCByteFlag>(flagSide, flagSide, false);

    for (int32_t i = 0; i < *numWindows; i++)
    {
        Windows.push_back(std::make_shared<MCTerrainWindow>(i, VisibleVerticesPerSide));
    }

    std::expected<std::unique_ptr<MCMapBlockManager>, std::string> blocks =
        MCMapBlockManager::Create(fileName, TotalBlocks);

    if (!blocks.has_value())
    {
        return std::unexpected(blocks.error());
    }

    MapBlocks = std::move(*blocks);
    MapTopLeft3d100.Z = MapBlocks->GetTopLeftElevation();

    std::expected<std::unique_ptr<MCObjectBlockManager>, std::string> objectBlocks =
        MCObjectBlockManager::Create(fileName);

    if (!objectBlocks.has_value())
    {
        return std::unexpected(objectBlocks.error());
    }

    ObjectBlocks = std::move(*objectBlocks);

    const std::expected<MCGuiHandle<MCTacticalMap>, int32_t> map = ScreenWindow()->TryCreateChild<MCTacticalMap>(
        [this](MCTacticalMap& made)
        {
            // Stored first: the map's Init reaches itself through TacticalMap().
            TacticalMap = made.WeakAs<MCTacticalMap>();
            return made.Init(0, 0);
        });

    if (!map.has_value())
    {
        return std::unexpected("could not build the tactical map");
    }

    TacticalMap = *map;
    TacticalMap->RefreshPage();
    terrainFile.Close();
    SetTopLeftElevation(MapTopLeft3d100.Z);
    return {};
}

auto MCTerrain::GetTerrainWindow(int32_t windowNum) -> std::shared_ptr<MCTerrainWindow>
{
    if (windowNum < 0 || std::cmp_greater_equal(windowNum, Windows.size()))
    {
        return nullptr;
    }

    return Windows[static_cast<size_t>(windowNum)];
}

auto MCTerrain::NewWindow(std::weak_ptr<MCCamera> cam) -> std::shared_ptr<MCTerrainWindow>
{
    for (const std::shared_ptr<MCTerrainWindow>& window : Windows)
    {
        if (window->IsFree())
        {
            window->Bind(std::move(cam));
            return window;
        }
    }

    return nullptr;
}

auto MCTerrain::Update() -> void
{
    Windows[0]->Update();
}

auto MCTerrain::SetOverlayTile(int32_t blockNum, int32_t vertexNum, int32_t value) const -> void
{
    MapBlocks->SetOverlayTile(blockNum, vertexNum, value);
}

auto MCTerrain::GetOverlayTile(int32_t blockNum, int32_t vertexNum) const -> int32_t
{
    return MapBlocks->GetOverlayTile(blockNum, vertexNum);
}

auto MCTerrain::GetTile(int32_t blockNum, int32_t vertexNum) const -> int32_t
{
    return MapBlocks->GetTile(blockNum, vertexNum);
}

auto MCTerrain::ProjectTerrain(const MCVector3D& pos) -> MCVector2D
{
    const float fromTop = MapTopLeft3d100.Y - pos.Y;
    const float fromLeft = pos.X - MapTopLeft3d100.X;
    MCVector2D screen;
    screen.X = (fromLeft - fromTop) * ProjectionCos;
    screen.X = static_cast<float>(static_cast<int32_t>(std::floor(screen.X))) + MapTopLeft2d100.X;
    const int32_t y = static_cast<int32_t>(std::floor((fromLeft + fromTop) * ProjectionSin));
    const int32_t z = static_cast<int32_t>(std::floor(pos.Z));
    screen.Y = (MapTopLeft2d100.Y - static_cast<float>(z)) + static_cast<float>(y);
    return screen;
}

auto MCTerrain::Render(int32_t hazeFactor, const MCCamera& cam) -> void
{
    for (const std::shared_ptr<MCTerrainWindow>& window : Windows)
    {
        if (window->Camera.lock().get() == &cam)
        {
            window->Render(hazeFactor);
        }
    }
}

auto MCTerrain::DrawLines() -> void
{
    for (const std::shared_ptr<MCTerrainWindow>& window : Windows)
    {
        if (window->CameraIsActive())
        {
            window->DrawLines();
        }
    }
}

auto MCTerrain::GetTerrainElevation(const MCVector3D& pos) -> float
{
    return TerrainElevationAt(pos);
}

auto MCTerrain::GetTerrainNormal(const MCVector3D& pos) -> MCVector3D
{
    return TerrainNormalAt(pos);
}

auto MCTerrain::UpdateAllObjects() const -> void
{
    ObjectBlocks->UpdateAllObjects();
}

auto MCTerrain::MarkSeen(const MCVector3D& looker, const MCVector3D& /*lookVector*/, float angle, float /*range*/,
                         uint8_t who) const -> void
{
    if (angle != 360.0f)
    {
        return;
    }

    const float fromTop = MapTopLeft3d100.Y - looker.Y;
    const double gridX = std::floor(OneOvermetersPerVertex * (looker.X - MapTopLeft3d100.X));
    const double gridY = std::floor(OneOvermetersPerVertex * fromTop);
    const int32_t col = static_cast<int32_t>(std::floor(static_cast<float>(gridX)));
    const int32_t row = static_cast<int32_t>(std::floor(static_cast<float>(gridY)));

    Assert(TileOnMap(row, col), 0, " Map Tile out of bounds ");
    const uint32_t cellsA = GameMap().Map[GameMap().Width * row + col].Cells;
    Assert(TileOnMap(row, col + 1), 0, " Map Tile out of bounds ");
    const uint32_t cellsB = GameMap().Map[GameMap().Width * row + col + 1].Cells;
    Assert(TileOnMap(row + 1, col + 1), 0, " Map Tile out of bounds ");
    const uint32_t cellsC = GameMap().Map[GameMap().Width * (row + 1) + col + 1].Cells;
    Assert(TileOnMap(row + 1, col), 0, " Map Tile out of bounds ");
    const uint32_t cellsD = GameMap().Map[GameMap().Width * (row + 1) + col].Cells;

    // The highest of the four corners sets the sight radius.
    const uint32_t level =
        std::max({(cellsA >> 7) & 0x3f, (cellsB >> 7) & 0x3f, (cellsC >> 7) & 0x3f, (cellsD >> 7) & 0x3f});
    MCByteFlag& visibleBits = who == 1 ? *ISVisibleBits : *ClanVisibleBits;
    visibleBits.SetCircle(static_cast<uint32_t>(col), static_cast<uint32_t>(row),
                          static_cast<uint32_t>(VisualRangeTable[level]));
}

auto MCTerrain::MarkRadiusSeen(const MCVector3D& looker, const MCVector3D& /*lookVector*/, float angle, float range,
                               uint8_t who) const -> void
{
    if (angle != 360.0f)
    {
        return;
    }

    // Issue #44: the range is in meters at the mission's scale (the original read WorldUnitsPerMeter, not its
    // default 3.34), or every scripted reveal came out two thirds as wide.
    const float radius = static_cast<float>(static_cast<double>(WorldUnitsPerMeter) * range * OneOvermetersPerVertex);
    const float fromTop = MapTopLeft3d100.Y - looker.Y;
    const double gridX = std::floor(OneOvermetersPerVertex * (static_cast<double>(looker.X) - MapTopLeft3d100.X));
    const double gridY = std::floor(static_cast<double>(OneOvermetersPerVertex) * fromTop);
    const int32_t col = static_cast<int32_t>(std::floor(static_cast<float>(gridX)));
    const int32_t row = static_cast<int32_t>(std::floor(static_cast<float>(gridY)));
    MCByteFlag& visibleBits = who == 1 ? *ISVisibleBits : *ClanVisibleBits;
    visibleBits.SetCircle(static_cast<uint32_t>(col), static_cast<uint32_t>(row),
                          static_cast<uint32_t>(static_cast<int32_t>(radius)));
}

auto MCTerrain::HomeVisibleBits() const -> MCByteFlag&
{
    return HomeTeam()->Alignment != -1 ? *ISVisibleBits : *ClanVisibleBits;
}

auto MCTerrain::MarkBlockUsed(int32_t blockNum) -> void
{
    if (!BlockUsed(blockNum))
    {
        _UsedBlocks.push_back(blockNum);
    }
}

auto MCTerrain::ClearUsedBlocks() -> void
{
    _UsedBlocks.clear();
}

auto MCTerrain::BlockUsed(int32_t blockNum) const -> bool
{
    return blockNum == -1 || std::ranges::contains(_UsedBlocks, blockNum);
}

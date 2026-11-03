#include "stdafx.h"
#include "terrain/MCMapBlockManager.h"
#include "ai/MCMoveSystem.h"
#include "lib/MCFatal.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "main/MCMissionGlobals.h"
#include "terrain/MCTerrain.h"
#include "terrain/MCTerrainWindow.h"

namespace
{
    /// <summary>Radians to degrees (the double at 0x0077c278).</summary>
    constexpr double RadsToDegrees = 0x1.ca5dc1a6402aap+5;

    /// <summary>Tile types that are drawn flat at their highest corner (bridge decks).</summary>
    bool IsFlatTileType(uint32_t tileType)
    {
        return tileType == 0x25 || tileType == 0x26 || tileType == 0x27 || tileType == 0x28;
    }

    /// <summary>The map tile at (<paramref name="row"/>, <paramref name="col"/>), asserting it is on the map.</summary>
    MCMapTile& MapTileAt(int32_t row, int32_t col)
    {
        const int ok = (row >= 0 && row < GameMap().Height && col >= 0 && col < GameMap().Width) ? 1 : 0;
        Assert(ok, 0, " Map Tile out of bounds ");
        return GameMap().Map[GameMap().Width * row + col];
    }

    /// <summary>The elevation level of a tile (bits 7-12 of its cells word).</summary>
    uint32_t TileElevation(uint32_t cells)
    {
        return (cells >> 7) & 0x3f;
    }

    /// <summary>Normalises <paramref name="v"/> in double precision, as the x87 code did.</summary>
    void NormalizeX87(MCVector3D& v)
    {
        const double length = std::sqrt((static_cast<double>(v.X) * v.X + static_cast<double>(v.Y) * v.Y) +
                                        static_cast<double>(v.Z) * v.Z);

        if (length > 0.0)
        {
            v.X = static_cast<float>(v.X / length);
            v.Y = static_cast<float>(v.Y / length);
            v.Z = static_cast<float>(v.Z / length);
        }
    }

    /// <summary>The cross product in double precision, as the x87 code did.</summary>
    MCVector3D CrossX87(const MCVector3D& a, const MCVector3D& b)
    {
        return MCVector3D(static_cast<float>(static_cast<double>(a.Y) * b.Z - static_cast<double>(a.Z) * b.Y),
                          static_cast<float>(static_cast<double>(a.Z) * b.X - static_cast<double>(a.X) * b.Z),
                          static_cast<float>(static_cast<double>(a.X) * b.Y - static_cast<double>(a.Y) * b.X));
    }

    /// <summary>
    /// The two edge vectors of the face (triangle) of the tile under <paramref name="pos"/>, from its top-left
    /// corner, and that corner's height. Shared by the angle and the normal (the binary repeats it).
    /// </summary>
    void FaceVectors(const MCVector3D& pos, MCVector3D& edge1, MCVector3D& edge2, float& cornerZ)
    {
        const float mpv = MCTerrain::MetersPerVertex;
        const float oneOver = MCTerrain::OneOvermetersPerVertex;
        const float cornerX = static_cast<float>(mpv * std::floor(static_cast<double>(oneOver) * pos.X));
        const float cornerY = static_cast<float>(mpv * (std::floor(static_cast<double>(oneOver) * pos.Y) + 1.0));
        const double gridX = static_cast<double>(oneOver) * cornerX;
        const float gridY = oneOver * cornerY;
        const int32_t half = (MCTerrain::BlocksMapSide * MCTerrain::VerticesBlockSide) >> 1;
        const int32_t col = static_cast<int32_t>(std::floor(gridX)) + half;
        const int32_t row = half - static_cast<int32_t>(std::floor(static_cast<double>(gridY)));

        const uint32_t tileA = MapTileAt(row, col).Cells;
        const uint32_t tileB = MapTileAt(row, col + 1).Cells;
        const uint32_t tileC = MapTileAt(row + 1, col + 1).Cells;
        const uint32_t tileD = MapTileAt(row + 1, col).Cells;
        const int32_t base = GameMap().BaseElevation;
        const float mpe = MCTerrain::MetersPerElevLevel;
        const auto levelOf = [base](uint32_t cells) -> double
        { return static_cast<double>(static_cast<int64_t>(TileElevation(cells) + base)); };

        const float x0 = static_cast<float>(std::floor(gridX) * mpv);
        const float y0 = static_cast<float>(std::floor(static_cast<double>(gridY)) * mpv);
        cornerZ = static_cast<float>(levelOf(tileA) * mpe);
        const double xPlus = static_cast<double>(x0) + mpv;
        const double offsetX = std::fabs(static_cast<double>(pos.X) - cornerX);
        const double offsetY = std::fabs(static_cast<double>(cornerY) - pos.Y);

        // The face is the triangle (A, D, C) nearer the x edge, else (A, B, C).
        if (!(offsetY >= offsetX))
        {
            const float zB = static_cast<float>(levelOf(tileB) * mpe);
            const float y1 = y0 - mpv;
            const float zC = static_cast<float>(levelOf(tileC) * mpe);
            const auto dx = static_cast<float>(xPlus - x0);
            edge1 = MCVector3D(dx, 0.0f, zB - cornerZ);
            edge2 = MCVector3D(dx, y1 - y0, zC - cornerZ);
        }
        else
        {
            const float y1 = y0 - mpv;
            const float zC = static_cast<float>(levelOf(tileC) * mpe);
            const double zD = levelOf(tileD) * mpe;
            const float dy = y1 - y0;
            edge1 = MCVector3D(0.0f, dy, static_cast<float>(zD - cornerZ));
            edge2 = MCVector3D(static_cast<float>(xPlus - x0), dy, zC - cornerZ);
        }

        NormalizeX87(edge1);
        NormalizeX87(edge2);
    }
}

auto MCMapBlockManager::Create(std::string_view fileName, int32_t numBlocks)
    -> std::expected<std::unique_ptr<MCMapBlockManager>, std::string>
{
    MCPacketFile blockFile;

    if (const int32_t result = blockFile.Open(GamePath(TerrainPath, fileName, ".elv")); result != 0)
    {
        return std::unexpected(
            std::format("could not open the block file {}.elv ({:#x})", fileName, static_cast<uint32_t>(result)));
    }

    const uint32_t size = blockFile.FileSize();

    if (size == 0x23318 || size == 0x4f2ec)
    {
        Fatal(-1, " Old Map format.  Resave in Teditor! ");
    }

    // Every block plus the off-map one, each a packet (a missing packet reads as zeros).
    const auto blockSize = static_cast<size_t>(VerticesPerBlock);
    std::vector<MCPrecompVertex> vertices(static_cast<size_t>(numBlocks + 1) * blockSize);

    for (int32_t i = 0; i <= numBlocks; i++)
    {
        const std::span<MCPrecompVertex> block =
            std::span(vertices).subspan(static_cast<size_t>(i) * blockSize, blockSize);
        blockFile.ReadPacket(i, std::span(reinterpret_cast<uint8_t*>(block.data()), block.size_bytes()));
    }

    return std::make_unique<MCMapBlockManager>(vertices, numBlocks);
}

MCMapBlockManager::MCMapBlockManager(std::span<const MCPrecompVertex> vertices, int32_t numBlocks)
    : _BlockSize(static_cast<size_t>(VerticesPerBlock)), _NumBlocks(numBlocks)
{
    _Vertices.resize(static_cast<size_t>(numBlocks + 1) * _BlockSize);
    std::ranges::copy(vertices.first(std::min(vertices.size(), _Vertices.size())), _Vertices.begin());
}

auto MCMapBlockManager::BlockStart(int32_t blockNum) const -> size_t
{
    if (blockNum < 0 || blockNum >= _NumBlocks)
    {
        blockNum = _NumBlocks;
    }

    return static_cast<size_t>(blockNum) * _BlockSize;
}

auto MCMapBlockManager::MapVertexIndex(int32_t row, int32_t col) const -> size_t
{
    const int32_t side = MCTerrain::VerticesBlockSide;
    int32_t blockNum = _NumBlocks;

    if (OnMap(row, col))
    {
        blockNum = (row / side) * MCTerrain::BlocksMapSide + col / side;
    }

    const int32_t vertexY = ((row % side) + side) % side;
    const int32_t vertexX = ((col % side) + side) % side;
    return BlockStart(blockNum) + static_cast<size_t>(vertexY * side + vertexX);
}

auto MCMapBlockManager::GetTopLeftElevation() -> float
{
    return static_cast<float>(_Vertices[0].Elevation) * MCTerrain::MetersPerElevLevel;
}

auto MCMapBlockManager::OnMap(int32_t row, int32_t col) -> bool
{
    const int32_t mapSide = MCTerrain::BlocksMapSide * MCTerrain::VerticesBlockSide;
    return row >= 0 && col >= 0 && row < mapSide && col < mapSide;
}

auto MCMapBlockManager::RaiseOffMapBlock() -> void
{
    for (MCPrecompVertex& vertex : Block(_NumBlocks))
    {
        vertex.Elevation = RaisedOffMapElevation();
    }
}

auto MCMapBlockManager::RaisedOffMapElevation() -> uint8_t
{
    return static_cast<uint8_t>(static_cast<uint8_t>(GameMap().BaseElevation) + 1);
}

auto MCMapBlockManager::Update(const MCVector3D& cameraPos, MCTerrainWindow& window) -> void
{
    if (Turn == 1)
    {
        RaiseOffMapBlock();
    }

    const MCVector3D& topLeft = MCTerrain::MapTopLeft3d100;
    const float fromTop = topLeft.Y - cameraPos.Y;
    const int32_t blockX =
        static_cast<int32_t>(std::floor(std::fabs((topLeft.X - cameraPos.X) / MCTerrain::MetersBlockSide)));
    const int32_t blockY = static_cast<int32_t>(std::floor(std::fabs(fromTop / MCTerrain::MetersBlockSide)));
    window.CurrentBlock = blockY * MCTerrain::BlocksMapSide + blockX;

    const float offsetX = (cameraPos.X - topLeft.X) * MCTerrain::OneOvermetersPerVertex;
    const float offsetY = (topLeft.Y - cameraPos.Y) * MCTerrain::OneOvermetersPerVertex;
    const int32_t vertexX = static_cast<int32_t>(std::floor(offsetX));
    const int32_t vertexY = static_cast<int32_t>(std::floor(offsetY));
    const int32_t half = window.Side() >> 1;
    const int32_t inBlockX = vertexX % MCTerrain::VerticesBlockSide;
    const int32_t inBlockY = vertexY % MCTerrain::VerticesBlockSide;
    const int32_t cornerX = static_cast<int32_t>(std::floor(static_cast<float>(vertexX) - static_cast<float>(half)));
    const int32_t cornerY = static_cast<int32_t>(std::floor(static_cast<float>(vertexY) - static_cast<float>(half)));
    window.TopLeftX = static_cast<float>(cornerX) * MCTerrain::MetersPerVertex + topLeft.X;
    window.TopLeftY = topLeft.Y - static_cast<float>(cornerY) * MCTerrain::MetersPerVertex;

    // The corner's vertex within its block, as seen from the camera's block. The literal 20 and 15 are the
    // original's (a 20-vertex block side).
    int32_t cornerInBlockX = cornerX < 0 ? cornerX + 20 : cornerX % MCTerrain::VerticesBlockSide;
    int32_t cornerInBlockY = cornerY < 0 ? cornerY + 20 : cornerY % MCTerrain::VerticesBlockSide;

    if (inBlockX >= 15)
    {
        cornerInBlockX += 20;
    }

    if (inBlockY >= 15)
    {
        cornerInBlockY += 20;
    }

    if (half > 15)
    {
        // Port fix: the literals only hold for the data's 30-vertex grid. A grid grown for a larger screen
        // (MCTerrain::Load) measures the corner from the block before the camera's, as the literals do for 30; it
        // can then lie more blocks back, which BuildWindow steps to.
        cornerInBlockX = cornerX - (blockX - 1) * MCTerrain::VerticesBlockSide;
        cornerInBlockY = cornerY - (blockY - 1) * MCTerrain::VerticesBlockSide;
    }

    window.VertexOffsetX = static_cast<float>(cornerInBlockX);
    window.VertexOffsetY = static_cast<float>(cornerInBlockY);
}

auto MCMapBlockManager::BuildWindow(MCTerrainWindow& window) -> void
{
    const int32_t side = MCTerrain::VerticesBlockSide;
    const int32_t blocksSide = MCTerrain::BlocksMapSide;
    int32_t startX = static_cast<int32_t>(std::floor(window.VertexOffsetX));
    int32_t vertexY = static_cast<int32_t>(std::floor(window.VertexOffsetY));

    // An offset past the block's side moves the corner into the next block.
    // Original behaviour (OB-102): in the map's last block column that wraps to the next block row.
    if (startX >= side)
    {
        window.VertexOffsetX -= static_cast<float>(side);
        window.CurrentBlock += 1;
        startX = static_cast<int32_t>(std::floor(window.VertexOffsetX));
    }

    if (vertexY >= side)
    {
        window.VertexOffsetY -= static_cast<float>(side);
        window.CurrentBlock += blocksSide;
        vertexY = static_cast<int32_t>(std::floor(window.VertexOffsetY));
    }

    // Port fix: a grid grown past 41 vertices (MCTerrain::Load) starts more than one block before the camera's.
    int32_t blocksBackX = 0;
    int32_t blocksBackY = 0;

    while (startX < 0)
    {
        startX += side;
        blocksBackX++;
    }

    while (vertexY < 0)
    {
        vertexY += side;
        blocksBackY++;
    }

    const int32_t corner = window.CurrentBlock;
    int32_t blockY = corner / blocksSide - 1 - blocksBackY;
    const int32_t firstBlockX = corner % blocksSide - 1 - blocksBackX;
    int32_t blockX = firstBlockX;
    const int32_t offMap = _NumBlocks;
    int32_t blockNum = offMap;

    if (blockY >= 0 && firstBlockX >= 0)
    {
        blockNum = blockY * blocksSide + firstBlockX;
    }

    size_t source = BlockStart(blockNum) + static_cast<size_t>(side * vertexY + startX);
    auto vertex = window.Vertices.begin();
    const int32_t perSide = window.Side();
    window.Cache = weak_from_this();

    for (int32_t row = 0; row < perSide; row++)
    {
        int32_t vertexX = startX;

        for (int32_t col = 0; col < perSide; col++)
        {
            vertex->MapVertex = static_cast<uint32_t>(source);
            vertex->BlockNum = static_cast<int16_t>(static_cast<int16_t>(blockY) * static_cast<int16_t>(blocksSide) +
                                                    static_cast<int16_t>(blockX));
            vertex->VertexNum = static_cast<int16_t>(static_cast<int16_t>(vertexY) * static_cast<int16_t>(side) +
                                                     static_cast<int16_t>(vertexX));
            vertex->PosTile = static_cast<uint32_t>((blockY * side + vertexY) * 0x10000) +
                              (static_cast<uint32_t>(blockX * side + vertexX) & 0xffff);
            vertex++;
            vertexX++;
            source++;

            if (vertexX == side)
            {
                vertexX = 0;
                blockX++;
                blockNum = offMap;

                // Port fix: the grown grid can step across blocks left of the map, which aren't the row before's.
                if (blockX >= 0 && blockX < blocksSide)
                {
                    blockNum = blockY * blocksSide + blockX;
                }

                source = BlockStart(blockNum) + static_cast<size_t>(side * vertexY);
            }
        }

        vertexY++;
        blockX = firstBlockX;

        if (vertexY == side)
        {
            vertexY = 0;
            blockY++;
        }

        blockNum = offMap;

        if (firstBlockX >= 0 && blockY >= 0 && firstBlockX <= blocksSide - 1 && blockY <= blocksSide - 1)
        {
            blockNum = blockY * blocksSide + firstBlockX;
        }

        source = BlockStart(blockNum) + static_cast<size_t>(side * vertexY + startX);
    }
}

auto MCMapBlockManager::SetOverlayTile(int32_t blockNum, int32_t vertexNum, int32_t value) -> void
{
    MCPrecompVertex& vertex = Block(blockNum)[static_cast<size_t>(vertexNum)];
    vertex.OverlayData = static_cast<int16_t>(vertex.OverlayData + value);
}

auto MCMapBlockManager::GetOverlayTile(int32_t blockNum, int32_t vertexNum) -> int32_t
{
    return Block(blockNum)[static_cast<size_t>(vertexNum)].OverlayData;
}

auto MCMapBlockManager::SetTile(int32_t blockNum, int32_t vertexNum, int32_t value) -> void
{
    MCPrecompVertex& vertex = Block(blockNum)[static_cast<size_t>(vertexNum)];
    vertex.TextureData = static_cast<int16_t>(vertex.TextureData + value);
}

auto MCMapBlockManager::GetTile(int32_t blockNum, int32_t vertexNum) -> int32_t
{
    return Block(blockNum)[static_cast<size_t>(vertexNum)].TextureData;
}

auto TerrainNormalAt(const MCVector3D& pos) -> MCVector3D
{
    MCVector3D edge1;
    MCVector3D edge2;
    float cornerZ = 0.0f;
    FaceVectors(pos, edge1, edge2, cornerZ);
    MCVector3D faceNormal = CrossX87(edge2, edge1);

    if (!(faceNormal.Z >= 0.0f))
    {
        faceNormal = CrossX87(edge1, edge2);
    }

    NormalizeX87(faceNormal);
    return faceNormal;
}

auto TerrainElevationAt(const MCVector3D& pos) -> float
{
    using Ext = double;
    const float mpv = MCTerrain::MetersPerVertex;
    const float oneOver = MCTerrain::OneOvermetersPerVertex;
    const float cornerX = static_cast<float>(mpv * std::floor(static_cast<double>(oneOver) * pos.X));
    const float cornerY = static_cast<float>(mpv * (std::floor(static_cast<double>(oneOver) * pos.Y) + 1.0));
    const double gridX = static_cast<double>(oneOver) * cornerX;
    const double gridY = static_cast<double>(static_cast<float>(oneOver * cornerY));
    const int32_t half = (MCTerrain::BlocksMapSide * MCTerrain::VerticesBlockSide) >> 1;
    const int32_t col = static_cast<int32_t>(std::floor(gridX)) + half;
    const int32_t row = half - static_cast<int32_t>(std::floor(gridY));

    if (row < 0 || row >= GameMap().Height || col < 0 || col >= GameMap().Width)
    {
        return 0.0f;
    }

    if (row + 1 < 0 || row + 1 >= GameMap().Height || col + 1 < 0 || col + 1 >= GameMap().Width)
    {
        return 0.0f;
    }

    const MCMapTile& tileA = MapTileAt(row, col);
    const uint32_t cellsA = tileA.Cells;
    const uint32_t overlayA = tileA.Overlay;
    const uint32_t cellsB = MapTileAt(row, col + 1).Cells;
    const uint32_t cellsC = MapTileAt(row + 1, col + 1).Cells;
    const uint32_t cellsD = MapTileAt(row + 1, col).Cells;

    if (IsFlatTileType(overlayA & 0x7f))
    {
        uint32_t level = TileElevation(cellsA);
        const uint32_t levelB = TileElevation(cellsB);
        const uint32_t levelC = TileElevation(cellsC);
        const uint32_t levelD = TileElevation(cellsD);

        if (levelB == level && levelB == levelC && levelC == levelD)
        {
            level++;
        }
        else
        {
            level = std::max({level, levelB, levelC, levelD});
        }

        return static_cast<float>(static_cast<Ext>(static_cast<int32_t>(GameMap().BaseElevation + level)) *
                                  MCTerrain::MetersPerElevLevel);
    }

    const int32_t base = GameMap().BaseElevation;
    const float mpe = MCTerrain::MetersPerElevLevel;
    const auto levelOf = [base](uint32_t cells)
    { return static_cast<int64_t>(static_cast<uint32_t>(static_cast<int32_t>(TileElevation(cells)) + base)); };
    const float x0 = static_cast<float>(std::floor(gridX) * mpv);
    const float y0 = static_cast<float>(std::floor(gridY) * mpv);
    const Ext elevA = static_cast<Ext>(levelOf(cellsA)) * mpe;
    const float offsetX = std::fabs(pos.X - cornerX);
    const float offsetY = std::fabs(cornerY - pos.Y);
    const Ext offsetXExt = std::fabs(static_cast<Ext>(pos.X) - cornerX);

    Ext uX;
    Ext uY;
    Ext vX;
    Ext vY;
    Ext vZ;
    float uZ;

    if (offsetXExt > static_cast<Ext>(offsetY))
    {
        const float elevB = static_cast<float>(static_cast<Ext>(levelOf(cellsB)) * mpe);
        const float y1 = static_cast<float>(y0 - mpv);
        const float elevC = static_cast<float>(static_cast<Ext>(levelOf(cellsC)) * mpe);
        const Ext spanX = (static_cast<Ext>(x0) + mpv) - x0;
        uZ = static_cast<float>(static_cast<Ext>(elevB) - elevA);
        uX = spanX;
        uY = 0.0;
        vX = static_cast<float>(spanX);
        vY = static_cast<Ext>(y1) - y0;
        vZ = static_cast<Ext>(elevC) - elevA;
    }
    else
    {
        const float x1 = static_cast<float>(static_cast<Ext>(x0) + mpv);
        const float y1 = static_cast<float>(y0 - mpv);
        const float elevC = static_cast<float>(static_cast<Ext>(levelOf(cellsC)) * mpe);
        const Ext elevD = static_cast<Ext>(levelOf(cellsD)) * mpe;
        const float spanY = static_cast<float>(static_cast<Ext>(y1) - y0);
        uZ = static_cast<float>(elevD - elevA);
        uX = 0.0;
        uY = spanY;
        vX = static_cast<Ext>(x1) - x0;
        vY = spanY;
        vZ = static_cast<Ext>(elevC) - elevA;
    }

    const float uYf = static_cast<float>(uY);
    const Ext lengthU = std::sqrt((static_cast<Ext>(uYf) * uYf + static_cast<Ext>(uZ) * uZ) + uX * uX);

    if (lengthU != 0.0)
    {
        uX = uX / lengthU;
        uY = uY / lengthU;
        uZ = static_cast<float>(static_cast<Ext>(uZ) / lengthU);
    }

    const float vZf = static_cast<float>(vZ);
    const float vYf = static_cast<float>(vY);
    const Ext lengthV = std::sqrt((vX * vX + static_cast<Ext>(vZf) * vZf) + static_cast<Ext>(vYf) * vYf);

    if (lengthV != 0.0)
    {
        vX = vX / lengthV;
        vY = vY / lengthV;
        vZ = vZ / lengthV;
    }

    // The plane through the corner: z = z0 - (nx/nz) * dx + (ny/nz) * dy.
    const float normalX = static_cast<float>(vZ * uY - vY * static_cast<Ext>(uZ));
    const float normalY = static_cast<float>(static_cast<Ext>(uZ) * vX - vZ * uX);
    const float normalZ = static_cast<float>(vY * uX - uY * vX);

    if (normalZ == 0.0f)
    {
        return static_cast<float>(elevA);
    }

    Ext nx = normalX;
    Ext ny = normalY;
    Ext nz = normalZ;

    if (normalZ < 0.0f)
    {
        nx = -nx;
        ny = -ny;
        nz = -nz;
    }

    return static_cast<float>(elevA +
                              -((ny / nz) * static_cast<Ext>(-offsetY) + (nx / nz) * static_cast<Ext>(offsetX)));
}

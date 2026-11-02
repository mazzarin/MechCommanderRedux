#include "stdafx.h"
#include "ai/MCMoveSystem.h"
#include "main/MCGameContext.h"

MCMoveSystem::MCMoveSystem() = default;

MCMoveSystem::~MCMoveSystem() = default;

auto MCMoveSystem::Load(MCFile& mapFile, MCFile& globalMapFile, int32_t pathWindow)
    -> std::expected<std::unique_ptr<MCMoveSystem>, std::string>
{
    auto system = std::make_unique<MCMoveSystem>();
    std::expected<std::unique_ptr<MCScenarioMap>, std::string> map = MCScenarioMap::Read(mapFile);

    if (!map)
    {
        return std::unexpected(std::format("the movement map: {}", map.error()));
    }

    system->Map = std::move(*map);
    system->ObjectMap = std::make_unique<MCObjectMap>(*system->Map);
    system->PathManager = std::make_unique<MCMovePathManager>();
    system->PathFinder = std::make_unique<MCMoveMap>(pathWindow, pathWindow, system->OpenList);
    std::expected<std::unique_ptr<MCGlobalMap>, std::string> globalMap =
        MCGlobalMap::Read(globalMapFile, system->OpenList);

    if (!globalMap)
    {
        return std::unexpected(std::format("the global map: {}", globalMap.error()));
    }

    system->GlobalMap = std::move(*globalMap);
    return system;
}

namespace
{
    /// <summary>The installed move system (asserted).</summary>
    MCMoveSystem& System()
    {
        return MCGameContext::Current().MoveSystem();
    }
}

auto GameMap() -> MCScenarioMap&
{
    SDL_assert(System().Map != nullptr);
    return *System().Map;
}

auto LoadedGameMap() -> const MCScenarioMap*
{
    return MCGameContext::Current().HasMoveSystem() ? System().Map.get() : nullptr;
}

auto GameObjectMap() -> MCObjectMap&
{
    SDL_assert(System().ObjectMap != nullptr);
    return *System().ObjectMap;
}

auto GlobalMoveMap() -> MCGlobalMap&
{
    SDL_assert(System().GlobalMap != nullptr);
    return *System().GlobalMap;
}

auto PathFindMap() -> MCMoveMap&
{
    SDL_assert(System().PathFinder != nullptr);
    return *System().PathFinder;
}

auto PathManager() -> MCMovePathManager&
{
    SDL_assert(System().PathManager != nullptr);
    return *System().PathManager;
}

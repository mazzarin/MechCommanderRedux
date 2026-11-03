#include "stdafx.h"
#include "main/MCGameContext.h"
#include "appear/MCAppearanceTypeList.h"
#include "abl/MCAblRuntime.h"
#include "abl/MCAblSymbolTable.h"
#include "ai/MCMoveSystem.h"
#include "camera/MCCameraList.h"
#include "color/MCPalette.h"
#include "engine/MCCraterManager.h"
#include "engine/MCElementBuffer.h"
#include "object/MCCollisionSystem.h"
#include "object/MCContactSystem.h"
#include "object/MCEffectSystem.h"
#include "object/MCForces.h"
#include "object/MCObjectSystem.h"
#include "object/MCTrainManager.h"
#include "gameos/MCSoundRenderer.h"
#include "sound/MCSoundSystem.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFastFileSet.h"
#include "sprite/MCSpriteManager.h"
#include "terrain/MCTerrain.h"
#include "platform/MCNeverDestroyed.h"

namespace
{
    /// <summary>The context with the real services, made on first use and never destroyed (threads and static
    /// destructors may still ask for the clock while the process ends).</summary>
    const std::shared_ptr<MCGameContext>& RootContext()
    {
        static MCNeverDestroyed<std::shared_ptr<MCGameContext>> root(
            []
            {
                auto context = std::make_shared<MCGameContext>(nullptr);
                context->SetClock(std::make_unique<MCSystemClock>());
                context->SetRandom(std::make_unique<MCCrtRandom>());
                context->SetFiles(std::make_unique<MCDiskFileSource>());
                context->SetAudio(std::make_unique<MCSdlAudioDevice>());
                context->SetNet(std::make_unique<MCSocketTransport>());
                context->SetFastFiles(std::make_unique<MCFastFileSet>());
                return context;
            }());
        return *root;
    }

    /// <summary>The installed context, or null for the root one.</summary>
    /// <remarks>Never destroyed, like the root: other threads may still ask while the process ends.</remarks>
    MCAtomicPtr<std::shared_ptr<MCGameContext>>& CurrentContext()
    {
        static MCNeverDestroyed<MCAtomicPtr<std::shared_ptr<MCGameContext>>> current;
        return *current;
    }

    /// <summary>
    /// The installed context's object, or null for the root one: what Current reads, many thousands of times a frame,
    /// where loading the shared pointer takes a lock (libc++ has no atomic shared_ptr) and its count.
    /// </summary>
    constinit std::atomic<MCGameContext*> CurrentObject{nullptr};
}

MCGameContext& MCGameContext::Current()
{
    // The installed context is kept by the scope that installed it for as long as it is current.
    MCGameContext* const context = CurrentObject.load(std::memory_order_acquire);
    return context != nullptr ? *context : *RootContext();
}

std::shared_ptr<MCGameContext> MCGameContext::CurrentShared()
{
    std::shared_ptr<MCGameContext> context = CurrentContext().load(std::memory_order_acquire);
    return context != nullptr ? context : RootContext();
}

void MCGameContext::SetCurrent(std::shared_ptr<MCGameContext> context)
{
    MCGameContext* const object = context.get();
    CurrentContext().store(std::move(context), std::memory_order_release);
    CurrentObject.store(object, std::memory_order_release);
}

template <auto Slot> MCOptionalRef<const MCGameContext> MCGameContext::Holder() const
{
    if (this->*Slot != nullptr)
    {
        return *this;
    }

    return _Parent != nullptr ? _Parent->Holder<Slot>() : std::nullopt;
}

template <auto Slot> auto& MCGameContext::Find() const
{
    const MCOptionalRef<const MCGameContext> holder = Holder<Slot>();
    SDL_assert(holder.has_value());
    return *(holder->get().*Slot);
}

template <auto Slot> auto MCGameContext::FindShared() const
{
    const MCOptionalRef<const MCGameContext> holder = Holder<Slot>();
    return holder.has_value() ? holder->get().*Slot : std::remove_cvref_t<decltype(this->*Slot)>();
}

MCGameContext::MCGameContext(std::shared_ptr<MCGameContext> parent) : _Parent(std::move(parent))
{
}

MCGameContext::~MCGameContext()
{
    Clear();
}

void MCGameContext::Clear()
{
    // The scenario and the mission go first, then the objects, while every system they reach for is still here. Then
    // each slot is emptied, last to first, so a system going finds the ones after it gone (null, or the parent's),
    // never deleted ones.
    _Scenario.reset();
    _Mission.reset();
    _MultiPlayer.reset();

    if (_ObjectSystem != nullptr)
    {
        _ObjectSystem->Unload();
    }

    _TacticalInterface.reset();
    _SoundSystem.reset();
    _SoundRenderer.reset();
    _AblRuntime.reset();
    _AblSymbols.reset();
    _ObjectSystem.reset();
    _TrainManager.reset();
    _EffectSystem.reset();
    _CollisionSystem.reset();
    _ContactSystem.reset();
    _Forces.reset();
    _MoveSystem.reset();
    _CameraList.reset();
    _Terrain.reset();
    _AppearanceTypeList.reset();
    _SpriteManager.reset();
    _CraterManager.reset();
    _ElementList.reset();
    _GuiSystem.reset();
    _Palette.reset();
    _FastFiles.reset();
    _Net.reset();
    _Audio.reset();
    _Files.reset();
    _Random.reset();
    _Clock.reset();
}

MCClock& MCGameContext::Clock() const
{
    return Find<&MCGameContext::_Clock>();
}

MCRandom& MCGameContext::Random() const
{
    return Find<&MCGameContext::_Random>();
}

MCFileSource& MCGameContext::Files() const
{
    return Find<&MCGameContext::_Files>();
}

MCAudioDevice& MCGameContext::Audio() const
{
    return Find<&MCGameContext::_Audio>();
}

MCNetTransport& MCGameContext::Net() const
{
    return Find<&MCGameContext::_Net>();
}

MCFastFileSet& MCGameContext::FastFiles() const
{
    return Find<&MCGameContext::_FastFiles>();
}

MCFastFileSet& MCGameContext::SetFastFiles(std::unique_ptr<MCFastFileSet> fastFiles)
{
    return Install(_FastFiles, std::move(fastFiles));
}

MCPalette& MCGameContext::Palette() const
{
    return Find<&MCGameContext::_Palette>();
}

bool MCGameContext::HasPalette() const
{
    return Holder<&MCGameContext::_Palette>().has_value();
}

MCElementBuffer& MCGameContext::ElementList() const
{
    return Find<&MCGameContext::_ElementList>();
}

bool MCGameContext::HasElementList() const
{
    return Holder<&MCGameContext::_ElementList>().has_value();
}

MCCraterManager& MCGameContext::CraterManager() const
{
    return Find<&MCGameContext::_CraterManager>();
}

bool MCGameContext::HasCraterManager() const
{
    return Holder<&MCGameContext::_CraterManager>().has_value();
}

std::shared_ptr<MCSpriteManager> MCGameContext::SpriteManager() const
{
    return FindShared<&MCGameContext::_SpriteManager>();
}

MCAppearanceTypeList& MCGameContext::AppearanceTypeList() const
{
    return Find<&MCGameContext::_AppearanceTypeList>();
}

bool MCGameContext::HasAppearanceTypeList() const
{
    return Holder<&MCGameContext::_AppearanceTypeList>().has_value();
}

std::shared_ptr<MCTerrain> MCGameContext::Terrain() const
{
    return FindShared<&MCGameContext::_Terrain>();
}

std::shared_ptr<MCCameraList> MCGameContext::CameraList() const
{
    return FindShared<&MCGameContext::_CameraList>();
}

MCMoveSystem& MCGameContext::MoveSystem() const
{
    return Find<&MCGameContext::_MoveSystem>();
}

bool MCGameContext::HasMoveSystem() const
{
    return Holder<&MCGameContext::_MoveSystem>().has_value();
}

std::unique_ptr<MCMoveSystem> MCGameContext::SetMoveSystem(std::unique_ptr<MCMoveSystem> moveSystem)
{
    return std::exchange(_MoveSystem, std::move(moveSystem));
}

MCForces& MCGameContext::Forces() const
{
    return Find<&MCGameContext::_Forces>();
}

bool MCGameContext::HasForces() const
{
    return Holder<&MCGameContext::_Forces>().has_value();
}

std::unique_ptr<MCForces> MCGameContext::SetForces(std::unique_ptr<MCForces> forces)
{
    return std::exchange(_Forces, std::move(forces));
}

MCContactSystem& MCGameContext::ContactSystem() const
{
    return Find<&MCGameContext::_ContactSystem>();
}

bool MCGameContext::HasContactSystem() const
{
    return Holder<&MCGameContext::_ContactSystem>().has_value();
}

std::unique_ptr<MCContactSystem> MCGameContext::SetContactSystem(std::unique_ptr<MCContactSystem> contactSystem)
{
    return std::exchange(_ContactSystem, std::move(contactSystem));
}

MCCollisionSystem& MCGameContext::CollisionSystem() const
{
    return Find<&MCGameContext::_CollisionSystem>();
}

bool MCGameContext::HasCollisionSystem() const
{
    return Holder<&MCGameContext::_CollisionSystem>().has_value();
}

std::unique_ptr<MCCollisionSystem> MCGameContext::SetCollisionSystem(std::unique_ptr<MCCollisionSystem> collisionSystem)
{
    return std::exchange(_CollisionSystem, std::move(collisionSystem));
}

MCEffectSystem& MCGameContext::EffectSystem() const
{
    return Find<&MCGameContext::_EffectSystem>();
}

bool MCGameContext::HasEffectSystem() const
{
    return Holder<&MCGameContext::_EffectSystem>().has_value();
}

std::unique_ptr<MCEffectSystem> MCGameContext::SetEffectSystem(std::unique_ptr<MCEffectSystem> effectSystem)
{
    return std::exchange(_EffectSystem, std::move(effectSystem));
}

MCTrainManager& MCGameContext::TrainManager() const
{
    return Find<&MCGameContext::_TrainManager>();
}

bool MCGameContext::HasTrainManager() const
{
    return Holder<&MCGameContext::_TrainManager>().has_value();
}

std::unique_ptr<MCTrainManager> MCGameContext::SetTrainManager(std::unique_ptr<MCTrainManager> trainManager)
{
    return std::exchange(_TrainManager, std::move(trainManager));
}

std::shared_ptr<MCObjectSystem> MCGameContext::ObjectSystem() const
{
    return FindShared<&MCGameContext::_ObjectSystem>();
}

std::shared_ptr<MCObjectSystem> MCGameContext::SetObjectSystem(std::shared_ptr<MCObjectSystem> objectSystem)
{
    return std::exchange(_ObjectSystem, std::move(objectSystem));
}

std::shared_ptr<MCAblSymbolTable> MCGameContext::AblSymbols() const
{
    return FindShared<&MCGameContext::_AblSymbols>();
}

std::shared_ptr<MCAblSymbolTable> MCGameContext::SetAblSymbols(std::shared_ptr<MCAblSymbolTable> symbols)
{
    return std::exchange(_AblSymbols, std::move(symbols));
}

std::shared_ptr<MCAblRuntime> MCGameContext::AblRuntime() const
{
    return FindShared<&MCGameContext::_AblRuntime>();
}

std::shared_ptr<MCAblRuntime> MCGameContext::SetAblRuntime(std::shared_ptr<MCAblRuntime> runtime)
{
    return std::exchange(_AblRuntime, std::move(runtime));
}

std::shared_ptr<MCSoundRenderer> MCGameContext::SoundRenderer() const
{
    return FindShared<&MCGameContext::_SoundRenderer>();
}

std::shared_ptr<MCSoundRenderer> MCGameContext::SetSoundRenderer(std::shared_ptr<MCSoundRenderer> renderer)
{
    return std::exchange(_SoundRenderer, std::move(renderer));
}

std::shared_ptr<MCMission> MCGameContext::Mission() const
{
    return FindShared<&MCGameContext::_Mission>();
}

std::shared_ptr<MCMission> MCGameContext::SetMission(std::shared_ptr<MCMission> mission)
{
    return std::exchange(_Mission, std::move(mission));
}

std::shared_ptr<MCMultiPlayer> MCGameContext::MultiPlayer() const
{
    return FindShared<&MCGameContext::_MultiPlayer>();
}

std::shared_ptr<MCMultiPlayer> MCGameContext::SetMultiPlayer(std::shared_ptr<MCMultiPlayer> multiPlayer)
{
    return std::exchange(_MultiPlayer, std::move(multiPlayer));
}

std::shared_ptr<MCScenario> MCGameContext::Scenario() const
{
    return FindShared<&MCGameContext::_Scenario>();
}

std::shared_ptr<MCScenario> MCGameContext::SetScenario(std::shared_ptr<MCScenario> scenario)
{
    return std::exchange(_Scenario, std::move(scenario));
}

std::shared_ptr<MCTacticalInterface> MCGameContext::TacticalInterface() const
{
    return FindShared<&MCGameContext::_TacticalInterface>();
}

std::shared_ptr<MCTacticalInterface> MCGameContext::SetTacticalInterface(
    std::shared_ptr<MCTacticalInterface> tacticalInterface)
{
    return std::exchange(_TacticalInterface, std::move(tacticalInterface));
}

std::shared_ptr<MCGuiSystem> MCGameContext::GuiSystem() const
{
    return FindShared<&MCGameContext::_GuiSystem>();
}

std::shared_ptr<MCGuiSystem> MCGameContext::SetGuiSystem(std::shared_ptr<MCGuiSystem> guiSystem)
{
    return std::exchange(_GuiSystem, std::move(guiSystem));
}

std::shared_ptr<MCSoundSystem> MCGameContext::SoundSystem() const
{
    return FindShared<&MCGameContext::_SoundSystem>();
}

std::shared_ptr<MCSoundSystem> MCGameContext::SetSoundSystem(std::shared_ptr<MCSoundSystem> soundSystem)
{
    return std::exchange(_SoundSystem, std::move(soundSystem));
}

std::shared_ptr<MCTerrain> MCGameContext::SetTerrain(std::shared_ptr<MCTerrain> terrain)
{
    return std::exchange(_Terrain, std::move(terrain));
}

std::shared_ptr<MCCameraList> MCGameContext::SetCameraList(std::shared_ptr<MCCameraList> cameraList)
{
    return std::exchange(_CameraList, std::move(cameraList));
}

std::shared_ptr<MCSpriteManager> MCGameContext::SetSpriteManager(std::shared_ptr<MCSpriteManager> spriteManager)
{
    return std::exchange(_SpriteManager, std::move(spriteManager));
}

std::unique_ptr<MCAppearanceTypeList> MCGameContext::SetAppearanceTypeList(
    std::unique_ptr<MCAppearanceTypeList> typeList)
{
    return std::exchange(_AppearanceTypeList, std::move(typeList));
}

std::unique_ptr<MCPalette> MCGameContext::SetPalette(std::unique_ptr<MCPalette> palette)
{
    return std::exchange(_Palette, std::move(palette));
}

std::unique_ptr<MCElementBuffer> MCGameContext::SetElementList(std::unique_ptr<MCElementBuffer> elementList)
{
    return std::exchange(_ElementList, std::move(elementList));
}

std::unique_ptr<MCCraterManager> MCGameContext::SetCraterManager(std::unique_ptr<MCCraterManager> craterManager)
{
    return std::exchange(_CraterManager, std::move(craterManager));
}

MCTestContextScope::MCTestContextScope()
    : _Previous(MCGameContext::CurrentShared()), _Context(std::make_shared<MCGameContext>(_Previous))
{
    MCGameContext::SetCurrent(_Context);
}

MCTestContextScope::~MCTestContextScope()
{
    _Context->Clear();
    MCGameContext::SetCurrent(_Previous);
}

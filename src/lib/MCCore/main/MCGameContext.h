#pragma once

#include "platform/MCServices.h"
#include "lib/MCOptionalRef.h"

class MCAblRuntime;
class MCAblSymbolTable;
class MCAppearanceTypeList;
class MCCameraList;
class MCCollisionSystem;
class MCContactSystem;
class MCCraterManager;
class MCEffectSystem;
class MCElementBuffer;
class MCFastFileSet;
class MCForces;
class MCGuiSystem;
class MCMission;
class MCMoveSystem;
class MCMultiPlayer;
class MCObjectSystem;
class MCPalette;
class MCScenario;
class MCSoundRenderer;
class MCSoundSystem;
class MCSpriteManager;
class MCTacticalInterface;
class MCTerrain;
class MCTrainManager;

/// <summary>
/// Everything the game reaches for that a test may want to replace: the port services (clock, dice, files, sound
/// output, network) and, as the modernization moves them here, the game's own systems. Game code asks
/// <see cref="Current"/> for them instead of reading a global.
/// </summary>
/// <remarks>
/// <para>A context owns the services it was given. A service it wasn't given comes from the context it was installed
/// over (<see cref="MCTestContextScope"/>), so a test replaces only what it needs. The first context, made on first
/// use, holds the real services and lives until the process ends.</para>
/// <para>The game's own systems (the map, the scenario, the object manager, ...) get their slots here as they move
/// out of globals.</para>
/// </remarks>
class MCGameContext
{
public:
    /// <summary>The context the game runs in now.</summary>
    static MCGameContext& Current();

    /// <summary>
    /// A context with no services of its own: each comes from <paramref name="parent"/> (null only for the first
    /// context), which it keeps.
    /// </summary>
    explicit MCGameContext(std::shared_ptr<MCGameContext> parent);

    ~MCGameContext();
    MCGameContext(const MCGameContext&) = delete;
    MCGameContext& operator=(const MCGameContext&) = delete;

    /// <summary>The clocks.</summary>
    MCClock& Clock() const;

    /// <summary>The dice.</summary>
    MCRandom& Random() const;

    /// <summary>Where the game's files come from.</summary>
    MCFileSource& Files() const;

    /// <summary>The sound output.</summary>
    MCAudioDevice& Audio() const;

    /// <summary>The network.</summary>
    MCNetTransport& Net() const;

    /// <summary>The FastFiles the game has open.</summary>
    MCFastFileSet& FastFiles() const;

    /// <summary>
    /// Port: the hash of every match's gameplay data (network/MCGameDataHash.h), kept once worked out: the files don't
    /// change while the game runs. This context's own, never the one it was installed over's, so a test's files are
    /// hashed afresh.
    /// </summary>
    std::optional<uint64_t>& SharedDataHash() { return _SharedDataHash; }

    /// <summary>The palette the game shows (asserted: there is one from the interface's start).</summary>
    MCPalette& Palette() const;

    /// <summary>Whether <see cref="Palette"/> has one (this context's or the one it was installed over).</summary>
    bool HasPalette() const;

    /// <summary>The frame's draw list (asserted: there is one in a mission).</summary>
    MCElementBuffer& ElementList() const;

    /// <summary>Whether <see cref="ElementList"/> has one (this context's or the one it was installed over).</summary>
    bool HasElementList() const;

    /// <summary>The mission's craters (asserted: there are some in a mission).</summary>
    MCCraterManager& CraterManager() const;

    /// <summary>Whether <see cref="CraterManager"/> has one (this context's or the one it was installed over).</summary>
    bool HasCraterManager() const;

    /// <summary>The mission's sprite cache (null outside a mission); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCSpriteManager> SpriteManager() const;

    /// <summary>The mission's appearance types (asserted: there are some in a mission).</summary>
    MCAppearanceTypeList& AppearanceTypeList() const;

    /// <summary>Whether <see cref="AppearanceTypeList"/> has one (this context's or the one it was installed over).</summary>
    bool HasAppearanceTypeList() const;

    /// <summary>The mission's terrain (null outside a mission).</summary>
    std::shared_ptr<MCTerrain> Terrain() const;

    /// <summary>The mission's cameras (null outside a mission); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCCameraList> CameraList() const;

    /// <summary>The mission's movement maps and path finders (asserted: there are some in a mission).</summary>
    MCMoveSystem& MoveSystem() const;

    /// <summary>Whether <see cref="MoveSystem"/> has one (this context's or the one it was installed over).</summary>
    bool HasMoveSystem() const;

    /// <summary>The mission's sides: its teams and commanders (asserted: there are some in a mission).</summary>
    MCForces& Forces() const;

    /// <summary>Whether <see cref="Forces"/> has one (this context's or the one it was installed over).</summary>
    bool HasForces() const;

    /// <summary>The mission's sensors and potential contacts (asserted: there is one in a mission).</summary>
    MCContactSystem& ContactSystem() const;

    /// <summary>Whether <see cref="ContactSystem"/> has one (this context's or the one it was installed over).</summary>
    bool HasContactSystem() const;

    /// <summary>The mission's collision system (asserted: there is one in a mission).</summary>
    MCCollisionSystem& CollisionSystem() const;

    /// <summary>Whether <see cref="CollisionSystem"/> has one (this context's or the one it was installed over).</summary>
    bool HasCollisionSystem() const;

    /// <summary>The mission's fires and smoke bookkeeping (asserted: there is one in a mission).</summary>
    MCEffectSystem& EffectSystem() const;

    /// <summary>Whether <see cref="EffectSystem"/> has one (this context's or the one it was installed over).</summary>
    bool HasEffectSystem() const;

    /// <summary>The mission's trains (asserted: there is one in a mission with trains).</summary>
    MCTrainManager& TrainManager() const;

    /// <summary>Whether <see cref="TrainManager"/> has one (this context's or the one it was installed over).</summary>
    bool HasTrainManager() const;

    /// <summary>The mission's objects: their types and lists (null outside a mission).</summary>
    std::shared_ptr<MCObjectSystem> ObjectSystem() const;

    /// <summary>ABL's symbols and types (null outside AblInit .. AblClose; the runtime shares them).</summary>
    std::shared_ptr<MCAblSymbolTable> AblSymbols() const;

    /// <summary>
    /// ABL's runtime: its modules, memory and interpreter (null outside AblInit .. AblClose; module instances refer to
    /// it).
    /// </summary>
    std::shared_ptr<MCAblRuntime> AblRuntime() const;

    /// <summary>The sound renderer: its channels, resources and mixer (null until the sound system installs it).
    /// </summary>
    std::shared_ptr<MCSoundRenderer> SoundRenderer() const;

    /// <summary>The game's sound: effects, radio, Betty, speech and music (null before UserInit makes it).</summary>
    std::shared_ptr<MCSoundSystem> SoundSystem() const;

    /// <summary>The game's top-level state machine (null before the game's start-up makes it).</summary>
    std::shared_ptr<MCMission> Mission() const;

    /// <summary>The current battle (null outside one); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCScenario> Scenario() const;

    /// <summary>The multiplayer game (null in a single-player game); the caller keeps it as its pin.</summary>
    std::shared_ptr<MCMultiPlayer> MultiPlayer() const;

    /// <summary>The player's command interface (null before the GUI's start-up makes it).</summary>
    std::shared_ptr<MCTacticalInterface> TacticalInterface() const;

    /// <summary>The GUI: the window tree, the display, the input routing, the timers and the fonts (null before the
    /// program's start makes it).</summary>
    std::shared_ptr<MCGuiSystem> GuiSystem() const;

    /// <summary>Gives this context its own clock.</summary>
    /// <returns>The clock, still reachable as its own type.</returns>
    template <std::derived_from<MCClock> T> T& SetClock(std::unique_ptr<T> clock)
    {
        return Install(_Clock, std::move(clock));
    }

    /// <summary>Gives this context its own dice.</summary>
    template <std::derived_from<MCRandom> T> T& SetRandom(std::unique_ptr<T> random)
    {
        return Install(_Random, std::move(random));
    }

    /// <summary>Gives this context its own file source.</summary>
    template <std::derived_from<MCFileSource> T> T& SetFiles(std::unique_ptr<T> files)
    {
        return Install(_Files, std::move(files));
    }

    /// <summary>Gives this context its own sound output.</summary>
    template <std::derived_from<MCAudioDevice> T> T& SetAudio(std::unique_ptr<T> audio)
    {
        return Install(_Audio, std::move(audio));
    }

    /// <summary>Gives this context its own network.</summary>
    template <std::derived_from<MCNetTransport> T> T& SetNet(std::unique_ptr<T> net)
    {
        return Install(_Net, std::move(net));
    }

    /// <summary>Gives this context its own set of FastFiles.</summary>
    MCFastFileSet& SetFastFiles(std::unique_ptr<MCFastFileSet> fastFiles);

    /// <summary>Gives this context its own palette (null: the one it was installed over, if any).</summary>
    /// <returns>The palette this context had.</returns>
    std::unique_ptr<MCPalette> SetPalette(std::unique_ptr<MCPalette> palette);

    /// <summary>Gives this context its own draw list (null: the one it was installed over, if any).</summary>
    /// <returns>The draw list this context had.</returns>
    std::unique_ptr<MCElementBuffer> SetElementList(std::unique_ptr<MCElementBuffer> elementList);

    /// <summary>Gives this context its own craters (null: the ones it was installed over, if any).</summary>
    /// <returns>The craters this context had.</returns>
    std::unique_ptr<MCCraterManager> SetCraterManager(std::unique_ptr<MCCraterManager> craterManager);

    /// <summary>Gives this context its own sprite cache (null: the one it was installed over, if any).</summary>
    /// <returns>The sprite cache this context had.</returns>
    std::shared_ptr<MCSpriteManager> SetSpriteManager(std::shared_ptr<MCSpriteManager> spriteManager);

    /// <summary>Gives this context its own appearance types (null: the ones it was installed over, if any).</summary>
    /// <returns>The appearance types this context had.</returns>
    std::unique_ptr<MCAppearanceTypeList> SetAppearanceTypeList(std::unique_ptr<MCAppearanceTypeList> typeList);

    /// <summary>Gives this context its own terrain (null: the one it was installed over, if any).</summary>
    /// <returns>The terrain this context had.</returns>
    std::shared_ptr<MCTerrain> SetTerrain(std::shared_ptr<MCTerrain> terrain);

    /// <summary>Gives this context its own cameras (null: the ones it was installed over, if any).</summary>
    /// <returns>The cameras this context had.</returns>
    std::shared_ptr<MCCameraList> SetCameraList(std::shared_ptr<MCCameraList> cameraList);

    /// <summary>Gives this context its own movement maps (null: the ones it was installed over, if any).</summary>
    /// <returns>The movement maps this context had.</returns>
    std::unique_ptr<MCMoveSystem> SetMoveSystem(std::unique_ptr<MCMoveSystem> moveSystem);

    /// <summary>Gives this context its own forces (null: the ones it was installed over, if any).</summary>
    /// <returns>The forces this context had.</returns>
    std::unique_ptr<MCForces> SetForces(std::unique_ptr<MCForces> forces);

    /// <summary>Gives this context its own sensors and contacts (null: the ones it was installed over, if any).</summary>
    /// <returns>The contact system this context had.</returns>
    std::unique_ptr<MCContactSystem> SetContactSystem(std::unique_ptr<MCContactSystem> contactSystem);

    /// <summary>Gives this context its own collision system (null: the one it was installed over, if any).</summary>
    /// <returns>The collision system this context had.</returns>
    std::unique_ptr<MCCollisionSystem> SetCollisionSystem(std::unique_ptr<MCCollisionSystem> collisionSystem);

    /// <summary>Gives this context its own fires and smoke (null: the ones it was installed over, if any).</summary>
    /// <returns>The effect system this context had.</returns>
    std::unique_ptr<MCEffectSystem> SetEffectSystem(std::unique_ptr<MCEffectSystem> effectSystem);

    /// <summary>Gives this context its own trains (null: the ones it was installed over, if any).</summary>
    /// <returns>The train manager this context had.</returns>
    std::unique_ptr<MCTrainManager> SetTrainManager(std::unique_ptr<MCTrainManager> trainManager);

    /// <summary>Gives this context its own object system (null: the one it was installed over, if any).</summary>
    /// <returns>The object system this context had.</returns>
    std::shared_ptr<MCObjectSystem> SetObjectSystem(std::shared_ptr<MCObjectSystem> objectSystem);

    /// <summary>Gives this context its own ABL symbol table (null: the one it was installed over, if any).</summary>
    /// <returns>The symbol table this context had.</returns>
    std::shared_ptr<MCAblSymbolTable> SetAblSymbols(std::shared_ptr<MCAblSymbolTable> symbols);

    /// <summary>Installs ABL's runtime (AblInit); returns the one it replaces.</summary>
    std::shared_ptr<MCAblRuntime> SetAblRuntime(std::shared_ptr<MCAblRuntime> runtime);

    /// <summary>Gives this context its own sound renderer (null: the one it was installed over, if any).</summary>
    /// <returns>The renderer this context had.</returns>
    std::shared_ptr<MCSoundRenderer> SetSoundRenderer(std::shared_ptr<MCSoundRenderer> renderer);

    /// <summary>Gives this context its own sound system (null: the one it was installed over, if any).</summary>
    /// <returns>The sound system this context had.</returns>
    std::shared_ptr<MCSoundSystem> SetSoundSystem(std::shared_ptr<MCSoundSystem> soundSystem);

    /// <summary>Gives this context its own mission (null: the one it was installed over, if any).</summary>
    /// <returns>The mission this context had.</returns>
    std::shared_ptr<MCMission> SetMission(std::shared_ptr<MCMission> mission);

    /// <summary>Gives this context its own scenario (null: the one it was installed over, if any).</summary>
    /// <returns>The scenario this context had.</returns>
    std::shared_ptr<MCScenario> SetScenario(std::shared_ptr<MCScenario> scenario);

    /// <summary>Gives this context its own multiplayer game (null: the one it was installed over, if any).</summary>
    /// <returns>The game this context had.</returns>
    std::shared_ptr<MCMultiPlayer> SetMultiPlayer(std::shared_ptr<MCMultiPlayer> multiPlayer);

    /// <summary>Gives this context its own tactical interface (null: the one it was installed over, if any).</summary>
    /// <returns>The interface this context had.</returns>
    std::shared_ptr<MCTacticalInterface> SetTacticalInterface(std::shared_ptr<MCTacticalInterface> tacticalInterface);

    /// <summary>Gives this context its own GUI system (null: the one it was installed over, if any).</summary>
    /// <returns>The GUI system this context had.</returns>
    std::shared_ptr<MCGuiSystem> SetGuiSystem(std::shared_ptr<MCGuiSystem> guiSystem);

private:
    friend class MCTestContextScope;

    /// <summary>Stores <paramref name="service"/> in <paramref name="slot"/> and returns it.</summary>
    template <typename Base, typename T> static T& Install(std::unique_ptr<Base>& slot, std::unique_ptr<T> service)
    {
        T& installed = *service;
        slot = std::move(service);
        return installed;
    }

    /// <summary>Stores <paramref name="service"/> in the shared <paramref name="slot"/> and returns it.</summary>
    template <typename Base, typename T> static T& Install(std::shared_ptr<Base>& slot, std::unique_ptr<T> service)
    {
        T& installed = *service;
        slot = std::move(service);
        return installed;
    }

    /// <summary>The context the game runs in now, shared (the first one when no other is installed).</summary>
    static std::shared_ptr<MCGameContext> CurrentShared();

    /// <summary>Makes <paramref name="context"/> the current one (null: the first one).</summary>
    static void SetCurrent(std::shared_ptr<MCGameContext> context);

    /// <summary>
    /// The context whose <paramref name="Slot"/> holds a service or system: this one, else the nearest one it was
    /// installed over (none when no context has one).
    /// </summary>
    template <auto Slot> MCOptionalRef<const MCGameContext> Holder() const;

    /// <summary>The service or system in the nearest <paramref name="Slot"/> that has one (asserted).</summary>
    template <auto Slot> auto& Find() const;

    /// <summary>The shared system in the nearest <paramref name="Slot"/> that has one (null when none).</summary>
    template <auto Slot> auto FindShared() const;

    /// <summary>Empties every slot, last to first, while the context may still be the current one.</summary>
    void Clear();

    /// <summary>Where the services this context doesn't own come from; null only for the first context.</summary>
    std::shared_ptr<MCGameContext> _Parent;
    std::unique_ptr<MCClock> _Clock;
    std::unique_ptr<MCRandom> _Random;
    std::unique_ptr<MCFileSource> _Files;
    /// <summary>Shared, so the mixers it opens can tell it of their buffers through a weak_ptr.</summary>
    std::shared_ptr<MCAudioDevice> _Audio;
    std::unique_ptr<MCNetTransport> _Net;
    std::unique_ptr<MCFastFileSet> _FastFiles;
    std::optional<uint64_t> _SharedDataHash;
    std::unique_ptr<MCPalette> _Palette;
    std::unique_ptr<MCElementBuffer> _ElementList;
    std::unique_ptr<MCCraterManager> _CraterManager;
    std::shared_ptr<MCSpriteManager> _SpriteManager;
    std::unique_ptr<MCAppearanceTypeList> _AppearanceTypeList;
    std::shared_ptr<MCTerrain> _Terrain;
    std::shared_ptr<MCCameraList> _CameraList;
    std::unique_ptr<MCMoveSystem> _MoveSystem;
    std::unique_ptr<MCForces> _Forces;
    std::unique_ptr<MCContactSystem> _ContactSystem;
    std::unique_ptr<MCCollisionSystem> _CollisionSystem;
    std::unique_ptr<MCEffectSystem> _EffectSystem;
    std::unique_ptr<MCTrainManager> _TrainManager;
    /// <summary>After the systems its objects reach for as they go: it goes before them.</summary>
    std::shared_ptr<MCObjectSystem> _ObjectSystem;
    std::shared_ptr<MCAblSymbolTable> _AblSymbols;
    /// <summary>Declared after the symbols: it goes first.</summary>
    std::shared_ptr<MCAblRuntime> _AblRuntime;
    std::shared_ptr<MCSoundRenderer> _SoundRenderer;
    /// <summary>Declared after the renderer: it goes first (it stops its channels and frees its resources).</summary>
    std::shared_ptr<MCSoundSystem> _SoundSystem;
    /// <summary>Goes first, before the objects (its own and the parts' still reach for every system).</summary>
    std::shared_ptr<MCScenario> _Scenario;
    /// <summary>Goes right after the scenario (with it gone, its results screen doesn't end it again).</summary>
    std::shared_ptr<MCMission> _Mission;
    /// <summary>Goes after the mission (its end reaches for the game), before the systems its handlers use.</summary>
    std::shared_ptr<MCMultiPlayer> _MultiPlayer;
    /// <summary>Goes after the objects (a mover going takes itself off the mech bar), before the other systems.</summary>
    std::shared_ptr<MCTacticalInterface> _TacticalInterface;
    /// <summary>
    /// Goes after every system holding GUI objects (they take themselves off it as they go). Shared, so the system can
    /// be referred to as a GUI object (a script's timer is the system's).
    /// </summary>
    std::shared_ptr<MCGuiSystem> _GuiSystem;
};

/// <summary>
/// Installs a fresh context over the current one for its lifetime, then puts the previous one back. Its services
/// come from the previous context until the test gives it its own (<see cref="Context"/>). Scopes nest, and must end
/// in the reverse order they began.
/// </summary>
class MCTestContextScope
{
public:
    MCTestContextScope();
    ~MCTestContextScope();
    MCTestContextScope(const MCTestContextScope&) = delete;
    MCTestContextScope& operator=(const MCTestContextScope&) = delete;

    /// <summary>The installed context, to give it services.</summary>
    MCGameContext& Context() { return *_Context; }

private:
    /// <summary>The context that was current before.</summary>
    std::shared_ptr<MCGameContext> _Previous;
    /// <summary>Emptied while still current: the objects of its systems reach for them as they go.</summary>
    std::shared_ptr<MCGameContext> _Context;
};

#pragma once

#include "lib/MCPacketFile.h"
#include "platform/MCBlockStore.h"

class MCBaseObject;
class MCObjectType;

/// <summary>
/// Loads and keeps the object types: the types in use (each until its last object goes, unless kept), the object
/// packet file they are read from, and two block stores for type and object data that has no other owner yet.
/// </summary>
/// <remarks>Original source: <c>object\objtype.cpp</c> (a LinkedList of types). Part of the
/// <see cref="MCObjectSystem"/>.</remarks>
class MCObjectTypeManager
{
public:
    /// <summary>A manager with no packet file (a test's types are added by hand).</summary>
    MCObjectTypeManager();
    /// <summary>Deletes the types still loaded and empties both block stores.</summary>
    ~MCObjectTypeManager();
    MCObjectTypeManager(const MCObjectTypeManager&) = delete;
    MCObjectTypeManager& operator=(const MCObjectTypeManager&) = delete;

    /// <summary>Opens the object packet file <paramref name="objectFileName"/>.pak in the object folder.</summary>
    static std::expected<std::unique_ptr<MCObjectTypeManager>, std::string> Create(std::string_view objectFileName);

    /// <summary>Adds a loaded type to the end of the list; returns it.</summary>
    std::shared_ptr<MCObjectType> Add(std::shared_ptr<MCObjectType> objType);
    /// <summary>
    /// Releases one user of <paramref name="objType"/>; the type leaves the list when it has no users left and isn't
    /// kept (it goes with the last object that holds it).
    /// </summary>
    void Remove(MCObjectType& objType);
    /// <summary>The loaded type numbered <paramref name="objTypeNum"/>, or null.</summary>
    std::shared_ptr<MCObjectType> Find(int32_t objTypeNum) const;
    /// <summary>Types loaded.</summary>
    size_t Size() const { return _Types.size(); }
    /// <summary>Port-only: the types loaded (MCParticles::MeasureExplosions).</summary>
    std::span<const std::shared_ptr<MCObjectType>> Types() const { return _Types; }
    /// <summary>Makes a new object of type <paramref name="objTypeNum"/>, loading the type if needed.</summary>
    std::shared_ptr<MCBaseObject> Get(int32_t objTypeNum);
    /// <summary>
    /// Loads type <paramref name="objTypeNum"/> from the packet file: reads its "ObjectClass" block to pick the
    /// class, then lets the new type read its packet. <paramref name="keepMe"/> marks it kept and preloads its
    /// appearance and explosion. Null when the type is loaded already, and for a negative or 0 number.
    /// </summary>
    std::shared_ptr<MCObjectType> Load(int32_t objTypeNum, int keepMe);

    /// <summary>
    /// Port-only (SPEC-first-use.md: the original's packets at the load): loads what the types loaded can make in play
    /// (MCObjectType::MadeTypes: wrecks, explosions, shots' effects and lights, fires), and theirs, round by round, each
    /// round's packets read together (MCPacketFile::HoldPackets), each kept for the mission as a kept type is (its
    /// appearance with it), so that none is read as it's first made. A type whose packet won't make one is left to fail
    /// as the original fails, if the mission ever makes it.
    /// </summary>
    /// <returns>The packets read together, their reads and their bytes.</returns>
    MCPacketsHeld LoadMadeTypes();

    /// <summary>
    /// Type data the types load (shapes, hot spot and stage tables): the original's type heap, which freed whatever
    /// a type left when the object system stopped.
    /// </summary>
    MCBlockStore TypeData;
    /// <summary>
    /// Object data with no single owner (the camera's pause and asked shapes): the original's object heap.
    /// </summary>
    MCBlockStore ObjectData;

private:
    /// <summary>
    /// <see cref="Load"/>, which with <paramref name="fatal"/> false gives null for a packet that won't make a type (its
    /// "ObjectClass" block missing or of no class, its Init failing) rather than stopping the game.
    /// </summary>
    std::shared_ptr<MCObjectType> LoadType(int32_t objTypeNum, int keepMe, bool fatal);

    /// <summary>The object packet file every type is read from.</summary>
    std::unique_ptr<MCPacketFile> _ObjectFile;
    /// <summary>The types loaded, in the order they were.</summary>
    std::vector<std::shared_ptr<MCObjectType>> _Types;
};

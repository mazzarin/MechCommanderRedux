#pragma once

#include "lib/MCFrameOfRef.h"
#include "lib/MCVector2D.h"
#include "lib/MCDice.h"

class MCAppearance;
class MCObjectEvent;
class MCObjectType;

/// <summary>
/// What an object is (<see cref="MCBaseObject::ObjectClass"/>). The values are the ones each class's init stores; the
/// names are the port's. Values no init in MCX.EXE stores (1, 0xb-0xd, 0xf, 0x14, 0x17) have no name, but for those its
/// code still tests (Unused1, Unused14, Unused17).
/// </summary>
enum class MCObjectClass : int32_t
{
    BaseObject = 0,
    /// <summary>No init stores it; the original's targeting tests it with the buildings and terrain objects.</summary>
    Unused1 = 1,
    BattleMech = 2,
    GroundVehicle = 3,
    Elemental = 4,
    Explosion = 5,
    Fire = 6,
    Artillery = 7,
    /// <summary>A <c>Mover</c> that is not yet a mech, vehicle or elemental (set by <c>Mover::init</c>).</summary>
    Mover = 8,
    GameObject = 9,
    BigGameObject = 10,
    Laser = 0xe,
    Building = 0x10,
    Smoke = 0x11,
    Bullet = 0x12,
    Debris = 0x13,
    /// <summary>No init stores it; the original's mouse code tests it with artillery and debris.</summary>
    Unused14 = 0x14,
    Tree = 0x15,
    TerrainObject = 0x16,
    /// <summary>No init stores it; the original's code tests it with the terrain objects.</summary>
    Unused17 = 0x17,
    MiscTerrainObject = 0x18,
    Jet = 0x19,
    ProjectileLaser = 0x1a,
    TreeBuilding = 0x1b,
    CameraDrone = 0x1c,
    TrainCar = 0x1d,
    Turret = 0x1e,
    Gate = 0x1f,
    Light = 0x20
};

/// <summary>Whether <paramref name="objectClass"/> is a mover's: a mech, vehicle, elemental or plain mover.</summary>
constexpr bool IsMoverClass(MCObjectClass objectClass)
{
    return objectClass == MCObjectClass::BattleMech || objectClass == MCObjectClass::GroundVehicle ||
           objectClass == MCObjectClass::Elemental || objectClass == MCObjectClass::Mover;
}

/// <summary>
/// The root of every object in the world: its class, id and part id. Objects are made by their
/// <see cref="MCObjectType"/>'s <c>CreateInstance</c> as a <c>std::shared_ptr</c>, owned by the
/// <see cref="MCObjectList"/> that holds them (or by the object they belong to), and referred to everywhere else by
/// <c>std::weak_ptr</c>, read with <see cref="LockObject"/>.
/// </summary>
/// <remarks>
/// Original source: <c>object\baseobj.h</c>, <c>object\baseobj.cpp</c>. The hierarchy keeps MCX.EXE's two-phase set-up:
/// the constructor, then <c>Init</c> from the type, whose failure the type's <c>CreateInstance</c> answers with no
/// object.
/// </remarks>
class MCBaseObject : public std::enable_shared_from_this<MCBaseObject>
{
public:
    MCBaseObject() = default;
    virtual ~MCBaseObject() = default;
    MCBaseObject(const MCBaseObject&) = delete;
    MCBaseObject& operator=(const MCBaseObject&) = delete;

    /// <summary>Sets the object up for its type; the base keeps its fields.</summary>
    virtual int32_t Init(std::shared_ptr<MCObjectType> objType) { return 0; }
    /// <summary>The object's type (none for a bare BaseObject).</summary>
    virtual std::shared_ptr<MCObjectType> GetObjectType() { return nullptr; }
    virtual int32_t Kill() { return 0; }
    /// <summary>Runs one frame of the object; nonzero keeps it in the world.</summary>
    virtual int32_t Update() { return 0; }
    /// <summary>Draws the object.</summary>
    virtual void Render() {}
    virtual std::shared_ptr<MCAppearance> GetAppearance() { return nullptr; }
    virtual int32_t HandleEvent(MCObjectEvent& event) { return 0; }
    /// <summary>The world position of hot spot <paramref name="hotSpot"/> of the object's appearance.</summary>
    virtual MCVector3D GetPositionFromHS(uint32_t hotSpot);
    virtual int UnderPlayerControl() { return 0; }
    virtual int32_t GetGroupId() { return -1; }
    virtual int GetUseMe() { return 0; }
    virtual void SetAwake(int awake) {}
    virtual void SetPartId(int32_t newPartId) { PartId = newPartId; }
    /// <summary>Resolves collisions with the terrain objects around the object.</summary>
    virtual void HandleStaticCollision() {}
    /// <summary>The terrain block and vertex the object stands on (-1, -1 for a bare BaseObject).</summary>
    virtual void GetBlockAndVertexNumber(int32_t& blockNumber, int32_t& vertexNumber)
    {
        blockNumber = -1;
        vertexNumber = -1;
    }

    /// <summary>The object as a <typeparamref name="T"/> that shares its ownership.</summary>
    template <typename T> std::shared_ptr<T> SharedAs() { return std::static_pointer_cast<T>(shared_from_this()); }
    /// <summary>A reference to the object as a <typeparamref name="T"/> (empty when no shared_ptr owns it).</summary>
    template <typename T> std::weak_ptr<T> WeakAs() { return std::static_pointer_cast<T>(weak_from_this().lock()); }
    /// <summary>
    /// Whether the object left the world: its list let it go at the moment MCX.EXE deleted it. A walk that pins it
    /// keeps its memory a little longer, but every reference reads it as gone (<see cref="LockObject"/>).
    /// </summary>
    bool IsRemoved() const { return _Removed; }
    /// <summary>Marks the object gone from the world (the list that removes it calls this).</summary>
    void MarkRemoved() { _Removed = true; }

    /// <summary>What the object is.</summary>
    MCObjectClass ObjectClass = MCObjectClass::BaseObject;
    /// <summary>The unique id <c>ObjectType::createInstance</c> gives it (from <c>NextIdNumber</c>).</summary>
    uint32_t IdNumber = 0;
    /// <summary>The object's part id in the scenario (-1 when it has none).</summary>
    int32_t PartId = -1;

private:
    /// <summary>Set when the object left the world.</summary>
    bool _Removed = false;
};

/// <summary>
/// The object <paramref name="reference"/> refers to while it is in the world; null once it was removed (even while a
/// walk still pins it) or freed. The pin it returns keeps the object for the caller's use.
/// </summary>
template <typename T> std::shared_ptr<T> LockObject(const std::weak_ptr<T>& reference)
{
    std::shared_ptr<T> object = reference.lock();

    if (object != nullptr && object->IsRemoved())
    {
        return nullptr;
    }

    return object;
}

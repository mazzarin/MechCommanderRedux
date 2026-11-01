#pragma once

#include "lib/MCVector3D.h"

class MCBaseObject;
class MCFile;
class MCFitIniFile;
class MCGameObject;

/// <summary>
/// The id the next object made gets (<see cref="MCBaseObject::IdNumber"/>); every <c>CreateInstance</c> takes it and
/// counts up.
/// </summary>
extern uint32_t NextIdNumber;

/// <summary>
/// Reads the effect id <paramref name="name"/> as the original's unchecked reads did: the file's value, 0 when the entry
/// is missing, <paramref name="value"/> left alone on any other error.
/// </summary>
void ReadEffectId(MCFitIniFile& file, std::string_view name, uint32_t& value);

/// <summary>
/// Port-only (HD shake, PREFS "HDShake"): whether an explosion shakes the picture (MCParticles), by what set it off: a
/// large artillery strike's explosions and a building's end do, nothing else.
/// </summary>
enum class MCBlastShake : uint8_t
{
    None,
    Shakes,
};

/// <summary>
/// The common part of every object type: what it looks like, what it leaves behind when destroyed or when it
/// explodes, its size, icon and alignment. A type is loaded once from the object packet file (<c>OBJECT.PAK</c>) and
/// shared, reference counted, by every object made from it.
/// </summary>
/// <remarks>
/// Original source: <c>object\objtype.h</c>, <c>object\objtype.cpp</c>. The FIT block is "ObjectType".
/// </remarks>
class MCObjectType : public std::enable_shared_from_this<MCObjectType>
{
public:
    MCObjectType() = default;
    virtual ~MCObjectType() = default;
    MCObjectType(const MCObjectType&) = delete;
    MCObjectType& operator=(const MCObjectType&) = delete;

    /// <summary>
    /// Reads the type from its packet (<paramref name="objFile"/>, <paramref name="fileSize"/> bytes); the base reads
    /// nothing.
    /// </summary>
    virtual int32_t Init(MCFile& objFile, uint32_t fileSize) { return 0; }
    /// <summary>Makes a new object of this type and gives it the next id number.</summary>
    virtual std::shared_ptr<MCBaseObject> CreateInstance();
    /// <summary>
    /// What happens when <paramref name="collidee"/> (of this type) and <paramref name="collider"/> touch. Nonzero
    /// lets the collision proceed.
    /// </summary>
    virtual int HandleCollision(MCGameObject& collidee, MCGameObject& collider) { return 1; }
    /// <summary>
    /// <paramref name="collidee"/> (of this type) was destroyed by <paramref name="collider"/>: makes the type's
    /// explosion at its position.
    /// </summary>
    virtual int HandleDestruction(MCGameObject& collidee, uint32_t colliderId);

    /// <summary>
    /// Reads the "ObjectType" block: Type, Appearance, ExplosionObject, DestroyedObject, ExtentRadius, and the
    /// optional KeepMe, IconNumber and Alignment.
    /// </summary>
    int32_t Init(MCFitIniFile& typeFile);
    /// <summary>
    /// Makes the type's explosion object at <paramref name="position"/>; with a nonzero <paramref name="radius"/>
    /// it also gets that radius and <paramref name="damage"/>. The explosion goes on the end of the first object list.
    /// Port-only: it shakes the picture as <paramref name="shake"/> says (MCExplosion::Shakes).
    /// </summary>
    void CreateExplosion(MCVector3D& position, float damage, float radius,
                         MCBlastShake shake = MCBlastShake::None) const;

    /// <summary>The type's number: its packet in the object file.</summary>
    int32_t ObjTypeNum = 0;
    /// <summary>How many objects of this type exist; the type is freed when the last goes (unless kept).</summary>
    int32_t NumUsers = 0;
    /// <summary>The FIT's "Type".</summary>
    int32_t TypeClass = -1;
    /// <summary>The type number of what the object leaves when destroyed (-1: nothing).</summary>
    int32_t DestroyedObject = -1;
    /// <summary>The type number of the explosion the object makes (-1: none).</summary>
    int32_t ExplosionObject = -1;
    /// <summary>The appearance type id (FIT "Appearance").</summary>
    uint32_t AppearName = 0;
    /// <summary>The object's extent (collision) radius.</summary>
    float ExtentRadius = 0.0f;
    /// <summary>Nonzero keeps the type loaded when no object uses it.</summary>
    int32_t KeepMe = 0;
    /// <summary>The object's icon (-1: none).</summary>
    int32_t IconNumber = -1;
    /// <summary>The alignment (team side) objects of this type start with (FIT "Alignment").</summary>
    int32_t TeamId = 0;
};

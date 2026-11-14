#pragma once

#include "lib/MCFitIniFile.h"
#include "sprite/MCHdArt.h"

class MCAppearance;
class MCShape;

/// <summary>
/// The appearance classes: the top byte of an appearance type id (<c>typeId &gt;&gt; 24</c>) picks the class
/// <see cref="MCAppearanceTypeList::GetAppearance"/> builds. The low 24 bits are the packet in the sprite PAK.
/// The values are the original's (its switch); the names are the port's.
/// </summary>
enum class MCAppearanceClass : uint32_t
{
    /// <summary><see cref="MCSpriteTree"/> (mechs).</summary>
    SpriteTree = 1,
    /// <summary><see cref="MCVfxAppearanceType"/>.</summary>
    Vfx = 2,
    /// <summary>A line type: nothing but its bounds is read.</summary>
    Line = 4,
    /// <summary><see cref="MCGVAppearanceType"/> (ground vehicles).</summary>
    GroundVehicle = 5,
    /// <summary><see cref="MCArmAppearanceType"/> (weapon effects).</summary>
    Arm = 6,
    /// <summary><see cref="MCVfxBuildingAppearanceType"/>.</summary>
    Building = 7,
    /// <summary><see cref="MCElementalTree"/>.</summary>
    ElementalTree = 8,
    /// <summary><see cref="MCPUAppearanceType"/> (pop-up turrets).</summary>
    PopUp = 9
};

/// <summary>Why an appearance type couldn't be loaded.</summary>
using MCAppearanceLoad = std::expected<void, std::string>;

/// <summary>
/// An appearance type's FIT packet, read entry by entry. The first failure (the packet isn't a FIT file, a block or
/// a required entry is missing) is kept, and the reads after it give nothing: the loader reads on and returns
/// <see cref="Result"/> at the end.
/// </summary>
class MCAppearanceFit
{
public:
    /// <summary>Opens the FIT file stored in the next <paramref name="size"/> bytes of <paramref name="packet"/>.</summary>
    MCAppearanceFit(MCFile& packet, uint32_t size);

    /// <summary>Makes block <paramref name="block"/> current; a missing block is a failure.</summary>
    bool SeekBlock(std::string_view block);

    /// <summary>Makes block <paramref name="block"/> current when the file has it (no failure when it hasn't).</summary>
    bool HasBlock(std::string_view block);

    /// <summary>The required entry <paramref name="name"/> (a default value after a failure).</summary>
    template <MCFitValue T> T Read(std::string_view name)
    {
        if (!_Error.empty())
        {
            return T{};
        }

        MCFitResult<T> value = _File.Read<T>(name);

        if (!value.has_value())
        {
            Fail(name, value.error());
            return T{};
        }

        return std::move(*value);
    }

    /// <summary>The optional entry <paramref name="name"/>, or <paramref name="fallback"/> when it can't be read.</summary>
    template <MCFitValue T> T Read(std::string_view name, T fallback)
    {
        MCFitResult<T> value = _File.Read<T>(name);
        return value.has_value() ? std::move(*value) : fallback;
    }

    /// <summary>The required array entry <paramref name="name"/> into <paramref name="values"/>.</summary>
    template <MCFitNumber T> void ReadArray(std::string_view name, std::span<T> values)
    {
        if (!_Error.empty())
        {
            return;
        }

        const MCFitResult<uint32_t> count = _File.ReadArray(name, values);

        if (!count.has_value())
        {
            Fail(name, count.error());
        }
    }

    /// <summary>Success, or the first failure.</summary>
    MCAppearanceLoad Result() const;

    /// <summary>Whether a read has failed.</summary>
    bool Failed() const { return !_Error.empty(); }

private:
    /// <summary>Keeps the failure to read <paramref name="what"/>.</summary>
    void Fail(std::string_view what, MCFitError error);

    /// <summary>The FIT file.</summary>
    MCFitIniFile _File;
    /// <summary>The first failure, empty while there is none.</summary>
    std::string _Error;
};

/// <summary>
/// What a kind of appearance shares between its instances: its shapes, gestures and bounds, loaded once from its
/// packet of the sprite PAK and counted by <see cref="MCAppearanceTypeList"/>.
/// </summary>
/// <remarks>
/// Original source: <c>appear\apprtype.cpp</c>, <c>appear\apprtype.h</c>. Its packet is a FIT text file;
/// <see cref="LoadBounds"/> reads the "Bounds" block after the class's own <see cref="Load"/>. The list and the
/// appearances bound to it share it; its shapes refer back to it by <c>std::weak_ptr</c>.
/// </remarks>
class MCAppearanceType : public std::enable_shared_from_this<MCAppearanceType>
{
public:
    MCAppearanceType() = default;
    virtual ~MCAppearanceType() = default;
    MCAppearanceType(const MCAppearanceType&) = delete;
    MCAppearanceType& operator=(const MCAppearanceType&) = delete;

    /// <summary>
    /// Loads the class's own data from <paramref name="apprFile"/> (its packet, <paramref name="fileSize"/> bytes).
    /// <see cref="AppearanceNum"/> is set. The base reads nothing.
    /// </summary>
    virtual MCAppearanceLoad Load(MCFile& apprFile, uint32_t fileSize) { return {}; }

    /// <summary>Reads the optional "Bounds" block (UpperLeftX/Y, LowerRightX/Y) of the type's FIT packet.</summary>
    MCAppearanceLoad LoadBounds(MCFile& apprFile, uint32_t fileSize);

    /// <summary>How many appearances hold the type (the list drops it at 0).</summary>
    int32_t NumUsers = 0;
    /// <summary>The type id: class in the top byte, packet number below.</summary>
    uint32_t AppearanceNum = 0xffffffff;
    /// <summary>Selection bounds relative to the object's screen position (FIT "Bounds"); all 0 when absent.</summary>
    int32_t BoundsUpperLeftX = 0;
    int32_t BoundsUpperLeftY = 0;
    int32_t BoundsLowerRightX = 0;
    int32_t BoundsLowerRightY = 0;

    /// <summary>
    /// Port-only (SPEC-first-use.md: the camera's surroundings): while one lives, the types' shape lookups (GetShape,
    /// GetGesture: PacketShape) load nothing: each has HD art's readers read the packet it would load whole instead
    /// (once a packet, MCHdArt::ReadPacketAhead) and gives only a shape already loaded. An appearance's ReadAhead makes
    /// its Render's lookups under one, so its packets are asked for as its Render would draw them. The game's thread's.
    /// </summary>
    class MCAskingAhead
    {
    public:
        MCAskingAhead() { Asking = true; }
        ~MCAskingAhead() { Asking = false; }
        MCAskingAhead(const MCAskingAhead&) = delete;
        MCAskingAhead& operator=(const MCAskingAhead&) = delete;

    private:
        friend class MCAppearanceType;
        static inline bool Asking = false;
    };

protected:
    /// <summary>The packet in the sprite PAK whose PAK holds the type's shapes.</summary>
    uint32_t ShapeFileNum() const { return AppearanceNum & 0xffffff; }

    /// <summary>
    /// Sizes <paramref name="shapes"/> to the packets of the type's PAK, all unloaded. A PAK without packets fails
    /// the load, as the original's empty shape list (a zero-byte allocation) did.
    /// </summary>
    MCAppearanceLoad MakeShapeList(std::vector<std::weak_ptr<MCShape>>& shapes) const;

    /// <summary>
    /// The shape of packet <paramref name="packet"/> of the type's PAK kept in <paramref name="slot"/>: the cached
    /// one (marked used this turn), else loaded from the sprite manager into the slot. Null when it can't be loaded.
    /// </summary>
    std::shared_ptr<MCShape> PacketShape(std::weak_ptr<MCShape>& slot, uint32_t packet);

private:
    /// <summary>
    /// Port-only (MCAskingAhead): has HD art's readers read packet <paramref name="packet"/> of the type's PAK whole,
    /// unless asked for since the readers last forgot what they read (MCHdArt::MCAskedPackets).
    /// </summary>
    void AskPacketAhead(uint32_t packet);

    /// <summary>Port-only: the packets asked to be read ahead (AskPacketAhead).</summary>
    MCHdArt::MCAskedPackets _AskedAhead;
};

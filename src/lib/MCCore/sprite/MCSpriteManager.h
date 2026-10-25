#pragma once

#include "main/MCGameContext.h"
#include "platform/MCRegisteredBlock.h"

class MCAppearanceType;
class MCPacketFile;
class MCShape;

/// <summary>The four parts a mech is drawn from, in the order of the part PAKs.</summary>
enum class MCMechPart : int32_t
{
    Legs = 0,
    Torso = 1,
    RightArm = 2,
    LeftArm = 3
};

/// <summary>The number of mech parts.</summary>
inline constexpr int32_t MechPartCount = 4;

/// <summary>One value per mech part, indexed by <see cref="MCMechPart"/> or by number.</summary>
template <typename T> struct MCMechParts : std::array<T, MechPartCount>
{
    using std::array<T, MechPartCount>::operator[];

    /// <summary>The value of <paramref name="part"/>.</summary>
    T& operator[](MCMechPart part) { return (*this)[static_cast<size_t>(part)]; }

    /// <summary>The value of <paramref name="part"/>.</summary>
    const T& operator[](MCMechPart part) const { return (*this)[static_cast<size_t>(part)]; }
};

/// <summary>
/// The sprite cache: the sprite PAKs (one packet per appearance, itself a PAK of that appearance's shapes), the mech
/// part PAKs (one packet per mech, likewise), the mech shadow shapes, and the loaded shapes in the order they were
/// loaded.
/// </summary>
/// <remarks>
/// Original source: <c>sprite\sprtmgr.cpp</c>. The original kept the shapes and the types' tables in two heaps sized
/// by the scenario; the cache now owns its shapes and each type its tables, and nothing runs out.
/// </remarks>
class MCSpriteManager
{
    /// <summary>Only <see cref="Create"/> makes one.</summary>
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>A manager with nothing open (<see cref="Create"/> opens its files).</summary>
    explicit MCSpriteManager(Key);

    ~MCSpriteManager();
    MCSpriteManager(const MCSpriteManager&) = delete;
    MCSpriteManager& operator=(const MCSpriteManager&) = delete;

    /// <summary>
    /// Opens the sprite PAK <paramref name="spriteFileName"/> ("&lt;name&gt;90.pak", "&lt;name&gt;.pak" in the demo,
    /// with "&lt;name&gt;.pak" as the fallback), the mech part PAKs (legs, torsos, rArms, lArms; their "90" versions
    /// too when <paramref name="use90PixelParts"/>), each from the sprite path or the CD's, and loads the mech
    /// shadows.
    /// </summary>
    static std::expected<std::unique_ptr<MCSpriteManager>, std::string> Create(std::string_view spriteFileName,
                                                                               bool use90PixelParts);

    /// <summary>
    /// Loads packet <paramref name="packetNum"/> of appearance <paramref name="appearanceNum"/>'s PAK as a shape
    /// used by <paramref name="owner"/>, used at <paramref name="turnUsed"/>. Null when the appearance or the packet
    /// is empty or missing. The cache owns the shape; the caller keeps a <c>std::weak_ptr</c>.
    /// </summary>
    std::shared_ptr<MCShape> GetShapeData(uint32_t appearanceNum, uint32_t packetNum, int32_t turnUsed,
                                          std::weak_ptr<MCAppearanceType> owner);

    /// <summary>
    /// The same for packet <paramref name="packetNum"/> of <paramref name="part"/> of mech
    /// <paramref name="mechNum"/>, from the 90-pixel part PAKs when they are open and <paramref name="zoomedOut"/>.
    /// </summary>
    std::shared_ptr<MCShape> GetMechShapeData(uint32_t mechNum, uint32_t packetNum, MCMechPart part, int32_t turnUsed,
                                              std::weak_ptr<MCAppearanceType> owner, bool zoomedOut);

    /// <summary>
    /// The name of the part PAK <see cref="GetMechShapeData"/> reads <paramref name="part"/> from with
    /// <paramref name="zoomedOut"/> ("legs", or "legs90" when the 90-pixel PAKs are open).
    /// </summary>
    std::string PartFileName(MCMechPart part, bool zoomedOut) const;

    /// <summary>The number of packets in appearance <paramref name="appearanceNum"/>'s PAK (0 when it has none).</summary>
    int32_t GetNumShapes(uint32_t appearanceNum);

    /// <summary>Frees every shape not used this turn or the last, and every ownerless one.</summary>
    void DumpLru();

    /// <summary>Frees every shape.</summary>
    void DumpAll();

    /// <summary>Mech shadow shape <paramref name="index"/> (one table of 32 facings), or empty when there is none.</summary>
    std::span<const uint8_t> MechShadow(size_t index) const;

    /// <summary>The loaded shapes, oldest first.</summary>
    std::span<const std::shared_ptr<MCShape>> Shapes() const { return _Shapes; }

    /// <summary>The number of appearances in the sprite PAK (its packets plus one).</summary>
    int32_t NumAppearances() const { return _NumAppearances; }

private:
    /// <summary>
    /// Opens the PAK of appearance <paramref name="appearanceNum"/> from a sprite PAK when first asked for (into
    /// <see cref="_AppearanceFiles"/>). False when it has none.
    /// </summary>
    bool OpenAppearanceFile(uint32_t appearanceNum);

    /// <summary>
    /// Reads the current packet (<paramref name="packetNum"/>, <paramref name="size"/> bytes) of
    /// <paramref name="file"/> into a new shape and adds it to the cache.
    /// </summary>
    std::shared_ptr<MCShape> LoadShape(MCPacketFile& file, uint32_t packetNum, uint32_t size, int32_t turnUsed,
                                       std::weak_ptr<MCAppearanceType> owner, bool checkSize);

    /// <summary>Removes the shapes <paramref name="dump"/> picks (their users' references expire).</summary>
    void Dump(const std::function<bool(const MCShape&)>& dump);

    /// <summary>The number of appearances in the sprite PAK.</summary>
    int32_t _NumAppearances = 0;
    /// <summary>
    /// The sprite PAKs: [0] the preferred one, [1] the fallback for appearances whose packet is empty in it. (The
    /// original picks between the two by packet size, not by zoom.)
    /// </summary>
    std::array<std::unique_ptr<MCPacketFile>, 2> _SpriteFiles;
    /// <summary>Each appearance's own PAK once opened (from whichever sprite PAK holds it).</summary>
    std::vector<std::unique_ptr<MCPacketFile>> _AppearanceFiles;
    /// <summary>The part PAKs (legs.pak, torsos.pak, rArms.pak, lArms.pak).</summary>
    MCMechParts<std::unique_ptr<MCPacketFile>> _PartFiles;
    /// <summary>The 90-pixel part PAKs; null unless the 90-pixel sprites are on.</summary>
    MCMechParts<std::unique_ptr<MCPacketFile>> _PartFiles90;
    /// <summary>Per part, each mech's own PAK once opened.</summary>
    MCMechParts<std::vector<std::unique_ptr<MCPacketFile>>> _MechFiles;
    /// <summary>Per part, each mech's own 90-pixel PAK once opened.</summary>
    MCMechParts<std::vector<std::unique_ptr<MCPacketFile>>> _MechFiles90;
    /// <summary>The mech shadow shapes (shadow.pak's packets).</summary>
    std::vector<MCRegisteredBlock> _MechShadows;
    /// <summary>The loaded shapes, oldest first.</summary>
    std::vector<std::shared_ptr<MCShape>> _Shapes;
};

/// <summary>The mission's sprite cache (null outside a mission); the caller keeps it as its pin.</summary>
inline std::shared_ptr<MCSpriteManager> SpriteManager()
{
    return MCGameContext::Current().SpriteManager();
}

/// <summary>Nonzero to use the 90-pixel mech part sprites when zoomed out (PREFS "Use90Pixel").</summary>
extern int Use90PixelSprite;

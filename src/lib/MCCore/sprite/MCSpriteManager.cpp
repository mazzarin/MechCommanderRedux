#include "stdafx.h"
#include "sprite/MCSpriteManager.h"
#include "appear/MCAppearanceType.h"
#include "lib/MCFatal.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "main/MCMissionGlobals.h"
#include "main/MCLogistics.h"
#include "platform/MCFrameLog.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCShape.h"

int Use90PixelSprite = 0;

namespace
{
    /// <summary>The part PAKs' names, in <see cref="MCMechPart"/> order.</summary>
    constexpr std::array<std::string_view, MechPartCount> PartFileNames = {"legs", "torsos", "rArms", "lArms"};

    /// <summary>The sprite PAKs' places in MCSpriteManager::_SpriteFiles: the preferred one, then the fallback.</summary>
    constexpr uint8_t PreferredSprites = 0;
    constexpr uint8_t FallbackSprites = 1;

    /// <summary><paramref name="text"/> in lower case (a PAK's name, as the HD art pack's folders spell it).</summary>
    std::string LowerCase(std::string_view text)
    {
        std::string lower(text);
        std::ranges::transform(lower, lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lower;
    }

    /// <summary>
    /// Opens the PAK <paramref name="name"/><paramref name="ext"/> from the sprite path, else
    /// <paramref name="name"/><paramref name="cdExt"/> from the CD's.
    /// </summary>
    auto OpenSpriteFile(std::string_view name, std::string_view ext, std::string_view cdExt)
        -> std::expected<std::unique_ptr<MCPacketFile>, std::string>
    {
        auto file = std::make_unique<MCPacketFile>();
        const std::string fileName = GamePath(SpritePath, name, ext);

        if (file->Open(fileName) == 0 || file->Open(GamePath(CDspritePath, name, cdExt)) == 0)
        {
            return file;
        }

        return std::unexpected(std::format("could not open {}", fileName));
    }

    /// <summary>The shadow PAK's packets, each a registered shape table; none when the PAK can't be opened.</summary>
    auto LoadMechShadows() -> std::vector<MCRegisteredBlock>
    {
        std::vector<MCRegisteredBlock> shadows;
        std::expected<std::unique_ptr<MCPacketFile>, std::string> file = OpenSpriteFile("shadow", ".pak", ".pak");

        if (!file.has_value())
        {
            return shadows;
        }

        MCPacketFile& pak = **file;

        for (int32_t i = 0; i < pak.GetNumPackets(); i++)
        {
            pak.SeekPacket(i);
            MCRegisteredBlock& shadow =
                shadows.emplace_back(static_cast<size_t>(pak.GetPacketSize()), MCDataKind::Shapes);
            pak.ReadPacket(i, shadow.Bytes());
        }

        return shadows;
    }
}

MCSpriteManager::MCSpriteManager(Key)
{
}

MCSpriteManager::~MCSpriteManager() = default;

auto MCSpriteManager::Create(std::string_view spriteFileName, bool use90PixelParts)
    -> std::expected<std::unique_ptr<MCSpriteManager>, std::string>
{
    auto manager = std::make_unique<MCSpriteManager>(Key{});

    // The preferred PAK is "<name>90.pak" ("<name>.pak" in the demo); faithful: the CD retry always adds "90.pak".
    const std::string_view preferredSuffix = InDemo == 0 ? "90" : "";
    std::expected<std::unique_ptr<MCPacketFile>, std::string> preferred =
        OpenSpriteFile(spriteFileName, std::format("{}.pak", preferredSuffix), "90.pak");

    if (!preferred.has_value())
    {
        return std::unexpected(preferred.error());
    }

    std::expected<std::unique_ptr<MCPacketFile>, std::string> fallback = OpenSpriteFile(spriteFileName, ".pak", ".pak");

    if (!fallback.has_value())
    {
        return std::unexpected(fallback.error());
    }

    manager->_NumAppearances = (*preferred)->GetNumPackets() + 1;
    manager->_SpriteFiles[PreferredSprites] = std::move(*preferred);
    manager->_SpriteFiles[FallbackSprites] = std::move(*fallback);
    manager->_SpriteFileNames[PreferredSprites] = LowerCase(std::format("{}{}", spriteFileName, preferredSuffix));
    manager->_SpriteFileNames[FallbackSprites] = LowerCase(spriteFileName);

    for (int32_t part = 0; part < MechPartCount; part++)
    {
        std::expected<std::unique_ptr<MCPacketFile>, std::string> file =
            OpenSpriteFile(PartFileNames[part], ".pak", ".pak");

        if (!file.has_value())
        {
            return std::unexpected(file.error());
        }

        manager->_PartFiles[part] = std::move(*file);
    }

    if (use90PixelParts)
    {
        for (int32_t part = 0; part < MechPartCount; part++)
        {
            std::expected<std::unique_ptr<MCPacketFile>, std::string> file =
                OpenSpriteFile(PartFileNames[part], "90.pak", "90.pak");

            if (!file.has_value())
            {
                return std::unexpected(file.error());
            }

            manager->_PartFiles90[part] = std::move(*file);
        }
    }

    manager->_MechShadows = LoadMechShadows();

    // Port-only: with HD art on, the shadows' edges are drawn smooth at the output's resolution.
    std::vector<MCAddressKey> shadowTables;

    for (const MCRegisteredBlock& shadow : manager->_MechShadows)
    {
        shadowTables.push_back(MCAddressKey::OfData(shadow.Bytes()));
    }

    MCHdArt::SetSmoothEdgeTables(shadowTables);

    // Port-only: the HD art pack's mech and object folders are numbered by these files; a pack records their sizes.
    std::vector<std::pair<std::string, uint32_t>> files = {
        {std::string(spriteFileName) + std::string(preferredSuffix),
         manager->_SpriteFiles[PreferredSprites]->GetLength()},
        {std::string(spriteFileName), manager->_SpriteFiles[FallbackSprites]->GetLength()}};

    for (int32_t part = 0; part < MechPartCount; part++)
    {
        files.emplace_back(PartFileNames[part], manager->_PartFiles[part]->GetLength());

        if (manager->_PartFiles90[part] != nullptr)
        {
            files.emplace_back(std::format("{}90", PartFileNames[part]), manager->_PartFiles90[part]->GetLength());
        }
    }

    MCHdArt::SetSpriteFiles(files);
    return manager;
}

auto MCSpriteManager::OpenAppearanceFile(uint32_t appearanceNum) -> bool
{
    if (appearanceNum < _AppearanceFiles.size() && _AppearanceFiles[appearanceNum] != nullptr)
    {
        return true;
    }

    // The first sprite PAK with a nonempty packet for it holds it: the preferred one, then the fallback.
    MCPacketFile& preferred = *_SpriteFiles[PreferredSprites];
    MCPacketFile& fallback = *_SpriteFiles[FallbackSprites];

    if (preferred.SeekPacket(static_cast<int32_t>(appearanceNum)) != 0 ||
        fallback.SeekPacket(static_cast<int32_t>(appearanceNum)) != 0)
    {
        return false;
    }

    const bool usePreferred = preferred.GetPacketSize() != 0;
    MCPacketFile& parent = usePreferred ? preferred : fallback;
    const int32_t size = parent.GetPacketSize();

    if (size == 0)
    {
        return false;
    }

    auto file = std::make_unique<MCPacketFile>();

    if (file->Open(parent, static_cast<uint32_t>(size)) != 0)
    {
        return false;
    }

    if (_AppearanceFiles.size() <= appearanceNum)
    {
        _AppearanceFiles.resize(appearanceNum + 1);
        _AppearanceSources.resize(appearanceNum + 1);
    }

    _AppearanceFiles[appearanceNum] = std::move(file);
    _AppearanceSources[appearanceNum] = usePreferred ? PreferredSprites : FallbackSprites;
    return true;
}

auto MCSpriteManager::ShapeFileName(uint32_t appearanceNum) -> std::string
{
    return OpenAppearanceFile(appearanceNum) ? _SpriteFileNames[_AppearanceSources[appearanceNum]] : std::string();
}

auto MCSpriteManager::LoadShape(MCPacketFile& file, uint32_t packetNum, uint32_t size, int32_t turnUsed,
                                std::weak_ptr<MCAppearanceType> owner, bool checkSize) -> std::shared_ptr<MCShape>
{
    // Port-only: a packet read on the game's thread as it's first needed (the frame log's), not with the load's.
    MCFrameLog::Count(MCPacketsFirstUsedCount);
    MCRegisteredBlock packet(size, MCDataKind::Shapes);
    const int32_t sizeRead = file.ReadPacket(static_cast<int32_t>(packetNum), packet.Bytes());

    if (checkSize)
    {
        Assert(static_cast<uint32_t>(sizeRead) == size, static_cast<uint32_t>(sizeRead), " Bad Packet in Shape file ");
    }

    return _Shapes.emplace_back(std::make_shared<MCShape>(std::move(packet), std::move(owner), turnUsed));
}

auto MCSpriteManager::AddShape(std::span<const uint8_t> bytes, int32_t turnUsed, std::weak_ptr<MCAppearanceType> owner)
    -> std::shared_ptr<MCShape>
{
    MCRegisteredBlock packet(bytes.size(), MCDataKind::Shapes);
    std::ranges::copy(bytes, packet.Bytes().begin());
    return _Shapes.emplace_back(std::make_shared<MCShape>(std::move(packet), std::move(owner), turnUsed));
}

auto MCSpriteManager::LoadTogether(
    std::span<const MCShapeWanted> wanted, int32_t turnUsed,
    const std::function<MCOptionalRef<MCPacketFile>(const MCShapeWanted&)>& fileOf,
    const std::function<std::shared_ptr<MCShape>(const MCShapeWanted&)>& alone,
    const std::function<void(const std::shared_ptr<MCShape>&, const MCShapeWanted&)>& tie,
    std::span<const MCStoredWanted> stored) -> MCShapesLoaded
{
    // Each file once with the packets and runs as stored wanted of it, and where each wanted's and run's result is (its
    // file, its place there).
    std::vector<std::reference_wrapper<MCPacketFile>> files;
    std::vector<std::vector<int32_t>> packets;
    std::vector<std::vector<MCStoredRun>> runs;
    std::vector<std::optional<std::pair<size_t, size_t>>> at(wanted.size());
    std::vector<std::optional<std::pair<size_t, size_t>>> storedAt(stored.size());
    const auto indexOf = [&](const MCShapeWanted& of) -> std::optional<size_t>
    {
        const MCOptionalRef<MCPacketFile> file = fileOf(of);

        if (!file.has_value())
        {
            return std::nullopt;
        }

        const auto found =
            std::ranges::find_if(files, [&](const MCPacketFile& known) { return &known == &file->get(); });

        if (found == files.end())
        {
            files.push_back(*file);
            packets.emplace_back();
            runs.emplace_back();
            return files.size() - 1;
        }

        return static_cast<size_t>(found - files.begin());
    };

    for (size_t i = 0; i < wanted.size(); ++i)
    {
        if (const std::optional<size_t> index = indexOf(wanted[i]))
        {
            at[i] = std::pair(*index, packets[*index].size());
            packets[*index].push_back(static_cast<int32_t>(wanted[i].Packet));
        }
    }

    for (size_t i = 0; i < stored.size(); ++i)
    {
        if (const std::optional<size_t> index = indexOf(stored[i].Of))
        {
            storedAt[i] = std::pair(*index, runs[*index].size());
            runs[*index].push_back(
                MCStoredRun{static_cast<int32_t>(stored[i].Of.Packet), static_cast<int32_t>(stored[i].Count)});
        }
    }

    std::vector<MCPacketsWanted> asks;

    for (size_t index = 0; index < files.size(); ++index)
    {
        asks.push_back(MCPacketsWanted{files[index].get(), packets[index], runs[index]});
    }

    std::vector<MCPacketBatch> batches = MCPacketFile::ReadPackets(asks);
    MCShapesLoaded loaded;
    loaded.Shapes.resize(wanted.size());
    loaded.Stored.resize(stored.size());

    for (size_t i = 0; i < stored.size(); ++i)
    {
        if (storedAt[i].has_value())
        {
            loaded.Stored[i] = std::move(batches[storedAt[i]->first].Stored[storedAt[i]->second]);
        }
    }

    for (const MCPacketBatch& batch : batches)
    {
        loaded.Reads += batch.Reads;
    }

    for (size_t i = 0; i < wanted.size(); ++i)
    {
        const MCOptionalRef<const std::optional<std::vector<uint8_t>>> read =
            at[i].has_value()
                ? MCOptionalRef<const std::optional<std::vector<uint8_t>>>(batches[at[i]->first].Packets[at[i]->second])
                : std::nullopt;

        if (read.has_value() && read->get().has_value())
        {
            const std::vector<uint8_t>& bytes = *read->get();

            // An empty packet loads none, as one at a time.
            if (!bytes.empty())
            {
                loaded.Shapes[i] = AddShape(bytes, turnUsed, wanted[i].Owner);
                tie(loaded.Shapes[i], wanted[i]);
                loaded.Bytes += bytes.size();
            }

            continue;
        }

        loaded.Shapes[i] = alone(wanted[i]);
        ++loaded.Reads;
    }

    return loaded;
}

auto MCSpriteManager::GetMechShapesData(std::span<const MCShapeWanted> wanted, int32_t turnUsed, bool zoomedOut,
                                        std::span<const MCStoredWanted> stored) -> MCShapesLoaded
{
    return LoadTogether(
        wanted, turnUsed, [&](const MCShapeWanted& shape) { return MechFile(shape.FileNum, shape.Part, zoomedOut); },
        [&](const MCShapeWanted& shape)
        { return GetMechShapeData(shape.FileNum, shape.Packet, shape.Part, turnUsed, shape.Owner, zoomedOut); },
        [&](const std::shared_ptr<MCShape>& shape, const MCShapeWanted& of)
        { TieMechShape(shape, of.FileNum, of.Packet, of.Part, zoomedOut); }, stored);
}

auto MCSpriteManager::GetShapesData(std::span<const MCShapeWanted> wanted, int32_t turnUsed) -> MCShapesLoaded
{
    return LoadTogether(
        wanted, turnUsed,
        [&](const MCShapeWanted& shape) -> MCOptionalRef<MCPacketFile>
        {
            return OpenAppearanceFile(shape.FileNum) ? MCOptionalRef<MCPacketFile>(*_AppearanceFiles[shape.FileNum])
                                                     : std::nullopt;
        },
        [&](const MCShapeWanted& shape) { return GetShapeData(shape.FileNum, shape.Packet, turnUsed, shape.Owner); },
        [&](const std::shared_ptr<MCShape>& shape, const MCShapeWanted& of)
        { TieShape(shape, of.FileNum, of.Packet); });
}

auto MCSpriteManager::Dump(const std::function<bool(const MCShape&)>& dump) -> void
{
    std::erase_if(_Shapes, [&dump](const std::shared_ptr<MCShape>& shape) { return dump(*shape); });
}

auto MCSpriteManager::DumpLru() -> void
{
    Dump([](const MCShape& shape) { return shape.Owner.expired() || Turn > shape.LastTurnUsed + 1; });
}

auto MCSpriteManager::DumpAll() -> void
{
    Dump([](const MCShape&) { return true; });
}

auto MCSpriteManager::GetShapeData(uint32_t appearanceNum, uint32_t packetNum, int32_t turnUsed,
                                   std::weak_ptr<MCAppearanceType> owner) -> std::shared_ptr<MCShape>
{
    if (!OpenAppearanceFile(appearanceNum))
    {
        return nullptr;
    }

    MCPacketFile& file = *_AppearanceFiles[appearanceNum];

    if (file.SeekPacket(static_cast<int32_t>(packetNum)) != 0)
    {
        return nullptr;
    }

    const auto size = static_cast<uint32_t>(file.GetPacketSize());

    if (size == 0)
    {
        return nullptr;
    }

    std::shared_ptr<MCShape> shape = LoadShape(file, packetNum, size, turnUsed, std::move(owner), true);
    ++_ReadAlone.Others;
    TieShape(shape, appearanceNum, packetNum);
    return shape;
}

auto MCSpriteManager::TieShape(const std::shared_ptr<MCShape>& shape, uint32_t appearanceNum, uint32_t packetNum) const
    -> void
{
    // Port-only: the HD art pack's frames for it, if it has them; tied now only while HD art is on, and by its HdFolder
    // when HD art comes on later (AttachHdArt).
    const MCHdArt::ObjectShapeKey key{_SpriteFileNames[_AppearanceSources[appearanceNum]], appearanceNum, packetNum};

    if (shape != nullptr)
    {
        shape->HdFolder = MCHdArt::FolderOf(key);
    }

    if (MCHdArt::Active())
    {
        MCHdArt::Attach(shape, key);
    }
}

auto MCSpriteManager::MechFile(uint32_t mechNum, MCMechPart part, bool zoomedOut) -> MCOptionalRef<MCPacketFile>
{
    const bool use90 = zoomedOut && _PartFiles90[part] != nullptr;
    MCPacketFile& parent = use90 ? *_PartFiles90[part] : *_PartFiles[part];
    std::vector<std::unique_ptr<MCPacketFile>>& table = use90 ? _MechFiles90[part] : _MechFiles[part];

    if (mechNum >= table.size() || table[mechNum] == nullptr)
    {
        // The mech's PAK, a packet of the part PAK, opened when first asked for.
        if (parent.SeekPacket(static_cast<int32_t>(mechNum)) != 0)
        {
            return std::nullopt;
        }

        auto opened = std::make_unique<MCPacketFile>();

        if (opened->Open(parent, static_cast<uint32_t>(parent.GetPacketSize())) != 0)
        {
            return std::nullopt;
        }

        if (table.size() <= mechNum)
        {
            table.resize(mechNum + 1);
        }

        table[mechNum] = std::move(opened);
    }

    return *table[mechNum];
}

auto MCSpriteManager::MechPacketsStored(uint32_t mechNum, MCMechPart part, uint32_t first, uint32_t count,
                                        bool zoomedOut) -> std::vector<uint8_t>
{
    const MCOptionalRef<MCPacketFile> file = MechFile(mechNum, part, zoomedOut);
    ++_ReadAlone.MechsStored;
    return file ? file->get().ReadStoredPackets(static_cast<int32_t>(first), static_cast<int32_t>(count))
                : std::vector<uint8_t>{};
}

auto MCSpriteManager::GetMechShapeData(uint32_t mechNum, uint32_t packetNum, MCMechPart part, int32_t turnUsed,
                                       std::weak_ptr<MCAppearanceType> owner, bool zoomedOut)
    -> std::shared_ptr<MCShape>
{
    const MCOptionalRef<MCPacketFile> opened = MechFile(mechNum, part, zoomedOut);

    if (!opened)
    {
        return nullptr;
    }

    MCPacketFile& file = opened->get();

    if (file.SeekPacket(static_cast<int32_t>(packetNum)) != 0)
    {
        return nullptr;
    }

    const auto size = static_cast<uint32_t>(file.GetPacketSize());

    if (size == 0)
    {
        return nullptr;
    }

    std::shared_ptr<MCShape> shape = LoadShape(file, packetNum, size, turnUsed, std::move(owner), false);
    ++_ReadAlone.Mechs;
    TieMechShape(shape, mechNum, packetNum, part, zoomedOut);
    return shape;
}

auto MCSpriteManager::TieMechShape(const std::shared_ptr<MCShape>& shape, uint32_t mechNum, uint32_t packetNum,
                                   MCMechPart part, bool zoomedOut) const -> void
{
    // Port-only: the HD art pack's frames for it, if it has them.
    const MCHdArt::MechShapeKey key{PartFileName(part, zoomedOut), mechNum, packetNum};

    if (shape != nullptr)
    {
        shape->HdFolder = MCHdArt::FolderOf(key);
    }

    MCHdArt::Attach(shape, key);
}

auto MCSpriteManager::AttachHdArt() -> void
{
    for (const std::shared_ptr<MCShape>& shape : _Shapes)
    {
        MCHdArt::AttachFolder(shape);
    }
}

auto MCSpriteManager::PartFileName(MCMechPart part, bool zoomedOut) const -> std::string
{
    const bool use90 = zoomedOut && _PartFiles90[part] != nullptr;
    return std::format("{}{}", PartFileNames[static_cast<size_t>(part)], use90 ? "90" : "");
}

auto MCSpriteManager::GetNumShapes(uint32_t appearanceNum) -> int32_t
{
    return OpenAppearanceFile(appearanceNum) ? _AppearanceFiles[appearanceNum]->GetNumPackets() : 0;
}

auto MCSpriteManager::GetNumAppearances() const -> uint32_t
{
    return static_cast<uint32_t>(
        std::max(_SpriteFiles[PreferredSprites]->GetNumPackets(), _SpriteFiles[FallbackSprites]->GetNumPackets()));
}

auto MCSpriteManager::MechShadow(size_t index) const -> std::span<const uint8_t>
{
    return index < _MechShadows.size() ? _MechShadows[index].Bytes() : std::span<const uint8_t>();
}

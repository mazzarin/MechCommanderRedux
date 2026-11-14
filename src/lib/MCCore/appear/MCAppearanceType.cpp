#include "stdafx.h"
#include "appear/MCAppearanceType.h"
#include "sprite/MCShape.h"
#include "main/MCMissionGlobals.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCSpriteManager.h"

MCAppearanceFit::MCAppearanceFit(MCFile& packet, uint32_t size)
{
    if (const int32_t result = _File.Open(packet, size); result != 0)
    {
        _Error = std::format("not a FIT file ({:#x})", static_cast<uint32_t>(result));
    }
}

auto MCAppearanceFit::SeekBlock(std::string_view block) -> bool
{
    if (!_Error.empty())
    {
        return false;
    }

    if (_File.SeekBlock(block) != 0)
    {
        _Error = std::format("no block [{}]", block);
        return false;
    }

    return true;
}

auto MCAppearanceFit::HasBlock(std::string_view block) -> bool
{
    return _Error.empty() && _File.SeekBlock(block) == 0;
}

auto MCAppearanceFit::Result() const -> MCAppearanceLoad
{
    if (_Error.empty())
    {
        return {};
    }

    return std::unexpected(_Error);
}

auto MCAppearanceFit::Fail(std::string_view what, MCFitError error) -> void
{
    _Error = std::format("could not read {} ({:#x})", what, static_cast<uint32_t>(std::to_underlying(error)));
}

auto MCAppearanceType::LoadBounds(MCFile& apprFile, uint32_t fileSize) -> MCAppearanceLoad
{
    MCAppearanceFit fit(apprFile, fileSize);

    if (fit.HasBlock("Bounds"))
    {
        BoundsUpperLeftX = fit.Read<int32_t>("UpperLeftX");
        BoundsUpperLeftY = fit.Read<int32_t>("UpperLeftY");
        BoundsLowerRightX = fit.Read<int32_t>("LowerRightX");
        BoundsLowerRightY = fit.Read<int32_t>("LowerRightY");
    }

    return fit.Result();
}

auto MCAppearanceType::MakeShapeList(std::vector<std::weak_ptr<MCShape>>& shapes) const -> MCAppearanceLoad
{
    const int32_t numShapes = SpriteManager()->GetNumShapes(ShapeFileNum());

    if (numShapes <= 0)
    {
        return std::unexpected(std::format("appearance {:#x} has no shapes", AppearanceNum));
    }

    shapes.assign(static_cast<size_t>(numShapes), {});

    // Port-only (SPEC-first-use.md: a load): the HD archive holding the type's frames opened by HD art's readers now,
    // its directory not read by the game's thread at the type's first draw or ask.
    if (MCHdArt::NumberedFolders())
    {
        if (const std::string shapeFile = SpriteManager()->ShapeFileName(ShapeFileNum()); !shapeFile.empty())
        {
            const std::array<std::filesystem::path, 1> folder{
                MCHdArt::FolderOf(MCHdArt::ObjectShapeKey{shapeFile, ShapeFileNum(), 0})};
            MCHdArt::OpenArchivesAhead(folder);
        }
    }

    return {};
}

auto MCAppearanceType::PacketShape(std::weak_ptr<MCShape>& slot, uint32_t packet) -> std::shared_ptr<MCShape>
{
    // Port-only (SPEC-first-use.md): asked ahead, the packet's HD frames read, not the shape loaded.
    if (MCAskingAhead::Asking)
    {
        AskPacketAhead(packet);
        return slot.lock();
    }

    if (std::shared_ptr<MCShape> shape = slot.lock())
    {
        shape->LastTurnUsed = Turn;
        return shape;
    }

    std::shared_ptr<MCShape> shape = SpriteManager()->GetShapeData(ShapeFileNum(), packet, Turn, weak_from_this());
    slot = shape;
    return shape;
}

auto MCAppearanceType::AskPacketAhead(uint32_t packet) -> void
{
    if (!_AskedAhead.Ask(packet))
    {
        return;
    }

    const std::string shapeFile = SpriteManager()->ShapeFileName(ShapeFileNum());

    if (!shapeFile.empty())
    {
        MCHdArt::ReadPacketAhead(MCHdArt::FolderOf(MCHdArt::ObjectShapeKey{shapeFile, ShapeFileNum(), packet}));
    }
}

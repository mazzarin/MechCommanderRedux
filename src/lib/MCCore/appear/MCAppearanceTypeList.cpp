#include "stdafx.h"
#include "appear/MCAppearanceTypeList.h"
#include "appear/MCAppearanceType.h"
#include "lib/MCHeldFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "sprite/MCArmAppearanceType.h"
#include "sprite/MCElementalTree.h"
#include "sprite/MCGVAppearanceType.h"
#include "sprite/MCPUAppearanceType.h"
#include "sprite/MCSpriteTree.h"
#include "sprite/MCVfxAppearanceType.h"
#include "sprite/MCVfxBuildingAppearanceType.h"

namespace
{
    /// <summary>A new, empty type of class <paramref name="appearanceClass"/>; null for a class the game hasn't.</summary>
    auto MakeType(MCAppearanceClass appearanceClass) -> std::shared_ptr<MCAppearanceType>
    {
        switch (appearanceClass)
        {
            case MCAppearanceClass::SpriteTree:
                return std::make_shared<MCSpriteTree>();
            case MCAppearanceClass::Vfx:
                return std::make_shared<MCVfxAppearanceType>();
            case MCAppearanceClass::Line:
                // The original's LineAppearanceType::init hid the virtual one, so a line type loads only its bounds.
                return std::make_shared<MCAppearanceType>();
            case MCAppearanceClass::GroundVehicle:
                return std::make_shared<MCGVAppearanceType>();
            case MCAppearanceClass::Arm:
                return std::make_shared<MCArmAppearanceType>();
            case MCAppearanceClass::Building:
                return std::make_shared<MCVfxBuildingAppearanceType>();
            case MCAppearanceClass::ElementalTree:
                return std::make_shared<MCElementalTree>();
            case MCAppearanceClass::PopUp:
                return std::make_shared<MCPUAppearanceType>();
            default:
                return nullptr;
        }
    }
}

MCAppearanceTypeList::MCAppearanceTypeList(std::unique_ptr<MCPacketFile> appearanceFile)
    : _AppearanceFile(std::move(appearanceFile))
{
}

MCAppearanceTypeList::~MCAppearanceTypeList() = default;

auto MCAppearanceTypeList::Create(std::string_view fileName)
    -> std::expected<std::unique_ptr<MCAppearanceTypeList>, std::string>
{
    auto file = std::make_unique<MCPacketFile>();
    const std::string spriteName = GamePath(SpritePath, fileName, ".pak");

    if (file->Open(spriteName) != 0 && file->Open(GamePath(CDspritePath, fileName, ".pak")) != 0)
    {
        return std::unexpected(std::format("could not open {}", spriteName));
    }

    // Port-only (SPEC-first-use.md): read whole as the mission's load begins, its appearances' fits from memory.
    file->HoldWhole(MCHeldFile::MostPacketFileBytes);
    return std::make_unique<MCAppearanceTypeList>(std::move(file));
}

auto AppearanceTypeList() -> MCAppearanceTypeList&
{
    return MCGameContext::Current().AppearanceTypeList();
}

auto MCAppearanceTypeList::GetAppearance(uint32_t appearanceId) -> std::shared_ptr<MCAppearanceType>
{
    const auto packetNum = static_cast<int32_t>(appearanceId & 0xffffff);
    const auto appearanceClass = static_cast<MCAppearanceClass>(appearanceId >> 24);

    if (std::to_underlying(appearanceClass) == 0)
    {
        return nullptr;
    }

    for (const std::shared_ptr<MCAppearanceType>& type : _Types)
    {
        if (type->AppearanceNum == appearanceId)
        {
            type->NumUsers++;
            return type;
        }
    }

    if (_AppearanceFile->SeekPacket(packetNum) != 0)
    {
        return nullptr;
    }

    const auto packetSize = static_cast<uint32_t>(_AppearanceFile->GetPacketSize());
    std::shared_ptr<MCAppearanceType> type = MakeType(appearanceClass);

    if (type == nullptr)
    {
        return nullptr;
    }

    type->AppearanceNum = appearanceId;

    if (!type->Load(*_AppearanceFile, packetSize).has_value())
    {
        return nullptr;
    }

    _AppearanceFile->SeekPacket(packetNum);

    if (!type->LoadBounds(*_AppearanceFile, packetSize).has_value())
    {
        return nullptr;
    }

    type->NumUsers = 1;
    return _Types.emplace_back(std::move(type));
}

auto MCAppearanceTypeList::RemoveAppearance(const MCAppearanceType& which) -> int32_t
{
    const auto found = std::ranges::find_if(_Types, [&which](const std::shared_ptr<MCAppearanceType>& type)
                                            { return type.get() == &which; });

    if (found == _Types.end())
    {
        return static_cast<int32_t>(0xadda0003);
    }

    if (--(*found)->NumUsers == 0)
    {
        _Types.erase(found);
    }

    return 0;
}

#include "stdafx.h"
#include "sprite/MCShape.h"
#include "vfx/MCVfxClip.h"

namespace
{
    /// <summary>The size of the gesture header in front of a mech gesture packet's shape table.</summary>
    constexpr size_t GestureHeaderSize = 6;
    /// <summary>Where a VFX shape table keeps its frame count.</summary>
    constexpr size_t CountAt = 4;
}

MCShape::MCShape(MCRegisteredBlock packet, std::weak_ptr<MCAppearanceType> owner, int32_t lastTurnUsed)
    : LastTurnUsed(lastTurnUsed), Owner(std::move(owner)), _Packet(std::move(packet))
{
    const std::span<const uint8_t> bytes = _Packet.Bytes();

    // A bare VFX shape table starts with its "1.10" version tag; anything else has a gesture header in front.
    const size_t start = std::memcmp(bytes.data(), "1.10", 4) != 0 ? GestureHeaderSize : 0;
    _FrameStart = start;
    const int32_t numFrames = VfxShapeCount(bytes.subspan(start));

    // OB-140 fixed: the original left a shape without frames ownerless (its type wasn't told when it went).
    if (numFrames == 0)
    {
        return;
    }

    int32_t firstFrame;
    std::memcpy(&firstFrame, bytes.subspan(start + 8, sizeof(firstFrame)).data(), sizeof(firstFrame));

    if (static_cast<int32_t>(_Packet.Size()) <= firstFrame || numFrames * 8 + 8 != firstFrame)
    {
        _FrameStart.reset();
        return;
    }

    // OB-190 (fixed): frames past the last one the table holds (MCVfxHasShape) aren't counted, so a gesture that plays
    // them shows that one, as it does a frame past the count (the Hollander's fall back from standing lists a 22nd
    // frame for its 21). A table holding none counts none, and its part borrows another's art
    // (MCSpriteTree::GetGesture).
    const std::span<uint8_t> table = _Packet.Bytes().subspan(start);
    int32_t held = numFrames;

    while (held > 0 && !MCVfxHasShape(table, held - 1))
    {
        --held;
    }

    if (held != numFrames)
    {
        std::memcpy(table.subspan(CountAt, sizeof(held)).data(), &held, sizeof(held));
    }
}

auto MCShape::HasArt() const -> bool
{
    return VfxShapeCount(FrameList()) > 0;
}

auto MCShape::FrameList() const -> std::span<const uint8_t>
{
    if (!_FrameStart.has_value())
    {
        return {};
    }

    return std::span<const uint8_t>(_Packet.Bytes()).subspan(*_FrameStart);
}

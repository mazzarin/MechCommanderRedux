#pragma once

#include "platform/MCRegisteredBlock.h"

class MCAppearanceType;

/// <summary>
/// A shape table in the sprite manager's cache: one packet of a sprite PAK, owned by the cache and handed to the
/// appearance type that asked for it. The type and its appearances refer to it by <c>std::weak_ptr</c>, which expires
/// when the cache drops the shape.
/// </summary>
/// <remarks>
/// Original source: <c>sprite\vfxshape.cpp</c>. A packet is either a VFX shape table (it starts with the "1.10"
/// version tag) or a 6-byte gesture header followed by one.
/// </remarks>
class MCShape
{
public:
    /// <summary>
    /// Takes the packet <paramref name="packet"/> for <paramref name="owner"/>, checking its shape table: a table whose
    /// first frame lies past the packet, or whose offset table doesn't match its frame count, gives an empty
    /// <see cref="FrameList"/>; the frames listed past the last one the table holds aren't counted.
    /// </summary>
    MCShape(MCRegisteredBlock packet, std::weak_ptr<MCAppearanceType> owner, int32_t lastTurnUsed);

    MCShape(const MCShape&) = delete;
    MCShape& operator=(const MCShape&) = delete;

    /// <summary>
    /// The VFX shape table (in the packet, after the gesture header if any) to the packet's end, or empty when it is
    /// bad.
    /// </summary>
    std::span<const uint8_t> FrameList() const;

    /// <summary>Whether the table holds a frame to draw (a table listing only frames it doesn't hold has none).</summary>
    bool HasArt() const;

    /// <summary>The turn the shape was last used (the cache keeps shapes used this turn or the last).</summary>
    int32_t LastTurnUsed = 0;
    /// <summary>The type that uses the shape; expired once the type is gone (the cache then drops the shape).</summary>
    std::weak_ptr<MCAppearanceType> Owner;
    /// <summary>
    /// Port-only: the HD art pack's folder of the shape's frames (sprite/MCHdArt.h's FolderOf its key), kept so a shape
    /// loaded while HD art was off can be attached when it goes on (MCSpriteManager::AttachHdArt).
    /// </summary>
    std::filesystem::path HdFolder;

private:
    /// <summary>The packet.</summary>
    MCRegisteredBlock _Packet;
    /// <summary>Where the shape table starts in the packet; none when it is bad.</summary>
    std::optional<size_t> _FrameStart;
};

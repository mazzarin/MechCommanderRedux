#pragma once

#include "lib/MCFastFile.h"

/// <summary>
/// The FastFiles the game has open, searched in their file names' order (MCFileSystem::NameLess: ART.FST before
/// MISC.FST), whatever order they were opened in. <c>MCFile::Open</c> reads a file from the first one that holds it
/// when the disk doesn't. The game's set lives in <c>MCGameContext</c>.
/// </summary>
/// <remarks>
/// Original source: <c>lib\fastfile.cpp</c> (<c>FastFileInit</c>, <c>FastFileFind</c>, <c>FastFileFini</c>), a table
/// sized from SYSTEM.CFG's NumFastFiles that refused archives past its size, searched in the order they were opened.
/// The port opens every <c>*.fst</c> in the game folder and keeps them in name order, which folder listings don't give
/// on every system (APFS's and NTFS's orders differ).
/// </remarks>
class MCFastFileSet
{
public:
    /// <summary>
    /// Opens the archive at <paramref name="fileName"/> and adds it to the set, in its file name's place (after any
    /// open archive of an equal name).
    /// </summary>
    /// <returns>Nothing, or why it couldn't be opened (the set is then unchanged).</returns>
    std::expected<void, std::string> Open(std::string_view fileName);

    /// <summary>Closes every archive.</summary>
    void Clear()
    {
        _Files.clear();
        _Names.clear();
    }

    /// <summary>
    /// The data of <paramref name="gamePath"/> from the first archive that holds it, unpacked (a packed entry that
    /// doesn't unpack fully keeps the bytes it did unpack, the rest zero).
    /// </summary>
    std::optional<std::vector<uint8_t>> Read(std::string_view gamePath);

    /// <summary>The open archives, in search order.</summary>
    const std::vector<std::unique_ptr<MCFastFile>>& Files() const { return _Files; }

private:
    /// <summary>The open archives, in search order.</summary>
    std::vector<std::unique_ptr<MCFastFile>> _Files;
    /// <summary>Their file names (without folders), in the same order.</summary>
    std::vector<std::string> _Names;
};

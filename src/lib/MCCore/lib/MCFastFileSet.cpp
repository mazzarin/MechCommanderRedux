#include "stdafx.h"
#include "lib/MCFastFileSet.h"
#include "platform/MCFileSystem.h"

std::expected<void, std::string> MCFastFileSet::Open(std::string_view fileName)
{
    auto fastFile = MCFastFile::Create(fileName);

    if (!fastFile.has_value())
    {
        return std::unexpected(std::move(fastFile.error()));
    }

    // Port: in name order, so which archive's copy of a name is read doesn't hang on the folder listing.
    std::string name(fileName.substr(fileName.find_last_of("\\/") + 1));
    const auto at = std::ranges::upper_bound(_Names, name, MCFileSystem::NameLess);
    _Files.insert(_Files.begin() + (at - _Names.begin()), std::move(*fastFile));
    _Names.insert(at, std::move(name));
    return {};
}

std::optional<std::vector<uint8_t>> MCFastFileSet::Read(std::string_view gamePath)
{
    for (const std::unique_ptr<MCFastFile>& fastFile : _Files)
    {
        if (const std::optional<int32_t> index = fastFile->Find(gamePath); index.has_value())
        {
            std::vector<uint8_t> data(
                static_cast<size_t>(std::max(fastFile->GetEntries()[static_cast<size_t>(*index)].RealSize, 0)));
            fastFile->ReadEntry(*index, data);
            return data;
        }
    }

    return std::nullopt;
}

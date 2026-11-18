#include "stdafx.h"
#include "lib/MCFastFileSet.h"
#include "platform/MCFileSystem.h"

std::expected<void, std::string> MCFastFileSet::Open(std::string_view fileName)
{
    return Add(MCFastFile::Create(fileName), fileName.substr(fileName.find_last_of("\\/") + 1));
}

std::expected<void, std::string> MCFastFileSet::OpenAt(const std::filesystem::path& path)
{
    const std::string name = MCFileSystem::ToUtf8(path.filename());
    return Add(MCFastFile::CreateAt(path, name), name);
}

std::expected<void, std::string> MCFastFileSet::Add(std::expected<std::unique_ptr<MCFastFile>, std::string> fastFile,
                                                    std::string_view name)
{
    if (!fastFile.has_value())
    {
        return std::unexpected(std::move(fastFile.error()));
    }

    // Port: in name order, so which archive's copy of a name is read doesn't hang on the folder listing.
    const auto at = std::ranges::upper_bound(_Names, name, MCFileSystem::NameLess);
    _Files.insert(_Files.begin() + (at - _Names.begin()), std::move(*fastFile));
    _Names.emplace(at, name);
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

std::optional<uint32_t> MCFastFileSet::SizeOf(std::string_view gamePath) const
{
    for (const std::unique_ptr<MCFastFile>& fastFile : _Files)
    {
        if (const std::optional<int32_t> index = fastFile->Find(gamePath); index.has_value())
        {
            return static_cast<uint32_t>(std::max(fastFile->GetEntries()[static_cast<size_t>(*index)].RealSize, 0));
        }
    }

    return std::nullopt;
}

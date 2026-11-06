#include "stdafx.h"
#include "update/MCContentFolder.h"
#include "platform/MCFileSystem.h"
#include "update/MCContentList.h"

namespace
{
    constexpr char Separator = '/';

    /// <summary>The parts of a '/'-separated path.</summary>
    std::vector<std::string_view> Parts(std::string_view path)
    {
        std::vector<std::string_view> parts;

        for (size_t start = 0; start <= path.size();)
        {
            const size_t end = std::min(path.find(Separator, start), path.size());
            parts.push_back(path.substr(start, end - start));
            start = end + 1;
        }

        return parts;
    }

    std::string Joined(const std::string& folder, std::string_view name)
    {
        return folder.empty() ? std::string(name) : folder + Separator + std::string(name);
    }

    /// <summary>Whether <paramref name="error"/> says what was looked at isn't there (or went since it was listed).</summary>
    bool Gone(const std::error_code& error)
    {
        return error == std::errc::no_such_file_or_directory;
    }
}

MCContentFolder::MCContentFolder(std::filesystem::path root) : _Root(std::move(root))
{
}

std::filesystem::path MCContentFolder::Full(std::string_view relative) const
{
    return _Root / MCFileSystem::FromUtf8(relative);
}

auto MCContentFolder::Listing(const std::string& folder)
    -> std::expected<std::reference_wrapper<const MCListing>, std::string>
{
    if (const auto found = _Listings.find(folder); found != _Listings.end())
    {
        return std::cref(found->second);
    }

    const std::filesystem::path path = Full(folder);
    MCListing listing;
    std::error_code error;
    std::filesystem::directory_iterator it(path, error);

    // A folder that isn't there holds nothing: what's listed in it is missing.
    if (Gone(error))
    {
        error.clear();
    }

    for (const std::filesystem::directory_iterator end; !error && it != end; it.increment(error))
    {
        const std::filesystem::file_status status = it->symlink_status(error);

        if (Gone(error))
        {
            error.clear();
            continue;
        }

        if (error)
        {
            break;
        }

        MCEntry entry;
        entry.Name = MCFileSystem::ToUtf8(it->path().filename());
        entry.Folder = std::filesystem::is_directory(status);
        entry.Kind = std::filesystem::is_regular_file(status) || entry.Folder ? MCKind::File : MCKind::Link;
        listing.emplace(MCContentList::FoldedPath(entry.Name), std::move(entry));
    }

    if (error)
    {
        return std::unexpected(std::format("{} can't be listed ({})", MCFileSystem::ToUtf8(path), error.message()));
    }

    return std::cref(_Listings.emplace(folder, std::move(listing)).first->second);
}

std::expected<MCContentFolder::MCFound, std::string> MCContentFolder::Find(std::string_view path)
{
    const std::vector<std::string_view> parts = Parts(path);
    std::string onDisk;

    for (size_t i = 0; i < parts.size(); ++i)
    {
        const auto listing = Listing(onDisk);

        if (!listing)
        {
            return std::unexpected(listing.error());
        }

        const auto found = listing->get().find(MCContentList::FoldedPath(parts[i]));

        if (found == listing->get().end())
        {
            return MCFound{MCKind::Missing, {}};
        }

        const MCEntry& entry = found->second;

        if (entry.Kind == MCKind::Link)
        {
            return MCFound{MCKind::Link, Joined(onDisk, entry.Name)};
        }

        const bool last = i + 1 == parts.size();

        if (entry.Folder == last)
        {
            return MCFound{MCKind::Blocked, Joined(onDisk, entry.Name)};
        }

        onDisk = Joined(onDisk, entry.Name);
    }

    return MCFound{MCKind::File, onDisk};
}

std::expected<std::string, std::string> MCContentFolder::Place(std::string_view path)
{
    const std::vector<std::string_view> parts = Parts(path);
    std::string placed;
    bool onDisk = true;

    for (size_t i = 0; i < parts.size(); ++i)
    {
        std::string_view name = parts[i];

        if (onDisk && i + 1 < parts.size())
        {
            const auto listing = Listing(placed);

            if (!listing)
            {
                return std::unexpected(listing.error());
            }

            if (const auto found = listing->get().find(MCContentList::FoldedPath(name));
                found != listing->get().end() && found->second.Folder)
            {
                name = found->second.Name;
            }
            else
            {
                onDisk = false;
            }
        }

        placed = Joined(placed, name);
    }

    return placed;
}

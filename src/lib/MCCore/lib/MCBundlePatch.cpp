#include "stdafx.h"
#include "lib/MCBundlePatch.h"
#include "lib/MCDiskFile.h"
#include "lib/MCFastFile.h"
#include "platform/MCFileSystem.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

namespace
{
    /// <summary>The furthest a FastFile's offsets reach (they are signed 32-bit).</summary>
    constexpr uint64_t MaxOffset = static_cast<uint64_t>(std::numeric_limits<int32_t>::max());

    /// <summary>An archive read from, and its path (for messages).</summary>
    struct Source
    {
        std::filesystem::path Path;
        std::unique_ptr<MCFastFile> File;
    };

    /// <summary>An entry to write: its name, and its source's index and entry there (none: a deletion).</summary>
    struct Pick
    {
        std::string Name;
        std::optional<std::pair<size_t, int32_t>> From;
    };

    /// <summary>What an entry's stored bytes are known by when shared: their XXH3 (128 bits) and the entry's sizes.</summary>
    using SharedKey = std::tuple<uint64_t, uint64_t, int32_t, int32_t>;

    std::string Shown(const std::filesystem::path& path)
    {
        return MCFileSystem::ToUtf8(path);
    }

    /// <summary>A name as the game finds it: its ASCII letters upper-cased (MCFastFile::Find).</summary>
    std::string Key(std::string_view name)
    {
        std::string key(name);

        for (char& c : key)
        {
            if (c >= 'a' && c <= 'z')
            {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }

        return key;
    }

    /// <summary>Whether an entry marks its name deleted: it holds nothing.</summary>
    bool Deletes(const MCFileEntry& entry)
    {
        return entry.RealSize == 0;
    }

    /// <summary>The archive at <paramref name="path"/>, opened, or why it can't be.</summary>
    std::expected<Source, std::string> Open(const std::filesystem::path& path)
    {
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(path, error);

        if (error)
        {
            return std::unexpected(std::format("{}: {}", Shown(path), error.message()));
        }

        auto file = MCFastFile::Create(MCFileSystem::ToUtf8(absolute));

        if (!file)
        {
            return std::unexpected(file.error());
        }

        return Source{absolute, std::move(*file)};
    }

    /// <summary>Each name's first entry in <paramref name="archive"/> (by Key), as the game finds it.</summary>
    std::unordered_map<std::string, int32_t> FirstEntries(const MCFastFile& archive)
    {
        const std::span<const MCFileEntry> entries = archive.GetEntries();
        std::unordered_map<std::string, int32_t> first;
        first.reserve(entries.size());

        for (size_t i = 0; i < entries.size(); ++i)
        {
            first.try_emplace(Key(entries[i].GetName()), static_cast<int32_t>(i));
        }

        return first;
    }

    /// <summary>Entry <paramref name="index"/>'s bytes as <paramref name="source"/> stores them, or why they can't be read.</summary>
    std::expected<std::vector<uint8_t>, std::string> Stored(Source& source, int32_t index)
    {
        const MCFileEntry& entry = source.File->GetEntries()[static_cast<size_t>(index)];
        std::vector<uint8_t> stored = source.File->ReadStored(index);

        if (stored.size() != static_cast<size_t>(entry.Size))
        {
            return std::unexpected(std::format("{}: {} can't be read", Shown(source.Path), entry.GetName()));
        }

        return stored;
    }

    /// <summary>Writes <paramref name="picks"/> to <paramref name="out"/> in MCBundlePatch::Apply's layout.</summary>
    std::expected<void, std::string> WriteBundle(std::vector<Source>& sources, std::vector<Pick>& picks,
                                                 const std::filesystem::path& out)
    {
        std::ranges::sort(picks, MCFileSystem::NameLess, &Pick::Name);
        const uint64_t directoryBytes = sizeof(int32_t) + picks.size() * sizeof(MCFileEntry);

        if (directoryBytes > MaxOffset)
        {
            return std::unexpected(std::format("{}: {} entries are too many", Shown(out), picks.size()));
        }

        std::optional<MCDiskFile> file = MCDiskFile::Open(out, MCDiskFileMode::Create);

        if (!file.has_value())
        {
            return std::unexpected(std::format("{} can't be written", Shown(out)));
        }

        // Value-initialized: every byte of a record not set below, a deletion's all of them, is zero.
        std::vector<MCFileEntry> directory(picks.size());
        std::map<SharedKey, int32_t> written;
        uint64_t at = directoryBytes;

        for (size_t i = 0; i < picks.size(); ++i)
        {
            const Pick& pick = picks[i];
            MCFileEntry& record = directory[i];
            // A source's name, so shorter than the record's (MCFastFile::Create ends each with a zero).
            std::ranges::copy(pick.Name, record.Name.begin());

            if (!pick.From.has_value())
            {
                continue;
            }

            Source& source = sources[pick.From->first];
            const MCFileEntry& entry = source.File->GetEntries()[static_cast<size_t>(pick.From->second)];
            auto stored = Stored(source, pick.From->second);

            if (!stored)
            {
                return std::unexpected(stored.error());
            }

            const XXH128_hash_t hash = XXH3_128bits(stored->data(), stored->size());
            const auto [shared, fresh] = written.try_emplace(
                SharedKey{hash.low64, hash.high64, entry.Size, entry.RealSize}, static_cast<int32_t>(at));

            if (fresh)
            {
                if (at + stored->size() > MaxOffset)
                {
                    return std::unexpected(std::format("{}: past 2 GB at {}", Shown(out), pick.Name));
                }

                if (file->WriteAt(static_cast<uint32_t>(at), *stored) != stored->size())
                {
                    return std::unexpected(std::format("{}: {} can't be written", Shown(out), pick.Name));
                }

                at += stored->size();
            }

            record.Offset = shared->second;
            record.Size = entry.Size;
            record.RealSize = entry.RealSize;
        }

        std::vector<uint8_t> header(static_cast<size_t>(directoryBytes));
        const auto count = static_cast<int32_t>(picks.size());
        std::memcpy(header.data(), &count, sizeof(count));
        std::memcpy(header.data() + sizeof(count), directory.data(), directory.size() * sizeof(MCFileEntry));

        if (file->WriteAt(0, header) != header.size() || !file->Flush())
        {
            return std::unexpected(std::format("{}: its directory can't be written", Shown(out)));
        }

        return {};
    }

    /// <summary>
    /// WriteBundle, after checking <paramref name="out"/> isn't one of <paramref name="sources"/>; on a failure, removes
    /// what it wrote.
    /// </summary>
    std::expected<void, std::string> Write(std::vector<Source>& sources, std::vector<Pick>& picks,
                                           const std::filesystem::path& out)
    {
        for (const Source& source : sources)
        {
            std::error_code error;

            if (std::filesystem::equivalent(out, source.Path, error))
            {
                return std::unexpected(std::format("{} would be written over while it's read", Shown(out)));
            }
        }

        auto written = WriteBundle(sources, picks, out);

        if (!written)
        {
            std::error_code error;
            std::filesystem::remove(out, error);
        }

        return written;
    }
}

std::expected<void, std::string> MCBundlePatch::Apply(const std::filesystem::path& base,
                                                      std::span<const std::filesystem::path> patches,
                                                      const std::filesystem::path& out)
{
    std::vector<Source> sources;
    std::unordered_map<std::string, Pick> chosen;

    for (size_t s = 0; s <= patches.size(); ++s)
    {
        auto source = Open(s == 0 ? base : patches[s - 1]);

        if (!source)
        {
            return std::unexpected(source.error());
        }

        sources.push_back(std::move(*source));
        const MCFastFile& archive = *sources.back().File;

        for (const auto& [key, index] : FirstEntries(archive))
        {
            const MCFileEntry& entry = archive.GetEntries()[static_cast<size_t>(index)];

            if (Deletes(entry))
            {
                chosen.erase(key);
            }
            else
            {
                chosen.insert_or_assign(key, Pick{std::string(entry.GetName()), std::pair{s, index}});
            }
        }
    }

    std::vector<Pick> picks;
    picks.reserve(chosen.size());

    for (auto& [key, pick] : chosen)
    {
        picks.push_back(std::move(pick));
    }

    return Write(sources, picks, out);
}

std::expected<void, std::string> MCBundlePatch::Make(const std::filesystem::path& from, const std::filesystem::path& to,
                                                     const std::filesystem::path& out)
{
    constexpr size_t FromSource = 0;
    constexpr size_t ToSource = 1;
    std::vector<Source> sources;

    for (const std::filesystem::path& path : {from, to})
    {
        auto source = Open(path);

        if (!source)
        {
            return std::unexpected(source.error());
        }

        sources.push_back(std::move(*source));
    }

    const std::unordered_map<std::string, int32_t> fromFirst = FirstEntries(*sources[FromSource].File);
    const std::unordered_map<std::string, int32_t> toFirst = FirstEntries(*sources[ToSource].File);
    const std::span<const MCFileEntry> fromEntries = sources[FromSource].File->GetEntries();
    const std::span<const MCFileEntry> toEntries = sources[ToSource].File->GetEntries();
    std::vector<Pick> picks;

    for (const auto& [key, toIndex] : toFirst)
    {
        const MCFileEntry& toEntry = toEntries[static_cast<size_t>(toIndex)];

        if (Deletes(toEntry))
        {
            continue;
        }

        if (const auto found = fromFirst.find(key); found != fromFirst.end())
        {
            const MCFileEntry& fromEntry = fromEntries[static_cast<size_t>(found->second)];

            // The same spelling too: the bundle made keeps the spelling of an entry it takes from the base.
            if (!Deletes(fromEntry) && fromEntry.GetName() == toEntry.GetName() && fromEntry.Size == toEntry.Size &&
                fromEntry.RealSize == toEntry.RealSize)
            {
                auto fromBytes = Stored(sources[FromSource], found->second);
                auto toBytes = Stored(sources[ToSource], toIndex);

                if (!fromBytes)
                {
                    return std::unexpected(fromBytes.error());
                }

                if (!toBytes)
                {
                    return std::unexpected(toBytes.error());
                }

                if (*fromBytes == *toBytes)
                {
                    continue;
                }
            }
        }

        picks.push_back(Pick{std::string(toEntry.GetName()), std::pair{ToSource, toIndex}});
    }

    for (const auto& [key, fromIndex] : fromFirst)
    {
        const MCFileEntry& fromEntry = fromEntries[static_cast<size_t>(fromIndex)];

        if (Deletes(fromEntry))
        {
            continue;
        }

        if (const auto found = toFirst.find(key);
            found == toFirst.end() || Deletes(toEntries[static_cast<size_t>(found->second)]))
        {
            picks.push_back(Pick{std::string(fromEntry.GetName()), std::nullopt});
        }
    }

    return Write(sources, picks, out);
}

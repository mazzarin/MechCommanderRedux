#include "stdafx.h"
#include "network/MCMcoContent.h"

#include "lib/MCSha256.h"
#include "platform/MCUtf8Path.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <fstream>
#include <map>

namespace
{
    constexpr std::string_view Header = "mco-redux-room-core-v2\n";
    /// <summary>The files' kinds counted.</summary>
    constexpr std::array<std::string_view, 9> Kinds = {".FST", ".PAK", ".MPK", ".PKK", ".FIT",
                                                       ".ABL", ".DPK", ".CSV", ".MAP"};
    /// <summary>The kinds counted by kind but left out by MCO's room rule.</summary>
    constexpr std::array<std::string_view, 2> LeftOutKinds = {".MPK", ".PKK"};
    /// <summary>Folders whose files are never counted (any depth), and Redux HD's own (the game folder's).</summary>
    constexpr std::array<std::string_view, 3> SkippedFolders = {"REDUX", "TEMP", "TMP"};
    constexpr std::array<std::string_view, 3> OurFolders = {"REDUX UPDATE", "REDUX BACKUP", "HD"};
    /// <summary>The places whose files aren't the room's (the saves and the maps).</summary>
    constexpr std::array<std::string_view, 3> LeftOutPlaces = {"DATA/SAVEGAME/", "DATA/MISSIONS/", "DATA/TERRAIN/"};
    constexpr std::array<std::string_view, 2> MapPlaces = {"DATA/MISSIONS/", "DATA/TERRAIN/"};
    constexpr std::string_view LeftOutFile = "WINDOWS.FIT";
    /// <summary>Where the user folder's files of the counted kinds stand in for the game's (with any .fst).</summary>
    constexpr std::string_view ObjectsPlace = "DATA/OBJECTS/";
    /// <summary>The archives counted even when they hold only maps.</summary>
    constexpr std::array<std::string_view, 5> CoreArchives = {"ART.FST", "MISC.FST", "MISSION.FST", "SHAPES.FST",
                                                              "TERRAIN.FST"};
    constexpr std::string_view ArchiveKind = ".FST";
    /// <summary>An .fst's table: a count, then entries of three int32s and a 250-byte name.</summary>
    constexpr int32_t ArchiveEntriesMost = 50000;
    constexpr size_t EntryNumbers = 3;
    constexpr size_t EntryNameBytes = 250;
    constexpr size_t EntryBytes = EntryNumbers * sizeof(int32_t) + EntryNameBytes;
    /// <summary>The most files a fingerprint counts (MCO's).</summary>
    constexpr size_t FilesMost = 50000;

    std::string Upper(std::string text)
    {
        std::ranges::transform(text, text.begin(),
                               [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; });
        return text;
    }

    bool StartsWithAny(std::string_view name, std::span<const std::string_view> starts)
    {
        return std::ranges::any_of(starts, [&](std::string_view start) { return name.starts_with(start); });
    }

    template <size_t Count> bool Among(std::string_view text, const std::array<std::string_view, Count>& among)
    {
        return std::ranges::find(among, text) != among.end();
    }

    /// <summary>The extension of a name already upper-cased (".FST"), or none.</summary>
    std::string_view KindOf(std::string_view name)
    {
        const size_t slash = name.rfind('/');
        const size_t dot = name.rfind('.');
        return dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash) ? std::string_view()
                                                                                                 : name.substr(dot);
    }

    /// <summary>MCO's IsRoomContent: the saves, the maps' folders, WINDOWS.FIT and the .mpk and .pkk aren't the room's.</summary>
    bool IsRoomContent(std::string_view name)
    {
        return name != LeftOutFile && !StartsWithAny(name, LeftOutPlaces) && !Among(KindOf(name), LeftOutKinds);
    }

    int32_t ReadInt32(std::istream& in)
    {
        std::array<char, sizeof(int32_t)> bytes{};
        in.read(bytes.data(), bytes.size());
        uint32_t value = 0;

        for (size_t i = 0; i < bytes.size(); i++)
        {
            value |= static_cast<uint32_t>(static_cast<uint8_t>(bytes[i])) << (8 * i);
        }

        return static_cast<int32_t>(value);
    }

    /// <summary>
    /// MCO's IsMapOnlyArchive: an .fst whose table is well formed and whose every entry is a map's file. A table that
    /// isn't one (too short, a count out of range, an entry past the file) isn't a map archive.
    /// </summary>
    bool IsMapOnlyArchive(const std::filesystem::path& path)
    {
        std::error_code error;
        const uintmax_t size = std::filesystem::file_size(path, error);
        std::ifstream in(path, std::ios::binary);

        if (error || !in || size < sizeof(int32_t))
        {
            return false;
        }

        const int32_t count = ReadInt32(in);

        if (count < 1 || count > ArchiveEntriesMost ||
            sizeof(int32_t) + static_cast<uintmax_t>(count) * EntryBytes > size)
        {
            return false;
        }

        bool maps = true;

        for (int32_t i = 0; i < count; i++)
        {
            const int32_t offset = ReadInt32(in);
            const int32_t length = ReadInt32(in);
            const int32_t other = ReadInt32(in);
            std::array<char, EntryNameBytes> raw{};
            in.read(raw.data(), raw.size());

            if (!in || offset < 0 || length < 0 || other < 0 ||
                static_cast<uintmax_t>(offset) + static_cast<uintmax_t>(length) > size)
            {
                return false;
            }

            // Up to the first NUL (249 characters when there's none, as MCO reads it), Latin-1, '\' as '/'.
            const auto nul = std::ranges::find(raw, '\0');
            std::string entry(raw.begin(), nul == raw.end() ? raw.end() - 1 : nul);
            std::ranges::replace(entry, '\\', '/');
            entry = Upper(std::move(entry));

            if (!StartsWithAny(entry, MapPlaces))
            {
                maps = false;
            }
        }

        return maps;
    }

    /// <summary>The files counted, by name (upper case, '/'), each where it is.</summary>
    using MCFiles = std::map<std::string, std::filesystem::path>;

    /// <summary>Which of a player's folders is read: the game folder, or the user folder overlaying it.</summary>
    enum class MCPass
    {
        Game,
        User,
    };

    /// <summary>Whether a folder on <paramref name="name"/>'s way is named Redux, Temp or Tmp.</summary>
    bool InSkippedFolder(std::string_view name)
    {
        for (size_t start = 0, slash = name.find('/'); slash != std::string_view::npos;
             start = slash + 1, slash = name.find('/', start))
        {
            if (Among(name.substr(start, slash - start), SkippedFolders))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>Whether the file <paramref name="name"/> in <paramref name="pass"/>'s folder is the room's (its archive aside).</summary>
    bool Counted(std::string_view name, MCPass pass)
    {
        const std::string_view kind = KindOf(name);

        if (!Among(kind, Kinds) || !IsRoomContent(name))
        {
            return false;
        }

        return pass == MCPass::Game ? !InSkippedFolder(name) : name.starts_with(ObjectsPlace) || kind == ArchiveKind;
    }

    /// <summary>
    /// The files counted in <paramref name="folder"/> (<paramref name="what"/> names it), each standing in for any of its
    /// name already in <paramref name="files"/>.
    /// </summary>
    std::expected<void, std::string> Collect(const std::filesystem::path& folder, MCPass pass, std::string_view what,
                                             MCFiles& files)
    {
        std::error_code error;
        auto it = std::filesystem::recursive_directory_iterator(folder, error);

        for (const auto end = std::filesystem::recursive_directory_iterator(); !error && it != end; it.increment(error))
        {
            const std::filesystem::directory_entry& entry = *it;
            std::string relative = MCFileSystem::ToUtf8(entry.path().lexically_relative(folder));
            std::ranges::replace(relative, '\\', '/');
            const std::string name = Upper(std::move(relative));

            // A link is never followed (MCO's skips reparse points); nor are Redux HD's own folders read.
            if (entry.is_symlink(error) || (entry.is_directory(error) && Among(name, OurFolders)))
            {
                if (entry.is_directory(error))
                {
                    it.disable_recursion_pending();
                }

                continue;
            }

            if (!entry.is_regular_file(error) || !Counted(name, pass))
            {
                continue;
            }

            if (KindOf(name) == ArchiveKind && !Among(std::string_view(name), CoreArchives) &&
                IsMapOnlyArchive(entry.path()))
            {
                continue;
            }

            files.insert_or_assign(name, entry.path());
        }

        if (error)
        {
            return std::unexpected(std::format("the {} can't be read: {}", what, error.message()));
        }

        return {};
    }
}

namespace MCMcoContent
{
    std::expected<std::string, std::string> Fingerprint(const std::filesystem::path& gameFolder,
                                                        const std::filesystem::path& userFolder)
    {
        MCFiles files;

        if (auto collected = Collect(gameFolder, MCPass::Game, "game folder", files); !collected)
        {
            return std::unexpected(collected.error());
        }

        std::error_code error;

        if (!userFolder.empty() && std::filesystem::is_directory(userFolder, error))
        {
            if (auto collected = Collect(userFolder, MCPass::User, "user folder", files); !collected)
            {
                return std::unexpected(collected.error());
            }
        }

        if (files.empty() || files.size() > FilesMost)
        {
            return std::unexpected(std::string("the game folder's data couldn't be checked"));
        }

        // The map keeps the names in byte order, MCO's ordinal order.
        MCSha256::MCHasher combined;
        combined.Add(Header);

        for (const auto& [name, path] : files)
        {
            const uintmax_t size = std::filesystem::file_size(path, error);
            auto digest = MCSha256::OfFile(path);

            if (error || !digest)
            {
                return std::unexpected(std::format("{} can't be read", name));
            }

            combined.Add(std::format("{}\n{}\n{}\n", name, size, MCSha256::Hex(*digest)));
        }

        return MCSha256::Hex(combined.Finish());
    }
}

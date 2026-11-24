#include "stdafx.h"
#include "logistics/MCMissionNames.h"
#include "platform/MCUtf8Path.h"

#include <nlohmann/json.hpp>

namespace
{
    using Json = nlohmann::json;

    /// <summary>The data file's name in the app's ui folder, and its two tables' keys.</summary>
    constexpr std::string_view UiFolder = "ui";
    constexpr std::string_view FileName = "missions.json";
    constexpr const char* OperationsKey = "operations";
    constexpr const char* MissionsKey = "missions";
    /// <summary>The line a briefing names its operation on, before the word (its operation's, then a slash).</summary>
    constexpr std::string_view FedcomMark = "FEDCOM:";
    /// <summary>The most a data file may hold (it is a few kilobytes): a larger one isn't read.</summary>
    constexpr std::uintmax_t MostFileBytes = 1 << 20;

    struct MCNames
    {
        bool Read = false;
        /// <summary>The operations by their written word; the missions by their briefing's file, lower case.</summary>
        std::unordered_map<std::string, std::string> Operations;
        std::unordered_map<std::string, std::string> Missions;
    };

    MCNames& Names()
    {
        static MCNames names;
        return names;
    }

    std::string_view Trimmed(std::string_view text)
    {
        constexpr std::string_view Blanks = " \t\r";
        const size_t first = text.find_first_not_of(Blanks);

        if (first == std::string_view::npos)
        {
            return {};
        }

        return text.substr(first, text.find_last_not_of(Blanks) - first + 1);
    }

    /// <summary>
    /// A briefing line's words without its formatting codes (MCSmuti's: %fc and a colour's digit, %f and a font's
    /// letter, %t and a tab's digits, %c, %n and the like).
    /// </summary>
    std::string Visible(std::string_view line)
    {
        std::string text;

        for (size_t at = 0; at < line.size(); ++at)
        {
            if (line[at] != '%' || at + 1 >= line.size())
            {
                text += line[at];
                continue;
            }

            const char code = line[++at];

            if (code == 'f' && at + 1 < line.size())
            {
                // %fc and one digit (a colour); %f and one letter (a font).
                at += line[at + 1] == 'c' && at + 2 < line.size() ? 2 : 1;
            }
            else if (code == 't')
            {
                while (at + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[at + 1])) != 0)
                {
                    ++at;
                }
            }
        }

        return text;
    }

    /// <summary>A title line's words, between its runs of stars ("***DESTROY CLAN***"); none for another line.</summary>
    std::optional<std::string> Starred(std::string_view visible)
    {
        const std::string_view text = Trimmed(visible);

        if (text.size() < 2 || text.front() != '*' || text.back() != '*')
        {
            return std::nullopt;
        }

        const size_t first = text.find_first_not_of("* ");
        const size_t last = text.find_last_not_of("* ");

        if (first == std::string_view::npos || last < first)
        {
            return std::nullopt;
        }

        return std::string(text.substr(first, last - first + 1));
    }

    /// <summary>The strings of <paramref name="table"/>, each by its key as <paramref name="key"/> makes it.</summary>
    std::unordered_map<std::string, std::string> StringsOf(const Json& table,
                                                           const std::function<std::string(std::string_view)>& key)
    {
        std::unordered_map<std::string, std::string> strings;

        if (!table.is_object())
        {
            return strings;
        }

        for (const auto& [name, value] : table.items())
        {
            if (value.is_string() && !value.get_ref<const std::string&>().empty())
            {
                strings.emplace(key(name), value.get<std::string>());
            }
            else
            {
                SDL_Log("MCMissionNames: '%s' has no name, so it shows as written", name.c_str());
            }
        }

        return strings;
    }

    void ReadAppFileOnce()
    {
        if (Names().Read)
        {
            return;
        }

        Names().Read = true;

        if (const auto read = MCMissionNames::Load(MCMissionNames::AppFile()); !read)
        {
            SDL_Log("MCMissionNames: %s; every name shows as its briefing writes it", read.error().c_str());
        }
    }
}

auto MCMissionNames::FromBriefing(std::string_view text) -> MCWritten
{
    MCWritten written;
    bool afterFedcom = false;
    std::vector<std::string> title;

    for (size_t start = 0; start <= text.size();)
    {
        const size_t end = std::min(text.find('\n', start), text.size());
        const std::string visible = Visible(text.substr(start, end - start));
        start = end + 1;

        if (const size_t mark = visible.find(FedcomMark); !afterFedcom && mark != std::string::npos)
        {
            const std::string_view rest = std::string_view(visible).substr(mark + FedcomMark.size());
            written.Operation = std::string(Trimmed(rest.substr(0, rest.find('/'))));
            afterFedcom = true;
            continue;
        }

        if (!afterFedcom)
        {
            continue;
        }

        if (std::optional<std::string> words = Starred(visible); words.has_value())
        {
            title.push_back(std::move(*words));
            continue;
        }

        // Before the title only the rule between them (dashes) or a blank line; after it, the title has ended.
        const std::string_view plain = Trimmed(visible);

        if (!title.empty() || plain.find_first_not_of('-') != std::string_view::npos)
        {
            break;
        }
    }

    for (const std::string& line : title)
    {
        written.Mission += (written.Mission.empty() ? "" : " ") + line;
    }

    return written;
}

auto MCMissionNames::Load(const std::filesystem::path& file) -> std::expected<void, std::string>
{
    Names() = MCNames{.Read = true};
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(file, error);

    if (error || size > MostFileBytes)
    {
        return std::unexpected(std::format("the missions' names {} can't be read{}", MCFileSystem::ToUtf8(file),
                                           error ? std::format(" ({})", error.message()) : std::string(": too large")));
    }

    std::ifstream in(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const Json parsed = Json::parse(bytes, nullptr, false);

    if (!parsed.is_object())
    {
        return std::unexpected(std::format("the missions' names {} aren't a JSON object", MCFileSystem::ToUtf8(file)));
    }

    const auto table = [&parsed](const char* key) { return parsed.contains(key) ? parsed.at(key) : Json(); };
    Names().Operations = StringsOf(table(OperationsKey), [](std::string_view key) { return std::string(key); });
    Names().Missions = StringsOf(table(MissionsKey), MCMissionNames::Lowered);
    return {};
}

auto MCMissionNames::Lowered(std::string_view text) -> std::string
{
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

auto MCMissionNames::AppFile() -> std::filesystem::path
{
    // SDL's base path is the program's folder, or the Mac app's resources (null when it can't tell).
    const char* base = SDL_GetBasePath();
    const std::filesystem::path folder = base != nullptr ? MCFileSystem::FromUtf8(base) : std::filesystem::path();
    return folder / UiFolder / FileName;
}

auto MCMissionNames::Mission(std::string_view briefingFile, std::string_view written) -> std::string
{
    ReadAppFileOnce();
    const auto found = Names().Missions.find(Lowered(briefingFile));
    return found != Names().Missions.end() ? found->second : std::string(written);
}

auto MCMissionNames::Operation(std::string_view written) -> std::string
{
    ReadAppFileOnce();
    const auto found = Names().Operations.find(std::string(written));
    return found != Names().Operations.end() ? found->second : std::string(written);
}

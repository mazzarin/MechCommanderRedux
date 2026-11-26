#include "stdafx.h"
#include "logistics/MCBriefingText.h"

namespace
{
    /// <summary>The formatting codes' lead and the codes the reading follows (MCSmuti's: a line's end, a font, a tab).</summary>
    constexpr char CodeMark = '%';
    constexpr char LineEnd = 'n';
    constexpr char FontCode = 'f';
    constexpr char ColorCode = 'c';
    constexpr char TabCode = 't';
    /// <summary>Characters under this are left out, as MCSmuti leaves them (the file's own line ends).</summary>
    constexpr unsigned char FirstPrinted = 0x20;

    /// <summary>The lines the parts are found by.</summary>
    constexpr std::string_view Rule = "----------";
    constexpr std::string_view Stars = "***";
    constexpr std::string_view FireSupportLine = "FIRE SUPPORT:";
    constexpr std::string_view ArtilleryLine = "ARTILLERY:";
    constexpr std::string_view SensorProbeLine = "SENSOR PROBE:";
    constexpr std::string_view CameraDroneLine = "CAMERA DRONE:";
    constexpr std::string_view SmallWord = "Small";
    constexpr std::string_view LargeWord = "Large";
    constexpr std::string_view WarningLead = "**TACTICAL";
    constexpr std::string_view WarningMark = "**";
    constexpr std::string_view ItemMark = "*";
    constexpr std::string_view EndLine = "<<<<END DISPATCH";
    constexpr char ObjectiveMark = '-';
    /// <summary>The fire support's one line when it gives nothing.</summary>
    constexpr std::string_view NoFireSupport = "None";
    /// <summary>
    /// A time limit's line: TIME LIMIT and its time (minutes, or minutes:seconds), and nothing but these words ("MISSION
    /// TIME LIMIT- 15:00", "%t9OBJECTIVE TIME LIMIT- 06:00", "TIME LIMIT: 25 MINUTES", "***25 MINUTE TIME LIMIT***"); an
    /// objective's names OBJECTIVE.
    /// </summary>
    constexpr std::string_view TimeWord = "TIME";
    constexpr std::string_view LimitWord = "LIMIT";
    constexpr std::string_view ObjectiveWord = "OBJECTIVE";
    constexpr std::array<std::string_view, 3> LimitWords = {"MISSION", "MINUTE", "MINUTES"};
    constexpr char TimeMark = ':';
    constexpr size_t SecondsDigits = 2;

    /// <summary>A line of the text (to its %n), its codes left out; whether it starts at a tab (a line carried on).</summary>
    struct MCLine
    {
        std::string Text;
        bool Tabbed = false;
    };

    std::string Trimmed(std::string_view text)
    {
        const size_t first = text.find_first_not_of(' ');

        if (first == std::string_view::npos)
        {
            return {};
        }

        return std::string(text.substr(first, text.find_last_not_of(' ') - first + 1));
    }

    /// <summary><paramref name="text"/> with each run of spaces one space, trimmed.</summary>
    std::string Spaced(std::string_view text)
    {
        std::string spaced;

        for (const char c : text)
        {
            if (c != ' ' || (!spaced.empty() && spaced.back() != ' '))
            {
                spaced += c;
            }
        }

        return Trimmed(spaced);
    }

    /// <summary>The text's lines as MCSmuti writes them, its codes left out.</summary>
    std::vector<MCLine> Lines(std::string_view text)
    {
        std::vector<MCLine> lines(1);

        for (size_t at = 0; at < text.size(); ++at)
        {
            const char c = text[at];

            if (c != CodeMark)
            {
                if (static_cast<unsigned char>(c) >= FirstPrinted)
                {
                    lines.back().Text += c;
                }

                continue;
            }

            if (at + 1 >= text.size())
            {
                break;
            }

            const char code = static_cast<char>(std::tolower(static_cast<unsigned char>(text[++at])));

            if (code == LineEnd)
            {
                lines.emplace_back();
            }
            else if (code == FontCode)
            {
                // A colour's digit follows its code; a size (l, m, s) is the code's own letter.
                if (at + 1 < text.size() && std::tolower(static_cast<unsigned char>(text[++at])) == ColorCode &&
                    at + 1 < text.size())
                {
                    ++at;
                }
            }
            else if (code == TabCode)
            {
                while (at + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[at + 1])) != 0)
                {
                    ++at;
                }

                // A tab at a line's start carries the line before on; inside one it parts words.
                if (Trimmed(lines.back().Text).empty())
                {
                    lines.back().Tabbed = true;
                }
                else
                {
                    lines.back().Text += ' ';
                }
            }
            else if (code == CodeMark)
            {
                lines.back().Text += CodeMark;
            }
            else if (code != ColorCode)
            {
                // Any other code is text, as MCSmuti writes it (a centred line's %c is left out).
                lines.back().Text += CodeMark;
                lines.back().Text += text[at];
            }
        }

        for (MCLine& line : lines)
        {
            line.Text = Spaced(line.Text);
        }

        return lines;
    }

    /// <summary>The number after <paramref name="lead"/> in <paramref name="line"/>, and before <paramref name="word"/> when given.</summary>
    int32_t NumberIn(std::string_view line, std::string_view lead, std::string_view word = {})
    {
        size_t at = line.find(lead);

        if (at == std::string_view::npos)
        {
            return 0;
        }

        std::string_view rest = line.substr(at + lead.size());

        if (!word.empty())
        {
            const size_t end = rest.find(word);

            if (end == std::string_view::npos)
            {
                return 0;
            }

            // The last number before the word.
            rest = rest.substr(0, end);
            const size_t last = rest.find_last_of("0123456789");

            if (last == std::string_view::npos)
            {
                return 0;
            }

            at = rest.find_last_not_of("0123456789", last);
            rest = rest.substr(at == std::string_view::npos ? 0 : at + 1);
        }

        const size_t digits = rest.find_first_of("0123456789");
        int32_t number = 0;

        if (digits != std::string_view::npos)
        {
            const std::string_view from = rest.substr(digits);
            std::from_chars(from.data(), from.data() + from.size(), number);
        }

        return number;
    }

    /// <summary>"1-Destroy the HQ": the objective's text after its number and dash; none for another line.</summary>
    std::optional<std::string> ObjectiveOf(std::string_view line)
    {
        size_t digits = 0;

        while (digits < line.size() && std::isdigit(static_cast<unsigned char>(line[digits])) != 0)
        {
            ++digits;
        }

        const std::string rest = Trimmed(line.substr(digits));

        if (digits == 0 || rest.empty() || rest.front() != ObjectiveMark)
        {
            return std::nullopt;
        }

        return Trimmed(std::string_view(rest).substr(1));
    }

    /// <summary>A time limit a line names: its time as m:ss ("15:00", "6:00"), and whether it's an objective's.</summary>
    struct MCLimitLine
    {
        std::string Time;
        bool Objective = false;
    };

    /// <summary>"25" or "06:00" as minutes and seconds; none for anything else.</summary>
    std::optional<std::pair<int32_t, int32_t>> TimeOf(std::string_view word)
    {
        const size_t mark = word.find(TimeMark);
        const std::string_view minutesWords = word.substr(0, mark);
        const std::string_view secondsWords =
            mark == std::string_view::npos ? std::string_view() : word.substr(mark + 1);
        int32_t minutes = 0;
        int32_t seconds = 0;
        const auto [minutesEnd, minutesError] =
            std::from_chars(minutesWords.data(), minutesWords.data() + minutesWords.size(), minutes);

        if (minutesWords.empty() || minutesError != std::errc() ||
            minutesEnd != minutesWords.data() + minutesWords.size())
        {
            return std::nullopt;
        }

        if (mark != std::string_view::npos)
        {
            const auto [secondsEnd, secondsError] =
                std::from_chars(secondsWords.data(), secondsWords.data() + secondsWords.size(), seconds);

            if (secondsWords.size() != SecondsDigits || secondsError != std::errc() ||
                secondsEnd != secondsWords.data() + secondsWords.size())
            {
                return std::nullopt;
            }
        }

        return std::pair{minutes, seconds};
    }

    /// <summary>The time limit <paramref name="line"/> names, if it is a time limit's line and nothing more.</summary>
    std::optional<MCLimitLine> TimeLimitOf(std::string_view line)
    {
        MCLimitLine limit;
        std::optional<std::pair<int32_t, int32_t>> time;
        bool named = false;
        std::string before;
        std::string word;

        const auto digit = [&line](size_t at)
        { return at < line.size() && std::isdigit(static_cast<unsigned char>(line[at])) != 0; };

        // Its words, parted by anything but letters and digits (the time's mark between digits is the time's).
        for (size_t at = 0; at <= line.size(); ++at)
        {
            const char c = at < line.size() ? line[at] : ' ';

            if (std::isalnum(static_cast<unsigned char>(c)) != 0 ||
                (c == TimeMark && at > 0 && digit(at - 1) && digit(at + 1)))
            {
                word += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                continue;
            }

            if (word.empty())
            {
                continue;
            }

            if (word == LimitWord && before == TimeWord)
            {
                named = true;
            }
            else if (word == ObjectiveWord)
            {
                limit.Objective = true;
            }
            else if (const std::optional<std::pair<int32_t, int32_t>> given = TimeOf(word);
                     given.has_value() && !time.has_value())
            {
                time = given;
            }
            else if (word != TimeWord && std::ranges::find(LimitWords, word) == LimitWords.end())
            {
                return std::nullopt;
            }

            before = std::move(word);
            word.clear();
        }

        if (!named || !time.has_value())
        {
            return std::nullopt;
        }

        limit.Time = std::format("{}:{:02}", time->first, time->second);
        return limit;
    }

    /// <summary>A mission's title (***LIBERATE WORK CAMPS***), not the classification over it (****CLASSIFIED****).</summary>
    bool Starred(std::string_view line)
    {
        return line.size() > Stars.size() * 2 && line.starts_with(Stars) && line.ends_with(Stars) &&
               line[Stars.size()] != Stars.front();
    }

    /// <summary>"<paramref name="count"/> <paramref name="one"/>", its plural with an s.</summary>
    std::string Counted(int32_t count, std::string_view one, bool plural)
    {
        return std::format("{} {}{}", count, one, plural && count != 1 ? "s" : "");
    }
}

auto MCBriefingText::Read(std::string_view text) -> MCRead
{
    enum class MCPart : uint8_t
    {
        Head,
        Objectives,
        FireSupport,
        Orders,
        Warnings,
    };

    MCRead read;
    MCPart part = MCPart::Head;
    bool titled = false;
    // A paragraph or an item being read (a line carried on joins it).
    bool open = false;

    const auto add = [&open](std::vector<std::string>& to, std::string_view line)
    {
        if (open && !to.empty())
        {
            to.back() += ' ';
            to.back() += line;
        }
        else
        {
            to.emplace_back(line);
        }

        open = true;
    };

    for (const MCLine& line : Lines(text))
    {
        const std::string_view words = line.Text;

        if (words.starts_with(EndLine))
        {
            break;
        }

        if (words.empty())
        {
            open = false;

            if (part == MCPart::Objectives)
            {
                part = MCPart::Head;
            }

            continue;
        }

        if (words == Rule)
        {
            open = false;
            // The orders follow the rule after the objectives (and the fire support).
            part = titled ? MCPart::Orders : MCPart::Head;
            continue;
        }

        // A time limit: the mission's, or the objective's above it (not words of the objective: it reads apart).
        if (const std::optional<MCLimitLine> limit = TimeLimitOf(words); limit.has_value())
        {
            if (limit->Objective && !read.Objectives.empty())
            {
                read.Objectives.back().TimeLimit = limit->Time;
            }
            else
            {
                read.TimeLimit = limit->Time;
            }

            open = false;
            continue;
        }

        if (Starred(words))
        {
            titled = true;
            part = MCPart::Objectives;
            open = false;
            continue;
        }

        if (words.starts_with(WarningLead))
        {
            std::string head(words);
            std::erase(head, WarningMark.front());
            read.WarningsHead = Trimmed(head);
            part = MCPart::Warnings;
            open = false;
            continue;
        }

        if (words.starts_with(FireSupportLine))
        {
            read.FireSupport = MCFireSupport{};
            part = MCPart::FireSupport;
            open = false;
            continue;
        }

        switch (part)
        {
            case MCPart::Head:
                break;
            case MCPart::Objectives:
            {
                if (std::optional<std::string> objective = ObjectiveOf(words); objective.has_value())
                {
                    read.Objectives.push_back(MCObjective{.Text = std::move(*objective)});
                }
                else if (!read.Objectives.empty())
                {
                    read.Objectives.back().Text += ' ';
                    read.Objectives.back().Text += words;
                }

                break;
            }
            case MCPart::FireSupport:
            {
                MCFireSupport& support = *read.FireSupport;

                if (words.starts_with(ArtilleryLine))
                {
                    support.SmallArtillery = NumberIn(words, ArtilleryLine, SmallWord);
                    support.LargeArtillery = NumberIn(words, ArtilleryLine, LargeWord);
                }
                else if (words.starts_with(SensorProbeLine))
                {
                    support.SensorProbes = NumberIn(words, SensorProbeLine);
                }
                else if (words.starts_with(CameraDroneLine))
                {
                    support.CameraDrones = NumberIn(words, CameraDroneLine);
                }

                break;
            }
            case MCPart::Orders:
            {
                add(read.Orders, words);
                break;
            }
            case MCPart::Warnings:
            {
                // A star starts a warning; a line without one carries the last on.
                if (words.starts_with(ItemMark))
                {
                    open = false;
                    add(read.Warnings, Trimmed(words.substr(ItemMark.size())));
                }
                else
                {
                    add(read.Warnings, words);
                }

                break;
            }
        }
    }

    return read;
}

auto MCBriefingText::Plain(std::string_view text) -> std::string
{
    std::string plain;

    for (const MCLine& line : Lines(text))
    {
        if (!line.Text.empty())
        {
            plain += plain.empty() ? line.Text : " " + line.Text;
        }
    }

    return plain;
}

auto MCBriefingText::FireSupportLines(const MCFireSupport& support) -> std::vector<std::string>
{
    std::vector<std::string> given;

    if (support.SmallArtillery > 0)
    {
        given.push_back(Counted(support.SmallArtillery, "small artillery", false));
    }

    if (support.LargeArtillery > 0)
    {
        given.push_back(Counted(support.LargeArtillery, "large artillery", false));
    }

    if (support.SensorProbes > 0)
    {
        given.push_back(Counted(support.SensorProbes, "sensor probe", true));
    }

    if (support.CameraDrones > 0)
    {
        given.push_back(Counted(support.CameraDrones, "camera drone", true));
    }

    if (given.empty())
    {
        given.emplace_back(NoFireSupport);
    }

    return given;
}

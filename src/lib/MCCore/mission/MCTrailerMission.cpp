#include "stdafx.h"
#include "mission/MCTrailerMission.h"
#include "lib/MCFastFileSet.h"
#include "main/MCGameContext.h"
#include "platform/MCFileSystem.h"

// The edits are those of trailer_mission.py's regular expressions, matched here as Python matches them (the text
// decoded as Latin-1, its \s and \w Python's for those characters), so the file is the script's to the byte.

namespace
{
    /// <summary>A pilot of the trailer's radio track, in the company's order, and the mech they drive.</summary>
    struct MCPilot
    {
        /// <summary>The callsign, for the reader (the warrior profile names the pilot to the game).</summary>
        std::string_view Callsign;
        /// <summary>The warrior profile: voice letter and movie.</summary>
        std::string_view Profile;
        int32_t ObjectNumber = 0;
        std::string_view ObjectProfile;
        /// <summary>The mech's name, the comment on its ObjectProfile line.</summary>
        std::string_view Name;
    };

    /// <summary>The track's voices, part 0x200 on: a mixed company, jump jets for the two who jump.</summary>
    constexpr std::array<MCPilot, 10> Pilots = {{
        {"BEAST", "PMW00051", 51, "PM109400", "Atlas"},
        {"SIREN", "PMW00053", 18, "PM106400", "Catapult"},
        {"FIEND", "PMW00061", 3, "PM108400", "Awesome"},
        {"ISIS", "PMW00055", 54, "PM204400", "Vulture"},
        {"GUNMAN", "PMW00056", 52, "PM207300", "Mad Cat J"},
        {"HAWK", "PMW00052", 1, "PM206300", "Thor J"},
        {"SCARAB", "PMW00069", 58, "PM203400", "Hunchback IIc"},
        {"SKATER", "PMW00058", 53, "PM208400", "Masakari"},
        {"BARON", "PMW00060", 2, "PM205400", "Loki"},
        {"SPICE", "PMW00059", 878, "PM211400", "Turkina"},
    }};

    /// <summary>The added mechs stand on the first's grid, two to a rank, a rank further back each.</summary>
    constexpr double RankStep = -50.0;
    constexpr double FileStep = 50.0;
    /// <summary>A commander's group holds twelve; the company is one group, its empty places 0.</summary>
    constexpr size_t GroupSize = 12;
    /// <summary>A part's line saying it's a mech (an own part without it, a mine layer, is made one).</summary>
    constexpr std::string_view MechControl = "ControlDataType      = 1";

    /// <summary>A mech the other side's are swapped for.</summary>
    struct MCMech
    {
        int32_t ObjectNumber = 0;
        std::string_view Profile;
        std::string_view Name;
    };

    constexpr MCMech MasakariA{53, "PM208100", "Masakari A"};
    constexpr MCMech MasakariW{53, "PM208400", "Masakari W"};
    constexpr MCMech MasakariJ{53, "PM208300", "Masakari J"};
    constexpr MCMech ThorA{1, "PM206100", "Thor A"};

    struct MCPlace
    {
        double X = 0.0;
        double Y = 0.0;
    };

    /// <summary>An enemy mech swapped, and set down where given.</summary>
    struct MCSwap
    {
        int32_t Part = 0;
        MCMech Mech;
        std::optional<MCPlace> Place;
    };

    /// <summary>Another part moved out of the way.</summary>
    struct MCMove
    {
        int32_t Part = 0;
        MCPlace Place;
    };

    /// <summary>
    /// A mission's edit: the player's own parts, in order (their warriors numbered alike); the part (a mech) and warrior
    /// the added ones are copied from; where the company starts (x, y and facing: the first's place on the grid, which
    /// then holds all ten), else where the first stands; the other side's mechs swapped; other parts moved away.
    /// </summary>
    struct MCSetup
    {
        std::vector<int32_t> Own;
        int32_t Template = 0;
        std::optional<std::array<double, 3>> Start;
        std::vector<MCSwap> Enemies;
        std::vector<MCMove> Moves;
    };

    /// <summary>Mission 29 ("Thwart Reinforcements"): the company of five made ten.</summary>
    constexpr std::string_view ThwartReinforcements = "mis0503";
    /// <summary>
    /// Mission 16's canyon ("Liberate Education Camp"): a terrace under a rock wall with a cliff dropping off to its
    /// south. The company starts at its west end and walks east along it; the other side's mechs (all heavies, so the
    /// late fight has shooters free) wait further east on it, the Thors in the group that holds its ground there; the
    /// hovercraft between them and the company are sent away.
    /// </summary>
    constexpr std::string_view Canyon = "mis0304";

    std::optional<MCSetup> SetupOf(std::string_view mission)
    {
        if (mission == ThwartReinforcements)
        {
            constexpr int32_t TemplatePart = 55;
            return MCSetup{.Own = {51, 52, 53, 54, 55}, .Template = TemplatePart};
        }

        if (mission != Canyon)
        {
            return std::nullopt;
        }

        constexpr int32_t FirstOwn = 42;
        constexpr int32_t EndOwn = 50;
        constexpr std::array<double, 3> CanyonStart = {-600.0, -3950.0, 90.0};
        constexpr MCPlace CanyonEnemies{2360.0, -2340.0};
        constexpr MCPlace CanyonAway{2500.0, 5800.0};
        constexpr double CanyonSpacing = 100.0;
        constexpr size_t AcrossAGrid = 4;
        constexpr std::array CanyonMechs = {1, 5, 6, 9, 10, 11, 17, 18, 19, 20, 21, 35, 36, 37};
        constexpr std::array CanyonSwaps = {MasakariA, MasakariA, MasakariW, ThorA,     ThorA,     ThorA, MasakariW,
                                            MasakariW, MasakariJ, MasakariA, MasakariW, MasakariW, ThorA, MasakariA};
        constexpr int32_t FirstHovercraft = 24;
        constexpr int32_t EndHovercraft = 35;
        static_assert(CanyonMechs.size() == CanyonSwaps.size());
        const auto GridPlace = [&](MCPlace corner, size_t slot)
        {
            return MCPlace{corner.X + static_cast<double>(slot % AcrossAGrid) * CanyonSpacing,
                           corner.Y + static_cast<double>(slot / AcrossAGrid) * CanyonSpacing};
        };

        MCSetup setup{.Template = FirstOwn, .Start = CanyonStart};

        for (int32_t part = FirstOwn; part < EndOwn; part++)
        {
            setup.Own.push_back(part);
        }

        for (size_t slot = 0; slot < CanyonMechs.size(); slot++)
        {
            setup.Enemies.push_back({CanyonMechs[slot], CanyonSwaps[slot], GridPlace(CanyonEnemies, slot)});
        }

        for (int32_t part = FirstHovercraft; part < EndHovercraft; part++)
        {
            setup.Moves.push_back({part, GridPlace(CanyonAway, static_cast<size_t>(part - FirstHovercraft))});
        }

        return setup;
    }

    /// <summary>Why the edit couldn't be made (thrown inside Make, returned from it).</summary>
    struct MCMissing
    {
        std::string What;
    };

    /// <summary>Python's \s for a Latin-1 character: ASCII whitespace, the separators 0x1c-0x1f, NEL and no-break space.</summary>
    bool Space(char c)
    {
        constexpr unsigned char NextLine = 0x85;
        constexpr unsigned char NoBreakSpace = 0xa0;
        const auto byte = static_cast<unsigned char>(c);
        return (byte >= '\t' && byte <= '\r') || (byte >= 0x1c && byte <= ' ') || byte == NextLine ||
               byte == NoBreakSpace;
    }

    /// <summary>Python's \w for a Latin-1 character: letters, digits (superscripts and fractions too) and _.</summary>
    bool Word(char c)
    {
        constexpr std::array<unsigned char, 9> OtherWord = {0xaa, 0xb2, 0xb3, 0xb5, 0xb9, 0xba, 0xbc, 0xbd, 0xbe};
        constexpr unsigned char FirstLetter = 0xc0;
        constexpr unsigned char Times = 0xd7;
        constexpr unsigned char Divide = 0xf7;
        const auto byte = static_cast<unsigned char>(c);
        return std::isalnum(byte) != 0 || byte == '_' || (byte >= FirstLetter && byte != Times && byte != Divide) ||
               std::ranges::contains(OtherWord, byte);
    }

    bool Digit(char c)
    {
        return c >= '0' && c <= '9';
    }

    /// <summary>Whether <paramref name="at"/> starts a line (re.M's ^).</summary>
    bool LineStart(std::string_view text, size_t at)
    {
        return at == 0 || text[at - 1] == '\n';
    }

    /// <summary>Whether re.M's $ holds at <paramref name="at"/>: the text's end, or before a newline.</summary>
    bool LineEnd(std::string_view text, size_t at)
    {
        return at == text.size() || text[at] == '\n';
    }

    /// <summary>Python's str.replace: every <paramref name="old"/> (not empty) replaced, left to right.</summary>
    std::string ReplaceAll(std::string_view text, std::string_view old, std::string_view replacement)
    {
        std::string out;

        for (size_t from = 0;;)
        {
            const size_t at = text.find(old, from);

            if (at == std::string_view::npos)
            {
                out += text.substr(from);
                return out;
            }

            out += text.substr(from, at - from);
            out += replacement;
            from = at + old.size();
        }
    }

    /// <summary>
    /// A FIT block (trailer_mission.py's block: ^\[name\]\r\n.*?(?=^\[|^FITend)), from its header to the next one.
    /// </summary>
    std::string Block(std::string_view text, std::string_view name)
    {
        const std::string header = std::format("[{}]\r\n", name);

        for (size_t at = text.find(header); at != std::string_view::npos; at = text.find(header, at + 1))
        {
            if (!LineStart(text, at))
            {
                continue;
            }

            for (size_t end = at + header.size(); end < text.size(); end++)
            {
                if (LineStart(text, end) && (text[end] == '[' || text.substr(end).starts_with("FITend")))
                {
                    return std::string(text.substr(at, end - at));
                }
            }

            break;
        }

        throw MCMissing{std::format("no [{}] block", name)};
    }

    /// <summary>Whether a line starts with block <paramref name="name"/>'s header (^\[name\]).</summary>
    bool HasBlock(std::string_view text, std::string_view name)
    {
        const std::string header = std::format("[{}]", name);

        for (size_t at = text.find(header); at != std::string_view::npos; at = text.find(header, at + 1))
        {
            if (LineStart(text, at))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>
    /// Where a value's line ends from <paramref name="at"/> on (the setting's (\s*(?://[^\r\n]*)?\r?)$, greedy as
    /// Python tries it): the end of that group, or none.
    /// </summary>
    std::optional<size_t> MatchLineEnd(std::string_view text, size_t at)
    {
        size_t spaces = at;

        while (spaces < text.size() && Space(text[spaces]))
        {
            spaces++;
        }

        for (size_t after = spaces + 1; after-- > at;)
        {
            std::array<size_t, 2> ends{};
            size_t tries = 0;

            if (text.substr(after).starts_with("//"))
            {
                size_t comment = after + 2;

                while (comment < text.size() && text[comment] != '\r' && text[comment] != '\n')
                {
                    comment++;
                }

                ends[tries++] = comment;
            }

            ends[tries++] = after;

            for (size_t i = 0; i < tries; i++)
            {
                if (ends[i] < text.size() && text[ends[i]] == '\r' && LineEnd(text, ends[i] + 1))
                {
                    return ends[i] + 1;
                }

                if (LineEnd(text, ends[i]))
                {
                    return ends[i];
                }
            }
        }

        return std::nullopt;
    }

    /// <summary>A setting's line matched: where its value starts, where the line's end (its comment) starts, the end.</summary>
    struct MCSettingMatch
    {
        size_t Value = 0;
        size_t Rest = 0;
        size_t End = 0;
    };

    /// <summary>
    /// The setting <paramref name="key"/>'s line starting at <paramref name="at"/>
    /// (^(\w+(?:\[\d+\])?\s+key\s*=\s*)[^/\r\n]*?(\s*(?://[^\r\n]*)?\r?)$), or none.
    /// </summary>
    std::optional<MCSettingMatch> MatchSetting(std::string_view text, size_t at, std::string_view key)
    {
        size_t p = at;

        while (p < text.size() && Word(text[p]))
        {
            p++;
        }

        if (p == at)
        {
            return std::nullopt;
        }

        if (p < text.size() && text[p] == '[')
        {
            size_t digits = p + 1;

            while (digits < text.size() && Digit(text[digits]))
            {
                digits++;
            }

            if (digits > p + 1 && digits < text.size() && text[digits] == ']')
            {
                p = digits + 1;
            }
        }

        const size_t typeEnd = p;

        while (p < text.size() && Space(text[p]))
        {
            p++;
        }

        if (p == typeEnd || !text.substr(p).starts_with(key))
        {
            return std::nullopt;
        }

        p += key.size();

        while (p < text.size() && Space(text[p]))
        {
            p++;
        }

        if (p >= text.size() || text[p] != '=')
        {
            return std::nullopt;
        }

        const size_t equals = p + 1;
        size_t spaces = equals;

        while (spaces < text.size() && Space(text[spaces]))
        {
            spaces++;
        }

        // The spaces after '=' greedy, the value as short as the line's end lets it be.
        for (size_t value = spaces + 1; value-- > equals;)
        {
            for (size_t end = value;; end++)
            {
                if (const std::optional<size_t> rest = MatchLineEnd(text, end); rest.has_value())
                {
                    return MCSettingMatch{.Value = value, .Rest = end, .End = *rest};
                }

                if (end >= text.size() || text[end] == '/' || text[end] == '\r' || text[end] == '\n')
                {
                    break;
                }
            }
        }

        return std::nullopt;
    }

    /// <summary>
    /// trailer_mission.py's set_value: the first `type key = value` line's value replaced, its comment kept (or
    /// replaced by <paramref name="comment"/>).
    /// </summary>
    std::string SetValue(std::string_view text, std::string_view key, std::string_view value,
                         std::optional<std::string_view> comment = std::nullopt)
    {
        for (size_t at = 0; at <= text.size(); at = text.find('\n', at) + 1)
        {
            if (const std::optional<MCSettingMatch> found = MatchSetting(text, at, key); found.has_value())
            {
                std::string out(text.substr(0, found->Value));
                out += value;
                out += comment.has_value() ? std::format("        // {}\r", *comment)
                                           : std::string(text.substr(found->Rest, found->End - found->Rest));
                out += text.substr(found->End);
                return out;
            }

            if (text.find('\n', at) == std::string_view::npos)
            {
                break;
            }
        }

        throw MCMissing{std::format("no {}", key)};
    }

    /// <summary>trailer_mission.py's read_number: the first `key = number` (key\s*=\s*(-?[\d.]+)).</summary>
    double ReadNumber(std::string_view text, std::string_view key)
    {
        for (size_t at = text.find(key); at != std::string_view::npos; at = text.find(key, at + 1))
        {
            size_t p = at + key.size();

            while (p < text.size() && Space(text[p]))
            {
                p++;
            }

            if (p >= text.size() || text[p] != '=')
            {
                continue;
            }

            p++;

            while (p < text.size() && Space(text[p]))
            {
                p++;
            }

            const size_t number = p;
            p += p < text.size() && text[p] == '-' ? 1 : 0;
            const size_t digits = p;

            while (p < text.size() && (Digit(text[p]) || text[p] == '.'))
            {
                p++;
            }

            if (p > digits)
            {
                // strtod: libc++ has floating-point from_chars only from macOS 26.
                return std::strtod(std::string(text.substr(number, p - number)).c_str(), nullptr);
            }
        }

        throw MCMissing{std::format("no {}", key)};
    }

    /// <summary>re.sub(r'Profile = "\w+"', replacement): every warrior profile in <paramref name="text"/> replaced.</summary>
    std::string SetProfiles(std::string_view text, std::string_view replacement)
    {
        constexpr std::string_view Head = "Profile = \"";
        std::string out;
        size_t from = 0;

        for (size_t at = text.find(Head); at != std::string_view::npos;)
        {
            size_t end = at + Head.size();

            while (end < text.size() && Word(text[end]))
            {
                end++;
            }

            if (end > at + Head.size() && end < text.size() && text[end] == '"')
            {
                out += text.substr(from, at - from);
                out += replacement;
                from = end + 1;
                at = text.find(Head, from);
            }
            else
            {
                at = text.find(Head, at + 1);
            }
        }

        out += text.substr(from);
        return out;
    }

    /// <summary>A number as Python's {:.0f} writes it.</summary>
    std::string Whole(double value)
    {
        return std::format("{:.0f}", value);
    }

    std::string PartBlock(int32_t part)
    {
        return std::format("Part{}", part);
    }

    std::string WarriorBlock(int32_t warrior)
    {
        return std::format("Warrior{}", warrior);
    }

    /// <summary>A part's mech swapped: its object, and its profile with the mech's name as the line's comment.</summary>
    std::string SetMech(std::string_view part, int32_t objectNumber, std::string_view profile, std::string_view name)
    {
        const std::string swapped = SetValue(part, "ObjectNumber", std::format(" {} ", objectNumber));
        return SetValue(swapped, "ObjectProfile", std::format("\"{}\"", profile), name);
    }

    /// <summary>A part set down at <paramref name="place"/>.</summary>
    std::string SetPlace(std::string_view part, MCPlace place)
    {
        return SetValue(SetValue(part, "PositionX", Whole(place.X)), "PositionY", Whole(place.Y) + " ");
    }

    /// <summary>The trailer's force in mission text <paramref name="original"/>, as <paramref name="setup"/> says.</summary>
    std::string Edit(std::string text, const MCSetup& setup)
    {
        const std::vector<int32_t>& own = setup.Own;
        const auto lastPart = static_cast<int32_t>(ReadNumber(text, "NumParts"));
        const auto lastWarrior = static_cast<int32_t>(ReadNumber(text, "NumWarriors"));
        // The pilots past the own parts are added after the mission's last part (and warrior).
        std::vector<int32_t> numbers(own.begin(),
                                     own.begin() + static_cast<ptrdiff_t>(std::min(own.size(), Pilots.size())));

        for (int32_t added = lastPart + 1; numbers.size() < Pilots.size(); added++)
        {
            numbers.push_back(added);
        }

        const int32_t last = numbers.back();
        const auto IsOwn = [&](int32_t number) { return std::ranges::contains(own, number); };

        // The own warriors made the first pilots; the others added after the last, as the template is.
        const std::string warriorTemplate = Block(text, WarriorBlock(setup.Template));
        std::string added;

        for (size_t i = 0; i < numbers.size(); i++)
        {
            const std::string profile = std::format("Profile = \"{}\"", Pilots[i].Profile);

            if (IsOwn(numbers[i]))
            {
                const std::string old = Block(text, WarriorBlock(numbers[i]));
                text = ReplaceAll(text, old, SetProfiles(old, profile));
            }
            else
            {
                added += SetProfiles(ReplaceAll(warriorTemplate, std::format("[{}]", WarriorBlock(setup.Template)),
                                                std::format("[{}]", WarriorBlock(numbers[i]))),
                                     profile);
            }
        }

        const std::string afterWarriors = Block(text, WarriorBlock(lastWarrior));
        text.insert(text.find(afterWarriors) + afterWarriors.size(), added);
        text = SetValue(text, "NumWarriors", std::format("{} ", last));

        // The mechs: an own part that is no mech (a mine layer) made one as the added are, copies of the template,
        // each with its own pilot and place.
        const std::string partTemplate = Block(text, PartBlock(setup.Template));
        const std::string first = Block(text, PartBlock(numbers.front()));
        const double x0 = setup.Start.has_value() ? (*setup.Start)[0] : ReadNumber(first, "PositionX");
        const double y0 = setup.Start.has_value() ? (*setup.Start)[1] : ReadNumber(first, "PositionY");
        std::string parts;

        for (size_t slot = 0; slot < numbers.size(); slot++)
        {
            const int32_t number = numbers[slot];
            const bool mech = IsOwn(number) && Block(text, PartBlock(number)).contains(MechControl);
            std::string part = mech ? Block(text, PartBlock(number))
                                    : ReplaceAll(partTemplate, std::format("[{}]", PartBlock(setup.Template)),
                                                 std::format("[{}]", PartBlock(number)));

            if (!mech)
            {
                part = SetValue(part, "Pilot", std::format("{} ", number));
            }

            if (setup.Start.has_value() || !mech)
            {
                part = SetPlace(part, {x0 + static_cast<double>(slot / 2) * RankStep,
                                       y0 + static_cast<double>(slot % 2) * FileStep});
            }

            if (setup.Start.has_value())
            {
                part = SetValue(part, "Rotation", std::format(" {} ", Whole((*setup.Start)[2])));
            }

            if (IsOwn(number))
            {
                text = ReplaceAll(text, Block(text, PartBlock(number)), part);
            }
            else
            {
                parts += part;
            }
        }

        const std::string afterParts = Block(text, PartBlock(lastPart));
        text.insert(text.find(afterParts) + afterParts.size(), parts);
        text = SetValue(text, "NumParts", std::format("{} ", last));

        // Each part its pilot's mech.
        for (size_t i = 0; i < numbers.size(); i++)
        {
            const std::string old = Block(text, PartBlock(numbers[i]));
            text = ReplaceAll(text, old, SetMech(old, Pilots[i].ObjectNumber, Pilots[i].ObjectProfile, Pilots[i].Name));
        }

        // The other side's mechs swapped (mech for mech), and moved if given a place.
        for (const MCSwap& swap : setup.Enemies)
        {
            const std::string old = Block(text, PartBlock(swap.Part));

            if (!old.contains(MechControl))
            {
                throw MCMissing{std::format("part {} is no mech", swap.Part)};
            }

            const std::string swapped = SetMech(old, swap.Mech.ObjectNumber, swap.Mech.Profile, swap.Mech.Name);
            text = ReplaceAll(text, old, swap.Place.has_value() ? SetPlace(swapped, *swap.Place) : swapped);
        }

        for (const MCMove& move : setup.Moves)
        {
            const std::string old = Block(text, PartBlock(move.Part));
            text = ReplaceAll(text, old, SetPlace(old, move.Place));
        }

        // One group of ten (twelve to a group); the company's other groups gone.
        std::vector<int32_t> mates = numbers;
        mates.resize(std::max(GroupSize, mates.size()), 0);
        std::string matesText;

        for (const int32_t mate : mates)
        {
            matesText += std::format("{}{}", matesText.empty() ? "" : ", ", mate);
        }

        const std::string group = Block(text, "Commander0Group:0");
        text = ReplaceAll(text, group, SetValue(group, "Mates", matesText));

        for (int32_t extra = 1; HasBlock(text, std::format("Commander0Group:{}", extra)); extra++)
        {
            text = ReplaceAll(text, Block(text, std::format("Commander0Group:{}", extra)), "");
        }

        return text;
    }
}

bool MCTrailerMission::Knows(std::string_view mission)
{
    return SetupOf(mission).has_value();
}

std::string MCTrailerMission::FilePath(std::string_view mission)
{
    return std::format("data\\missions\\{}.fit", mission);
}

std::expected<std::string, std::string> MCTrailerMission::Make(std::string_view mission, std::string_view original)
{
    const std::optional<MCSetup> setup = SetupOf(mission);

    if (!setup.has_value())
    {
        return std::unexpected(std::format("the trailer has no force for mission {}", mission));
    }

    try
    {
        return Edit(std::string(original), *setup);
    }
    catch (const MCMissing& missing)
    {
        return std::unexpected(std::format("the trailer's mission {}: {}", mission, missing.What));
    }
}

std::expected<void, std::string> MCTrailerMission::Put(std::string_view mission)
{
    if (!Knows(mission))
    {
        return std::unexpected(std::format("the trailer has no force for mission {}", mission));
    }

    const std::string path = FilePath(mission);
    MCFileSystem::PutImage(
        path,
        [path, name = std::string(mission)]() -> std::optional<std::vector<uint8_t>>
        {
            const std::optional<std::vector<uint8_t>> original = MCGameContext::Current().FastFiles().Read(path);

            if (!original.has_value())
            {
                SDL_Log("The trailer's mission: %s isn't in the game's FastFiles", path.c_str());
                return std::nullopt;
            }

            const std::expected<std::string, std::string> made =
                Make(name, std::string_view(reinterpret_cast<const char*>(original->data()), original->size()));

            if (!made.has_value())
            {
                SDL_Log("%s; the mission's own is played", made.error().c_str());
                return std::nullopt;
            }

            return std::vector<uint8_t>(made->begin(), made->end());
        });
    return {};
}

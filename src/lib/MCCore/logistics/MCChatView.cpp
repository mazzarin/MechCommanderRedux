#include "stdafx.h"
#include "logistics/MCChatView.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCLogChat.h"
#include "logistics/MCRoomModel.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "network/MCMcoSeats.h"
#include "network/MCMultiPlayer.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlModel.h"

namespace
{
    /// <summary>The players' colours by their number (the look's names; the chat's colours, gamesys.fit's order).</summary>
    constexpr std::array<std::string_view, MCRoomModel::MaxPlayers> PlayerColours = {"orange", "red",   "cream",
                                                                                     "cyan",   "green", "white"};
    /// <summary>A line of the chat's that no player said (a join, a leave, the code): grey.</summary>
    constexpr std::string_view GameColour = "grey";
    /// <summary>The chat's colour codes (SMUTI's): "%fc" and a digit.</summary>
    constexpr std::string_view ColourCode = "%fc";
    /// <summary>Between a player's name and their text in a line (MCLogChat::ProcessChatString's).</summary>
    constexpr std::string_view NameEnd = ": ";
    /// <summary>The text colour of a line to the sender's team (MCLogChatInput's).</summary>
    constexpr char TeamTextColour = '4';
    /// <summary>As the 1998 chat box sends it (MCLogChatInput): to all in colour 6, to the team in 4.</summary>
    constexpr int32_t AllColour = 6;
    constexpr int32_t TeamColour = 4;

    /// <summary>The characters the 1998 chat box takes (MCLogChatInput): printable ASCII, and 0xbf-0xfd.</summary>
    constexpr Uint32 FirstPrintable = 0x20;
    constexpr Uint32 LastPrintable = 0x7e;
    constexpr Uint32 FirstAccented = 0xbf;
    constexpr Uint32 LastAccented = 0xfd;
    /// <summary>The chat formatter's code character, which the 1998 box never took.</summary>
    constexpr Uint32 ChatCode = '%';
    constexpr unsigned char FirstNonAscii = 0x80;
    /// <summary>The most bytes one code point takes in UTF-8, with room for SDL_UCS4ToUTF8's.</summary>
    constexpr size_t Utf8Most = 4;

    /// <summary>Text with the chat's colour codes taken out.</summary>
    std::string Uncoded(std::string_view text)
    {
        std::string plain;
        size_t at = 0;

        while (at < text.size())
        {
            if (text.substr(at).starts_with(ColourCode) && at + ColourCode.size() < text.size())
            {
                at += ColourCode.size() + 1;
                continue;
            }

            plain.push_back(text[at++]);
        }

        return plain;
    }

    /// <summary>A sentence of the game's own, by its string's id and the buffer its sender cut it to.</summary>
    struct MCGameSentence
    {
        uint32_t Id;
        int Length;
    };

    /// <summary>
    /// The game's own sentences a game sends as its player's chat line (1998's, said under that player's name): DEPLOY's
    /// "%s has entered the mission." (MCDeploy), the host's "%s has left the game." and a new server's line
    /// (MCMultiPlayer::PlayerLeftGame, SetServer).
    /// </summary>
    constexpr std::array<MCGameSentence, 3> GameSentences = {MCGameSentence{.Id = 0x379, .Length = 199},
                                                             MCGameSentence{.Id = 0x382, .Length = 0xfe},
                                                             MCGameSentence{.Id = 0x378, .Length = 0xfe}};
    /// <summary>Where a game sentence takes a name.</summary>
    constexpr std::string_view NameMark = "%s";

    /// <summary>
    /// Whether <paramref name="text"/> (the chat's bytes) is one of <see cref="GameSentences"/>, with any name where it
    /// takes one: what every build and the 1998 game send, so read as the game's line whoever's game sent it.
    /// </summary>
    bool IsGameSentence(std::string_view text)
    {
        return std::ranges::any_of(
            GameSentences,
            [text](const MCGameSentence& game)
            {
                const std::string sentence = LoadGameString(game.Id, game.Length);
                const size_t mark = sentence.find(NameMark);

                if (sentence.empty() || mark == sentence.npos)
                {
                    return !sentence.empty() && text == sentence;
                }

                const std::string_view before = std::string_view(sentence).substr(0, mark);
                const std::string_view after = std::string_view(sentence).substr(mark + NameMark.size());
                return text.size() > before.size() + after.size() && text.starts_with(before) && text.ends_with(after);
            });
    }

    /// <summary>The chat's unseen mark (MCChatView::Unread).</summary>
    bool UnreadLines = false;

    /// <summary>The player number whose chat colour is the code's digit <paramref name="digit"/>; none for another.</summary>
    std::optional<int32_t> NumberOfColour(char digit)
    {
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();

        if (logistics == nullptr)
        {
            return std::nullopt;
        }

        const auto found = std::ranges::find(logistics->PlayerColors, digit - '0');
        return found != logistics->PlayerColors.end()
                   ? std::optional(static_cast<int32_t>(found - logistics->PlayerColors.begin()))
                   : std::nullopt;
    }
}

void MCChatView::Bind(MCRmlModel& model, std::string name)
{
    _Name = std::move(name);
    model.List(_Name, _Lines, std::pair{"game", &MCLine::Game}, std::pair{"name", &MCLine::Name},
               std::pair{"colour", &MCLine::Colour}, std::pair{"text", &MCLine::Text},
               std::pair{"team", &MCLine::Team});
}

void MCChatView::Show(MCRmlModel& model)
{
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    const std::shared_ptr<MCLogChat> chat = logistics != nullptr ? logistics->Chat : nullptr;

    if (chat == nullptr || chat->LinesAdded == _Seen)
    {
        return;
    }

    _Seen = chat->LinesAdded;
    std::vector<MCLine> lines = Read(*chat);

    if (lines == _Lines)
    {
        return;
    }

    _Lines = std::move(lines);
    model.Changed(_Name);
}

void MCChatView::Forget()
{
    _Seen = 0;
}

std::vector<MCChatView::MCLine> MCChatView::Read(const MCLogChat& chat)
{
    std::vector<MCLine> lines;
    const std::string nameEnd = std::format("{}{}", NameEnd, ColourCode);

    // A player's line is "%fc<their colour>name: %fc<text colour>text" (MCLogChat::ProcessChatString); the rest are the
    // game's own (who joined, who left, the code), as are the game's sentences sent as a player's line. Names, text and
    // the game's lines all in UTF-8: a name can hold the chat's accented bytes too (an MCO player's).
    for (const MCLogChat::HistoryLine& history : chat.Lines)
    {
        const std::string_view text = history.Text;
        const size_t split = text.find(nameEnd);
        const bool said = text.starts_with(ColourCode) && text.size() > ColourCode.size() && split != text.npos &&
                          split + nameEnd.size() < text.size();
        const std::optional<int32_t> number = said ? NumberOfColour(text[ColourCode.size()]) : std::nullopt;

        if (!said || !number.has_value())
        {
            lines.push_back(MCLine{.Game = true,
                                   .Colour = std::string(GameColour),
                                   .Text = MCRmlDialogs::AsSentence(AsUtf8(Uncoded(text)))});
            continue;
        }

        const size_t textColourAt = split + nameEnd.size();
        const std::string saidText = Uncoded(text.substr(textColourAt + 1));

        // The game's own sentence sent as its player's line ("FALCON: Falcon has entered the mission."): the game's.
        if (IsGameSentence(saidText))
        {
            lines.push_back(MCLine{
                .Game = true, .Colour = std::string(GameColour), .Text = MCRmlDialogs::AsSentence(AsUtf8(saidText))});
            continue;
        }

        lines.push_back(
            MCLine{.Game = false,
                   .Name = AsUtf8(Uncoded(text.substr(ColourCode.size() + 1, split - ColourCode.size() - 1))),
                   .Colour = ColourOf(*number),
                   .Text = AsUtf8(saidText),
                   .Team = text[textColourAt] == TeamTextColour});
    }

    return lines;
}

std::string MCChatView::ColourOf(int32_t playerNumber)
{
    return playerNumber >= 0 && static_cast<size_t>(playerNumber) < PlayerColours.size()
               ? std::string(PlayerColours[static_cast<size_t>(playerNumber)])
               : std::string(GameColour);
}

void MCChatView::Say(std::string_view text, bool toTeam)
{
    const std::shared_ptr<MCMultiPlayer> game = MultiPlayer();
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    const std::shared_ptr<MCLogChat> chat = logistics != nullptr ? logistics->Chat : nullptr;
    const std::string line = AsChatBytes(text);

    if (game == nullptr || chat == nullptr || line.empty() || game->SessionManager == nullptr ||
        game->SessionManager->MyPlayer() == nullptr)
    {
        return;
    }

    game->SendChat(toTeam ? game->HomeTeamGroupID : 0, line);
    chat->ProcessChatString(game->SessionManager->MyPlayer()->Id, line, toTeam ? TeamColour : AllColour);

    // Port-only (Redux HD; SPEC-mco-hosting.md's "The chat"): a line to all said in the internet game's MCO room too,
    // from this game's own seat there, as the session got it; a team's stays in the session.
    if (!toTeam)
    {
        MCMcoSeats::Said(AsUtf8(line));
    }
}

std::string MCChatView::AsChatBytes(std::string_view typed)
{
    std::string bytes;
    const char* at = typed.data();
    size_t left = typed.size();

    while (left > 0 && bytes.size() < MCLogChat::MaxLength)
    {
        const Uint32 point = SDL_StepUTF8(&at, &left);

        if (point == 0)
        {
            break;
        }

        if ((point >= FirstPrintable && point <= LastPrintable && point != ChatCode) ||
            (point >= FirstAccented && point <= LastAccented))
        {
            bytes.push_back(static_cast<char>(point));
        }
    }

    return bytes;
}

std::string MCChatView::AsUtf8(std::string_view bytes)
{
    std::string text;

    for (const char c : bytes)
    {
        const auto byte = static_cast<unsigned char>(c);

        if (byte < FirstNonAscii)
        {
            text.push_back(c);
            continue;
        }

        std::array<char, Utf8Most> utf8{};
        const char* end = SDL_UCS4ToUTF8(byte, utf8.data());
        text.append(utf8.data(), static_cast<size_t>(end - utf8.data()));
    }

    return text;
}

bool MCChatView::Unread()
{
    return UnreadLines;
}

void MCChatView::SetUnread(bool unread)
{
    UnreadLines = unread;
}

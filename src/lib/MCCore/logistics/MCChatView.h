#pragma once

// Port-only (SPEC-menus.md's stage 4, the room's chat; stage 8's briefing views it too): the chat (logistics/MCLogChat)
// as the remade screens show it, and what they say to it. Each line with its sayer's name in their colour (the chat's
// SMUTI colour codes read), the game's own lines (a join, a leave, the code) grey, a line to a team marked; what's typed
// sent as the 1998 chat box (MCLogChatInput) sent it.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class MCLogChat;
class MCRmlModel;

/// <summary>The chat's lines in a remade screen's model, and its say.</summary>
class MCChatView
{
public:
    /// <summary>A line as a remade screen binds it.</summary>
    struct MCLine
    {
        /// <summary>The game's own line, no player's.</summary>
        bool Game = false;
        std::string Name;
        /// <summary>The name's colour class (the look's, <see cref="ColourOf"/>); grey for the game's lines.</summary>
        std::string Colour;
        std::string Text;
        /// <summary>Said to the sayer's team.</summary>
        bool Team = false;

        bool operator==(const MCLine&) const = default;
    };

    /// <summary>The lines put in <paramref name="model"/> as the list <paramref name="name"/> (game, name, colour, text, team).</summary>
    void Bind(MCRmlModel& model, std::string name);

    /// <summary>The chat's lines read again when it has new ones, <paramref name="model"/> told when they changed.</summary>
    void Show(MCRmlModel& model);

    /// <summary>The lines read afresh at the next <see cref="Show"/> (the screen shown again).</summary>
    void Forget();

    const std::vector<MCLine>& Lines() const { return _Lines; }

    /// <summary>The chat's lines as a remade screen shows them, oldest first.</summary>
    static std::vector<MCLine> Read(const MCLogChat& chat);

    /// <summary>A player's name's colour class (the look's), by the player's number as the chat's colours go; grey for none.</summary>
    static std::string ColourOf(int32_t playerNumber);

    /// <summary>
    /// What's typed (<paramref name="text"/>, UTF-8) said to all or to the player's team, as the 1998 chat box said it:
    /// sent to the room, and put in the chat. Nothing without a game, or when nothing typed is the box's.
    /// </summary>
    static void Say(std::string_view text, bool toTeam);

    /// <summary>
    /// What's typed (UTF-8) as the 1998 chat box sends it: its characters (printable ASCII, and 0xbf-0xfd: the code
    /// points they are) one byte each, '%' (the chat formatter's code) and anything else left out, at most the chat's
    /// MaxLength.
    /// </summary>
    static std::string AsChatBytes(std::string_view typed);

    /// <summary>A line's bytes as the chat holds them, in UTF-8 for the remade screens (0xbf-0xfd the code points).</summary>
    static std::string AsUtf8(std::string_view bytes);

private:
    std::string _Name;
    std::vector<MCLine> _Lines;
    /// <summary>The chat's count of lines added when last read.</summary>
    size_t _Seen = 0;
};

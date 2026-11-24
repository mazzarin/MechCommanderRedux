#pragma once

#include "logistics/MCLogOptionList.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gui/MCGuiRef.h"

class MCGuiObject;
class MCLogOptionList;
class MCLogPort;
struct MCPane;

// Port-only: the multiplayer session screen's MATCH OPTIONS, a second tab under MULTIPLAYER CHAT, as the briefing has
// two (OPERATION and MISSION BRIEFING). It shows the match's options in the chat's place: the weather and time of day
// every player sees, and the rules the match plays by (MCMatchOptions, MultiPlayer()->Played). The host sets them, and
// what the match plays of every change goes to the guests, who see them locked. An option a player's game doesn't have
// is held at the mission's own (or off) on the host's list, saying whose, while that player is in the session.

/// <summary>The tab's word, lettered in the briefing tabs' own letters.</summary>
inline constexpr std::string_view MatchOptionsWord = "MATCH OPTIONS";

/// <summary>A tab of the session screen's communications panel.</summary>
enum class MCSessionTab : uint8_t
{
    Chat,
    Options,
};

/// <summary>
/// Makes the session screen's tabs as the briefing's multiplayer pictures draw them (lsbdw02 with MULTIPLAYER CHAT in
/// front, lsbdw03 behind), MISSION BRIEFING lettered <see cref="MatchOptionsWord"/>, with <paramref name="front"/>'s tab
/// in front: for MULTIPLAYER CHAT only the tab behind it (the background has the one in front). Their HD copy is the HD
/// art pack's, found by their pixels (the pack draws the words in its font). Null when the art isn't the tabs' as
/// expected.
/// </summary>
std::shared_ptr<MCLogPort> MakeMatchTabs(MCSessionTab front);

/// <summary>
/// Draws <paramref name="tabs"/> (<see cref="MakeMatchTabs"/> of <paramref name="front"/>) over the session screen's
/// background (<paramref name="target"/>), MULTIPLAYER CHAT lit white behind MATCH OPTIONS while
/// <paramref name="chatBlinkOn"/> (new chat, as the briefing's tab blinks).
/// </summary>
void DrawMatchTabs(const MCPane& target, const MCLogPort& tabs, MCSessionTab front, bool chatBlinkOn);

/// <summary>The tab at the session screen's point (<paramref name="xPos"/>, <paramref name="yPos"/>), if any.</summary>
std::optional<MCSessionTab> MatchTabAt(int32_t xPos, int32_t yPos);

/// <summary>
/// Adds the MATCH OPTIONS list to <paramref name="screen"/> where the session screen's chat window is (hidden), under
/// PLAY: WEATHER (its play too, mission/MCWeatherPlay.h), TIME OF DAY (its sight too) and BRAKING.
/// </summary>
MCGuiHandle<MCLogOptionList> AddMatchOptionsList(MCGuiObject& screen);

/// <summary>
/// Shows the match's options in <paramref name="list"/>: what the match plays now (MultiPlayer()->Played), the list
/// locked for a guest; each option a player's game doesn't have held, naming whose (on every player's screen: each
/// game tells the others its rules, MCMultiPlayer::SendPlayerRules). Called before each draw, so a guest's list follows
/// the host's messages.
/// </summary>
void ShowMatchOptions(MCLogOptionList& list);

/// <summary>
/// Port-only (SPEC-menus.md's stage 4): the MATCH OPTIONS rows, headings and options in the list's order (PLAY, then
/// WEAPONS' families), each option's value the one <see cref="ReadMatchOptions"/> read, its change the host's (sent as
/// the list's is).
/// </summary>
std::span<const MCLogOptionList::Row> MatchOptionRows();

/// <summary>The rows' values (<see cref="MatchOptionRows"/>) read from what the match plays now (MultiPlayer()->Played).</summary>
void ReadMatchOptions();

/// <summary>
/// Why the host can't change row <paramref name="row"/> of <see cref="MatchOptionRows"/> ("TELL BOB TO UPGRADE."), a
/// weapon family's with its heading's; empty when it can.
/// </summary>
std::string MatchOptionHeld(int32_t row);

/// <summary>
/// Port-only (SPEC-menus.md's stage 4): the other players whose game holds row <paramref name="row"/> (MatchOptionHeld),
/// their names as typed; none when it isn't held, or out of a session.
/// </summary>
std::vector<std::string> MatchOptionHeldBy(int32_t row);

/// <summary>
/// Why an option is held, for the other players (<paramref name="names"/>, as shown) whose game doesn't have it, every
/// one named: "TELL BOB, ANN AND SALLY TO UPGRADE." (the list wraps it over as many lines as it needs,
/// MCLogOptionList::SetHeld). Empty for no players.
/// </summary>
std::string MatchTellToUpgrade(std::span<const std::string> names);

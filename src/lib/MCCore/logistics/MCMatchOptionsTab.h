#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

class MCLogOptionList;
class MCLogPort;
struct MCPane;

// Port-only: the multiplayer session screen's MATCH OPTIONS, a second tab under MULTIPLAYER CHAT, as the briefing has
// two (OPERATION and MISSION BRIEFING). It shows the match's options in the chat's place: the weather and time of day
// every player sees, and the rules the match plays by (MCMatchOptions, MultiPlayer()->MatchOptions). The host sets
// them, and every change goes to the guests, who see them locked.

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
/// in front: for MULTIPLAYER CHAT only the tab behind it (the background has the one in front). Null when the art isn't
/// the tabs' as expected.
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
/// Makes the MATCH OPTIONS list where the session screen's chat window is (hidden): WEATHER and TIME OF DAY under LOOK,
/// BRAKING under PLAY.
/// </summary>
std::shared_ptr<MCLogOptionList> MakeMatchOptionsList();

/// <summary>
/// Shows the match's options in <paramref name="list"/>: what MultiPlayer()->MatchOptions says now, the list locked
/// for a guest. Called before each draw, so a guest's list follows the host's messages.
/// </summary>
void ShowMatchOptions(MCLogOptionList& list);

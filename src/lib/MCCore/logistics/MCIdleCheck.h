#pragma once

// Port (Redux HD): players away from an internet game (SPEC-in-game-lobby.md's "Who counts"). In an internet game's room
// (the session screen, before its match), 10 minutes without the player's input asks "ARE YOU STILL THERE?", with a minute to
// answer: any input answers it, and none has the player leave the room as its EXIT does (a host's leaving ends the
// game). The INTERNET screen closes its socket to the list after 10 minutes without input, until the next
// (logistics/MCInternetMenu.h). None in battle.

namespace MCIdleCheck
{
    /// <summary>How long without the player's input before the room asks, or the INTERNET screen lets its list go.</summary>
    inline constexpr std::chrono::minutes AwayTime{10};
    /// <summary>How long the room's "ARE YOU STILL THERE?" waits for an answer.</summary>
    inline constexpr std::chrono::minutes AnswerTime{1};

    /// <summary>A clock the waits go by.</summary>
    using MCClock = std::function<std::chrono::steady_clock::time_point()>;

    /// <summary>The player's input (gui/MCGuiInput's DispatchGuiEvent): a click, a key, the mouse moving.</summary>
    void Input();

    /// <summary>Whether the player has given no input for <see cref="AwayTime"/>.</summary>
    bool Away();

    /// <summary>Whether the room is asking "ARE YOU STILL THERE?".</summary>
    bool Asking();

    /// <summary>Each frame in the menus: the room asks, an answer closes the question, and no answer leaves the room.</summary>
    void Frame();

    /// <summary>Tests' hook: the waits go by <paramref name="clock"/> (none: the steady clock), from its now.</summary>
    void SetClock(MCClock clock);
}

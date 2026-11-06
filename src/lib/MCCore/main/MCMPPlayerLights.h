#pragma once

// Original source: mcx\logistics.cpp (MPPlayerLights).

#include "gui/MCGuiRef.h"

class MCMPPlayerLightsView;

/// <summary>
/// The row of lights on the multiplayer screens, one per player, showing whether each is ready: the players, their
/// status and the blink. Each screen that shows the lights has its own view of them (<see cref="MCMPPlayerLightsView"/>),
/// all made with the lights.
/// </summary>
/// <remarks>
/// Original source: <c>logistics.cpp</c>, 0x50c bytes. The original's lights were one GUI object that the logistics
/// screens moved from screen to screen; their state is this model, owned by the logistics screen
/// (<c>MCLogistics::PlayerLights</c>).
/// </remarks>
class MCMPPlayerLights
{
public:
    /// <summary>The most players a session has (a game rule: the drop zones are shared out among six).</summary>
    static constexpr int32_t MaxPlayers = 6;

    /// <summary>Sets how many lights there are (and so the views' width).</summary>
    void SetNumPlayers(int32_t count);

    /// <summary>Sets the player shown by light <paramref name="light"/>.</summary>
    void SetPlayerID(int32_t light, uint32_t playerID);

    /// <summary>Sets a player's status (0..2); status 2 starts the blink timer.</summary>
    void SetPlayerStatus(uint32_t playerID, int32_t status);

    /// <summary>The view showing the lights (on the screen they are on), or null when none does.</summary>
    std::shared_ptr<MCMPPlayerLightsView> ShownView() const;

    int32_t NumPlayers = 0;
    std::array<uint32_t, MaxPlayers> PlayerIDs{};
    std::array<int32_t, MaxPlayers> PlayerStatus{};
    /// <summary>Set while the blink timer runs.</summary>
    bool TimerRunning = false;
    /// <summary>The blink phase.</summary>
    bool BlinkOn = false;
    /// <summary>The lights' views, one on each screen that can show them (children of the screens).</summary>
    std::vector<MCGuiHandle<MCMPPlayerLightsView>> Views;
};

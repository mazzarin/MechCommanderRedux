#pragma once

// Original source: mcx\logistics.cpp (MPPlayerLights).

/// <summary>
/// The multiplayer ready lights, one per player, showing whether each is ready: the players and their status, which the
/// remade room, briefing and loading screens show.
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

    /// <summary>Sets how many lights there are.</summary>
    void SetNumPlayers(int32_t count);

    /// <summary>Sets the player shown by light <paramref name="light"/>.</summary>
    void SetPlayerID(int32_t light, uint32_t playerID);

    /// <summary>Sets a player's status (0..2).</summary>
    void SetPlayerStatus(uint32_t playerID, int32_t status);

    int32_t NumPlayers = 0;
    std::array<uint32_t, MaxPlayers> PlayerIDs{};
    std::array<int32_t, MaxPlayers> PlayerStatus{};
};

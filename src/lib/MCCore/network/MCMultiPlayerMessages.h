#pragma once

// Original source: mcx\network\multplyr.cpp: the game's message types and the fixed layouts its send functions build
// in MultiPlayer's message buffer. The struct names are the port's (the original wrote through raw pointers). The
// update messages (mover, turret, weapon fire, hits, world state) are arrays of packed chunks and are built in place.

#include "linkup/MCLinkupMessages.h"

/// <summary>
/// The game's message types (bits 0-9 of the linkup header), handled by MultiPlayerApplicationCallback. 6, 9 and 12
/// are the linkup layer's, passed on.
/// </summary>
/// <remarks>The names are the port's (after the handlers); the values are the original's (wire format).</remarks>
enum class MCMPMessageType : uint16_t
{
    NewServer = 6,
    PlayerRemoved = 9,
    Latency = 12,
    Chat = 13,
    PlayerCheckIn = 15,
    PlayerSetup = 16,
    PlayerCheckInReceipt = 17,
    StartPlanning = 19,
    StartScenario = 20,
    EndScenario = 21,
    PlayerOrder = 22,
    PlayerMoverGroup = 23,
    PlayerArtillery = 24,
    MoverUpdate = 25,
    TurretUpdate = 26,
    MoverWeaponFireUpdate = 27,
    TurretWeaponFireUpdate = 28,
    MoverCriticalHitUpdate = 29,
    WeaponHitUpdate = 30,
    WorldStateUpdate = 31,
    DeployForce = 32,
    RemoveForce = 33,
    PlayerUpdate = 34,
    PrepareScenario = 35,
    ReadyForBattle = 36,
    FileInquiry = 37,
    FileReport = 38,
    LoadMission = 39,
    Start = 40,
    JoinTeam = 41,
    SwitchScreen = 42,
    RPUpdate = 43,
    TechbaseChange = 44,
    SessionCheckIn = 45,
    /// <summary>Port: a configured match (<c>MPGameConfiguration</c>): teams, tech bases, points and the mission in one.</summary>
    StartMatch = 46
};

/// <summary>The header word of a guaranteed game message of <paramref name="type"/>.</summary>
constexpr uint16_t GuaranteedHeader(MCMPMessageType type)
{
    return static_cast<uint16_t>(FIMSG_GUARANTEED | static_cast<uint16_t>(type));
}

/// <summary>The header word of a plain game message of <paramref name="type"/>.</summary>
constexpr uint16_t PlainHeader(MCMPMessageType type)
{
    return static_cast<uint16_t>(type);
}

#pragma pack(push, 1)

/// <summary>Chat (guaranteed): a flag, then the zero-terminated text; sent as strlen(text) + 10 bytes.</summary>
/// <remarks>Fixed layout; the text follows it.</remarks>
struct MCMPChatMessage : public MCFIGuaranteedMessageHeader
{
    /// <summary>1 when sent to everyone (receiver 0).</summary>
    uint8_t ToAll = 0;
};

static_assert(sizeof(MCMPChatMessage) == 9);

/// <summary>Player check-in and ready-for-battle (guaranteed): the sender's check-in id and home team.</summary>
/// <remarks>Fixed layout.</remarks>
struct MCMPPlayerCheckInMessage : public MCFIGuaranteedMessageHeader
{
    int8_t CheckInId = 0;
    int8_t HomeTeam = 0;
};

static_assert(sizeof(MCMPPlayerCheckInMessage) == 10);

/// <summary>Player setup (guaranteed): the server's group ids.</summary>
/// <remarks>Fixed layout.</remarks>
struct MCMPPlayerSetupMessage : public MCFIGuaranteedMessageHeader
{
    uint32_t AllPlayerGroupID = 0;
    uint32_t ClanGroupID = 0;
    uint32_t InnerSphereGroupID = 0;
};

static_assert(sizeof(MCMPPlayerSetupMessage) == 0x14);

/// <summary>Check-in receipt (guaranteed, to the server): the check-in id; also end scenario: the result.</summary>
/// <remarks>Fixed layout.</remarks>
struct MCMPLongMessage : public MCFIGuaranteedMessageHeader
{
    int32_t Value = 0;
};

static_assert(sizeof(MCMPLongMessage) == 0xc);

/// <summary>Player order (guaranteed, to the server): a tactical order packed into two words.</summary>
/// <remarks>Fixed layout.</remarks>
struct MCMPPlayerOrderMessage : public MCFIGuaranteedMessageHeader
{
    int8_t CheckInId = 0;
    /// <summary>Bit 0: queued; bit 5: the order came from a group; bits 1-4: 1 &lt;&lt; (group id + 1) per group.</summary>
    uint8_t Flags = 0;
    /// <summary>The order's first way point, for move and jump orders (its x and y as float bits).</summary>
    uint32_t OrderParam1 = 0;
    uint32_t OrderParam2 = 0;
    /// <summary>TacticalOrder::Pack's two words.</summary>
    std::array<uint32_t, 2> PackedOrder{};
};

static_assert(sizeof(MCMPPlayerOrderMessage) == 0x1a);

/// <summary>Player mover group (guaranteed): which movers form a group and its point man.</summary>
/// <remarks>Fixed layout.</remarks>
struct MCMPPlayerMoverGroupMessage : public MCFIGuaranteedMessageHeader
{
    int8_t CheckInId = 0;
    int8_t GroupId = 0;
    /// <summary>(1 &lt;&lt; local index per mover) &lt;&lt; 4 | the point man's local index.</summary>
    uint16_t Members = 0;
};

static_assert(sizeof(MCMPPlayerMoverGroupMessage) == 0xc);

/// <summary>Player artillery (guaranteed): the target's x and y and the packed ArtilleryChunk.</summary>
/// <remarks>Fixed layout.</remarks>
struct MCMPPlayerArtilleryMessage : public MCFIGuaranteedMessageHeader
{
    float TargetX = 0;
    float TargetY = 0;
    uint32_t ArtilleryData = 0;
};

static_assert(sizeof(MCMPPlayerArtilleryMessage) == 0x14);

/// <summary>
/// Start scenario (guaranteed, to everyone): per player a value (the caller's array), per mover a flag byte (bit 0:
/// the pilot escapes by ejecting), then the mission name; sent as strlen(name) + 0x39 bytes.
/// </summary>
/// <remarks>Fixed layout; the name follows it.</remarks>
struct MCMPStartScenarioMessage : public MCFIGuaranteedMessageHeader
{
    std::array<int32_t, MaxLinkupPlayers> PlayerValues{};
    std::array<uint8_t, 24> MoverFlags{};
};

static_assert(sizeof(MCMPStartScenarioMessage) == 0x38);

/// <summary>
/// Load mission, start and file report (guaranteed): a word, then a file name; sent as strlen(name) + 0xd bytes.
/// The word is a file report's checksum and is not written by the others.
/// </summary>
/// <remarks>Fixed layout; the name follows it.</remarks>
struct MCMPFileNameMessage : public MCFIGuaranteedMessageHeader
{
    int32_t Value = 0;
};

static_assert(sizeof(MCMPFileNameMessage) == 0xc);

/// <summary>Join team (guaranteed, from the host): the player, the team (0 = none) and the slot on it.</summary>
/// <remarks>Fixed layout.</remarks>
struct MCMPJoinTeamMessage : public MCFIGuaranteedMessageHeader
{
    uint32_t PlayerID = 0;
    int8_t Team = 0;
    int8_t Slot = 0;
};

static_assert(sizeof(MCMPJoinTeamMessage) == 0xe);

/// <summary>
/// Two longs (guaranteed, from the session screen): switch screen (the screen; the second unused), RP update (the
/// points, then the team) and tech base change (the team, then the tech base).
/// </summary>
/// <remarks>Fixed layout.</remarks>
struct MCMPTwoLongMessage : public MCFIGuaranteedMessageHeader
{
    int32_t Value1 = 0;
    int32_t Value2 = 0;
};

static_assert(sizeof(MCMPTwoLongMessage) == 0x10);

/// <summary>One player of a configured match: the id, the team (1 or 2) and the slot on it.</summary>
struct MCMPMatchPlayer
{
    uint32_t PlayerID = 0;
    int8_t Team = 0;
    int8_t Slot = 0;
};

static_assert(sizeof(MCMPMatchPlayer) == 6);

/// <summary>
/// Start match (guaranteed, from the host): the whole of a configured match. A guest that gets it puts the players on
/// their teams, sets the tech bases and points and goes to the mech bay, without the session screen.
/// </summary>
/// <remarks>Fixed layout; the mission's bare name (zero-terminated) follows it.</remarks>
struct MCMPStartMatchMessage : public MCFIGuaranteedMessageHeader
{
    static constexpr size_t MaxMatchPlayers = 6;

    /// <summary>Each team's resource point pool.</summary>
    int32_t Team1Points = 0;
    int32_t Team2Points = 0;
    int8_t Team1TechBase = 1;
    int8_t Team2TechBase = -1;
    uint8_t NumPlayers = 0;
    std::array<MCMPMatchPlayer, MaxMatchPlayers> Players{};
};

static_assert(sizeof(MCMPStartMatchMessage) == 0x37);

#pragma pack(pop)

/// <summary>A zero-terminated text at <paramref name="offset"/> of a message, read no further than its end.</summary>
inline std::string_view MessageText(std::span<const uint8_t> bytes, size_t offset)
{
    if (bytes.size() <= offset)
    {
        return {};
    }

    const std::string_view rest(reinterpret_cast<const char*>(bytes.data()) + offset, bytes.size() - offset);
    return rest.substr(0, rest.find('\0'));
}

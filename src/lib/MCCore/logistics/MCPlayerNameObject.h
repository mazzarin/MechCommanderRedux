#pragma once

#include "logistics/MCLogObject.h"

class MCGuiEvent;
class MCGuiFont;

/// <summary>
/// A player's name on the multiplayer session screen, which the host can drag between the unassigned list and
/// the two teams.
/// </summary>
/// <remarks>Original source: <c>logistics\logsession.cpp</c> (<c>PlayerNameObject</c>).</remarks>
class MCPlayerNameObject : public MCLogObject
{
public:
    /// <summary>A player id that stands for no player.</summary>
    static constexpr uint32_t NoPlayer = 0xffffffff;

    /// <summary>
    /// Whether the player has the loaded mission's file (<c>MCSessionScreen::LoadMission</c>, <c>FileReport</c>).
    /// </summary>
    enum class MCFileStatus : int8_t
    {
        /// <summary>No inquiry out.</summary>
        NotAsked = -1,
        /// <summary>Waiting for the answer.</summary>
        Waiting = 0,
        Missing = 1,
        Present = 2,
        /// <summary>Port: the file is there, but the player's game data differ from the host's (as good as missing).</summary>
        Differs = 3,
        /// <summary>Port: the file is there; the player's build says nothing of its game data (it plays).</summary>
        Unchecked = 4,
    };

    ~MCPlayerNameObject() override { Destroy(); }

    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;

    void Destroy() override;

    /// <summary>
    /// Fills the box and writes the name (not while it is being dragged). Port: drawn each frame, over the player
    /// number the session screen put on the left (<see cref="NumberArt"/>).
    /// </summary>
    void Draw() override;

    /// <summary>Port: the name draws itself each frame (its port is a view).</summary>
    bool DrawsLive() override { return true; }

    /// <summary>Dragging: grabbed on a press (when <see cref="Draggable"/>), follows the mouse, dropped on release.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    void SetPlayerName(std::string_view name);

    /// <summary>Sets the player and takes the name from the session.</summary>
    void SetPlayerId(uint32_t playerId);

    void SetFont(std::shared_ptr<MCGuiFont> newFont);

    MCFileStatus FileStatus = MCFileStatus::NotAsked;
    std::string PlayerName;
    std::shared_ptr<MCGuiFont> Font;
    /// <summary>The player's network id, or <see cref="NoPlayer"/>.</summary>
    uint32_t PlayerId = NoPlayer;
    /// <summary>The name can be dragged (the host's screen).</summary>
    bool Draggable = false;

    /// <summary>
    /// Port: what <c>MCSessionScreen::Init</c> painted into the name's picture: a wipe to <see cref="NumberBack"/> and
    /// the player number picture (<c>ses_p&lt;n&gt;</c>) at (1, 1); null before. The logistics art cache owns it.
    /// </summary>
    std::shared_ptr<MCLogPort> NumberArt;
    int32_t NumberBack = 0xff;
};

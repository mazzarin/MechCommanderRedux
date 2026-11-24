#pragma once

#include "logistics/MCLogObject.h"
#include "logistics/MCRoomModel.h"

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

    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;

    /// <summary>Frees the name.</summary>
    void OnDestroy() override;

    /// <summary>
    /// Fills the box and writes the name (not while it is being dragged). Port: drawn each frame, over the player
    /// number the session screen put on the left (<see cref="NumberArt"/>).
    /// </summary>
    void Draw() override;

    /// <summary>Port: the name draws itself each frame (its port is a view).</summary>
    bool DrawsLive() override { return true; }

    /// <summary>Dragging: grabbed on a press (when <see cref="Draggable"/>), follows the mouse, dropped on release.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// Sets the player and takes the name from the session. Port-only (SPEC-menus.md's stage 4): its seat's in the room
    /// model (<see cref="MCRoomModel::Seat"/>), which the name shows.
    /// </summary>
    void SetPlayerId(uint32_t playerId);

    /// <summary>The player the name shows (its seat's), or <see cref="NoPlayer"/>.</summary>
    uint32_t PlayerId() const;
    /// <summary>The name shown (its seat's).</summary>
    std::string PlayerName() const;

    void SetFont(std::shared_ptr<MCGuiFont> newFont);

    std::shared_ptr<MCGuiFont> Font;
    /// <summary>Port-only (SPEC-menus.md's stage 4): the room model's seat the name shows (its slot on the screen).</summary>
    size_t SeatIndex = 0;
    /// <summary>The name can be dragged (the host's screen).</summary>
    bool Draggable = false;

    /// <summary>
    /// Port: what <c>MCSessionScreen::Init</c> painted into the name's picture: a wipe to <see cref="NumberBack"/> and
    /// the player number picture (<c>ses_p&lt;n&gt;</c>) at (1, 1); null before. The logistics art cache owns it.
    /// </summary>
    std::shared_ptr<MCLogPort> NumberArt;
    int32_t NumberBack = 0xff;
};

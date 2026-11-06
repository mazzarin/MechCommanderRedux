#pragma once

#include "iface/MCInterfaceTypes.h"
#include "iface/MCMechIcon.h"

/// <summary>
/// One of the player's movers on the mech bar: its damage diagram, pilot portrait, pilot health and weapon cooldown
/// bars, and its lance colour. Clicking selects the mover (see <see cref="MechIconHandleEvent"/>).
/// </summary>
/// <remarks>Original source: <c>iface\iface.cpp</c> (<c>FriendlyMechIcon</c>).</remarks>
class MCFriendlyMechIcon : public MCMechIcon
{
public:
    ~MCFriendlyMechIcon() override = default;

    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view bitmapName) override;
    void OnDestroy() override;
    /// <summary>Shows the pilot/mech tag, highlights the mover and sets the cursor for the current mode.</summary>
    void Enter() override;
    /// <summary>Draws the icon into its own port (<see cref="DrawIcon"/>).</summary>
    void Draw() override;
    /// <summary>Only while the mover is active.</summary>
    void Display() override;
    using MCGuiObject::DrawBox;
    /// <summary>Draws the selection frame, unless the mech bar is shuffling its buttons.</summary>
    void DrawBox(uint8_t color, int32_t left, int32_t top, int32_t right, int32_t bottom) override;

    /// <summary>
    /// Attaches the icon to the mover with part id <paramref name="partId"/>: loads its damage shapes
    /// (<c>mi%02i.shp</c> for mechs, <c>vi%i.shp</c> for vehicles) and its pilot portrait.
    /// </summary>
    void SetID(int32_t partId);

    /// <summary>Port: the part colours, then the portrait switched to the wounded or dead image (once each).</summary>
    void UpdateModel() override;
    /// <summary>
    /// Port: the background (<see cref="IconBackground"/>), weapon bar, damage diagram, lance colour strip, then the
    /// pilot (or the mover's name).
    /// </summary>
    void DrawIcon(MCGuiPort& target) override;

    /// <summary>Whether the mover is in play (the icon is shown and counts in its lance).</summary>
    bool Active = false;
    /// <summary>The lance (0-3) the mover belongs to, or <see cref="NoLance"/>; indexes <see cref="LanceColors"/>.</summary>
    int32_t Lance = NoLance;
    /// <summary>The x the icon moves to when the mech bar shuffles its buttons.</summary>
    int32_t TargetX = 0;
    /// <summary>The x step per frame of that shuffle.</summary>
    int32_t ShuffleStep = 0;
    /// <summary>Whether the mover is its lance's point (leader).</summary>
    bool IsPoint = false;
    /// <summary>The pilot portrait.</summary>
    std::unique_ptr<MCGuiPort> PilotImage;
    /// <summary>Set once the portrait was switched to the dead pilot image.</summary>
    bool ShowingDeadPilot = false;
    /// <summary>Set once the portrait was switched to the wounded pilot image.</summary>
    bool ShowingWoundedPilot = false;
    /// <summary>
    /// Port: the icon's background (<c>guiub00.tga</c>), which the original loaded into the icon's own picture and
    /// drew over.
    /// </summary>
    std::unique_ptr<MCGuiPort> IconBackground;

private:
    /// <summary>The pilot portrait, health bar and name, into <paramref name="target"/>.</summary>
    void DrawPilot(MCGuiPort& target);
    /// <summary>The weapon recycle bar, into <paramref name="target"/>.</summary>
    void DrawWeapon(MCGuiPort& target);
};

/// <summary>
/// The event routine of each mech bar icon: a click selects the mover (shift toggles it), or gives the selection the
/// current mode's order on the mover; a double click points the camera at it.
/// </summary>
void MechIconHandleEvent(MCGuiObject& icon, MCGuiEvent& event);

#pragma once

#include "gui/MCGuiOwned.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCGuiPort.h"
#include "platform/MCRegisteredBlock.h"

class MCBaseObject;

/// <summary>
/// The damage diagram of one mover in the tactical interface: a small window drawing the mover's damage shape
/// (<c>.shp</c>) with each body part tinted by how damaged it is.
/// </summary>
/// <remarks>Original source: <c>iface\iface.cpp</c> (<c>aMechIcon</c>). Base of <see cref="MCFriendlyMechIcon"/>.</remarks>
class MCMechIcon : public MCGuiObject
{
public:
    /// <summary>The body parts a damage diagram shows (a mech's eight locations; the shape files hold one per part).</summary>
    static constexpr size_t MaxParts = 8;

    ~MCMechIcon() override = default;

    /// <summary>Makes the window and its "destroyed" image; every part starts uncoloured.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view bitmapName) override;
    /// <summary>Frees the "destroyed" image and the damage shapes.</summary>
    void Destroy() override;
    /// <summary>Draws the icon into its own port (<see cref="DrawIcon"/>).</summary>
    void Draw() override;
    /// <summary>Marks this mover as the one the mech bar highlights.</summary>
    void Enter() override;
    /// <summary>Hides the floating tag, clears the mech bar's highlight and resets the cursor.</summary>
    void Leave() override;
    /// <summary>Brings the model up to date and draws, while shown.</summary>
    void Display() override;

    /// <summary>Attaches the icon to the mover (or salvage craft) with part id <paramref name="partId"/>.</summary>
    void SetID(int32_t partId);

    /// <summary>Port: icons draw themselves each frame (see <see cref="DrawIcon"/>).</summary>
    bool DrawsLive() override { return true; }

    /// <summary>
    /// Port: brings what the icon shows up to date with its mover: the part colours (and, for a friendly icon, the
    /// wounded or dead portrait). The original did this as it drew, every 500 ms; it runs every frame now.
    /// </summary>
    virtual void UpdateModel();

    /// <summary>
    /// Port: draws the icon into <paramref name="target"/>: its port's view in the frame pass, or a picture (the
    /// mission results screen copies the icon).
    /// </summary>
    virtual void DrawIcon(MCGuiPort& target);

    /// <summary>The part id of the mover shown.</summary>
    int32_t PartId = 0;
    /// <summary>The mover shown (a Mover, or the salvage craft's object); read it with <c>LockObject</c>.</summary>
    std::weak_ptr<MCBaseObject> Mover;
    /// <summary>
    /// Each body part's colour code: 0x0b undamaged, 0xf2 light, 0xeb heavy, 0xef critical, 0x19 destroyed, 0xff not
    /// yet set.
    /// </summary>
    std::array<uint8_t, MaxParts> PartColor{};
    /// <summary>The number of body parts (the mover's location count).</summary>
    int32_t NumParts = 0;
    /// <summary>Where the damage diagram is drawn in the window.</summary>
    int32_t DiagramX = 0;
    int32_t DiagramY = 0;
    /// <summary>The "destroyed" image, copied over the diagram once the mover is dead.</summary>
    MCGuiOwned<MCGuiPort> DeadImage;
    /// <summary>The damage shapes (one per body part), loaded from <c>artPath</c>.</summary>
    MCRegisteredBlock DamageShapes;
    /// <summary>Port: their file's name, lower case, without its extension (the HD art pack's folder for them).</summary>
    std::string DamageShapeName;

    /// <summary>
    /// Port-only: ties the damage shapes to the HD art pack's frames of them (sprite/MCHdArt.h's icons/) when HD art is
    /// on; done when they load, and again by whoever turns HD art on during a mission.
    /// </summary>
    void AttachHdArt();

protected:
    /// <summary>Keeps the HD art pack's ties to the damage shapes while they are these.</summary>
    std::shared_ptr<const int> _HdArtOwner;

    /// <summary>Draws each body part's shape in its colour (translucent unless undamaged) into <paramref name="target"/>.</summary>
    void DrawParts(MCGuiPort& target);
    /// <summary>Picks each body part's colour from its armour left (green, yellow, orange, red, destroyed).</summary>
    void GetColors();

    /// <summary>aObject::FillBox on any port: wipes the rectangle (port coordinates) of <paramref name="target"/>.</summary>
    static void FillPortBox(MCGuiPort& target, int16_t left, int16_t top, int16_t right, int16_t bottom, uint8_t color);
};

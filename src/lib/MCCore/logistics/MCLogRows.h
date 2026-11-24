#pragma once

#include "logistics/MCLogPort.h"
#include "object/MCMasterComponent.h"

class MCGuiEvent;
class MCGuiFont;
class MCGuiObject;
class MCInventoryList;

/// <summary>
/// The 256-entry colour table greyed-out rows darken through (<c>Logistics::darken</c>): <c>AlphaTable</c> row
/// 0x100.
/// </summary>
MCColorTable LogisticFadetable();

/// <summary>
/// The colours that replace the mech and vehicle diagram shapes' pixels 0xea, 0xe8 and 0xe7, per damage state (armor
/// left: 0 under 76%, 1 under 51%, 2 under 26%, 3 armor and structure gone; above that the shape keeps its own).
/// </summary>
inline constexpr std::array<std::array<uint8_t, 3>, 4> IconFade = {
    {{242, 241, 240}, {245, 244, 243}, {239, 238, 237}, {19, 19, 19}}};

/// <summary>Whether the repair screen is the logistics screen shown.</summary>
bool OnRepairScreen();

/// <summary>Whether the purchase screen is the logistics screen shown.</summary>
bool OnPurchaseScreen();

/// <summary>
/// Shows the one-button message dialog with string <paramref name="stringId"/> and no callback; with
/// <paramref name="okayArt"/>, the button first gets the "okay" pictures and is enabled.
/// </summary>
void ShowLogMessage(uint32_t stringId, bool okayArt = true);

/// <summary>As the other overload, with <paramref name="text"/>.</summary>
void ShowLogMessage(std::string_view text, bool okayArt = true);

/// <summary>Opens the purchase dialog (a sale takes a negative <paramref name="unitCost"/>) on the current screen.</summary>
void OpenPurchaseDialog(int32_t purchaseType, int32_t unitCost, int32_t maxQuantity, std::string_view title,
                        std::string_view subtitle, MCOptionalRef<const MCGuiPort> picture,
                        std::function<void(int32_t result, int32_t quantity)> callback);

/// <summary>The string table index of the weight class of <paramref name="tonnage"/> (light .. assault).</summary>
uint32_t WeightClassString(float tonnage);

/// <summary>The string table index of the armor rating of <paramref name="armorTonnage"/>.</summary>
uint32_t ArmorClassString(float armorTonnage);

/// <summary>The string table index of the structure word for <paramref name="internals"/> internal structure points.</summary>
uint32_t InternalClassString(int32_t internals);

/// <summary>
/// Whether the event is inside a pane, left of its scroll bar (all edges out): the position comes from
/// <paramref name="posPane"/>, the width from <paramref name="widthPane"/> and the height from
/// <paramref name="heightPane"/> (some rows mix the inventory and unit panes, or the two screens').
/// </summary>
bool OverPaneInside(MCGuiObject& posPane, MCGuiObject& widthPane, MCGuiObject& heightPane, MCGuiEvent& event);

/// <summary>Whether the event is inside <paramref name="pane"/>, left of its scroll bar (all edges out).</summary>
bool OverPaneInside(MCGuiObject& pane, MCGuiEvent& event);

/// <summary>Whether (<paramref name="xPos"/>, <paramref name="yPos"/>) is over <paramref name="pane"/>, edges and scroll bar in.</summary>
bool OverPaneArea(MCGuiObject& pane, int32_t xPos, int32_t yPos);

/// <summary>Whether the event is on <paramref name="row"/> (edges in).</summary>
bool OnRow(MCGuiObject& row, MCGuiEvent& event);

/// <summary>Draws <paramref name="text"/> with <paramref name="font"/> at (<paramref name="xPos"/>, <paramref name="yPos"/>) of <paramref name="port"/>.</summary>
void WriteText(MCGuiFont& font, MCLogPort& port, int32_t xPos, int32_t yPos, std::string_view text);

/// <summary>
/// Where a row is put together: a block of <paramref name="port"/> at row <paramref name="top"/>, the size of
/// <paramref name="art"/>, with the art copied in. The original put the row together in a picture and copied it there
/// (keyed on 0xff when <paramref name="keyed"/>); it is drawn in place.
/// </summary>
std::shared_ptr<MCLogBlockPort> RowPicture(MCLogPort& art, MCLogPort& port, int32_t top, bool keyed);

/// <summary>Copies the logistics art <paramref name="name"/> keyed into <paramref name="port"/> at (<paramref name="xPos"/>, <paramref name="yPos"/>).</summary>
void CopyArt(MCLogPort& port, int32_t xPos, int32_t yPos, std::string_view name);

/// <summary>Fills a <paramref name="width"/> x <paramref name="height"/> box of <paramref name="port"/> with <paramref name="color"/>.</summary>
void WipeBox(MCLogPort& port, int32_t xPos, int32_t yPos, int32_t width, int32_t height, int32_t color);

/// <summary>
/// A description in the info box or a row: formatted by the text formatter into a <paramref name="width"/> x
/// <paramref name="height"/> picture drawn keyed at (<paramref name="xPos"/>, <paramref name="yPos"/>) of
/// <paramref name="port"/>; nothing without one.
/// </summary>
void DrawInfoDescription(MCLogPort& port, int32_t width, int32_t height, std::string_view description, int32_t xPos,
                         int32_t yPos);

/// <summary>
/// Readies a description for the info box as the original did when it showed it: its fourth character (a colour
/// code's digit) becomes '9' (a shorter one is left).
/// </summary>
void PrepareInfoDescription(std::string& description);

/// <summary>The form of master component <paramref name="masterID"/>.</summary>
MCComponentForm ComponentForm(uint8_t masterID);

/// <summary>An energy, ballistic or missile weapon.</summary>
bool IsWeapon(MCComponentForm form);

/// <summary>A sensor, ECM or probe.</summary>
bool IsEquipment(MCComponentForm form);

/// <summary>A weapon with its own ammo (ballistic and missile): moving it moves an ammo item too.</summary>
bool UsesAmmo(uint8_t masterID);

/// <summary>
/// A weapon's worth in the condition figures: its damage per 10 seconds, as a short, times its long range over 24, as
/// a short.
/// </summary>
int16_t WeaponWorth(const MCMasterComponent& component);

/// <summary>
/// The shade of a location on the repair screen's diagram from its percentage left: 1 above 75, 2 above 50, 3 above
/// 25, 4 above 0, 0 when gone.
/// </summary>
int32_t RepairShade(uint32_t percent);

/// <summary>The percentage of <paramref name="maximum"/> that <paramref name="current"/> is (0 for a zero maximum).</summary>
uint32_t PercentLeft(uint8_t current, uint8_t maximum);

/// <summary>The colour table of armor shade <paramref name="shade"/> (<see cref="RepairShade"/>).</summary>
MCColorTable ArmorShadeTable(int32_t shade);

/// <summary>The colour table of internal structure shade <paramref name="shade"/>.</summary>
MCColorTable InternalShadeTable(int32_t shade);

/// <summary>
/// Draws the tonnage bar at <paramref name="xPos"/>: a 0x35-pixel frame over rows 9..12 of <paramref name="pane"/>,
/// filled <paramref name="fill"/> pixels.
/// </summary>
void DrawTonnageBar(const MCPane& pane, int32_t xPos, int32_t fill);

/// <summary>
/// Writes a unit's weapons (short, medium, then long range, as "count name") and its sensors, ECM and probes down the
/// right of a store row in the green font.
/// </summary>
/// <returns>The last line written, or none.</returns>
std::optional<std::string> DrawInventoryList(MCInventoryList& inventory, MCLogPort& port);

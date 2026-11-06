#pragma once

#include "logistics/MCLogObject.h"

/// <summary>
/// Port-only: a drop-down list for the logistics screens (the preferences screen's DIFFICULTY and RENDERER). A field
/// shows the current choice; a click opens a list of the choices below it, a click on a row chooses it. The control
/// edits a setting it points at (its model): what it shows is always the setting's value, so a setting changed
/// elsewhere (CANCEL putting the old one back) shows at once. It draws every frame from its state, in the preferences
/// panel's style: a 0x14 outline, white font in 0xe3, the hovered row lit in 0x14.
/// </summary>
/// <remarks>
/// While open the control is taller (the list is part of it), in front of its siblings and holds the mouse grab, so
/// every mouse and key event comes to it until it closes: a press outside closes it without a change. A choice that
/// changes the setting runs the changed routine once, after the grab is let go.
/// Mouse: a click on the field opens or closes; a release over a row chooses it (also at the end of a drag from the
/// field); the wheel moves the choice (closed) or the lit row (open). Keys: up and down do the same, Return chooses the
/// lit row, Escape closes. Keys reach a closed control while the mouse is over it, as for every logistics control.
/// </remarks>
class MCLogComboBox : public MCLogObject
{
public:
    /// <summary>One choice: the text shown and the setting's value for it.</summary>
    struct Item
    {
        /// <summary>The text shown (white font's characters).</summary>
        std::string Label;
        /// <summary>The setting's value for this choice.</summary>
        int32_t Value = 0;
    };

    /// <summary>The field's height, and each row's in the list.</summary>
    static constexpr int32_t FieldHeight = 11;
    static constexpr int32_t RowHeight = 10;
    /// <summary>The rows the list shows at most; longer lists scroll.</summary>
    static constexpr int32_t MaxRows = 8;

    /// <summary>Destroys the object if nothing did (its cleanup lets go of the GUI system's grab).</summary>
    ~MCLogComboBox() override;

    /// <summary>
    /// Places the field at (<paramref name="xPos"/>, <paramref name="yPos"/>), <paramref name="width"/> wide, editing
    /// <paramref name="setting"/> with <paramref name="items"/>; <paramref name="changed"/> (may be empty) runs with the
    /// new value after each choice that changes the setting.
    /// </summary>
    void Init(int32_t xPos, int32_t yPos, int32_t width, int32_t& setting, std::vector<Item> items,
              std::function<void(int32_t value)> changed);

    /// <summary>Closes the list (letting go of the grab) before the object goes.</summary>
    void OnDestroy() override;

    /// <summary>Draws the field and, while open, the list, from the state.</summary>
    void Draw() override;

    bool DrawsLive() override { return true; }

    /// <summary>The mouse and keys, as the class remarks say.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>The wheel moves the choice while closed, the lit row while open.</summary>
    bool MouseWheel(int32_t steps, int32_t xPos, int32_t yPos) override;

    /// <summary>Whether the list is open.</summary>
    bool IsOpen() const { return _Open; }

    /// <summary>The index of the item the setting holds, or -1 when it holds none of them.</summary>
    int32_t Selected() const;

    /// <summary>The lit row (an item index) while open.</summary>
    int32_t Hovered() const { return _Hovered; }

    /// <summary>The items.</summary>
    const std::vector<Item>& Items() const { return _Items; }

    /// <summary>Shows item <paramref name="index"/> as <paramref name="label"/> from now on (none past the last).</summary>
    void SetLabel(size_t index, std::string label);

    /// <summary>Opens the list: the lit row is the current choice; the control comes in front and takes the mouse.</summary>
    void Open();

    /// <summary>Closes the list without a choice and lets go of the mouse.</summary>
    void Close();

    /// <summary>
    /// Sets the setting to item <paramref name="index"/>'s value; when that changes it, runs the changed routine.
    /// </summary>
    void Choose(int32_t index);

    /// <summary>The item under the screen point (<paramref name="xPos"/>, <paramref name="yPos"/>) in the open list, or -1.</summary>
    int32_t RowAt(int32_t xPos, int32_t yPos);

    /// <summary>White font's inked values in the label colour 0xe3 (255 stays out), registered once.</summary>
    static MCColorTable LabelColors();

private:
    /// <summary>The rows the open list shows.</summary>
    int32_t VisibleRows() const;

    /// <summary>Shrinks the control back to its field (the mouse stays as it is).</summary>
    void CloseList();

    /// <summary>Lights row <paramref name="index"/> (clamped) and scrolls it into view.</summary>
    void Hover(int32_t index);

    /// <summary>Whether the screen point lies on the field.</summary>
    bool InField(int32_t xPos, int32_t yPos);

    /// <summary>
    /// Puts this object, and each of its parents below its screen, last among its siblings of its depth, so the list
    /// draws over everything on the screen.
    /// </summary>
    void RaiseOnScreen();

    /// <summary>Writes <paramref name="text"/> at (<paramref name="xPos"/>, <paramref name="yPos"/>) in the label colour.</summary>
    void WriteLabel(int32_t xPos, int32_t yPos, const std::string& text);

    /// <summary>The setting the control edits (its model).</summary>
    MCOptionalRef<int32_t> _Setting;

    /// <summary>The setting edited (set by <see cref="Init"/>; fatal before).</summary>
    int32_t& Setting() const;
    /// <summary>The choices, top to bottom.</summary>
    std::vector<Item> _Items;
    /// <summary>Runs with the new value after a choice that changed the setting (may be empty).</summary>
    std::function<void(int32_t value)> _Changed;
    /// <summary>Whether the list is open.</summary>
    bool _Open = false;
    /// <summary>The lit row (an item index) while open.</summary>
    int32_t _Hovered = -1;
    /// <summary>The first item the open list shows.</summary>
    int32_t _FirstRow = 0;
    /// <summary>A press closed the list: the control keeps the mouse until that press is let go.</summary>
    bool _HoldUntilRelease = false;
};

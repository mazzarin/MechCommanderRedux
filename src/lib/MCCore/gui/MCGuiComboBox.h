#pragma once

#include "gui/MCGuiObject.h"
#include "gui/MCGuiRef.h"

class MCGuiListBox;
class MCGuiTextObject;
class MCGuiToolButton;

/// <summary>A drop-down list: a read-only text field, a drop button and a list box shown below while open.</summary>
/// <remarks>Original source: <c>gui\alistbox.cpp</c> (<c>aComboBox</c>).</remarks>
class MCGuiComboBox : public MCGuiObject
{
public:
    /// <summary>The message the drop button and the list's <see cref="CloseListOnMousedown"/> post to the box.</summary>
    static constexpr int32_t ToggleList = 0x15;

    ~MCGuiComboBox() override;

    /// <summary>Makes the list (hidden, closed by <see cref="CloseListOnMousedown"/>), the drop button and the text field.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    void Draw() override;
    /// <summary>Port: draws itself each frame (its frame; the list, button and text field draw themselves).</summary>
    bool DrawsLive() override { return true; }
    /// <summary>Opens the list on <see cref="ToggleList"/>; closing it takes the list's selection into the text field.</summary>
    void HandleEvent(MCGuiEvent& event) override;
    void Resize(int32_t newWidth, int32_t newHeight) override;

    /// <summary>Changes a list string (and the text field when it is the selected one).</summary>
    /// <returns>Whether <paramref name="item"/> is an item.</returns>
    bool ChangeItemString(int32_t item, std::string_view text) const;
    /// <returns>Whether <paramref name="item"/> is an item.</returns>
    bool SelectItem(int32_t item) const;

    /// <summary>The field showing the selection (read-only).</summary>
    MCGuiHandle<MCGuiTextObject> TextField;
    /// <summary>The list shown below while open.</summary>
    MCGuiHandle<MCGuiListBox> ListBox;
    /// <summary>The drop button (a tool button, art packets 0xe/0xf).</summary>
    MCGuiHandle<MCGuiToolButton> DropButton;
};

/// <summary>A combo box list's event routine: closes the list when the mouse is pressed on it.</summary>
void CloseListOnMousedown(MCGuiObject& obj, MCGuiEvent& event);

#pragma once

#include "gui/MCGuiCallback.h"
#include "gui/MCGuiObject.h"

class MCGuiFont;

/// <summary>
/// A pop-up menu: items with a callback, a data value and an optional accelerator letter. An item named
/// <see cref="MCGuiMenu::Separator"/> is drawn as a separator line.
/// </summary>
/// <remarks>Original source: <c>gui\awindow.cpp</c> and <c>gui\awindow.h</c> (<c>aMenu</c>).</remarks>
class MCGuiMenu : public MCGuiObject
{
public:
    /// <summary>The text of a separator item.</summary>
    static constexpr std::string_view Separator = "::::";

    /// <summary>One line of the menu.</summary>
    struct Item
    {
        /// <summary>The item's text (<see cref="Separator"/> for a separator line).</summary>
        std::string Text;
        /// <summary>Run when the item is chosen; none for a separator.</summary>
        std::shared_ptr<MCGuiCallback> Callback;
        /// <summary>A value for the menu's owner (0 for an item, -1 for a separator).</summary>
        int32_t Data = 0;
        /// <summary>The accelerator letter drawn at the right, or 0.</summary>
        char Letter = 0;
    };

    /// <summary>Also sets <see cref="Shown"/> when showing.</summary>
    void ShowGuiWindow(bool show) override;
    /// <summary>Clears the items; the white font, back colour 0.</summary>
    /// <returns>0, or the base's error.</returns>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    /// <summary>Empties the menu.</summary>
    void OnDestroy() override;
    /// <summary>Tracks the highlighted item; a release over an item runs its callback and hides the menu.</summary>
    void HandleEvent(MCGuiEvent& event) override;
    void Draw() override;
    /// <summary>Port: draws itself each frame from its items and the highlighted one.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>Sizes the menu to its widest item and the item count.</summary>
    void ResizeMenu();
    /// <summary>Appends an item with an empty callback.</summary>
    /// <returns>The item's index.</returns>
    int32_t AddItem(std::string_view text);
    /// <summary>Removes the first item whose text matches <paramref name="text"/> (case-insensitively).</summary>
    /// <returns>Whether one was removed.</returns>
    bool RemoveItem(std::string_view text);
    /// <summary>Removes item <paramref name="index"/>.</summary>
    /// <returns>Whether it was an item.</returns>
    bool RemoveItem(int32_t index);
    /// <summary>Appends a separator (no callback; the menu is not resized).</summary>
    /// <returns>The new item count (not the index).</returns>
    int32_t AddSeparator();
    /// <summary>Makes item <paramref name="index"/> run <paramref name="exec"/>.</summary>
    void SetCallback(int32_t index, std::function<void()> exec);
    /// <summary>Makes item <paramref name="index"/> post <paramref name="message"/> to <paramref name="target"/>.</summary>
    void SetMessage(int32_t index, std::weak_ptr<MCGuiObject> target, int32_t message);
    /// <returns>Whether <paramref name="index"/> is an item.</returns>
    bool ChangeItemString(int32_t index, std::string_view text);
    /// <summary>Moves the menu back onto the screen.</summary>
    void KeepOnScreen();
    /// <summary>Sets item <paramref name="index"/>'s data.</summary>
    void SetItemData(int32_t index, int32_t data);
    /// <returns>The item's data, or <see cref="BadIndex"/>.</returns>
    int32_t GetItemData(int32_t index);
    /// <summary>Sets item <paramref name="index"/>'s accelerator letter (the menu widens for the letters).</summary>
    void SetItemLetter(int32_t index, char letter);
    /// <returns>The item's letter, or 3 for a bad index.</returns>
    char GetItemLetter(int32_t index);
    /// <summary>The number of items (separators included).</summary>
    int32_t NumItems() const { return static_cast<int32_t>(Items.size()); }

    /// <summary>What <see cref="GetItemData"/> returns for a bad index.</summary>
    static constexpr int32_t BadIndex = static_cast<int32_t>(0xeeee0003);

    /// <summary>Whether the item text is right-aligned.</summary>
    bool RightAligned = false;
    /// <summary>The highlighted item, or -1.</summary>
    int32_t SelectedItem = -1;
    /// <summary>The items, top to bottom.</summary>
    std::vector<Item> Items;
    /// <summary>Set once any item has a letter (it widens the menu).</summary>
    bool HasLetters = false;
    /// <summary>The items' font (the white one).</summary>
    std::shared_ptr<MCGuiFont> Font;
    /// <summary>The height of an item: the font's plus 8.</summary>
    int32_t ItemHeight = 0;
    /// <summary>Set when the menu is shown.</summary>
    bool Shown = false;

private:
    /// <summary>Whether <paramref name="index"/> is an item.</summary>
    bool IsItem(int32_t index) const { return index >= 0 && index < NumItems(); }
};

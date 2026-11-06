#pragma once

#include "gui/MCGuiObject.h"
#include "gui/MCGuiRef.h"
#include "gui/MCGuiScrollBar.h"

class MCGuiFont;

/// <summary>A list of strings in the grey font, with a selection and an optional scroll bar.</summary>
/// <remarks>Original source: <c>gui\alistbox.cpp</c> (<c>aListBox</c>).</remarks>
class MCGuiListBox : public MCGuiObject
{
public:
    ~MCGuiListBox() override;

    /// <summary>Like the base's init; rounds the height up to whole lines.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    /// <summary>Empties the list.</summary>
    void OnDestroy() override;
    /// <summary>Draws the visible lines, the selected one highlighted.</summary>
    void Draw() override;
    /// <summary>Port: draws itself each frame from its items, top line and selection.</summary>
    bool DrawsLive() override { return true; }
    /// <summary>
    /// Tracks the highlighted line under the mouse, selects on a click and while dragging (scrolling past the ends),
    /// moves with the paging and arrow keys, and follows the scroll bar.
    /// </summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>Appends <paramref name="text"/>, and grows the box (no scroll bar) or the scroll range.</summary>
    void AddItem(std::string_view text);
    /// <returns>Whether <paramref name="item"/> is an item.</returns>
    bool ChangeItemString(int32_t item, std::string_view text);
    /// <returns>Whether <paramref name="item"/> is an item.</returns>
    bool SelectItem(int32_t item);
    /// <summary>Adds the scroll bar at the right edge (once).</summary>
    /// <returns>0, or the bar's init error.</returns>
    int32_t ActivateScrollbar();
    /// <returns>Item <paramref name="item"/>'s text, or null for a bad index.</returns>
    std::optional<std::string> GetItemString(int32_t item) const;
    /// <summary>The number of items.</summary>
    int32_t NumItems() const { return static_cast<int32_t>(Items.size()); }
    /// <summary>The number of whole lines the box shows.</summary>
    int32_t VisibleItems() const;

    /// <summary>The items' text, top to bottom.</summary>
    std::vector<std::string> Items;
    /// <summary>The selected line, or -1.</summary>
    int32_t SelectedItem = -1;
    /// <summary>The line under the mouse, or -1.</summary>
    int32_t HighlightedItem = -1;
    /// <summary>The first line shown (the scroll position).</summary>
    int32_t TopItem = 0;
    /// <summary>The items' font (the grey one).</summary>
    std::shared_ptr<MCGuiFont> ItemFont;
    /// <summary>The scroll bar, once <see cref="ActivateScrollbar"/> added it.</summary>
    MCGuiHandle<MCGuiScrollBar> ScrollBar;
    /// <summary>The font's height + 8.</summary>
    int32_t ItemHeight = 0;

private:
    /// <summary>Tells an active scroll bar the new top line without it posting back.</summary>
    void SetBarPosition(int32_t position) const;
    /// <summary>Selects and highlights <paramref name="item"/>.</summary>
    void Choose(int32_t item);
    /// <summary>Sets the scroll bar's range to the lines past the visible ones.</summary>
    void UpdateScrollRange() const;
};

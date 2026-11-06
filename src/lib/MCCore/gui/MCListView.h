#pragma once

#include "gui/MCScrollPane.h"
#include "platform/MCAddressKey.h"

class MCListView;

/// <summary>
/// One row of a <see cref="MCListView"/>: a child of the list bound to one item of the list's source, which it draws.
/// The list makes it, places it and destroys it once its item leaves the source.
/// </summary>
/// <remarks>
/// A row keeps what it works out from its item (pictures, texts) as its own cache; the item never draws or holds its
/// row. The list draws the rows into its content (<see cref="DrawRow"/>), so a row draws nothing of its own in the frame
/// pass; it takes the clicks on its part of the list.
/// </remarks>
class MCListRow : public MCLogObject
{
public:
    /// <summary>
    /// Draws the row's item into <paramref name="port"/> (the list's content) with its top at <paramref name="top"/> (its
    /// left edge is the row's own <c>X</c>).
    /// </summary>
    virtual void DrawRow(MCLogPort& port, int32_t top) = 0;

    /// <summary>
    /// Called by <see cref="MCListView::Refresh"/> for every row, after placing it: the row works out again what it
    /// shows from its item and the screen (a greyed-out state). Nothing in the base.
    /// </summary>
    virtual void OnRefresh() {}

    /// <summary>The item the row shows (expired once the item is gone).</summary>
    const std::weak_ptr<void>& ListItem() const { return _ListItem; }

private:
    friend class MCListView;

    /// <summary>The item the row shows, set by the list when it makes the row.</summary>
    std::weak_ptr<void> _ListItem;
    /// <summary>Where the item lies, to find the row of an item (only while <see cref="_ListItem"/> lives).</summary>
    MCAddressKey _ListKey;
};

/// <summary>
/// What a <see cref="MCListView"/> shows: its items, which of them get a row, their order, which rows take clicks, and
/// how a row is made.
/// </summary>
template <typename Item> struct MCListSource
{
    /// <summary>The items (the model's list), read at each refresh and each frame.</summary>
    std::function<const std::vector<std::shared_ptr<Item>>&()> Items;
    /// <summary>Which items get a row; empty: all of them.</summary>
    std::function<bool(const Item&)> Filter;
    /// <summary>The row order (a strict weak order, applied stably); empty: the order of <see cref="Items"/>.</summary>
    std::function<bool(const Item&, const Item&)> Sort;
    /// <summary>
    /// Which rows take clicks; empty: all of them. A row that doesn't is drawn but hidden, so the list's clicks pass
    /// over it (it still gets the events of a drag it started, through the grab).
    /// </summary>
    std::function<bool(const Item&)> Clickable;
    /// <summary>Makes the row for an item on the list (<see cref="MCListView::CreateRow"/>).</summary>
    std::function<MCGuiHandle<MCListRow>(MCListView& list, std::shared_ptr<Item> item)> MakeRow;
};

/// <summary>How a <see cref="MCListView"/> lays out its rows.</summary>
/// <remarks>
/// Rows fill a grid of <see cref="Columns"/> columns, a line at a time: item n is in column n % columns of line
/// n / columns, at (<see cref="Left"/> + column times <see cref="ColumnWidth"/>, <see cref="Top"/> + line times
/// <see cref="RowHeight"/>).
/// </remarks>
struct MCListLayout
{
    /// <summary>Each line's height.</summary>
    int32_t RowHeight = 0;
    /// <summary>The content's width.</summary>
    int32_t Width = 0;
    /// <summary>The content's smallest height (the content is the lines' height, or this when that is less).</summary>
    int32_t MinHeight = 0;
    /// <summary>The colour under the rows.</summary>
    uint8_t Background = 0;
    /// <summary>The rows on a line.</summary>
    int32_t Columns = 1;
    /// <summary>Each column's width (with more than one).</summary>
    int32_t ColumnWidth = 0;
    /// <summary>The first column's left edge.</summary>
    int32_t Left = 0;
    /// <summary>The first line's top (the content's height doesn't count it).</summary>
    int32_t Top = 0;

    /// <summary>Item <paramref name="index"/>'s place in the content.</summary>
    POINT Place(size_t index) const
    {
        const auto columns = static_cast<size_t>(std::max(Columns, 1));
        return {Left + static_cast<int32_t>(index % columns) * ColumnWidth,
                Top + static_cast<int32_t>(index / columns) * RowHeight};
    }

    /// <summary>The content's height for <paramref name="count"/> items.</summary>
    int32_t ContentHeight(size_t count) const
    {
        const auto columns = static_cast<size_t>(std::max(Columns, 1));
        const auto lines = static_cast<int32_t>((count + columns - 1) / columns);
        return std::max(lines * RowHeight, MinHeight);
    }
};

/// <summary>
/// A scrolling list of rows (<see cref="MCListRow"/>) over a data source (<see cref="MCListSource"/>): the list is a
/// view of the source, each row a view of one item.
/// </summary>
/// <remarks>
/// <para><see cref="Refresh"/> matches the rows to the source: it keeps the row of an item still there, makes rows for
/// new items, destroys the rows of items gone, and places each row at its place in the order.</para>
/// <para>Each frame the list draws from the source, in the source's order at that moment, through the rows it has: a
/// change in the model shows at once, and the rows (the click areas) follow at the next refresh.</para>
/// <para>A row being dragged (the GUI system's grab) whose item leaves the source keeps living, hidden and out of the
/// order, so the drag can end on it; the first refresh after the drag destroys it.</para>
/// </remarks>
class MCListView : public MCScrollPane
{
public:
    /// <summary>Shows <paramref name="source"/> laid out by <paramref name="layout"/>: sets them and refreshes.</summary>
    template <typename Item> void Show(MCListSource<Item> source, const MCListLayout& layout, bool resetScroll)
    {
        _Entries = [source]()
        {
            std::vector<std::shared_ptr<Item>> items;

            for (const std::shared_ptr<Item>& item : source.Items())
            {
                if (item != nullptr && (!source.Filter || source.Filter(*item)))
                {
                    items.push_back(item);
                }
            }

            if (source.Sort)
            {
                std::ranges::stable_sort(items,
                                         [&](const std::shared_ptr<Item>& first, const std::shared_ptr<Item>& second)
                                         { return source.Sort(*first, *second); });
            }

            std::vector<Entry> entries;
            entries.reserve(items.size());

            for (std::shared_ptr<Item>& item : items)
            {
                const bool clickable = !source.Clickable || source.Clickable(*item);
                const MCAddressKey key = MCAddressKey::Of(*item);
                entries.push_back({std::move(item), key, clickable});
            }

            return entries;
        };

        _MakeRow = [make = source.MakeRow](MCListView& list, const std::shared_ptr<void>& item)
        { return make(list, std::static_pointer_cast<Item>(item)); };
        _Layout = layout;
        Refresh(resetScroll);
    }

    /// <summary>
    /// Matches the rows to the source and places them, then shows the content sized for them (from the top when
    /// <paramref name="resetScroll"/>, else at the same offset, as <see cref="SetDisplayPort"/>). Nothing without a
    /// source.
    /// </summary>
    void Refresh(bool resetScroll);

    /// <summary>Destroys the rows and forgets the source (the content is left as it is).</summary>
    void Clear();

    /// <summary>
    /// Makes a <typeparamref name="T"/> row on the list, set up by <paramref name="setup"/> (its <c>Init</c>); for
    /// <see cref="MCListSource::MakeRow"/>.
    /// </summary>
    template <typename T, typename Setup>
        requires std::derived_from<T, MCListRow>
    MCGuiHandle<MCListRow> CreateRow(Setup&& setup)
    {
        return CreateChild<T>(std::forward<Setup>(setup));
    }

    /// <summary>The row showing <paramref name="item"/>, as a <typeparamref name="Row"/>, or null; the caller keeps it as its pin.</summary>
    template <typename Row, typename Item> std::shared_ptr<Row> RowOf(const Item& item) const
    {
        return std::dynamic_pointer_cast<Row>(FindRow(MCAddressKey::Of(item)));
    }

    /// <summary>The rows in their order (rows destroyed since the last refresh are empty handles).</summary>
    const std::vector<MCGuiHandle<MCListRow>>& Rows() const { return _Rows; }

    /// <summary>
    /// Whether <paramref name="child"/> is one of the list's rows: in the order, or held while it is dragged (see
    /// <see cref="Refresh"/>).
    /// </summary>
    bool IsRow(const MCGuiObject& child) const;

    /// <summary>The content: the rows, drawn from the source (see the class remarks).</summary>
    void DrawContent() override;

    /// <summary>Forgets the source and the rows' handles.</summary>
    void OnDestroy() override;

private:
    /// <summary>An item to show, where it lies, and whether its row takes clicks.</summary>
    struct Entry
    {
        std::shared_ptr<void> Item;
        MCAddressKey Key;
        bool Clickable = true;
    };

    /// <summary>The row bound to the item at <paramref name="item"/>, or null.</summary>
    std::shared_ptr<MCListRow> FindRow(MCAddressKey item) const;

    /// <summary>The source's items, filtered and in order (empty without a source).</summary>
    std::function<std::vector<Entry>()> _Entries;
    /// <summary>Makes the row for an item.</summary>
    std::function<MCGuiHandle<MCListRow>(MCListView& list, const std::shared_ptr<void>& item)> _MakeRow;
    /// <summary>The layout.</summary>
    MCListLayout _Layout;
    /// <summary>The rows, in order.</summary>
    std::vector<MCGuiHandle<MCListRow>> _Rows;
    /// <summary>A row being dragged whose item left the source: hidden, out of the order, until the drag is over.</summary>
    MCGuiHandle<MCListRow> _Held;
    /// <summary>The content the list shows (a view the rows are drawn into).</summary>
    std::shared_ptr<MCLogPort> _View;
};

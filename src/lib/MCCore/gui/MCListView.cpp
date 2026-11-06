#include "stdafx.h"
#include "gui/MCListView.h"
#include "gui/MCGuiSystem.h"
#include "vfx/MCVfxFunctions.h"

auto MCListView::Refresh(bool resetScroll) -> void
{
    if (!_Entries)
    {
        return;
    }

    const std::vector<Entry> entries = _Entries();
    auto shown = [&](const MCListRow& row)
    {
        const std::shared_ptr<void> item = row.ListItem().lock();
        return item != nullptr && std::ranges::any_of(entries, [&](const Entry& entry) { return entry.Item == item; });
    };

    // The rows of items gone (or filtered out) go first, except one being dragged: it is hidden and kept out of the
    // order until a refresh after the drag, as the original's block, taken out of its pane, lived on. A held row whose
    // item comes back is a row in the order again.
    const std::shared_ptr<MCGuiObject> grabbed = GuiSystem().GrabbedObject();
    std::vector<MCGuiHandle<MCListRow>> previous = std::move(_Rows);
    previous.push_back(std::exchange(_Held, nullptr));
    _Rows = previous;

    for (const MCGuiHandle<MCListRow>& handle : previous)
    {
        if (const std::shared_ptr<MCListRow> row = handle.Lock(); row != nullptr && !shown(*row))
        {
            if (row == grabbed)
            {
                row->ShowGuiWindow(false);
                _Held = handle;
                continue;
            }

            RemoveChild(*row);
        }
    }

    std::vector<MCGuiHandle<MCListRow>> rows;
    rows.reserve(entries.size());

    for (size_t index = 0; index < entries.size(); ++index)
    {
        const Entry& entry = entries[index];
        std::shared_ptr<MCListRow> row = FindRow(entry.Key);

        if (row == nullptr)
        {
            row = _MakeRow(*this, entry.Item).Lock();

            if (row == nullptr)
            {
                continue;
            }

            row->_ListItem = entry.Item;
            row->_ListKey = entry.Key;
        }

        // Placed unscrolled: showing the content below scrolls the rows with it.
        const POINT place = _Layout.Place(index);
        row->MoveTo(place.x, place.y, false);
        row->ShowGuiWindow(entry.Clickable);
        row->BringToFront(false);
        row->OnRefresh();
        rows.emplace_back(row);
    }

    _Rows = std::move(rows);

    if (_View == nullptr)
    {
        _View = std::make_shared<MCLogPort>();
    }

    _View->InitView(_Layout.Width, _Layout.ContentHeight(entries.size()));
    SetDisplayPort(_View, resetScroll);
}

auto MCListView::Clear() -> void
{
    for (const MCGuiHandle<MCListRow>& handle : _Rows)
    {
        if (const std::shared_ptr<MCListRow> row = handle.Lock(); row != nullptr)
        {
            RemoveChild(*row);
        }
    }

    if (const std::shared_ptr<MCListRow> held = _Held.Lock(); held != nullptr)
    {
        RemoveChild(*held);
    }

    _Rows.clear();
    _Held.reset();
    _Entries = nullptr;
    _MakeRow = nullptr;
}

auto MCListView::IsRow(const MCGuiObject& child) const -> bool
{
    auto refersTo = [&](const MCGuiHandle<MCListRow>& handle) { return handle.Lock().get() == &child; };
    return std::ranges::any_of(_Rows, refersTo) || refersTo(_Held);
}

auto MCListView::FindRow(MCAddressKey item) const -> std::shared_ptr<MCListRow>
{
    auto bound = [&](const std::shared_ptr<MCListRow>& row)
    { return row != nullptr && row->_ListKey == item && !row->ListItem().expired(); };

    for (const MCGuiHandle<MCListRow>& handle : _Rows)
    {
        if (std::shared_ptr<MCListRow> row = handle.Lock(); bound(row))
        {
            return row;
        }
    }

    if (std::shared_ptr<MCListRow> held = _Held.Lock(); bound(held))
    {
        return held;
    }

    return nullptr;
}

auto MCListView::DrawContent() -> void
{
    // Content shown by someone else (a screen still filling the pane the old way) draws itself.
    if (ContentPort == nullptr || ContentPort != _View || !_Entries)
    {
        MCScrollPane::DrawContent();
        return;
    }

    MCLogPort& view = *_View;
    VfxPaneWipe(view.Frame(), _Layout.Background);
    const MCView& place = view.View();
    const std::vector<Entry> entries = _Entries();

    for (size_t index = 0; index < entries.size(); ++index)
    {
        const int32_t top = _Layout.Place(index).y;
        const int32_t screenTop = place.OriginY + top;

        if (screenTop <= place.Scissor.Y1 && place.Scissor.Y0 < screenTop + _Layout.RowHeight)
        {
            if (const std::shared_ptr<MCListRow> row = FindRow(entries[index].Key); row != nullptr)
            {
                row->DrawRow(view, top);
            }
        }
    }
}

auto MCListView::OnDestroy() -> void
{
    _Rows.clear();
    _Held.reset();
    _Entries = nullptr;
    _MakeRow = nullptr;
    _View.reset();
    MCScrollPane::OnDestroy();
}

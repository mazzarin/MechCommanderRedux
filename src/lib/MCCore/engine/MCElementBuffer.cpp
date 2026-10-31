#include "stdafx.h"
#include "engine/MCElementBuffer.h"
#include "camera/MCCamera.h"
#include "platform/MCRenderer.h"

MCElementBuffer::MCElementBuffer()
{
    Reset();
}

auto ElementList() -> MCElementBuffer&
{
    return MCGameContext::Current().ElementList();
}

auto MCElementBuffer::Add(MCElement& element) -> void
{
    _Elements.emplace_back(element);
    _Groups.back().Count++;
}

auto MCElementBuffer::OpenGroup() -> void
{
    BeginGroup(0.0f, true);
}

auto MCElementBuffer::OpenGroup(int32_t depth, bool sortElements) -> void
{
    BeginGroup(static_cast<float>(depth), sortElements);
}

auto MCElementBuffer::BeginGroup(float depth, bool sortElements) -> void
{
    SortGroup(_Groups.back());
    _DrawOrder.push_back(_Groups.size());
    _Groups.push_back(Group{_Elements.size(), 0, depth, sortElements});
}

auto MCElementBuffer::SortGroup(const Group& group) -> void
{
    if (group.Count < 2 || !group.SortElements)
    {
        return;
    }

    // Deepest first: each slot takes the deepest of the elements from it on, swapping as it finds deeper ones (not a
    // stable sort; equal depths keep the order this gives).
    const auto first = _Elements.begin() + static_cast<ptrdiff_t>(group.First);
    const auto end = first + static_cast<ptrdiff_t>(group.Count);

    for (auto slot = first; slot != end; ++slot)
    {
        for (auto other = slot; other != end; ++other)
        {
            if (slot->get().Depth < other->get().Depth)
            {
                std::swap(*slot, *other);
            }
        }
    }
}

auto MCElementBuffer::Sort() -> void
{
    SortGroup(_Groups.back());
    // The original bubble-sorted the groups (in the order they were opened), deepest first: a stable sort.
    std::ranges::stable_sort(_DrawOrder, [this](size_t a, size_t b) { return _Groups[a].Depth > _Groups[b].Depth; });
}

auto MCElementBuffer::Draw() -> void
{
    for (const size_t index : _DrawOrder)
    {
        const Group& group = _Groups[index];

        for (size_t i = group.First; i < group.First + group.Count; i++)
        {
            MCElement& element = _Elements[i];
            const MCRenderer::GlowScope glow(element.Glow);
            const MCRenderer::InBetweenScope inBetween(element.InBetween);

            // Port: a unit overlay (health bar, selection mark, strike timer) draws on the screen over the view, at
            // the screen's scale (its position was mapped there when it was made); the rest into the world surface.
            if (MCOverlay.Pane != nullptr && MCIsOverlayDepth(element.Depth))
            {
                const std::shared_ptr<MCPane> worldPane = GlobalPane;
                GlobalPane = MCOverlay.Pane;
                element.Draw();
                GlobalPane = worldPane;
            }
            else
            {
                element.Draw();
            }
        }
    }
}

auto MCElementBuffer::Reset() -> void
{
    _Elements.clear();
    _Made.clear();
    _Groups.clear();
    _Groups.push_back(Group{});
    _DrawOrder.assign(1, 0);
}

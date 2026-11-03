#include "stdafx.h"
#include "engine/MCElementBuffer.h"
#include "camera/MCCamera.h"
#include "platform/MCRenderer.h"

namespace
{
    /// <summary>Sort's digits: 8 bits of a group's 32-bit key at a time.</summary>
    constexpr int DigitBits = 8;
    constexpr size_t DigitValues = size_t{1} << DigitBits;
    constexpr size_t KeyDigits = 32 / DigitBits;
    /// <summary>Where a sort entry holds its group's key, above the group's index.</summary>
    constexpr int KeyShift = 32;
    constexpr uint32_t SignBit = 0x80000000u;

    /// <summary>
    /// The key Sort orders a group at <paramref name="depth"/> by, smallest first: smaller the deeper, and the same for
    /// equal depths (0 and -0 too).
    /// </summary>
    uint32_t DeepestFirst(float depth)
    {
        // A float's bits ordered as an unsigned integer is: a negative one's all flipped, the rest's sign bit set.
        const auto bits = std::bit_cast<uint32_t>(depth + 0.0f);
        return (bits & SignBit) != 0 ? bits : ~(bits | SignBit);
    }
}

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
    // The original bubble-sorted the groups (in the order they were opened), deepest first: a stable sort. Port: a
    // radix sort of their keys (rain's drops are a group each, tens of thousands a frame), stable as well.
    std::array<std::array<size_t, DigitValues>, KeyDigits> counts{};
    _SortEntries.resize(_DrawOrder.size());

    for (size_t i = 0; i < _DrawOrder.size(); ++i)
    {
        const uint32_t key = DeepestFirst(_Groups[_DrawOrder[i]].Depth);
        _SortEntries[i] = uint64_t{key} << KeyShift | _DrawOrder[i];

        for (size_t digit = 0; digit < KeyDigits; ++digit)
        {
            ++counts[digit][(key >> (digit * DigitBits)) & (DigitValues - 1)];
        }
    }

    _SortScratch.resize(_SortEntries.size());

    for (size_t digit = 0; digit < KeyDigits; ++digit)
    {
        std::array<size_t, DigitValues>& starts = counts[digit];

        // A digit every key has the same moves nothing.
        if (std::ranges::contains(starts, _SortEntries.size()))
        {
            continue;
        }

        size_t start = 0;

        for (size_t& count : starts)
        {
            start += std::exchange(count, start);
        }

        const auto shift = static_cast<int>(KeyShift + digit * DigitBits);

        for (const uint64_t entry : _SortEntries)
        {
            _SortScratch[starts[(entry >> shift) & (DigitValues - 1)]++] = entry;
        }

        _SortEntries.swap(_SortScratch);
    }

    for (size_t i = 0; i < _SortEntries.size(); ++i)
    {
        _DrawOrder[i] = static_cast<uint32_t>(_SortEntries[i]);
    }
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
            const MCRenderer::SunShadowScope sunShadow(element.SunShadow);
            const MCRenderer::TurnScope turn(element.Turn);

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

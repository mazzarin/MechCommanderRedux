#pragma once

#include "engine/MCElement.h"
#include "main/MCGameContext.h"

/// <summary>
/// The frame's draw list. It owns the elements made for the frame (<see cref="Make"/>) and lists the ones to draw
/// (<see cref="Add"/>) in groups: a group is a run of consecutive elements drawn together (an object's pieces, the
/// terrain, the interface). <see cref="Sort"/> orders the groups by depth, and the elements inside a group by their
/// own depth when the group asks for it; <see cref="Draw"/> draws them. <see cref="Reset"/> empties it every frame.
/// </summary>
/// <remarks>
/// Original source: <c>engine\ceglist.cpp</c> (the list) and <c>engine\celement.cpp</c> (the element pool). The
/// original's pool was a fixed-size stack and running it dry was fatal; its list held the scenario's MaxElements
/// elements and MaxGroups groups, dropped elements past them and stopped drawing objects (<c>maxObjectsDrawn</c>)
/// when the groups ran out. The port's grow.
/// </remarks>
class MCElementBuffer
{
public:
    /// <summary>An empty list with its first group open.</summary>
    MCElementBuffer();

    MCElementBuffer(const MCElementBuffer&) = delete;
    MCElementBuffer& operator=(const MCElementBuffer&) = delete;

    /// <summary>Makes an element that lives until the next <see cref="Reset"/>.</summary>
    template <std::derived_from<MCElement> T, typename... Args> T& Make(Args&&... args)
    {
        auto element = std::make_unique<T>(std::forward<Args>(args)...);
        T& made = *element;
        _Made.push_back(std::move(element));
        return made;
    }

    /// <summary>Adds <paramref name="element"/> (one this list made) to the open group.</summary>
    void Add(MCElement& element);

    /// <summary>Closes (sorts) the open group and opens the next, at depth 0 with its elements sorted.</summary>
    void OpenGroup();

    /// <summary>
    /// Closes (sorts) the open group and opens the next at <paramref name="depth"/>, its elements sorted when
    /// <paramref name="sortElements"/>.
    /// </summary>
    void OpenGroup(int32_t depth, bool sortElements);

    /// <summary>Closes the open group and orders the groups, deepest first.</summary>
    void Sort();

    /// <summary>Draws every group in order.</summary>
    void Draw();

    /// <summary>Frees the frame's elements, empties the list and opens its first group.</summary>
    void Reset();

    /// <summary>The number of elements in the list.</summary>
    size_t ElementCount() const { return _Elements.size(); }

    /// <summary>The number of groups opened.</summary>
    size_t GroupCount() const { return _Groups.size(); }

    /// <summary>The number of elements made since the last <see cref="Reset"/>.</summary>
    size_t MadeCount() const { return _Made.size(); }

private:
    /// <summary>A run of consecutive elements of the list.</summary>
    struct Group
    {
        /// <summary>The index of the group's first element.</summary>
        size_t First = 0;
        /// <summary>The number of elements in the group.</summary>
        size_t Count = 0;
        /// <summary>The group's sort key.</summary>
        float Depth = 0.0f;
        /// <summary>Whether the elements are sorted by depth.</summary>
        bool SortElements = true;
    };

    /// <summary>Opens a group at <paramref name="depth"/> after closing the last one.</summary>
    void BeginGroup(float depth, bool sortElements);

    /// <summary>Sorts a group's elements, deepest first, when it asks for it.</summary>
    void SortGroup(const Group& group);

    /// <summary>The elements made this frame.</summary>
    std::vector<std::unique_ptr<MCElement>> _Made;
    /// <summary>The elements to draw, group after group (each one of <see cref="_Made"/>).</summary>
    std::vector<std::reference_wrapper<MCElement>> _Elements;
    /// <summary>The groups, in the order they were opened (the last is open).</summary>
    std::vector<Group> _Groups;
    /// <summary>The group indices in drawing order: the order they were opened until <see cref="Sort"/>.</summary>
    std::vector<size_t> _DrawOrder;
    /// <summary>
    /// <see cref="Sort"/>'s entries (a group's key above its index) and their copy, kept for the next sort.
    /// </summary>
    std::vector<uint64_t> _SortEntries;
    std::vector<uint64_t> _SortScratch;
};

/// <summary>The frame's draw list (set for the whole scenario).</summary>
MCElementBuffer& ElementList();

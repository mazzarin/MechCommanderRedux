#pragma once

#include "gui/MCGuiObject.h"

/// <summary>
/// How the tests make GUI objects, close them and refer to them: a root (the one owner outside the tree), a child made on
/// its parent, closing a child, a handle (refers, never owns) and a pin (keeps the memory for one block).
/// </summary>
/// <remarks>
/// The tests of the GUI's ownership go through these only, so they keep their bodies when the way the GUI owns its
/// objects changes: only these functions change with it. The tree owns its children: <see cref="MakeChild"/> returns a
/// handle, and only <see cref="MakeRoot"/> and <see cref="NewRoot"/> return an owner.
/// </remarks>
namespace MCGuiTree
{
    /// <summary>
    /// A root object of type <typeparamref name="T"/>: placed at (x, y) with the given size, on no parent, owned by what
    /// it returns (as the GUI system owns its screen window).
    /// </summary>
    template <typename T, typename... Args>
    MCGuiRef<T> MakeRoot(int32_t x, int32_t y, int32_t width, int32_t height, Args&&... args)
    {
        MCGuiRef<T> object = MCGuiRef<T>::Make(std::forward<Args>(args)...);
        object->Init(x, y, width, height, std::string_view());
        return object;
    }

    /// <summary>
    /// A root object of type <typeparamref name="T"/>, not placed yet: for a test that sets it up with an
    /// <c>Init</c> of its own (a class's own overload, or one whose result it checks).
    /// </summary>
    template <typename T, typename... Args> MCGuiRef<T> NewRoot(Args&&... args)
    {
        return MCGuiRef<T>::Make(std::forward<Args>(args)...);
    }

    /// <summary>
    /// A child of type <typeparamref name="T"/> made on <paramref name="parent"/> at (x, y) with the given size; the
    /// parent keeps it until the child is closed or the parent destroyed.
    /// </summary>
    template <typename T, typename... Args>
    MCGuiHandle<T> MakeChild(MCGuiObject& parent, int32_t x, int32_t y, int32_t width, int32_t height, Args&&... args)
    {
        return parent.CreateChild<T>([&](T& made) { made.Init(x, y, width, height, std::string_view()); },
                                     std::forward<Args>(args)...);
    }

    /// <summary>A handle to <paramref name="object"/>: refers to it, doesn't keep it.</summary>
    template <typename T> MCGuiHandle<T> HandleOf(const std::shared_ptr<T>& object)
    {
        return object;
    }

    /// <summary>A handle to the object <paramref name="owner"/> owns.</summary>
    template <typename T> MCGuiHandle<T> HandleOf(const MCGuiRef<T>& owner)
    {
        return owner.Handle();
    }

    /// <summary>A handle is its own handle.</summary>
    template <typename T> MCGuiHandle<T> HandleOf(const MCGuiHandle<T>& handle)
    {
        return handle;
    }

    /// <summary>Whether the object <paramref name="handle"/> refers to is still there (not destroyed).</summary>
    template <typename T> bool Alive(const MCGuiHandle<T>& handle)
    {
        return static_cast<bool>(handle);
    }

    /// <summary>
    /// A pin on the object <paramref name="handle"/> refers to, for the length of a block: keeps its memory (null once
    /// it is destroyed).
    /// </summary>
    template <typename T> std::shared_ptr<T> Pin(const MCGuiHandle<T>& handle)
    {
        return handle.Lock();
    }

    /// <summary>Takes <paramref name="child"/> off its parent: what closes a window that is a child.</summary>
    inline void Close(MCGuiObject& child)
    {
        if (const std::shared_ptr<MCGuiObject> parent = child.ParentObject(); parent != nullptr)
        {
            parent->RemoveChild(child);
        }
    }
}

#pragma once

template <typename T> class MCGuiHandle;

/// <summary>
/// The one owner of a GUI object: its parent's entry for it (<see cref="MCGuiObject::ChildList"/>), or, for the root of
/// the tree, the GUI system. When the owner lets go, the object is destroyed at once (<c>Destroy</c>), while it, its
/// parent and its children are all still alive; its memory goes with the last pin.
/// </summary>
/// <remarks>
/// <para>Move-only: only the tree owns UI objects, and a UI object has one parent, so it has one owner. Everything else
/// refers to it by <see cref="MCGuiHandle"/> and pins it (a <c>shared_ptr</c>) for the length of a call; a pin keeps
/// the memory, not the object (it is destroyed all the same).</para>
/// <para><c>reset()</c> and the comparisons with <c>nullptr</c> are named as a smart pointer's, so an owner can replace
/// one where it was used.</para>
/// </remarks>
template <typename T> class MCGuiRef
{
public:
    MCGuiRef() = default;
    MCGuiRef(std::nullptr_t) {}
    ~MCGuiRef() { reset(); }
    MCGuiRef(const MCGuiRef&) = delete;
    MCGuiRef& operator=(const MCGuiRef&) = delete;
    MCGuiRef(MCGuiRef&& other) noexcept : _Object(std::move(other._Object)) {}

    /// <summary>Takes <paramref name="other"/>'s object (a <typeparamref name="U"/> is a <typeparamref name="T"/>).</summary>
    template <typename U>
        requires std::convertible_to<std::shared_ptr<U>, std::shared_ptr<T>>
    MCGuiRef(MCGuiRef<U>&& other) noexcept : _Object(std::move(other._Object))
    {
    }

    /// <summary>Destroys the object held now, then takes <paramref name="other"/>'s.</summary>
    MCGuiRef& operator=(MCGuiRef&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            _Object = std::move(other._Object);
        }

        return *this;
    }

    /// <summary>A new <typeparamref name="T"/> made from <paramref name="args"/>, owned by what this returns.</summary>
    template <typename... Args> static MCGuiRef Make(Args&&... args)
    {
        MCGuiRef made;
        made._Object = std::make_shared<T>(std::forward<Args>(args)...);
        return made;
    }

    /// <summary>
    /// Lets go of the object, which destroys it now (its memory stays while a pin holds it). Nothing when empty.
    /// </summary>
    void reset()
    {
        // Taken out first, so the object no longer has an owner while it is destroyed, yet stays alive until the end.
        if (const std::shared_ptr<T> object = std::exchange(_Object, nullptr); object != nullptr)
        {
            object->Destroy();
        }
    }

    /// <summary>A pin on the object, or null when empty.</summary>
    std::shared_ptr<T> Pin() const { return _Object; }
    /// <summary>A handle to the object (empty when this is).</summary>
    MCGuiHandle<T> Handle() const { return MCGuiHandle<T>(_Object); }

    /// <summary>The object (it must be there).</summary>
    T& operator*() const
    {
        SDL_assert(_Object != nullptr);
        return *_Object;
    }

    /// <summary>Reaches the object (it must be there).</summary>
    const std::shared_ptr<T>& operator->() const
    {
        SDL_assert(_Object != nullptr);
        return _Object;
    }

    explicit operator bool() const { return _Object != nullptr; }
    bool operator==(std::nullptr_t) const { return _Object == nullptr; }

private:
    template <typename U> friend class MCGuiRef;

    std::shared_ptr<T> _Object;
};

/// <summary>
/// A reference to a GUI object that doesn't keep it: what everything but the object's parent holds (a system, game
/// data, a container's shortcut to its own parts, a timer, a callback). It is empty once the object is destroyed, even
/// while a pin still keeps the memory.
/// </summary>
/// <remarks>
/// <c>-&gt;</c> pins the object for the length of one expression (and asserts it is there); <see cref="Lock"/> gives a
/// pin for a block of statements. <c>reset()</c> and the comparisons with <c>nullptr</c> are named as a smart
/// pointer's, so a handle can replace one where it was used.
/// </remarks>
template <typename T> class MCGuiHandle
{
public:
    MCGuiHandle() = default;
    MCGuiHandle(std::nullptr_t) {}

    /// <summary>A handle to the object <paramref name="object"/> refers to.</summary>
    template <typename U>
        requires std::convertible_to<std::shared_ptr<U>, std::shared_ptr<T>>
    MCGuiHandle(std::weak_ptr<U> object) : _Object(std::move(object))
    {
    }

    /// <summary>A handle to the object <paramref name="object"/> pins.</summary>
    template <typename U>
        requires std::convertible_to<std::shared_ptr<U>, std::shared_ptr<T>>
    MCGuiHandle(const std::shared_ptr<U>& object) : _Object(object)
    {
    }

    /// <summary>A handle to <paramref name="other"/>'s object (a <typeparamref name="U"/> is a <typeparamref name="T"/>).</summary>
    template <typename U>
        requires std::convertible_to<std::shared_ptr<U>, std::shared_ptr<T>>
    MCGuiHandle(const MCGuiHandle<U>& other) : _Object(other.Weak())
    {
    }

    /// <summary>A pin on the object, or null once it is destroyed (or for an empty handle).</summary>
    std::shared_ptr<T> Lock() const
    {
        std::shared_ptr<T> object = _Object.lock();
        return object != nullptr && !object->WasDestroyed() ? object : nullptr;
    }

    /// <summary>The reference underneath (it doesn't know whether the object was destroyed).</summary>
    const std::weak_ptr<T>& Weak() const { return _Object; }

    /// <summary>The reference as a <c>weak_ptr</c>, for the functions that take one (the GUI system's, the timers').</summary>
    template <typename U>
        requires std::convertible_to<std::shared_ptr<T>, std::shared_ptr<U>>
    operator std::weak_ptr<U>() const
    {
        return _Object;
    }

    /// <summary>Refers to nothing from now on.</summary>
    void reset() { _Object.reset(); }

    /// <summary>The object (it must be there; the tree keeps it while the caller uses it).</summary>
    T& operator*() const
    {
        const std::shared_ptr<T> object = Lock();
        SDL_assert(object != nullptr);
        return *object;
    }

    /// <summary>Reaches the object, pinned to the end of the expression (it must be there).</summary>
    std::shared_ptr<T> operator->() const
    {
        std::shared_ptr<T> object = Lock();
        SDL_assert(object != nullptr);
        return object;
    }

    /// <summary>Whether the object is there (not destroyed).</summary>
    explicit operator bool() const { return Lock() != nullptr; }
    bool operator==(std::nullptr_t) const { return Lock() == nullptr; }

    /// <summary>Whether <paramref name="other"/> pins the object this refers to (both empty counts as the same).</summary>
    template <typename U> bool operator==(const std::shared_ptr<U>& other) const { return Lock() == other; }

    /// <summary>Whether this refers to <paramref name="object"/> (by its control block).</summary>
    template <typename U> bool Is(const U& object) const { return MCRefersTo(_Object, object); }

private:
    std::weak_ptr<T> _Object;
};

/// <summary>
/// Whether <paramref name="first"/> and <paramref name="second"/> refer to the same object: the same control block, so
/// it also holds once the object's last share is gone. Two references to nothing are not the same.
/// </summary>
template <typename T, typename U> bool MCSameObject(const std::weak_ptr<T>& first, const std::weak_ptr<U>& second)
{
    const std::weak_ptr<T> none;
    const bool refers = first.owner_before(none) || none.owner_before(first);
    return refers && !first.owner_before(second) && !second.owner_before(first);
}

/// <summary>
/// <paramref name="object"/>'s own <c>weak_from_this</c> reference, which keeps its control block while the object is
/// being destroyed.
/// </summary>
/// <remarks>
/// <c>weak_from_this() const</c> returns a <c>weak_ptr&lt;const T&gt;</c> converted from the stored one, and libc++
/// converts a <c>weak_ptr</c> by locking it, so once the last share is gone the result is empty and no longer names
/// the object (MSVC's STL keeps the control block). The non-const overload copies the stored reference instead.
/// </remarks>
template <typename U> auto MCWeakSelf(const U& object)
{
    // Only to reach the non-const overload, which copies; nothing is modified.
    return const_cast<U&>(object).weak_from_this();
}

/// <summary>
/// Whether <paramref name="reference"/> refers to <paramref name="object"/> (by its control block, so it also holds
/// while the object is being destroyed). False for an object no <c>shared_ptr</c> owns.
/// </summary>
template <typename T, typename U> bool MCRefersTo(const std::weak_ptr<T>& reference, const U& object)
{
    return MCSameObject(MCWeakSelf(object), reference);
}

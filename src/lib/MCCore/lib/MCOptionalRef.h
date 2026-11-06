#pragma once

#include <functional>
#include <memory>
#include <optional>

/// <summary>A reference that may be absent: what a one-call command or parameter borrows when it may borrow nothing.</summary>
template <typename T> using MCOptionalRef = std::optional<std::reference_wrapper<T>>;

/// <summary>
/// The object <paramref name="pointer"/> points to as an <see cref="MCOptionalRef{T}"/> (absent for null). The caller
/// keeps its own pointer for as long as it uses the reference.
/// </summary>
template <typename T> auto MCOptionalOf(std::shared_ptr<T> pointer) -> MCOptionalRef<T>
{
    return pointer != nullptr ? MCOptionalRef<T>(*pointer) : std::nullopt;
}

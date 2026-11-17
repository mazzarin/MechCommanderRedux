#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

/// <summary>
/// Makes mimalloc the process allocator: this unit replaces the global <c>operator new</c>/<c>delete</c>
/// (<c>mimalloc-new-delete.h</c>), and <see cref="MCInitializeAllocator"/> points SDL's allocations at it too.
/// </summary>
/// <remarks>
/// Call it first in <c>main</c>, before any SDL call: memory SDL allocated through its default functions must not be
/// freed through mimalloc. The call also makes the linker pull this unit (and so the new/delete replacements) out of
/// the MCCore library.
/// </remarks>
/// <returns>Nothing, or why SDL refused the functions or <c>new</c> isn't served by mimalloc. Failing is fatal.</returns>
std::expected<void, std::string> MCInitializeAllocator();

/// <summary>
/// macOS: the tag the heap's regions of memory carry (VM_MEMORY_APPLICATION_SPECIFIC_1), by which the system's tools
/// (footprint, vmmap, Instruments) and the memory ledger (MCMemorySystemMac.mm) tell them from the rest.
/// </summary>
inline constexpr unsigned int MCHeapVmTag = 240;

/// <summary>
/// Windows: the bytes mimalloc has committed for the game's heap now, by its own count: what <c>new</c> and SDL's
/// allocations hold, what it rounds their blocks up to, and the free blocks in its pages it hasn't handed back. On
/// macOS, which overcommits, mimalloc maps its arenas committed and leaves their pages out of this count: there the
/// heap is counted by its regions (MCHeapVmTag).
/// </summary>
/// <returns>The bytes, or 0 where mimalloc can't say.</returns>
uint64_t MCHeapCommittedBytes();

/// <summary>
/// The bytes the allocator gave <paramref name="block"/> (a block <c>new</c> or SDL allocated): its size rounded up to
/// its size class's. 0 for null.
/// </summary>
size_t MCAllocatedBytes(const void* block);

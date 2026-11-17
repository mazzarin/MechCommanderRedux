#include "stdafx.h"
#include "platform/MCAllocator.h"
#include <mimalloc-new-delete.h>
#include <mimalloc-stats.h>

#if defined(__APPLE__)
#include <mach/vm_statistics.h>

namespace
{
    // macOS names each region of a program's memory by a tag its maker gives it (Activity Monitor, footprint, vmmap,
    // Instruments). mimalloc's default, 100, is the tag macOS gives Metal's own memory (VM_MEMORY_IOACCELERATOR), so
    // the system's tools showed the game's heap as the GPU's: the heap takes the first tag left to applications.
    static_assert(MCHeapVmTag == VM_MEMORY_APPLICATION_SPECIFIC_1);

    // mimalloc tags each region as it maps it, and C++'s static objects allocate before main: this runs first, at the
    // first priority left to programs. mimalloc's own start, at it too, maps its metadata and a first region for small
    // blocks (under a MB) before this runs, with its default tag.
    [[gnu::constructor(101)]] void TagHeap()
    {
        mi_option_set(mi_option_os_tag, MCHeapVmTag);
    }
}
#endif

std::expected<void, std::string> MCInitializeAllocator()
{
    if (!SDL_SetMemoryFunctions(mi_malloc, mi_calloc, mi_realloc, mi_free))
    {
        return std::unexpected(std::format("SDL_SetMemoryFunctions failed: {}", SDL_GetError()));
    }

    // The new/delete replacements only win when the linker took them instead of the CRT's: check that it did.
    const auto probe = std::make_unique<int>();

    if (!mi_is_in_heap_region(probe.get()))
    {
        return std::unexpected("operator new isn't served by mimalloc (the CRT's won at link time)");
    }

    return {};
}

uint64_t MCHeapCommittedBytes()
{
    // The main subprocess's figures with its heaps': mimalloc counts what it commits in the subprocess's own, in every
    // build (the heaps' finer figures, as the blocks in use, only with MI_STAT, which these builds leave off). The
    // subprocess's own alone (mi_subproc_stats_get_exclusive) can't be read: its copy lacks the size the call checks.
    mi_stats_t_decl(stats);

    if (!mi_subproc_stats_get(mi_subproc_main(), &stats))
    {
        return 0;
    }

    return static_cast<uint64_t>(std::max<int64_t>(stats.committed.current, 0));
}

size_t MCAllocatedBytes(const void* block)
{
    return mi_usable_size(block);
}

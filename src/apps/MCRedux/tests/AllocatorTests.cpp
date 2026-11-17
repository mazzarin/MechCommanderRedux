#include "stdafx.h"
#include "MCTest.h"
#include "platform/MCAllocator.h"
#include <mimalloc.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

// The process allocator (platform/MCAllocator.h): main installed mimalloc before any test ran.

namespace
{
    constexpr size_t HeldBlockBytes = size_t{64} << 20;

#if defined(__APPLE__)
    /// <summary>The tag macOS has for the region holding <paramref name="block"/>, or none when it finds none.</summary>
    std::optional<unsigned int> RegionTag(const void* block)
    {
        const auto wanted = reinterpret_cast<mach_vm_address_t>(block);
        mach_vm_address_t address = wanted;
        mach_vm_size_t size = 0;
        natural_t depth = 0;
        vm_region_submap_info_data_64_t info{};
        mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;

        if (mach_vm_region_recurse(mach_task_self(), &address, &size, &depth,
                                   reinterpret_cast<vm_region_recurse_info_t>(&info), &count) != KERN_SUCCESS ||
            address > wanted)
        {
            return std::nullopt;
        }

        return info.user_tag;
    }
#endif
}

TEST_CASE("allocator: new, STL containers and SDL_malloc are served by mimalloc")
{
    const auto single = std::make_unique<int>(7);
    const auto array = std::make_unique<uint8_t[]>(1 << 20);
    std::vector<uint32_t> vector(1000);
    std::string text(200, 'x');

    CHECK(mi_is_in_heap_region(single.get()));
    CHECK(mi_is_in_heap_region(array.get()));
    CHECK(mi_is_in_heap_region(vector.data()));
    CHECK(mi_is_in_heap_region(text.data()));

    void* sdlBlock = SDL_malloc(64);
    REQUIRE(sdlBlock != nullptr);
    CHECK(mi_is_in_heap_region(sdlBlock));
    sdlBlock = SDL_realloc(sdlBlock, 1 << 16);
    REQUIRE(sdlBlock != nullptr);
    CHECK(mi_is_in_heap_region(sdlBlock));
    SDL_free(sdlBlock);
}

TEST_CASE("allocator: a block's size is its size class's, and on Windows the heap's committed bytes take it in")
{
    std::vector<uint8_t> held(HeldBlockBytes, 1);
#if defined(_WIN32)
    // Windows commits as mimalloc asks (macOS overcommits: mimalloc maps its arenas committed, uncounted).
    CHECK(MCHeapCommittedBytes() >= HeldBlockBytes);
#endif

    constexpr size_t OddBytes = 1000;
    const auto odd = std::make_unique<uint8_t[]>(OddBytes);
    CHECK(MCAllocatedBytes(odd.get()) >= OddBytes);
    CHECK_EQ(MCAllocatedBytes(nullptr), 0u);
}

#if defined(__APPLE__)
/// <summary>
/// mimalloc's arenas, where the heap's blocks are (all but those in its first small region, under a MB, mapped as it
/// starts), are all mapped after the tag is set.
/// </summary>
TEST_CASE("allocator: macOS tags the heap's arenas as the game's own, not as Metal's")
{
    std::vector<uint8_t> large(HeldBlockBytes, 1);
    CHECK_EQ(RegionTag(large.data()), std::optional<unsigned int>(MCHeapVmTag));
}
#endif

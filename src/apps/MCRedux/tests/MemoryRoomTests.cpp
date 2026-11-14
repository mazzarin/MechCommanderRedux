#include "stdafx.h"
#include "../MCMemorySystem.h"
#include "MCTest.h"
#include "fakes/MCFakeMemorySystem.h"
#include "platform/MCMemoryRoom.h"
#include "platform/MCSdlHandle.h"
#include "platform/MCVulkanShaders.h"
#include "sprite/MCHdArt.h"

#include <array>

// Port-only (HD art): the room the HD art frames may take, in memory and on the GPU (platform/MCMemoryRoom.h): what a
// machine's answers make of it, and this platform's real answers (MCMemorySystemMac.mm, MCMemorySystemWin.cpp).

namespace
{
    constexpr uint64_t GB = uint64_t{1} << 30;
    constexpr uint64_t MB = uint64_t{1} << 20;
    /// <summary>The name the readers ask the stand-in's card by.</summary>
    constexpr std::string_view DeviceName = "Test GPU";

    /// <summary>A clock the test moves.</summary>
    struct MCTestClock
    {
        std::chrono::steady_clock::time_point Now{};

        MCMemoryRoom::MCReader::MCClock Reads()
        {
            return [this] { return Now; };
        }
    };

    /// <summary>
    /// A machine as its system reports it: the card's budget for the game (on Apple silicon Metal's recommended working
    /// set, about two thirds of the memory to 16 GB and three quarters past; on a PC DXGI's, most of the card's own),
    /// what the rest of the game holds there, and whether it's the machine's memory.
    /// </summary>
    struct MCMachine
    {
        std::string_view Name;
        uint64_t Budget = 0;
        uint64_t Rest = 0;
        bool SharesMemory = false;
    };
}

/// <summary>
/// The HD frames kept in memory may take 4 GB on any machine (MCMemoryRoom::MostHdBytes), never a share of its memory;
/// those on the GPU what the system gives the game on the card less what the rest of the game holds there, at most 4
/// GB, the same before and after the HD pictures hold some of it (what they hold is theirs to keep).
/// </summary>
TEST_CASE("hd caches: the frames in memory may take 4 GB, on the GPU what the card gives less the rest, at most 4 GB")
{
    CHECK_EQ(MCHdArt::CacheBudget(), static_cast<size_t>(MCMemoryRoom::MostHdBytes));
    CHECK_EQ(MCMemoryRoom::MostHdBytes, 4 * GB);

    constexpr std::array Machines = {
        MCMachine{"an 8 GB Mac", 5461 * MB, 900 * MB, true},
        MCMachine{"a 16 GB Mac", 10923 * MB, 900 * MB, true},
        MCMachine{"a 64 GB Mac", 48 * GB, 900 * MB, true},
        MCMachine{"a 16 GB PC with a 4 GB card", 3584 * MB, 700 * MB, false},
        MCMachine{"a 16 GB PC with a 12 GB card", 11 * GB, 700 * MB, false},
        MCMachine{"an 8 GB PC with integrated graphics", 4 * GB, 700 * MB, true},
    };

    // What each may take: the card's budget less the rest of the game, at most 4 GB.
    constexpr std::array<uint64_t, Machines.size()> Rooms = {4 * GB, 4 * GB, 4 * GB, 2884 * MB, 4 * GB, 3396 * MB};
    constexpr std::array<uint64_t, 3> HeldAmounts = {0, 512 * MB, 2 * GB};

    const auto system = std::make_shared<MCFakeMemorySystem>();
    uint64_t held = 0;
    system->Held = [&held] { return held; };
    MCFakeMemoryScope scope(system);

    for (size_t i = 0; i < Machines.size(); ++i)
    {
        const MCMachine& machine = Machines[i];
        system->Budget = machine.Budget;
        system->Rest = machine.Rest;
        system->SharesMemory = machine.SharesMemory;

        for (const uint64_t holding : HeldAmounts)
        {
            MCTest::Scope where(std::format("{}, the HD pictures holding {} MB", machine.Name, holding / MB));
            held = holding;
            MCTestClock clock;
            MCMemoryRoom::MCReader reader(std::string(DeviceName), clock.Reads());
            const MCMemoryRoom::MCFrameRoom room = reader.Frame(held);
            CHECK_EQ(room.PictureBytes, Rooms[i]);
            CHECK(!room.MemoryShort);
            CHECK_EQ(system->AskedName, std::string(DeviceName));
            CHECK_EQ(MCMemoryRoom::CardRoom(MCMemoryRoom::MCCard{machine.Budget, machine.Rest + held, false}, held),
                     Rooms[i]);
        }
    }

    // The rest of the game taking all the card gives leaves the HD pictures nothing; with no system, or one that can't
    // say, they may take 4 GB.
    CHECK_EQ(MCMemoryRoom::CardRoom(MCMemoryRoom::MCCard{GB, 2 * GB, false}, 512 * MB), uint64_t{0});
    system->Reports = false;
    MCMemoryRoom::MCReader unreported{std::string(DeviceName)};
    CHECK_EQ(unreported.Frame(0).PictureBytes, MCMemoryRoom::MostHdBytes);
    MCMemoryRoom::SetSystem(nullptr);
    MCMemoryRoom::MCReader none{std::string(DeviceName)};
    const MCMemoryRoom::MCFrameRoom alone = none.Frame(0);
    CHECK_EQ(alone.PictureBytes, MCMemoryRoom::MostHdBytes);
    CHECK(!alone.MemoryShort);
}

/// <summary>
/// The card is asked again every two seconds (MCMemoryRoom::CardReadInterval), its room holding between; a card over
/// its budget (other programs took some) trims the HD pictures to what's left and, sharing the machine's memory, the
/// frames kept in memory too. The system short of memory trims the frames kept in memory, and the HD pictures on a
/// card sharing the machine's memory (or one the system can't speak for), down to what the last frame drew.
/// </summary>
TEST_CASE("hd caches: the card is asked every two seconds, and memory or a shared card short trims both sides")
{
    const auto system = std::make_shared<MCFakeMemorySystem>();
    uint64_t held = GB;
    system->Held = [&held] { return held; };
    system->Budget = 8 * GB;
    system->Rest = GB / 4;
    MCFakeMemoryScope scope(system);
    MCTestClock clock;
    MCMemoryRoom::MCReader reader(std::string(DeviceName), clock.Reads());

    CHECK_EQ(reader.Frame(held).PictureBytes, 4 * GB);
    CHECK_EQ(system->CardReads, 1);

    // Another program takes most of the card: the room holds until the card is asked again.
    system->Budget = 2 * GB;
    clock.Now += std::chrono::seconds(1);
    CHECK_EQ(reader.Frame(held).PictureBytes, 4 * GB);
    CHECK_EQ(system->CardReads, 1);
    clock.Now += std::chrono::seconds(1);
    MCMemoryRoom::MCFrameRoom room = reader.Frame(held);
    CHECK_EQ(system->CardReads, 2);
    CHECK_EQ(room.PictureBytes, 2 * GB - system->Rest);
    CHECK(!room.MemoryShort);

    // Over its budget: the pictures go down to what's left; a card with memory of its own leaves the frames in memory.
    system->Budget = 3 * GB / 4;
    MCMemoryRoom::AskAgain();
    room = reader.Frame(held);
    CHECK_EQ(system->CardReads, 3);
    CHECK_EQ(room.PictureBytes, GB / 2);
    CHECK(!room.MemoryShort);

    // A card sharing the machine's memory over its budget: the frames in memory go too.
    system->SharesMemory = true;
    MCMemoryRoom::AskAgain();
    room = reader.Frame(held);
    CHECK_EQ(room.PictureBytes, GB / 2);
    CHECK(room.MemoryShort);

    // Under its budget again (the pictures let go of some), it says so at the next reading.
    held = GB / 4;
    clock.Now += MCMemoryRoom::CardReadInterval;
    room = reader.Frame(held);
    CHECK(!room.MemoryShort);
    CHECK_EQ(room.PictureBytes, GB / 2);

    // The system short of memory: the frames in memory go, and the pictures on a card sharing it, every frame it says so.
    system->Budget = 8 * GB;
    system->IsShort = true;
    MCMemoryRoom::AskAgain();
    room = reader.Frame(held);
    CHECK(room.MemoryShort);
    CHECK_EQ(room.PictureBytes, uint64_t{0});
    system->IsShort = false;
    room = reader.Frame(held);
    CHECK(!room.MemoryShort);
    CHECK_EQ(room.PictureBytes, 4 * GB);

    // A card with memory of its own keeps its pictures while the system is short.
    system->SharesMemory = false;
    system->IsShort = true;
    MCMemoryRoom::AskAgain();
    room = reader.Frame(held);
    CHECK(room.MemoryShort);
    CHECK_EQ(room.PictureBytes, 4 * GB);

    // A card the system can't speak for follows the memory, as one sharing it does.
    system->Reports = false;
    MCMemoryRoom::AskAgain();
    room = reader.Frame(held);
    CHECK(room.MemoryShort);
    CHECK_EQ(room.PictureBytes, uint64_t{0});
}

/// <summary>
/// The personal build: this platform's real answers (MCMemorySystemMac.mm, MCMemorySystemWin.cpp) on the card SDL's GPU
/// device runs on, by the name SDL gives it: a budget, and the game's own use there, which a texture made through SDL
/// adds to (Metal's count is the device SDL draws with; DXGI's counts Vulkan's allocations). Skipped without a GPU.
/// </summary>
TEST_CASE_ISOLATED("hd caches: this platform says the card SDL's device runs on, counting the game's textures there")
{
    constexpr uint32_t Side = 4096;
    constexpr uint64_t TextureBytes = uint64_t{Side} * Side * 4;

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        std::cout << std::format("  (skipped: no video: {})\n", SDL_GetError());
        return;
    }

    std::shared_ptr<SDL_GPUDevice> device(SDL_CreateGPUDevice(MCVulkanShaders::Format, false, MCVulkanShaders::Driver),
                                          MCSdlDeleter{});

    if (device == nullptr)
    {
        std::cout << std::format("  (skipped: no GPU device: {})\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return;
    }

    const char* const name =
        SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device.get()), SDL_PROP_GPU_DEVICE_NAME_STRING, "");
    REQUIRE(name != nullptr);
    const std::shared_ptr<MCMemoryRoom::MCSystem> system = MCMakeMemorySystem();
    const std::optional<MCMemoryRoom::MCCard> before = system->Card(name);

#ifdef _WIN32
    // DXGI lists no hardware adapter under a Vulkan device drawing in software (a build machine's).
    if (!before.has_value())
    {
        std::cout << std::format("  (skipped: DXGI says nothing of {})\n", name);
        device.reset();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return;
    }
#endif

    REQUIRE(before.has_value());
    CHECK(before->Budget > 0);
    std::cout << std::format("  {}: budget {} MB, used {} MB, {}; memory short: {}\n", name, before->Budget / MB,
                             before->Used / MB, before->SharesMemory ? "the machine's memory" : "memory of its own",
                             system->Short());

#if defined(__APPLE__) && defined(__aarch64__)
    CHECK(before->SharesMemory);
#endif

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = Side;
    info.height = Side;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    MCGpuHandle<SDL_GPUTexture> texture(SDL_CreateGPUTexture(device.get(), &info), MCGpuRelease{device});
    REQUIRE(texture != nullptr);
    const std::optional<MCMemoryRoom::MCCard> after = system->Card(name);
    REQUIRE(after.has_value());
    std::cout << std::format("  with a {} MB texture: used {} MB\n", TextureBytes / MB, after->Used / MB);
    CHECK(after->Used >= before->Used + TextureBytes);
    texture.reset();
    device.reset();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

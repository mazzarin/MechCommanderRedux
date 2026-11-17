#include "stdafx.h"
#include "../MCMemorySystem.h"
#include "MCTest.h"
#include "TestGame.h"
#include "fakes/MCFakeMemorySystem.h"
#include "lib/MCFastFile.h"
#include "platform/MCFileSystem.h"
#include "platform/MCGpuCounted.h"
#include "platform/MCMemoryLedger.h"
#include "platform/MCMemoryRoom.h"
#include "platform/MCVulkanShaders.h"
#include "sprite/MCHdArt.h"

// Port-only (SPEC-memory.md's "The ledger"): every byte the game holds counted under a named holder, with its side and
// its payload, allocations and retired bytes (platform/MCMemoryLedger.h); the GPU's objects counted from their making to
// their release (platform/MCGpuCounted.h); the frame log's line; and the game planned as another machine (-hdmemory,
// -hdcard). Other tests' holders may be counted at the same time, so each test checks what its own shares change.

namespace
{
    using MCMemoryLedger::MCFigures;
    using MCMemoryLedger::MCHolder;

    constexpr uint64_t GB = uint64_t{1} << 30;
    constexpr int64_t MB = int64_t{1} << 20;
    constexpr std::string_view DeviceName = "Test GPU";

    /// <summary>Figures as given (a function, so a check's macro takes them as one argument).</summary>
    MCFigures Figures(int64_t payload, int64_t allocated, int64_t retired = 0)
    {
        return {.Payload = payload, .Allocated = allocated, .Retired = retired};
    }

    /// <summary><paramref name="holder"/>'s figures less <paramref name="before"/>.</summary>
    MCFigures Since(MCHolder holder, const MCFigures& before)
    {
        const MCFigures now = MCMemoryLedger::Of(holder);
        return {.Payload = now.Payload - before.Payload,
                .Allocated = now.Allocated - before.Allocated,
                .Retired = now.Retired - before.Retired};
    }

    /// <summary>Sets the machine and card planned as while it lives, and the machine's own again however the test ends.</summary>
    class MCPlannedScope
    {
    public:
        MCPlannedScope(std::optional<uint64_t> machine, std::optional<uint64_t> card)
        {
            MCMemoryRoom::SetMachineBytes(machine);
            MCMemoryRoom::SetCardBytes(card);
        }

        ~MCPlannedScope()
        {
            MCMemoryRoom::SetMachineBytes(std::nullopt);
            MCMemoryRoom::SetCardBytes(std::nullopt);
        }

        MCPlannedScope(const MCPlannedScope&) = delete;
        MCPlannedScope& operator=(const MCPlannedScope&) = delete;
    };
}

TEST_CASE("memory ledger: a share counts its figures while it lives, replaced by each Set and moved with it")
{
    const MCFigures before = MCMemoryLedger::Of(MCHolder::HdHold);

    {
        MCMemoryLedger::MCShare share(MCHolder::HdHold);
        share.SetBytes(static_cast<size_t>(3 * MB));
        CHECK(Since(MCHolder::HdHold, before) == Figures(3 * MB, 3 * MB));

        // Replaced, not added to.
        share.Set({.Payload = MB, .Allocated = 2 * MB, .Retired = MB / 2});
        CHECK(Since(MCHolder::HdHold, before) == Figures(MB, 2 * MB, MB / 2));

        // Moved: counted once, by the share it moved to.
        MCMemoryLedger::MCShare moved = std::move(share);
        CHECK(Since(MCHolder::HdHold, before) == Figures(MB, 2 * MB, MB / 2));
        share.SetBytes(static_cast<size_t>(MB));
        CHECK(Since(MCHolder::HdHold, before) == Figures(MB, 2 * MB, MB / 2));

        // A share moved onto another lets go of what that one counted.
        MCMemoryLedger::MCShare other(MCHolder::HdHold);
        other.SetBytes(static_cast<size_t>(5 * MB));
        other = std::move(moved);
        CHECK(Since(MCHolder::HdHold, before) == Figures(MB, 2 * MB, MB / 2));
    }

    CHECK(Since(MCHolder::HdHold, before) == MCFigures{});

    // Each holder named, on its side.
    CHECK_EQ(MCMemoryLedger::NameOf(MCHolder::HdFrames), "hd frames");
    CHECK(MCMemoryLedger::SideOf(MCHolder::ArchiveTables) == MCMemoryLedger::MCSide::Cpu);
    CHECK(MCMemoryLedger::SideOf(MCHolder::GpuTargets) == MCMemoryLedger::MCSide::Gpu);
    CHECK(MCMemoryLedger::PayloadApart(MCHolder::GpuHdPictures));
    CHECK(!MCMemoryLedger::PayloadApart(MCHolder::GpuTargets));
}

TEST_CASE("memory ledger: the line names each holder with bytes, each side's sum, the ceiling and the system's figure")
{
    MCMemoryLedger::MCShare hold(MCHolder::HdHold);
    hold.SetBytes(static_cast<size_t>(700 * MB));
    MCMemoryLedger::MCShare targets(MCHolder::GpuTargets);
    targets.Set({.Payload = 90 * MB, .Allocated = 90 * MB, .Retired = 10 * MB});

    // Shared memory: one pool, half the machine's.
    const std::string shared = MCMemoryLedger::Line({.MachineBytes = 16 * GB, .Shared = true},
                                                    {.Footprint = static_cast<uint64_t>(2500 * MB)});
    CHECK(shared.starts_with("   memory (MB, payload/allocated/retired): cpu: "));
    CHECK(shared.contains("hd hold "));
    CHECK(shared.contains("; gpu: "));
    CHECK(shared.contains("targets "));
    CHECK(shared.contains(" of 8192 (half of 16384, shared)"));
    CHECK(shared.contains("; system: 2500, unnamed "));
    CHECK(!shared.contains("gpu local"));

    // A card of its own: the CPU's side against half the machine, the card's against 90% of its room.
    const std::string card = MCMemoryLedger::Line(
        {.MachineBytes = 8 * GB, .Shared = false, .CardBytes = 4 * GB},
        {.Footprint = static_cast<uint64_t>(1800 * MB), .GpuLocal = static_cast<uint64_t>(900 * MB), .GpuNonLocal = 0});
    CHECK(card.contains(" of 4096 (half of 8192), the card "));
    CHECK(card.contains(" of 3686 (90% of 4096)"));
    CHECK(card.contains("; system: 1800, unnamed "));
    CHECK(card.contains(", gpu local 900, non-local 0, unnamed "));

    CHECK_EQ(MCMemoryLedger::Ceiling({.MachineBytes = 8 * GB}), 4 * GB);
    CHECK_EQ(MCMemoryLedger::CardCeiling(10 * GB), 9 * GB);
}

TEST_CASE("memory ledger: -hdmemory plans as another machine, -hdcard as drawing on a card of its own")
{
    const auto system = std::make_shared<MCFakeMemorySystem>();
    system->Total = 48 * GB;
    system->SharesMemory = true;
    system->Budget = 32 * GB;
    system->Rest = GB;
    system->Held = [] { return GB; };
    MCFakeMemoryScope scope(system);
    CHECK_EQ(MCMemoryRoom::TotalBytes(), 48 * GB);

    {
        MCPlannedScope planned(8 * GB, std::nullopt);
        CHECK_EQ(MCMemoryRoom::TotalBytes(), 8 * GB);
        MCMemoryRoom::MCReader reader{std::string(DeviceName)};
        reader.Frame(0);
        REQUIRE(reader.Card().has_value());
        CHECK(reader.Card()->SharesMemory);
        CHECK(MCMemoryRoom::LedgerLine().contains(" of 4096 (half of 8192, shared)"));
    }

    CHECK_EQ(MCMemoryRoom::TotalBytes(), 48 * GB);

    {
        // The card's room the driver gives the game, holding what the system says the game holds on the GPU.
        MCPlannedScope planned(16 * GB, 4 * GB);
        MCMemoryRoom::MCReader reader{std::string(DeviceName)};
        CHECK_EQ(reader.Frame(GB).PictureBytes, 4 * GB - system->Rest);
        REQUIRE(reader.Card().has_value());
        CHECK(!reader.Card()->SharesMemory);
        CHECK_EQ(reader.Card()->Budget, 4 * GB);
        CHECK_EQ(reader.Card()->Used, system->Rest + GB);
        CHECK(MCMemoryRoom::LedgerLine().contains(" of 8192 (half of 16384), the card "));
        CHECK(MCMemoryRoom::LedgerLine().contains(" of 3686 (90% of 4096)"));
    }

    MCMemoryRoom::MCReader reader{std::string(DeviceName)};
    reader.Frame(0);
    REQUIRE(reader.Card().has_value());
    CHECK(reader.Card()->SharesMemory);
}

TEST_CASE("memory ledger: a GPU texture's bytes are its format's blocks over its size and every level")
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;

    // BC7: 16 bytes a 4x4 block, a byte an HD pixel; a side not a whole number of blocks is padded to one.
    info.format = SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;
    info.width = 2048;
    info.height = 2048;
    CHECK_EQ(MCGpuCounted::TextureBytes(info), uint64_t{2048} * 2048);
    info.width = 6;
    info.height = 6;
    CHECK_EQ(MCGpuCounted::TextureBytes(info), uint64_t{4} * 16);

    // The indices: R8G8, 2 bytes an original pixel.
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
    info.width = 683;
    info.height = 683;
    CHECK_EQ(MCGpuCounted::TextureBytes(info), uint64_t{683} * 683 * 2);

    // A presenter's target with its levels: each half the last, down to a pixel.
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.width = 256;
    info.height = 64;
    info.num_levels = 3;
    CHECK_EQ(MCGpuCounted::TextureBytes(info), uint64_t{256 * 64 + 128 * 32 + 64 * 16} * 4);
}

TEST_CASE("memory ledger: an archive's tables count while it's open, the HD art's frames as each frame starts")
{
    if (!MCTestGame::Available())
    {
        std::cout << "  (skipped: no game install)\n";
        return;
    }

    const MCFigures before = MCMemoryLedger::Of(MCHolder::ArchiveTables);
    auto archive = MCFastFile::Create((MCFileSystem::GameRoot() / "MISSION.FST").string());
    REQUIRE(archive.has_value());
    const MCFigures open = Since(MCHolder::ArchiveTables, before);
    const auto entries = static_cast<int64_t>((*archive)->GetEntries().size());
    std::cout << std::format("  MISSION.FST: {} entries, {} bytes counted ({} allocated)\n", entries, open.Payload,
                             open.Allocated);
    CHECK(entries > 0);
    CHECK(open.Payload >= entries * static_cast<int64_t>(sizeof(MCFileEntry)));
    CHECK(open.Allocated >= open.Payload);
    archive->reset();
    CHECK(Since(MCHolder::ArchiveTables, before) == MCFigures{});

    MCHdArt::NextFrame(false);
    CHECK_EQ(MCMemoryLedger::Of(MCHolder::HdFrames).Payload, static_cast<int64_t>(MCHdArt::CachedBytes()));
}

/// <summary>
/// A real GPU texture counted from its making to its release, retired while a frame still holds it; and the system's
/// figure for the game (macOS's physical footprint, Windows' private bytes).
/// </summary>
TEST_CASE_ISOLATED("memory ledger: a GPU object counts from its making to its release, retired while a frame holds it")
{
    constexpr uint32_t Side = 1024;
    constexpr int64_t TextureBytes = int64_t{Side} * Side * 4;

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

    const MCFigures before = MCMemoryLedger::Of(MCHolder::GpuTargets);
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = Side;
    info.height = Side;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;

    {
        std::shared_ptr<SDL_GPUTexture> owned = MCGpuCounted::MakeTexture(device, info, MCHolder::GpuTargets);
        REQUIRE(owned != nullptr);
        CHECK(Since(MCHolder::GpuTargets, before) == Figures(TextureBytes, TextureBytes));

        // Let go into a frame's retired list: retired until the frame lets go too.
        const std::shared_ptr<SDL_GPUTexture> frame = owned;
        MCGpuCounted::Retire(owned);
        MCGpuCounted::Retire(owned);
        owned.reset();
        CHECK(Since(MCHolder::GpuTargets, before) == Figures(0, 0, TextureBytes));
    }

    CHECK(Since(MCHolder::GpuTargets, before) == MCFigures{});

    // The HD pictures' textures add to their allocations alone: their payload is the pictures placed in them.
    const MCFigures picturesBefore = MCMemoryLedger::Of(MCHolder::GpuHdPictures);

    {
        const MCCountedGpuHandle<SDL_GPUTexture> page =
            MCGpuCounted::MakeTexture(device, info, MCHolder::GpuHdPictures);
        REQUIRE(page != nullptr);
        CHECK(Since(MCHolder::GpuHdPictures, picturesBefore) == Figures(0, TextureBytes));
    }

    CHECK(Since(MCHolder::GpuHdPictures, picturesBefore) == MCFigures{});

    const std::shared_ptr<MCMemoryRoom::MCSystem> system = MCMakeMemorySystem();
    const std::optional<MCMemoryLedger::MCProcessFigures> process = system->Process();
    REQUIRE(process.has_value());
    std::cout << std::format("  the system's figure for the tests: {} MB\n", process->Footprint / uint64_t(MB));
    CHECK(process->Footprint > 0);
    device.reset();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

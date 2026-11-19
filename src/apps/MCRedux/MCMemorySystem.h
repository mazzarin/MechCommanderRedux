#pragma once

#include "platform/MCMemoryRoom.h"

#include <cstdint>
#include <memory>
#include <optional>

// Port-only (the personal build): what this platform says the HD art frames may take (platform/MCMemoryRoom.h): the
// Mac's (MCMemorySystemMac.mm: the system's memory pressure, Metal's working set on the device SDL draws with) or
// Windows' (MCMemorySystemWin.cpp: the system's memory notifications, DXGI's video memory budget on the adapter SDL's
// Vulkan device runs on).

/// <summary>This platform's system, for MCMemoryRoom::SetSystem.</summary>
std::shared_ptr<MCMemoryRoom::MCSystem> MCMakeMemorySystem();

/// <summary>The least a GPU's own memory is to count as a card of its own (integrated graphics report less).</summary>
inline constexpr uint64_t OwnCardLeast = uint64_t{1} << 30;

/// <summary>
/// The memory of the machine's graphics card, one of its own, for the HD art it takes by default
/// (MCContentUpdater::MCMachine): Windows' largest hardware adapter's own memory by DXGI, the Mac's GPU's working set
/// when it doesn't share the machine's memory; none when the GPU shares the machine's (Apple silicon, integrated
/// graphics: under OwnCardLeast of its own), or nothing says.
/// </summary>
std::optional<uint64_t> MCOwnCardBytes();

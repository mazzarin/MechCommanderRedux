#pragma once

#include "platform/MCMemoryRoom.h"

#include <memory>

// Port-only (the personal build): what this platform says the HD art frames may take (platform/MCMemoryRoom.h): the
// Mac's (MCMemorySystemMac.mm: the system's memory pressure, Metal's working set on the device SDL draws with) or
// Windows' (MCMemorySystemWin.cpp: the system's memory notifications, DXGI's video memory budget on the adapter SDL's
// Vulkan device runs on).

/// <summary>This platform's system, for MCMemoryRoom::SetSystem.</summary>
std::shared_ptr<MCMemoryRoom::MCSystem> MCMakeMemorySystem();

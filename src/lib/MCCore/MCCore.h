#pragma once

// Standard headers every MCCore header relies on. The core library, the game and the tools all include this first
// (through their stdafx.h), so MCCore headers can stay free of per-file standard includes.
//
// MCCore is the reconstructed game (the original mcx\ tree, one folder per original folder) plus the platform layer
// that replaces Win32 and DirectX with SDL3 (platform\).

// No Windows.h: the reconstructed game calls no Win32 at all (SDL does that), and leaving it out keeps its macros
// (PlaySound, GetObject, RGB, ...) from colliding with the original's names. The Win32 constants the game's logic
// compares against (message ids, virtual-key codes) are in platform/MCWin32Defs.h.

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cctype>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <concepts>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "MCPort.h"
#include "platform/MCWin32Defs.h"
#include "vfx/MCVfx.h"

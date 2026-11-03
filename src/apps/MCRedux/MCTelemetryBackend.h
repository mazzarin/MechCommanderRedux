#pragma once

#include "main/MCTelemetry.h"

#include <filesystem>
#include <memory>

// Port-only (the personal build): the reports' backend (main/MCTelemetry.h) on this platform: the Mac app's
// (MCTelemetryMac.mm: sysctl and IOKit, NSURLSession, the system's crash reports) or Windows'
// (MCTelemetryWin.cpp: the registry and WinHTTP, MCCrashTrace's record). The server is the build's: the Mac app's
// Info.plist names it (MCReportsURL), Windows' MC_REPORTS_URL; a build that names none has no backend and sends nothing.

/// <summary>
/// The backend, keeping the black box in <paramref name="userFolder"/>, or none when the build names no server.
/// </summary>
std::unique_ptr<MCTelemetry::MCBackend> MCMakeTelemetryBackend(const std::filesystem::path& userFolder);

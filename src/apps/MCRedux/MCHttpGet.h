#pragma once

#include "update/MCHttpTransfer.h"

#include <memory>

// Port-only (the personal build): this platform's transfer for the release's host (update/MCHttpSource.h): the Mac's
// NSURLSession (MCHttpGetMac.mm) or Windows' WinHTTP (MCHttpGetWin.cpp), as the reports use.

/// <summary>A transfer of its own (one at a time); its connections are kept for the next file.</summary>
std::unique_ptr<MCHttpSource::MCGet> MCMakeHttpGet();

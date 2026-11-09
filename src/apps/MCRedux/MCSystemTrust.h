#pragma once

#include "platform/MCTlsLink.h"

#include <memory>

// Port-only (Redux HD): this computer's trust in a server's certificates, as its browser judges them, for the TLS links
// (platform/MCTlsLink.h): the Mac's Security framework (MCSystemTrustMac.mm) or Windows' certificate chain engine
// (MCSystemTrustWin.cpp).

/// <summary>The computer's trust: the chain must reach an authority it trusts and name the host.</summary>
std::unique_ptr<MCTlsLink::MCTrust> MCMakeSystemTrust();

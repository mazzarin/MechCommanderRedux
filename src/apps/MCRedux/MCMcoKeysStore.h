#pragma once

#include "network/MCMcoKeys.h"

#include <memory>

// Port-only (Redux HD; SPEC-mco-rooms.md's "The account"): where MechCommander Online's sign-in is kept between starts,
// the system's own store: the Mac's Keychain (MCMcoKeysMac.mm), Windows' Credential Manager (MCMcoKeysWin.cpp). Only
// the tokens and the player's name are kept (network/MCMcoRooms.h's MCMcoKept), never a password.

/// <summary>The system's store for the sign-in.</summary>
std::shared_ptr<MCMcoKeys> MCMakeMcoKeys();

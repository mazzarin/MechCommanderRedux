#pragma once

// Port-only (Redux HD; SPEC-mco-rooms.md's "The account"): what MechCommander Online's sign-in is kept by between
// starts, and where (network/MCMcoRooms.h uses it; the app's MCMcoKeysStore.h is the system's own store). The standard
// library alone, so a platform's store can include it beside its system's headers.

#include <optional>
#include <string>
#include <string_view>

/// <summary>What a sign-in is kept by between starts: its tokens and its player, never its password.</summary>
struct MCMcoKept
{
    std::string ClientId;
    std::string PeerToken;
    /// <summary>The account's session token; none for a guest's profile.</summary>
    std::string AccountToken;
    std::string Username;
    bool operator==(const MCMcoKept&) const = default;
};

/// <summary>Where the sign-in is kept: the system's own store (the Mac's Keychain, Windows' Credential Manager).</summary>
class MCMcoKeys
{
public:
    virtual ~MCMcoKeys() = default;
    /// <summary>The sign-in kept, if one is (and reads as one).</summary>
    virtual std::optional<MCMcoKept> Load() = 0;
    /// <summary>Keeps <paramref name="kept"/> in place of what was; whether it could.</summary>
    virtual bool Save(const MCMcoKept& kept) = 0;
    /// <summary>Forgets the sign-in (SIGN OUT).</summary>
    virtual void Erase() = 0;
};

/// <summary>A sign-in as the system's store keeps it: one line of text (JSON), and back.</summary>
std::string MCMcoKeptText(const MCMcoKept& kept);
std::optional<MCMcoKept> MCMcoKeptFrom(std::string_view text);

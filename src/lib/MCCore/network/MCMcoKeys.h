#pragma once

// Port-only (Redux HD; SPEC-mco-rooms.md's "The account"): what MechCommander Online's sign-in is kept by between
// starts, and where: a file in the user folder (network/MCMcoRooms.h uses it; the tests keep it in memory).

#include <filesystem>
#include <mutex>
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

/// <summary>Where the sign-in is kept.</summary>
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

/// <summary>
/// The sign-in kept in the user folder's <see cref="FileName"/>, which only its owner may read (on the Mac by its
/// permissions; on Windows by the folder's own, in the user's profile). A save is written beside it, flushed and moved
/// over it, so a stop at any moment leaves the old sign-in or the new one, whole. From any thread.
/// </summary>
class MCMcoKeysFile final : public MCMcoKeys
{
public:
    static constexpr std::string_view FileName = "mco-sign-in.json";

    explicit MCMcoKeysFile(const std::filesystem::path& folder) : _File(folder / FileName) {}

    std::optional<MCMcoKept> Load() override;
    bool Save(const MCMcoKept& kept) override;
    void Erase() override;

private:
    std::mutex _Lock;
    std::filesystem::path _File;
};

/// <summary>A sign-in as it's kept: one line of text (JSON), and back.</summary>
std::string MCMcoKeptText(const MCMcoKept& kept);
std::optional<MCMcoKept> MCMcoKeptFrom(std::string_view text);

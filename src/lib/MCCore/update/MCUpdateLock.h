#pragma once

// Only standard headers and update/MCContentList.h (which holds to that too): MCUpdateLock.cpp is built without the
// precompiled header on Windows.
#include "update/MCContentList.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

/// <summary>
/// Port-only: the locks that keep a running copy's files from moving under it (SPEC-install-and-updates.md's Applying
/// an update: Lock), the system's own (flock on the Mac, LockFileEx on Windows), so a copy that quits or crashes lets
/// go:
///
/// - <see cref="GameLock"/>: held shared by every running game, exclusive to swap. While another copy runs, a staged
///   update waits for a later start.
/// - <see cref="StagingLock"/>: exclusive while a copy stages and arms an update, so two copies never write the staged
///   files at once.
///
/// Each file is made the first time it's needed and never replaced or deleted, so every copy locks the same file. A
/// lock is held through one open file: two in one process are as two copies.
/// </summary>
class MCUpdateLock
{
public:
    /// <summary>The locks, in the game folder.</summary>
    static constexpr std::string_view GameLock = MCContentList::InUpdateFolder<"game.lock">;
    static constexpr std::string_view StagingLock = MCContentList::InUpdateFolder<"staging.lock">;

    /// <summary>
    /// The lock <paramref name="name"/> in <paramref name="gameFolder"/>, made with its folder if need be, held by
    /// no one yet; none when it can't be opened (a folder this account can't write, before the lock was made).
    /// </summary>
    static std::unique_ptr<MCUpdateLock> Open(const std::filesystem::path& gameFolder, std::string_view name);

    ~MCUpdateLock();
    MCUpdateLock(const MCUpdateLock&) = delete;
    MCUpdateLock& operator=(const MCUpdateLock&) = delete;

    /// <summary>Takes it exclusive without waiting: false while another holds it, shared or exclusive.</summary>
    bool TryExclusive();

    /// <summary>Holds it shared, waiting while another holds it exclusive (another copy's swap: a second or so).</summary>
    bool Shared();

    /// <summary>Lets go.</summary>
    void Release();

private:
    enum class MCHeld : uint8_t
    {
        None,
        Shared,
        Exclusive,
    };

    explicit MCUpdateLock(intptr_t handle) : _Handle(handle) {}

    /// <summary>The open file: a descriptor on the Mac, a HANDLE on Windows.</summary>
    intptr_t _Handle;
    MCHeld _Held = MCHeld::None;
};

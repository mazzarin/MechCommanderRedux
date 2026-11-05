#pragma once

// Only standard headers: MCSyncedFolder.cpp is built without the precompiled header on Windows.
#include <filesystem>
#include <optional>
#include <string>

/// <summary>
/// Port-only (Windows): the sync client (OneDrive) that keeps <paramref name="folder"/>, found by where it resolves
/// (links and junctions followed, a folder not made yet by the nearest that is): a sync root registered with Windows'
/// Cloud Files API, or the folder OneDrive's own variables name. None when no client syncs it, and on other systems.
/// </summary>
std::optional<std::string> MCSyncedBy(const std::filesystem::path& folder);

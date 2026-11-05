#pragma once

#include "update/MCInstalled.h"
#include "update/MCUpdateJson.h"

/// <summary>
/// Port-only: the installed record (update/MCInstalled.h) as a JSON value, for a file that holds one inside its own (the
/// swap's journal): kept apart so MCInstalled.h needs no JSON library.
/// </summary>
namespace MCInstalled
{
    /// <summary>The record as a JSON value (<see cref="Text"/> writes it).</summary>
    MCUpdateJson::MCJson ToJson(const MCRecord& record);

    /// <summary>The record <paramref name="json"/> holds, every rule checked.</summary>
    std::expected<MCRecord, std::string> FromJson(const MCUpdateJson::MCJson& json);
}

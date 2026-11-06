#pragma once

#include "logistics/MCLogScrollTextObject.h"

class MCFidpSession;

/// <summary>
/// The multiplayer game browser: the open sessions as lines of an <see cref="MCLogScrollTextObject"/> ("name", a tab,
/// and the free slots or FULL), one of them selected.
/// </summary>
/// <remarks>Original source: <c>logistics\loggen.cpp</c> (<c>GameList</c>).</remarks>
class MCGameList : public MCLogScrollTextObject
{
public:
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view text) override;

    /// <summary>
    /// Port: the first part of the original's draw: rebuilds the lines from the session list (the selection
    /// highlighted). Called when the sessions or the selection change.
    /// </summary>
    void RebuildLines();

    /// <summary>
    /// A click selects a game (and tells the parent); a refresh (timer event) lists the sessions the session manager
    /// has again: those with players, up to the first one dropped before, dropping the empty ones for good.
    /// </summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>The selected session's GUID, or null (and a refresh when the selection is past the list).</summary>
    std::optional<_GUID> GetSelectedGame();

    /// <summary>Drops the selection, once the list has been filled.</summary>
    void ClearSelection();

    /// <summary>The number of sessions listed, or -1 before the first refresh.</summary>
    int32_t NumSessions() const { return Listed ? static_cast<int32_t>(Sessions.size()) : -1; }

    /// <summary>Whether <paramref name="session"/> was dropped from the lists (it had no players).</summary>
    static bool IsSessionDeleted(const MCFidpSession& session);

    /// <summary>The sessions listed.</summary>
    std::vector<_GUID> Sessions;
    /// <summary>The list has been refreshed at least once.</summary>
    bool Listed = false;
    int32_t SelectedSession = -1;
    /// <summary>The selected session's GUID, to find it again after a refresh.</summary>
    _GUID SelectedGuid = {};

    /// <summary>Sessions dropped from the lists because they had no players (never listed again).</summary>
    static inline std::vector<_GUID> DeletedSessions;
};

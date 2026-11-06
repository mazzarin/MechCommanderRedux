#include "stdafx.h"
#include "logistics/MCGameList.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "network/MCMultiPlayer.h"

namespace
{
    /// <summary>Whether two GUIDs are the same.</summary>
    bool SameGuid(const _GUID& a, const _GUID& b)
    {
        return std::memcmp(&a, &b, sizeof(_GUID)) == 0;
    }
}

auto MCGameList::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view newText) -> int32_t
{
    Sessions.clear();
    Listed = false;
    SelectedSession = -1;
    const int32_t result = MCLogScrollTextObject::Init(xPos, yPos, width, height, newText);
    TabColumn = 0x73;
    HighlightColor[0] = 0x14;
    return result;
}

auto MCGameList::RebuildLines() -> void
{
    Clear();

    if (MultiPlayer() == nullptr)
    {
        return;
    }

    // "name <tab> free slots", or FULL; the selection is highlighted.
    for (int32_t i = 0; i < NumSessions(); i++)
    {
        auto session = MultiPlayer()->SessionManager->FindMatchingSession(Sessions[static_cast<size_t>(i)]);

        if (session == nullptr)
        {
            continue;
        }

        const auto freeSlots =
            static_cast<int32_t>(session->SessionDesc.dwMaxPlayers - session->SessionDesc.dwCurrentPlayers);
        const std::string name = session->SessionDesc.SessionName.value_or("");
        const std::string line =
            freeSlots == 0 ? std::format("{}\tFULL", name) : std::format("{}\t{}", name, freeSlots);

        if (i == SelectedSession)
        {
            Print(line, 0x1f);
            HighlightLine[0] = i;
        }
        else
        {
            Print(line, 0x0c);
        }
    }
}

auto MCGameList::IsSessionDeleted(const MCFidpSession& session) -> bool
{
    return std::ranges::any_of(DeletedSessions, [&](const _GUID& deleted)
                               { return SameGuid(session.SessionDesc.guidInstance, deleted); });
}

auto MCGameList::HandleEvent(MCGuiEvent& event) -> void
{
    const std::shared_ptr<MCMultiPlayer> multiPlayer = MultiPlayer();

    if (event.Type == MCGuiEventType::LeftButtonDown)
    {
        // Selects the clicked session and tells the parent.
        const int32_t rowHeight = Fonts[0][FontIndex]->Height() + 4;
        const int32_t clickX = event.X - GlobalX();
        const int32_t clickY = (FirstPixel + event.Y) - GlobalY();
        int32_t row = 0;

        for (int32_t rowTop = 0;; rowTop += rowHeight)
        {
            if (row >= NumSessions())
            {
                return;
            }

            const tagRECT rect = {1, rowTop, Width() - 0xd, rowTop + rowHeight};

            if (PtInRect(rect, tagPOINT{clickX, clickY}))
            {
                break;
            }

            row++;
        }

        SelectedSession = row;
        SelectedGuid = Sessions[static_cast<size_t>(row)];
        RebuildLines();
        MCGuiEvent selected;
        selected.Type = MCGuiEventType::Focus;
        selected.Data = MCLogNotice::GameSelected;
        ParentObject()->HandleEvent(selected);
    }
    else if (event.Type == MCGuiEventType::Timer)
    {
        Sessions.clear();
        Listed = true;

        if (multiPlayer != nullptr && multiPlayer->SessionManager != nullptr)
        {
            // The sessions that have players (an empty one is remembered as deleted and never listed again). The scan
            // stops at the first deleted session.
            const std::optional<std::vector<std::shared_ptr<MCFidpSession>>> list =
                multiPlayer->SessionManager->GetSessions();

            for (size_t i = 0; list.has_value() && i < list->size(); i++)
            {
                const std::shared_ptr<MCFidpSession>& session = (*list)[i];

                if (IsSessionDeleted(*session))
                {
                    break;
                }

                if (multiPlayer->SessionManager->GetPlayers(session).empty())
                {
                    DeletedSessions.push_back(session->SessionDesc.guidInstance);
                }
                else
                {
                    Sessions.push_back(session->SessionDesc.guidInstance);
                }
            }

            // Keep the selection on its session, or tell the parent it went.
            const auto found =
                std::ranges::find_if(Sessions, [&](const _GUID& guid) { return SameGuid(guid, SelectedGuid); });

            if (found != Sessions.end())
            {
                SelectedSession = static_cast<int32_t>(found - Sessions.begin());
            }
            else
            {
                MCGuiEvent lost;
                lost.Type = MCGuiEventType::Focus;
                lost.Data = MCLogNotice::GameLost;
                lost.LParam = -1;
                ParentObject()->HandleEvent(lost);
                SelectedSession = -1;
            }
        }

        RebuildLines();
    }
}

auto MCGameList::GetSelectedGame() -> std::optional<_GUID>
{
    if (SelectedSession >= NumSessions())
    {
        SelectedSession = -1;
        MCGuiEvent refresh;
        refresh.Clear();
        refresh.Type = MCGuiEventType::Timer;
        HandleEvent(refresh);
        return std::nullopt;
    }

    // With no selection the original returned the entry before the table.
    if (SelectedSession < 0)
    {
        return std::nullopt;
    }

    return Sessions[static_cast<size_t>(SelectedSession)];
}

auto MCGameList::ClearSelection() -> void
{
    if (Listed)
    {
        SelectedSession = -1;
        // OB-085: the original copied the entry before the table as the selected GUID; no session is selected, so it
        // is cleared.
        SelectedGuid = {};
    }
}

#include "stdafx.h"
#include "main/MCMPPlayerLights.h"
#include "main/MCMPPlayerLightsView.h"

auto MCMPPlayerLights::SetNumPlayers(int32_t count) -> void
{
    NumPlayers = count;

    for (const MCGuiHandle<MCMPPlayerLightsView>& handle : Views)
    {
        if (const std::shared_ptr<MCMPPlayerLightsView> view = handle.Lock(); view != nullptr)
        {
            view->FitLights();
        }
    }
}

auto MCMPPlayerLights::SetPlayerID(int32_t light, uint32_t playerID) -> void
{
    if (light < NumPlayers && light >= 0 && light < MaxPlayers)
    {
        PlayerIDs[static_cast<size_t>(light)] = playerID;
    }
}

auto MCMPPlayerLights::SetPlayerStatus(uint32_t playerID, int32_t status) -> void
{
    int32_t light = 0;

    while (light < NumPlayers && light < MaxPlayers && PlayerIDs[static_cast<size_t>(light)] != playerID)
    {
        light++;
    }

    // Original behaviour (OB-099): a player not in the session sets the status of the light after the last one. Port fix:
    // with six lights that index is past PlayerStatus (the original overwrote LightWidth), so it is skipped.
    if (status >= 0 && status < 3 && light < MaxPlayers)
    {
        PlayerStatus[static_cast<size_t>(light)] = status;
    }

    // Every view's timer starts now, so the one showing the lights blinks in the original's step whichever it is.
    if (!TimerRunning && status == 2)
    {
        for (const MCGuiHandle<MCMPPlayerLightsView>& handle : Views)
        {
            if (const std::shared_ptr<MCMPPlayerLightsView> view = handle.Lock(); view != nullptr)
            {
                view->StartBlinkTimer();
            }
        }

        TimerRunning = true;
    }
}

auto MCMPPlayerLights::ShownView() const -> std::shared_ptr<MCMPPlayerLightsView>
{
    for (const MCGuiHandle<MCMPPlayerLightsView>& handle : Views)
    {
        if (const std::shared_ptr<MCMPPlayerLightsView> view = handle.Lock(); view != nullptr && view->IsShowing())
        {
            return view;
        }
    }

    return nullptr;
}

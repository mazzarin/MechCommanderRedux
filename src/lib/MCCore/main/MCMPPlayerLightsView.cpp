#include "stdafx.h"
#include "main/MCMPPlayerLightsView.h"
#include "gui/MCGuiSystem.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCTicker.h"
#include "main/MCGamePaths.h"
#include "main/MCLogistics.h"
#include "main/MCMPPlayerLights.h"
#include "network/MCMultiPlayer.h"

namespace
{
    /// <summary>How often the blink timer fires, in milliseconds.</summary>
    constexpr int32_t BlinkInterval = 500;
}

auto MCMPPlayerLightsView::Init(std::weak_ptr<MCMPPlayerLights> lights) -> void
{
    _Lights = std::move(lights);
    MCLogObject::Init(0xd8, 0, 1, 0x10);
    // One light's width comes from the first player's light.
    MCLogPort firstLight;
    firstLight.Load(ArtPath + "logart\\lsc_p1.tga");
    LightWidth = firstLight.Width();
    LightsPort = std::make_shared<MCLogPort>();
    LightsPort->Load(ArtPath + "logart\\lsc_pn.tga");
    ReadyPort = std::make_shared<MCLogPort>();
    ReadyPort->Load(ArtPath + "logart\\lsc_pg.tga");
    BlinkPort = std::make_shared<MCLogPort>();
    BlinkPort->Load(ArtPath + "logart\\lsc_pg1.tga");
    FitLights();
    ShowGuiWindow(false);
}

auto MCMPPlayerLightsView::Show(bool show, std::optional<int32_t> depth) -> void
{
    // In front, as the original's lights were when added to the screen.
    if (show)
    {
        BringToFront(true);

        if (depth.has_value())
        {
            SetDepth(*depth);
        }
    }

    ShowGuiWindow(show);
}

auto MCMPPlayerLightsView::FitLights() -> void
{
    if (const std::shared_ptr<MCMPPlayerLights> lights = _Lights.lock(); lights != nullptr)
    {
        Resize(LightWidth * lights->NumPlayers, Height());
    }
}

auto MCMPPlayerLightsView::StartBlinkTimer() -> void
{
    GuiSystem().AddTimer(weak_from_this(), BlinkTimer, BlinkInterval, 0, 0, false);
}

auto MCMPPlayerLightsView::Draw() -> void
{
    const std::shared_ptr<MCMPPlayerLights> lights = _Lights.lock();

    if (lights == nullptr)
    {
        return;
    }

    MCPane& target = Lport()->Frame();

    for (int32_t light = 0; light < std::min(lights->NumPlayers, MCMPPlayerLights::MaxPlayers); light++)
    {
        // The numbered light, then the status over it: 1 lit, 2 blinking (while the timer runs).
        const std::shared_ptr<MCLogPort> lightPort = LogScreenArt(std::format("lsc_p{}.tga", light + 1));

        if (lightPort == nullptr)
        {
            continue;
        }

        lightPort->CopyTo(target, lightPort->Width() * light, 0, false);
        const int32_t status = lights->PlayerStatus[static_cast<size_t>(light)];

        if (status == 1)
        {
            if (const std::shared_ptr<MCLogPort> statusPort = LogScreenArt("lsc_ph.tga"))
            {
                statusPort->CopyTo(target, lightPort->Width() * light, 2, true);
            }
        }
        else if (status == 2 && lights->TimerRunning)
        {
            MCLogPort& blink = lights->BlinkOn ? *ReadyPort : *BlinkPort;
            blink.CopyTo(target, LightWidth * light, 2, true);
        }
    }
}

auto MCMPPlayerLightsView::HandleEvent(MCGuiEvent& event) -> void
{
    // The original's lights did nothing while on no screen; a hidden view stands for lights on another one.
    const std::shared_ptr<MCMPPlayerLights> lights = _Lights.lock();

    if (lights == nullptr || !IsShowing())
    {
        return;
    }

    if (event.Type == MCGuiEventType::Timer)
    {
        lights->BlinkOn = !lights->BlinkOn;
    }

    // Pointing at a light shows its player's name on the ticker.
    const int32_t light = (event.X - 0xd8) / LightWidth;

    if (light >= 0 && light < lights->NumPlayers && light < MCMPPlayerLights::MaxPlayers &&
        GlobalLog()->Ticker != nullptr && MultiPlayer() != nullptr)
    {
        if (const auto player = MultiPlayer()->SessionManager->GetPlayer(lights->PlayerIDs[static_cast<size_t>(light)]))
        {
            GlobalLog()->Ticker->SetString(player->Name);
        }
    }
}

#include "stdafx.h"
#include "main/MCArtModes.h"
#include "engine/MCCraterManager.h"
#include "iface/MCFriendlyMechIcon.h"
#include "iface/MCMechBar.h"
#include "iface/MCTacticalInterface.h"
#include "main/MCGameContext.h"
#include "main/MCLogistics.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCSpriteManager.h"

namespace
{
    /// <summary>HD ART's settings (PREFS "HDArt").</summary>
    constexpr int HdArtOff = 0;
    constexpr int HdArtOn = 1;

    /// <summary>The modes.</summary>
    constexpr std::array<MCArtMode, 5> Modes = {{
        {"original", HdArtOff, HdLightingOff},
        {"hd", HdArtOn, HdLightingOff},
        {"day", HdArtOn, HdLightingOn},
        {"dusk", HdArtOn, HdLightingDusk},
        {"night", HdArtOn, HdLightingNight},
    }};
}

auto ArtModes() -> std::span<const MCArtMode>
{
    return Modes;
}

auto ArtModeNamed(std::string_view name) -> std::optional<MCArtMode>
{
    const auto found =
        std::ranges::find_if(Modes, [name](const MCArtMode& mode) { return MCIEquals(mode.Name, name); });
    return found != Modes.end() ? std::optional(*found) : std::nullopt;
}

auto ShowArtMode(const MCArtMode& mode) -> std::expected<void, std::string>
{
    const int hdArtWas = GHdArt;
    GHdArt = mode.HdArt;

    if (GHdArt != HdArtOff && !MCHdArt::Active())
    {
        GHdArt = hdArtWas;
        return std::unexpected(std::format("{} needs an HD art pack: there is none readable at {}", mode.Name,
                                           MCHdArt::PackFolder().string()));
    }

    GHdLighting = mode.HdLighting;

    if (hdArtWas == HdArtOff && GHdArt != HdArtOff)
    {
        AttachHdArtToLoaded();
    }

    return {};
}

auto AttachHdArtToLoaded() -> void
{
    if (!MCHdArt::Active())
    {
        return;
    }

    if (const std::shared_ptr<MCSpriteManager> sprites = SpriteManager(); sprites != nullptr)
    {
        sprites->AttachHdArt();
    }

    if (MCGameContext::Current().HasCraterManager())
    {
        CraterManager().AttachHdArt();
    }

    if (const std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();
        iface != nullptr && iface->MechBar != nullptr)
    {
        for (const std::shared_ptr<MCFriendlyMechIcon>& button : iface->MechBar->Buttons)
        {
            button->AttachHdArt();
        }
    }

    if (const std::shared_ptr<MCLogistics> logistics = GlobalLog(); logistics != nullptr)
    {
        logistics->AttachHdArt();
    }
}

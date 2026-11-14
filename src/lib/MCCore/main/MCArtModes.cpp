#include "stdafx.h"
#include "main/MCArtModes.h"
#include "gui/MCGuiSystem.h"
#include "engine/MCCraterManager.h"
#include "iface/MCFriendlyMechIcon.h"
#include "iface/MCMechBar.h"
#include "iface/MCTacticalInterface.h"
#include "main/MCGameContext.h"
#include "main/MCLogistics.h"
#include "platform/MCUtf8Path.h"
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

    /// <summary>The player's own HD ART and HD LIGHTING (what PREFS keeps) while a mode is shown.</summary>
    std::optional<MCArtMode> PlayersOwn;

    /// <summary>
    /// Sets HD ART and HD LIGHTING to <paramref name="art"/>'s, tying the shapes loaded so far to the pack when HD art
    /// comes on.
    /// </summary>
    void SetArt(const MCArtMode& art)
    {
        const int hdArtWas = GHdArt;
        GHdArt = art.HdArt;
        GHdLighting = art.HdLighting;

        if (hdArtWas == HdArtOff && GHdArt != HdArtOff)
        {
            AttachHdArtToLoaded();
        }
    }
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
    // MCHdArt::Active asks HD ART too, so it is asked with the mode's.
    const int hdArtWas = GHdArt;
    GHdArt = mode.HdArt;
    const bool drawable = GHdArt == HdArtOff || MCHdArt::Active();
    GHdArt = hdArtWas;

    if (!drawable)
    {
        return std::unexpected(std::format("{} needs an HD art pack: there is none readable at {}", mode.Name,
                                           MCFileSystem::ToUtf8(MCHdArt::PackFolder())));
    }

    if (!PlayersOwn.has_value())
    {
        PlayersOwn = MCArtMode{.HdArt = GHdArt, .HdLighting = GHdLighting};
    }

    SetArt(mode);
    return {};
}

auto EndArtMode() -> void
{
    if (const std::optional<MCArtMode> own = std::exchange(PlayersOwn, std::nullopt); own.has_value())
    {
        SetArt(*own);
    }
}

MCPlayersArtScope::MCPlayersArtScope() : _HdArt(GHdArt), _HdLighting(GHdLighting)
{
    if (PlayersOwn.has_value())
    {
        GHdArt = PlayersOwn->HdArt;
        GHdLighting = PlayersOwn->HdLighting;
    }
}

MCPlayersArtScope::~MCPlayersArtScope()
{
    GHdArt = _HdArt;
    GHdLighting = _HdLighting;
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
        for (const std::shared_ptr<MCFriendlyMechIcon>& button : iface->MechBar->Buttons())
        {
            button->AttachHdArt();
        }
    }

    if (const std::shared_ptr<MCLogistics> logistics = GlobalLog(); logistics != nullptr)
    {
        logistics->AttachHdArt();
        logistics->MakeAhead();
        logistics->MakeLineArtAhead();
    }
    else if (HasGuiSystem())
    {
        // Port-only: the screens' HD pictures read from now (logistics' MakeAhead asks for them after its own).
        GuiSystem().ReadMenuSetAhead();
    }
}

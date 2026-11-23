#include "stdafx.h"
#include "logistics/MCRemadeScreens.h"
#include "main/MCHdSettings.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlScreen.h"

namespace
{
    /// <summary>The PREFS key of HD ART, which the menus' pictures follow.</summary>
    constexpr std::string_view HdArtKey = "HDArt";

    /// <summary>The screen a kind made, while it's prepared or shown.</summary>
    struct MCMade
    {
        MCGuiHandle<MCRmlScreen> Screen;
    };
}

MCOptionalRef<MCRmlMenus> MCRemadeScreens::Menus()
{
    if (const MCOptionalRef<MCRmlMenus> running = MCRmlMenus::Running(); running.has_value())
    {
        return running;
    }

    const bool hd = MCHdSettings::Find(HdArtKey).Value.get() != 0;
    auto started = MCRmlMenus::Start(MCRmlMenus::MCOptions{.Hd = hd});

    if (!started)
    {
        SDL_Log("Menus: the remade screens can't start: %s", started.error().c_str());
        return std::nullopt;
    }

    return started->get();
}

bool MCRemadeScreens::Showing()
{
    const MCOptionalRef<MCRmlMenus> running = MCRmlMenus::Running();
    return running.has_value() && running->get().Menus().Showing();
}

MCScreenFlow::MCScreenKind MCRemadeScreens::Kind(std::string name, MCMake make, std::function<void()> leave)
{
    const auto made = std::make_shared<MCMade>();

    return MCScreenFlow::MCScreenKind{
        .Name = std::move(name),
        .Prepare =
            [made, make = std::move(make)]
        {
            // Made afresh at each press: it reads the settings, saves or games as they are then.
            if (const std::shared_ptr<MCRmlScreen> old = made->Screen.Lock(); old != nullptr)
            {
                old->Close();
            }

            made->Screen.reset();
            const MCOptionalRef<MCRmlMenus> menus = Menus();

            if (!menus.has_value())
            {
                return std::vector<MCScreenChange::MCPiece>{};
            }

            made->Screen = make(menus->get());
            const std::shared_ptr<MCRmlScreen> screen = made->Screen.Lock();

            if (screen == nullptr)
            {
                return std::vector<MCScreenChange::MCPiece>{};
            }

            screen->Back = [] { MCScreenFlow::Back(); };
            return screen->Prepare();
        },
        .Show =
            [made]
        {
            if (const std::shared_ptr<MCRmlScreen> screen = made->Screen.Lock(); screen != nullptr)
            {
                if (!screen->Error().empty())
                {
                    SDL_Log("Menus: %s can't show: %s", screen->DocumentPath().c_str(), screen->Error().c_str());
                }

                screen->Show();
            }
        },
        .Hide =
            [made]
        {
            if (const std::shared_ptr<MCRmlScreen> screen = made->Screen.Lock(); screen != nullptr)
            {
                screen->Close();
            }

            made->Screen.reset();
        },
        // A screen that couldn't be made or read holds nothing up (its error is logged as it shows).
        .Ready =
            [made]
        {
            const std::shared_ptr<MCRmlScreen> screen = made->Screen.Lock();
            return screen == nullptr || !screen->Error().empty() || screen->Ready();
        },
        // The screen's own Leave first (Preferences' CANCEL puts the settings back), then the kind's or the screen below.
        .Leave =
            [made, leave = std::move(leave)]
        {
            if (const std::shared_ptr<MCRmlScreen> screen = made->Screen.Lock(); screen != nullptr)
            {
                screen->Leave();
            }

            if (leave)
            {
                leave();
            }
            else
            {
                MCScreenFlow::ShowBelow();
            }
        },
    };
}

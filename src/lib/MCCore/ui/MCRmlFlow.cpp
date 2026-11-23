#include "stdafx.h"
#include "ui/MCRmlFlow.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "sprite/MCHdArt.h"

namespace
{
    /// <summary>The remade screens made, by their kinds' names.</summary>
    std::vector<std::pair<std::string, MCGuiHandle<MCRmlScreen>>>& MadeScreens()
    {
        static std::vector<std::pair<std::string, MCGuiHandle<MCRmlScreen>>> made;
        return made;
    }

    /// <summary>Whether the menus' RmlUi couldn't start once (said once in the log, not every frame).</summary>
    bool StartFailed = false;

    /// <summary>The kind's screen, made now if it isn't (its menus started).</summary>
    MCGuiHandle<MCRmlScreen> MakeIfNone(const std::string& name, const MCRmlFlow::MCMake& make)
    {
        if (MCGuiHandle<MCRmlScreen> made = MCRmlFlow::Made(name); made)
        {
            return made;
        }

        const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();

        if (!menus.has_value())
        {
            return {};
        }

        MCGuiHandle<MCRmlScreen> made = make(menus->get().Menus());
        std::erase_if(MadeScreens(), [&name](const auto& kept) { return kept.first == name; });
        MadeScreens().emplace_back(name, made);
        return made;
    }
}

MCOptionalRef<MCRmlMenus> MCRmlFlow::Menus()
{
    if (const MCOptionalRef<MCRmlMenus> running = MCRmlMenus::Running(); running.has_value())
    {
        running->get().SetHd(GHdArt != 0);
        return running;
    }

    int width = MCRmlMenus::ScreenWidth;
    int height = MCRmlMenus::ScreenHeight;

    if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
    {
        SDL_GetWindowSizeInPixels(&display->Window(), &width, &height);
    }

    auto started = MCRmlMenus::Start(MCRmlMenus::MCOptions{.Width = width, .Height = height, .Hd = GHdArt != 0});

    if (!started)
    {
        if (!StartFailed)
        {
            SDL_Log("Menus: the remade screens can't start: %s", started.error().c_str());
            StartFailed = true;
        }

        return std::nullopt;
    }

    return MCOptionalRef<MCRmlMenus>(started->get());
}

MCGuiHandle<MCRmlScreen> MCRmlFlow::Made(std::string_view name)
{
    const auto found = std::ranges::find(MadeScreens(), name, &std::pair<std::string, MCGuiHandle<MCRmlScreen>>::first);
    return found != MadeScreens().end() && found->second ? found->second : MCGuiHandle<MCRmlScreen>{};
}

MCScreenFlow::MCScreenKind MCRmlFlow::Kind(std::string name, MCMake make)
{
    const auto screen = [name, make = std::move(make)] { return MakeIfNone(name, make); };
    return {.Name = name,
            .Prepare =
                [screen]
            {
                const MCGuiHandle<MCRmlScreen> made = screen();
                return made ? made->Prepare() : std::vector<MCScreenChange::MCPiece>{};
            },
            .Show =
                [screen]
            {
                if (const MCGuiHandle<MCRmlScreen> made = screen(); made)
                {
                    // Read now if the change had no time to (a change made at once, with nothing shown).
                    if (!made->Document().has_value())
                    {
                        for (const MCScreenChange::MCPiece& piece : made->Prepare())
                        {
                            piece.Run();
                        }
                    }

                    made->Show();
                }
            },
            .Hide =
                [name]
            {
                if (const MCGuiHandle<MCRmlScreen> made = Made(name); made)
                {
                    made->Hide();
                }
            },
            .Ready =
                [name]
            {
                const MCGuiHandle<MCRmlScreen> made = Made(name);
                return !made || made->Ready() || !made->Error().empty();
            },
            .Shows =
                [name]
            {
                const MCGuiHandle<MCRmlScreen> made = Made(name);
                return made && made->Shown();
            }};
}

void MCRmlFlow::Tend()
{
    if (const MCOptionalRef<MCRmlMenus> running = MCRmlMenus::Running(); running.has_value())
    {
        running->get().Menus().Tend();
    }
}

void MCRmlFlow::OldScreenShown()
{
    if (const MCOptionalRef<MCRmlMenus> running = MCRmlMenus::Running();
        running.has_value() && running->get().Menus().Showing())
    {
        SDL_Log("Menus: a 1998 screen shows: the remade screens hide");
        running->get().Menus().HideAll();
    }
}

bool MCRmlFlow::Showing()
{
    const MCOptionalRef<MCRmlMenus> running = MCRmlMenus::Running();
    return running.has_value() && running->get().Menus().Showing();
}

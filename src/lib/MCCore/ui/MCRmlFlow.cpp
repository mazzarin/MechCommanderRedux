#include "stdafx.h"
#include "ui/MCRmlFlow.h"
#include "gui/MCScreenChange.h"
#include "logistics/MCIdleCheck.h"
#include "main/MCHdSettings.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlScreen.h"

namespace
{
    /// <summary>The PREFS key of HD ART, which the menus' pictures follow.</summary>
    constexpr std::string_view HdArtKey = "HDArt";

    using MCMadeScreen = std::pair<std::string, MCGuiHandle<MCRmlScreen>>;

    /// <summary>The remade screens made, by their kinds' names.</summary>
    std::vector<MCMadeScreen>& MadeScreens()
    {
        static std::vector<MCMadeScreen> made;
        return made;
    }

    /// <summary>Whether the menus' RmlUi couldn't start once (said once in the log, not every frame).</summary>
    bool StartFailed = false;

    /// <summary>Whether the menus' pictures are HD, as PREFS' HD ART says.</summary>
    bool HdArt()
    {
        return MCHdSettings::Find(HdArtKey).Value.get() != 0;
    }

    /// <summary>Closes the kind's screen, if one is made (an afresh kind's as it hides, or before it's made again).</summary>
    void CloseMade(const std::string& name)
    {
        if (const MCGuiHandle<MCRmlScreen> made = MCRmlFlow::Made(name); made)
        {
            if (const std::shared_ptr<MCRmlScreen> screen = made.Lock(); screen != nullptr)
            {
                screen->Close();
            }
        }

        std::erase_if(MadeScreens(), [&name](const MCMadeScreen& kept) { return kept.first == name; });
    }

    /// <summary>The kind's screen made now (its menus started); none when they can't start.</summary>
    MCGuiHandle<MCRmlScreen> MakeNow(const std::string& name, const MCRmlFlow::MCMake& make)
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();

        if (!menus.has_value())
        {
            return {};
        }

        MCGuiHandle<MCRmlScreen> made = make(menus->get().Menus());
        std::erase_if(MadeScreens(), [&name](const MCMadeScreen& kept) { return kept.first == name; });
        MadeScreens().emplace_back(name, made);
        return made;
    }

    /// <summary>The kind's screen, made now if it isn't.</summary>
    MCGuiHandle<MCRmlScreen> MakeIfNone(const std::string& name, const MCRmlFlow::MCMake& make)
    {
        if (MCGuiHandle<MCRmlScreen> made = MCRmlFlow::Made(name); made)
        {
            return made;
        }

        return MakeNow(name, make);
    }
}

MCOptionalRef<MCRmlMenus> MCRmlFlow::Menus()
{
    if (const MCOptionalRef<MCRmlMenus> running = MCRmlMenus::Running(); running.has_value())
    {
        running->get().SetHd(HdArt());
        return running;
    }

    int width = MCRmlMenus::ScreenWidth;
    int height = MCRmlMenus::ScreenHeight;

    if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
    {
        SDL_GetWindowSizeInPixels(&display->Window(), &width, &height);
    }

    auto started = MCRmlMenus::Start(MCRmlMenus::MCOptions{.Width = width, .Height = height, .Hd = HdArt()});

    if (!started)
    {
        if (!StartFailed)
        {
            SDL_Log("Menus: the remade screens can't start: %s", started.error().c_str());
            StartFailed = true;
        }

        return std::nullopt;
    }

    // A change of screen holds the player's clicks and keys until the next screen is in, as the 1998 screens' are
    // (gui/MCScreenChange.h): a second ENTER on a screen fading out presses nothing.
    started->get().HoldInput([] { return MCScreenChange::HoldsInput(); });
    // The player's input on a remade screen counts as on the 1998 ones (an internet game's idle check: INTERNET keeps
    // its list, a room doesn't ask "Still there?").
    started->get().OnPlayerInput([] { MCIdleCheck::Input(); });
    return MCOptionalRef<MCRmlMenus>(started->get());
}

void MCRmlFlow::Stop()
{
    MadeScreens().clear();
    StartFailed = false;
    MCRmlMenus::Stop();
}

MCGuiHandle<MCRmlScreen> MCRmlFlow::Made(std::string_view name)
{
    const auto found = std::ranges::find(MadeScreens(), name, &MCMadeScreen::first);
    return found != MadeScreens().end() && found->second ? found->second : MCGuiHandle<MCRmlScreen>{};
}

MCScreenFlow::MCScreenKind MCRmlFlow::Kind(std::string name, MCMake make, MCKeep keep)
{
    const auto screen = [name, make] { return MakeIfNone(name, make); };
    return {.Name = name,
            .Prepare =
                [name, make, keep, screen]
            {
                MCGuiHandle<MCRmlScreen> made;

                // Afresh: the screen made at the press, so it reads what it shows as it is now.
                if (keep == MCKeep::Afresh)
                {
                    CloseMade(name);
                    made = MakeNow(name, make);
                }
                else
                {
                    made = screen();
                }

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

                    if (!made->Error().empty())
                    {
                        SDL_Log("Menus: %s can't show: %s", made->DocumentPath().c_str(), made->Error().c_str());
                    }

                    made->Show();
                }
            },
            .Hide =
                [name, keep]
            {
                if (keep == MCKeep::Afresh)
                {
                    CloseMade(name);
                }
                else if (const MCGuiHandle<MCRmlScreen> made = Made(name); made)
                {
                    made->Hide();
                }
            },
            // A screen that couldn't be made or read holds nothing up (its error is logged as it shows).
            .Ready =
                [name]
            {
                const MCGuiHandle<MCRmlScreen> made = Made(name);
                return !made || made->Ready() || !made->Error().empty();
            },
            // The screen's own Leave first (CANCEL putting settings back), then the screen below.
            .Leave =
                [name]
            {
                if (const MCGuiHandle<MCRmlScreen> made = Made(name); made)
                {
                    made->Leave();
                }

                MCScreenFlow::ShowBelow();
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
        // A dialog's say a change of screens hid comes back once the screens have changed.
        MCRmlDialogs::Tend();
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

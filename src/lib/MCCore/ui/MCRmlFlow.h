#pragma once

#include "gui/MCScreenFlow.h"
#include "lib/MCOptionalRef.h"
#include "ui/MCRmlMenus.h"

// Port-only (SPEC-menus.md, the remade screens in the game): the game's menus' RmlUi, started over its display the
// first time a remade screen is wanted and stopped at the game's end, and each remade screen as one of the screen flow's
// kinds (gui/MCScreenFlow.h), so it and the 1998 screens hand over to each other: made on the menus' layer at its
// preparation, its document read and laid out live from the press, shown once the screen before is dark. The player's
// clicks and keys are held while a change fades, as the 1998 screens' are. An old screen coming up
// (MCLogistics::SetCurrentScreen) hides the remade ones over it.

namespace MCRmlFlow
{
    /// <summary>
    /// The game's menus' RmlUi, started over the display at its window's size with HD art as PREFS' HD ART has it; none
    /// when it can't start (the log says why, once).
    /// </summary>
    MCOptionalRef<MCRmlMenus> Menus();

    /// <summary>Stops the game's menus (the game's end): its screens destroyed, RmlUi shut down.</summary>
    void Stop();

    /// <summary>What the player is told when the menus' files don't load, before how to repair the copy.</summary>
    inline constexpr std::string_view FilesMissing = "Redux HD's menu files are missing.";

    /// <summary>
    /// At the game's start, before anything opens: whether the menus' files in <paramref name="folder"/> (the app's own:
    /// the program brings them, so a copy without them is damaged) load (MCRmlMenus::CheckFiles). When they don't, the
    /// log says which and why, <paramref name="tell"/> is given <see cref="FilesMissing"/> for the player, and the game
    /// doesn't start: no menus' RmlUi is left started, and no screen is shown. There are no other screens to play from.
    /// </summary>
    bool FilesLoad(const std::filesystem::path& folder, const std::function<void(std::string_view)>& tell);

    /// <summary>Makes a remade screen on the menus' layer (its document, its keys, its Back).</summary>
    using MCMake = std::function<MCGuiHandle<MCRmlScreen>(MCRmlLayer& layer)>;

    /// <summary>How long a kind's screen lives.</summary>
    enum class MCKeep
    {
        /// <summary>Made at its first preparation and kept (a list's link and pick held across a room).</summary>
        Kept,
        /// <summary>Made at each press and closed as it hides: it reads what it shows as it is then (the settings).</summary>
        Afresh
    };

    /// <summary>
    /// The remade screen <paramref name="make"/> makes, as the flow's kind named <paramref name="name"/>, kept or made
    /// afresh by <paramref name="keep"/>; it shows while its document does. Its Leave: the screen's own, then the
    /// screen below (a caller may set its own).
    /// </summary>
    MCScreenFlow::MCScreenKind Kind(std::string name, MCMake make, MCKeep keep);

    /// <summary>The screen kind <paramref name="name"/> made (none before its preparation, or once closed).</summary>
    MCGuiHandle<MCRmlScreen> Made(std::string_view name);

    /// <summary>The game's frame for the remade screens shown (MCGuiSystem::RunFrameCallbacks): each's OnFrame.</summary>
    void Tend();

    /// <summary>An old screen came up (MCLogistics::SetCurrentScreen): the menus' remade screens hide.</summary>
    void OldScreenShown();

    /// <summary>Whether a remade screen shows on the menus' layer (an old dialog would show under it).</summary>
    bool Showing();

    /// <summary>The layer a screen the game shows itself is made on.</summary>
    enum class MCOn
    {
        /// <summary>The menus' layer.</summary>
        Menus,
        /// <summary>Port-only (stage 9): the mission's, over the battlefield at its INTERFACE SIZE (the results).</summary>
        Mission
    };

    /// <summary>
    /// The screen <paramref name="handle"/> holds, made on the layer <paramref name="on"/> names if it isn't (or was
    /// closed), its document not read by this; none when the menus can't start or it can't be made (the log says why,
    /// naming it <paramref name="what"/>).
    /// </summary>
    template <class T>
    std::shared_ptr<T> MadeOnLayer(MCGuiHandle<T>& handle, std::string_view what, MCOn on = MCOn::Menus)
    {
        const MCOptionalRef<MCRmlMenus> menus = Menus();

        if (!menus.has_value())
        {
            return nullptr;
        }

        if (handle.Lock() == nullptr)
        {
            MCRmlLayer& layer = on == MCOn::Mission ? menus->get().Mission() : menus->get().Menus();
            handle = layer.template CreateChild<T>([](T&) {});
        }

        std::shared_ptr<T> screen = handle.Lock();

        if (screen == nullptr)
        {
            SDL_Log("Menus: %.*s couldn't be made", static_cast<int>(what.size()), what.data());
        }

        return screen;
    }

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 8): the screen <paramref name="handle"/> holds, made on the layer
    /// <paramref name="on"/> names if it isn't (or was closed) and its document read and laid out at once if a change's
    /// job hasn't (<see cref="ReadAhead"/>), for a screen the game shows itself rather than through a change (the
    /// briefing over the 1998 one, the loading screen, the results); none when the menus can't start or its document
    /// can't be read (the log says why, naming it <paramref name="what"/>).
    /// </summary>
    template <class T> MCOptionalRef<T> MadeNow(MCGuiHandle<T>& handle, std::string_view what, MCOn on = MCOn::Menus)
    {
        const std::shared_ptr<T> screen = MadeOnLayer(handle, what, on);

        if (screen == nullptr)
        {
            return std::nullopt;
        }

        if (!screen->Document().has_value())
        {
            for (const MCScreenChange::MCPiece& piece : screen->Prepare())
            {
                piece.Run();
            }
        }

        if (!screen->Error().empty())
        {
            SDL_Log("Menus: %.*s can't show: %s", static_cast<int>(what.size()), what.data(), screen->Error().c_str());
            return std::nullopt;
        }

        return *screen;
    }

    /// <summary>
    /// The pieces that read and lay out the document of the screen <paramref name="handle"/> holds (made on the menus'
    /// layer if it isn't), for the job of a change that shows it (<see cref="MadeNow"/>) once the screen is dark: run
    /// live from the press, so the dark has none of it. None when it is read already, or can't be made.
    /// </summary>
    template <class T> std::vector<MCScreenChange::MCPiece> ReadAhead(MCGuiHandle<T>& handle, std::string_view what)
    {
        const std::shared_ptr<T> screen = MadeOnLayer(handle, what);
        return screen != nullptr && !screen->Document().has_value() ? screen->Prepare()
                                                                    : std::vector<MCScreenChange::MCPiece>{};
    }

    /// <summary>The screen <paramref name="handle"/> holds while it lives; none once closed.</summary>
    template <class T> MCOptionalRef<T> Alive(const MCGuiHandle<T>& handle)
    {
        const std::shared_ptr<T> screen = handle.Lock();
        return screen != nullptr ? MCOptionalRef<T>(*screen) : std::nullopt;
    }
}

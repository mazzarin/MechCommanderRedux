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
}

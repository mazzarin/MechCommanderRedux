#pragma once

#include "gui/MCScreenFlow.h"
#include "lib/MCOptionalRef.h"
#include "ui/MCRmlMenus.h"

// Port-only (SPEC-menus.md's stage 3, the first remade screens in the game): the game's menus' RmlUi, started over its
// display the first time a remade screen is wanted, and each remade screen as one of the screen flow's kinds
// (gui/MCScreenFlow.h), so it and the 1998 screens hand over to each other: made on the menus' layer at its first
// preparation and kept (the menus' data outlives a mission), its document read and laid out live from the press, shown
// once the screen before is dark. An old screen coming up (MCLogistics::SetCurrentScreen) hides the remade ones over it.

namespace MCRmlFlow
{
    /// <summary>
    /// The game's menus' RmlUi, started over the display at its window's size with HD art as the game has it; none when
    /// it can't start (the log says why).
    /// </summary>
    MCOptionalRef<MCRmlMenus> Menus();

    /// <summary>Makes a remade screen on the menus' layer (its document, its keys, its Back).</summary>
    using MCMake = std::function<MCGuiHandle<MCRmlScreen>(MCRmlLayer& layer)>;

    /// <summary>
    /// The remade screen <paramref name="make"/> makes, as the flow's kind named <paramref name="name"/>: made at its
    /// first Prepare and kept; it shows while its document does.
    /// </summary>
    MCScreenFlow::MCScreenKind Kind(std::string name, MCMake make);

    /// <summary>The screen kind <paramref name="name"/> made (none before its first Prepare).</summary>
    MCGuiHandle<MCRmlScreen> Made(std::string_view name);

    /// <summary>The game's frame for the remade screens shown (MCGuiSystem::RunFrameCallbacks): each's OnFrame.</summary>
    void Tend();

    /// <summary>An old screen came up (MCLogistics::SetCurrentScreen): the menus' remade screens hide.</summary>
    void OldScreenShown();

    /// <summary>Whether a remade screen shows on the menus' layer (an old dialog would show under it).</summary>
    bool Showing();
}

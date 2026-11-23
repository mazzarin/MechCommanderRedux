#pragma once

#include "gui/MCGuiRef.h"
#include "gui/MCScreenFlow.h"
#include "lib/MCOptionalRef.h"

class MCRmlMenus;
class MCRmlScreen;

// Port-only (SPEC-menus.md, "How it's built"): the remade screens in the game. The game's menus' RmlUi is started the
// first time a remade screen is wanted, and a remade screen is one of the screens' flow's kinds (gui/MCScreenFlow.h),
// so it and the old screens hand over to each other until the last is remade.

namespace MCRemadeScreens
{
    /// <summary>
    /// The game's menus' RmlUi (ui/MCRmlMenus), started over the game's display with its HD ART the first time it's
    /// wanted; none, the reason logged, when it can't start.
    /// </summary>
    MCOptionalRef<MCRmlMenus> Menus();

    /// <summary>Whether a remade screen shows on the menus' layer (the old screen under it stays hidden).</summary>
    bool Showing();

    /// <summary>What makes a remade screen on the menus' layer (MCRmlLayer::CreateChild).</summary>
    using MCMake = std::function<MCGuiHandle<MCRmlScreen>(MCRmlMenus& menus)>;

    /// <summary>
    /// A remade screen's kind named <paramref name="name"/>: made by <paramref name="make"/> at the press, its document
    /// read, laid out and recorded a piece at a time while the screen before fades out; shown in the dark, polled until
    /// its first frame is whole, closed once another shows. Its Back (Escape and its way back) is the flow's: leaving
    /// runs the screen's own Leave (MCRmlScreen::Leave), then <paramref name="leave"/> (none: the screen below shows).
    /// </summary>
    MCScreenFlow::MCScreenKind Kind(std::string name, MCMake make, std::function<void()> leave = {});
}

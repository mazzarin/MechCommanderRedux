#pragma once

// Port-only (SPEC-menus.md's stage 9): what the mission's results read of the battle's interface, in one place: a pilot's
// card on the mech bar, and the salvage (the tactical map's list, and its SALVAGE page's text). The battle's model
// (menus-hud's, SPEC-menus.md's "How it's built") is to hold these; when it does, these read it, and nothing else of the
// results changes.

class MCFriendlyMechIcon;
class MCGameObject;
class MCGuiScrollTextObject;
class MCMechWarrior;

namespace MCResultsReads
{
    /// <summary>The card the mech bar shows for <paramref name="warrior"/>'s unit; none when its unit has none.</summary>
    std::shared_ptr<MCFriendlyMechIcon> Card(const MCMechWarrior& warrior);

    /// <summary>
    /// What the mission salvaged, in the order it was found: the tactical map's salvage list, each object pinned (one
    /// gone from the world left out), as the campaign's bridge gives them to the player.
    /// </summary>
    std::vector<std::shared_ptr<MCGameObject>> Salvage();

    /// <summary>The SALVAGE page's text box (the 1998 results' debriefing showed its text); none without the map.</summary>
    std::shared_ptr<MCGuiScrollTextObject> SalvageText();
}

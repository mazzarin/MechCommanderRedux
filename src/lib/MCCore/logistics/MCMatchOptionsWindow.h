#pragma once

// Port-only (SPEC-menus.md's stage 4, "MATCH OPTIONS"): the match's options as a window over the remade room
// (ui/match_options.rml), the room's VIEW (a guest's) or CHANGE (the host's). FORCES, PLAY and WEAPONS down its side,
// the tab's options beside them: the options the 1998 MATCH OPTIONS tab has (logistics/MCMatchOptionRows.h), no
// others, each with the value the match plays. The host's are switches and steppers, each change the 1998 list's
// (sent as it was); a guest's are their values, with "The host sets these.". An option a player's game doesn't have is
// held, saying whose: "Tell Ann to upgrade.". DONE (the host's) or CLOSE (a guest's), and Escape, go back to the room.

#include "ui/MCRmlScreen.h"

#include <cstdint>
#include <string>
#include <vector>

/// <summary>MATCH OPTIONS' window (<c>ui/match_options.rml</c>).</summary>
class MCMatchOptionsWindow final : public MCRmlScreen
{
public:
    /// <summary>The window's tabs, down its side.</summary>
    enum class MCTab : uint8_t
    {
        Forces,
        Play,
        Weapons,
    };

    MCMatchOptionsWindow();

    /// <summary>Opens over the room at FORCES.</summary>
    void OpenOver();

    /// <summary>An option's row as a player reads it (the tests').</summary>
    struct MCSeenRow
    {
        std::string Name;
        /// <summary>What it covers ("Streak racks too."), or why it's held ("Tell Ann to upgrade."); empty for none.</summary>
        std::string Line;
        /// <summary>The value the match plays ("Mission's", "On").</summary>
        std::string Value;
        /// <summary>Whether the player sets it here (the host's switch or stepper), not only reads it.</summary>
        bool Set = false;
        bool Held = false;
    };

    /// <summary>
    /// Row <paramref name="row"/> of MATCH OPTIONS' rows (MatchOptionRows' index), on whichever tab has it, as it shows
    /// now (each frame's values).
    /// </summary>
    MCSeenRow Seen(int32_t row) const;

    /// <summary>The rows of tab <paramref name="tab"/>, MatchOptionRows' indexes, in the window's order.</summary>
    static std::vector<int32_t> RowsOf(MCTab tab);

    MCTab Tab() const { return _Tab; }
    /// <summary>A tab chosen, as a click on it (or Q and E) would.</summary>
    void ShowTab(MCTab tab);

    /// <summary>
    /// The host's control of row <paramref name="row"/> moved <paramref name="steps"/> choices on (back, when less than
    /// 0) among those the room plays, as a click on a switch's choice or a stepper's arrow would: the 1998 list's change.
    /// Nothing for a held row, or a guest's.
    /// </summary>
    void Step(int32_t row, int32_t steps);

    /// <summary>DONE or CLOSE.</summary>
    void PressDone();

    void OnFrame() override;

protected:
    void OnLoaded(Rml::ElementDocument& document) override;
    void OnShow() override;

private:
    /// <summary>A row as it stands this frame.</summary>
    struct MCRowNow
    {
        int32_t Row = 0;
        /// <summary>The values the room plays, in the order a click steps through them.</summary>
        std::vector<int32_t> Offered;
        /// <summary>Their words.</summary>
        std::vector<std::string> Words;
        /// <summary>The value's index among them; -1 when it's none of them.</summary>
        int32_t Choice = -1;
        std::string Value;
        /// <summary>Whose games hold it, as typed.</summary>
        std::vector<std::string> HeldBy;
        bool Stepper = false;

        bool operator==(const MCRowNow&) const = default;
    };

    /// <summary>Row <paramref name="row"/> as the match plays it now.</summary>
    MCRowNow RowNow(int32_t row) const;
    /// <summary>The tab's rows put in the document again (its layout: the host's controls, or a guest's values).</summary>
    void Build();
    /// <summary>The rows' values read again: the document's rows built again when their layout changed, else set.</summary>
    void Refresh();
    /// <summary>A control's change (its row's index among the offered values), the 1998 list's change of its value.</summary>
    void Changed(int32_t row, int32_t choice);
    bool Hosting() const;

    MCTab _Tab = MCTab::Forces;
    /// <summary>What the document shows: whether as the host's, and each row of the tab.</summary>
    bool _BuiltHosting = false;
    std::vector<MCRowNow> _Built;
};

/// <summary>MATCH OPTIONS' window opened over the remade room; false when it can't be read (the log says why).</summary>
bool ShowMatchOptionsWindow();

/// <summary>MATCH OPTIONS' window (none before it first opened).</summary>
MCGuiHandle<MCMatchOptionsWindow> MatchOptionsWindow();

/// <summary>Whether MATCH OPTIONS' window shows.</summary>
bool MatchOptionsWindowShown();

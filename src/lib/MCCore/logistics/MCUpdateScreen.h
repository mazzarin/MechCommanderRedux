#pragma once

#include "main/MCUpdater.h"
#include "ui/MCRmlScreen.h"

/// <summary>
/// Port-only (SPEC-menus.md, "UPDATE"; stage 6): the UPDATE page remade (<c>ui/update.rml</c>), a screen of the flow
/// opened from the main menu's UPDATE and from INTERNET's newer game: one panel over the painting, titled by the version
/// running; at the left what the new version brings, a line each, scrolling; at the right the version, its download and
/// where the update stands, in the game's words, with the one thing to do about it: SKIP and INSTALL, STOP while its
/// content downloads (RESUME after), RESTART once it's in, RETRY after a failure. BACK and Escape leave the update
/// waiting. It follows the updater every frame (main/MCUpdater.h). A lobby's room keeps the old page until its stage.
/// </summary>
class MCUpdateScreen final : public MCRmlScreen
{
public:
    MCUpdateScreen();

    /// <summary>
    /// UPDATE opened through the screens' flow over the screen shown (the main menu's and the room's UPDATE).
    /// INTERNET's and LAN's newer games open the flow's "update" themselves (MCGamesScreen), setting the game to join once
    /// it's in (network/MCLobbyUpdate.h).
    /// </summary>
    static void Open();

    /// <summary>Whether the remade UPDATE shows (the reports' and the due art's notices wait for it).</summary>
    static bool Shows();

    /// <summary>
    /// What the status line says of <paramref name="status"/> (in sentence case, the game's words), the game running
    /// <paramref name="running"/> ("0.7.2 (1500)").
    /// </summary>
    static std::string StatusText(const MCUpdater::MCStatus& status, std::string_view running);

    void OnFrame() override;

protected:
    void OnLoaded(Rml::ElementDocument& document) override;
    void OnShow() override;
    void OnDestroy() override;
    MCOptionalRef<Rml::Element> FirstFocus(Rml::ElementDocument& document) override;

private:
    /// <summary>What the screen's buttons do now (besides BACK).</summary>
    enum class MCAct : uint8_t
    {
        None,
        Install,
        Skip,
        Stop,
        Resume,
        Restart,
        Retry,
    };

    /// <summary>A button's word for what it does.</summary>
    static std::string_view Word(MCAct act);
    /// <summary>The screen made to show the updater as it is now (its words set only where they changed).</summary>
    void Refresh();
    /// <summary>The release notes, a line each, written again when they changed.</summary>
    void ShowNotes(const MCUpdater::MCStatus& status);
    /// <summary>A button pressed: what it does now.</summary>
    void Press(MCAct act);

    MCAct _Main = MCAct::None;
    MCAct _Second = MCAct::None;
    /// <summary>The notes and status last written (RmlUi lays the document out again for a change).</summary>
    std::string _Notes;
    std::string _Status;
};

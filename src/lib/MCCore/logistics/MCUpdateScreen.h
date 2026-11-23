#pragma once

#include "main/MCUpdater.h"
#include "network/MCLobbyUpdate.h"
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
    /// <summary><paramref name="place"/>: where it opened, over the name ("Main menu", "Internet").</summary>
    explicit MCUpdateScreen(std::string place);

    /// <summary>
    /// UPDATE pressed (the main menu's button, INTERNET's newer game): the remade screen opened through the screens'
    /// flow over the screen shown; the old page when the menus' RmlUi can't start.
    /// </summary>
    static void OpenFromMenus();

    /// <summary>
    /// UPDATE opened through the screens' flow over the screen shown, a newer game picked on INTERNET (<paramref
    /// name="join"/>) joined once its update is in: the game kept for the new version as UPDATE shows (INTERNET's going
    /// forgets one picked before); false when the menus' RmlUi can't start (the caller shows the old page).
    /// </summary>
    static bool Open(std::optional<MCLobbyUpdate::MCJoinAfter> join = std::nullopt);

    /// <summary>Where UPDATE opens from now, as its eyebrow names it: the screen the flow shows ("Internet", else "Main menu").</summary>
    static std::string PlaceOfShown();

    /// <summary>Whether the remade UPDATE shows (the reports' and the due art's notices wait for it).</summary>
    static bool Shows();

    /// <summary>What the status line says of <paramref name="status"/> (in sentence case, the game's words).</summary>
    static std::string StatusText(const MCUpdater::MCStatus& status);

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

    std::string _Place;
    /// <summary>The newer game to join once the update is in, kept as the screen shows.</summary>
    std::optional<MCLobbyUpdate::MCJoinAfter> _Join;
    MCAct _Main = MCAct::None;
    MCAct _Second = MCAct::None;
    /// <summary>The notes and status last written (RmlUi lays the document out again for a change).</summary>
    std::string _Notes;
    std::string _Status;
};

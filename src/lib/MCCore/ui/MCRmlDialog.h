#pragma once

#include "ui/MCRmlScreen.h"

/// <summary>
/// Port-only (SPEC-menus.md's stage 3, "When something goes wrong"): a dialog over the remade screens
/// (<c>ui/dialog.rml</c>): what it belongs to above in cyan, its title when it has one, one short sentence under it,
/// the way back (CANCEL) and the answer (OK, RETRY, UPDATE). Escape is its way back, or its answer when it has only one;
/// one with neither (a wait no one can call off) takes no keys.
/// </summary>
class MCRmlDialog final : public MCRmlScreen
{
public:
    /// <summary>What a dialog says, and what its buttons do.</summary>
    struct MCSay
    {
        /// <summary>What it belongs to: the screen ("Internet"), or the game it names.</summary>
        std::string Eyebrow;
        /// <summary>Its title, when the sentence needs one ("Needs version 0.7.2"); empty for none.</summary>
        std::string Heading;
        /// <summary>The sentence.</summary>
        std::string Text;
        /// <summary>The answer's word ("OK", "Retry", "Update"); empty for none.</summary>
        std::string Main = "OK";
        /// <summary>The way back's word ("Cancel"); empty for none.</summary>
        std::string Other;
        std::function<void()> OnMain;
        std::function<void()> OnOther;
    };

    MCRmlDialog();

    /// <summary>Says <paramref name="say"/>, shown over the screens (the one shown before replaced).</summary>
    void Say(MCSay say);

    /// <summary>What it says now (the tests read it).</summary>
    const MCSay& Said() const { return _Say; }

    /// <summary>Presses its answer, or its way back, as a click would (and the tests).</summary>
    void PressMain();
    void PressOther();

    /// <summary>Text made safe to put in a document (its markup's characters as entities).</summary>
    static std::string Escape(std::string_view text);

protected:
    void OnLoaded(Rml::ElementDocument& document) override;

private:
    /// <summary>The words put in the document, and its buttons shown or not.</summary>
    void Fill();

    MCSay _Say;
};

namespace MCRmlDialogs
{
    /// <summary>The dialog over the menus' remade screens, made once on the menus' layer; none when RmlUi can't start.</summary>
    MCGuiHandle<MCRmlDialog> Dialog();

    /// <summary>Says <paramref name="say"/> over the remade screen shown; false when none shows (the old dialog's turn).</summary>
    bool Show(MCRmlDialog::MCSay say);

    /// <summary>
    /// What the dialogs belong to when they don't say (the remade screen shown: "Internet", "LAN"), above their sentence.
    /// </summary>
    void SetPlace(std::string place);

    /// <summary>The dialog closed (a job's wait ended, the screen under it gone).</summary>
    void Close();

    /// <summary>Whether it shows.</summary>
    bool Shown();

    /// <summary>A wait shown by <see cref="Waiting"/> ended (the 1998 dialog's Deactivate): the dialog closes.</summary>
    void EndWait();

    /// <summary>
    /// An old message (ShowMenuMessage, in the 1998 dialog's capitals) said over the remade screen shown, in sentence
    /// case; OK runs <paramref name="then"/>. False when no remade screen shows.
    /// </summary>
    bool Message(std::string_view text, std::function<void()> then = {});

    /// <summary>A wait with no way out (ShowWaitingDialog: "Opening your room again...") over the remade screen shown.</summary>
    bool Waiting(std::string_view text);

    /// <summary>The 1998 dialog's capitals in sentence case: "BOB CLOSED THE GAME." to "Bob closed the game.".</summary>
    std::string SentenceCase(std::string_view text);

    /// <summary>Text in the 1998 dialog's capitals (no small letter) in sentence case; a sentence already, as it is.</summary>
    std::string AsSentence(std::string_view text);
}

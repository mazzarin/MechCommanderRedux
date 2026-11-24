#pragma once

#include "ui/MCRmlScreen.h"

#include <optional>

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
        /// <summary>
        /// A key it asks for, in the game's key code (the LIGHTS key question): each key pressed but its buttons' (Enter,
        /// Escape) and a modifier alone is offered; none returned takes it (the dialog closes, as its answer does),
        /// words say why not (the dialog says them in its sentence's place). Empty: it takes no keys of its own.
        /// </summary>
        std::function<std::optional<std::string>(uint32_t key)> TakesKey;
        /// <summary>
        /// Port-only (SPEC-menus.md's stage 4): a wait (MCRmlDialogs::Waiting), which MCRmlDialogs::EndWait ends, even
        /// with a way out (a launcher's CANCEL). It stands: see <see cref="OnLost"/>.
        /// </summary>
        bool Wait = false;
        /// <summary>
        /// Port-only (SPEC-menus.md's stage 4): an old dialog's say (MCReusableDialog), which stands as a wait does until
        /// answered or ended: a change of screens hides it, and it says itself again over the next remade screen
        /// (MCRmlDialogs::Tend); with none to say itself over, this shows it the old way instead.
        /// </summary>
        std::function<void()> OnLost;

        /// <summary>Whether a change of screens leaves it standing (a wait, or an old dialog's say).</summary>
        bool Stands() const { return Wait || OnLost != nullptr; }
    };

    MCRmlDialog();

    /// <summary>Says <paramref name="say"/>, shown over the screens (the one shown before replaced).</summary>
    void Say(MCSay say);

    /// <summary>What it says now (the tests read it).</summary>
    const MCSay& Said() const { return _Say; }

    /// <summary>Presses its answer, or its way back, as a click would (and the tests).</summary>
    void PressMain();
    void PressOther();

    /// <summary>Hidden as answered or ended (MCRmlDialogs::Close, EndWait): it won't say itself again.</summary>
    void End();

    bool TakesKeys() const override;
    bool OnRawKey(const SDL_KeyboardEvent& key) override;

    /// <summary>Text made safe to put in a document (its markup's characters as entities).</summary>
    static std::string Escape(std::string_view text);

protected:
    void OnLoaded(Rml::ElementDocument& document) override;
    void OnHide() override;

private:
    /// <summary>The words put in the document, and its buttons shown or not.</summary>
    void Fill();

    MCSay _Say;
    /// <summary>Shown, and not yet answered or ended: hidden now, a change of screens hid it.</summary>
    bool _Standing = false;
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
    /// Port-only (SPEC-menus.md's stage 4): whether a say that stands (MCRmlDialog::MCSay::Stands) was hidden by a change
    /// of screens and says itself again at <see cref="Tend"/>; with <paramref name="text"/>, whether that say is it.
    /// </summary>
    bool Returning(std::optional<std::string_view> text = std::nullopt);

    /// <summary>
    /// Each frame, after the screens': a say a change of screens hid says itself again over the remade screen shown, or,
    /// with none, its old dialog shows it (MCRmlDialog::MCSay::OnLost).
    /// </summary>
    void Tend();

    /// <summary>
    /// An old message (ShowMenuMessage, in the 1998 dialog's capitals) said over the remade screen shown, in sentence
    /// case; OK runs <paramref name="then"/>. False when no remade screen shows.
    /// </summary>
    bool Message(std::string_view text, std::function<void()> then = {});

    /// <summary>
    /// An old message that asks for a key (the LIGHTS key's) said over the remade screen shown: <paramref name="take"/>
    /// offered each key (MCRmlDialog::MCSay::TakesKey); OK, Escape or a key taken runs <paramref name="then"/>. False
    /// when no remade screen shows.
    /// </summary>
    bool AskKey(std::string_view text, std::function<std::optional<std::string>(uint32_t key)> take,
                std::function<void()> then);

    /// <summary>A wait with no way out (ShowWaitingDialog: "Opening your room again...") over the remade screen shown.</summary>
    bool Waiting(std::string_view text);

    /// <summary>The 1998 dialog's capitals in sentence case: "BOB CLOSED THE GAME." to "Bob closed the game.".</summary>
    std::string SentenceCase(std::string_view text);

    /// <summary>Text in the 1998 dialog's capitals (no small letter) in sentence case; a sentence already, as it is.</summary>
    std::string AsSentence(std::string_view text);
}
